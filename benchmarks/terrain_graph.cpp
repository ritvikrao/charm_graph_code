// Copernicus DEM GLO-90 or GLO-30 terrain as an 8-neighbour cost-distance
// graph, in the wide GAPBS .wsg layout (int64 ids; {int64 v; int32 w; 4 bytes
// pad} per edge, graphlib/gapbs.h), optionally narrow, optionally also (or
// only) as a Galois .gr.
//
//   terrain_graph TILEDIR LAT0 LAT1 LON0 LON1 SEED_LAT SEED_LON SOURCES OUT.wsg
//       [--arcsec 3|1] [--count] [--narrow] [--gr OUT.gr] [--part K/P]
//
// TILEDIR holds one raw little-endian float32 T x T array per 1-degree tile
// (gdal_translate -of ENVI of the Copernicus COG), named like the COG with
// .f32. --arcsec 3 (the default) reads GLO-90: T = 1200,
// Copernicus_DSM_COG_30_N45_00_E006_00_DEM.f32 covers pixel centres at
// latitudes 45 + 1/1200 .. 46 and longitudes 6 .. 7 - 1/1200. --arcsec 1 reads
// GLO-30: T = 3600, Copernicus_DSM_COG_10_... . The region is the tiles with
// south edge in [LAT0, LAT1) and west edge in [LON0, LON1); every such tile is
// T x T on one uniform grid only within [-50, 50).
//
// Options. --count stops after the component and writes nothing. --narrow
// writes int32 ids ({int32 v; int32 w} per edge) and a version-1 .gr; it needs
// fewer than 2^31 vertices. --gr also writes the Galois .gr that
// benchmarks/to_galois.py would make from the .wsg (version 2 for wide ids,
// version 1 for narrow); OUT.wsg "-" writes only the .gr. --part K/P writes
// only the blocks whose first edge falls in the K-th of P equal edge ranges,
// into existing or new files without truncating them, so P jobs can write one
// graph; part 0 also writes the headers and the final offset.
//
// Vertices. A cell is land when its height is not exactly 0 (Copernicus writes
// the ocean as 0; land below sea level keeps its negative height) and its tile
// exists (tiles are only published where there is land). Only the 8-connected
// component of land containing the cell nearest (SEED_LAT, SEED_LON) is kept,
// as the OSM roads keep their largest component. Ids follow the Morton order of
// (row, column) over the region's raster, row bit b at 2b and column bit b at
// 2b+1 (as prepare_graph meshz), compacted to the kept cells.
//
// Edges. Each kept cell links to its kept 8-neighbours (both directions; the
// .wsg is the canonical undirected CSR, neighbours sorted by id). The weight is
// walking time by Tobler's hiking function, v(s) = 6 exp(-3.5 |s + 0.05|) km/h
// on slope s = dh / d over the horizontal distance d between cell centres
// (spherical Earth, R = 6371008.8 m; east-west spacing scaled by the cosine of
// the mean latitude), averaged over the two directions so the graph stays
// undirected: t = d (1/v(s) + 1/v(-s)) / 2. Weights are deciseconds, at least
// 1, capped at 36000 (an hour per cell: slopes past ~50 degrees).
//
// Sources: SOURCES of them, drawn as reference_gap draws them
// (bench::mix(20260913 + trial) % n; every kept vertex has an edge), printed
// with the first two marked tune. The output is written with pwrite from every
// thread; stripe the directory widely first (lfs setstripe).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#include <omp.h>
#include "common.h"

namespace {

int T = 1200;                     // cells per tile side (--arcsec)
const char *TILE_PREFIX = "30";   // Copernicus_DSM_COG_<prefix>_: 30 for GLO-90, 10 for GLO-30
constexpr int B = 64;             // Morton block side
constexpr double RADIUS = 6371008.8;
constexpr int MAX_WEIGHT = 36000;

uint64_t spread(uint64_t x) {     // bit b -> bit 2b (x < 2^32)
  x &= 0xffffffffULL;
  x = (x | (x << 16)) & 0x0000FFFF0000FFFFULL;
  x = (x | (x << 8)) & 0x00FF00FF00FF00FFULL;
  x = (x | (x << 4)) & 0x0F0F0F0F0F0F0F0FULL;
  x = (x | (x << 2)) & 0x3333333333333333ULL;
  x = (x | (x << 1)) & 0x5555555555555555ULL;
  return x;
}
uint64_t morton(uint64_t row, uint64_t col) { return spread(row) | (spread(col) << 1); }

std::string tile_name(int lat, int lon) {
  char s[96];
  snprintf(s, sizeof s, "Copernicus_DSM_COG_%s_%c%02d_00_%c%03d_00_DEM.f32", TILE_PREFIX,
           lat < 0 ? 'S' : 'N', std::abs(lat), lon < 0 ? 'W' : 'E', std::abs(lon));
  return s;
}

struct Raster {
  int lat0, lat1, lon0, lon1, trows, tcols;
  int64_t rows, cols;
  std::vector<std::vector<float>> tiles;  // empty: no tile (ocean)
  // Row 0 is the northernmost row (latitude lat1), column 0 is lon0.
  const float *tile(int64_t r, int64_t c) const {
    const auto &t = tiles[(r / T) * tcols + c / T];
    return t.empty() ? nullptr : t.data();
  }
  bool land(int64_t r, int64_t c) const {
    if (r < 0 || r >= rows || c < 0 || c >= cols) return false;
    const float *t = tile(r, c);
    return t && t[(r % T) * T + c % T] != 0.0f;
  }
  float height(int64_t r, int64_t c) const { return tile(r, c)[(r % T) * T + c % T]; }
  double latitude(int64_t r) const { return lat1 - double(r) / T; }
};

const int DR[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
const int DC[8] = {-1, 0, 1, -1, 1, -1, 0, 1};

double tobler_kmh(double s) { return 6.0 * std::exp(-3.5 * std::fabs(s + 0.05)); }

int32_t weight(const Raster &g, int64_t r, int64_t c, int64_t r2, int64_t c2) {
  const double dy = RADIUS * M_PI / 180.0 / T;
  const double lat = (g.latitude(r) + g.latitude(r2)) / 2 * M_PI / 180.0;
  const double dx = dy * std::cos(lat);
  const double d = std::sqrt(double((r2 - r) * (r2 - r)) * dy * dy + double((c2 - c) * (c2 - c)) * dx * dx);
  const double s = (double(g.height(r2, c2)) - double(g.height(r, c))) / d;
  const double seconds = d * (1.0 / tobler_kmh(s) + 1.0 / tobler_kmh(-s)) / 2 * 3.6;  // km/h -> m/s
  const double ds = std::llround(seconds * 10);
  return int32_t(std::min<double>(MAX_WEIGHT, std::max<double>(1, ds)));
}

void pwrite_all(int fd, const void *data, size_t bytes, off_t at) {
  const char *p = static_cast<const char *>(data);
  while (bytes) {
    const ssize_t k = pwrite(fd, p, bytes, at);
    if (k <= 0) throw std::runtime_error("pwrite failed");
    p += k; bytes -= size_t(k); at += k;
  }
}

}  // namespace

int main(int argc, char **argv) try {
  if (argc < 10)
    throw std::runtime_error("usage: terrain_graph TILEDIR LAT0 LAT1 LON0 LON1 SEED_LAT SEED_LON SOURCES OUT.wsg"
                             " [--arcsec 3|1] [--count] [--narrow] [--gr OUT.gr] [--part K/P]");
  const std::string dir = argv[1], out = argv[9];
  bool count_only = false, narrow = false;
  std::string gr_out;
  int part = 0, parts = 1;
  for (int i = 10; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error(a + " needs a value");
      return argv[++i];
    };
    if (a == "--arcsec") {
      const std::string v = value();
      if (v == "3") { T = 1200; TILE_PREFIX = "30"; }
      else if (v == "1") { T = 3600; TILE_PREFIX = "10"; }
      else throw std::runtime_error("--arcsec must be 3 or 1");
    } else if (a == "--count") count_only = true;
    else if (a == "--narrow") narrow = true;
    else if (a == "--gr") gr_out = value();
    else if (a == "--part") {
      const std::string v = value();
      const size_t slash = v.find('/');
      if (slash == std::string::npos) throw std::runtime_error("--part needs K/P");
      part = std::stoi(v.substr(0, slash)); parts = std::stoi(v.substr(slash + 1));
      if (parts < 1 || part < 0 || part >= parts) throw std::runtime_error("--part needs 0 <= K < P");
    } else throw std::runtime_error("unknown option " + a);
  }
  if (out == "-" && gr_out.empty() && !count_only) throw std::runtime_error("nothing to write");
  Raster g;
  g.lat0 = std::stoi(argv[2]); g.lat1 = std::stoi(argv[3]);
  g.lon0 = std::stoi(argv[4]); g.lon1 = std::stoi(argv[5]);
  const double seed_lat = std::stod(argv[6]), seed_lon = std::stod(argv[7]);
  const int source_count = std::stoi(argv[8]);
  if (g.lat0 < -50 || g.lat1 > 50 || g.lat0 >= g.lat1 || g.lon0 >= g.lon1 || g.lon0 < -180 || g.lon1 > 180)
    throw std::runtime_error("region must lie in latitudes [-50, 50) (uniform 3\" tiles)");
  g.trows = g.lat1 - g.lat0; g.tcols = g.lon1 - g.lon0;
  g.rows = int64_t(g.trows) * T; g.cols = int64_t(g.tcols) * T;
  g.tiles.resize(size_t(g.trows) * g.tcols);
  const double t0 = omp_get_wtime();

  // 1. Tiles. Tile row tr has south edge lat1 - 1 - tr.
  std::atomic<long> present{0}, land_cells{0};
#pragma omp parallel for schedule(dynamic, 1)
  for (int i = 0; i < g.trows * g.tcols; ++i) {
    const int lat = g.lat1 - 1 - i / g.tcols, lon = g.lon0 + i % g.tcols;
    FILE *f = fopen((dir + "/" + tile_name(lat, lon)).c_str(), "rb");
    if (!f) continue;
    std::vector<float> t(size_t(T) * T);
    const size_t got = fread(t.data(), 4, t.size(), f);
    const bool extra = fgetc(f) != EOF;
    fclose(f);
    if (got != t.size() || extra)
      throw std::runtime_error("tile is not " + std::to_string(T) + " x " + std::to_string(T) + " float32: " + tile_name(lat, lon));
    long k = 0;
    for (float h : t) k += h != 0.0f && std::isfinite(h);
    for (float h : t) if (!std::isfinite(h)) throw std::runtime_error("non-finite height in " + tile_name(lat, lon));
    land_cells += k; ++present;
    g.tiles[i] = std::move(t);
  }
  printf("tiles=%ld land_cells=%ld raster=%lld x %lld (%.1f s)\n", present.load(), land_cells.load(),
         (long long)g.rows, (long long)g.cols, omp_get_wtime() - t0);
  fflush(stdout);

  // 2. The component of the seed: nearest land cell by raster distance.
  const int64_t sr0 = std::llround((g.lat1 - seed_lat) * T), sc0 = std::llround((seed_lon - g.lon0) * T);
  int64_t seed = -1;
  for (int64_t rad = 0; seed < 0 && rad < 12000; ++rad)
    for (int64_t dr = -rad; dr <= rad && seed < 0; ++dr)
      for (int64_t dc = -rad; dc <= rad && seed < 0; ++dc)
        if (std::max(std::llabs(dr), std::llabs(dc)) == rad && g.land(sr0 + dr, sc0 + dc))
          seed = (sr0 + dr) * g.cols + sc0 + dc;
  if (seed < 0) throw std::runtime_error("no land near the seed");
  const uint64_t cells = uint64_t(g.rows) * g.cols;
  std::vector<std::atomic<uint64_t>> kept((cells + 63) / 64);
  for (auto &w : kept) w.store(0, std::memory_order_relaxed);
  auto claim = [&](uint64_t i) {
    const uint64_t bit = 1ULL << (i & 63);
    return !(kept[i >> 6].fetch_or(bit, std::memory_order_relaxed) & bit);
  };
  auto is_kept = [&](int64_t r, int64_t c) {
    if (r < 0 || r >= g.rows || c < 0 || c >= g.cols) return false;
    const uint64_t i = uint64_t(r) * g.cols + c;
    return bool(kept[i >> 6].load(std::memory_order_relaxed) >> (i & 63) & 1);
  };
  claim(seed);
  std::vector<uint64_t> frontier{uint64_t(seed)}, next;
  long levels = 0, component = 1;
  const int threads = omp_get_max_threads();
  std::vector<std::vector<uint64_t>> local(threads);
  while (!frontier.empty()) {
#pragma omp parallel
    {
      auto &mine = local[omp_get_thread_num()];
      mine.clear();
#pragma omp for schedule(dynamic, 4096)
      for (size_t k = 0; k < frontier.size(); ++k) {
        const int64_t r = frontier[k] / g.cols, c = frontier[k] % g.cols;
        for (int d = 0; d < 8; ++d) {
          const int64_t r2 = r + DR[d], c2 = c + DC[d];
          if (g.land(r2, c2) && claim(uint64_t(r2) * g.cols + c2)) mine.push_back(uint64_t(r2) * g.cols + c2);
        }
      }
    }
    next.clear();
    for (auto &m : local) next.insert(next.end(), m.begin(), m.end());
    component += next.size();
    frontier.swap(next);
    ++levels;
  }
  printf("component=%ld of %ld land cells, BFS levels=%ld, seed row=%lld col=%lld (%.1f s)\n", component,
         land_cells.load(), levels, (long long)(seed / g.cols), (long long)(seed % g.cols), omp_get_wtime() - t0);
  fflush(stdout);
  if (count_only) return 0;
  if (narrow && component >= (1L << 31)) throw std::runtime_error("--narrow needs fewer than 2^31 vertices");

  // 3. Morton blocks: kept-cell bitmaps in in-block Morton order, blocks in
  // Morton order of (block row, block column); ids are block start + rank.
  const int64_t brows = (g.rows + B - 1) / B, bcols = (g.cols + B - 1) / B, blocks = brows * bcols;
  std::vector<uint64_t> bits(size_t(blocks) * 64, 0);
  std::vector<uint16_t> prefix(size_t(blocks) * 64, 0);
  std::vector<int64_t> count(blocks, 0), edge_count(blocks, 0);
#pragma omp parallel for schedule(dynamic, 64)
  for (int64_t b = 0; b < blocks; ++b) {
    const int64_t r0 = (b / bcols) * B, c0 = (b % bcols) * B;
    uint64_t *w = &bits[size_t(b) * 64];
    int64_t edges = 0;
    for (int lr = 0; lr < B; ++lr)
      for (int lc = 0; lc < B; ++lc)
        if (is_kept(r0 + lr, c0 + lc)) {
          const uint64_t m = morton(lr, lc);
          w[m >> 6] |= 1ULL << (m & 63);
          for (int d = 0; d < 8; ++d) edges += is_kept(r0 + lr + DR[d], c0 + lc + DC[d]);
        }
    int64_t k = 0;
    for (int i = 0; i < 64; ++i) { prefix[size_t(b) * 64 + i] = uint16_t(k); k += __builtin_popcountll(w[i]); }
    count[b] = k; edge_count[b] = edges;
  }
  std::vector<int64_t> order(blocks);
  for (int64_t b = 0; b < blocks; ++b) order[b] = b;
  std::sort(order.begin(), order.end(), [&](int64_t a, int64_t b) {
    return morton(a / bcols, a % bcols) < morton(b / bcols, b % bcols);
  });
  std::vector<int64_t> vstart(blocks), estart(blocks);
  int64_t n = 0, m = 0;
  for (int64_t b : order) { vstart[b] = n; estart[b] = m; n += count[b]; m += edge_count[b]; }
  if (n != component) throw std::runtime_error("block counts differ from the component size");
  auto id = [&](int64_t r, int64_t c) {
    const int64_t b = (r / B) * bcols + c / B;
    const uint64_t k = morton(r % B, c % B);
    const uint64_t w = bits[size_t(b) * 64 + (k >> 6)];
    return vstart[b] + prefix[size_t(b) * 64 + (k >> 6)] + __builtin_popcountll(w & ((1ULL << (k & 63)) - 1));
  };
  printf("vertices=%lld directed_edges=%lld (%.1f s)\n", (long long)n, (long long)m, omp_get_wtime() - t0);
  fflush(stdout);

  // 4. The files. Each block writes its own slice of every array: the .wsg
  // header, n + 1 offsets and m edges (16 bytes wide, 8 narrow); the .gr
  // header, n end offsets, m destinations (8 bytes in version 2, 4 in version
  // 1, then 4 bytes of padding when m is odd) and m weights.
  const int flags = O_WRONLY | O_CREAT | (parts == 1 ? O_TRUNC : 0);
  const int fd = out == "-" ? -1 : open(out.c_str(), flags, 0644);
  if (out != "-" && fd < 0) throw std::runtime_error("cannot open " + out);
  const int gfd = gr_out.empty() ? -1 : open(gr_out.c_str(), flags, 0644);
  if (!gr_out.empty() && gfd < 0) throw std::runtime_error("cannot open " + gr_out);
  const int64_t erec = narrow ? 8 : 16, gdst = narrow ? 4 : 8;
  const off_t offsets_at = 17, edges_at = 17 + (n + 1) * 8;
  const off_t g_dst_at = 32 + n * 8, g_w_at = g_dst_at + m * gdst + (narrow && (m & 1) ? 4 : 0);
  if (part == 0) {
    if (fd >= 0) {
      char head[17]; head[0] = 0;
      std::memcpy(head + 1, &m, 8); std::memcpy(head + 9, &n, 8);
      pwrite_all(fd, head, 17, 0);
      pwrite_all(fd, &m, 8, offsets_at + n * 8);
    }
    if (gfd >= 0) {
      const uint64_t head[4] = {narrow ? 1ULL : 2ULL, 4, uint64_t(n), uint64_t(m)};
      pwrite_all(gfd, head, 32, 0);
      if (narrow && (m & 1)) { const uint32_t zero = 0; pwrite_all(gfd, &zero, 4, g_dst_at + m * 4); }
    }
  }
  const int64_t lo = m / parts * part + std::min<int64_t>(part, m % parts);
  const int64_t hi = m / parts * (part + 1) + std::min<int64_t>(part + 1, m % parts);
  std::atomic<int32_t> max_weight{0};
  std::atomic<long> capped{0}, part_blocks{0}, part_edges{0};
#pragma omp parallel
  {
    std::vector<int64_t> off, ends;
    std::vector<char> rec, dst;
    std::vector<int32_t> wts;
    std::vector<std::pair<int64_t, int32_t>> nb;
    int32_t my_max = 0; long my_capped = 0, my_blocks = 0, my_edges = 0;
#pragma omp for schedule(dynamic, 16)
    for (int64_t b = 0; b < blocks; ++b) {
      if (!count[b] || estart[b] < lo || estart[b] >= hi) continue;
      const int64_t r0 = (b / bcols) * B, c0 = (b % bcols) * B;
      off.clear(); ends.clear(); rec.clear(); dst.clear(); wts.clear();
      int64_t at = estart[b];
      for (uint64_t k = 0; k < uint64_t(B) * B; ++k) {        // in-block Morton order
        if (!(bits[size_t(b) * 64 + (k >> 6)] >> (k & 63) & 1)) continue;
        uint64_t rr = 0, cc = 0;
        for (int i = 0; i < 6; ++i) { rr |= (k >> (2 * i) & 1) << i; cc |= (k >> (2 * i + 1) & 1) << i; }
        const int64_t r = r0 + rr, c = c0 + cc;
        nb.clear();
        for (int d = 0; d < 8; ++d) {
          const int64_t r2 = r + DR[d], c2 = c + DC[d];
          if (!is_kept(r2, c2)) continue;
          const int32_t w = weight(g, r, c, r2, c2);
          my_max = std::max(my_max, w); my_capped += w == MAX_WEIGHT;
          nb.emplace_back(id(r2, c2), w);
        }
        std::sort(nb.begin(), nb.end());
        off.push_back(at);
        for (auto &[v, w] : nb) {
          if (fd >= 0) {
            char e[16] = {};
            if (narrow) { const int32_t v32 = int32_t(v); std::memcpy(e, &v32, 4); std::memcpy(e + 4, &w, 4); }
            else { std::memcpy(e, &v, 8); std::memcpy(e + 8, &w, 4); }
            rec.insert(rec.end(), e, e + erec);
          }
          if (gfd >= 0) {
            if (narrow) { const uint32_t v32 = uint32_t(v); dst.insert(dst.end(), (char *)&v32, (char *)&v32 + 4); }
            else { const uint64_t v64 = uint64_t(v); dst.insert(dst.end(), (char *)&v64, (char *)&v64 + 8); }
            wts.push_back(w);
          }
        }
        at += int64_t(nb.size());
        ends.push_back(at);
      }
      if (at != estart[b] + edge_count[b]) throw std::runtime_error("edge count mismatch");
      if (fd >= 0) {
        pwrite_all(fd, off.data(), off.size() * 8, offsets_at + vstart[b] * 8);
        pwrite_all(fd, rec.data(), rec.size(), edges_at + estart[b] * erec);
      }
      if (gfd >= 0) {
        pwrite_all(gfd, ends.data(), ends.size() * 8, 32 + vstart[b] * 8);
        pwrite_all(gfd, dst.data(), dst.size(), g_dst_at + estart[b] * gdst);
        pwrite_all(gfd, wts.data(), wts.size() * 4, g_w_at + estart[b] * 4);
      }
      ++my_blocks; my_edges += edge_count[b];
    }
    int32_t cur = max_weight.load();
    while (my_max > cur && !max_weight.compare_exchange_weak(cur, my_max)) {}
    capped += my_capped; part_blocks += my_blocks; part_edges += my_edges;
  }
  if (fd >= 0 && (fsync(fd) || close(fd))) throw std::runtime_error("close failed");
  if (gfd >= 0 && (fsync(gfd) || close(gfd))) throw std::runtime_error("close failed");
  const long long wsg_bytes = edges_at + m * erec, gr_bytes = g_w_at + m * 4;
  if (parts > 1) {
    // A part sees only its own weights; the job that joins the parts takes
    // the largest max_weight and sums capped_edges and part_edges.
    printf("part=%d/%d blocks=%ld part_edges=%ld max_weight=%d capped_edges=%ld wsg_bytes=%lld gr_bytes=%lld (%.1f s)\n",
           part, parts, part_blocks.load(), part_edges.load(), max_weight.load(), capped.load(), wsg_bytes, gr_bytes,
           omp_get_wtime() - t0);
  } else {
    int64_t denominator = 1;
    while (denominator < max_weight) denominator *= 2;
    printf("vertices=%lld arcs=%lld max_weight=%d riken_denominator=%lld\n", (long long)n, (long long)m,
           max_weight.load(), (long long)denominator);
    printf("capped_edges=%ld bytes=%lld gr_bytes=%lld (%.1f s)\n", capped.load(), fd >= 0 ? wsg_bytes : 0LL,
           gfd >= 0 ? gr_bytes : 0LL, omp_get_wtime() - t0);
  }

  // 5. Sources, as reference_gap draws them.
  std::set<int64_t> used;
  printf("sources");
  for (int index = 0; index < source_count; ++index) {
    uint64_t trial = index;
    int64_t s;
    do { s = int64_t(bench::mix(20260913 + trial++) % uint64_t(n)); } while (used.count(s));
    used.insert(s);
    printf(" %lld", (long long)s);
  }
  printf("\n");
  return 0;
} catch (const std::exception &e) {
  fprintf(stderr, "terrain_graph: %s\n", e.what());
  return 1;
}
