#ifndef GRAPHLIB_CSR_H
#define GRAPHLIB_CSR_H

/**
 * Flat CSR for one PE's slice of the graph.
 *
 * What this replaces: an array of Node, each holding its own
 * std::vector<Edge>. That is one heap allocation per vertex and one pointer
 * chase per vertex visit, and it costs more than it looks like. Per vertex of
 * average degree d the old layout paid 24 bytes of vector header, 8 for the
 * distance, 4 for a home_process field that was always thisIndex, 4 of
 * padding, an allocator header of its own, and 16d for the edges -- with the
 * edges scattered across the heap in generation order, and each vector's
 * capacity rounded up by doubling, so up to half the edge bytes were slack.
 * CSR pays 8 bytes of offset and 16d contiguous bytes, and walks a frontier
 * vertex's adjacency as one sequential read.
 *
 * The distance vector deliberately stays out of this structure. It is written
 * on every accepted update while the topology is read-only for the whole run,
 * so interleaving them would dirty a cache line of edges on every relaxation.
 *
 * Edges within a vertex stay sorted by weight, which the algorithm relies on:
 * process_update reads edges(v)[0].distance as the cheapest way out of v.
 *
 * Peak memory is the thing to be careful about here, not steady state. An
 * earlier draft of this class built into scratch vectors and copied them into
 * exactly-sized arrays, which held both copies at once and made the peak
 * *worse* than the layout it replaced -- and peak is what decides whether a
 * partition fits on a node. The build vectors are therefore the storage: they
 * are reserved once from the caller's estimate and moved into place, so a good
 * estimate means the edge array is allocated exactly once and never copied.
 */

#include <algorithm>
#include <cstddef>
#include <vector>

#ifndef GRAPH_GEN_STANDALONE
#include "weighted_node_struct.h"
#endif

// Charm++ is not available to the standalone tools, which build this header
// without a runtime.
#ifdef GRAPH_GEN_STANDALONE
#include <cstdio>
#include <cstdlib>
#define GRAPHLIB_ABORT(...)                                                    \
  do {                                                                         \
    std::fprintf(stderr, __VA_ARGS__);                                         \
    std::fprintf(stderr, "\n");                                                \
    std::abort();                                                              \
  } while (0)
#else
#define GRAPHLIB_ABORT(...) CkAbort(__VA_ARGS__)
#endif

class LocalCsr {
public:
  long num_vertices() const { return (long)offset_.size() - 1; }
  long num_edges() const { return (long)edge_.size(); }

  long degree(long v) const { return offset_[(size_t)v + 1] - offset_[(size_t)v]; }
  const Edge *edges(long v) const { return edge_.data() + offset_[(size_t)v]; }

  // Bytes held by the topology, for the memory figures the paper reports.
  // Capacity rather than size, so slack from a bad estimate is visible.
  size_t bytes() const {
    return sizeof(long) * offset_.capacity() + sizeof(Edge) * edge_.capacity();
  }

  /**
   * Vertex-at-a-time build, for generators that produce one vertex's adjacency
   * at a time. Call begin(), then append() exactly num_vertices times in
   * ascending local order, then finish().
   *
   * append() sorts the caller's scratch vector in place, so one buffer can be
   * reused for every vertex and nothing is allocated per vertex.
   *
   * expected_edges should be an upper bound where one is known. It only sets
   * the reservation: a low estimate costs a reallocation, not correctness.
   */
  void begin(long num_vertices, long expected_edges = 0) {
    offset_.clear();
    offset_.reserve((size_t)num_vertices + 1);
    offset_.push_back(0);
    edge_.clear();
    if (expected_edges > 0)
      edge_.reserve((size_t)expected_edges);
    expected_vertices_ = num_vertices;
  }

  void append(std::vector<Edge> &adjacency) {
    std::sort(adjacency.begin(), adjacency.end(),
              [](const Edge &a, const Edge &b) { return a.distance < b.distance; });
    edge_.insert(edge_.end(), adjacency.begin(), adjacency.end());
    offset_.push_back((long)edge_.size());
  }

  void finish() {
    // A caller that appended the wrong number of vertices gets a shape error
    // here rather than reads past the end of a row much later.
    if ((long)offset_.size() != expected_vertices_ + 1)
      GRAPHLIB_ABORT("graphlib: CSR build appended %ld vertices, expected %ld",
                     (long)offset_.size() - 1, expected_vertices_);
    shrink_if_slack();
  }

  /**
   * Edge-list build, for sources that hand over this PE's edges in no
   * particular order: the GAPBS reader and the edge-indexed generators.
   * Vertex ids in the input are global; start_vertex is this PE's first.
   *
   * Counting sort: two passes over the input, no per-vertex allocation.
   */
  template <typename EdgeT>
  void build_from_edges(long num_vertices, long start_vertex,
                        const EdgeT *edges, long count) {
    offset_.assign((size_t)num_vertices + 1, 0);
    for (long i = 0; i < count; i++) {
      long v = edges[i].begin - start_vertex;
      if (v < 0 || v >= num_vertices)
        GRAPHLIB_ABORT("graphlib: edge from vertex %ld is not in this "
                       "partition's range [%ld, %ld)",
                       (long)edges[i].begin, start_vertex,
                       start_vertex + num_vertices);
      offset_[(size_t)v + 1]++;
    }
    for (long v = 0; v < num_vertices; v++)
      offset_[(size_t)v + 1] += offset_[(size_t)v];

    edge_.assign((size_t)count, Edge());
    std::vector<long> cursor(offset_.begin(), offset_.end() - 1);
    for (long i = 0; i < count; i++) {
      long v = edges[i].begin - start_vertex;
      Edge e;
      e.end = edges[i].end;
      e.distance = edges[i].distance;
      edge_[(size_t)cursor[(size_t)v]++] = e;
    }
    for (long v = 0; v < num_vertices; v++)
      std::sort(edge_.begin() + offset_[(size_t)v],
                edge_.begin() + offset_[(size_t)v + 1],
                [](const Edge &a, const Edge &b) { return a.distance < b.distance; });
    expected_vertices_ = num_vertices;
  }

private:
  // Only worth a copy when the estimate was bad enough for the slack to matter;
  // shrink_to_fit itself allocates the new array before freeing the old one.
  void shrink_if_slack() {
    if (edge_.capacity() > edge_.size() + edge_.size() / 8 + 64)
      edge_.shrink_to_fit();
    if (offset_.capacity() > offset_.size() + offset_.size() / 8 + 64)
      offset_.shrink_to_fit();
  }

  std::vector<long> offset_; // num_vertices + 1 entries
  std::vector<Edge> edge_;   // edges of vertex v at [offset_[v], offset_[v+1])
  long expected_vertices_ = 0;
};

#endif // GRAPHLIB_CSR_H
