#ifndef GRAPHLIB_GAPBS_H
#define GRAPHLIB_GAPBS_H

/**
 * GAP Benchmark Suite serialized-graph format: .sg (unweighted) and .wsg
 * (weighted).
 *
 * This is the only file format the code reads. GAPBS ships a `converter` tool
 * that turns SNAP, DIMACS, MatrixMarket and edge-list text into these two
 * binary layouts, so letting it handle every text format offline collapses the
 * I/O surface here to one reader -- and the same file is what the GAPBS
 * reference implementations read, which is what makes a cross-validation
 * against them an actual comparison rather than an approximate one.
 *
 * The layout, checked against GAPBS src/writer.h WriteSerializedGraph and
 * src/reader.h ReadSerializedGraph (commit fetched 2026-09-12), is a raw dump
 * of the CSR with no padding between fields:
 *
 *     offset 0    bool     directed            (1 byte)
 *     offset 1    int64    num_edges           (directed edge count)
 *     offset 9    int64    num_nodes
 *     offset 17   int64    offsets[num_nodes+1]   -- in elements, not bytes
 *     ...         DestID   neighbours[num_edges]
 *     if directed: the same two arrays again, for the in-edges
 *
 * DestID is int32 for .sg, and {int32 v; int32 w} for .wsg -- GAPBS fixes both
 * node ids and weights at int32, and refuses anything else. Reads are done with
 * memcpy because the header fields are not naturally aligned: num_edges starts
 * at byte 1.
 *
 * Byte order is the writer's. GAPBS makes the same assumption; a file written
 * on a big-endian machine is not portable, and the sanity checks below will
 * reject it rather than produce nonsense.
 *
 * What this buys, concretely: because the file is CSR ordered by source vertex
 * and the partition is a contiguous vertex range, each PE seeks to its own rows
 * and reads only those. There is no exchange and no parsing on the critical
 * path.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "types.h"


struct GapbsHeader {
  bool directed = false;
  bool weighted = false;
  int64_t num_edges = 0; // directed edge count, as written
  int64_t num_nodes = 0;

  int dest_size() const { return weighted ? 8 : 4; }
  // Byte offset of the out-edge offsets array.
  int64_t offsets_at() const { return 17; }
  int64_t neighbours_at() const { return 17 + (num_nodes + 1) * 8; }
  int64_t expected_size() const {
    int64_t one = (num_nodes + 1) * 8 + num_edges * dest_size();
    return 17 + (directed ? 2 * one : one);
  }
};

inline bool gapbs_path_is_weighted(const std::string &path) {
  return path.size() >= 4 && path.compare(path.size() - 4, 4, ".wsg") == 0;
}

/**
 * Read and validate the header. The size check is the important one: it is
 * what turns "the format guess was wrong" or "this file is truncated" into an
 * immediate, specific failure instead of a graph full of garbage vertex ids.
 */
inline GapbsHeader gapbs_read_header(const std::string &path) {
  GapbsHeader h;
  h.weighted = gapbs_path_is_weighted(path);
  std::FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    GRAPHLIB_ABORT("graphlib: cannot open %s", path.c_str());

  unsigned char head[17];
  if (std::fread(head, 1, 17, f) != 17)
    GRAPHLIB_ABORT("graphlib: %s is too short to hold a GAPBS header",
                   path.c_str());
  h.directed = head[0] != 0;
  std::memcpy(&h.num_edges, head + 1, 8);
  std::memcpy(&h.num_nodes, head + 9, 8);

  std::fseek(f, 0, SEEK_END);
  int64_t actual = (int64_t)std::ftell(f);
  std::fclose(f);

  if (h.num_nodes <= 0 || h.num_edges < 0 || h.num_nodes > (int64_t)1 << 40)
    GRAPHLIB_ABORT("graphlib: %s declares %lld nodes and %lld edges, which is "
                   "not a GAPBS %s file (or was written on a machine with the "
                   "opposite byte order)",
                   path.c_str(), (long long)h.num_nodes,
                   (long long)h.num_edges, h.weighted ? ".wsg" : ".sg");
  if (actual != h.expected_size())
    GRAPHLIB_ABORT("graphlib: %s is %lld bytes but %lld nodes and %lld %s "
                   "edges need %lld. Truncated, or not the format the suffix "
                   "claims.",
                   path.c_str(), (long long)actual, (long long)h.num_nodes,
                   (long long)h.num_edges,
                   h.directed ? "directed" : "undirected",
                   (long long)h.expected_size());
  return h;
}

/**
 * Read offsets[first .. last] inclusive -- last-first+1 entries, enough to give
 * the row bounds of vertices [first, last).
 */
inline void gapbs_read_offsets(const std::string &path, const GapbsHeader &h,
                               int64_t first, int64_t last,
                               std::vector<int64_t> &out) {
  std::FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    GRAPHLIB_ABORT("graphlib: cannot open %s", path.c_str());
  out.resize((size_t)(last - first + 1));
  std::fseek(f, (long)(h.offsets_at() + first * 8), SEEK_SET);
  if (std::fread(out.data(), 8, out.size(), f) != out.size())
    GRAPHLIB_ABORT("graphlib: short read of the offsets array in %s",
                   path.c_str());
  std::fclose(f);
}

/**
 * Read the out-edges of vertices [first, last) into a CSR-shaped pair of
 * arrays: row_offset has last-first+1 entries relative to the first edge read,
 * and edges holds the destinations and weights.
 *
 * An unweighted .sg file has no weights, so one is assigned from the supplied
 * WeightAssigner -- that is how an unweighted real-world topology (most of them)
 * gets used for SSSP at all, and it keeps the weight rule the same function of
 * (u, v) that the generators use.
 */
template <typename WeightFn>
inline void gapbs_read_slice(const std::string &path, const GapbsHeader &h,
                             int64_t first, int64_t last,
                             const WeightFn &weight,
                             std::vector<long> &row_offset,
                             std::vector<Edge> &edges) {
  std::vector<int64_t> bounds;
  gapbs_read_offsets(path, h, first, last, bounds);
  const int64_t begin_edge = bounds.front();
  const int64_t end_edge = bounds.back();
  const int64_t count = end_edge - begin_edge;

  row_offset.resize(bounds.size());
  for (size_t i = 0; i < bounds.size(); i++)
    row_offset[i] = (long)(bounds[i] - begin_edge);

  edges.clear();
  edges.resize((size_t)count);
  if (count == 0)
    return;

  std::FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    GRAPHLIB_ABORT("graphlib: cannot open %s", path.c_str());
  std::fseek(f, (long)(h.neighbours_at() + begin_edge * h.dest_size()),
             SEEK_SET);

  // Read in blocks rather than one fread for the whole slice: a partition of a
  // large graph is hundreds of megabytes, and this keeps the transient buffer
  // to a fixed size on top of the CSR that has to exist anyway.
  const size_t block = 1 << 16;
  std::vector<int32_t> raw(block * 2);
  int64_t done = 0;
  int64_t vertex = first;
  while (done < count) {
    size_t want = (size_t)((count - done) < (int64_t)block ? (count - done)
                                                           : (int64_t)block);
    size_t words = h.weighted ? want * 2 : want;
    if (std::fread(raw.data(), 4, words, f) != words)
      GRAPHLIB_ABORT("graphlib: short read of the neighbour array in %s",
                     path.c_str());
    for (size_t i = 0; i < want; i++) {
      // Advance to the vertex this edge belongs to, so an unweighted file can
      // be given a weight that depends on both endpoints.
      while (vertex < last && row_offset[(size_t)(vertex - first + 1)] <= done + (int64_t)i)
        vertex++;
      Edge e;
      if (h.weighted) {
        e.end = (long)raw[i * 2];
        e.distance = (cost)raw[i * 2 + 1];
      } else {
        e.end = (long)raw[i];
        e.distance = weight((long)vertex, e.end);
      }
      if (e.end < 0 || e.end >= h.num_nodes)
        GRAPHLIB_ABORT("graphlib: %s has an edge to vertex %ld, outside "
                       "[0, %lld)",
                       path.c_str(), (long)e.end, (long long)h.num_nodes);
      edges[(size_t)(done + (int64_t)i)] = e;
    }
    done += (int64_t)want;
  }
  std::fclose(f);
}

/**
 * Write a graph as GAPBS .wsg. Used by tools/graph_convert to turn a generated
 * graph into a file, which is what gives the reader something to be tested
 * against -- and gives GAPBS's own SSSP the identical input, so the two can be
 * compared directly.
 *
 * Always written as directed, which means the in-edge index has to be built
 * too. Writing it as undirected instead would be a claim about the graph
 * (that every edge appears in both endpoints' lists) that the generators do
 * not make.
 */
inline void gapbs_write_wsg(const std::string &path, long num_nodes,
                            const std::vector<long> &row_offset,
                            const std::vector<Edge> &edges) {
  std::FILE *f = std::fopen(path.c_str(), "wb");
  if (!f)
    GRAPHLIB_ABORT("graphlib: cannot write %s", path.c_str());

  // GAPBS fixes node ids and weights at int32 and refuses anything else, so a
  // graph that does not fit has to be rejected here rather than silently
  // truncated into a file that reads back as a different graph.
  if (num_nodes > (long)2147483647)
    GRAPHLIB_ABORT("graphlib: %ld vertices does not fit GAPBS's int32 node ids",
                   num_nodes);
  for (size_t i = 0; i < edges.size(); i++)
    if (edges[i].distance > (cost)2147483647 || edges[i].distance < 0)
      GRAPHLIB_ABORT("graphlib: edge weight %ld does not fit GAPBS's int32 "
                     "weights",
                     (long)edges[i].distance);

  const bool directed = true;
  const int64_t num_edges = (int64_t)edges.size();
  const int64_t nodes = (int64_t)num_nodes;
  unsigned char head[17];
  head[0] = 1;
  std::memcpy(head + 1, &num_edges, 8);
  std::memcpy(head + 9, &nodes, 8);
  std::fwrite(head, 1, 17, f);

  std::vector<int64_t> offsets((size_t)num_nodes + 1);
  for (long v = 0; v <= num_nodes; v++)
    offsets[(size_t)v] = (int64_t)row_offset[(size_t)v];
  std::fwrite(offsets.data(), 8, offsets.size(), f);

  std::vector<int32_t> out(2 * (size_t)num_edges);
  for (size_t i = 0; i < (size_t)num_edges; i++) {
    out[i * 2] = (int32_t)edges[i].end;
    out[i * 2 + 1] = (int32_t)edges[i].distance;
  }
  if (num_edges > 0)
    std::fwrite(out.data(), 4, out.size(), f);

  // In-edge index: the transpose, built by counting sort.
  std::vector<int64_t> in_offsets((size_t)num_nodes + 2, 0);
  for (size_t i = 0; i < (size_t)num_edges; i++)
    in_offsets[(size_t)edges[i].end + 1]++;
  for (long v = 0; v < num_nodes; v++)
    in_offsets[(size_t)v + 1] += in_offsets[(size_t)v];
  std::vector<int32_t> in_neigh(2 * (size_t)num_edges);
  std::vector<int64_t> cursor(in_offsets.begin(), in_offsets.begin() + num_nodes);
  for (long v = 0; v < num_nodes; v++) {
    for (long j = row_offset[(size_t)v]; j < row_offset[(size_t)v + 1]; j++) {
      int64_t slot = cursor[(size_t)edges[(size_t)j].end]++;
      in_neigh[(size_t)slot * 2] = (int32_t)v;
      in_neigh[(size_t)slot * 2 + 1] = (int32_t)edges[(size_t)j].distance;
    }
  }
  std::fwrite(in_offsets.data(), 8, (size_t)num_nodes + 1, f);
  if (num_edges > 0)
    std::fwrite(in_neigh.data(), 4, in_neigh.size(), f);
  std::fclose(f);
}

#endif // GRAPHLIB_GAPBS_H
