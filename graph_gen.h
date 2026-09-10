#ifndef GRAPH_GEN_H
#define GRAPH_GEN_H

/**
 * Deterministic graph generation and a serial reference solver.
 *
 * Every edge and every weight produced here is a pure function of the global
 * vertex id and the run seed, so the generated graph is byte-identical for any
 * PE count, any partitioning, and any run. That invariance is what makes
 * --verify meaningful: the same graph is solved in parallel and serially, and
 * a hash over the two distance vectors must agree.
 *
 * Weights are drawn from a hash of the ordered pair (u, v) rather than from the
 * per-vertex generator stream, so topology and weight distribution are
 * independent knobs.
 */

#include <cmath>
#include <cstdint>
#include <queue>
#include <random>
#include <vector>

#include "weighted_node_struct.h"

// SplitMix64. Used as both a seed mixer and a standalone hash.
inline uint64_t splitmix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

inline uint64_t hash_pair(uint64_t a, uint64_t b, uint64_t seed) {
  return splitmix64(splitmix64(a ^ splitmix64(seed)) ^ (b * 0x9E3779B97F4A7C15ULL));
}

// Edge weights live in [1, MAX_EDGE_WEIGHT], matching the 2024 configuration.
#define MAX_EDGE_WEIGHT 1000

inline cost edge_weight(long u, long v, int seed) {
  return (cost)(hash_pair((uint64_t)u, (uint64_t)v, (uint64_t)seed) %
                MAX_EDGE_WEIGHT) +
         1;
}

// Per-vertex topology stream. Seeded only by (vertex, seed), never by position.
inline std::mt19937_64 vertex_rng(long vertex, int seed) {
  return std::mt19937_64(splitmix64((uint64_t)vertex ^ splitmix64((uint64_t)seed)));
}

/**
 * Uniform random graph: out-degree ~ U[0, 2*average_degree], destinations
 * drawn without replacement from [0, V).
 */
inline void gen_random_vertex(long vertex, long num_vertices,
                              long average_degree, int seed,
                              std::vector<Edge> &out) {
  std::mt19937_64 generator = vertex_rng(vertex, seed);
  std::uniform_int_distribution<long> edge_count_distribution(
      0, 2 * average_degree);
  std::uniform_int_distribution<long> edge_dest_distribution(0,
                                                             num_vertices - 1);
  long degree = edge_count_distribution(generator);
  if (degree > num_vertices)
    degree = num_vertices; // cannot draw more distinct destinations than exist
  out.clear();
  out.reserve(degree);
  for (long j = 0; j < degree; j++) {
    long candidate_end = edge_dest_distribution(generator);
    bool repeated = true;
    while (repeated) {
      repeated = false;
      for (size_t k = 0; k < out.size(); k++) {
        if (out[k].end == candidate_end) {
          repeated = true;
          break;
        }
      }
      if (repeated)
        candidate_end = edge_dest_distribution(generator);
    }
    Edge new_edge;
    new_edge.end = candidate_end;
    new_edge.distance = edge_weight(vertex, candidate_end, seed);
    out.push_back(new_edge);
  }
}

/**
 * 2-D mesh: up to four axis-aligned neighbours on a side_length x side_length
 * grid. Vertices at or beyond side_length^2 are isolated.
 */
inline void gen_mesh_vertex(long vertex, long side_length, int seed,
                            std::vector<Edge> &out) {
  out.clear();
  long x_index = vertex / side_length;
  long y_index = vertex % side_length;
  if (x_index >= side_length)
    return;
  out.reserve(4);
  for (int axis = 0; axis < 2; axis++) {
    for (int j = -1; j <= 1; j += 2) {
      long neighbor_x = x_index + (axis == 0 ? j : 0);
      long neighbor_y = y_index + (axis == 0 ? 0 : j);
      if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= side_length ||
          neighbor_y >= side_length)
        continue;
      Edge new_edge;
      new_edge.end = neighbor_x * side_length + neighbor_y;
      new_edge.distance = edge_weight(vertex, new_edge.end, seed);
      out.push_back(new_edge);
    }
  }
}

inline long mesh_side_length(long num_vertices) {
  return (long)std::sqrt((double)num_vertices);
}

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
 * Serial Dijkstra over the same generated graph, for use as ground truth.
 * Regenerates each vertex's adjacency on demand so the reference never depends
 * on the parallel code's partitioned copy.
 *
 * generate_mode: 1 = uniform random, 2 = 2-D mesh.
 */
inline void serial_dijkstra(long num_vertices, long average_degree, int seed,
                            int generate_mode, long source,
                            cost unreachable_sentinel,
                            std::vector<cost> &distances) {
  distances.assign(num_vertices, unreachable_sentinel);
  if (source < 0 || source >= num_vertices)
    return;
  long side_length = mesh_side_length(num_vertices);

  typedef std::pair<cost, long> HeapEntry; // (distance, vertex)
  std::priority_queue<HeapEntry, std::vector<HeapEntry>,
                      std::greater<HeapEntry>>
      frontier;
  distances[source] = 0;
  frontier.push(HeapEntry(0, source));

  std::vector<Edge> adjacent;
  while (!frontier.empty()) {
    HeapEntry top = frontier.top();
    frontier.pop();
    if (top.first > distances[top.second])
      continue; // stale entry
    if (generate_mode == 2)
      gen_mesh_vertex(top.second, side_length, seed, adjacent);
    else
      gen_random_vertex(top.second, num_vertices, average_degree, seed,
                        adjacent);
    for (size_t i = 0; i < adjacent.size(); i++) {
      long end = adjacent[i].end;
      cost candidate = top.first + adjacent[i].distance;
      if (candidate < distances[end]) {
        distances[end] = candidate;
        frontier.push(HeapEntry(candidate, end));
      }
    }
  }
}

#endif // GRAPH_GEN_H
