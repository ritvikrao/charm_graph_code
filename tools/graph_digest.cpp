/**
 * Standalone digest of a graph's exact SSSP solution.
 *
 * Builds without Charm++ so the same source can be compiled by several
 * toolchains and the results compared. If two compilers disagree, graph
 * generation is not reproducible and no cross-machine result is comparable.
 * scripts/check_generator_portability.sh does exactly that.
 *
 *   c++ -O2 -std=c++17 -DGRAPH_GEN_STANDALONE -I. tools/graph_digest.cpp
 *   ./a.out <vertices> <edges|degree> <seed> <mode> [source] [path]
 *
 * mode: 1 uniform (arg 2 is the average degree), 2 mesh, 3 rmat (arg 2 is the
 * edge count), 4 gapbs (arg 2 ignored, path required).
 */
#include "graphlib/graphlib.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

int main(int argc, char **argv) {
  if (argc < 5) {
    std::fprintf(stderr,
                 "usage: %s <vertices> <edges|average degree> <seed> "
                 "<mode 1=uniform,2=mesh,3=rmat,4=gapbs> [source] [path]\n",
                 argv[0]);
    return 2;
  }
  GraphSpec spec;
  spec.num_vertices = atol(argv[1]);
  spec.seed = atoi(argv[3]);
  spec.mode = atoi(argv[4]);
  long source = (argc > 5) ? atol(argv[5]) : 0;
  if (argc > 6)
    spec.path = argv[6];
  spec.weights = WeightAssigner(spec.seed);

  if (spec.mode == MODE_RMAT) {
    spec.num_edges = atol(argv[2]);
    if (!rmat_vertex_count_ok(spec.num_vertices)) {
      std::fprintf(stderr,
                   "rmat needs a power-of-two vertex count; %ld is not one\n",
                   spec.num_vertices);
      return 2;
    }
  } else {
    spec.average_degree = atol(argv[2]);
  }
  if (spec.mode == MODE_GAPBS) {
    if (spec.path.empty()) {
      std::fprintf(stderr, "mode 4 needs a path\n");
      return 2;
    }
    spec.num_vertices = gapbs_read_header(spec.path).num_nodes;
  }

  cost unreachable = std::numeric_limits<cost>::max();
  std::vector<cost> distances;
  serial_reference<LongEdge>(spec, source, unreachable, distances);

  DistanceDigest digest;
  for (long i = 0; i < (long)distances.size(); i++)
    digest.add(i, distances[(size_t)i], unreachable);

  std::printf("h1=%llu h2=%llu reachable=%llu distance_sum=%llu\n",
              (unsigned long long)digest.h1, (unsigned long long)digest.h2,
              (unsigned long long)digest.reachable,
              (unsigned long long)digest.distance_sum);
  return 0;
}
