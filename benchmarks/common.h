#ifndef ACIC_BENCH_COMMON_H
#define ACIC_BENCH_COMMON_H
// Independent benchmark I/O and result checksum. No solver/graphlib dependency.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace bench {
constexpr int64_t inf = std::numeric_limits<int64_t>::max();
inline uint64_t mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}
// Same published checksum contract as graphlib/reference.h; algorithms and
// readers are independent. Unreachable sentinels are normalized before hashing.
struct Digest {
  uint64_t h1=0, h2=0, reachable=0, distance_sum=0;
  void add(uint64_t v, int64_t distance) {
    uint64_t d = distance == inf ? UINT64_MAX : uint64_t(distance);
    h1 += mix(v * 0x9E3779B97F4A7C15ULL ^ mix(d));
    h2 += mix(d * 0xC2B2AE3D27D4EB4FULL ^ mix(v + 1));
    if (distance != inf) { ++reachable; distance_sum += d; }
  }
};
inline long rss_kib() { rusage r{}; getrusage(RUSAGE_SELF, &r); return r.ru_maxrss; }
inline void print(int64_t source, double seconds, const Digest &d) {
  std::printf("BENCH source=%lld solve_seconds=%.9f h1=%llu h2=%llu reachable=%llu distance_sum=%llu rss_kib=%ld\n",
    (long long)source, seconds, (unsigned long long)d.h1,
    (unsigned long long)d.h2, (unsigned long long)d.reachable,
    (unsigned long long)d.distance_sum, rss_kib());
}
template<class T> inline void read(std::istream &f, T *p, size_t n) {
  if (!f.read(reinterpret_cast<char*>(p), n*sizeof(T)))
    throw std::runtime_error("short benchmark graph read");
}
struct Edge { int32_t v, w; };
static_assert(sizeof(Edge)==8, "GAPBS weighted edge layout");
struct Header {
  uint8_t directed=0;
  int64_t m=0, n=0;
  explicit Header(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    read(f,&directed,1); read(f,&m,1); read(f,&n,1);
    if (directed > 1 || n <= 0 || n > INT32_MAX || m < 0)
      throw std::runtime_error("invalid graph dimensions");
    f.seekg(0,std::ios::end);
    if (f.tellg() != 17 + (directed ? 2 : 1)*((n+1)*8+m*8))
      throw std::runtime_error("invalid GAPBS graph size");
  }
};
struct Graph {
  Header h;
  std::vector<int64_t> offsets;
  std::vector<Edge> edges;
  explicit Graph(const std::string &path):h(path),offsets(h.n+1),edges(h.m) {
    std::ifstream f(path,std::ios::binary); f.seekg(17);
    read(f,offsets.data(),offsets.size()); read(f,edges.data(),edges.size());
    if(offsets.front()!=0 || offsets.back()!=h.m ||
       !std::is_sorted(offsets.begin(),offsets.end()))
      throw std::runtime_error("invalid CSR offsets");
    for(const auto &e:edges)
      if(e.v<0 || e.v>=h.n || e.w<=0)
        throw std::runtime_error("invalid endpoint or nonpositive weight");
  }
};
}
#endif
