/**
 * Write a graph out as a GAPBS serialized .wsg file, and inspect one.
 *
 * Two jobs, both offline:
 *
 *   - Produce real input files from the in-memory generators, so the reader has
 *     something to be tested against and so GAPBS's own SSSP can be run on the
 *     identical graph. That is what makes a comparison against the single-node
 *     reference a comparison rather than an approximation.
 *   - Migrate the legacy comma-separated edge lists in graphs/ to the one
 *     binary format the solver reads, so mode 0 can eventually go.
 *
 * Builds without Charm++:
 *   c++ -O2 -std=c++17 -DGRAPH_GEN_STANDALONE -I. tools/graph_convert.cpp
 *
 *   graph_convert gen  <mode 1|2|3> <vertices> <edges|degree> <seed> <out.wsg>
 *   graph_convert csv  <in.csv> <vertices> <seed> <out.wsg>
 *   graph_convert stat <file.sg|file.wsg>
 *   graph_convert source <mode 1|2|3> <vertices> <edges|degree> <seed>
 */
#include "graphlib/graphlib.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

// Flatten a CSR back into the two arrays the writer wants.
static void flatten(const LocalCsr &csr, std::vector<long> &row_offset,
                    std::vector<Edge> &edges) {
  row_offset.assign((size_t)csr.num_vertices() + 1, 0);
  edges.clear();
  edges.reserve((size_t)csr.num_edges());
  for (long v = 0; v < csr.num_vertices(); v++) {
    row_offset[(size_t)v] = (long)edges.size();
    const Edge *row = csr.edges(v);
    for (long i = 0; i < csr.degree(v); i++)
      edges.push_back(row[i]);
  }
  row_offset[(size_t)csr.num_vertices()] = (long)edges.size();
}

// <mode> <vertices> <edges|degree> <seed>, starting at argv[2]. Shared by gen
// and source, which describe the same graph and differ only in what they do
// with it.
static bool spec_from_args(char **argv, GraphSpec &spec) {
  spec.mode = atoi(argv[2]);
  spec.num_vertices = atol(argv[3]);
  spec.seed = atoi(argv[5]);
  spec.weights = WeightAssigner(spec.seed);
  if (spec.mode == MODE_RMAT) {
    spec.num_edges = atol(argv[4]);
    if (!rmat_vertex_count_ok(spec.num_vertices)) {
      std::fprintf(stderr, "rmat needs a power-of-two vertex count\n");
      return false;
    }
  } else {
    spec.average_degree = atol(argv[4]);
  }
  return true;
}

static int do_gen(int argc, char **argv) {
  if (argc < 7) {
    std::fprintf(stderr, "gen needs <mode> <vertices> <edges|degree> <seed> "
                         "<out.wsg>\n");
    return 2;
  }
  GraphSpec spec;
  if (!spec_from_args(argv, spec))
    return 2;
  std::string out = argv[6];

  LocalCsr csr;
  build_full_csr<LongEdge>(spec, csr);
  std::vector<long> row_offset;
  std::vector<Edge> edges;
  flatten(csr, row_offset, edges);
  gapbs_write_wsg(out, spec.num_vertices, row_offset, edges);
  std::printf("wrote %s: %ld vertices, %ld edges (%s)\n", out.c_str(),
              spec.num_vertices, (long)edges.size(), mode_name(spec.mode));
  return 0;
}

/**
 * Legacy path: "u,v" per line, weights assigned from the endpoint pair exactly
 * as the in-Main reader did, so a converted file solves to the same answer as
 * the csv it came from.
 */
static int do_csv(int argc, char **argv) {
  if (argc < 6) {
    std::fprintf(stderr, "csv needs <in.csv> <vertices> <seed> <out.wsg>\n");
    return 2;
  }
  std::string in = argv[2];
  long num_vertices = atol(argv[3]);
  int seed = atoi(argv[4]);
  std::string out = argv[5];
  WeightAssigner weight(seed);

  std::ifstream file(in);
  if (!file) {
    std::fprintf(stderr, "cannot open %s\n", in.c_str());
    return 1;
  }
  std::vector<LongEdge> edge_list;
  std::string line;
  long max_index = 0;
  while (std::getline(file, line)) {
    size_t comma = line.find(',');
    if (comma == std::string::npos)
      continue;
    LongEdge e;
    e.begin = std::stol(line.substr(0, comma));
    e.end = std::stol(line.substr(comma + 1));
    e.distance = weight(e.begin, e.end);
    if (e.begin > max_index) max_index = e.begin;
    if (e.end > max_index) max_index = e.end;
    edge_list.push_back(e);
  }
  if (num_vertices <= max_index)
    num_vertices = max_index + 1;

  LocalCsr csr;
  csr.build_from_edges(num_vertices, 0, edge_list.data(), (long)edge_list.size());
  std::vector<long> row_offset;
  std::vector<Edge> edges;
  flatten(csr, row_offset, edges);
  gapbs_write_wsg(out, num_vertices, row_offset, edges);
  std::printf("wrote %s: %ld vertices, %ld edges (from %s)\n", out.c_str(),
              num_vertices, (long)edges.size(), in.c_str());
  return 0;
}

/**
 * Print a usable source vertex, and the out-degree distribution behind the
 * choice.
 *
 * RMAT makes this a real question rather than a formality. A third of the
 * vertices of a scale-free graph have no out-edges at all, so an arbitrary
 * source both solves a trivial problem and -- because the convergence test
 * wants more than a thousand updates before it will believe a run finished --
 * never terminates. Picking the source by hand per configuration is how a
 * measurement quietly becomes irreproducible, so pick it by a stated rule: the
 * lowest-numbered vertex whose out-degree is at least the graph's mean. That
 * is deterministic, it is the same vertex at every PE count, and it is not
 * "the biggest hub", which would be a different experiment.
 */
static int do_source(int argc, char **argv) {
  if (argc < 6) {
    std::fprintf(stderr, "source needs <mode> <vertices> <edges|degree> "
                         "<seed>\n");
    return 2;
  }
  GraphSpec spec;
  if (!spec_from_args(argv, spec))
    return 2;
  LocalCsr csr;
  build_full_csr<LongEdge>(spec, csr);

  long isolated = 0, max_degree = 0;
  for (long v = 0; v < csr.num_vertices(); v++) {
    long d = csr.degree(v);
    if (d == 0)
      isolated++;
    if (d > max_degree)
      max_degree = d;
  }
  double mean = csr.num_vertices()
                    ? (double)csr.num_edges() / (double)csr.num_vertices()
                    : 0.0;
  long threshold = (long)mean;
  if (threshold < 1)
    threshold = 1;
  long source = -1;
  for (long v = 0; v < csr.num_vertices() && source < 0; v++)
    if (csr.degree(v) >= threshold)
      source = v;
  if (source < 0) {
    std::fprintf(stderr, "no vertex has out-degree %ld or more\n", threshold);
    return 1;
  }
  std::fprintf(stderr,
               "%s: %ld vertices, %ld edges, mean out-degree %.2f, "
               "max %ld, %ld isolated (%.1f%%)\n",
               mode_name(spec.mode), csr.num_vertices(), csr.num_edges(), mean,
               max_degree, isolated,
               csr.num_vertices() ? 100.0 * isolated / csr.num_vertices() : 0.0);
  std::printf("%ld\n", source);
  return 0;
}

static int do_stat(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "stat needs a file\n");
    return 2;
  }
  GapbsHeader h = gapbs_read_header(argv[2]);
  std::printf("%s: %s, %s, %lld vertices, %lld edges, %lld bytes\n", argv[2],
              h.directed ? "directed" : "undirected",
              h.weighted ? "weighted" : "unweighted", (long long)h.num_nodes,
              (long long)h.num_edges, (long long)h.expected_size());
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: %s gen  <mode 1|2|3> <vertices> <edges|degree> "
                 "<seed> <out.wsg>\n"
                 "       %s csv  <in.csv> <vertices> <seed> <out.wsg>\n"
                 "       %s stat <file.sg|file.wsg>\n"
                 "       %s source <mode 1|2|3> <vertices> <edges|degree> "
                 "<seed>\n",
                 argv[0], argv[0], argv[0], argv[0]);
    return 2;
  }
  std::string command = argv[1];
  if (command == "gen")
    return do_gen(argc, argv);
  if (command == "csv")
    return do_csv(argc, argv);
  if (command == "stat")
    return do_stat(argc, argv);
  if (command == "source")
    return do_source(argc, argv);
  std::fprintf(stderr, "unknown command %s\n", command.c_str());
  return 2;
}
