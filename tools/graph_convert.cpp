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

static int do_gen(int argc, char **argv) {
  if (argc < 7) {
    std::fprintf(stderr, "gen needs <mode> <vertices> <edges|degree> <seed> "
                         "<out.wsg>\n");
    return 2;
  }
  GraphSpec spec;
  spec.mode = atoi(argv[2]);
  spec.num_vertices = atol(argv[3]);
  spec.seed = atoi(argv[5]);
  spec.weights = WeightAssigner(spec.seed);
  if (spec.mode == MODE_RMAT) {
    spec.num_edges = atol(argv[4]);
    if (!rmat_vertex_count_ok(spec.num_vertices)) {
      std::fprintf(stderr, "rmat needs a power-of-two vertex count\n");
      return 2;
    }
  } else {
    spec.average_degree = atol(argv[4]);
  }
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
                 "       %s stat <file.sg|file.wsg>\n",
                 argv[0], argv[0], argv[0]);
    return 2;
  }
  std::string command = argv[1];
  if (command == "gen")
    return do_gen(argc, argv);
  if (command == "csv")
    return do_csv(argc, argv);
  if (command == "stat")
    return do_stat(argc, argv);
  std::fprintf(stderr, "unknown command %s\n", command.c_str());
  return 2;
}
