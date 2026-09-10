/**
 * Standalone digest of a generated graph's exact SSSP solution.
 *
 * Builds without Charm++ so the same source can be compiled by several
 * toolchains and the results compared. If two compilers disagree, graph
 * generation is not reproducible and no cross-machine result is comparable.
 *
 *   c++ -O2 -std=c++17 -DGRAPH_GEN_STANDALONE -I.. tools/graph_digest.cpp
 *   ./a.out <vertices> <average degree> <seed> <mode 1=random,2=mesh> [source]
 */
#include "graph_gen.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

int main(int argc, char **argv) {
  if (argc < 5) {
    fprintf(stderr,
            "usage: %s <vertices> <average degree> <seed> "
            "<mode 1=random,2=mesh> [source]\n",
            argv[0]);
    return 2;
  }
  long num_vertices = atol(argv[1]);
  long average_degree = atol(argv[2]);
  int seed = atoi(argv[3]);
  int mode = atoi(argv[4]);
  long source = (argc > 5) ? atol(argv[5]) : 0;

  cost unreachable = std::numeric_limits<cost>::max();
  std::vector<cost> distances;
  serial_dijkstra(num_vertices, average_degree, seed, mode, source, unreachable,
                  distances);

  DistanceDigest digest;
  for (long i = 0; i < num_vertices; i++)
    digest.add(i, distances[i], unreachable);

  printf("h1=%llu h2=%llu reachable=%llu distance_sum=%llu\n",
         (unsigned long long)digest.h1, (unsigned long long)digest.h2,
         (unsigned long long)digest.reachable,
         (unsigned long long)digest.distance_sum);
  return 0;
}
