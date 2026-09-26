// Morton-ordered 2-D (4-neighbour) and 3-D (6-neighbour) grids, written in
// parallel: the synthetic inputs past one node (mesh32-z), the 3-D grids and
// the edge-weight-range variants.
//
//   grid_graph DIM N SEED MAX_WEIGHT SOURCES OUT.wsg [--wide] [--gr OUT.gr] [--part K/P]
//
// DIM 2 is exactly `prepare_graph meshz` (byte-identical for MAX_WEIGHT 1000;
// see its comment): side^2 = N, id = Morton(row, column) with row bit b at bit
// 2b and column bit b at 2b+1, and an undirected edge {u, v} keeps the lighter
// of the two generator weights of its row-major ids u, v. DIM 3 is the same
// rule on a side^3 = N cube: id = Morton(a, b, c) with a's bit b at 3b, b's at
// 3b+1 and c's at 3b+2, row-major id (a side + b) side + c, six neighbours.
// Weights: graphlib's WeightAssigner(SEED, uniform, MAX_WEIGHT), uniform over
// [1, MAX_WEIGHT] (the default graphs use 1000).
//
// Output: the canonical undirected .wsg, rows sorted by neighbour; narrow
// ({int32 v; int32 w}) below 2^31 vertices unless --wide, wide ({int64 v;
// int32 w; pad}) at or past it. --gr also writes the Galois .gr that
// benchmarks/to_galois.py would make (version 1 narrow, version 2 wide); OUT
// "-" writes only the .gr. --part K/P writes the K-th of P equal id ranges
// without truncating, so P jobs can write one graph; part 0 also writes the
// headers and the final offset. SOURCES are drawn as reference_gap draws them
// (bench::mix(20260913 + trial) % N) and printed after "sources".
#include <algorithm>
#include <atomic>
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
#include "graphlib/graphlib.h"

namespace {

int D = 2;          // dimensions
int64_t SIDE = 0;   // cells per side (a power of two)

uint64_t spread(uint64_t x) {   // bit b of x -> bit D*b
  uint64_t r = 0;
  for (int b = 0; b < 64 / D && (x >> b); ++b) r |= ((x >> b) & 1) << (D * b);
  return r;
}
uint64_t gather(uint64_t z) {   // bits 0, D, 2D, ... -> 0, 1, 2, ...
  uint64_t r = 0;
  for (int b = 0; b * D < 64; ++b) r |= ((z >> (D * b)) & 1) << b;
  return r;
}
uint64_t encode(const int64_t *x) {
  uint64_t z = 0;
  for (int d = 0; d < D; ++d) z |= spread(uint64_t(x[d])) << d;
  return z;
}
void decode(uint64_t z, int64_t *x) {
  for (int d = 0; d < D; ++d) x[d] = int64_t(gather(z >> d));
}
int64_t row_major(const int64_t *x) {
  int64_t u = 0;
  for (int d = 0; d < D; ++d) u = u * SIDE + x[d];
  return u;
}
int degree(uint64_t z) {
  int64_t x[3];
  decode(z, x);
  int k = 0;
  for (int d = 0; d < D; ++d) k += (x[d] > 0) + (x[d] < SIDE - 1);
  return k;
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
  if (argc < 7)
    throw std::runtime_error("usage: grid_graph DIM N SEED MAX_WEIGHT SOURCES OUT.wsg [--wide] [--gr OUT.gr] [--part K/P]");
  D = std::stoi(argv[1]);
  const int64_t n = std::stoll(argv[2]);
  const int seed = std::stoi(argv[3]);
  const long max_w = std::stol(argv[4]);
  const int source_count = std::stoi(argv[5]);
  const std::string out = argv[6];
  bool wide = n >= (int64_t(1) << 31);
  std::string gr_out;
  int part = 0, parts = 1;
  for (int i = 7; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error(a + " needs a value");
      return argv[++i];
    };
    if (a == "--wide") wide = true;
    else if (a == "--gr") gr_out = value();
    else if (a == "--part") {
      const std::string v = value();
      const size_t slash = v.find('/');
      if (slash == std::string::npos) throw std::runtime_error("--part needs K/P");
      part = std::stoi(v.substr(0, slash)); parts = std::stoi(v.substr(slash + 1));
      if (parts < 1 || part < 0 || part >= parts) throw std::runtime_error("--part needs 0 <= K < P");
    } else throw std::runtime_error("unknown option " + a);
  }
  if (D != 2 && D != 3) throw std::runtime_error("DIM must be 2 or 3");
  if (max_w < 1 || max_w > INT32_MAX) throw std::runtime_error("MAX_WEIGHT must be in [1, 2^31)");
  if (out == "-" && gr_out.empty()) throw std::runtime_error("nothing to write");
  SIDE = 1;
  while (true) {
    int64_t cells = 1;
    for (int d = 0; d < D; ++d) cells *= SIDE;
    if (cells == n) break;
    if (cells > n) throw std::runtime_error("N must be a power of two to the DIM-th power of a power of two");
    SIDE <<= 1;
  }
  const double t0 = omp_get_wtime();
  const WeightAssigner weight(seed, WEIGHT_UNIFORM, max_w);
  const int64_t chunk = std::min<int64_t>(n, int64_t(1) << 16);
  const int64_t chunks = n / chunk;
  // Directed edges: every cell has 2D neighbours minus one per face it lies on.
  const int64_t m = int64_t(2) * D * n - int64_t(2) * D * (n / SIDE);
  std::vector<int64_t> estart(chunks + 1, 0);
#pragma omp parallel for schedule(static)
  for (int64_t k = 0; k < chunks; ++k) {
    int64_t e = 0;
    for (int64_t z = k * chunk; z < (k + 1) * chunk; ++z) e += degree(uint64_t(z));
    estart[k + 1] = e;
  }
  for (int64_t k = 0; k < chunks; ++k) estart[k + 1] += estart[k];
  if (estart[chunks] != m) throw std::runtime_error("degree sum differs from 2 D (N - N / side)");

  const int flags = O_WRONLY | O_CREAT | (parts == 1 ? O_TRUNC : 0);
  const int fd = out == "-" ? -1 : open(out.c_str(), flags, 0644);
  if (out != "-" && fd < 0) throw std::runtime_error("cannot open " + out);
  const int gfd = gr_out.empty() ? -1 : open(gr_out.c_str(), flags, 0644);
  if (!gr_out.empty() && gfd < 0) throw std::runtime_error("cannot open " + gr_out);
  const int64_t erec = wide ? 16 : 8, gdst = wide ? 8 : 4;
  const off_t offsets_at = 17, edges_at = 17 + (n + 1) * 8;
  const off_t g_dst_at = 32 + n * 8, g_w_at = g_dst_at + m * gdst + (!wide && (m & 1) ? 4 : 0);
  if (part == 0) {
    if (fd >= 0) {
      char head[17]; head[0] = 0;
      std::memcpy(head + 1, &m, 8); std::memcpy(head + 9, &n, 8);
      pwrite_all(fd, head, 17, 0);
      pwrite_all(fd, &m, 8, offsets_at + n * 8);
    }
    if (gfd >= 0) {
      const uint64_t head[4] = {wide ? 2ULL : 1ULL, 4, uint64_t(n), uint64_t(m)};
      pwrite_all(gfd, head, 32, 0);
      if (!wide && (m & 1)) { const uint32_t zero = 0; pwrite_all(gfd, &zero, 4, g_dst_at + m * 4); }
    }
  }
  const int64_t k0 = chunks / parts * part + std::min<int64_t>(part, chunks % parts);
  const int64_t k1 = chunks / parts * (part + 1) + std::min<int64_t>(part + 1, chunks % parts);
  std::atomic<int64_t> max_weight{0};
#pragma omp parallel
  {
    std::vector<int64_t> off, ends;
    std::vector<char> rec, dst;
    std::vector<int32_t> wts;
    int64_t my_max = 0;
#pragma omp for schedule(dynamic, 4)
    for (int64_t k = k0; k < k1; ++k) {
      off.clear(); ends.clear(); rec.clear(); dst.clear(); wts.clear();
      int64_t at = estart[k];
      for (int64_t z = k * chunk; z < (k + 1) * chunk; ++z) {
        int64_t x[3], y[3];
        decode(uint64_t(z), x);
        const int64_t u = row_major(x);
        std::pair<int64_t, int32_t> nb[6];
        int c = 0;
        for (int d = 0; d < D; ++d)
          for (int step : {-1, 1}) {
            if ((step < 0 && x[d] == 0) || (step > 0 && x[d] == SIDE - 1)) continue;
            std::copy(x, x + D, y);
            y[d] += step;
            const int64_t v = row_major(y);
            const int64_t w = std::min(weight(u, v), weight(v, u));
            my_max = std::max(my_max, w);
            nb[c++] = {int64_t(encode(y)), int32_t(w)};
          }
        std::sort(nb, nb + c);
        off.push_back(at);
        for (int i = 0; i < c; ++i) {
          const int64_t v = nb[i].first;
          const int32_t w = nb[i].second;
          if (fd >= 0) {
            char e[16] = {};
            if (wide) { std::memcpy(e, &v, 8); std::memcpy(e + 8, &w, 4); }
            else { const int32_t v32 = int32_t(v); std::memcpy(e, &v32, 4); std::memcpy(e + 4, &w, 4); }
            rec.insert(rec.end(), e, e + erec);
          }
          if (gfd >= 0) {
            if (wide) { const uint64_t v64 = uint64_t(v); dst.insert(dst.end(), (char *)&v64, (char *)&v64 + 8); }
            else { const uint32_t v32 = uint32_t(v); dst.insert(dst.end(), (char *)&v32, (char *)&v32 + 4); }
            wts.push_back(w);
          }
        }
        at += c;
        ends.push_back(at);
      }
      if (at != estart[k + 1]) throw std::runtime_error("edge count mismatch");
      if (fd >= 0) {
        pwrite_all(fd, off.data(), off.size() * 8, offsets_at + k * chunk * 8);
        pwrite_all(fd, rec.data(), rec.size(), edges_at + estart[k] * erec);
      }
      if (gfd >= 0) {
        pwrite_all(gfd, ends.data(), ends.size() * 8, 32 + k * chunk * 8);
        pwrite_all(gfd, dst.data(), dst.size(), g_dst_at + estart[k] * gdst);
        pwrite_all(gfd, wts.data(), wts.size() * 4, g_w_at + estart[k] * 4);
      }
    }
    int64_t cur = max_weight.load();
    while (my_max > cur && !max_weight.compare_exchange_weak(cur, my_max)) {}
  }
  if (fd >= 0 && (fsync(fd) || close(fd))) throw std::runtime_error("close failed");
  if (gfd >= 0 && (fsync(gfd) || close(gfd))) throw std::runtime_error("close failed");
  if (parts > 1)
    printf("part=%d/%d chunks=[%lld, %lld) max_weight=%lld (%.1f s)\n", part, parts, (long long)k0, (long long)k1,
           (long long)max_weight.load(), omp_get_wtime() - t0);
  else {
    int64_t denominator = 1;
    while (denominator < max_weight) denominator *= 2;
    printf("vertices=%lld arcs=%lld max_weight=%lld riken_denominator=%lld\n", (long long)n, (long long)m,
           (long long)max_weight.load(), (long long)denominator);
    printf("dim=%d side=%lld wide=%d bytes=%lld gr_bytes=%lld (%.1f s)\n", D, (long long)SIDE, int(wide),
           fd >= 0 ? (long long)(edges_at + m * erec) : 0LL, gfd >= 0 ? (long long)(g_w_at + m * 4) : 0LL,
           omp_get_wtime() - t0);
  }
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
  fprintf(stderr, "grid_graph: %s\n", e.what());
  return 1;
}
