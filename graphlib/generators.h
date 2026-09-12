#ifndef GRAPHLIB_GENERATORS_H
#define GRAPHLIB_GENERATORS_H

/**
 * In-memory graph generators.
 *
 * Every edge and every weight is a pure function of an index and the run seed,
 * so a generated graph is byte-identical for any PE count, any partitioning,
 * and any run. That invariance is what makes --verify meaningful: the same
 * graph is solved in parallel and serially, and a digest over the two distance
 * vectors must agree. It is also what makes 512-node runs practical, since no
 * input has to be staged or read.
 *
 * There are two shapes of generator here, and the difference decides how the
 * graph reaches the PE that owns it:
 *
 *   Vertex-indexed (uniform, mesh). The adjacency of vertex v is a function of
 *   v alone, so the owner of v generates it directly and no edge ever crosses
 *   the network during construction.
 *
 *   Edge-indexed (RMAT/Kronecker). Edge i is a function of i alone, but its
 *   source vertex is not known until it has been generated, so edges have to be
 *   routed to their owners after generation -- the same path the file readers
 *   use. This is not a shortcoming of the implementation: an RMAT vertex's
 *   degree is the outcome of every edge draw in the graph, so no local rule can
 *   produce one vertex's adjacency.
 */

#include "rng.h"
#include "weights.h"

#include <cmath>
#include <vector>

#include "types.h"

// ---------------------------------------------------------------- uniform --

/**
 * Uniform random graph: out-degree ~ U[0, 2*average_degree], destinations
 * drawn without replacement from [0, V).
 */
inline void gen_random_vertex(long vertex, long num_vertices,
                              long average_degree, int seed,
                              std::vector<Edge> &out) {
  WeightAssigner weight(seed);
  VertexRng rng(vertex, seed);
  // Out-degree uniform over [0, 2*average_degree], inclusive at both ends.
  long degree = (long)rng.bounded((uint64_t)(2 * average_degree + 1));
  if (degree > num_vertices)
    degree = num_vertices; // cannot draw more distinct destinations than exist
  out.clear();
  out.reserve(degree);
  for (long j = 0; j < degree; j++) {
    long candidate_end = (long)rng.bounded((uint64_t)num_vertices);
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
        candidate_end = (long)rng.bounded((uint64_t)num_vertices);
    }
    Edge new_edge;
    new_edge.end = candidate_end;
    new_edge.distance = weight(vertex, candidate_end);
    out.push_back(new_edge);
  }
}

// ------------------------------------------------------------------- mesh --

/**
 * 2-D mesh: up to four axis-aligned neighbours on a side_length x side_length
 * grid. Vertices at or beyond side_length^2 are isolated.
 */
inline void gen_mesh_vertex(long vertex, long side_length, int seed,
                            std::vector<Edge> &out) {
  WeightAssigner weight(seed);
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
      new_edge.distance = weight(vertex, new_edge.end);
      out.push_back(new_edge);
    }
  }
}

inline long mesh_side_length(long num_vertices) {
  return (long)std::sqrt((double)num_vertices);
}

/**
 * How many neighbours gen_mesh_vertex must have produced: 2 at a corner, 3
 * along an edge, 4 in the interior, 0 for a vertex outside the square when V
 * is not a perfect square. Used as a self-check on the one input whose exact
 * structure is known in advance.
 */
inline int mesh_expected_degree(long vertex, long side_length) {
  long x_index = vertex / side_length;
  long y_index = vertex % side_length;
  if (x_index >= side_length)
    return 0;
  int on_boundary = (x_index == 0 || x_index == side_length - 1) +
                    (y_index == 0 || y_index == side_length - 1);
  return 4 - on_boundary;
}

// ------------------------------------------------------------------- RMAT --

/**
 * RMAT / stochastic Kronecker, with the Graph500 quadrant probabilities
 * a=0.57, b=0.19, c=0.19, d=0.05, written as integers per ten-thousand so the
 * draw involves no floating point.
 *
 * Two deliberate departures from the Graph500 reference, both of which matter
 * to a reader:
 *
 *   No per-level noise. Graph500 perturbs (a, b, c) at each level of the
 *   descent. Leaving it out costs a little of the degree distribution's tail
 *   and buys an edge that is a pure function of its own index -- which is what
 *   lets any PE generate exactly its own slice with no communication and no
 *   shared state.
 *
 *   Self-loops are dropped; duplicate edges are kept. A self-loop can never
 *   improve a distance, and a duplicate edge is a legitimate parallel edge that
 *   the solver handles. Dropping duplicates would need a global sort.
 *
 * Vertex labels are permuted (see permute_id): the descent builds an id one bit
 * per level, so without relabelling vertex 0 is the heaviest hub by
 * construction and a contiguous partitioning would hand every hub to the first
 * PE.
 */
#define RMAT_A 5700
#define RMAT_B 1900
#define RMAT_C 1900
// d is the remainder, 500.

inline int rmat_scale(long num_vertices) {
  int scale = 0;
  while ((1L << scale) < num_vertices)
    scale++;
  return scale;
}

inline bool rmat_vertex_count_ok(long num_vertices) {
  return num_vertices > 1 && (num_vertices & (num_vertices - 1)) == 0;
}

/**
 * Generate edge `index` of an RMAT graph on num_vertices = 2^scale vertices.
 * Returns false for a self-loop, which the caller drops.
 */
inline bool gen_rmat_edge(long index, int scale, int seed, long &begin,
                          long &end) {
  IndexRng rng(index, seed, 0x524D4154ULL /* "RMAT" */);
  uint64_t u = 0, v = 0;
  for (int level = 0; level < scale; level++) {
    uint64_t draw = rng.bounded(10000);
    uint64_t bit = 1ULL << level;
    if (draw < RMAT_A) {
      // top-left: neither endpoint takes this bit
    } else if (draw < RMAT_A + RMAT_B) {
      v |= bit;
    } else if (draw < RMAT_A + RMAT_B + RMAT_C) {
      u |= bit;
    } else {
      u |= bit;
      v |= bit;
    }
  }
  const uint64_t key = splitmix64((uint64_t)(uint32_t)seed ^ 0x5045524DULL);
  begin = (long)permute_id(u, scale, key);
  end = (long)permute_id(v, scale, key);
  if (begin == end)
    return false;
  return true;
}

/**
 * Generate edges [first, last) of an RMAT graph, appending to out. Each edge is
 * emitted once in the direction the descent produced; the caller decides
 * whether to add the reverse.
 */
template <typename LongEdgeT>
inline void gen_rmat_range(long first, long last, long num_vertices, int seed,
                           const WeightAssigner &weight,
                           std::vector<LongEdgeT> &out) {
  int scale = rmat_scale(num_vertices);
  for (long i = first; i < last; i++) {
    long begin = 0, end = 0;
    if (!gen_rmat_edge(i, scale, seed, begin, end))
      continue;
    LongEdgeT e;
    e.begin = begin;
    e.end = end;
    e.distance = weight(begin, end);
    out.push_back(e);
  }
}

#endif // GRAPHLIB_GENERATORS_H
