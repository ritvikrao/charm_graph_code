// Compile against the unmodified Wasp SC25 artifact (zenodo 15872863,
// impl/wasp). Only its main is replaced, as gap_driver.cpp does for GAPBS:
// the same .wsg input, one fixed source, one delta, the solve timed alone
// (DeltaStep, which includes Wasp's own leaf detection and scheduler setup),
// and the harness BENCH digest over the returned distances.
#define main wasp_original_main
#include "sssp.cc"
#undef main
#include "common.h"
#include <omp.h>

int main(int argc, char **argv) try {
  if (argc != 4)
    throw std::runtime_error("usage: wasp_sssp graph.wsg source delta");
  const int source = std::stoi(argv[2]), delta = std::stoi(argv[3]);
  numa_distance_map::initialize();
  char a0[] = "wasp_sssp", a1[] = "-f";
  char *args[] = {a0, a1, argv[1]};
  CLDelta<WeightT> cli(3, args, "SSSP comparison");
  if (!cli.ParseArgs()) return 1;
  WeightedBuilder builder(cli);
  WGraph g = builder.MakeGraph();
  if (source < 0 || source >= g.num_nodes() || delta <= 0) return 1;
  const double start = omp_get_wtime();
  auto distances = DeltaStep(g, source, delta);
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
