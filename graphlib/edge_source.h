#ifndef GRAPHLIB_EDGE_SOURCE_H
#define GRAPHLIB_EDGE_SOURCE_H

/**
 * Where a graph comes from, and the one thing the rest of the code needs to
 * know about it: whether a PE can produce its own vertices' adjacency on its
 * own, or whether edges have to be routed to their owners first.
 *
 *   Vertex-indexed sources (uniform, mesh) answer "what are v's out-edges?"
 *   from v and the seed alone. The owner builds its rows directly. No
 *   communication, and the serial reference can regenerate any vertex on
 *   demand.
 *
 *   Edge-indexed sources (RMAT) answer "what is edge i?" from i and the seed.
 *   The source vertex is an output of the draw, not an input, so every PE
 *   generates a contiguous slice of the edge index space and the edges are then
 *   exchanged. This is inherent to RMAT: a vertex's degree is the outcome of
 *   every edge draw in the graph.
 *
 *   Row-addressable sources (GAPBS .sg/.wsg) are already CSR on disk and
 *   ordered by source vertex, so a PE seeks to its own rows and reads only
 *   those -- no exchange either, and no parsing.
 */

#include "gapbs.h"
#include "generators.h"
#include "weights.h"

#include <string>
#include <vector>

enum GenerateMode {
  MODE_CSV = 0,     // legacy comma-separated edge list, read serially on PE 0
  MODE_UNIFORM = 1, // uniform random
  MODE_MESH = 2,    // 2-D mesh
  MODE_RMAT = 3,    // RMAT / Kronecker, Graph500 quadrant probabilities
  MODE_GAPBS = 4,   // GAPBS serialized .sg / .wsg
};

inline const char *mode_name(int mode) {
  switch (mode) {
  case MODE_CSV: return "csv";
  case MODE_UNIFORM: return "uniform";
  case MODE_MESH: return "mesh";
  case MODE_RMAT: return "rmat";
  case MODE_GAPBS: return "gapbs";
  default: return "unknown";
  }
}

// The owner of a vertex can build its own row.
inline bool mode_is_vertex_indexed(int mode) {
  return mode == MODE_UNIFORM || mode == MODE_MESH;
}

// Edges are produced against an edge index and must be routed to their owners.
inline bool mode_needs_edge_exchange(int mode) { return mode == MODE_RMAT; }

struct GraphSpec {
  int mode = MODE_UNIFORM;
  long num_vertices = 0;
  long num_edges = 0;      // requested; mesh and the file modes derive it
  long average_degree = 0;
  int seed = 0;
  std::string path;
  WeightAssigner weights;

  void adjacency(long vertex, std::vector<Edge> &out) const {
    if (mode == MODE_MESH)
      gen_mesh_vertex(vertex, mesh_side_length(num_vertices), seed, out);
    else
      gen_random_vertex(vertex, num_vertices, average_degree, seed, out);
  }
};

/**
 * Build the whole graph into one CSR. Used by the serial reference solver and
 * by tools/graph_convert -- never on the parallel path, where the point is that
 * no PE ever holds the whole graph.
 */
template <typename LongEdgeT>
inline void build_full_csr(const GraphSpec &spec, LocalCsr &csr) {
  if (mode_is_vertex_indexed(spec.mode)) {
    std::vector<Edge> adjacency;
    csr.begin(spec.num_vertices,
              spec.mode == MODE_MESH ? 4 * spec.num_vertices
                                     : spec.average_degree * spec.num_vertices);
    for (long v = 0; v < spec.num_vertices; v++) {
      spec.adjacency(v, adjacency);
      csr.append(adjacency);
    }
    csr.finish();
    return;
  }
  if (spec.mode == MODE_RMAT) {
    std::vector<LongEdgeT> edges;
    edges.reserve((size_t)spec.num_edges);
    gen_rmat_range(0, spec.num_edges, spec.num_vertices, spec.seed,
                   spec.weights, edges);
    csr.build_from_edges(spec.num_vertices, 0, edges.data(), (long)edges.size());
    return;
  }
  if (spec.mode == MODE_GAPBS) {
    GapbsHeader header = gapbs_read_header(spec.path);
    std::vector<long> row_offset;
    std::vector<Edge> edges;
    gapbs_read_slice(spec.path, header, 0, header.num_nodes, spec.weights,
                     row_offset, edges);
    csr.adopt_rows(header.num_nodes, row_offset, edges);
    return;
  }
  GRAPHLIB_ABORT("graphlib: build_full_csr does not handle mode %s",
                 mode_name(spec.mode));
}

#endif // GRAPHLIB_EDGE_SOURCE_H
