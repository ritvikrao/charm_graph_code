#ifndef GRAPHLIB_EDGE_PARTITION_H
#define GRAPHLIB_EDGE_PARTITION_H

/**
 * The equal-edge partition of a GAPBS file, computed from the file's offsets
 * array without holding it.
 *
 * PE 0 used to read all V+1 offsets into memory and, with reader tiles on, build
 * a second, tiled copy: 2 x 275 GB for terrain30-l-z (34.3B vertices), which
 * killed task 0 on a 512 GB node (job 5551488). The partition only needs
 * offsets at the PE boundaries, so each boundary is found by binary search over
 * offsets read on demand: O(PEs log V) reads instead of V, spread over threads
 * where the owners of a tiled layout make them independent.
 *
 * partition_by_edges gives exactly the boundaries of the linear scan it
 * replaced (tests/test_edge_partition.cpp checks the two against each other).
 */

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

#include "gapbs.h"
#include "tile_layout.h"

/**
 * offsets[v] of a GAPBS file, read with pread in blocks of `block` entries.
 * The last four blocks are kept: a search's probes land close together.
 * One-at-a-time probes are latency-bound on Lustre (about 20 ms per uncached
 * read, job 5558176), so random probes use small blocks and are spread over
 * threads (partition_tiled), and forward scans use large ones.
 */
class FileOffsets {
  static constexpr int WAYS = 4;
  int fd_ = -1;
  int64_t at_ = 0, entries_ = 0, block_size_;
  std::string path_;
  mutable int64_t tag_[WAYS] = {-1, -1, -1, -1};
  mutable std::vector<int64_t> data_[WAYS];
  mutable int next_ = 0;

public:
  FileOffsets(const std::string &path, const GapbsHeader &h, int64_t block = 8192)
      : at_(h.offsets_at()), entries_(h.num_nodes + 1), block_size_(block), path_(path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0)
      GRAPHLIB_ABORT("graphlib: cannot open %s", path.c_str());
  }
  FileOffsets(const FileOffsets &) = delete;
  FileOffsets &operator=(const FileOffsets &) = delete;
  ~FileOffsets() {
    if (fd_ >= 0) ::close(fd_);
  }
  int64_t operator()(int64_t v) const {
    const int64_t block = v / block_size_;
    for (int w = 0; w < WAYS; ++w)
      if (tag_[w] == block) return data_[w][(size_t)(v - block * block_size_)];
    const int w = next_;
    next_ = (next_ + 1) % WAYS;
    const int64_t first = block * block_size_;
    const int64_t count = std::min(block_size_, entries_ - first);
    data_[w].resize((size_t)count);
    const size_t bytes = (size_t)count * 8;
    size_t done = 0;
    while (done < bytes) {
      const ssize_t got = ::pread(fd_, (char *)data_[w].data() + done, bytes - done,
                                  (off_t)(at_ + first * 8 + (int64_t)done));
      if (got <= 0)
        GRAPHLIB_ABORT("graphlib: short read of the offsets array in %s", path_.c_str());
      done += (size_t)got;
    }
    tag_[w] = block;
    return data_[w][(size_t)(v - first)];
  }
};

/**
 * Run fn(accessor, k) for k in [0, count) on up to `threads` threads, each
 * with its own accessor from make() (FileOffsets is not shareable: it caches).
 */
template <typename Make, typename Fn>
inline void parallel_with(long count, int threads, const Make &make, const Fn &fn) {
  threads = (int)std::max(1L, std::min((long)threads, count));
  std::atomic<long> next(0);
  auto work = [&]() {
    auto off = make();
    for (long k; (k = next++) < count;) fn(*off, k);
  };
  std::vector<std::thread> pool;
  for (int i = 1; i < threads; ++i) pool.emplace_back(work);
  work();
  for (auto &t : pool) t.join();
}

/**
 * Per-tile entries for TiledOffsets: tiling moves whole tiles, so a tiled
 * offset is the tile's tiled start plus the vertex's offset within its tile.
 * One entry per tile (about 64 x owners with --reader-tile auto) instead of a
 * permuted copy of the whole array.
 */
struct TileTable {
  long vertices = 0, tile = 1;
  int64_t total = 0;
  std::vector<int64_t> first; // per original tile: file offset of its first vertex
  std::vector<int64_t> base;  // per original tile: tiled offset of its first vertex

  template <typename Make>
  TileTable(long vertices_, long tile_, int owners, int threads, const Make &make)
      : vertices(vertices_), tile(tile_) {
    const long tiles = vertices / tile + (vertices % tile != 0);
    first.resize((size_t)tiles);
    base.resize((size_t)tiles);
    total = (*make())(vertices);
    parallel_with(tiles, threads, make, [&](const auto &off, long t) { first[(size_t)t] = off(t * tile); });
    // TileLayout gives owner o the original tiles o, o + owners, ... in order,
    // and lays the owners out one after another.
    int64_t running = 0;
    for (int owner = 0; owner < owners; ++owner)
      for (long t = owner; t < tiles; t += owners) {
        base[(size_t)t] = running;
        running += (t + 1 < tiles ? first[(size_t)t + 1] : total) - first[(size_t)t];
      }
  }
};

/** Offsets in the reader's tiled vertex order, through a TileTable. */
template <typename Offsets>
class TiledOffsets {
  const Offsets &off_;
  const TileLayout &layout_;
  const TileTable &table_;

public:
  TiledOffsets(const Offsets &off, const TileLayout &layout, const TileTable &table)
      : off_(off), layout_(layout), table_(table) {}
  int64_t operator()(long v) const {
    if (v == table_.vertices) return table_.total;
    const long original = layout_.original(v);
    const long t = original / table_.tile;
    return table_.base[(size_t)t] + off_(original) - table_.first[(size_t)t];
  }
};

/**
 * Split vertices [begin, end) over PEs [first_pe, last_pe) into contiguous
 * ranges of about equal edge count, writing partition_index[first_pe ..
 * last_pe]. Each PE takes vertices until its edges reach ceil(edges / PEs),
 * leaving at least one vertex for every PE after it (so an edgeless graph still
 * spreads out). `off(v)` is the offsets array, in any form that is
 * non-decreasing in v.
 *
 * Each boundary is a binary search from the previous one, galloping first so
 * the probes stay near the answer (and in the accessor's cached blocks).
 */
template <typename Offsets>
inline void partition_by_edges(const Offsets &off, int first_pe, int last_pe,
                               long begin, long end, long *partition_index) {
  const long pes = last_pe - first_pe;
  const long region_edges = (long)(off(end) - off(begin));
  const long per_pe = std::max(1L, (region_edges + pes - 1) / pes);
  long vertex = begin;
  for (int i = first_pe; i < last_pe; ++i) {
    partition_index[i] = vertex;
    const long cap = end - (last_pe - i - 1);
    if (vertex >= cap) continue;
    // The first v in [vertex, cap) with off(v) >= off(vertex) + per_pe, or cap.
    const int64_t target = off(vertex) + per_pe;
    long lo = vertex, hi = cap; // every v in [vertex, lo) is below target
    for (long step = 1; lo < hi; step *= 2) {
      const long probe = std::min(hi, lo + step) - 1;
      if (off(probe) < target) lo = probe + 1;
      else { hi = probe; break; }
    }
    while (lo < hi) {
      const long mid = lo + (hi - lo) / 2;
      if (off(mid) < target) lo = mid + 1;
      else hi = mid;
    }
    vertex = lo;
  }
  partition_index[last_pe] = end;
}

/**
 * The tiled partition: owner o's PEs [owner_pe[o], owner_pe[o + 1]) split
 * layout's owner region o. Owners are independent, so they run on up to
 * `threads` threads (each owner's boundaries are sequential).
 * partition_index[owner_pe[owners]] is the vertex count.
 */
template <typename Make>
inline void partition_tiled(const TileLayout &layout, const TileTable &table,
                            const std::vector<int> &owner_pe, int threads,
                            const Make &make, long *partition_index) {
  const int owners = (int)owner_pe.size() - 1;
  parallel_with(owners, threads, make, [&](const auto &off, long o) {
    using Accessor = typename std::decay<decltype(off)>::type;
    TiledOffsets<Accessor> tiled(off, layout, table);
    const int first = owner_pe[(size_t)o], last = owner_pe[(size_t)o + 1];
    // Owner o's last entry is owner o + 1's first: write it only once.
    std::vector<long> local((size_t)(last - first) + 1);
    partition_by_edges(tiled, 0, last - first, layout.owner_begin((int)o),
                       layout.owner_end((int)o), local.data());
    std::copy(local.begin(), local.end() - 1, partition_index + first);
  });
  partition_index[owner_pe.back()] = table.vertices;
}

#endif
