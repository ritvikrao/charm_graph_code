#include "TopoManager.h"
#include "htram_group.h"
#include "sssp_smp.decl.h"
#include "graph_gen.h"
#include "graphlib/csr.h"
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
int histo_reduction_width = HISTO_BUCKET_COUNT / 8;
double reduction_delay =
    0.1;                   // each histogram reduction happens at this interval
int initial_threshold = 3; // initial histo threshold
bool verify_mode = false;  // --verify: check the result against serial Dijkstra
// Flush the aggregation buffers once every this many controller rounds.
// Step 7 of the SC27 plan makes this cadence adaptive; until then it is at
// least a named, reproducible knob rather than a coin flip.
int flush_round_interval = 5;
// --timeout <seconds>: abandon a run that has not converged. 0 disables it.
// There is deliberately no default: a truncated run is a failed run, and the
// old behaviour was to give up after 30 s and print the partial distances with
// the same banner and the same exit status as a converged run.
double timeout_seconds = 0.0;
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
      } else if (arg.rfind("--", 0) == 0) {
        ckout << "Unknown option " << arg.c_str() << endl;
        CkExit(1);
        return;
      } else
        args.push_back(arg);
    }
    if (args.size() < 7) {
      ckout << "Usage: sssp_smp <vertices> <file|edge count> <seed> "
            << "<start vertex> <mode 0=file,1=random,2=mesh> "
            << "<tram percentile> <heap percentile> "
            << "[--verify] [--timeout <seconds>] [--bufsize <items>]" << endl;
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
    if (verify_mode && generate_mode == 0) {
      ckout << "--verify requires a generated graph (mode 1 or 2); the file "
            << "reader has no serial reference yet" << endl;
      CkExit(1);
      return;
    }
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
    if (generate_mode == 2) {
      long side_length = (int)std::sqrt((double)V);
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
      arr.generate_2d_graph(partition_index, N + 1);
    } else if (generate_mode == 1) {
      num_global_edges = std::stol(file_name);
#ifdef INFO_PRINTS
      ckout << "Graph will be automatically generated with " << V << " vertices"
            << endl;
#endif
      average_degree = num_global_edges / V;
      // for each pe, generate a random vertex and edge count, and send to pes
      long remaining_vertices = V;
      long current_start_index = 0; // tracks start vertex for indices
      // Partition sizes vary by +-20%. Drawn with the same portable generator
      // as the graph itself, so a run has the same load balance on every
      // machine -- <random> would not give that. See graph_gen.h.
      VertexRng partition_rng(-1, S);
      long vertex_low = (V * 4) / (N * 5);
      long vertex_span = ((V * 6) / (N * 5)) - vertex_low + 1;
      long edge_low = (num_global_edges * 4) / (N * 5);
      long edge_span = ((num_global_edges * 6) / (N * 5)) - edge_low + 1;
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
      for (int i = 0; i < N; i++) {
        arr[i].generate_local_graph(vertex_counts[i], edge_counts[i],
                                    partition_index, N + 1);
      }
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
        // order edges are read in. See graph_gen.h.
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
    // calculate the total histogram sum
    for (int i = 0; i < histo_reduction_width; i++) {
      histogram_sum += histo_values[i];
      if ((histo_values[i] > 0) && (first_nonzero == -1)) {
        first_nonzero = i + last_first_nonzero;
      }
    }
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
    // arr.contribute_histogram(first_nonzero-1);
    last_first_nonzero = first_nonzero;
    arr.current_thresholds(heap_threshold, tram_threshold, bfs_threshold,
                           first_nonzero - 1, current_phase);

    // start next reduction round
    // CcdCallFnAfter(start_reductions, (void *) this, reduction_delay);
  }

  void done(long *msg_stats, int N) {
    // ends program, prints that program is ended
    // ckout << "Completed" << endl;
    // CkPrintf("Memory usage at end: %f\n", CmiMemoryUsage()/(1024.0*1024.0));
    total_time = CkWallTimer() - start_time;
    ckout << "Actual edges: " << msg_stats[4 + HISTO_BUCKET_COUNT] << endl;
    ckout << "Read time: " << read_time << endl;
    ckout << "Compute time: " << compute_time << endl;
    ckout << "Total time: " << total_time << endl;
    ckout << "Wasted updates: " << msg_stats[0] - V << endl;
    ckout << "Wasted updates normalized to |E|: "
          << (double)(msg_stats[0] - V) / msg_stats[4 + HISTO_BUCKET_COUNT]
          << endl;
    ckout << "Rejected updates: " << msg_stats[1] << endl;
    ckout << "Rejected updates normalized to |E|: "
          << (double)msg_stats[1] / msg_stats[4 + HISTO_BUCKET_COUNT] << endl;
    ckout << "Number of threshold changes: " << threshold_change_counter
          << endl;
    ckout << "Number of reductions: " << reduction_counts << endl;
    ckout << "Updates noted: " << msg_stats[3 + HISTO_BUCKET_COUNT] << endl;
    ckout << "Distance changes: " << msg_stats[5 + HISTO_BUCKET_COUNT]
          << ", per vertex: " << msg_stats[5 + HISTO_BUCKET_COUNT] * 1.0 / V
          << endl;
#ifdef VCOUNT
    long vcount_sum = 0;
    ckout << "Vcount: [ ";
    for (int i = 0; i < HISTO_BUCKET_COUNT + 1; i++) {
      ckout << msg_stats[i + 2] << ", ";
      vcount_sum += msg_stats[i + 2];
    }
    ckout << endl;
    ckout << "Vcount sum: " << vcount_sum << endl;
#endif
    ckout << "Graph bytes: " << msg_stats[6 + HISTO_BUCKET_COUNT]
          << ", per edge: "
          << msg_stats[6 + HISTO_BUCKET_COUNT] * 1.0 /
                 msg_stats[4 + HISTO_BUCKET_COUNT]
          << endl;
#ifdef PAPI
    ckout << "Total insts: " << msg_stats[7 + HISTO_BUCKET_COUNT] << endl;
    ckout << "Insts per edge: "
          << msg_stats[7 + HISTO_BUCKET_COUNT] * 1.0 /
                 msg_stats[4 + HISTO_BUCKET_COUNT]
          << endl;
#endif
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
    serial_dijkstra(V, average_degree, S, generate_mode, start_vertex, lmax,
                    reference);
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
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++) {
      histogram[i] = 0;
      vcount[i] = 0;
    }
    vcount[HISTO_BUCKET_COUNT] = 0;
    partition_index = new long[dividers];
    for (int i = 0; i < dividers; i++) {
      partition_index[i] = partition[i];
    }
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
    vcount[HISTO_BUCKET_COUNT] += num_vertices;
    flush_rng = VertexRng(thisIndex, S);
    heap_threshold = initial_threshold;
    tram_threshold = initial_threshold + 2;
    bfs_threshold = heap_threshold;
    pq_hold = new std::vector<Update>[HISTO_BUCKET_COUNT];
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++)
      pq_hold[i].reserve(4096);
    info_array = new long[histo_reduction_width + 7];
    bucket_multiplier = HISTO_BUCKET_COUNT / (HISTO_BUCKET_COUNT * log(V));
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
    bucket_multiplier = HISTO_BUCKET_COUNT / (HISTO_BUCKET_COUNT * sqrt(V));
#ifdef INFO_PRINTS
    ckout << "Generating local graph on PE " << CkMyPe() << " with "
          << num_vertices << " vertices" << endl;
#endif
    long side_length = mesh_side_length(V);
    bucket_multiplier = HISTO_BUCKET_COUNT / (HISTO_BUCKET_COUNT * sqrt(V));
    cost max_edges_sum = 0;
    std::vector<Edge> adjacency; // one scratch buffer, reused for every vertex
    local_graph.begin(num_vertices, 4 * num_vertices);
    for (long i = 0; i < num_vertices; i++) {
      long this_vertex = i + start_vertex;
      // See graph_gen.h: identical adjacency for any PE count.
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
    ckout << "Generating local graph on PE " << CkMyPe() << " with "
          << _num_vertices << " vertices and " << _num_edges << " edges"
          << endl;
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
        // so the graph does not change with PE count. See graph_gen.h.
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

  void get_graph(LongEdge *edges, long E, long *partition, int dividers) {
    actual_edges = E;
    initialize_data(partition, dividers);
    local_graph.build_from_edges(num_vertices, start_vertex, edges, E);
    // reduce largest edge: edges are sorted by weight within a vertex, so the
    // heaviest one is the last.
    cost max_edges_sum = 0;
    for (long i = 0; i < num_vertices; i++) {
      long degree = local_graph.degree(i);
      if (degree > 0)
        max_edges_sum += local_graph.edges(i)[degree - 1].distance;
    }
    CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
    contribute(sizeof(cost), &max_edges_sum, CkReduction::sum_long, cb);
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
    process_update(new_vertex_and_distance);
    // arr[thisIndex].process_heap();
  }

  static void process_update_caller(void *p, Update *new_vertex_and_distances,
                                    int count) {
    // ckout << "PE " << CkMyPe() << " receiving " << count << " updates" <<
    // endl;
    for (int i = 0; i < count; i++) {
      ((SsspChares *)p)->process_update(new_vertex_and_distances[i]);
    }
    //((SsspChares *)p)->process_heap();
  }

  static int get_dest_proc_local_caller(void *p, Update new_upd) {
    return ((SsspChares *)p)->get_dest_proc_local(new_upd);
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
    contribute_histogram(behind_first_nonzero);
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
    long msg_stats[8 + HISTO_BUCKET_COUNT];
#else
    long msg_stats[7 + HISTO_BUCKET_COUNT];
#endif
    msg_stats[0] = wasted_updates;
    msg_stats[1] = rejected_updates;
    for (int i = 0; i < HISTO_BUCKET_COUNT + 1; i++) {
      msg_stats[i + 2] = vcount[i];
    }
    msg_stats[3 + HISTO_BUCKET_COUNT] = updates_noted;
    msg_stats[4 + HISTO_BUCKET_COUNT] = actual_edges;
    msg_stats[5 + HISTO_BUCKET_COUNT] = distance_changes;
    // Topology bytes actually held, summed over PEs. Reported from inside the
    // run because peak RSS cannot see it: this runtime reserves a fixed ~554 MB
    // regardless of graph or PE count, which swamps the graph until it passes a
    // few million vertices.
    msg_stats[6 + HISTO_BUCKET_COUNT] =
        (long)(local_graph.bytes() + sizeof(cost) * (size_t)num_vertices);
#ifdef PAPI
    msg_stats[7 + HISTO_BUCKET_COUNT] = values[0];
#endif

    CkCallback cb(CkReductionTarget(Main, done), mainProxy);

#ifdef PAPI
    contribute((8 + HISTO_BUCKET_COUNT) * sizeof(long), msg_stats,
               CkReduction::sum_long, cb);
#else
    contribute((7 + HISTO_BUCKET_COUNT) * sizeof(long), msg_stats,
               CkReduction::sum_long, cb);
#endif
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
