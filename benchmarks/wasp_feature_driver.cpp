// Run the SC25 Wasp artifact's existing ablation implementations against the
// same .wsg, source and digest interface as benchmarks/wasp_driver.cpp.
// Modes: noopt, pull-only and leaves-only. The full implementation is the
// separately built, unmodified wasp_sssp binary.
#define main wasp_ablation_original_main
#include "sssp-ablation.cc"
#undef main
#include "common.h"
#include <omp.h>

int main(int argc, char **argv) try {
  if (argc != 5)
    throw std::runtime_error("usage: wasp_feature graph.wsg source delta noopt|pull|leaves");
  const int source = std::stoi(argv[2]), delta = std::stoi(argv[3]);
  const std::string mode = argv[4];
  if (mode != "noopt" && mode != "pull" && mode != "leaves")
    throw std::runtime_error("unknown Wasp feature mode");
  numa_distance_map::initialize();
  char a0[] = "wasp_feature", a1[] = "-f";
  char *args[] = {a0, a1, argv[1]};
  CLDelta<WeightT> cli(3, args, "SSSP feature comparison");
  if (!cli.ParseArgs()) return 1;
  WeightedBuilder builder(cli);
  WGraph g = builder.MakeGraph();
  if (g.directed() || source < 0 || source >= g.num_nodes() || delta <= 0)
    return 1;
  const double start = omp_get_wtime();
  auto distances = mode == "noopt" ? NoOptDS(g, source, delta)
                 : mode == "pull" ? BidirectionalRelaxDS<false>(g, source, delta)
                 : LeavesDS<false, true>(g, source, delta);
  const double elapsed = omp_get_wtime() - start;
  bench::Digest digest;
  for (int64_t v = 0; v < g.num_nodes(); ++v) {
    const WeightT d = distances[v].load(std::memory_order_relaxed);
    digest.add(v, d == DIST_INF ? bench::inf : d);
  }
  bench::print(source, elapsed, digest);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
