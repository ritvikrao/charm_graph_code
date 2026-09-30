#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "gapbs.h"

/**
 * How local a vertex order is at a given tile size: the fraction of edges
 * (u, v) with u / tile == v / tile, estimated from a sample of the file.
 *
 * --reader-tile auto deals tiles of V / (64 x owners) vertices round-robin.
 * On a Morton-ordered mesh a tile is a compact block and nearly all its edges
 * stay inside it; on the same mesh in row-major order a tile of one row keeps
 * only the horizontal edges, every vertical edge crosses to another owner, and
 * the solve is 107-183x slower (current-state section 33). This measures that
 * difference at load time so the tile size can follow the order.
 *
 * The sample is `runs` runs of `run_length` consecutive vertices at
 * pseudo-random positions (a fixed seed, so the choice is reproducible). Each
 * run reads one span of the offsets array and one of the neighbour array, at
 * most `run_edges` edges; runs are spread over threads because a random read
 * on Lustre costs about 20 ms.
 */
struct TileLocality {
  std::vector<long> tiles;     // tile sizes measured, as given
  std::vector<double> inside;  // per tile size: fraction of sampled edges inside a tile
  int64_t edges = 0;           // edges sampled
};

inline TileLocality measure_tile_locality(const std::string &path, const GapbsHeader &h,
                                          const std::vector<long> &tiles, int runs = 256,
                                          long run_length = 256, long run_edges = 1L << 16,
                                          int threads = 64) {
  TileLocality result;
  result.tiles = tiles;
  result.inside.assign(tiles.size(), 0.0);
  const long V = (long)h.num_nodes;
  if (V < 2 || tiles.empty()) return result;
  run_length = std::min(run_length, V);
  std::vector<std::vector<int64_t>> counts((size_t)runs, std::vector<int64_t>(tiles.size() + 1, 0));
  auto one_run = [&](int fd, int r) {
    uint64_t x = 0x9e3779b97f4a7c15ULL * (uint64_t)(r + 1);  // splitmix64
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= x >> 31;
    const long first = (long)(x % (uint64_t)(V - run_length + 1));
    std::vector<int64_t> offsets((size_t)run_length + 1);
    if (::pread(fd, offsets.data(), offsets.size() * 8, (off_t)(h.offsets_at() + first * 8)) !=
        (ssize_t)(offsets.size() * 8))
      GRAPHLIB_ABORT("graphlib: short read of the offsets array in %s", path.c_str());
    const int64_t begin = offsets.front();
    const int64_t count = std::min<int64_t>(offsets.back() - begin, run_edges);
    const size_t record = (size_t)h.dest_size();
    std::vector<unsigned char> raw((size_t)count * record);
    size_t done = 0;
    while (done < raw.size()) {
      const ssize_t got = ::pread(fd, raw.data() + done, raw.size() - done,
                                  (off_t)(h.neighbours_at() + begin * (int64_t)record + (int64_t)done));
      if (got <= 0) GRAPHLIB_ABORT("graphlib: short read of the neighbour array in %s", path.c_str());
      done += (size_t)got;
    }
    std::vector<int64_t> &c = counts[(size_t)r];
    long u = first;
    for (int64_t i = 0; i < count; ++i) {
      while (offsets[(size_t)(u - first + 1)] <= begin + i) ++u;
      int64_t v;
      if (h.wide) {
        std::memcpy(&v, raw.data() + (size_t)i * record, 8);
      } else {
        int32_t v32;
        std::memcpy(&v32, raw.data() + (size_t)i * record, 4);
        v = v32;
      }
      for (size_t t = 0; t < tiles.size(); ++t)
        c[t] += tiles[t] > 0 && u / tiles[t] == v / tiles[t];
      ++c.back();
    }
  };
  std::atomic<int> next(0);
  auto work = [&]() {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) GRAPHLIB_ABORT("graphlib: cannot open %s", path.c_str());
    for (int r; (r = next++) < runs;) one_run(fd, r);
    ::close(fd);
  };
  std::vector<std::thread> pool;
  for (int i = 1; i < std::min(threads, runs); ++i) pool.emplace_back(work);
  work();
  for (auto &t : pool) t.join();
  std::vector<int64_t> total(tiles.size() + 1, 0);
  for (const auto &c : counts)
    for (size_t t = 0; t < c.size(); ++t) total[t] += c[t];
  result.edges = total.back();
  for (size_t t = 0; t < tiles.size(); ++t)
    result.inside[t] = result.edges ? (double)total[t] / (double)result.edges : 0.0;
  return result;
}

/**
 * The tile size for --reader-tile locality. The ladder is the auto size
 * V / (64 x owners) and its 4x and 16x multiples; the untiled layout (one
 * block of V / owners per owner) is measured too. The rule takes the smallest
 * rung that keeps at least min(target, untiled - slack) of the sampled edges
 * inside a tile:
 *
 * - An order with locality at every scale (Morton meshes, grids, terrain and
 *   roads: 0.953 or more at the auto size for 8-512 owners) keeps the auto
 *   size.
 * - An order whose locality only exists at the scale of a whole block
 *   (row-major mesh26: 0.50 at a one-row tile, 0.98 untiled) gets larger
 *   tiles, or none: a rung that fails the target means tiling would cut edges
 *   the untiled layout keeps, so the rule returns 0 (off). O1b: tiling off is
 *   68-123x faster there.
 * - An order with no locality at any scale (DIMACS road-usa: 0.33-0.40 at
 *   every size, untiled included) keeps the auto size, because tiling cuts
 *   nothing the untiled layout would keep and still spreads the front. O1b:
 *   tiling off is 2.1-2.4x slower there.
 *
 * `measured` receives the ladder with the untiled size last.
 */
inline long choose_tile_by_locality(const std::string &path, const GapbsHeader &h, int owners,
                                    double target, TileLocality *measured = nullptr,
                                    double slack = 0.05) {
  const long V = (long)h.num_nodes;
  std::vector<long> ladder;
  for (long tile = std::max(1L, V / (64L * owners)); tile <= V / (2L * owners); tile *= 4)
    ladder.push_back(tile);
  const long block = std::max(1L, V / owners);
  std::vector<long> sizes = ladder;
  sizes.push_back(block);
  const TileLocality m = measure_tile_locality(path, h, sizes);
  if (measured) *measured = m;
  const double need = std::min(target, m.inside.back() - slack);
  for (size_t t = 0; t < ladder.size(); ++t)
    if (m.inside[t] >= need) return ladder[t];
  return 0;
}
