// RoutingKit's OSM car graph (bin/osm_extract, 7-argument form) to the DIMACS
// 9th-challenge files the campaign already reads for road-usa: a ".gr" arc
// list and a ".co" coordinate list, so prepare_graph and reorder_graph.py treat
// an OSM region exactly like USA-road-d.
//
//   osm_to_dimacs DIR OUT_PREFIX
//
// DIR holds first_out, head, geo_distance, latitude and longitude as RoutingKit
// writes them: raw arrays of uint32 (float for the coordinates), no header.
// Weights are geo_distance in metres, as in USA-road-d (physical distance).
// OSM has arcs of length 0 (consecutive nodes at one coordinate); each becomes
// 1 m so every weight is positive, and the count is reported. Coordinates are
// written like DIMACS's: longitude and latitude in millionths of a degree.
// The graph stays directed here (one-way streets); prepare_graph's dimacs mode
// makes it canonical undirected, as it does road-usa. OSM extracts carry many
// small pieces (islands, private roads, clipped borders), so only the largest
// connected component of the undirected graph is kept, renumbered in the
// original order: every source then reaches the whole network, as on road-usa.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

template <class T> static std::vector<T> load(const std::string &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) throw std::runtime_error("cannot open " + path);
  const std::streamsize bytes = in.tellg();
  if (bytes % sizeof(T)) throw std::runtime_error(path + ": size is not a multiple of the element");
  std::vector<T> v(bytes / sizeof(T));
  in.seekg(0);
  in.read(reinterpret_cast<char *>(v.data()), bytes);
  return v;
}

int main(int argc, char **argv) try {
  if (argc != 3) {
    fprintf(stderr, "usage: osm_to_dimacs DIR OUT_PREFIX\n");
    return 2;
  }
  const std::string dir = argv[1], out = argv[2];
  const auto first_out = load<uint32_t>(dir + "/first_out");
  const auto head = load<uint32_t>(dir + "/head");
  const auto length = load<uint32_t>(dir + "/geo_distance");
  const auto lat = load<float>(dir + "/latitude");
  const auto lon = load<float>(dir + "/longitude");
  const uint64_t n = first_out.size() - 1, m = head.size();
  if (first_out.front() != 0 || first_out.back() != m || length.size() != m ||
      lat.size() != n || lon.size() != n)
    throw std::runtime_error("inconsistent RoutingKit vectors");
  // Largest connected component, ignoring direction (union-find).
  std::vector<uint32_t> parent(n);
  std::iota(parent.begin(), parent.end(), 0u);
  auto find = [&](uint32_t x) {
    while (parent[x] != x) x = parent[x] = parent[parent[x]];
    return x;
  };
  for (uint64_t u = 0; u < n; u++)
    for (uint64_t a = first_out[u]; a < first_out[u + 1]; a++) {
      if (head[a] >= n) throw std::runtime_error("head out of range");
      uint32_t x = find((uint32_t)u), y = find(head[a]);
      if (x != y) parent[std::max(x, y)] = std::min(x, y);
    }
  std::vector<uint64_t> size(n, 0);
  for (uint64_t v = 0; v < n; v++) size[find((uint32_t)v)]++;
  const uint32_t giant = (uint32_t)(std::max_element(size.begin(), size.end()) - size.begin());
  std::vector<uint32_t> id(n, UINT32_MAX);
  uint64_t kept = 0, kept_arcs = 0;
  for (uint64_t v = 0; v < n; v++)
    if (find((uint32_t)v) == giant) id[v] = (uint32_t)kept++;
  for (uint64_t u = 0; u < n; u++)
    if (id[u] != UINT32_MAX) kept_arcs += first_out[u + 1] - first_out[u];
  FILE *gr = fopen((out + ".gr").c_str(), "w");
  FILE *co = fopen((out + ".co").c_str(), "w");
  if (!gr || !co) throw std::runtime_error("cannot write " + out);
  static char buf_gr[1 << 24], buf_co[1 << 24];
  setvbuf(gr, buf_gr, _IOFBF, sizeof buf_gr);
  setvbuf(co, buf_co, _IOFBF, sizeof buf_co);
  fprintf(gr, "c OSM car graph (RoutingKit osm_extract), weight = geo_distance [m], 0 -> 1\n");
  fprintf(gr, "p sp %llu %llu\n", (unsigned long long)kept, (unsigned long long)kept_arcs);
  uint64_t zero = 0, self = 0, heaviest = 0;
  for (uint64_t u = 0; u < n; u++) {
    if (id[u] == UINT32_MAX) continue;
    for (uint64_t a = first_out[u]; a < first_out[u + 1]; a++) {
      uint64_t w = length[a];
      if (w == 0) { w = 1; zero++; }
      if (head[a] == u) self++;
      if (w > heaviest) heaviest = w;
      fprintf(gr, "a %u %u %llu\n", id[u] + 1, id[head[a]] + 1, (unsigned long long)w);
    }
  }
  fprintf(co, "p aux sp co %llu\n", (unsigned long long)kept);
  for (uint64_t v = 0; v < n; v++)
    if (id[v] != UINT32_MAX)
      fprintf(co, "v %u %lld %lld\n", id[v] + 1,
              (long long)std::llround(lon[v] * 1e6), (long long)std::llround(lat[v] * 1e6));
  if (fclose(gr) || fclose(co)) throw std::runtime_error("write failed");
  printf("input_vertices=%llu input_arcs=%llu vertices=%llu arcs=%llu zero_length_arcs=%llu "
         "self_loops=%llu max_weight=%llu\n",
         (unsigned long long)n, (unsigned long long)m, (unsigned long long)kept,
         (unsigned long long)kept_arcs, (unsigned long long)zero, (unsigned long long)self,
         (unsigned long long)heaviest);
  return 0;
} catch (const std::exception &e) {
  fprintf(stderr, "osm_to_dimacs: %s\n", e.what());
  return 1;
}
