#include "TopoManager.h"
#include "htram_group.h"
#include "sssp_smp.decl.h"
#include "graphlib/graphlib.h"
// #define PAPI
#ifdef PAPI
#include <papi.h>
#endif
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define INFO_PRINTS
// #define PRINT_HISTO //print histograms to file
#define LOCAL_TO_TRAM // add all outgoing updates (even local) to tram
// #define PQ_HOLD_ONLY
// #define PQ_EDGE_DIST //add cost of smallest edge when finding bucket
// #define VCOUNT
// #define ALL_TO_TRAM_HOLD //place all updates in the tram hold at first

// set data type for messages
using tram_proxy_t = CProxy_HTram;
using tram_t = HTram;

/* readonly */
// tram_proxy_t tram_proxy;
CProxy_HTramRecv nodeGrpProxy;
CProxy_HTramNodeGrp srcNodeGrpProxy;
CProxy_Main mainProxy;
CProxy_SsspChares arr;
CProxy_SharedInfo shared;
int N;                 // number of processors
long V;                // number of vertices
int M = 1024;          // divisor for dest_table (must be power of 2)
long num_global_edges; // number of global edges in the graph (used when graph
                       // is generated)
long average_degree; // average degree of graph
int generate_mode;   // 0 = read from file, 1 = generate automatically
int S;               // seed for randomization
cost lmax;           // long maximum
#define HISTO_BUCKET_COUNT 2048 // needed macro for array init
// Degrees and arrival counts are binned by floor(log2(x)) + 1, with 0 in its
// own bin. 32 covers any count a long can hold that a run could produce.
#define DEGREE_CLASSES 32
int histo_reduction_width = HISTO_BUCKET_COUNT / 8;
double reduction_delay =
    0.1;                   // each histogram reduction happens at this interval
int initial_threshold = 3; // initial histo threshold
bool verify_mode = false;  // --verify: check the result against serial Dijkstra
// Everything needed to rebuild the graph from scratch, used by the serial
// reference. Filled in by Main; not a Charm readonly, because only PE 0 needs
// it and the chares get their slice through their own entry methods.
GraphSpec graph_spec;
// Flush the aggregation buffers once every this many controller rounds, on
// average -- each chare draws independently, so this is a rate and not a
// period. Step 7 of the SC27 plan makes this cadence adaptive; until then it is
// at least a named, reproducible knob rather than a coin flip, and --flush-
// interval makes it an A/B.
//
// It matters more than it looks. htram's own idle-triggered flush is compiled
// out (IDLE_FLUSH at htram_group.h:7; idleFlush() returns true and does
// nothing) and the periodic timer is off by default, so a partly-filled
// aggregation buffer has exactly two ways out: fill to bufSize, or catch one of
// these draws. In the tail there is not enough traffic left to fill anything,
// which is the mechanism H4 of step 6 is about.
int flush_round_interval = 5;
// --timeout <seconds>: abandon a run that has not converged. 0 disables it.
// There is deliberately no default: a truncated run is a failed run, and the
// old behaviour was to give up after 30 s and print the partial distances with
// the same banner and the same exit status as a converged run.
double timeout_seconds = 0.0;
// --bucket-width <w>: distance units per histogram bucket. The width is
// otherwise derived from |V| alone -- log V for the random-graph modes, sqrt V
// for the mesh -- which is exactly what H1 of step 6 puts in question: a rule
// that reads nothing about the distances the graph actually produces. 0 keeps
// the derived rule, so this is inert unless passed.
double bucket_width_override = 0.0;
// --round-delay <ms>: wait this long between the end of one controller round
// and the start of the next. The cycle is otherwise back-to-back -- every chare
// calls contribute_histogram() at the end of current_thresholds() -- so the
// cadence is whatever a reduction plus a broadcast costs, and is not a knob at
// all. 0 keeps that, which is what every measurement so far was taken with.
double round_delay_ms = 0.0;
// --partition-jitter <percent>: how far a PE's share of the vertices may
// deviate from V/N in the uniform mode, which draws its partition sizes at
// random rather than dividing evenly.
//
// This is not a cosmetic option. Mode 1 is the only mode that does it: the mesh
// and RMAT both hand each PE exactly V/N vertices. So a comparison of load
// balance between the uniform graph and a scale-free one is, at the default,
// comparing a deliberately skewed partition against an even one. H3 of step 6
// is that comparison, so it runs every mode at 0. The default stays 20 because
// that is what every measurement to date used and what the golden digests were
// recorded with.
//
// The digests do not move either way. A uniform graph's adjacency is a function
// of the global vertex id alone, so which PE owns a vertex changes who
// generates it and nothing about what is generated -- which is also why the
// gate can require the same digest at ppn 1 and ppn 4.
int partition_jitter_percent = 20;
// --diag <prefix>: write the controller's own time series to
// <prefix>.rounds.csv, and in the ACIC_DIAG build the bucket, vertex-count,
// degree and arrival profiles beside it. Empty disables all of them.
// design/scale-free-diagnosis.md lists what each file holds.
std::string diag_prefix;
// tram constants
// Aggregation buffer size in items, settable with --bufsize. htram used to
// accept this and then ignore it, so changing the buffer size meant editing
// BUFSIZE in htram_group.h and rebuilding both libraries; the default here is
// BUFSIZE so the out-of-the-box configuration is unchanged.
int buffer_size = BUFSIZE;
double flush_timer = 0.01; // milliseconds
bool enable_buffer_flushing =
    false; // true = buffer flushes at interval specified by flush_timer
tram_proxy_t tram_proxy;

void fast_exit(void *obj, double time);

/**
 * Layout of the msg_stats reduction that print_distances() sends to done().
 * The array grew by accretion with its indices open-coded at both ends, as
 * `3 + HISTO_BUCKET_COUNT` and friends; naming them is the only way to append
 * to it without arithmetic errors that a reduction would never report.
 */
enum {
  STAT_WASTED = 0,
  STAT_REJECTED,
  STAT_VCOUNT, // HISTO_BUCKET_COUNT + 1 entries: one per bucket, plus infinity
  STAT_NOTED = STAT_VCOUNT + HISTO_BUCKET_COUNT + 1,
  STAT_EDGES,
  STAT_DISTANCE_CHANGES,
  STAT_GRAPH_BYTES,
  STAT_INSTRUCTIONS,  // PAPI builds only
  STAT_BATCH_ITEMS,   // ACIC_DIAG builds only, from here down
  STAT_BATCH_ABSORBABLE,
  STAT_HISTO_CREATED, // HISTO_BUCKET_COUNT entries
  // Vertices binned by out-degree, and the traffic that lands on each bin.
  // H2 is a claim about *where* the redundant updates go, not how many there
  // are, and a total cannot answer it.
  STAT_DEG_VERTICES = STAT_HISTO_CREATED + HISTO_BUCKET_COUNT,
  STAT_DEG_EDGES = STAT_DEG_VERTICES + DEGREE_CLASSES,
  STAT_DEG_ARRIVALS = STAT_DEG_EDGES + DEGREE_CLASSES,
  STAT_DEG_REJECTS = STAT_DEG_ARRIVALS + DEGREE_CLASSES,
  // The same traffic binned by how much of it each vertex received, which is
  // the concentration figure: what share of all arrivals lands on what share
  // of the vertices. That is what a combining table has to exploit.
  STAT_ARR_VERTICES = STAT_DEG_REJECTS + DEGREE_CLASSES,
  STAT_ARR_ARRIVALS = STAT_ARR_VERTICES + DEGREE_CLASSES,
  STAT_END = STAT_ARR_ARRIVALS + DEGREE_CLASSES
};

#ifdef ACIC_DIAG
const int stat_count = STAT_END;
#elif defined(PAPI)
const int stat_count = STAT_INSTRUCTIONS + 1;
#else
const int stat_count = STAT_INSTRUCTIONS;
#endif

void start_reductions(void *obj, double time) { arr.contribute_histogram(0); }

struct ComparePairs {
  bool operator()(const Update &lhs, const Update &rhs) const {
    // Compare the second integers of the pairs
    return lhs.distance > rhs.distance; // '>' for min heap, '<' for max heap
  }
};

struct histoInstance {
public:
  int fnz;
  int width;
  int *reducedValues;
};

class histogramSequence {
private:
  int maxBuckets;
  std::vector<histoInstance> histos;

public:
  histogramSequence(int _maxBuckets) {
    maxBuckets = _maxBuckets;
    histoInstance h;
    h.fnz = 0;
    h.width = 10;
    h.reducedValues = new int[10];
    histos.push_back(h);
  }

  void insert(int fnz, int width, long *histo) {
    histoInstance h;
    h.fnz = fnz;
    h.width = width;
    h.reducedValues = new int[width];
    for (int i = 0; i < width; i++)
      h.reducedValues[i] = histo[i];
    histos.push_back(h);
  }

  void putout() {
    // using cout instead of ckout to avoid buffer overflow. (should be output
    // to a file)
    std::ofstream out_file;
    out_file.open("histos.txt");
    for (int i = 0; i < histos.size(); i++) {
      for (int j = 0; j < histos[i].fnz; j++)
        out_file << "0 ";
      for (int j = 0; j < histos[i].width; j++)
        out_file << histos[i].reducedValues[j] << " ";
      for (int j = 0; j < (maxBuckets - histos[i].fnz - histos[i].width); j++)
        out_file << "0 ";
      out_file << endl;
    }
  }
};

/**
 * One row of the controller's own time series, kept by Main and written out at
 * the end by --diag. Everything here is already computed inside
 * reduce_histogram(); recording it costs a push_back on PE 0 per round and
 * nothing at all on the worker path, which is what makes it safe to leave in
 * the timed build.
 *
 * `occupied` and `span` are the measurement H1 turns on. The reduction only
 * carries a window of histo_reduction_width buckets, so both describe the
 * window rather than the whole 2048-bucket range -- but the window is also
 * exactly what the percentile cut has to work with, so it is the right
 * denominator for asking whether the controller has any resolution to use.
 */
struct RoundRecord {
  double t;      // seconds since compute_begin
  long histogram_sum;
  int window_first;  // bucket index the reduced window starts at
  int first_nonzero; // the frontier: lowest bucket still holding work
  int occupied;      // buckets inside the window holding a positive count
  int span;          // last occupied bucket - first occupied + 1, or 0
  int heap_threshold;
  int tram_threshold;
  long updates_created;
  long updates_processed;
  long updates_noted;
  long distance_changes;
  long done_vertices;
};

class Main : public CBase_Main {
private:
  long start_vertex;
  long *partition_index;
  double start_time;
  double read_time;
  double total_time;
  long max_index;
  int threshold_change_counter;
  int previous_threshold;
  int reduction_counts = 0;
  int no_incoming = 0;
  std::vector<double> reduction_times;
  std::vector<RoundRecord> rounds; // --diag; see RoundRecord
  bool first_qd_done = false;
  bool second_qd_done = false;
  int activeBucketMax = 10;
  int current_phase = 0; // 0=initial, 1=bfs, 2=converged_bfs
  int last_first_nonzero = 0;
  long previous_updates_created = 0;
  long previous_updates_processed = 0;
  long previous_distance_changes = 0;
  double tram_percentile = 0.01;
  double heap_percentile = 0.01;
#ifdef PRINT_HISTO
  histogramSequence *histoSeq;
#endif

public:
  double compute_begin;
  double compute_time;
  bool run_truncated = false; // set by fast_exit; forces a nonzero exit

  /**
   * Read in graph from csv (currently sequential)
   */
  Main(CkArgMsg *m) {
    N = CkNumPes();
    // Separate option flags from the positional arguments so flags may appear
    // anywhere on the command line.
    std::vector<std::string> args;
    for (int i = 1; i < m->argc; i++) {
      if (m->argv[i] == NULL)
        continue;
      std::string arg = m->argv[i];
      if (arg == "--verify")
        verify_mode = true;
      else if (arg.rfind("--timeout=", 0) == 0)
        timeout_seconds = std::stod(arg.substr(10));
      else if (arg == "--timeout") {
        if (i + 1 >= m->argc) {
          ckout << "--timeout needs a value in seconds" << endl;
          CkExit(1);
          return;
        }
        timeout_seconds = std::stod(m->argv[++i]);
      } else if (arg.rfind("--bufsize=", 0) == 0) {
        buffer_size = std::stoi(arg.substr(10));
      } else if (arg == "--bufsize") {
        if (i + 1 >= m->argc) {
          ckout << "--bufsize needs a value in items" << endl;
          CkExit(1);
          return;
        }
        buffer_size = std::stoi(m->argv[++i]);
      } else if (arg.rfind("--bucket-width=", 0) == 0) {
        bucket_width_override = std::stod(arg.substr(15));
      } else if (arg == "--bucket-width") {
        if (i + 1 >= m->argc) {
          ckout << "--bucket-width needs a width in distance units" << endl;
          CkExit(1);
          return;
        }
        bucket_width_override = std::stod(m->argv[++i]);
      } else if (arg.rfind("--round-delay=", 0) == 0) {
        round_delay_ms = std::stod(arg.substr(14));
      } else if (arg == "--round-delay") {
        if (i + 1 >= m->argc) {
          ckout << "--round-delay needs a value in milliseconds" << endl;
          CkExit(1);
          return;
        }
        round_delay_ms = std::stod(m->argv[++i]);
      } else if (arg.rfind("--partition-jitter=", 0) == 0) {
        partition_jitter_percent = std::stoi(arg.substr(19));
      } else if (arg == "--partition-jitter") {
        if (i + 1 >= m->argc) {
          ckout << "--partition-jitter needs a percentage" << endl;
          CkExit(1);
          return;
        }
        partition_jitter_percent = std::stoi(m->argv[++i]);
      } else if (arg.rfind("--flush-interval=", 0) == 0) {
        flush_round_interval = std::stoi(arg.substr(17));
      } else if (arg == "--flush-interval") {
        if (i + 1 >= m->argc) {
          ckout << "--flush-interval needs a number of controller rounds"
                << endl;
          CkExit(1);
          return;
        }
        flush_round_interval = std::stoi(m->argv[++i]);
      } else if (arg.rfind("--diag=", 0) == 0) {
        diag_prefix = arg.substr(7);
      } else if (arg == "--diag") {
        if (i + 1 >= m->argc) {
          ckout << "--diag needs an output path prefix" << endl;
          CkExit(1);
          return;
        }
        diag_prefix = m->argv[++i];
      } else if (arg.rfind("--", 0) == 0) {
        ckout << "Unknown option " << arg.c_str() << endl;
        CkExit(1);
        return;
      } else
        args.push_back(arg);
    }
    if (args.size() < 7) {
      ckout << "Usage: sssp_smp <vertices> <path|edge count> <seed> "
            << "<start vertex> "
            << "<mode 0=csv,1=uniform,2=mesh,3=rmat,4=gapbs> "
            << "<tram percentile> <heap percentile> "
            << "[--verify] [--timeout <seconds>] [--bufsize <items>]" << endl
            << "       [--bucket-width <distance units>] "
            << "[--round-delay <ms>] [--flush-interval <rounds>] "
            << "[--partition-jitter <percent>] [--diag <prefix>]" << endl
            << "  mode 3 takes the edge count in argument 2 and needs a "
            << "power-of-two vertex count." << endl
            << "  mode 4 takes a GAPBS .sg or .wsg path in argument 2; the "
            << "vertex count is read from the file." << endl
            << "  mode 0 reads a comma-separated edge list serially on PE 0 "
            << "and is kept only for the graphs/ directory; convert those to "
            << ".wsg with tools/graph_convert." << endl;
      CkExit(1);
      return;
    }
    if (partition_jitter_percent < 0 || partition_jitter_percent > 100) {
      ckout << "--partition-jitter must be a percentage in 0..100" << endl;
      CkExit(1);
      return;
    }
    if (flush_round_interval < 1) {
      ckout << "--flush-interval must be at least 1 (flush every round)"
            << endl;
      CkExit(1);
      return;
    }
    if (bucket_width_override < 0.0) {
      ckout << "--bucket-width must be positive" << endl;
      CkExit(1);
      return;
    }
    if (buffer_size <= 0 || buffer_size > BUFSIZE) {
      ckout << "--bufsize must be in 1.." << BUFSIZE << endl;
      CkExit(1);
      return;
    }
    V = atol(args[0].c_str());          // number of vertices
    std::string file_name = args[1];    // file name or edge count
    S = atoi(args[2].c_str());          // randomization seed
    start_vertex = atol(args[3].c_str());
    generate_mode = atoi(args[4].c_str()); // 0 read from csv, 1/2 generate
    tram_percentile = std::stod(args[5]);
    heap_percentile = std::stod(args[6]);
    if (verify_mode && generate_mode == MODE_CSV) {
      ckout << "--verify does not cover mode 0. The csv reader builds the "
            << "graph inside Main rather than from a GraphSpec, so there is "
            << "nothing for the reference solver to rebuild independently. "
            << "Convert the file to .wsg and use mode 4." << endl;
      CkExit(1);
      return;
    }
    // Everything the reference solver needs to rebuild the graph on its own.
    graph_spec.mode = generate_mode;
    graph_spec.seed = S;
    graph_spec.weights = WeightAssigner(S);
    if (generate_mode == MODE_GAPBS)
      graph_spec.path = file_name;
#ifdef PRINT_HISTO
    histoSeq = new histogramSequence(HISTO_BUCKET_COUNT);
#endif
    // create TRAM proxy
    nodeGrpProxy = CProxy_HTramRecv::ckNew();
    srcNodeGrpProxy = CProxy_HTramNodeGrp::ckNew();
    CkCallback ignore_cb(CkCallback::ignore);
    tram_proxy = tram_proxy_t::ckNew(nodeGrpProxy.ckGetGroupID(),
                                     srcNodeGrpProxy.ckGetGroupID(),
                                     buffer_size, enable_buffer_flushing,
                                     flush_timer, false, true, ignore_cb);
    shared = CProxy_SharedInfo::ckNew();
    arr = CProxy_SsspChares::ckNew(tram_proxy, N);
    mainProxy = thisProxy;
    arr.initiate_pointers();
    partition_index = new long[N + 1]; // last index=maximum index
    lmax = std::numeric_limits<cost>::max();
    start_time = CkWallTimer();
    if (generate_mode == MODE_MESH) {
      long side_length = mesh_side_length(V);
#ifdef INFO_PRINTS
      ckout << "Side length: " << side_length << endl;
#endif
      num_global_edges =
          4 * side_length * (side_length - 1); // will ignore command line input
#ifdef INFO_PRINTS
      ckout << "2-D Graph will be automatically generated with " << V
            << " vertices and " << num_global_edges << " edges" << endl;
#endif
      for (int i = 0; i < N + 1; i++) {
        partition_index[i] = i * (V / N);
        if (i == N)
          partition_index[i] = V;
      }
      graph_spec.num_vertices = V;
      graph_spec.num_edges = num_global_edges;
      arr.generate_2d_graph(partition_index, N + 1);
    } else if (generate_mode == MODE_UNIFORM) {
      num_global_edges = std::stol(file_name);
#ifdef INFO_PRINTS
      ckout << "Graph will be automatically generated with " << V << " vertices"
            << endl;
#endif
      average_degree = num_global_edges / V;
      // for each pe, generate a random vertex and edge count, and send to pes
      long remaining_vertices = V;
      long current_start_index = 0; // tracks start vertex for indices
      // Partition sizes vary by +-partition-jitter percent, 20 by default.
      // Drawn with the same portable generator as the graph itself, so a run
      // has the same load balance on every machine -- <random> would not give
      // that. See graphlib/rng.h.
      //
      // Integer arithmetic throughout, because 80V/100N and 4V/5N are the same
      // rational and so floor to the same value: at the default this draws the
      // identical partition the previous code did.
      VertexRng partition_rng(-1, S);
      const long low_pct = 100 - partition_jitter_percent;
      const long high_pct = 100 + partition_jitter_percent;
      long vertex_low = (V * low_pct) / (N * 100);
      long vertex_span = ((V * high_pct) / (N * 100)) - vertex_low + 1;
      long edge_low = (num_global_edges * low_pct) / (N * 100);
      long edge_span =
          ((num_global_edges * high_pct) / (N * 100)) - edge_low + 1;
      long *vertex_counts = new long[N];
      long *edge_counts = new long[N];
      for (int i = 0; i < N; i++) {
        partition_index[i] = current_start_index;
        long vertex_count =
            vertex_low + (long)partition_rng.bounded((uint64_t)vertex_span);
        long edge_count =
            edge_low + (long)partition_rng.bounded((uint64_t)edge_span);
        if ((i == N - 1) || (vertex_count > remaining_vertices))
          vertex_count = remaining_vertices; // make sure num_vertices = V
        remaining_vertices -= vertex_count;
        vertex_counts[i] = vertex_count;
        edge_counts[i] = edge_count;
        current_start_index += vertex_count;
      }
      partition_index[N] = V;
#ifdef INFO_PRINTS
      ckout << "Partition index: [";
      for (int i = 0; i < N + 1; i++) {
        ckout << partition_index[i] << ", ";
      }
      ckout << "]" << endl;
#endif
      graph_spec.num_vertices = V;
      graph_spec.num_edges = num_global_edges;
      graph_spec.average_degree = average_degree;
      for (int i = 0; i < N; i++) {
        arr[i].generate_local_graph(vertex_counts[i], edge_counts[i],
                                    partition_index, N + 1);
      }
    } else if (generate_mode == MODE_RMAT) {
      // RMAT is edge-indexed: edge i is a pure function of (i, seed), but its
      // source vertex is an output of the draw, so a PE cannot generate its own
      // rows. Each PE generates a contiguous slice of the edge index space and
      // routes what it produces to the owners. See graphlib/edge_source.h.
      if (!rmat_vertex_count_ok(V)) {
        ckout << "rmat needs a power-of-two vertex count; " << V
              << " is not one. The generator descends one bit of the vertex id "
              << "per level, so the vertex space is 2^scale by construction."
              << endl;
        CkExit(1);
        return;
      }
      num_global_edges = std::stol(file_name);
      average_degree = num_global_edges / V;
#ifdef INFO_PRINTS
      ckout << "RMAT graph, scale " << rmat_scale(V) << ", " << V
            << " vertices and " << num_global_edges << " edges" << endl;
#endif
      // Equal vertex ranges. RMAT degrees are wildly unequal, so this is not a
      // balanced partition -- but the label permutation scatters the hubs, so
      // it is not systematically skewed either. Balancing it properly is
      // hypothesis H3 in step 6, and needs the partitioning to be measured
      // before it is changed.
      for (int i = 0; i < N + 1; i++)
        partition_index[i] = (i == N) ? V : i * (V / N);
      graph_spec.num_vertices = V;
      graph_spec.num_edges = num_global_edges;
      graph_spec.average_degree = average_degree;
      arr.generate_rmat_graph(partition_index, N + 1);
      // Nothing knows how many edges will arrive where, so completion is
      // detected rather than counted: an all-to-all of per-destination counts
      // would cost N^2 messages before the first edge moved.
      CkStartQD(CkCallback(CkIndex_Main::rmat_edges_distributed(), mainProxy));
    } else if (generate_mode == MODE_GAPBS) {
      // The file is already CSR ordered by source vertex, so each PE seeks to
      // its own rows and reads only those: no exchange and no parsing.
      GapbsHeader header = gapbs_read_header(file_name);
      V = header.num_nodes;
      num_global_edges = header.num_edges;
      average_degree = num_global_edges / (V > 0 ? V : 1);
#ifdef INFO_PRINTS
      ckout << "Reading " << file_name.c_str() << ": " << V << " vertices, "
            << num_global_edges << (header.directed ? " directed" : " undirected")
            << " edges, " << (header.weighted ? "weighted" : "unweighted")
            << endl;
#endif
      // Partition on equal edge counts rather than equal vertex counts. The
      // offsets array is the whole cost of knowing this, and it has to be read
      // anyway. A real graph's degree distribution makes equal vertex ranges a
      // bad split; this is the one place the information is free.
      std::vector<int64_t> offsets;
      gapbs_read_offsets(file_name, header, 0, V, offsets);
      // At least one, so an edgeless graph still spreads its vertices instead
      // of handing every one of them to the last PE.
      long per_pe = (num_global_edges + N - 1) / (N > 0 ? N : 1);
      if (per_pe < 1)
        per_pe = 1;
      long vertex = 0;
      for (int i = 0; i < N; i++) {
        partition_index[i] = vertex;
        long target = (long)offsets[(size_t)vertex] + per_pe;
        while (vertex < V && (long)offsets[(size_t)vertex] < target &&
               V - vertex > N - i - 1)
          vertex++;
      }
      partition_index[N] = V;
      graph_spec.num_vertices = V;
      graph_spec.num_edges = num_global_edges;
      arr.load_gapbs_graph(file_name, partition_index, N + 1);
    } else {
#ifdef INFO_PRINTS
      ckout << "Graph will be read from file" << endl;
#endif
      // read file
      std::ifstream file(file_name);
      std::string readbuf;
      std::string delim = ",";
      // iterate through edge list
      CkVec<LongEdge> edges;
      int *incoming_count =
          new int[V]; // how many times does each vertex appear in the edge list
      for (int i = 0; i < V; i++)
        incoming_count[i] = 0;
      max_index = 0;       // maximum vertex index
      long edges_read = 0; // number of edges read
      while (getline(file, readbuf)) {
        // get nodes on each edge
        std::string token = readbuf.substr(0, readbuf.find(delim));
        std::string token2 =
            readbuf.substr(readbuf.find(delim) + 1, readbuf.length());
        // string to int
        long node_num = std::stol(token);    // v
        long node_num_2 = std::stol(token2); // w
        // Weight is a hash of the endpoint pair, so it does not depend on the
        // order edges are read in. See graphlib/weights.h.
        cost edge_distance = edge_weight(node_num, node_num_2, S);
        incoming_count[node_num_2]++;
        // find the maximum vertex index
        if (node_num > max_index)
          max_index = node_num;
        if (node_num_2 > max_index)
          max_index = node_num_2;
        LongEdge new_edge;
        new_edge.begin = node_num;
        new_edge.end = node_num_2;
        new_edge.distance = edge_distance;
        edges.insertAtEnd(new_edge);
        edges_read++;
        // if(edges_read%10000000==0) ckout << "Read " << edges_read << " edges"
        // << endl;
        //  ckout << "One loop iteration complete" << endl;
      }
      average_degree = edges_read / V;
      for (int i = 0; i < max_index; i++) {
        if (incoming_count[i] == 0)
          no_incoming++;
      }
#ifdef INFO_PRINTS
      ckout << "Vertices with no incoming edges: " << no_incoming << endl;
      ckout << "Max index: " << max_index << endl;
#endif
      // ckout << "Loop complete" << endl;
      file.close();
      read_time = CkWallTimer() - start_time;
      // assign nodes to location
      std::vector<LongEdge> *edge_lists = new std::vector<LongEdge>[N];
      long average = edges.size() / N;
      for (int i = 0; i < edges.size(); i++) {
        int dest_proc = i / average;
        if (dest_proc >= N)
          dest_proc = N - 1;
        else if (i % average == 0)
          partition_index[dest_proc] = edges[i].begin;
        edge_lists[dest_proc].insert(edge_lists[dest_proc].end(), edges[i]);
      }
      partition_index[N] = max_index + 1;
      // reassign edges to move to correct pe
      for (int i = 0; i < N - 1; i++) {
        for (int j = edge_lists[i].size() - 1; j >= 0; --j) {
          // TODO
          if (edge_lists[i][j].begin >= partition_index[i + 1]) {
            edge_lists[i + 1].insert(edge_lists[i + 1].begin(),
                                     edge_lists[i][j]);
            edge_lists[i].erase(edge_lists[i].begin() + j);
          }
        }
      }
      // add nodes to node lists
      // send subgraphs to nodes
      for (int i = 0; i < N; i++) {
        arr[i].get_graph(edge_lists[i].data(), edge_lists[i].size(),
                         partition_index, N + 1);
      }
    }
  }

  /**
   * Quiescence after the RMAT exchange: every edge any PE generated has been
   * delivered to the PE that owns its source vertex, so the rows can be built.
   */
  void rmat_edges_distributed() { arr.build_rmat_csr(); }

  /**
   * Start algorithm from source vertex
   */
  void begin(cost max_sum) {
    // ready to begin algorithm
    shared.max_path_value(max_sum);
    if (generate_mode == 1 || generate_mode == 2)
      read_time = CkWallTimer() - start_time;
#ifdef INFO_PRINTS
    ckout << "The sum of the maximum out-edges is " << max_sum << endl;
#endif
    Update new_edge;
    new_edge.dest_vertex = start_vertex;
    new_edge.distance = 0;
    int dest_proc = 0;
    for (int i = 0; i < N; i++) {
      if (start_vertex >= partition_index[i] &&
          start_vertex < partition_index[i + 1]) {
        dest_proc = i;
        break;
      }
      if (i == N - 1)
        dest_proc = N - 1;
    }
    threshold_change_counter = 0;
    previous_threshold = initial_threshold;
    CcdCallFnAfter(start_reductions, (void *)this, reduction_delay);
    if (timeout_seconds > 0.0)
      CcdCallFnAfter(fast_exit, (void *)this, timeout_seconds * 1000.0);
    compute_begin = CkWallTimer();
#ifdef INFO_PRINTS
    ckout << "Beginning at time: " << compute_begin << endl;
#endif
    arr.start_papi();
    arr[dest_proc].start_algo(new_edge);
  }

  void record_round(double now, long histogram_sum, int first_nonzero,
                    int occupied, int span, int heap_threshold,
                    int tram_threshold,
                    long updates_created, long updates_processed,
                    long updates_noted, long distance_changes,
                    long done_vertices) {
    if (diag_prefix.empty())
      return;
    RoundRecord r;
    r.t = now - compute_begin;
    r.histogram_sum = histogram_sum;
    r.window_first = last_first_nonzero;
    r.first_nonzero = first_nonzero;
    r.occupied = occupied;
    r.span = span;
    r.heap_threshold = heap_threshold;
    r.tram_threshold = tram_threshold;
    r.updates_created = updates_created;
    r.updates_processed = updates_processed;
    r.updates_noted = updates_noted;
    r.distance_changes = distance_changes;
    r.done_vertices = done_vertices;
    rounds.push_back(r);
  }

  /**
   * Receive histo values from pes
   * The idea is to get the distribution of update values, then
   * do local processing to select thresholds
   */
  void reduce_histogram(long *histo_values, int histo_length) {
    reduction_times.push_back(CkWallTimer());
    reduction_counts++;
    long histogram_sum = 0;
    int first_nonzero = -1;
    long updates_processed = histo_values[histo_reduction_width + 1];
    long updates_created = histo_values[histo_reduction_width];
    long bfs_processed = histo_values[histo_reduction_width + 2];
    long done_vertex_count = histo_values[histo_reduction_width + 3];
    long updates_noted = histo_values[histo_reduction_width + 4];
    long bfs_noted = histo_values[histo_reduction_width + 5];
    long distance_changes = histo_values[histo_reduction_width + 6];
    int heap_threshold = 0;
    int tram_threshold = 0;
    int bfs_threshold = heap_threshold;
    long active_counter = 0;
    int occupied = 0;      // buckets in the window with a positive count
    int window_last = -1;  // index of the last of them, within the window
    // calculate the total histogram sum
    for (int i = 0; i < histo_reduction_width; i++) {
      histogram_sum += histo_values[i];
      if (histo_values[i] > 0) {
        occupied++;
        window_last = i;
        if (first_nonzero == -1) {
          first_nonzero = i + last_first_nonzero;
        }
      }
    }
    int span = (first_nonzero == -1)
                   ? 0
                   : window_last - (first_nonzero - last_first_nonzero) + 1;
#ifdef PRINT_HISTO
    histoSeq->insert(last_first_nonzero, histo_reduction_width, histo_values);
#endif
#ifdef INFO_PRINTS
    ckout << "Updates: created: " << updates_created
          << ", noted: " << updates_noted
          << ", processed: " << updates_processed
          << ", distance changes: " << distance_changes
          << ", Done vertices: " << done_vertex_count
          << ", BFS noted: " << bfs_noted;
#endif
    if ((updates_processed - updates_created == 1) &&
        (updates_created > 1000) &&
        (updates_created == previous_updates_created) &&
        (updates_processed == previous_updates_processed)) {
      record_round(CkWallTimer(), histogram_sum, first_nonzero, occupied, span,
                   -1, -1, updates_created, updates_processed, updates_noted,
                   distance_changes, done_vertex_count);
      ckout << endl << "updates_processed and updates_created match" << endl;
#ifdef INFO_PRINTS
      ckout << "Threshold: " << previous_threshold << endl;
#endif
      compute_time = CkWallTimer() - compute_begin;
      arr.print_distances();
      return;
    }
    previous_updates_created = updates_created;
    previous_updates_processed = updates_processed;
    // calculate target percentile
    double heap_percent; // heap percentage
    double tram_percent; // tram percentage
    if (histogram_sum <= N * 100) {
      heap_percent = 0.9999;
      tram_percent = 0.9999;
    } else {
      heap_percent = heap_percentile;
      tram_percent = tram_percentile;
    }
    previous_distance_changes = distance_changes;
    previous_updates_processed = updates_processed;
    // select bucket limit
    for (int i = 0; i < histo_reduction_width; i++) {
      active_counter += histo_values[i];
      if ((double)active_counter >= histogram_sum * heap_percent) {
        heap_threshold = i + last_first_nonzero;
        break;
      }
    }
    bfs_threshold = heap_threshold;
    active_counter = 0;
    for (int i = 0; i < histo_reduction_width; i++) {
      active_counter += histo_values[i];
      if ((double)active_counter >= histogram_sum * tram_percent) {
        tram_threshold = i + last_first_nonzero;
        break;
      }
    }
    // in case of floating point weirdness
    if (heap_threshold >= HISTO_BUCKET_COUNT)
      heap_threshold = HISTO_BUCKET_COUNT - 1;
    if (tram_threshold >= HISTO_BUCKET_COUNT)
      tram_threshold = HISTO_BUCKET_COUNT - 1;
    if (histogram_sum == 0) {
      heap_threshold = HISTO_BUCKET_COUNT - 1;
      tram_threshold = HISTO_BUCKET_COUNT - 1;
      bfs_threshold = HISTO_BUCKET_COUNT - 1;
    }
    if (heap_threshold != previous_threshold) {
      previous_threshold = heap_threshold;
      threshold_change_counter++;
    }
#ifdef INFO_PRINTS
    ckout << ", Heap threshold: " << heap_threshold
          << ", Tram: " << tram_threshold
          << ", BFS threshold: " << bfs_threshold
          << ", first nonzero: " << first_nonzero << ", t= " << CkWallTimer()
          << endl;
#endif
    if (first_nonzero == -1) {
      // Nothing in the reduced window: either the run has converged or all the
      // remaining work sits past the window's right edge. Either way the window
      // stays put. Letting -1 through here propagates to
      // contribute_histogram(-2), which then reads histogram[-1] on every PE
      // and sums that garbage into the next global histogram.
      first_nonzero = last_first_nonzero;
    }
    // Recorded before the -1 is folded away below, so a round whose window
    // held nothing reads as exactly that rather than as a window that happened
    // not to move. heap_threshold - first_nonzero is the controller's actual
    // dynamic range for the round, which is what H1 is really asking about.
    record_round(CkWallTimer(), histogram_sum, first_nonzero, occupied, span,
                 heap_threshold, tram_threshold, updates_created,
                 updates_processed, updates_noted, distance_changes,
                 done_vertex_count);
    // arr.contribute_histogram(first_nonzero-1);
    last_first_nonzero = first_nonzero;
    arr.current_thresholds(heap_threshold, tram_threshold, bfs_threshold,
                           first_nonzero - 1, current_phase);

    // start next reduction round
    // CcdCallFnAfter(start_reductions, (void *) this, reduction_delay);
  }

  /**
   * --diag output. Written once, at the end, from PE 0 -- nothing here is on a
   * timed path. The round series comes from Main's own record; the bucket
   * profile is a cumulative per-bucket creation count reduced over the PEs,
   * which exists only in the ACIC_DIAG build because keeping it costs an extra
   * increment per relaxation.
   */
  void write_diag_files(long *msg_stats) {
    if (diag_prefix.empty())
      return;
    std::string rounds_path = diag_prefix + ".rounds.csv";
    std::ofstream out(rounds_path.c_str());
    out << "round,t,histogram_sum,window_first,first_nonzero,occupied,span,"
           "heap_threshold,tram_threshold,updates_created,updates_processed,"
           "updates_noted,distance_changes,done_vertices\n";
    for (size_t i = 0; i < rounds.size(); i++) {
      const RoundRecord &r = rounds[i];
      out << i << ',' << r.t << ',' << r.histogram_sum << ','
          << r.window_first << ',' << r.first_nonzero << ',' << r.occupied
          << ',' << r.span << ',' << r.heap_threshold << ','
          << r.tram_threshold << ','
          << r.updates_created << ',' << r.updates_processed << ','
          << r.updates_noted << ',' << r.distance_changes << ','
          << r.done_vertices << '\n';
    }
    ckout << "DIAG wrote " << rounds.size() << " rounds to "
          << rounds_path.c_str() << endl;
#ifdef ACIC_DIAG
    std::string buckets_path = diag_prefix + ".buckets.csv";
    std::ofstream buckets(buckets_path.c_str());
    buckets << "bucket,created\n";
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++)
      buckets << i << ',' << msg_stats[STAT_HISTO_CREATED + i] << '\n';
    ckout << "DIAG wrote " << HISTO_BUCKET_COUNT << " buckets to "
          << buckets_path.c_str() << endl;

    std::string degree_path = diag_prefix + ".degree.csv";
    std::ofstream degree_out(degree_path.c_str());
    degree_out << "class,min_degree,vertices,out_edges,arrivals,rejects\n";
    for (int i = 0; i < DEGREE_CLASSES; i++)
      degree_out << i << ',' << (i == 0 ? 0 : (1L << (i - 1))) << ','
                 << msg_stats[STAT_DEG_VERTICES + i] << ','
                 << msg_stats[STAT_DEG_EDGES + i] << ','
                 << msg_stats[STAT_DEG_ARRIVALS + i] << ','
                 << msg_stats[STAT_DEG_REJECTS + i] << '\n';

    std::string arrivals_path = diag_prefix + ".arrivals.csv";
    std::ofstream arrivals_out(arrivals_path.c_str());
    arrivals_out << "class,min_arrivals,vertices,arrivals\n";
    for (int i = 0; i < DEGREE_CLASSES; i++)
      arrivals_out << i << ',' << (i == 0 ? 0 : (1L << (i - 1))) << ','
                   << msg_stats[STAT_ARR_VERTICES + i] << ','
                   << msg_stats[STAT_ARR_ARRIVALS + i] << '\n';
    ckout << "DIAG wrote degree and arrival profiles to "
          << degree_path.c_str() << " and " << arrivals_path.c_str() << endl;
#endif
#ifdef VCOUNT
    // Vertices whose tentative distance currently falls in each bucket, plus
    // one trailing row for the unreached. The controller's done_vertices
    // column is a prefix sum of this, which is why it reads 0 in a build
    // without VCOUNT.
    std::string vcount_path = diag_prefix + ".vcount.csv";
    std::ofstream vcount_out(vcount_path.c_str());
    vcount_out << "bucket,vertices\n";
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++)
      vcount_out << i << ',' << msg_stats[STAT_VCOUNT + i] << '\n';
    vcount_out << "unreached," << msg_stats[STAT_VCOUNT + HISTO_BUCKET_COUNT]
               << '\n';
    ckout << "DIAG wrote vertex counts to " << vcount_path.c_str() << endl;
#endif
  }

  void done(long *msg_stats, int N) {
    // ends program, prints that program is ended
    // ckout << "Completed" << endl;
    // CkPrintf("Memory usage at end: %f\n", CmiMemoryUsage()/(1024.0*1024.0));
    total_time = CkWallTimer() - start_time;
    ckout << "Actual edges: " << msg_stats[STAT_EDGES] << endl;
    ckout << "Read time: " << read_time << endl;
    ckout << "Compute time: " << compute_time << endl;
    ckout << "Total time: " << total_time << endl;
    ckout << "Wasted updates: " << msg_stats[STAT_WASTED] - V << endl;
    ckout << "Wasted updates normalized to |E|: "
          << (double)(msg_stats[STAT_WASTED] - V) / msg_stats[STAT_EDGES]
          << endl;
    ckout << "Rejected updates: " << msg_stats[STAT_REJECTED] << endl;
    ckout << "Rejected updates normalized to |E|: "
          << (double)msg_stats[STAT_REJECTED] / msg_stats[STAT_EDGES] << endl;
    ckout << "Number of threshold changes: " << threshold_change_counter
          << endl;
    ckout << "Number of reductions: " << reduction_counts << endl;
    ckout << "Updates noted: " << msg_stats[STAT_NOTED] << endl;
    ckout << "Distance changes: " << msg_stats[STAT_DISTANCE_CHANGES]
          << ", per vertex: " << msg_stats[STAT_DISTANCE_CHANGES] * 1.0 / V
          << endl;
#ifdef VCOUNT
    long vcount_sum = 0;
    for (int i = 0; i < HISTO_BUCKET_COUNT + 1; i++)
      vcount_sum += msg_stats[STAT_VCOUNT + i];
    ckout << "Vcount sum: " << vcount_sum << endl;
#endif
    ckout << "Graph bytes: " << msg_stats[STAT_GRAPH_BYTES] << ", per edge: "
          << msg_stats[STAT_GRAPH_BYTES] * 1.0 / msg_stats[STAT_EDGES] << endl;
#ifdef PAPI
    ckout << "Total insts: " << msg_stats[STAT_INSTRUCTIONS] << endl;
    ckout << "Insts per edge: "
          << msg_stats[STAT_INSTRUCTIONS] * 1.0 / msg_stats[STAT_EDGES] << endl;
#endif
#ifdef ACIC_DIAG
    // The receiver-side half of the combining ceiling: how much of the traffic
    // a fold inside the delivery callback could have collapsed. Reported
    // against rejected updates, which bound what *any* combining scheme could
    // remove, so the gap between the two is the part that needs a hold living
    // longer than one batch.
    ckout << "Batch items: " << msg_stats[STAT_BATCH_ITEMS]
          << ", absorbable within a batch: " << msg_stats[STAT_BATCH_ABSORBABLE]
          << " (" << 100.0 * msg_stats[STAT_BATCH_ABSORBABLE] /
                         (msg_stats[STAT_BATCH_ITEMS] ? msg_stats[STAT_BATCH_ITEMS] : 1)
          << "%)" << endl;
#endif
    write_diag_files(msg_stats);
    arr.get_max_cost();
  }

  void done_max_cost(cost max_cost) {
    ckout << "Maximum vertex cost, not counting unreachable: " << max_cost
          << endl;
#ifdef PRINT_HISTO
    histoSeq->putout();
#endif
    tram_proxy.tramStats(
        CkCallback(CkReductionTarget(Main, done_tram_stats), mainProxy));
  }

  /**
   * Aggregation-layer volume, reported alongside every wall-clock number:
   * bytes actually handed to the send path, and bytes allocated to carry them.
   */
  void done_tram_stats(unsigned long long *values, int n) {
    ckout << "TRAM messages: " << values[0] << ", bytes sent: " << values[1]
          << ", bytes allocated: " << values[2] << endl;
    ckout << "TRAM node messages: " << values[3]
          << ", bytes allocated: " << values[4] << endl;
    if (verify_mode)
      arr.verify_hash();
    else
      CkExit(run_truncated ? 1 : 0);
  }

  /**
   * Compare the parallel result against serial Dijkstra over the same
   * generated graph. Exits nonzero on mismatch so a regression script can gate
   * on it.
   */
  void done_verify(unsigned long long *values, int n) {
    DistanceDigest parallel;
    parallel.h1 = values[0];
    parallel.h2 = values[1];
    parallel.reachable = values[2];
    parallel.distance_sum = values[3];
    ckout << "VERIFY parallel digest h1=" << parallel.h1
          << " h2=" << parallel.h2 << " reachable=" << parallel.reachable
          << " distance_sum=" << parallel.distance_sum << endl;

    double reference_begin = CkWallTimer();
    std::vector<cost> reference;
    serial_reference<LongEdge>(graph_spec, start_vertex, lmax, reference);
    DistanceDigest serial;
    for (long i = 0; i < V; i++)
      serial.add(i, reference[i], lmax);
    ckout << "VERIFY serial   digest h1=" << serial.h1 << " h2=" << serial.h2
          << " reachable=" << serial.reachable
          << " distance_sum=" << serial.distance_sum << " (computed in "
          << CkWallTimer() - reference_begin << " s)" << endl;

    if (run_truncated) {
      // The digests may even agree if the timeout landed after the last real
      // update, but the run still did not converge on its own terms.
      ckout << "VERIFY FAIL: run was truncated by --timeout before converging"
            << endl;
      CkExit(1);
    } else if (parallel == serial) {
      ckout << "VERIFY PASS" << endl;
      CkExit(0);
    } else {
      ckout << "VERIFY FAIL: parallel result does not match serial Dijkstra"
            << endl;
      CkExit(1);
    }
  }
};

/**
 * Timeout handler. The run is abandoned, not finished: the distances that
 * follow are a partial result. Everything downstream is told so, and the
 * process exits nonzero, because the previous behaviour -- print the partial
 * answer and exit 0 -- makes a non-converged run indistinguishable from a
 * converged one in a batch log.
 */
void fast_exit(void *obj, double time) {
  Main *main_chare = (Main *)obj;
  ckout << endl
        << "TIMEOUT: no convergence after " << timeout_seconds
        << " s. The results below are a PARTIAL result and must not be "
           "reported as a solution."
        << endl;
  main_chare->compute_time = CkWallTimer() - main_chare->compute_begin;
  main_chare->run_truncated = true;
  arr.print_distances();
}

/**
 * This holds information that needs to be broadcasted
 * but that is calculated after the Main method
 */
class SharedInfo : public CBase_SharedInfo {
public:
  cost max_path;
  int event_id;

  SharedInfo() {
    event_id = traceRegisterUserEvent("Contrib reduction");
#ifdef PAPI
    if (CkNodeFirst(CkMyNode()) == CkMyPe()) {
      int retval = PAPI_library_init(PAPI_VER_CURRENT);
      if (retval != PAPI_VER_CURRENT) {
        fprintf(stderr, "PAPI library init error!\n");
        CkExit(1);
      }
    }
#endif
  }

  void max_path_value(cost max_path_val) { max_path = max_path_val; }
};

/**
 * Array of chares for Dijkstra
 */
class SsspChares : public CBase_SsspChares {
private:
  LocalCsr local_graph;  // this PE's slice of the topology, flat CSR
  std::vector<LongEdge> incoming_edges; // RMAT staging, emptied once built
  WeightAssigner graph_weights;         // same weight rule as the generators
  cost *distances = nullptr; // tentative distance per local vertex
  long start_vertex;     // global index of lowest vertex assigned to this pe
  long num_vertices = 0; // number of vertices assigned to this pe
  VertexRng flush_rng = VertexRng(0, 0); // per-chare flush cadence draw
  long updates_created_locally = 0;   // number of update messages sent
  long updates_processed_locally = 0; // number of update messages received
  long *partition_index;   // defines boundaries of indices for each pe
  long wasted_updates = 0; // number of updates that don't have the final answer
  long rejected_updates = 0; // number of updates that don't decrease a distance
                             // value/create more messages
  tram_proxy_t tram_proxy;
  tram_t *tram; // tram library
  SharedInfo *shared_local;
  std::priority_queue<Update, std::vector<Update>, ComparePairs>
      pq;          // heap of messages
  long *histogram; // local histogram of data, from 0 to max_size, divided into
                   // HISTO_BUCKET_COUNT buckets
#ifdef ACIC_DIAG
  // histogram[] is the live population and is back at zero when the run ends,
  // so it cannot answer how much of the bucket range a graph ever reached.
  // This one is only ever incremented. It costs a second 16 KB array touched
  // once per relaxation, which is why it is not in the timed build.
  long *histo_created = nullptr;
  long batch_items = 0;      // updates delivered to this PE in batches
  long batch_absorbable = 0; // ... repeating a destination inside their batch
  std::vector<long> batch_table;  // open addressed, reused between batches
  long *arrivals_per_vertex = nullptr; // updates delivered to each local vertex
  long deg_vertices[DEGREE_CLASSES] = {0};
  long deg_edges[DEGREE_CLASSES] = {0};
  long deg_arrivals[DEGREE_CLASSES] = {0};
  long deg_rejects[DEGREE_CLASSES] = {0};
  long arr_vertices[DEGREE_CLASSES] = {0};
  long arr_arrivals[DEGREE_CLASSES] = {0};

  // floor(log2(x)) + 1, with 0 in its own bin: 0 -> 0, 1 -> 1, 2..3 -> 2,
  // 4..7 -> 3, and so on.
  static int degree_class(long x) {
    int c = 0;
    while (x > 0 && c < DEGREE_CLASSES - 1) {
      x >>= 1;
      c++;
    }
    return c;
  }
#endif
  long *vcount; // array of vertex distances, calculated with same formula as
                // histogram
  int heap_threshold; // highest bucket where messages can be pushed to heap
  int tram_threshold; // highest bucket where messages can be pushed to tram
  int bfs_threshold;
  double bucket_multiplier;       // constant to calculate bucket
  std::vector<Update> *pq_hold; // hold for heap messages
  long bfs_created = 0;           // bfs created messages
  long bfs_processed = 0;         // bfs processed messages
  int updates_noted = 0; // updates that have either updated a vertex value, or
                         // are confirmed to not be an improvement
  int *dest_table; // destination table for faster pe calculation
  int current_phase = 0;
  long actual_edges = 0; // when graph is generated, here's how many edges
                         // actually got generated
  long bfs_noted = 0;
  long *info_array;
  long distance_changes = 0;
  long updates_in_tram = 0;
#ifdef PAPI
  int eventset;
#endif

public:
  /**
   * Gets the destination processor for a given vertex
   */
  int get_dest_proc(long vertex) {
    int dest_proc = 0;
    for (int j = 0; j < N; j++) {
      // find first partition that begins at a higher edge count;
      if (vertex >= partition_index[j] && vertex < partition_index[j + 1]) {
        dest_proc = j;
        break;
      }
      if (j == N - 1) {
        dest_proc = N - 1;
      }
    }
    return dest_proc;
  }

  int get_dest_proc_fast(long vertex) {
    // look up x/M and 1+x/M
    int xm_pe, xm_plus_one_pe;
    long dest_table_index = vertex / M;
    // if this points to the end of dest_table
    if (dest_table_index >= (V / M) - 1) {
      xm_pe = dest_table[(V / M) - 1];
      int dest_proc = xm_pe;
      for (int j = xm_pe; j < N; j++) {
        // find first partition that begins at a higher edge count;
        if (vertex >= partition_index[j] && vertex < partition_index[j + 1]) {
          dest_proc = j;
          break;
        }
        if (j == N - 1) {
          dest_proc = N - 1;
        }
      }
      return dest_proc;
    }
    xm_pe = dest_table[dest_table_index];
    xm_plus_one_pe = dest_table[dest_table_index + 1];
    int dest_proc = xm_pe;
    for (int j = xm_pe; j <= xm_plus_one_pe; j++) {
      // find first partition that begins at a higher edge count;
      if (vertex >= partition_index[j] && vertex < partition_index[j + 1]) {
        dest_proc = j;
        break;
      }
      if (j == N - 1) {
        dest_proc = N - 1;
      }
    }
    return dest_proc;
  }

  int get_dest_proc_local(Update upd) {
    return get_dest_proc_fast(upd.dest_vertex);
  }

  SsspChares(CProxy_HTram htram) { tram_proxy = htram; }

  void initiate_pointers() {
    tram = tram_proxy.ckLocalBranch();
    // No batch-done callback: the local-delivery shortcut it used to drive was
    // abandoned, and htram now tolerates a null one.
    tram->set_func_ptr_retarr(SsspChares::process_update_caller,
                              get_dest_proc_local_caller, nullptr, this);
    shared_local = shared.ckLocalBranch();
#ifdef PAPI
    eventset = PAPI_NULL;
    int result = PAPI_create_eventset(&eventset);
    if (result != PAPI_OK) {
      printf("Error PAPI create eventset: %s\n", PAPI_strerror(result));
    }
    result = PAPI_add_event(eventset, PAPI_TOT_INS);
    if (result != PAPI_OK) {
      printf("Error PAPI add_event %s\n", PAPI_strerror(result));
    }
#endif
  }

  bool idle_triggered() {
    process_heap();
    return true;
  }

  void initialize_data(long *partition, int dividers) {
    histogram = new long[HISTO_BUCKET_COUNT];
    vcount = new long[HISTO_BUCKET_COUNT + 1]; // histo buckets plus infty
#ifdef ACIC_DIAG
    histo_created = new long[HISTO_BUCKET_COUNT];
#endif
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++) {
      histogram[i] = 0;
      vcount[i] = 0;
#ifdef ACIC_DIAG
      histo_created[i] = 0;
#endif
    }
    vcount[HISTO_BUCKET_COUNT] = 0;
    partition_index = new long[dividers];
    for (int i = 0; i < dividers; i++) {
      partition_index[i] = partition[i];
    }
    graph_weights = WeightAssigner(S);
    start_vertex = partition_index[thisIndex];
    num_vertices = partition_index[CkMyPe() + 1] - partition_index[CkMyPe()];
    // The loop below writes ceil(V/M) entries -- j runs while j*M < V -- so a
    // floor division here overruns the allocation by one int whenever V is not
    // a multiple of M. That corrupted the heap and showed up much later as an
    // intermittent SIGBUS inside an unrelated operator new.
    dest_table = new int[(V + M - 1) / M];
    for (int i = 0, j = 0; i < V; j++, i = j * M) {
      dest_table[j] = get_dest_proc(i);
    }
    distances = new cost[num_vertices];
    for (long i = 0; i < num_vertices; i++)
      distances[i] = lmax;
#ifdef ACIC_DIAG
    arrivals_per_vertex = new long[num_vertices];
    for (long i = 0; i < num_vertices; i++)
      arrivals_per_vertex[i] = 0;
#endif
    vcount[HISTO_BUCKET_COUNT] += num_vertices;
    flush_rng = VertexRng(thisIndex, S);
    heap_threshold = initial_threshold;
    tram_threshold = initial_threshold + 2;
    bfs_threshold = heap_threshold;
    pq_hold = new std::vector<Update>[HISTO_BUCKET_COUNT];
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++)
      pq_hold[i].reserve(4096);
    info_array = new long[histo_reduction_width + 7];
    set_bucket_width(log(V));
    CkCallWhenIdle(CkIndex_SsspChares::idle_triggered(), this);
  }

  // The mesh is the only input whose exact shape is known in advance, which
  // makes it the one place a generator bug is caught without a reference
  // solver. Cheap enough to leave on.
  void check_mesh_degree(long vertex, long side_length, size_t produced) {
    int expected = mesh_expected_degree(vertex, side_length);
    if ((size_t)expected != produced)
      ckout << "Edge count wrong for vertex " << vertex << ": should be "
            << expected << " not " << produced << endl;
  }

  void generate_2d_graph(long *partition, int dividers) {
    initialize_data(partition, dividers);
    set_bucket_width(sqrt(V));
#ifdef INFO_PRINTS
    ckout << "Generating local graph on PE " << CkMyPe() << " with "
          << num_vertices << " vertices" << endl;
#endif
    long side_length = mesh_side_length(V);
    cost max_edges_sum = 0;
    std::vector<Edge> adjacency; // one scratch buffer, reused for every vertex
    local_graph.begin(num_vertices, 4 * num_vertices);
    for (long i = 0; i < num_vertices; i++) {
      long this_vertex = i + start_vertex;
      // See graphlib/generators.h: identical adjacency for any PE count.
      gen_mesh_vertex(this_vertex, side_length, S, adjacency);
      actual_edges += adjacency.size();
      cost largest_outedge = 0;
      for (size_t j = 0; j < adjacency.size(); j++)
        if (adjacency[j].distance > largest_outedge)
          largest_outedge = adjacency[j].distance;
      max_edges_sum += largest_outedge;
      check_mesh_degree(this_vertex, side_length, adjacency.size());
      local_graph.append(adjacency);
    }
    local_graph.finish();
#ifdef INFO_PRINTS
    ckout << "PE " << CkMyPe() << " generated " << actual_edges << " edges"
          << endl;
#endif
    CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
    contribute(sizeof(cost), &max_edges_sum, CkReduction::sum_long, cb);
  }

  void generate_local_graph(long _num_vertices, long _num_edges,
                            long *partition, int dividers) {
#ifdef INFO_PRINTS
    // _num_edges is not printed: it is a draw Main made that nothing acts on,
    // because gen_random_vertex derives a vertex's degree from the vertex id.
    // Printing it suggested this PE had been told how many edges to make.
    ckout << "Generating local graph on PE " << CkMyPe() << " with "
          << _num_vertices << " vertices" << endl;
#endif
    initialize_data(partition, dividers);
    cost max_edges_sum = 0;
    std::vector<Edge> adjacency; // one scratch buffer, reused for every vertex
    local_graph.begin(num_vertices, average_degree * num_vertices);
    for (long i = 0; i < num_vertices; i++) {
      adjacency.clear();
      // Past the end of this PE's share the vertex stays isolated, but it is
      // still a vertex: it needs a CSR row, or every later row is off by one.
      if (!((CkMyPe() == N - 1) && (i >= _num_vertices))) {
        // Adjacency is a pure function of the global vertex id and the seed,
        // so the graph does not change with PE count. See graphlib/generators.h.
        gen_random_vertex(i + start_vertex, V, average_degree, S, adjacency);
        actual_edges += adjacency.size();
        cost largest_outedge = 0;
        for (size_t j = 0; j < adjacency.size(); j++)
          if (adjacency[j].distance > largest_outedge)
            largest_outedge = adjacency[j].distance;
        max_edges_sum += largest_outedge;
      }
      local_graph.append(adjacency);
    }
    local_graph.finish();
    CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
    contribute(sizeof(cost), &max_edges_sum, CkReduction::sum_long, cb);
  }

  /**
   * RMAT: generate this PE's contiguous slice of the edge index space and send
   * each edge to the owner of its source vertex.
   *
   * The slice is by edge index, not by vertex, because that is the only thing
   * an RMAT edge is a function of. Every PE therefore produces edges for every
   * other PE, and the result is the same graph at any PE count -- edge i is
   * edge i regardless of who drew it.
   */
  void generate_rmat_graph(long *partition, int dividers) {
    initialize_data(partition, dividers);
    set_bucket_width(log(V));

    const long first_edge = (long)((double)num_global_edges * thisIndex / N);
    const long last_edge = (long)((double)num_global_edges * (thisIndex + 1) / N);
#ifdef INFO_PRINTS
    ckout << "PE " << CkMyPe() << " generating RMAT edges [" << first_edge
          << ", " << last_edge << ")" << endl;
#endif
    std::vector<LongEdge> generated;
    generated.reserve((size_t)(last_edge - first_edge));
    gen_rmat_range(first_edge, last_edge, V, S, graph_weights, generated);

    // Bucket by owner, then send. Chunked so that one destination's share of a
    // very large slice does not become a single enormous message.
    std::vector<std::vector<LongEdge>> outgoing((size_t)N);
    const size_t chunk = 1 << 20;
    for (size_t i = 0; i < generated.size(); i++) {
      int owner = get_dest_proc(generated[i].begin);
      outgoing[(size_t)owner].push_back(generated[i]);
      if (outgoing[(size_t)owner].size() >= chunk) {
        arr[owner].receive_edges(outgoing[(size_t)owner].data(),
                                 (long)outgoing[(size_t)owner].size());
        outgoing[(size_t)owner].clear();
      }
    }
    for (int p = 0; p < N; p++)
      if (!outgoing[(size_t)p].empty())
        arr[p].receive_edges(outgoing[(size_t)p].data(),
                             (long)outgoing[(size_t)p].size());
  }

  void receive_edges(LongEdge *edges, long E) {
    incoming_edges.insert(incoming_edges.end(), edges, edges + E);
  }

  /**
   * Quiescence has established that every edge has arrived, so the rows can be
   * built and the run can start.
   */
  void build_rmat_csr() {
    actual_edges = (long)incoming_edges.size();
    local_graph.build_from_edges(num_vertices, start_vertex,
                                 incoming_edges.data(), actual_edges);
    // Release the staging copy before the solve: it is as large as the CSR.
    std::vector<LongEdge>().swap(incoming_edges);
    contribute_largest_outedges();
  }

  /**
   * GAPBS: read this PE's own rows out of the file. The format is CSR ordered
   * by source vertex and the partition is a contiguous vertex range, so this is
   * a seek and a read -- no exchange, and no other PE involved.
   */
  void load_gapbs_graph(std::string path, long *partition, int dividers) {
    initialize_data(partition, dividers);
    set_bucket_width(log(V));
    GapbsHeader header = gapbs_read_header(path);
    std::vector<long> row_offset;
    std::vector<Edge> edges;
    gapbs_read_slice(path, header, start_vertex, start_vertex + num_vertices,
                     graph_weights, row_offset, edges);
    actual_edges = (long)edges.size();
    local_graph.adopt_rows(num_vertices, row_offset, edges);
#ifdef INFO_PRINTS
    ckout << "PE " << CkMyPe() << " read vertices [" << start_vertex << ", "
          << start_vertex + num_vertices << ") and " << actual_edges
          << " edges" << endl;
#endif
    contribute_largest_outedges();
  }

  /**
   * The reduction every graph-construction path ends in: the sum over local
   * vertices of the heaviest out-edge, which Main turns into lmax. Edges are
   * sorted by weight within a row, so the heaviest is the last.
   */
  void contribute_largest_outedges() {
    cost max_edges_sum = 0;
    for (long i = 0; i < num_vertices; i++) {
      long degree = local_graph.degree(i);
      if (degree > 0)
        max_edges_sum += local_graph.edges(i)[degree - 1].distance;
    }
    CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
    contribute(sizeof(cost), &max_edges_sum, CkReduction::sum_long, cb);
  }

  void get_graph(LongEdge *edges, long E, long *partition, int dividers) {
    actual_edges = E;
    initialize_data(partition, dividers);
    local_graph.build_from_edges(num_vertices, start_vertex, edges, E);
    contribute_largest_outedges();
  }

  void start_papi() {
#ifdef PAPI
    int result = PAPI_start(eventset);
    if (result != PAPI_OK) {
      printf("Error PAPI start: %s\n", PAPI_strerror(result));
    }
#endif
    traceBegin();
  }

  /**
   * Method that accepts initial update to source vertex
   */
  void start_algo(Update new_vertex_and_distance) {
    // A source with no out-edges produces no updates at all, and the
    // termination test in reduce_histogram needs updates_created > 1000 before
    // it will believe a run has converged -- so such a run sits until the
    // timeout rather than finishing instantly with a one-vertex answer. That
    // is easy to hit on RMAT and on real graphs, where a large fraction of
    // vertices have out-degree zero, so at least say what happened. Making the
    // predicate itself handle it is a change to the convergence logic and
    // belongs with the tail work in step 7 of the plan.
    long local_index = new_vertex_and_distance.dest_vertex - start_vertex;
    if (local_index >= 0 && local_index < num_vertices &&
        local_graph.degree(local_index) == 0)
      ckout << "WARNING: source vertex " << new_vertex_and_distance.dest_vertex
            << " has no outgoing edges. Nothing can be relaxed, and the "
               "convergence test will not fire; this run will sit until "
               "--timeout. Pick a source with out-edges."
            << endl;
    process_update(new_vertex_and_distance);
  }

  static void process_update_caller(void *p, Update *new_vertex_and_distances,
                                    int count) {
    // ckout << "PE " << CkMyPe() << " receiving " << count << " updates" <<
    // endl;
    SsspChares *self = (SsspChares *)p;
#ifdef ACIC_DIAG
    self->count_batch_duplicates(new_vertex_and_distances, count);
#endif
    for (int i = 0; i < count; i++) {
      self->process_update(new_vertex_and_distances[i]);
    }
    // self->process_heap();
  }

#ifdef ACIC_DIAG
  /**
   * The receiver-side half of the combining ceiling: how many items in this
   * batch repeat a destination another item in the same batch already carries.
   * A min-combine folds each repeat into the entry already there, so this is
   * exactly what the batch-local fold the plan proposes could absorb -- an
   * upper bound on it, since it ignores whether the fold would also have to
   * keep the loser for the histogram.
   *
   * It is a ceiling from below, not from above: source-side combining sees
   * items this never does, and rejected_updates bounds every scheme. The three
   * numbers together say which half of the redundancy is reachable from where.
   */
  void count_batch_duplicates(const Update *items, int count) {
    if (count <= 0)
      return;
    size_t slots = 64;
    while (slots < (size_t)count * 2)
      slots <<= 1;
    batch_table.assign(slots, -1);
    const size_t mask = slots - 1;
    for (int i = 0; i < count; i++) {
      long key = items[i].dest_vertex;
      size_t slot = (size_t)splitmix64((uint64_t)key) & mask;
      while (batch_table[slot] != -1 && batch_table[slot] != key)
        slot = (slot + 1) & mask;
      if (batch_table[slot] == key)
        batch_absorbable++;
      else
        batch_table[slot] = key;
    }
    batch_items += count;
  }
#endif

  static int get_dest_proc_local_caller(void *p, Update new_upd) {
    return ((SsspChares *)p)->get_dest_proc_local(new_upd);
  }

  /**
   * Set the histogram's bucket width, in distance units. Each graph mode passes
   * the width it derives from |V| -- log V for the random-graph modes, sqrt V
   * for the mesh -- and --bucket-width replaces that wholesale, which is what
   * makes H1 of the SC27 plan an A/B rather than a rebuild. Neither rule reads
   * anything about the distances the graph actually produces, which is the
   * substance of the hypothesis.
   *
   * The reciprocal is written the long way round because that is the
   * expression every recorded measurement was taken with. Scaling numerator and
   * denominator by the same power of two cannot change a correctly rounded
   * quotient, so it is the same double as 1.0 / width -- but the golden digests
   * do depend on it, so there is no reason to find out the hard way.
   */
  void set_bucket_width(double natural_width) {
    double width =
        bucket_width_override > 0.0 ? bucket_width_override : natural_width;
    bucket_multiplier = HISTO_BUCKET_COUNT / (HISTO_BUCKET_COUNT * width);
  }

  /**
   * Gets the histogram bucket for any given distance
   */
  int get_histo_bucket(cost distance) {
    double bucket = distance * bucket_multiplier;
    int result = (int)bucket;
    if (result >= HISTO_BUCKET_COUNT)
      return HISTO_BUCKET_COUNT - 1;
    else
      return result;
  }

  void generate_updates(long local_index, bool bfs) {
    const Edge *adjacency = local_graph.edges(local_index);
    const long degree = local_graph.degree(local_index);
    const cost source_distance = distances[local_index];
    for (long i = 0; i < degree; i++) {
      // calculate distance pair for neighbor
      Update new_update;
      new_update.dest_vertex = adjacency[i].end;
      new_update.distance = source_distance + adjacency[i].distance;
      // we are going to send this, so add to the histogram and the send update
      // count
      int neighbor_bucket = get_histo_bucket(new_update.distance);
      histogram[neighbor_bucket]++;
#ifdef ACIC_DIAG
      histo_created[neighbor_bucket]++;
#endif
      updates_created_locally++;
      // Bucket 0 means "send now"; a bucket above the threshold hands the
      // item to the library's own per-destination hold, to be released when
      // changeThreshold() admits that bucket.
#ifndef ALL_TO_TRAM_HOLD
      if ((neighbor_bucket > tram_threshold) && !bfs) {
        tram->sendItemPrioDeferredDest(new_update, neighbor_bucket);
      } else {
#ifndef LOCAL_TO_TRAM
        // get_dest_proc_fast is a table lookup, but it used to run on this
        // path even when LOCAL_TO_TRAM made its result unreachable -- once per
        // outgoing edge, for nothing.
        if (get_dest_proc_fast(new_update.dest_vertex) == CkMyPe())
          process_update(new_update);
        else
          tram->sendItemPrioDeferredDest(new_update, 0);
#else
        tram->sendItemPrioDeferredDest(new_update, 0);
#endif
      }
#else
      tram->sendItemPrioDeferredDest(new_update, neighbor_bucket);
      if (neighbor_bucket <= tram_threshold)
        updates_in_tram++;
#endif
    }
  }


  /**
   * Takes a distance update and immediately adds it to the local heap/pq
   */
  inline void process_update(Update new_vertex_and_distance) {
    long dest_vertex = new_vertex_and_distance.dest_vertex;
    long local_index = dest_vertex - start_vertex;
    cost this_cost = new_vertex_and_distance.distance;
    int this_bucket = get_histo_bucket(this_cost);
#ifdef ACIC_DIAG
    arrivals_per_vertex[local_index]++;
    const int deg_class = degree_class(local_graph.degree(local_index));
    deg_arrivals[deg_class]++;
#endif
    if (this_cost < distances[local_index]) {
#ifdef VCOUNT
      vcount[this_bucket]++;
      if (distances[local_index] == lmax) {
        vcount[HISTO_BUCKET_COUNT]--;
      } else
        vcount[get_histo_bucket(distances[local_index])]--;
#endif
      distances[local_index] = this_cost;
      distance_changes++;
      updates_noted++;
      int pq_bucket;
      if (local_graph.degree(local_index) > 0) {
#ifdef PQ_EDGE_DIST
        pq_bucket = get_histo_bucket(
            local_graph.edges(local_index)[0].distance + this_cost);
#ifndef PQ_HOLD_ONLY
        if (pq_bucket > heap_threshold) {
          pq_hold[pq_bucket].push_back(new_vertex_and_distance);
        } else
          pq.push(new_vertex_and_distance);
#else
        pq_hold[pq_bucket].push_back(new_vertex_and_distance);
#endif
#else
#ifndef PQ_HOLD_ONLY
        if (this_bucket > heap_threshold) {
          pq_hold[this_bucket].push_back(new_vertex_and_distance);
        } else
          pq.push(new_vertex_and_distance);
#else
        pq_hold[this_bucket].push_back(new_vertex_and_distance);
#endif
#endif
      } else {
        wasted_updates++;
        histogram[this_bucket]--;
        updates_processed_locally++;
      }
    } else {
      wasted_updates++;
      rejected_updates++;
      histogram[this_bucket]--;
      updates_noted++;
      updates_processed_locally++;
#ifdef ACIC_DIAG
      deg_rejects[deg_class]++;
#endif
    }
  }

  /**
   * Update distances, but locally (the incoming pair comes from this PE)
   * this is not an entry method
   * returns true (runs when pe is idle)
   */
  void process_heap() {
#ifdef PQ_HOLD_ONLY
    for (int i = 0; i <= heap_threshold; i++) // iterate to heap threshold
    {
      long items_processed = 0;
      items_processed = pq_hold[i].size();
      for (int j = 0; j < pq_hold[i].size();
           j++) // iterate pq bucket in reverse
      {
        Update new_vertex_and_distance = pq_hold[i][j];
        long dest_vertex = new_vertex_and_distance.dest_vertex;
        cost new_distance = new_vertex_and_distance.distance;
        int this_histo_bucket = get_histo_bucket(new_distance);
        long local_index = dest_vertex - start_vertex;
        if (new_distance == distances[local_index]) {
          // for all neighbors
          generate_updates(local_index, false);
        } else {
          rejected_updates++;
        }
        wasted_updates++;
        histogram[this_histo_bucket]--;
        updates_processed_locally++;
      }
      if (items_processed > 0) {
        pq_hold[i].clear();
        arr[thisIndex].process_heap();
        break;
      }
    }
#else
    int heap_count = 0;
    while (pq.size() > 0) {
      if (++heap_count > 100) {
        thisProxy[thisIndex].process_heap();
        break;
      } // give other eps a chance to run
      Update new_vertex_and_distance = pq.top();
      long dest_vertex = new_vertex_and_distance.dest_vertex;
      cost new_distance = new_vertex_and_distance.distance;
      int this_histo_bucket = get_histo_bucket(new_distance);
      if (this_histo_bucket > heap_threshold) {
        break;
      }
      pq.pop();
      if (dest_vertex >= partition_index[thisIndex] &&
          dest_vertex < partition_index[thisIndex + 1]) {
        long local_index = dest_vertex - start_vertex;
        //  if the incoming distance is actually smaller
        if (new_distance == distances[local_index]) {
          generate_updates(local_index, false);
        } else {
          rejected_updates++;
        }
      }
      wasted_updates++;
      histogram[this_histo_bucket]--;
      updates_processed_locally++;
    }
#endif
  }

  /**
   * Contribute to a reduction to get the overall histogram to pe 0/main chare
   */
  void contribute_histogram(int behind_first_nonzero) {
    long donecount = 0;
    traceUserEvent(shared_local->event_id);
    CkCallback cb(CkReductionTarget(Main, reduce_histogram), mainProxy);
    int first_nonzero = behind_first_nonzero + 1;
    for (int i = first_nonzero; i < (first_nonzero + histo_reduction_width);
         i++) {
      if (i < 0 || i >= HISTO_BUCKET_COUNT)
        info_array[i - first_nonzero] = 0;
      else
        info_array[i - first_nonzero] = histogram[i];
    }
    info_array[histo_reduction_width] = updates_created_locally;
    info_array[histo_reduction_width + 1] = updates_processed_locally;
    info_array[histo_reduction_width + 2] = bfs_processed;
    for (int i = 0; i <= behind_first_nonzero; i++)
      donecount += vcount[i];
    info_array[histo_reduction_width + 3] = donecount;
    info_array[histo_reduction_width + 4] = updates_noted;
    info_array[histo_reduction_width + 5] = bfs_noted;
    info_array[histo_reduction_width + 6] = distance_changes;
    contribute((histo_reduction_width + 7) * sizeof(long), info_array,
               CkReduction::sum_long, cb);
  }

  void clear_pq_hold() {
    // we should maintain lower bound
    for (int i = 0; i <= heap_threshold; i++) {
      for (int j = 0; j < pq_hold[i].size(); j++) {
        pq.push(pq_hold[i][j]);
      }
      pq_hold[i].clear();
    }
  }

  /**
   * Broadcasts bucket limit
   */
  void current_thresholds(int _heap_threshold, int _tram_threshold,
                          int _bfs_threshold, int behind_first_nonzero,
                          int phase) {
    heap_threshold = _heap_threshold;
    tram_threshold = _tram_threshold;
    bfs_threshold = _bfs_threshold;
    current_phase = phase;
    // after every reduction, push out messages in hold that are in limit
    // replace this loop with call to tram->changethreshold(tram_threshold)
    tram->setHistoBucketCount(HISTO_BUCKET_COUNT);
    int direct_threshold = behind_first_nonzero + 8;
    // int direct_threshold = tram_threshold;
    if (direct_threshold > tram_threshold - 1)
      direct_threshold = tram_threshold - 1;
    float selectivity = 1.0;
    // if(behind_first_nonzero > 68) selectivity = 1.0;
    tram->changeThreshold(direct_threshold, tram_threshold, selectivity);
#ifndef PQ_HOLD_ONLY
    arr[thisIndex].clear_pq_hold();
// add user event
#endif
    // This was `rand() % 5 == 0`. rand() keeps process-global state, shared by
    // every worker thread in an SMP process: a contention point where it is
    // thread-safe at all, and an uncontrolled random factor in every
    // measurement. A per-chare stream fixes both without changing what the
    // cadence actually does.
    //
    // Deliberately still a draw rather than an exact period. Two periodic
    // variants were measured over 8 runs and both are worse: one shared phase
    // (all chares flushing on the same round) costs 1.7% more wasted updates
    // because communication turns bursty, and a phase staggered by chare index
    // is worse still on both mean and variance. Independent per-chare draws are
    // what the 2024 measurements were taken with, so keep that and make it
    // reproducible.
    if (flush_rng.bounded((uint64_t)flush_round_interval) == 0)
      tram->tflush();
    //    tram->sanityCheck();
    //    tram->flush_everything();
    arr[thisIndex].process_heap();
    // The controller's cadence is normally not a knob: this call closes the
    // loop, so a round costs exactly a reduction plus a broadcast and nothing
    // sets the period. H4 says the tail advances only at that cadence, which
    // is testable by making the period longer and seeing whether the tail
    // grows with it. Default 0 keeps the closed loop, which is what every
    // measurement so far was taken with.
    if (round_delay_ms > 0.0) {
      pending_first_nonzero = behind_first_nonzero;
      CcdCallFnAfter(delayed_contribute, (void *)this, round_delay_ms);
    } else
      contribute_histogram(behind_first_nonzero);
  }

  int pending_first_nonzero = 0;
  static void delayed_contribute(void *p, double) {
    SsspChares *self = (SsspChares *)p;
    self->contribute_histogram(self->pending_first_nonzero);
  }

  /**
   * Print out the final distances calculated by the algorithm
   */
  void print_distances() {
    traceEnd();
#ifdef PAPI
    long long values[1] = {(long long)0};
    int result = PAPI_stop(eventset, values);
    if (result != PAPI_OK) {
      printf("Error PAPI stop %s\n", PAPI_strerror(result));
    }
    // ckout << "PE " << CkMyPe() << " total instructions: " << values[0] <<
    // endl;
#endif
    std::vector<long> msg_stats(stat_count, 0);
    msg_stats[STAT_WASTED] = wasted_updates;
    msg_stats[STAT_REJECTED] = rejected_updates;
    for (int i = 0; i < HISTO_BUCKET_COUNT + 1; i++) {
      msg_stats[STAT_VCOUNT + i] = vcount[i];
    }
    msg_stats[STAT_NOTED] = updates_noted;
    msg_stats[STAT_EDGES] = actual_edges;
    msg_stats[STAT_DISTANCE_CHANGES] = distance_changes;
    // Topology bytes actually held, summed over PEs. Reported from inside the
    // run because peak RSS cannot see it: this runtime reserves a fixed ~554 MB
    // regardless of graph or PE count, which swamps the graph until it passes a
    // few million vertices.
    msg_stats[STAT_GRAPH_BYTES] =
        (long)(local_graph.bytes() + sizeof(cost) * (size_t)num_vertices);
#ifdef PAPI
    msg_stats[STAT_INSTRUCTIONS] = values[0];
#endif
#ifdef ACIC_DIAG
    msg_stats[STAT_BATCH_ITEMS] = batch_items;
    msg_stats[STAT_BATCH_ABSORBABLE] = batch_absorbable;
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++)
      msg_stats[STAT_HISTO_CREATED + i] = histo_created[i];
    for (long i = 0; i < num_vertices; i++) {
      long degree = local_graph.degree(i);
      int dc = degree_class(degree);
      deg_vertices[dc]++;
      deg_edges[dc] += degree;
      int ac = degree_class(arrivals_per_vertex[i]);
      arr_vertices[ac]++;
      arr_arrivals[ac] += arrivals_per_vertex[i];
    }
    for (int i = 0; i < DEGREE_CLASSES; i++) {
      msg_stats[STAT_DEG_VERTICES + i] = deg_vertices[i];
      msg_stats[STAT_DEG_EDGES + i] = deg_edges[i];
      msg_stats[STAT_DEG_ARRIVALS + i] = deg_arrivals[i];
      msg_stats[STAT_DEG_REJECTS + i] = deg_rejects[i];
      msg_stats[STAT_ARR_VERTICES + i] = arr_vertices[i];
      msg_stats[STAT_ARR_ARRIVALS + i] = arr_arrivals[i];
    }
    // One line per PE, read back out of the run log by the harness. H3 asks
    // whether a contiguous 1-D partitioning of a power law leaves one PE
    // holding the work, and that is a question about the spread across PEs,
    // not about a sum or a maximum -- so print the spread rather than reduce
    // it away. At one line per PE this is affordable up to any PE count a
    // diagnosis run uses.
    ckout << "DIAG_PE " << CkMyPe() << " vertices=" << num_vertices
          << " edges=" << actual_edges
          << " updates_created=" << updates_created_locally
          << " updates_processed=" << updates_processed_locally
          << " distance_changes=" << distance_changes
          << " rejected=" << rejected_updates
          << " batch_items=" << batch_items
          << " batch_absorbable=" << batch_absorbable << endl;
#endif

    CkCallback cb(CkReductionTarget(Main, done), mainProxy);
    contribute(stat_count * sizeof(long), msg_stats.data(),
               CkReduction::sum_long, cb);
    // mainProxy.done();
  }

  /**
   * Fold this PE's slice of the distance vector into an order-independent
   * digest. Summation makes the reduction invariant to PE count, so a run on
   * any number of PEs must produce the same digest for the same graph.
   */
  void verify_hash() {
    DistanceDigest digest;
    for (int i = 0; i < num_vertices; i++) {
      digest.add(start_vertex + i, distances[i], lmax);
    }
    unsigned long long values[4] = {digest.h1, digest.h2, digest.reachable,
                                    digest.distance_sum};
    CkCallback cb(CkReductionTarget(Main, done_verify), mainProxy);
    contribute(4 * sizeof(unsigned long long), values,
               CkReduction::sum_ulong_long, cb);
  }

  void get_max_cost() {
    cost max_cost = 0;
    for (int i = 0; i < num_vertices; i++) {
      cost vertex_cost = distances[i];
      if (vertex_cost != lmax) {
        if (vertex_cost > max_cost) {
          max_cost = vertex_cost;
        }
      }
    }
    CkCallback cb(CkReductionTarget(Main, done_max_cost), mainProxy);
    contribute(sizeof(cost), &max_cost, CkReduction::max_long, cb);
  }

  void stop_periodic_flush() { tram->stop_periodic_flush(); }
};

#include "sssp_smp.def.h"
