#ifndef GRAPHLIB_REFERENCE_H
#define GRAPHLIB_REFERENCE_H

/**
 * Ground truth: an order-independent checksum of a distance vector, and a
 * serial Dijkstra to produce one to compare against.
 */

#include "csr.h"
#include "edge_source.h"

#include <queue>
#include <utility>
#include <vector>

/**
 * Order-independent checksum over the distance vector. Two independent 64-bit
 * sums plus a population count; summation makes the result invariant to the
 * order vertices are visited in and to how they are spread over PEs.
 */
struct DistanceDigest {
  unsigned long long h1 = 0;
  unsigned long long h2 = 0;
  unsigned long long reachable = 0;
  unsigned long long distance_sum = 0;

  // Unreachable vertices are folded in as a distinct marker rather than as
  // their sentinel value, so the digest does not depend on what lmax happens
  // to be.
  void add(long vertex, cost distance, cost unreachable_sentinel) {
    uint64_t v = (uint64_t)vertex;
    uint64_t d = (distance == unreachable_sentinel) ? 0xFFFFFFFFFFFFFFFFULL
                                                    : (uint64_t)distance;
    h1 += splitmix64(v * 0x9E3779B97F4A7C15ULL ^ splitmix64(d));
    h2 += splitmix64(d * 0xC2B2AE3D27D4EB4FULL ^ splitmix64(v + 1));
    if (distance != unreachable_sentinel) {
      reachable++;
      distance_sum += (uint64_t)distance;
    }
  }

  bool operator==(const DistanceDigest &o) const {
    return h1 == o.h1 && h2 == o.h2 && reachable == o.reachable &&
           distance_sum == o.distance_sum;
  }
};

/**
 * Serial Dijkstra over a CSR, for use as ground truth.
 */
inline void serial_dijkstra(const LocalCsr &graph, long source,
                            cost unreachable_sentinel,
                            std::vector<cost> &distances) {
  long num_vertices = graph.num_vertices();
  distances.assign((size_t)num_vertices, unreachable_sentinel);
  if (source < 0 || source >= num_vertices)
    return;

  typedef std::pair<cost, long> HeapEntry; // (distance, vertex)
  std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>>
      frontier;
  distances[(size_t)source] = 0;
  frontier.push(HeapEntry(0, source));

  while (!frontier.empty()) {
    HeapEntry top = frontier.top();
    frontier.pop();
    if (top.first > distances[(size_t)top.second])
      continue; // stale entry
    const Edge *adjacency = graph.edges(top.second);
    long degree = graph.degree(top.second);
    for (long i = 0; i < degree; i++) {
      long end = adjacency[i].end;
      cost candidate = top.first + adjacency[i].distance;
      if (candidate < distances[(size_t)end]) {
        distances[(size_t)end] = candidate;
        frontier.push(HeapEntry(candidate, end));
      }
    }
  }
}

/**
 * Build the whole graph and solve it. The reference deliberately builds its own
 * copy from the spec rather than reading the parallel code's partitioned one:
 * a bug that corrupts the partitioning has to be able to show up as a
 * mismatch, which it cannot if both sides read the same array.
 */
template <typename LongEdgeT>
inline void serial_reference(const GraphSpec &spec, long source,
                             cost unreachable_sentinel,
                             std::vector<cost> &distances) {
  LocalCsr graph;
  build_full_csr<LongEdgeT>(spec, graph);
  serial_dijkstra(graph, source, unreachable_sentinel, distances);
}

#endif // GRAPHLIB_REFERENCE_H
