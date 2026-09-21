#include "TopoManager.h"
#include "htram_group.h"
#include "sssp_smp.decl.h"
#include "graphlib/graphlib.h"
#include "process_work.h"
#include "live_slack.h"
#include "graphlib/tile_layout.h"
#ifdef PAPI
#include "acic_prof.h"
#endif
#ifdef ACIC_COMM_SHARE
#include <x86intrin.h>
// Step 7.6o: how a PE's solve splits into the solver's own work and
// everything else (sends, network progress, the scheduler, waiting). Work is
// the time inside process_heap() and the TRAM delivery callback, less the
// htram sends made from inside them; one rdtsc pair per call, so cheap next
// to the per-item cost. Build with -DACIC_COMM_SHARE in CHARMC_SMP, so htram
// counts its sends too.
namespace comm_share {
thread_local unsigned long work_tsc = 0, work_send_tsc = 0;
thread_local int depth = 0;
struct Work {
  unsigned long t0, s0;
  Work() {
    if (depth++ == 0) {
      t0 = __rdtsc();
      s0 = htram_send_tsc;
    }
  }
  ~Work() {
    if (--depth == 0) {
      work_tsc += __rdtsc() - t0;
      work_send_tsc += htram_send_tsc - s0;
    }
  }
};
thread_local unsigned long window_tsc0 = 0;
thread_local double window_s0 = 0;
// Step 8e: time between the scheduler's begin-idle and end-idle conditions,
// less the solver work the idle callback did inside it (process_heap runs
// from there), so idle + work never counts a tick twice.
thread_local unsigned long idle_tsc = 0, idle_t0 = 0, idle_work0 = 0;
inline void idle_begin(void *) {
  idle_t0 = __rdtsc();
  idle_work0 = work_tsc;
}
inline void idle_end(void *) {
  if (idle_t0) {
    idle_tsc += (__rdtsc() - idle_t0) - (work_tsc - idle_work0);
    idle_t0 = 0;
  }
}
} // namespace comm_share
#define COMM_SHARE_WORK comm_share::Work comm_share_work_;
#else
#define COMM_SHARE_WORK
#endif
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sstream>

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
// Expansion lead over the frontier, binned the same way (step 8a).
#define LEAD_CLASSES 12
int histo_reduction_width = HISTO_BUCKET_COUNT / 8;
double reduction_delay =
    0.1;                   // each histogram reduction happens at this interval
int initial_threshold = 3; // initial histo threshold
bool verify_mode = false;  // --verify: check the result against serial Dijkstra
enum { PROCESS_SHARE_OFF = 0, PROCESS_SHARE_ON = 1, PROCESS_SHARE_AUTO = 2 };
int process_share_mode = PROCESS_SHARE_OFF;
enum { PROCESS_QUEUE_LOCAL = 0, PROCESS_QUEUE_NEAREST = 1 };
int process_queue_policy = PROCESS_QUEUE_LOCAL;
long reader_tile_size = 0;
int reader_tile_owners = 1;
int slack_control_mode = PROCESS_SHARE_OFF;
static bool slack_control_active() {
  return slack_control_mode == PROCESS_SHARE_ON ||
         (slack_control_mode == PROCESS_SHARE_AUTO && num_global_edges < 8L * V);
}
static bool process_share_active() {
  return process_share_mode == PROCESS_SHARE_ON ||
         (process_share_mode == PROCESS_SHARE_AUTO && num_global_edges < 8L * V);
}
bool result_digest = false; // --result-digest: emit distances' digest after timing
// Everything needed to rebuild the graph from scratch, used by the serial
// reference. Filled in by Main; not a Charm readonly, because only PE 0 needs
// it and the chares get their slice through their own entry methods.
GraphSpec graph_spec;
// Flush the aggregation buffers once every this many controller rounds, on
// average -- each chare draws independently, so this is a rate and not a
// period, and --flush-interval makes it an A/B. Under the default
// --flush-policy adaptive (below) this draw is the floor, not the whole cadence.
//
// It matters more than it looks. htram's own idle-triggered flush is compiled
// out (IDLE_FLUSH at htram_group.h:7; idleFlush() returns true and does
// nothing) and the periodic timer is off by default, so a partly-filled
// aggregation buffer has exactly two ways out: fill to bufSize, or catch one of
// these draws. In the tail there is not enough traffic left to fill anything,
// which is the mechanism H4 of step 6 is about.
int flush_round_interval = 5;
// --flush-policy fixed|stale|adaptive. Step 6 found the cadence above is the
// whole story on a high-diameter graph -- flushing every round is 3.2x faster
// on the mesh -- and irrelevant or harmful on RMAT, where buffers fill on their
// own. Both non-fixed policies only ever add flushes to what the fixed draw
// does; the draw still runs.
//
//   stale     flush, every round, each destination whose buffer has not filled
//             since the previous round. Local and per destination. Rejected:
//             on two nodes a round is shorter than the time an RMAT stream
//             takes to fill a buffer, so it fires on half of RMAT's streams,
//             sends 26% more messages and costs 12%. Kept so that the A/B
//             that rejected it can be rerun.
//   adaptive  the same per-destination rule, applied only in rounds where the
//             controller sees too little work in flight to fill the buffers
//             at all -- fewer items in the window than one bufSize per
//             (sender PE, destination) stream. The mesh never has that much
//             in flight, so it flushes every round; RMAT and the uniform graph
//             have it for the middle of the run, so they flush as they did.
//             The default: 3.6x faster on the mesh on one node and 3.8x on
//             two, and no slower on RMAT or the uniform graph on either. See
//             design/step7-flush-cadence.md.
//
// fixed is the pre-step-7 behaviour and what every step 6 number used.
enum { FLUSH_FIXED = 0, FLUSH_STALE = 1, FLUSH_ADAPTIVE = 2 };
int flush_policy = FLUSH_ADAPTIVE;
// --combine off|hold. hold turns on htram's source-side CombiningHold: an
// update waits in its destination's hold until a full buffer's worth has been
// admitted or a flush reaches it, and a later update for the same vertex folds
// into it, keeping the smaller distance. The loser never travels; absorb()
// below does the bookkeeping it would otherwise have done at the receiver.
// Step 6 measured RMAT's rejected updates at 1.14 per edge against a
// batch-local fold's reach of 9.9%; this is the part whose reach is not bounded
// by one message. See design/step7-combining.md.
enum { COMBINE_OFF = 0, COMBINE_HOLD = 1 };
int combine_mode = COMBINE_OFF;
// --batch-fold off|on: the receiver-side half. Before a delivered batch is
// processed, updates in it that repeat a destination vertex are folded to the
// one with the smallest distance, and the rest are accounted as processed.
// Step 6 measured what this can reach -- 41.6% of a batch at 2^14 on RMAT,
// 9.9% at 2^20 -- and the plan requires its contribution to be reported
// separately from the source hold's.
bool batch_fold = false;
// --send-filter-bits N (0 = off). 7.6j. Each PE remembers, in a direct-mapped
// table of 2^N entries, the last distance it created an update with for a
// vertex, and drops a new update to that vertex that is no shorter. The one it
// remembers is still on its way and will be retired at the destination, so the
// dropped one could only have been rejected there -- 98.8% of arrivals were --
// and dropping it before it is charged keeps the histogram exact. A collision
// only overwrites the entry, which costs a missed drop, never a wrong one.
int send_filter_bits = 0;
// --send-filter auto (step 7.6k, the default): the filter at 17 bits, on only
// while the
// buffer size says the input is in the redundant, scale-free regime (at least
// SEND_FILTER_MIN_BUFFER items; see --bufsize-policy). It is a loss where most
// arrivals improve a distance: road-usa 0.79x at 1024 items. An explicit
// --send-filter-bits N keeps it on for the whole run instead (0: off).
bool send_filter_auto = true;
// --lazy-heavy off|auto|<distance> (step 8d). Off: a vertex relaxes every edge
// each time process_heap() finds its distance current, as before. On: it
// relaxes its light edges (weight <= L) then, and leaves the rest behind one
// token per weight range (L, 2L], (2L, 4L], ... The token for range j is
// queued at d + L 2^j, below every update it can make, and charged to the
// histogram like an update at that distance, so the controller admits it
// exactly when it would admit the first update it will make. When it is
// admitted it relaxes its range only if d is still the vertex's distance, and
// queues the next range's token. A vertex that improves meanwhile never sends
// the heavy updates of the distance it left, which is where 8a found the
// growth: at 8 nodes rmat25 created 3.7 heavy updates per heavy edge, and
// RIKEN, which relaxes heavy edges once per settled vertex, sends 0.5.
// on: L is the natural bucket width. auto: the same, but only for inputs of
// average degree 8 or more -- the scale-free regime, the same line the send
// filter draws (SEND_FILTER_MIN_BUFFER). On mesh24 and road-usa at 2 nodes
// every edge is heavy, deferring them lengthens the improvement chains, and
// the solve was 2.1x slower (job 20820633). The token lives only in the local
// queue; it never reaches htram.
enum { LAZY_OFF = 0, LAZY_ON = -1, LAZY_AUTO = -2 };
// auto is the default from step 8e: at 8 nodes rmat25 0.78 -> 0.21 s and
// orkut 0.34 -> 0.20 s with the hold bitmap and the idle-flush interval, and
// it never turns on for the high-diameter graphs.
long lazy_cut = LAZY_AUTO;
// --lazy-heap P: the heap percentile while --lazy-heavy is active, in place of
// the positional one. Tokens make running ahead cheap -- a stale range is
// dropped rather than sent -- so fewer, wider rounds pay: rmat25 at 8 nodes
// went 0.81 s (0.005) -> 0.47 s (0.5) -> 0.42 s (0.95), job 20820559.
double lazy_heap_percentile = 0.95;
// --lazy-growth G: token range j is (L G^j, L G^(j+1)] (default 2). A larger G
// means fewer tokens per vertex -- fewer queue operations -- and a coarser
// deferral of the heavier edges (step 8f).
int lazy_growth = 2;
// --skip-empty auto|on|off (IPDPS ablation): whether a node delivery skips
// the PEs it has no items for (HTram::setSkipEmptyDeliveries). auto follows
// the lazy-relaxation regime, as it always has; on/off separate the two.
enum { SKIP_EMPTY_OFF = 0, SKIP_EMPTY_ON = 1, SKIP_EMPTY_AUTO = 2 };
int skip_empty_mode = SKIP_EMPTY_AUTO;
// --control reduction|node (step 8c). reduction: each round is a Charm++
// reduction to Main and an array broadcast back, as always. Both travel in
// the PEs' own queues, which Reconverse polls only when the node queue --
// where every htram delivery lands -- is empty, so under load a round waits
// for the backlog at every hop: 5-7 ms per round at 8 nodes once the rounds
// became the critical path (8d). node: the PEs of a process sum their
// contributions in shared memory and the last one sends the sum to process 0
// on the node queue; the thresholds come back the same way, into shared
// state that each PE picks up at its next delivery, heap pass or idle pass
// (and, failing those, from a message in its own queue).
//
// The reduction path is paced by the queue it waits in; this one is not, and
// a round per delivery would let the rounds' own messages outgrow the queue
// (a single PE livelocked that way). So a PE keeps at most one heap pass and
// one fallback message queued, and process 0 leaves at least
// --control-interval ms between broadcasts.
enum { CONTROL_REDUCTION = 0, CONTROL_NODE = 1 };
int control_mode = CONTROL_REDUCTION;
double control_interval_ms = 0.25;
CProxy_ControlNode controlProxy;
class Main;
Main *main_instance = nullptr;
// --warm-links on|off (step 8e). Before the solve, every PE sends one small
// message to a PE of every other process and the solve starts at quiescence.
// ACIC reads its partition locally, so without this the first message
// between two processes is sent inside the timed solve: at 8 nodes rmat25's
// first round took 13.7 ms with 30 updates in flight, and orkut's first five
// rounds 2-6 ms each with almost none, against 0.1-0.2 ms for an idle round
// later on. The baselines' setup already exchanges data all-to-all. Off by
// default: it shortened the first round (11.8 -> 5.3 ms on rmat25 at 8 nodes)
// but not the solve, rmat25 0.250 -> 0.235 s and orkut 0.156 -> 0.169 s,
// within the noise (job 20823450).
bool warm_links_on = false;
// --lazy-skew CV (IPDPS change 2): auto also needs the degree distribution
// to be skewed, a coefficient of variation of at least CV over vertices with
// edges (default 1; 0 restores 8g's average-degree rule). Lazy relaxation
// pays by cutting the traffic that hub re-relaxations send: at 2 nodes it
// takes rmat25 from 1.25e9 to 8.7e8 updates and orkut from 4.0e8 to 1.9e8,
// but leaves uniform25 at 1.07e9 while adding rounds, and uniform25 ran 1.55x
// faster without it (job 20826260). Degree CV: rmat20-27 6.7-13.5, youtube
// 9.6, orkut 2.0; uniform 0.32, road-usa 0.39, meshes 0.01. PE 0 computes it
// from the offsets array it already reads for the partition (mode 4); -1
// (other modes) keeps the average-degree rule alone.
double degree_cv = -1.0;
double lazy_skew_min = 1.0;
static bool lazy_active() {
  return lazy_cut > 0 || lazy_cut == LAZY_ON ||
         (lazy_cut == LAZY_AUTO && num_global_edges >= 8L * V &&
          (degree_cv < 0.0 || degree_cv >= lazy_skew_min));
}
const int SEND_FILTER_AUTO_BITS = 17;
const int SEND_FILTER_MIN_BUFFER = 2048;
// --idle-flush off|on|starved. The per-chare [whenidle] callback drains the
// heap; with this on, a PE that finds nothing admissible left also flushes
// every destination still holding admitted updates. An idle PE cannot add to
// its buffers until a message arrives, so a buffer it holds only waits for the
// next round boundary. --flush-policy acts at those boundaries; this acts
// between them.
//   on       whenever the PE goes idle.
//   starved  only while the last round was starved (the --flush-policy
//            adaptive gate), so a PE that is momentarily idle in the middle
//            of an RMAT run does not turn full buffers into partial ones.
enum { IDLE_FLUSH_OFF = 0, IDLE_FLUSH_ON = 1, IDLE_FLUSH_STARVED = 2 };
// starved by default: it is a 1.09x speedup on the one-node mesh and 1.12x on
// two-node RMAT, and within the harness's own position bias everywhere else,
// over 20 repetitions with a repeated-baseline control. Ungated (`on`) it is
// 1.12-1.16x slower on the uniform graph, where it doubles the message count
// by turning full buffers into partial ones. See design/step7-idle-flush.md.
int idle_flush_policy = IDLE_FLUSH_STARVED;
// --idle-flush-interval auto|<us> (step 8e): at least this long between two
// idle flushes of a PE that sent something; 0 lets every idle scheduler pass
// flush, as before. See HTram::setIdleFlushInterval. Once the hold bitmap
// made a flush cheap, idle PEs flushed 4x as often and road-usa at 2 nodes
// ran 2x slower (1.73 -> 3.48 s, job 20821099). auto (-1, the default) is
// 30 us at average degree 8 or more and 100 us below, the best of 0/10/30/100
// for each class (job 20821256-57): rmat25 at 8 nodes 0.212 s at 30 against
// 0.259 at 100; mesh24 0.441 s at 100 against 0.547 at 30.
double idle_flush_interval_us = -1.0;
static double idle_flush_interval_seconds() {
  if (idle_flush_interval_us >= 0.0)
    return idle_flush_interval_us * 1e-6;
  return num_global_edges >= 8L * V ? 30e-6 : 100e-6;
}
// --bucket-policy fixed|adaptive, --bucket-target <buckets>. The histogram's
// bucket width is set once from |V| (log V, or sqrt V for the mesh) or by
// --bucket-width. Step 7.3's rerun of step 6's width sweep, on top of the 7.1
// flush policy, found that rule too fine on every graph class: 4x coarser is
// a 1.22x speedup on the mesh and 16x coarser is 1.39x on RMAT, while 1/4 of
// the rule is 1.2-1.6x slower on all three. Speedups here and in the design
// notes are baseline/variant, so above 1 is faster.
// See design/step7-bucketing.md.
//
//   adaptive  each round, Main measures the band holding the middle 90% of
//             the reduced histogram's mass. When that band spans at least
//             2 * target buckets it merges every k = band / target adjacent
//             buckets into one, on every PE and in htram's holds. Merging by
//             an integer factor is exact -- floor(floor(x) / k) is
//             floor(x / k) -- so no in-flight update is ever counted in one
//             bucket and uncounted from another. Coarsening only: the finer
//             resolution cannot be recovered, because the histogram counts
//             updates that are in flight and no PE holds them.
//
// adaptive with target 8 is the default: a 1.14x speedup on the mesh and 1.11x
// on RMAT at 2^20 on one node, against a same-configuration control at 1.03x
// and 0.95x, and never measurably slower at 2^20 or 2^22 on one or two nodes.
// fixed is the pre-7.3 behaviour and what every step 6, 7.1 and 7.2 number
// used. --combine hold falls back to fixed unless adaptive is asked for.
enum { BUCKET_FIXED = 0, BUCKET_ADAPTIVE = 1 };
int bucket_policy = BUCKET_ADAPTIVE;
int bucket_target = 8;
bool bucket_policy_given = false; // Main only: set by --bucket-policy
// --two-tier-per-pe <n>, --two-tier-absolute <n>. A round holding fewer than
// this many updates in its reduced window is treated as too small to describe
// a distribution: both admission percentiles go to 0.9999 and coarsening is
// skipped. The rule has always been N * 100 with N the total PE count, which
// makes the same graph on more PEs enter the branch at a proportionally larger
// population -- a scale dependence nobody chose. --two-tier-absolute pins the
// count instead, so the two can be told apart. Negative keeps the per-PE rule.
int two_tier_per_pe = 100;
long two_tier_absolute = -1;
// --coarsen-clamped block|allow|strict. The clamp bucket is the one bucket a
// merge cannot place: its contents mean "at or past the top of the range", not
// an index, so a merge sends them somewhere no retirement will look. block is
// the shipped rule -- refuse while the reduced clamp count is positive -- and
// it reads that count as of the previous contribution, which leaves a gap.
// strict closes the gap by refusing for the rest of the run once any update
// has been charged there. allow removes the guard, for measuring what it costs.
enum { CLAMP_BLOCK = 0, CLAMP_ALLOW = 1, CLAMP_STRICT = 2 };
int coarsen_clamp_policy = CLAMP_BLOCK;
// --window-follow off|on. The reduced window starts at the lowest bucket last
// seen occupied and never moves to anything it cannot see, so work that jumps
// past its right edge leaves it stranded: every round from then on admits
// everything, which is safe but is no admission control at all. on slides the
// window up by one width whenever it is empty and work is outstanding, which
// finds the frontier in at most HISTO_BUCKET_COUNT / width rounds.
int window_follow = 0;
// --timeout <seconds>: abandon a run that has not converged. 0 disables it.
// There is deliberately no default: a truncated run is a failed run, and the
// old behaviour was to give up after 30 s and print the partial distances with
// the same banner and the same exit status as a converged run.
double timeout_seconds = 0.0;
// --bucket-width <w>: distance units per histogram bucket, replacing whichever
// rule --bucket-width-rule selects. 0 keeps the derived rule, so this is inert
// unless passed. It is what makes H1 of step 6 an A/B rather than a rebuild.
double bucket_width_override = 0.0;
// --bucket-width-rule logv|weight. Which rule derives the width when
// --bucket-width does not replace it wholesale.
//
// logv is what every measurement before this was taken with: log(V) for the
// file and random-graph modes, sqrt(V) for the generated mesh. It reads
// nothing about the distances being bucketed, and 7.6b priced that. The
// histogram spans 2048 * width, so a graph is representable only while the
// derived width exceeds max_distance / 2048, and the three campaign graphs
// that fail that test -- road-ny by 50x, mesh22 by 16x, mesh20 by 9x -- are
// exactly the three where pinning the width was worth anything. See
// design/step76-controller.md.
//
// weight spans the histogram over 2048 maximum-weight edges instead: the width
// is the heaviest edge in the graph, which is a distance, and is already
// reduced at build time. It is sufficient whenever the deepest branch of the
// shortest-path tree averages at most max_weight * 2048 / depth per hop, which
// holds with a factor of four to spare on every graph measured here. It is a
// rule and not a bound -- no cheap bound on the distance range is available:
// max_sum, the one global the solver already computed, is the sum of every
// vertex's heaviest out-edge and so is three orders of magnitude too loose to
// divide by 2048.
//
// **logv is the default, and weight is not, because weight was measured and
// lost.** Job 22061958 ran both over all nine campaign graphs at one node and
// 120 workers: weight is 2.73x faster on road-ny on every source, and 2.4x to
// 2.9x slower on rmat20 and rmat20-s2 on every source, with rmat22 and youtube
// losing on three sources of four. The reason is visible in the bucket scale:
// the graphs weight loses on are exactly the ones whose adaptive coarsening
// already reaches scale 7-10 by itself, so log(V) times that scale is an
// effective width near 112 and weight overrides it with 1000. The graphs it
// wins on are exactly the ones stuck at scale 1. The width rule helps only
// where the controller cannot help itself. See design/step76-width-repair.md.
enum { WIDTH_RULE_LOGV = 0, WIDTH_RULE_WEIGHT = 1 };
int bucket_width_rule = WIDTH_RULE_LOGV;
// --clamp-freeze on|off. Whether the clamp threshold moves when buckets merge.
//
// off is the shipped behaviour: coarsen_buckets raises bucket_limit alongside
// bucket_scale, so an update clamped at creation is not clamped at retirement
// and its decrement lands in an ordinary bucket while its increment sits on
// 2047. That is the one thing that breaks the merge identity
// floor(floor(x/s)/k) == floor(x/(s*k)), and --coarsen-clamped exists only to
// stop merging before it can happen -- which is why a run that once reaches
// the clamp bucket never coarsens again.
//
// on freezes the threshold in distance space and leaves bucket 2047 out of the
// merge, making it an overflow slot rather than an index: increment and
// decrement both land there for the life of the run, and conservation holds
// for every bucket. The price was that coarsening could no longer extend the
// representable range, and that was said here to be tolerable because the
// width rule above would keep the range from needing extension. 7.6d refuted
// that: the width rule is a per-case setting, so on a graph running the
// default rule the frozen range is all the range there is. --range-extend
// below buys it back, using the same freeze -- a fixed overflow index -- as
// the thing that makes a creation-time flag mean something.
bool clamp_freeze = true;
// --range-extend on|off. Raise the clamp threshold when the overflow slot
// starts holding a real share of the live population, so that a graph whose
// distances run past 2048 * width does not spend most of its rounds with
// everything in one bucket and no ordering left to schedule by. Needs the
// frozen clamp underneath it -- the overflow slot has to stay at a fixed index
// for an update's creation-time flag to mean anything -- so it does nothing
// when --clamp-freeze is off. It is also compiled out of a VCOUNT build:
// vcount is indexed by a vertex's current distance and re-derived at each
// change, so a clamp that moves between two of those lookups unbalances it.
// VCOUNT is diagnostic only, and it is the one accounting the bit cannot
// carry.
//
// Default off. It is worth 10x on mesh20 and 3.6x less work on road-ny in
// eight-PE runs on a login node, and zero extensions fire on a graph already
// in range -- but 7.6d made a width rule the default on a prior and then
// measured it losing 2.9x, and the order of those two steps was the mistake.
// This ships as a flag with a campaign arm behind it.
bool range_extend = false;
// --skew-defer off|on. 7.6g. At bucket scale 1, index HISTO_BUCKET_COUNT - 1 is
// two things at once: the overflow slot, and the top real bucket, holding
// distances in [2047, 2048) widths. charge_new_update() flags both at creation,
// so a creator never disagrees with itself about it. A receiver that has not
// yet applied a coarsening its creator already has does: the creator, at scale
// k, charges such an update to 2047 / k and leaves it unflagged, and the
// receiver, still at scale 1, computes 2047 -- the one index coarsen_buckets()
// leaves out of the merge. The update is then either parked in pq_hold[2047],
// where no threshold below 2047 ever releases it, or rejected against
// histogram[2047], leaving +1 at the creator's 2047 / k that no retirement will
// take away. Either way the window pins at floor(2047 / scale), which is where
// every recorded stall pinned.
//
// An unflagged update this PE would charge to 2047 can only have been created
// under a different scale, so it is counted (skew_top_arrivals) always, and
// with this on it waits one broadcast in the same deferral created_beyond_my_
// clamp() uses. Off reproduces the shipped behaviour exactly.
//
// Not the cause of the 7.6g deadlock, and never observed. It was the first
// explanation tested, because it predicts the same pin; but skew_top_arrivals
// was 0 in every one of 160 mesh20/mesh22 runs at 8 x 15 and in every
// single-process sweep, including all the runs that hung. The deadlock was
// queue order (--pq-overflow-last). The counter stays because it is one
// comparison on a path that already computes the bucket, and it is the receipt
// that the race it describes is not happening; the flag stays off.
bool skew_defer = false;
// The overflow slot must hold this share of the live population before the
// clamp is raised. It is the one tuned number in the rule: a single freak edge
// should not cost every bucket half its resolution, and a graph that is
// genuinely out of range passes an eighth within a few rounds.
const double RANGE_EXTEND_SHARE = 0.125;
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
// Aggregation buffer size in items, settable with --bufsize (at most htram's
// BUFSIZE). The best size depends on the graph class (step 7.6j, compact
// items, two nodes): rmat25 was fastest at 6144 (1.3x over 2048) while mesh24
// and road-usa were 2x slower there and fastest at 1024 (1.2x and 1.4x over
// 2048). 2048 is the one size that regressed neither class against the
// pre-7.6j build. Under --bufsize-policy fixed it is the size for the whole
// run; under acceptance it is only the size the run starts with.
int buffer_size = 2048;
// --bufsize-policy fixed|acceptance (step 7.6k; acceptance is the default
// from 7.6k on, and an explicit --bufsize without a policy means fixed, as it
// did before). acceptance lets the
// controller choose the size from the share of arriving updates that improve
// a distance. That share separates the graph classes by 5-10x and hardly
// moves with the buffer size itself (mesh24 0.28 at 1024, 2048 and 6144;
// road-usa 0.43; rmat25 0.05; orkut 0.03), so steering by it cannot chase its
// own tail. A high share means long improvement chains, where every item held
// back in a buffer delays the next link and multiplies work (mesh24 created
// 4.7x more updates at 6144 than at 1024). A low share means most arrivals
// are redundant, the delay costs little, and fewer, larger sends pay.
// The size is --bufsize-acc-items / share, clamped to --bufsize-range: 288
// puts mesh24 at about 1024 and rmat25 at about 5700, the two measured optima.
//
// The share cannot pick the size a run starts with. On rmat25 at two nodes
// the first round that has seen enough updates to judge arrives after 40% of
// all the run's updates were created, so a run that starts at 2048 does its
// ramp at 2048 whatever the policy says later (1.38x slower than a fixed
// 6144, job 20767814). The starting size is therefore 256 items per unit of
// average degree, which the solver knows before the first edge moves:
// scale-free inputs have high degree and low acceptance, high-diameter inputs
// low degree and high acceptance, so the two estimates agree on every graph
// measured, and the share only corrects the start.
// The ceiling is 6144 because 8192 was slower than 6144 on rmat25 (7.6j).
enum { BUFSIZE_FIXED = 0, BUFSIZE_ACCEPTANCE = 1 };
int bufsize_policy = BUFSIZE_ACCEPTANCE;
double bufsize_acc_items = 288.0;
int bufsize_min = 512;
int bufsize_max = 6144;

// Multiples of 256 items within --bufsize-range, so nearby estimates name the
// same size and every PE computes the same one.
static int quantize_buffer_size(double want) {
  int items = (int)std::min(want, (double)bufsize_max);
  items = std::max(256, (items + 128) / 256 * 256);
  return std::max(bufsize_min, std::min(bufsize_max, items));
}

// The size every PE starts the solve with.
static int initial_buffer_size() {
  if (bufsize_policy != BUFSIZE_ACCEPTANCE)
    return buffer_size;
  // The exact ratio: average_degree is truncated, which reads a mesh's 3.99
  // as 3.
  return quantize_buffer_size(256.0 * (double)num_global_edges /
                              (double)std::max(V, 1L));
}
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
  STAT_ABSORBED,      // updates folded away at the source by --combine
  STAT_FOLDED,        // updates folded away in a delivered batch, --batch-fold
  STAT_SKEW_TOP,      // unflagged arrivals charged to the overflow index, 7.6g
  STAT_SEND_FILTERED, // updates dropped by --send-filter-bits, 7.6j
  STAT_TOKENS,        // --lazy-heavy tokens queued, 8d
  STAT_TOKENS_STALE,  // ... and dropped because their vertex had moved
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
  // The live histogram at the end, HISTO_BUCKET_COUNT entries. Summed over
  // PEs every bucket must be zero: each update is counted where it was created
  // and uncounted where it was processed, and --bucket-policy adaptive merges
  // buckets while updates are in flight. A nonzero bucket is a merge, or a
  // bucket computation, that placed the two ends differently.
  STAT_HISTO_LIVE = STAT_ARR_ARRIVALS + DEGREE_CLASSES,
  // Largest disagreement, over every round, between htram's admitted counters
  // and a recount of what they describe (HTram::admittedDrift), summed over
  // PEs. Coarsening moves items across the threshold, so this is the check
  // that it moved the counters with them.
  STAT_ADMITTED_DRIFT = STAT_HISTO_LIVE + HISTO_BUCKET_COUNT,
  // Step 8a: where the work per edge goes as the PE count grows. A vertex's
  // edges are relaxed each time process_heap() finds its queued distance still
  // current; the last such expansion is at its final distance, so every other
  // one is speculation that lost (H5). Heavy edges are those longer than one
  // natural bucket width, RIKEN's light/heavy cut (H8).
  STAT_EXPANSIONS,        // generate_updates() calls
  STAT_HEAVY_CREATED,     // updates created over heavy edges
  STAT_REACHED_EXPANDABLE,// reached vertices with an edge: the least expansions
  STAT_REACHED_EDGES,     // their edges: the least updates any run creates
  STAT_REACHED_HEAVY,     // of which heavy
  STAT_ARRIVAL_SETTLED,   // arrivals at a vertex below the frontier, so final
  // Expansions by how far the vertex was above the frontier the PE last heard
  // of, in natural bucket widths (0, 1, 2-3, 4-7, ...), and of those the ones
  // at the vertex's final distance.
  STAT_LEAD_EXPANSIONS,
  STAT_LEAD_FINAL = STAT_LEAD_EXPANSIONS + LEAD_CLASSES,
  // Arrivals at final vertices by the target's degree class: what a filter
  // on settled hubs could remove at the sender.
  STAT_SETTLED_DEG = STAT_LEAD_FINAL + LEAD_CLASSES,
  // ACIC_COMM_SHARE builds only: TSC ticks summed over PEs.
  STAT_WORK_TSC = STAT_SETTLED_DEG + DEGREE_CLASSES,
                      // inside process_heap() or the delivery callback
  STAT_WORK_SEND_TSC, // of which htram sends
  STAT_SEND_TSC,      // every htram send in the window
  STAT_WINDOW_TSC,    // each PE's window, start_papi() to print_distances()
  STAT_WINDOW_US,     // the same windows in microseconds, to convert ticks
  STAT_IDLE_TSC,      // scheduler idle, less the work idle callbacks did
  STAT_SAME_PE_CHANGES,
  STAT_CROSS_PE_CHANGES,
  STAT_COST_METRICS,
  STAT_END = STAT_COST_METRICS + work_cost::COUNT
};

#if defined(ACIC_DIAG) || defined(ACIC_COMM_SHARE) || defined(ACIC_WORK_COST)
const int stat_count = STAT_END;
#elif defined(PAPI)
const int stat_count = STAT_INSTRUCTIONS + 1;
#else
const int stat_count = STAT_INSTRUCTIONS;
#endif

void start_reductions(void *obj, double time) { arr.contribute_histogram(0); }

// --pq-overflow-last off|on. 7.6g's second candidate. process_heap() stops at
// the first queue top whose bucket is above the heap threshold, which is only
// correct if bucket never decreases along the queue's order. Ordered by
// distance alone it can: at bucket scale 1, charge_new_update() flags the
// in-range slice [2047, 2048) widths as overflow, so such an update keeps
// bucket 2047 for life, while an update created after a coarsening by k with a
// slightly larger distance gets floor(2047 / k). If the flagged one was admitted
// to the queue while the threshold was 2047 and the threshold then fell with the
// coarsening, it sits on top, above the threshold, and every admissible update
// behind it waits -- at exactly floor(2047 / scale), where the stalls pinned.
// on sorts every flagged update after every unflagged one, which makes bucket
// monotone along the queue again. Read once at startup, so the heap's order is
// fixed for the life of the run.
//
// Default on. On Anvil, mesh22 at the recorded configuration (8 x 15, logv,
// frozen clamp, rescue disabled) hung 12 of 80 runs with it off and 0 of 80
// with it on, every digest correct, and was not slower where both finished
// (on faster in 49 of 68 pairs, median 1.02x). Every stall off pinned the window
// at floor(2047 / scale) with a bucket-2047 update on top of some PE's queue
// and admissible updates behind it. See design/step76-default-deadlock.md.
bool pq_overflow_last = true;

// The order is "flagged after unflagged, then by distance" when
// pq_overflow_last is on, and "by distance" when it is off. Both are one
// unsigned comparison of a key that puts the overflow flag above every
// distance bit: distances are non-negative, so they fit below bit 63. This is
// the same strict order the two-branch comparison defined, so the heap makes
// the same moves; it only stops paying for the branches on every sift step.
// The mask is taken from the readonly when the heap is built, which is after
// the readonlies have arrived.
struct ComparePairs {
  unsigned long flag_mask = pq_overflow_last ? ~0UL : 0UL;
  unsigned long key(const Update &u) const {
    return (unsigned long)u.distance |
           (((unsigned long)u.dest_vertex << 1) & (1UL << 63) & flag_mask);
  }
  bool operator()(const Update &lhs, const Update &rhs) const {
    return key(lhs) > key(rhs); // '>' for min heap
  }
};

struct ProcessDistanceKey {
  long operator()(const Update &u) const { return u.distance; }
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
// Why a round did or did not merge buckets. Item 2 of the step 7.5 next-work
// list asks for this directly: "record why each round can or cannot coarsen".
enum {
  COARSEN_MERGED = 0,        // merged, by the factor in coarsen_k
  COARSEN_POLICY_OFF = 1,    // --bucket-policy fixed
  COARSEN_TWO_TIER = 2,      // too little in flight to describe a distribution
  COARSEN_EMPTY = 3,         // nothing live in the reduced window
  COARSEN_CLAMP_LIVE = 4,    // updates charged to the clamp bucket right now
  COARSEN_CLAMP_SEEN = 5,    // --coarsen-clamped strict: some were, earlier
  COARSEN_NO_BAND = 6,       // the middle 90% of the mass has no two ends
  COARSEN_BAND_NARROW = 7,   // band < 2 * target: nothing to gain
  COARSEN_TOP_UNREACHABLE = 8, // the merged window would not reach the top
  COARSEN_NOT_ASKED = 9,       // the round ended before the question came up
  COARSEN_EXTEND = 10          // merged to raise the clamp, not to narrow the band
};

struct RoundRecord {
  double t;      // seconds since compute_begin
  long histogram_sum;
  int window_first;  // bucket index the reduced window starts at
  int first_nonzero; // the frontier: lowest bucket still holding work
  int occupied;      // buckets inside the window holding a positive count
  int span;          // last occupied bucket - first occupied + 1, or 0
  int heap_threshold;
  int tram_threshold;
  // Whether this round took the histogram_sum <= N*100 branch, which abandons
  // the configured percentiles for 0.9999 -- that is, admits everything. The
  // plan calls this the two-tier hack; knowing which round it fires on is the
  // difference between a tail that is cadence-bound and one that is bound by a
  // controller that has stopped controlling.
  int two_tier;
  // Whether this round told the chares that too little is in flight to fill
  // the aggregation buffers, which is what --flush-policy adaptive acts on.
  int starved;
  // Buckets merged into one since the start, under --bucket-policy adaptive.
  // Bucket indices in this row are in units of that many original buckets.
  int bucket_scale;
  // Why this round did or did not merge buckets, and by what factor if it did.
  int coarsen_reason;
  int coarsen_k;
  // What is sitting in the overflow slot, and how many arrived there this
  // round. --range-extend reads the second: the first does not fall when the
  // clamp rises, because a flagged update retires from the overflow slot
  // whatever the clamp becomes.
  long clamped;
  long clamped_arrivals;
  long updates_created;
  long updates_processed;
  long updates_noted;
  long distance_changes;
  long done_vertices;
  // Items per aggregation buffer the chares ran this round with.
  int buffer_size;
  long active_pes;
  double round_seconds;
  double slack_widths, slack_ratio, slack_idle;
  int slack_action;
};

class Main : public CBase_Main {
private:
  long start_vertex;
  long *partition_index;
  double start_time;
  // Every timer below is -1 until something assigns it, so an unmeasured
  // phase reports -1 rather than a plausible zero. Before this, read_time was
  // an uninitialized double that only MODE_CSV and modes 1-2 ever wrote, so
  // MODE_RMAT and MODE_GAPBS -- the whole benchmark campaign -- printed
  // whatever the allocation happened to hold. It read 0.0, which is why the
  // 7.5 note could say the field was unassigned but not that it was wrong.
  double read_time = -1.0;   // input only, where the mode can separate it
  double index_time = -1.0;  // MODE_GAPBS: header and offsets, read on PE 0
  double setup_time = -1.0;  // start_time -> solve start: input + build
  double stats_time = -1.0;  // solve end -> total: the statistics reduction
  double total_time = -1.0;
  // The heaviest edge in the graph, reduced at build time. The bucket width
  // is derived from it, so a run that reports one reports the other.
  cost graph_max_weight = 0;
  long max_index;
  int threshold_change_counter;
  int previous_threshold;
  int reduction_counts = 0;
  int no_incoming = 0;
  std::vector<double> reduction_times;
  long round_active_pes = 0;
  LiveSlack live_slack;
  std::vector<RoundRecord> rounds; // --diag; see RoundRecord
  bool first_qd_done = false;
  bool second_qd_done = false;
  int activeBucketMax = 10;
  int current_phase = 0; // 0=initial, 1=bfs, 2=converged_bfs
  int last_first_nonzero = 0;
  int bucket_scale = 1; // --bucket-policy adaptive: original buckets per bucket
  int coarsenings = 0;
  long previous_updates_created = 0;
  long previous_updates_processed = 0;
  // Consecutive rounds in which no update was created or retired anywhere.
  // Reported on a geometric schedule: a stalled run turns rounds over as fast
  // as the reduction allows, so a fixed period would bury the log.
  long stall_rounds = 0;
  long stall_report_at = 256;
  long stall_rescues = 0;
  // Consecutive no-progress reductions before the controller admits every
  // bucket to break a stall the window cannot describe; 0 disables it. The
  // stall report fires at 256, so this is deliberately far earlier: a global
  // standstill with live work outstanding is already pathological. Not 1,
  // because updates in flight are created but not yet retired, so a run under
  // heavy buffering can legitimately hold both counters still for a few
  // rounds.
  long stall_rescue_rounds = 32;
  int stall_reports = 0;
  // Rounds whose reduced window held no live update while work was still
  // outstanding, i.e. the frontier had moved past the window's right edge and
  // the controller could not see it. Such a round has to admit everything to
  // stay safe, so it is a round with no admission control at all. One
  // comparison per round to count, and the count is the difference between a
  // controller that is steering and one that is only along for the ride.
  long rounds_window_empty = 0;
  long max_above_window = 0;
  int range_extensions = 0; // --range-extend: times the clamp was raised
  long last_clamped_created = 0; // overflow arrivals as of the previous round
  long last_updates_created_seen = 0; // creations as of the previous round
  long extend_ready_at = 0;      // no extension before this round; see below
  long windows_slid = 0; // --window-follow on: windows advanced past empty
  // Consecutive rounds whose reduced window claimed more live updates than
  // exist. A reduction is not a global instant -- each PE contributes its own
  // state when the round reaches it -- so one such round can be an artifact of
  // that skew and is not worth a word. A real accounting error does not go
  // away, hence the run of rounds before anything is said.
  int above_negative_rounds = 0;
  bool conservation_warned = false;
  // Whether the reduced clamp count has ever been positive. --coarsen-clamped
  // strict refuses to merge from then on: the live count is read as of the
  // previous contribution, and a chare keeps creating updates between
  // contributing and receiving the broadcast that carries the merge, so a
  // count that is zero now does not mean none will be charged before the merge
  // lands. Monotone, so the answer cannot flap.
  bool clamp_ever_live = false;
  long previous_distance_changes = 0;
  // --bufsize-policy acceptance: the size the chares were last told, the
  // counters at the start of the sample being gathered, the smoothed share,
  // and how often the size changed.
  int current_buffer_size = 0;
  long acc_noted_mark = 0;
  long acc_changes_mark = 0;
  double smoothed_acceptance = -1.0;
  int buffer_size_changes = 0;
  std::vector<long> sources;
  std::string source_spec, base_diag_prefix;
  std::vector<unsigned long long> previous_tram_stats;
  double tram_percentile = 0.01;
  double heap_percentile = 0.01;
#ifdef PRINT_HISTO
  histogramSequence *histoSeq;
#endif

public:
  double compute_begin;
  double compute_time = -1.0; // set on convergence, or by the timeout handler
  bool run_truncated = false; // set by fast_exit; forces a nonzero exit
  size_t source_epoch = 0;
  bool source_running = false;
  int control_generation = 0; // --control node: rounds broadcast so far

  /**
   * Read in graph from csv (currently sequential)
   */
  Main(CkArgMsg *m) {
    N = CkNumPes();
    // Separate option flags from the positional arguments so flags may appear
    // anywhere on the command line.
    std::vector<std::string> args;
    bool bufsize_given = false, bufsize_policy_given = false;
    for (int i = 1; i < m->argc; i++) {
      if (m->argv[i] == NULL)
        continue;
      std::string arg = m->argv[i];
      if (arg == "--verify")
        verify_mode = true;
      else if (arg == "--result-digest")
        result_digest = true;
      else if (arg == "--reader-tile" || arg.rfind("--reader-tile=", 0) == 0) {
        const std::string value = arg == "--reader-tile"
            ? (i + 1 < m->argc ? m->argv[++i] : "") : arg.substr(14);
        if (value == "off") reader_tile_size = 0;
        else if (value == "auto") reader_tile_size = -1;
        else {
          size_t used = 0;
          reader_tile_size = std::stol(value, &used);
          if (used != value.size() || reader_tile_size < 1)
            CkAbort("--reader-tile must be off, auto, or a positive vertex count");
        }
      }
      else if (arg == "--slack-control" || arg.rfind("--slack-control=", 0) == 0) {
        const std::string value = arg == "--slack-control"
            ? (i + 1 < m->argc ? m->argv[++i] : "") : arg.substr(16);
        if (value == "off") slack_control_mode = PROCESS_SHARE_OFF;
        else if (value == "on") slack_control_mode = PROCESS_SHARE_ON;
        else if (value == "auto") slack_control_mode = PROCESS_SHARE_AUTO;
        else CkAbort("--slack-control must be off, on, or auto");
      }
      else if (arg == "--sources" || arg.rfind("--sources=", 0) == 0) {
        source_spec = arg == "--sources"
            ? (i + 1 < m->argc ? m->argv[++i] : "") : arg.substr(10);
        if (source_spec.empty()) CkAbort("--sources needs a comma-separated list");
      }
      else if (arg == "--process-share" || arg.rfind("--process-share=", 0) == 0) {
        const std::string value = arg == "--process-share"
            ? (i + 1 < m->argc ? m->argv[++i] : "") : arg.substr(16);
        if (value == "off") process_share_mode = PROCESS_SHARE_OFF;
        else if (value == "on") process_share_mode = PROCESS_SHARE_ON;
        else if (value == "auto") process_share_mode = PROCESS_SHARE_AUTO;
        else CkAbort("--process-share must be off, on, or auto");
      }
      else if (arg == "--process-queue" || arg.rfind("--process-queue=", 0) == 0) {
        const std::string value = arg == "--process-queue"
            ? (i + 1 < m->argc ? m->argv[++i] : "") : arg.substr(16);
        if (value == "local") process_queue_policy = PROCESS_QUEUE_LOCAL;
        else if (value == "nearest") process_queue_policy = PROCESS_QUEUE_NEAREST;
        else CkAbort("--process-queue must be local or nearest");
      }
      else if (arg.rfind("--timeout=", 0) == 0)
        timeout_seconds = std::stod(arg.substr(10));
      else if (arg == "--timeout") {
        if (i + 1 >= m->argc) {
          ckout << "--timeout needs a value in seconds" << endl;
          CkExit(1);
          return;
        }
        timeout_seconds = std::stod(m->argv[++i]);
      } else if (arg == "--bufsize-policy" || arg.rfind("--bufsize-policy=", 0) == 0) {
        bufsize_policy_given = true;
        std::string value;
        if (arg == "--bufsize-policy") {
          if (i + 1 < m->argc)
            value = m->argv[++i];
        } else
          value = arg.substr(17);
        if (value == "fixed")
          bufsize_policy = BUFSIZE_FIXED;
        else if (value == "acceptance")
          bufsize_policy = BUFSIZE_ACCEPTANCE;
        else {
          ckout << "--bufsize-policy must be fixed or acceptance" << endl;
          CkExit(1);
          return;
        }
      } else if (arg == "--bufsize-acc-items") {
        if (i + 1 >= m->argc) {
          ckout << "--bufsize-acc-items needs a value in items" << endl;
          CkExit(1);
          return;
        }
        bufsize_acc_items = std::stod(m->argv[++i]);
      } else if (arg == "--bufsize-range") {
        // MIN:MAX in items
        const std::string value = (i + 1 < m->argc) ? m->argv[++i] : "";
        const size_t colon = value.find(':');
        if (colon == std::string::npos) {
          ckout << "--bufsize-range needs MIN:MAX in items" << endl;
          CkExit(1);
          return;
        }
        bufsize_min = std::stoi(value.substr(0, colon));
        bufsize_max = std::stoi(value.substr(colon + 1));
      } else if (arg.rfind("--bufsize=", 0) == 0) {
        bufsize_given = true;
        buffer_size = std::stoi(arg.substr(10));
      } else if (arg == "--bufsize") {
        if (i + 1 >= m->argc) {
          ckout << "--bufsize needs a value in items" << endl;
          CkExit(1);
          return;
        }
        bufsize_given = true;
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
      } else if (arg.rfind("--flush-policy", 0) == 0) {
        std::string value;
        if (arg.rfind("--flush-policy=", 0) == 0)
          value = arg.substr(15);
        else if (arg == "--flush-policy" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "fixed")
          flush_policy = FLUSH_FIXED;
        else if (value == "stale")
          flush_policy = FLUSH_STALE;
        else if (value == "adaptive")
          flush_policy = FLUSH_ADAPTIVE;
        else {
          ckout << "--flush-policy must be fixed, stale or adaptive" << endl;
          CkExit(1);
          return;
        }
      } else if (arg.rfind("--combine", 0) == 0) {
        std::string value;
        if (arg.rfind("--combine=", 0) == 0)
          value = arg.substr(10);
        else if (arg == "--combine" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "off")
          combine_mode = COMBINE_OFF;
        else if (value == "hold")
          combine_mode = COMBINE_HOLD;
        else {
          ckout << "--combine must be off or hold" << endl;
          CkExit(1);
          return;
        }
      } else if (arg.rfind("--bucket-policy", 0) == 0) {
        std::string value;
        if (arg.rfind("--bucket-policy=", 0) == 0)
          value = arg.substr(16);
        else if (arg == "--bucket-policy" && i + 1 < m->argc)
          value = m->argv[++i];
        bucket_policy_given = true;
        if (value == "fixed")
          bucket_policy = BUCKET_FIXED;
        else if (value == "adaptive")
          bucket_policy = BUCKET_ADAPTIVE;
        else {
          ckout << "--bucket-policy must be fixed or adaptive" << endl;
          CkExit(1);
          return;
        }
      } else if (arg == "--two-tier-per-pe") {
        if (i + 1 >= m->argc) {
          ckout << "--two-tier-per-pe needs a number of updates per PE" << endl;
          CkExit(1);
          return;
        }
        two_tier_per_pe = std::stoi(m->argv[++i]);
      } else if (arg == "--two-tier-absolute") {
        if (i + 1 >= m->argc) {
          ckout << "--two-tier-absolute needs a number of updates" << endl;
          CkExit(1);
          return;
        }
        two_tier_absolute = std::stol(m->argv[++i]);
      } else if (arg == "--stall-rescue") {
        if (i + 1 >= m->argc) {
          ckout << "--stall-rescue needs a number of rounds (0 disables)"
                << endl;
          CkExit(1);
          return;
        }
        stall_rescue_rounds = std::stol(m->argv[++i]);
      } else if (arg.rfind("--coarsen-clamped", 0) == 0) {
        std::string value;
        if (arg.rfind("--coarsen-clamped=", 0) == 0)
          value = arg.substr(18);
        else if (arg == "--coarsen-clamped" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "block")
          coarsen_clamp_policy = CLAMP_BLOCK;
        else if (value == "allow")
          coarsen_clamp_policy = CLAMP_ALLOW;
        else if (value == "strict")
          coarsen_clamp_policy = CLAMP_STRICT;
        else {
          ckout << "--coarsen-clamped must be block, allow or strict" << endl;
          CkExit(1);
          return;
        }
      } else if (arg.rfind("--bucket-width-rule", 0) == 0) {
        std::string value;
        if (arg.rfind("--bucket-width-rule=", 0) == 0)
          value = arg.substr(20);
        else if (arg == "--bucket-width-rule" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "logv")
          bucket_width_rule = WIDTH_RULE_LOGV;
        else if (value == "weight")
          bucket_width_rule = WIDTH_RULE_WEIGHT;
        else {
          ckout << "--bucket-width-rule must be logv or weight" << endl;
          CkExit(1);
          return;
        }
      } else if (arg.rfind("--clamp-freeze", 0) == 0) {
        std::string value;
        if (arg.rfind("--clamp-freeze=", 0) == 0)
          value = arg.substr(15);
        else if (arg == "--clamp-freeze" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "off")
          clamp_freeze = false;
        else if (value == "on")
          clamp_freeze = true;
        else {
          ckout << "--clamp-freeze must be off or on" << endl;
          CkExit(1);
          return;
        }
      } else if (arg.rfind("--range-extend", 0) == 0) {
        std::string value;
        if (arg.rfind("--range-extend=", 0) == 0)
          value = arg.substr(15);
        else if (arg == "--range-extend" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "off")
          range_extend = false;
        else if (value == "on")
          range_extend = true;
        else {
          ckout << "--range-extend must be off or on" << endl;
          CkExit(1);
          return;
        }
      } else if (arg.rfind("--pq-overflow-last", 0) == 0) {
        std::string value;
        if (arg.rfind("--pq-overflow-last=", 0) == 0)
          value = arg.substr(19);
        else if (arg == "--pq-overflow-last" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "off")
          pq_overflow_last = false;
        else if (value == "on")
          pq_overflow_last = true;
        else {
          ckout << "--pq-overflow-last must be off or on" << endl;
          CkExit(1);
          return;
        }
      } else if (arg.rfind("--skew-defer", 0) == 0) {
        std::string value;
        if (arg.rfind("--skew-defer=", 0) == 0)
          value = arg.substr(13);
        else if (arg == "--skew-defer" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "off")
          skew_defer = false;
        else if (value == "on")
          skew_defer = true;
        else {
          ckout << "--skew-defer must be off or on" << endl;
          CkExit(1);
          return;
        }
      } else if (arg.rfind("--window-follow", 0) == 0) {
        std::string value;
        if (arg.rfind("--window-follow=", 0) == 0)
          value = arg.substr(16);
        else if (arg == "--window-follow" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "off")
          window_follow = 0;
        else if (value == "on")
          window_follow = 1;
        else {
          ckout << "--window-follow must be off or on" << endl;
          CkExit(1);
          return;
        }
      } else if (arg == "--bucket-target") {
        if (i + 1 >= m->argc) {
          ckout << "--bucket-target needs a number of buckets" << endl;
          CkExit(1);
          return;
        }
        bucket_target = std::stoi(m->argv[++i]);
      } else if (arg == "--idle-flush-interval") {
        if (i + 1 >= m->argc) {
          ckout << "--idle-flush-interval needs microseconds" << endl;
          CkExit(1);
          return;
        }
        const std::string value = m->argv[++i];
        idle_flush_interval_us = value == "auto" ? -1.0 : std::stod(value);
      } else if (arg.rfind("--idle-flush", 0) == 0) {
        std::string value;
        if (arg.rfind("--idle-flush=", 0) == 0)
          value = arg.substr(13);
        else if (arg == "--idle-flush" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "off")
          idle_flush_policy = IDLE_FLUSH_OFF;
        else if (value == "on")
          idle_flush_policy = IDLE_FLUSH_ON;
        else if (value == "starved")
          idle_flush_policy = IDLE_FLUSH_STARVED;
        else {
          ckout << "--idle-flush must be off, on or starved" << endl;
          CkExit(1);
          return;
        }
      } else if (arg == "--lazy-heavy") {
        const std::string value = i + 1 < m->argc ? m->argv[++i] : "";
        if (value == "off")
          lazy_cut = LAZY_OFF;
        else if (value == "on")
          lazy_cut = LAZY_ON;
        else if (value == "auto")
          lazy_cut = LAZY_AUTO;
        else if (!value.empty() && std::stol(value) > 0)
          lazy_cut = std::stol(value);
        else {
          ckout << "--lazy-heavy must be off, on, auto or a positive distance" << endl;
          CkExit(1);
          return;
        }
      } else if (arg == "--control") {
        const std::string value = i + 1 < m->argc ? m->argv[++i] : "";
        if (value == "reduction")
          control_mode = CONTROL_REDUCTION;
        else if (value == "node")
          control_mode = CONTROL_NODE;
        else {
          ckout << "--control must be reduction or node" << endl;
          CkExit(1);
          return;
        }
      } else if (arg == "--control-interval") {
        if (i + 1 >= m->argc) {
          ckout << "--control-interval needs milliseconds" << endl;
          CkExit(1);
          return;
        }
        control_interval_ms = std::stod(m->argv[++i]);
      } else if (arg == "--warm-links") {
        const std::string value = i + 1 < m->argc ? m->argv[++i] : "";
        if (value == "on" || value == "off")
          warm_links_on = value == "on";
        else {
          ckout << "--warm-links must be on or off" << endl;
          CkExit(1);
          return;
        }
      } else if (arg == "--lazy-skew") {
        if (i + 1 >= m->argc) {
          ckout << "--lazy-skew needs a coefficient of variation" << endl;
          CkExit(1);
          return;
        }
        lazy_skew_min = std::stod(m->argv[++i]);
      } else if (arg == "--skip-empty") {
        const std::string value = i + 1 < m->argc ? m->argv[++i] : "";
        if (value == "auto")
          skip_empty_mode = SKIP_EMPTY_AUTO;
        else if (value == "on")
          skip_empty_mode = SKIP_EMPTY_ON;
        else if (value == "off")
          skip_empty_mode = SKIP_EMPTY_OFF;
        else {
          ckout << "--skip-empty must be auto, on or off" << endl;
          CkExit(1);
          return;
        }
      } else if (arg == "--lazy-growth") {
        if (i + 1 >= m->argc || std::stoi(m->argv[i + 1]) < 2) {
          ckout << "--lazy-growth needs an integer of at least 2" << endl;
          CkExit(1);
          return;
        }
        lazy_growth = std::stoi(m->argv[++i]);
      } else if (arg == "--lazy-heap") {
        if (i + 1 >= m->argc) {
          ckout << "--lazy-heap needs a percentile" << endl;
          CkExit(1);
          return;
        }
        lazy_heap_percentile = std::stod(m->argv[++i]);
      } else if (arg == "--send-filter" || arg.rfind("--send-filter=", 0) == 0) {
        std::string value;
        if (arg == "--send-filter") {
          if (i + 1 < m->argc)
            value = m->argv[++i];
        } else
          value = arg.substr(14);
        if (value == "auto")
          send_filter_auto = true;
        else if (value == "off") {
          send_filter_auto = false;
          send_filter_bits = 0;
        } else {
          ckout << "--send-filter must be auto or off "
                << "(--send-filter-bits N keeps it on)" << endl;
          CkExit(1);
          return;
        }
      } else if (arg.rfind("--send-filter-bits=", 0) == 0) {
        send_filter_bits = std::stoi(arg.substr(19));
        send_filter_auto = false;
      } else if (arg == "--send-filter-bits") {
        if (i + 1 >= m->argc) {
          ckout << "--send-filter-bits needs a number of bits" << endl;
          CkExit(1);
          return;
        }
        send_filter_bits = std::stoi(m->argv[++i]);
        send_filter_auto = false;
      } else if (arg.rfind("--batch-fold", 0) == 0) {
        std::string value;
        if (arg.rfind("--batch-fold=", 0) == 0)
          value = arg.substr(13);
        else if (arg == "--batch-fold" && i + 1 < m->argc)
          value = m->argv[++i];
        if (value == "off")
          batch_fold = false;
        else if (value == "on")
          batch_fold = true;
        else {
          ckout << "--batch-fold must be off or on" << endl;
          CkExit(1);
          return;
        }
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
            << "[--verify] [--result-digest] [--timeout <seconds>] [--bufsize <items>]" << endl
            << "       [--bufsize-policy fixed|acceptance] [--bufsize-acc-items <items>] "
            << "[--bufsize-range <min>:<max>]" << endl
            << "       [--bucket-width <distance units>] "
            << "[--round-delay <ms>] [--flush-interval <rounds>] "
            << "[--flush-policy fixed|stale|adaptive] "
            << "[--bucket-policy fixed|adaptive] [--bucket-target <buckets>] "
            << "[--two-tier-per-pe <updates>] [--two-tier-absolute <updates>] "
            << "[--coarsen-clamped block|allow|strict] "
            << "[--bucket-width-rule logv|weight] "
            << "[--clamp-freeze off|on] "
            << "[--range-extend off|on] "
            << "[--skew-defer off|on] [--pq-overflow-last off|on] "
            << "[--window-follow off|on] "
            << "[--idle-flush off|on|starved] "
            << "[--send-filter-bits <n>] [--send-filter auto|off] "
            << "[--combine off|hold] "
            << "[--batch-fold off|on] "
            << "[--partition-jitter <percent>] [--process-share off|on|auto] [--process-queue local|nearest] [--sources v1,v2,...] [--slack-control off|on|auto] [--reader-tile off|auto|T] [--diag <prefix>]" << endl
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
#ifdef VCOUNT
    // vcount is indexed by a vertex's current distance and re-derived at every
    // change, so nothing carries a creation-time bucket for it the way
    // UPDATE_OVERFLOW_BIT does for the histogram. A clamp that moved between
    // two of those lookups would unbalance it silently, and vcount exists to
    // be read. This build measures; it does not extend.
    range_extend = false;
#endif
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
    if (bucket_target < 1) {
      ckout << "--bucket-target must be at least 1" << endl;
      CkExit(1);
      return;
    }
    if (two_tier_per_pe < 0) {
      ckout << "--two-tier-per-pe must not be negative" << endl;
      CkExit(1);
      return;
    }
    if (bucket_policy == BUCKET_ADAPTIVE && combine_mode == COMBINE_HOLD) {
      // CombiningHold keeps per-bucket reference lists that cannot be merged
      // in place. Combining is off by default and measured as a loss, so the
      // pair was never needed; asking for it by name is an error, and getting
      // it by default falls back to the width combining was measured with.
      if (bucket_policy_given) {
        ckout << "--bucket-policy adaptive cannot be combined with --combine "
              << "hold" << endl;
        CkExit(1);
        return;
      }
      bucket_policy = BUCKET_FIXED;
      ckout << "--combine hold: using --bucket-policy fixed" << endl;
    }
    if (buffer_size <= 0 || buffer_size > BUFSIZE) {
      ckout << "--bufsize must be in 1.." << BUFSIZE << endl;
      CkExit(1);
      return;
    }
    if (bufsize_given && !bufsize_policy_given)
      bufsize_policy = BUFSIZE_FIXED;
    if (bufsize_min <= 0 || bufsize_min > bufsize_max || bufsize_max > BUFSIZE ||
        bufsize_acc_items <= 0) {
      ckout << "--bufsize-range must satisfy 1 <= min <= max <= " << BUFSIZE
            << ", and --bufsize-acc-items must be positive" << endl;
      CkExit(1);
      return;
    }
    current_buffer_size = buffer_size;
    V = atol(args[0].c_str());          // number of vertices
    std::string file_name = args[1];    // file name or edge count
    S = atoi(args[2].c_str());          // randomization seed
    start_vertex = atol(args[3].c_str());
    if (source_spec.empty()) sources.push_back(start_vertex);
    else {
      std::istringstream input(source_spec);
      std::string token;
      while (std::getline(input, token, ',')) {
        size_t used = 0;
        long source = std::stol(token, &used);
        if (used != token.size() || source < 0) CkAbort("invalid --sources list");
        sources.push_back(source);
      }
      if (source_spec.back() == ',') CkAbort("invalid --sources list");
      start_vertex = sources.front();
    }
    base_diag_prefix = diag_prefix;
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
    main_instance = this;
    controlProxy = CProxy_ControlNode::ckNew();
    arr = CProxy_SsspChares::ckNew(tram_proxy, N);
    mainProxy = thisProxy;
    arr.initiate_pointers();
    partition_index = new long[N + 1]; // last index=maximum index
    lmax = std::numeric_limits<cost>::max();
    start_time = CkWallTimer();
    if (reader_tile_size != 0 && generate_mode != MODE_GAPBS)
      CkAbort("--reader-tile requires a GAPBS input file (mode 4)");
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
      reader_tile_owners = process_share_active() ? CkNumNodes() : N;
      if (reader_tile_size == -1)
        reader_tile_size = num_global_edges < 8L * V
            ? std::max(1L, V / (64L * reader_tile_owners)) : 0;
      if (reader_tile_size > 0) {
        TileLayout layout(V, reader_tile_size, reader_tile_owners);
        std::vector<int64_t> tiled((size_t)V + 1, 0);
        for (long v = 0; v < V; ++v) {
          const long original = layout.original(v);
          tiled[(size_t)v + 1] = tiled[(size_t)v] +
              offsets[(size_t)original + 1] - offsets[(size_t)original];
        }
        offsets.swap(tiled);
        ckout << "Reader tiles: vertices=" << reader_tile_size
              << " owners=" << reader_tile_owners << endl;
      }
      // At least one, so an edgeless graph still spreads its vertices instead
      // of handing every one of them to the last PE.
      auto partition_region = [&](int first_pe, int last_pe, long begin, long end) {
        const long pes = last_pe - first_pe;
        const long region_edges = offsets[(size_t)end] - offsets[(size_t)begin];
        const long per_pe = std::max(1L, (region_edges + pes - 1) / pes);
        long vertex = begin;
        for (int i = first_pe; i < last_pe; ++i) {
          partition_index[i] = vertex;
          const long target = (long)offsets[(size_t)vertex] + per_pe;
          while (vertex < end && (long)offsets[(size_t)vertex] < target &&
                 end - vertex > last_pe - i - 1)
            ++vertex;
        }
        partition_index[last_pe] = end;
      };
      if (reader_tile_size > 0) {
        TileLayout layout(V, reader_tile_size, reader_tile_owners);
        for (int owner = 0; owner < reader_tile_owners; ++owner) {
          const int first = process_share_active() ? CkNodeFirst(owner) : owner;
          const int size = process_share_active() ? CkNodeSize(owner) : 1;
          partition_region(first, first + size, layout.owner_begin(owner), layout.owner_end(owner));
        }
      } else {
        partition_region(0, N, 0, V);
      }
      {
        double sum = 0.0, sum_sq = 0.0;
        long with_edges = 0;
        for (long v = 0; v < V; v++) {
          const double d = (double)(offsets[(size_t)v + 1] - offsets[(size_t)v]);
          if (d > 0) {
            sum += d;
            sum_sq += d * d;
            with_edges++;
          }
        }
        if (with_edges > 0) {
          const double mean = sum / with_edges;
          degree_cv = std::sqrt(std::max(0.0, sum_sq / with_edges - mean * mean)) / mean;
        }
        ckout << "Degree CV: " << degree_cv << endl;
      }
      graph_spec.num_vertices = V;
      graph_spec.num_edges = num_global_edges;
      // Everything above is PE 0 reading the header and the offsets array to
      // decide the partition. The edge rows are read by each PE inside
      // load_gapbs_graph, so this split is index vs graph, not read vs build.
      index_time = CkWallTimer() - start_time;
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
   * Every chare holds its partition, and the reduction that says so now
   * carries the heaviest edge in the graph. Seed the bucket width from it.
   */
  void begin(cost max_edge_weight) {
    graph_max_weight = max_edge_weight;
    if (process_share_active() && lazy_active())
      CkAbort("process sharing currently requires --lazy-heavy off (auto sharing selects sparse graphs)");
    ckout << "Live slack: " << (slack_control_active() ? "on" : "off") << endl;
    ckout << "Process sharing: " << (process_share_active() ? "on" : "off") << endl;
    ckout << "Process queue: " << (process_queue_policy == PROCESS_QUEUE_NEAREST ? "nearest" : "local")
          << (process_share_active() ? "" : " (inactive)") << endl;
#ifdef INFO_PRINTS
    ckout << "The heaviest edge in the graph weighs " << max_edge_weight << endl;
#endif
    // The seed has to reach every chare before any update is created, and it
    // cannot ride the SharedInfo broadcast above: that is a group proxy and
    // start_algo goes to an array element, and Charm++ orders neither against
    // the other. So it is a real barrier -- one broadcast and one empty
    // reduction, against a setup phase measured in seconds.
    if (bucket_width_rule == WIDTH_RULE_WEIGHT && bucket_width_override <= 0.0)
      arr.seed_bucket_width(max_edge_weight);
    else
      start_source();
  }

  /** Every chare is binning at the seeded width. */
  void width_seeded() { start_source(); }

  /** Start algorithm from source vertex. */
  bool links_warmed = false;
  void start_source() {
    if (warm_links_on && !links_warmed) {
      links_warmed = true;
      arr.warm_links();
      CkStartQD(CkCallback(CkIndex_Main::start_source(), mainProxy));
      return;
    }
    start_vertex = sources[source_epoch];
    for (long source : sources)
      if (source < 0 || source >= V) CkAbort("source %ld outside graph", source);
    if (sources.size() > 1 && !base_diag_prefix.empty())
      diag_prefix = base_diag_prefix + ".s" + std::to_string(source_epoch) +
                    "-v" + std::to_string(start_vertex);
    ckout << "SOURCE_RUN index=" << source_epoch << " source=" << start_vertex << endl;
    // Reached once every chare holds its partition and the width it will
    // bucket with, in every mode, so this is the one boundary that means the
    // same thing everywhere: the solver could start now. MODE_UNIFORM and
    // MODE_MESH generate rather than read, so for them input and build are the
    // same phase and read_time is that phase; the guard that used to sit here
    // left modes 3 and 4 unassigned.
    setup_time = CkWallTimer() - start_time;
    if (generate_mode == MODE_UNIFORM || generate_mode == MODE_MESH ||
        generate_mode == MODE_RMAT)
      read_time = setup_time; // generated, so there is no separable input
    Update new_edge;
    const TileLayout layout(V, reader_tile_size, reader_tile_owners);
    new_edge.dest_vertex = layout.internal(start_vertex);
    new_edge.distance = 0;
    int dest_proc = 0;
    for (int i = 0; i < N; i++) {
      if (new_edge.dest_vertex >= partition_index[i] &&
          new_edge.dest_vertex < partition_index[i + 1]) {
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
      CcdCallFnAfter(fast_exit, new std::pair<Main *, size_t>(this, source_epoch),
                     timeout_seconds * 1000.0);
    // The chares set the same size when they built their partitions.
    current_buffer_size = initial_buffer_size();
    source_running = true;
    compute_begin = CkWallTimer();
#ifdef INFO_PRINTS
    ckout << "Beginning at time: " << compute_begin << endl;
#endif
    arr.start_papi();
    arr[dest_proc].start_algo(new_edge);
  }

  /**
   * --bucket-policy adaptive. Returns how many adjacent buckets to merge into
   * one this round, or 1 for none. The band is the reduced window's middle 90%
   * of mass, so a straggler far above the frontier cannot widen it; rounds
   * with too little in flight to describe a distribution (the two-tier branch)
   * are skipped. Never coarsens while anything sits in the clamp bucket: an
   * update clamped there has lost its real index, so it is the one bucket a
   * merge cannot place exactly.
   */
  int choose_coarsening(const long *histo_values, long histogram_sum,
                        long clamped, int two_tier, int window_last,
                        int *reason) {
    if (bucket_policy != BUCKET_ADAPTIVE)
      return *reason = COARSEN_POLICY_OFF, 1;
    if (two_tier)
      return *reason = COARSEN_TWO_TIER, 1;
    if (histogram_sum <= 0)
      return *reason = COARSEN_EMPTY, 1;
    // With the clamp threshold frozen the merge is exact for every bucket,
    // including 2047, which coarsen_buckets leaves alone. There is then
    // nothing for these guards to protect and they are what stops a run that
    // has touched the clamp bucket from ever coarsening again, so a frozen
    // threshold retires them rather than leaving them to cost rounds.
    if (!clamp_freeze) {
      if (coarsen_clamp_policy != CLAMP_ALLOW && clamped > 0)
        return *reason = COARSEN_CLAMP_LIVE, 1;
      if (coarsen_clamp_policy == CLAMP_STRICT && clamp_ever_live)
        return *reason = COARSEN_CLAMP_SEEN, 1;
    }
    const double lo_mass = 0.05 * histogram_sum, hi_mass = 0.95 * histogram_sum;
    long mass = 0;
    int lo = -1, hi = -1;
    for (int i = 0; i < histo_reduction_width; i++) {
      mass += histo_values[i];
      if (lo < 0 && mass > lo_mass)
        lo = i;
      if (mass >= hi_mass) {
        hi = i;
        break;
      }
    }
    if (lo < 0 || hi < 0)
      return *reason = COARSEN_NO_BAND, 1;
    const int k = (hi - lo) / bucket_target;
    if (k < 2)
      return *reason = COARSEN_BAND_NARROW, 1;
    // The merged window must still reach everything the window reaches now.
    const int top = last_first_nonzero + (window_last < 0 ? 0 : window_last);
    if (top / k >= HISTO_BUCKET_COUNT - 1)
      return *reason = COARSEN_TOP_UNREACHABLE, 1;
    *reason = COARSEN_MERGED;
    return k;
  }

  /**
   * Bounded diagnostics for a run that has stopped making progress. Three
   * things are wanted and none of them is visible from one place: what the
   * controller can see (the reduced window, and the thresholds it computed
   * from it); what it cannot (work above the window's right edge, which is the
   * difference between the conserved live count and the window's own sum); and
   * where that work is actually sitting, which only the PEs know. Hence one
   * line from here and one per PE.
   *
   * Nothing here ends the run. A watchdog that forces an exit destroys the
   * state that says why the run stopped, which is the only thing a stall is
   * good for.
   */
  void report_stall(long histogram_sum, int occupied, int span, long clamped,
                    long live_updates, long above_window, long updates_created,
                    long updates_processed, long updates_noted,
                    int first_nonzero, int heap_threshold,
                    int tram_threshold) {
    stall_reports++;
    ckout << endl
          << "PROGRESS_STALL " << stall_reports << " main"
          << " rounds_without_progress=" << stall_rounds
          << " t=" << CkWallTimer() - compute_begin
          << " created=" << updates_created
          << " processed=" << updates_processed
          << " noted=" << updates_noted << " live=" << live_updates
          << " window_first=" << last_first_nonzero
          << " window_width=" << histo_reduction_width
          << " first_nonzero=" << first_nonzero << " occupied=" << occupied
          << " span=" << span << " window_sum=" << histogram_sum
          << " above_window=" << above_window << " clamped=" << clamped
          << " heap_threshold=" << heap_threshold
          << " tram_threshold=" << tram_threshold
          << " bucket_scale=" << bucket_scale << " phase=" << current_phase
          << endl;
    // The conservation invariant. Every live update is charged to exactly one
    // bucket, so the window can never hold more of them than exist. If it
    // does, the histogram is not a population count any more and no threshold
    // derived from it means anything -- including the one that is keeping this
    // run from finishing.
    if (above_window < 0)
      ckout << "PROGRESS_STALL " << stall_reports
            << " main CONSERVATION VIOLATED: the reduced window sums to "
            << histogram_sum << " with only " << live_updates
            << " updates live. Every threshold computed from it is "
               "meaningless."
            << endl;
    arr.report_progress_state(stall_reports);
  }

  /**
   * --bufsize-policy acceptance. Returns the new buffer size to broadcast, or
   * 0 for no change. A sample closes once enough updates have been noted to
   * give a share worth acting on; the shares are smoothed across samples, and
   * the size moves only when the target differs by 2x or more. The start
   * already comes from the degree, so the share is there to correct a wrong
   * start, not to tune a right one: the first samples of a scale-free ramp
   * read 0.08 and would otherwise halve a size that is correct.
   */
  // `updates_seen` counts what the send filter dropped as well as what
  // arrived: a filtered update is a rejection made early, and leaving it out
  // reads a filtered mesh as accepting nearly everything (mesh20: 0.99).
  int choose_buffer_size(long updates_seen, long distance_changes) {
    if (bufsize_policy != BUFSIZE_ACCEPTANCE)
      return 0;
    const long noted = updates_seen - acc_noted_mark;
    const long min_sample = std::max(100000L, 64L * (long)N);
    if (noted < min_sample)
      return 0;
    const double share =
        (double)(distance_changes - acc_changes_mark) / (double)noted;
    acc_noted_mark = updates_seen;
    acc_changes_mark = distance_changes;
    smoothed_acceptance = (smoothed_acceptance < 0)
                              ? share
                              : 0.5 * smoothed_acceptance + 0.5 * share;
    const double want =
        bufsize_acc_items / std::max(smoothed_acceptance, 1e-6);
    const int target = quantize_buffer_size(want);
    if (target >= 2 * current_buffer_size ||
        2 * target <= current_buffer_size) {
      current_buffer_size = target;
      buffer_size_changes++;
      return target;
    }
    return 0;
  }

  void record_round(double now, long histogram_sum, int first_nonzero,
                    int occupied, int span, int heap_threshold,
                    int tram_threshold, int two_tier, int starved,
                    long updates_created, long updates_processed,
                    long updates_noted, long distance_changes,
                    long done_vertices, long clamped, long clamped_arrivals,
                    int coarsen_reason = COARSEN_NOT_ASKED, int coarsen_k = 1) {
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
    r.two_tier = two_tier;
    r.starved = starved;
    r.bucket_scale = bucket_scale;
    r.coarsen_reason = coarsen_reason;
    r.coarsen_k = coarsen_k;
    r.clamped = clamped;
    r.clamped_arrivals = clamped_arrivals;
    r.updates_created = updates_created;
    r.updates_processed = updates_processed;
    r.updates_noted = updates_noted;
    r.distance_changes = distance_changes;
    r.done_vertices = done_vertices;
    r.buffer_size = current_buffer_size;
    r.active_pes = round_active_pes;
    r.round_seconds = r.t - (rounds.empty() ? 0.0 : rounds.back().t);
    r.slack_widths = slack_control_active() ? live_slack.widths : 0.0;
    r.slack_ratio = live_slack.changes_per_retired;
    r.slack_idle = live_slack.idle_fraction;
    r.slack_action = live_slack.action;
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
    round_active_pes = histo_values[histo_reduction_width + 10];
    long histogram_sum = 0;
    int first_nonzero = -1;
    long updates_processed = histo_values[histo_reduction_width + 1];
    long updates_created = histo_values[histo_reduction_width];
    long bfs_processed = histo_values[histo_reduction_width + 2];
    long done_vertex_count = histo_values[histo_reduction_width + 3];
    long updates_noted = histo_values[histo_reduction_width + 4];
    long bfs_noted = histo_values[histo_reduction_width + 5];
    long distance_changes = histo_values[histo_reduction_width + 6];
    if (slack_control_active())
      live_slack.observe(distance_changes - previous_distance_changes,
                         updates_processed - previous_updates_processed,
                         round_active_pes, N);
    long clamped = histo_values[histo_reduction_width + 7];
    const long clamped_created = histo_values[histo_reduction_width + 8];
    const long send_filtered = histo_values[histo_reduction_width + 9];
    const long clamped_arrivals = clamped_created - last_clamped_created;
    last_clamped_created = clamped_created;
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
    // Every live update is charged to exactly one bucket of the global
    // histogram from the moment it is created until it is retired, so
    // created - processed is that histogram's total over all
    // HISTO_BUCKET_COUNT buckets. The reduction carries only
    // histo_reduction_width of them, and the difference is therefore the work
    // sitting above the window's right edge -- the one thing the controller
    // otherwise cannot tell apart from having converged. It cannot be
    // negative, and if it is then no threshold computed from this window means
    // anything; report_stall() says so rather than letting the run limp on.
    const long live_updates = updates_created - updates_processed;
    const long above_window = live_updates - histogram_sum;
    if (histogram_sum <= 0 && live_updates > 0)
      rounds_window_empty++;
    if (above_window > max_above_window)
      max_above_window = above_window;
    // The window cannot hold more live updates than exist. One comparison per
    // round, said once per run, because every threshold below is computed from
    // a histogram that this would prove is not a population count -- and the
    // way that shows up is a run that does not finish, which is a much harder
    // thing to read after the fact than a line saying so while it happens.
    above_negative_rounds = (above_window < 0) ? above_negative_rounds + 1 : 0;
    if (above_negative_rounds >= 8 && !conservation_warned) {
      conservation_warned = true;
      ckout << endl
            << "CONSERVATION VIOLATED: the reduced window sums to "
            << histogram_sum << " with only " << live_updates
            << " updates live, at round " << reduction_counts
            << ", window_first=" << last_first_nonzero
            << ", bucket_scale=" << bucket_scale << ", for "
            << above_negative_rounds
            << " rounds. Thresholds computed from this window are meaningless."
            << endl;
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
    // Each round's broadcast is sent only after every contribution to the
    // previous round has arrived. Unchanged monotone created/processed sums
    // therefore imply a common interval with no update creation/retirement.
    // updates_created > 0 is what proves the source started: start_algo()
    // charges the injected source update to its PE, so the count is zero only
    // before that has happened. It used to be proved by processed == created
    // + 1, the source update being retired without ever having been created --
    // which also left the global histogram permanently negative in bucket 0.
    // A minimum traffic volume adds no safety and prevents small components
    // (including an isolated source) from ever terminating.
    if ((updates_created > 0) && (updates_processed == updates_created) &&
        (updates_created == previous_updates_created) &&
        (updates_processed == previous_updates_processed)) {
      record_round(CkWallTimer(), histogram_sum, first_nonzero, occupied, span,
                   -1, -1, 0, 0, updates_created, updates_processed, updates_noted,
                   distance_changes, done_vertex_count, clamped,
                   clamped_arrivals);
      ckout << endl << "updates_processed and updates_created match" << endl;
#ifdef INFO_PRINTS
      ckout << "Threshold: " << previous_threshold << endl;
#endif
      compute_time = CkWallTimer() - compute_begin;
      source_running = false;
      arr.print_distances();
      return;
    }
    // Both sums standing still means no update was created or retired anywhere
    // in the interval between two rounds. With live work outstanding that is a
    // stall, not convergence, and nothing will restart it. Counted here, while
    // previous_* still holds the previous round; reported further down, once
    // the thresholds this round computed are known.
    if (updates_created == previous_updates_created &&
        updates_processed == previous_updates_processed)
      stall_rounds++;
    else
      stall_rounds = 0;
    previous_updates_created = updates_created;
    previous_updates_processed = updates_processed;
    // calculate target percentile
    double heap_percent; // heap percentage
    double tram_percent; // tram percentage
    // Fewer updates in the window than this is treated as too few to describe
    // a distribution. The default is the historical N * 100, which ties the
    // rule to the PE count; --two-tier-absolute pins it instead so the two can
    // be told apart on the same graph at different scales.
    const long two_tier_limit = (two_tier_absolute >= 0)
                                    ? two_tier_absolute
                                    : (long)N * (long)two_tier_per_pe;
    const int two_tier = (histogram_sum <= two_tier_limit) ? 1 : 0;
    if (two_tier) {
      heap_percent = 0.9999;
      tram_percent = 0.9999;
    } else {
      heap_percent = lazy_active() ? lazy_heap_percentile : heap_percentile;
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
    if (slack_control_active() && histogram_sum > 0 && first_nonzero >= 0) {
      heap_threshold = live_slack.threshold(first_nonzero, bucket_scale, HISTO_BUCKET_COUNT - 1);
      tram_threshold = heap_threshold;
      bfs_threshold = heap_threshold;
    }
    // in case of floating point weirdness
    if (heap_threshold >= HISTO_BUCKET_COUNT)
      heap_threshold = HISTO_BUCKET_COUNT - 1;
    if (tram_threshold >= HISTO_BUCKET_COUNT)
      tram_threshold = HISTO_BUCKET_COUNT - 1;
    // Nothing live inside the window. Whatever is left is above the window's
    // right edge, where the controller can neither see it nor aim a threshold
    // at it, so the only move guaranteed to make progress is to admit
    // everything: every live update is then below both thresholds, every PE
    // can retire what it holds, and updates_processed has to rise. Written as
    // <= rather than ==, because a window that sums to a negative number is
    // the same situation seen through an accounting error -- and that is the
    // case that used to deadlock, since the percentile scan cannot reach a
    // negative target and leaves both thresholds at the window's own origin.
    bool rescued = false;
    if (histogram_sum <= 0) {
      heap_threshold = HISTO_BUCKET_COUNT - 1;
      tram_threshold = HISTO_BUCKET_COUNT - 1;
      bfs_threshold = HISTO_BUCKET_COUNT - 1;
    } else if (stall_rescue_rounds > 0 && stall_rounds >= stall_rescue_rounds &&
               above_window > 0 && live_updates > 0) {
      // The rescue above asks whether the window is empty. That is the wrong
      // question, and job 22071815 is what it costs: five baseline runs at one
      // node froze with 5,969 of 5,973 live updates in the overflow slot and
      // the remaining FOUR inside the window. histogram_sum was 4, so the
      // emptiness guard did not fire, and no threshold computed inside a
      // 256-wide window anchored below the overflow slot can reach it. Four
      // updates blocked a rescue that 5,969 updates needed, and the run stood
      // still -- created, processed, live and both thresholds bit-identical --
      // for the rest of its wall clock.
      //
      // So the guard is progress, not emptiness. stall_rounds counts
      // consecutive reductions in which no update was created or retired
      // anywhere; with work above the window that the controller cannot aim a
      // threshold at, admitting everything is the one move that must make
      // progress, exactly as in the empty-window case: every live update is
      // then below both thresholds and every PE can retire what it holds.
      //
      // This is not a watchdog and it does not force an exit -- it changes
      // admission so that stranded work becomes reachable and the run
      // continues to a correct answer. But it is still a repair of the
      // symptom: whatever left four admissible updates unprocessed at their
      // own threshold is not fixed by this, so every firing is counted,
      // printed, and failed by the gate rather than quietly saving a run.
      heap_threshold = HISTO_BUCKET_COUNT - 1;
      tram_threshold = HISTO_BUCKET_COUNT - 1;
      bfs_threshold = HISTO_BUCKET_COUNT - 1;
      rescued = true;
      stall_rescues++;
      stall_rounds = 0;
      // Leading endl for the same reason COARSEN_CLAMPED has one: without it
      // the line is appended to whatever was printed last and the gate's
      // ^STALL_RESCUE match never fires, which is exactly what happened the
      // first time this was tested.
      ckout << endl
            << "STALL_RESCUE rounds=" << stall_rescue_rounds
            << " live=" << live_updates << " window_sum=" << histogram_sum
            << " above_window=" << above_window << " clamped=" << clamped
            << " first_nonzero=" << first_nonzero
            << " occupied=" << occupied
            << " bucket_scale=" << bucket_scale
            << ": admitted every bucket to break a stall the window could not"
               " describe." << endl;
    }
    (void)rescued;
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
      // remaining work sits past the window's right edge. Letting -1 through
      // here propagates to contribute_histogram(-2), which then reads
      // histogram[-1] on every PE and sums that garbage into the next global
      // histogram.
      //
      // --window-follow on: if work is outstanding it is above the right edge,
      // so slide the window one width up rather than leaving it behind. The
      // buckets it leaves are empty by the same reduction that says the window
      // is, and the origin is bounded by HISTO_BUCKET_COUNT, so at most
      // HISTO_BUCKET_COUNT / histo_reduction_width slides happen before the
      // window contains the lowest live bucket. Off keeps the window where it
      // is, which is what every measurement before step 7.6 was taken with.
      first_nonzero = last_first_nonzero;
      if (window_follow && above_window > 0 &&
          last_first_nonzero + histo_reduction_width < HISTO_BUCKET_COUNT) {
        first_nonzero = last_first_nonzero + histo_reduction_width;
        windows_slid++;
      }
    }
    if (clamped > 0)
      clamp_ever_live = true;
    int coarsen_reason = COARSEN_NOT_ASKED;
    int coarsen = choose_coarsening(histo_values, histogram_sum, clamped,
                                    two_tier, window_last, &coarsen_reason);
    // --range-extend. The band test above asks whether merging would make the
    // frontier easier to describe. It is the wrong question when the overflow
    // slot is filling: what is wrong then is not the resolution but the range,
    // and the two refusals that dominate on a graph out of range -- too little
    // in flight to describe a distribution, and a band already narrower than
    // the target -- are both true and both beside the point. mesh20, mesh22
    // and road-ny spend three quarters of their rounds with first_nonzero
    // pinned at the overflow slot, one occupied bucket, no ordering left to
    // schedule by, and 31x to 45x the updates a run in range creates.
    //
    // So this asks the other question, and it is asked whatever the band says
    // and whatever the two-tier branch says. Raising the clamp costs one
    // merge, which halves resolution -- cheap against having none.
    const bool can_extend = range_extend && clamp_freeze &&
                            bucket_policy == BUCKET_ADAPTIVE &&
                            bucket_scale < (1 << 20);
    // Of the updates created this round, what share was out of range? That is
    // the question, and the denominator is the round's own creations rather
    // than the live population, which was the first denominator and was wrong
    // in both directions. Early it is too large, because most of what is live
    // was created before the round; late it collapses to nothing, so a handful
    // of arrivals against a nearly drained population reads as a range
    // emergency and the rule keeps doubling a scale that is already wide
    // enough. On mesh20 that was the difference between 4 extensions and a run
    // that had not finished in 250x the time.
    //
    // The cooldown is for the pipeline, not for the policy. A chare applies an
    // extension on the broadcast that follows the reduction it was decided
    // from, so the next round's arrivals were partly charged under the old
    // clamp and would buy a doubling that has already been bought.
    const long created_this_round = updates_created - last_updates_created_seen;
    last_updates_created_seen = updates_created;
    const bool extend = can_extend && clamped_arrivals > 0 &&
                        created_this_round > 0 &&
                        reduction_counts >= extend_ready_at &&
                        (double)clamped_arrivals >=
                            RANGE_EXTEND_SHARE * created_this_round;
    if (extend) {
      extend_ready_at = reduction_counts + 2;
      range_extensions++;
      // A round that was already merging merges by its own k and spends that
      // on distance; a round that was not merges by two, which is the smallest
      // step that frees any index space at all.
      if (coarsen < 2) {
        coarsen = 2;
        coarsen_reason = COARSEN_EXTEND;
      }
    }
    if (coarsen > 1) {
      // The overflow slot does not move when buckets merge -- that is what the
      // freeze means -- so an index sitting on it must not be divided with the
      // rest. Dividing the window origin points the controller at 2047 / k,
      // where by construction nothing is; dividing a threshold that was open
      // to the overflow closes it. Either one stops the overflow draining, and
      // a run whose overflow cannot drain does not finish. This is the same
      // stranding the clamp freeze was written to prevent, one level up: 7.6d
      // fixed it in the histogram and left it here, where nothing reached it
      // because the graphs that coarsen never have anything in the overflow
      // and the graphs with something in it never coarsened.
      const int top = HISTO_BUCKET_COUNT - 1;
      const bool frozen_top = clamp_freeze;
      if (!(frozen_top && heap_threshold == top))
        heap_threshold /= coarsen;
      if (!(frozen_top && tram_threshold == top))
        tram_threshold /= coarsen;
      if (!(frozen_top && bfs_threshold == top))
        bfs_threshold /= coarsen;
      if (!(frozen_top && first_nonzero == top))
        first_nonzero /= coarsen;
      previous_threshold = heap_threshold;
      bucket_scale *= coarsen;
      coarsenings++;
    }
    // Recorded before the -1 is folded away below, so a round whose window
    // held nothing reads as exactly that rather than as a window that happened
    // not to move. heap_threshold - first_nonzero is the controller's actual
    // dynamic range for the round, which is what H1 is really asking about.
    // Starved: less work in flight than it takes to fill one buffer on every
    // (sender PE, destination) stream. A buffer on such a stream cannot be
    // expected to fill, so waiting for it to is pure latency. The count is the
    // reduced window rather than every bucket, which is the population the
    // controller is acting on anyway.
    const int new_buffer_size =
        choose_buffer_size(updates_noted + send_filtered, distance_changes);
    const long streams = (long)N * (long)CkNumNodes();
    const int starved =
        (histogram_sum < streams * (long)current_buffer_size) ? 1 : 0;
    if (stall_rounds >= stall_report_at) {
      report_stall(histogram_sum, occupied, span, clamped, live_updates,
                   above_window, updates_created, updates_processed,
                   updates_noted, first_nonzero, heap_threshold,
                   tram_threshold);
      stall_report_at *= 2;
    }
    record_round(CkWallTimer(), histogram_sum, first_nonzero, occupied, span,
                 heap_threshold, tram_threshold, two_tier, starved,
                 updates_created,
                 updates_processed, updates_noted, distance_changes,
                 done_vertex_count, clamped, clamped_arrivals, coarsen_reason,
                 coarsen);
    // arr.contribute_histogram(first_nonzero-1);
    last_first_nonzero = first_nonzero;
    if (control_mode == CONTROL_NODE)
      controlProxy.thresholds(++control_generation, heap_threshold,
                              tram_threshold, bfs_threshold, first_nonzero - 1,
                              current_phase, starved, coarsen, extend ? 1 : 0,
                              new_buffer_size);
    else
      arr.current_thresholds(heap_threshold, tram_threshold, bfs_threshold,
                             first_nonzero - 1, current_phase, starved, coarsen,
                             extend ? 1 : 0, new_buffer_size);

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
           "heap_threshold,tram_threshold,two_tier,starved,bucket_scale,"
           "coarsen_reason,coarsen_k,"
           "updates_created,updates_processed,updates_noted,distance_changes,"
           "done_vertices,clamped,clamped_arrivals,buffer_size,active_pes,round_seconds,slack_widths,slack_ratio,slack_idle,slack_action\n";
    for (size_t i = 0; i < rounds.size(); i++) {
      const RoundRecord &r = rounds[i];
      out << i << ',' << r.t << ',' << r.histogram_sum << ','
          << r.window_first << ',' << r.first_nonzero << ',' << r.occupied
          << ',' << r.span << ',' << r.heap_threshold << ','
          << r.tram_threshold << ',' << r.two_tier << ',' << r.starved << ','
          << r.bucket_scale << ',' << r.coarsen_reason << ',' << r.coarsen_k
          << ',' << r.updates_created << ',' << r.updates_processed << ','
          << r.updates_noted << ',' << r.distance_changes << ','
          << r.done_vertices << ',' << r.clamped << ','
          << r.clamped_arrivals << ',' << r.buffer_size << ',' << r.active_pes
          << ',' << r.round_seconds << ',' << r.slack_widths << ',' << r.slack_ratio
          << ',' << r.slack_idle << ',' << r.slack_action << '\n';
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
    // start_algo charges the source, so every bucket must return to zero.
    int live_nonzero = 0;
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++)
      if (msg_stats[STAT_HISTO_LIVE + i] != 0) {
        if (live_nonzero++ < 4)
          ckout << "DIAG histogram bucket " << i << " ends at "
                << msg_stats[STAT_HISTO_LIVE + i] << endl;
      }
    ckout << "DIAG histogram buckets not back to zero: " << live_nonzero
          << endl;
    ckout << "DIAG admitted-count drift: " << msg_stats[STAT_ADMITTED_DRIFT]
          << endl;

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
    if (setup_time >= 0.0 && compute_time >= 0.0)
      stats_time = total_time - setup_time - compute_time;
    ckout << "Actual edges: " << msg_stats[STAT_EDGES] << endl;
    // Three non-overlapping phases that add up to Total time. Anything that
    // compares cold starts has to say which of these it is comparing; a
    // number that silently folds setup into solve is not a measurement.
    ckout << "Setup time: " << setup_time << endl;
    ckout << "Index time: " << index_time << endl;
    ckout << "Read time: " << read_time << endl;
    ckout << "Compute time: " << compute_time << endl;
    ckout << "Stats time: " << stats_time << endl;
    ckout << "Total time: " << total_time << endl;
    ckout << "Wasted updates: " << msg_stats[STAT_WASTED] - V << endl;
    ckout << "Wasted updates normalized to |E|: "
          << (double)(msg_stats[STAT_WASTED] - V) / msg_stats[STAT_EDGES]
          << endl;
    ckout << "Rejected updates: " << msg_stats[STAT_REJECTED] << endl;
    ckout << "Rejected updates normalized to |E|: "
          << (double)msg_stats[STAT_REJECTED] / msg_stats[STAT_EDGES] << endl;
    // Absorbed updates were created and then folded into a better one before
    // they left their source, so they are in neither count above. Rejected
    // plus absorbed is the redundant work a run did, wherever it was caught.
    ckout << "Send-filtered updates: " << msg_stats[STAT_SEND_FILTERED]
          << ", normalized to |E|: "
          << msg_stats[STAT_SEND_FILTERED] * 1.0 / msg_stats[STAT_EDGES] << endl;
#ifdef ACIC_COMM_SHARE
    {
      const double s_per_tick =
          1e-6 * msg_stats[STAT_WINDOW_US] / std::max(1L, msg_stats[STAT_WINDOW_TSC]);
      const double pe_seconds = compute_time * CkNumPes();
      const double work = msg_stats[STAT_WORK_TSC] * s_per_tick;
      const double work_send = msg_stats[STAT_WORK_SEND_TSC] * s_per_tick;
      const double send = msg_stats[STAT_SEND_TSC] * s_per_tick;
      ckout << "COMM_SHARE pes=" << CkNumPes() << " solve_seconds=" << compute_time
            << " pe_seconds=" << pe_seconds << " work_seconds=" << work
            << " work_send_seconds=" << work_send << " send_seconds=" << send
            << " window_pe_seconds=" << 1e-6 * msg_stats[STAT_WINDOW_US]
            << " compute_share=" << (work - work_send) / pe_seconds
            << " send_share=" << send / pe_seconds
            << " other_share=" << 1.0 - (work - work_send + send) / pe_seconds
            << " idle_share=" << msg_stats[STAT_IDLE_TSC] * s_per_tick / pe_seconds
            << endl;
    }
#endif
#ifdef ACIC_WORK_COST
    CkPrintf("%s\n", work_cost::record(msg_stats + STAT_COST_METRICS).c_str());
    CkPrintf("WORK_CLOCK window_ticks=%ld window_us=%ld\n",
             msg_stats[STAT_WINDOW_TSC], msg_stats[STAT_WINDOW_US]);
#endif
    if (lazy_active())
      ckout << "Lazy heavy: " << msg_stats[STAT_TOKENS] << " tokens, "
            << msg_stats[STAT_TOKENS_STALE] << " stale, per vertex: "
            << msg_stats[STAT_TOKENS] * 1.0 / V << endl;
    ckout << "Absorbed updates: " << msg_stats[STAT_ABSORBED]
          << ", normalized to |E|: "
          << (double)msg_stats[STAT_ABSORBED] / msg_stats[STAT_EDGES] << endl;
    ckout << "Batch-folded updates: " << msg_stats[STAT_FOLDED]
          << ", normalized to |E|: "
          << (double)msg_stats[STAT_FOLDED] / msg_stats[STAT_EDGES] << endl;
    ckout << "Number of threshold changes: " << threshold_change_counter
          << endl;
    ckout << "Number of reductions: " << reduction_counts << endl;
    // Every firing is a run that would otherwise have hung. Printed
    // unconditionally, including the zero, so that "did this run need
    // rescuing?" is answerable from the summary of any run rather than only
    // from a log that happens to contain the STALL_RESCUE line.
    ckout << "Stall rescues: " << stall_rescues << endl;
    // 7.6g. Nonzero means some PE was handed an update by a creator a
    // coarsening ahead of it, at the one index the merge skips. With
    // --skew-defer off each one strands a count at floor(2047 / scale).
    ckout << "Skewed top-bucket arrivals: " << msg_stats[STAT_SKEW_TOP]
          << " (skew-defer " << (skew_defer ? "on" : "off") << ")" << endl;
    // A round in this count admitted everything, because the work had moved
    // past the right edge of the reduced window and nothing the controller
    // could see said where it went. The maximum is how far behind the window
    // ever fell, in updates.
    ckout << "Windows slid past an empty reduced window: " << windows_slid
          << endl;
    ckout << "Rounds with the frontier outside the window: "
          << rounds_window_empty << ", most updates outside it: "
          << max_above_window << endl;
    if (bufsize_policy == BUFSIZE_ACCEPTANCE)
      ckout << "Buffer size: acceptance policy, started at "
            << initial_buffer_size() << ", final " << current_buffer_size
            << " items after " << buffer_size_changes
            << " changes, last smoothed acceptance " << smoothed_acceptance
            << endl;
    else
      ckout << "Buffer size: fixed, " << buffer_size << " items" << endl;
    ckout << "Bucket scale: " << bucket_scale << " (" << coarsenings
          << " coarsenings, " << range_extensions << " range extensions)"
          << endl;
    // The width is what the histogram's 2048 buckets span, and until 7.6d no
    // run said what it was -- which is why a rule derived from |V| could bucket
    // distances for a whole campaign without anyone reading it.
    ckout << "Bucket width: "
          << (bucket_width_override > 0.0
                  ? bucket_width_override
                  : (bucket_width_rule == WIDTH_RULE_WEIGHT
                         ? std::max(log((double)V), (double)graph_max_weight)
                         : (generate_mode == MODE_MESH ? sqrt((double)V)
                                                       : log((double)V))))
          << ", heaviest edge: " << graph_max_weight << endl;
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
    {
      const double reached = std::max(1L, msg_stats[STAT_REACHED_EDGES]);
      ckout << "STEP8A expansions=" << msg_stats[STAT_EXPANSIONS]
            << " least_expansions=" << msg_stats[STAT_REACHED_EXPANDABLE]
            << " reached_edges=" << msg_stats[STAT_REACHED_EDGES]
            << " reached_heavy=" << msg_stats[STAT_REACHED_HEAVY]
            << " heavy_created=" << msg_stats[STAT_HEAVY_CREATED]
            << " heavy_per_reached_heavy="
            << msg_stats[STAT_HEAVY_CREATED] * 1.0 /
                   std::max(1L, msg_stats[STAT_REACHED_HEAVY])
            << " light_per_reached_light="
            << (msg_stats[STAT_NOTED] + msg_stats[STAT_SEND_FILTERED] -
                msg_stats[STAT_HEAVY_CREATED]) * 1.0 /
                   std::max(1.0, reached - msg_stats[STAT_REACHED_HEAVY])
            << " arrivals_at_settled=" << msg_stats[STAT_ARRIVAL_SETTLED]
            << endl;
      ckout << "STEP8A_SETTLED_BY_DEGREE class,min_degree,settled_arrivals,arrivals";
      for (int i = 0; i < DEGREE_CLASSES; i++)
        if (msg_stats[STAT_DEG_ARRIVALS + i])
          ckout << " " << i << "," << (i == 0 ? 0 : (1L << (i - 1))) << ","
                << msg_stats[STAT_SETTLED_DEG + i] << ","
                << msg_stats[STAT_DEG_ARRIVALS + i];
      ckout << endl;
      ckout << "STEP8A_LEAD class,min_widths,expansions,final";
      for (int i = 0; i < LEAD_CLASSES; i++)
        if (msg_stats[STAT_LEAD_EXPANSIONS + i])
          ckout << " " << i << "," << (i == 0 ? 0 : (1L << (i - 1))) << ","
                << msg_stats[STAT_LEAD_EXPANSIONS + i] << ","
                << msg_stats[STAT_LEAD_FINAL + i];
      ckout << endl;
    }
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
#ifdef ACIC_IPDPS_DIAG
    ckout << "ONENODE_CHANGES same_pe=" << msg_stats[STAT_SAME_PE_CHANGES]
          << " cross_pe=" << msg_stats[STAT_CROSS_PE_CHANGES]
          << " total=" << msg_stats[STAT_DISTANCE_CHANGES] << " vertices=" << V
          << " source_in_same_pe=1" << endl;
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
    if (previous_tram_stats.empty()) previous_tram_stats.assign(n, 0);
    for (int i = 0; i < n; ++i) {
      const auto cumulative = values[i];
      values[i] -= previous_tram_stats[i];
      previous_tram_stats[i] = cumulative;
    }
    ckout << "TRAM messages: " << values[0] << ", bytes sent: " << values[1]
          << ", bytes allocated: " << values[2] << endl;
    ckout << "TRAM node messages: " << values[3]
          << ", bytes allocated: " << values[4] << endl;
    ckout << "TRAM stale-destination flushes: " << values[5] << endl;
    if (n > 8)
      ckout << "TRAM idle flushes: " << values[8] << endl;
    if (n > 7 && values[7])
      ckout << "TRAM hold: absorbed " << values[6] << " of " << values[7]
            << " items (" << 100.0 * values[6] / values[7] << "%)" << endl;
    if (verify_mode || result_digest)
      arr.verify_hash();
    else
      finish_source();
  }

  void finish_source() {
    if (run_truncated) { CkExit(1); return; }
    if (source_epoch + 1 == sources.size()) { CkExit(0); return; }
    // No old data/control message may observe the next source's empty ledger.
    CkStartQD(CkCallback(CkIndex_Main::advance_source(), mainProxy));
  }

  void advance_source() {
    ++source_epoch;
    source_running = false;
    start_time = CkWallTimer();
    read_time = index_time = 0.0; // topology retained; only solver state resets
    setup_time = stats_time = total_time = compute_time = -1.0;
    threshold_change_counter = 0;
    previous_threshold = initial_threshold;
    reduction_counts = no_incoming = current_phase = last_first_nonzero = 0;
    reduction_times.clear(); rounds.clear(); round_active_pes = 0;
    bucket_scale = 1; coarsenings = 0; live_slack = LiveSlack();
    previous_updates_created = previous_updates_processed = previous_distance_changes = 0;
    stall_rounds = stall_rescues = stall_reports = 0; stall_report_at = 256;
    rounds_window_empty = max_above_window = range_extensions = 0;
    last_clamped_created = last_updates_created_seen = extend_ready_at = windows_slid = 0;
    above_negative_rounds = 0; conservation_warned = clamp_ever_live = false;
    acc_noted_mark = acc_changes_mark = buffer_size_changes = 0;
    smoothed_acceptance = -1.0; current_buffer_size = initial_buffer_size();
    // Keep control generations monotone across solves, so stale pickup
    // notifications cannot replay a previous source's final thresholds.
    arr.reset_for_source();
  }

  void source_reset_done() { start_source(); }

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

    // Digest reduction is outside compute_time. External benchmark drivers
    // compare it with an independently produced reference for every run.
    // --verify still performs its in-process Dijkstra check when both flags
    // are present, and a timed-out solve can never become a successful result.
    if (!verify_mode) {
      finish_source();
      return;
    }

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
      finish_source();
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
  std::unique_ptr<std::pair<Main *, size_t>> ticket((std::pair<Main *, size_t> *)obj);
  Main *main_chare = ticket->first;
  if (ticket->second != main_chare->source_epoch || !main_chare->source_running) return;
  main_chare->source_running = false;
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
/** One round's thresholds, as --control node leaves them for the PEs. */
struct ControlRound {
  int gen = 0, heap = 0, tram = 0, bfs = 0, behind = 0, phase = 0,
      starved = 0, coarsen = 1, extend = 0, bufsize = 0;
};

/**
 * --control node. One per process. add() is called by each PE of the process
 * with its round contribution; the last one forwards the sum to process 0,
 * whose collect() hands the global sum to Main once every process has
 * reported. thresholds() stores the round for the PEs to pick up.
 */
struct ProcessPartition {
  long first = 0, size = 0;
  LocalCsr *graph = nullptr;
  cost *distances = nullptr;
#ifdef ACIC_DIAG
  long *arrivals = nullptr;
  unsigned char *last_lead = nullptr;
#endif
};

class ControlNode : public CBase_ControlNode {
  std::mutex acc_lock, root_lock, round_lock;
  std::vector<long> acc, root_acc;
  int acc_count = 0, root_count = 0;
  ControlRound latest;
  // Process 0 only: the round waiting out --control-interval.
  std::vector<long> ready;
  double last_broadcast = 0.0;

  static void release_ready(void *p, double) {
    ControlNode *self = (ControlNode *)p;
    std::vector<long> total;
    total.swap(self->ready);
    self->last_broadcast = CkWallTimer();
    main_instance->reduce_histogram(total.data(), (int)total.size());
  }

public:
  ProcessWork<Update, ComparePairs, ProcessDistanceKey> work{
      CkNodeSize(CkMyNode()), process_queue_policy == PROCESS_QUEUE_NEAREST};
  std::vector<ProcessPartition> partitions{(size_t)CkNodeSize(CkMyNode())};
  std::atomic<int> generation{0};
  std::unique_ptr<std::atomic<char>[]> fallback_queued;
  ControlNode() : fallback_queued(new std::atomic<char>[CkNodeSize(CkMyNode())]) {
    for (int i = 0; i < CkNodeSize(CkMyNode()); i++)
      fallback_queued[i] = 0;
  }

  void add(const long *values, int n) {
    std::vector<long> out;
    {
      std::lock_guard<std::mutex> guard(acc_lock);
      if (acc.empty())
        acc.assign(n, 0);
      for (int i = 0; i < n; i++)
        acc[i] += values[i];
      if (++acc_count == CkNodeSize(CkMyNode())) {
        out.swap(acc);
        acc_count = 0;
      }
    }
    if (!out.empty())
      thisProxy[0].collect(n, out.data());
  }

  void collect(int n, long *values) {
    std::vector<long> total;
    {
      std::lock_guard<std::mutex> guard(root_lock);
      if (root_acc.empty())
        root_acc.assign(n, 0);
      for (int i = 0; i < n; i++)
        root_acc[i] += values[i];
      if (++root_count == CkNumNodes()) {
        total.swap(root_acc);
        root_count = 0;
      }
    }
    // One round at a time: the next cannot complete before this one's
    // thresholds have gone out, which is the last thing reduce_histogram does.
    if (total.empty())
      return;
    ready.swap(total);
    const double wait_ms =
        control_interval_ms - 1e3 * (CkWallTimer() - last_broadcast);
    if (wait_ms > 0.0)
      CcdCallFnAfter(release_ready, this, wait_ms);
    else
      release_ready(this, 0.0);
  }

  void thresholds(int gen, int heap, int tram, int bfs, int behind, int phase,
                  int starved, int coarsen, int extend, int bufsize) {
    {
      std::lock_guard<std::mutex> guard(round_lock);
      latest.gen = gen;
      latest.heap = heap;
      latest.tram = tram;
      latest.bfs = bfs;
      latest.behind = behind;
      latest.phase = phase;
      latest.starved = starved;
      latest.coarsen = coarsen;
      latest.extend = extend;
      latest.bufsize = bufsize;
    }
    generation.store(gen, std::memory_order_release);
    // The fallback, for a PE that runs none of the pickup points first; at
    // most one queued per PE.
    const int first = CkNodeFirst(CkMyNode());
    for (int r = 0; r < CkNodeSize(CkMyNode()); r++)
      if (!fallback_queued[r].exchange(1))
        arr[first + r].pickup_fallback();
  }

  ControlRound get() {
    std::lock_guard<std::mutex> guard(round_lock);
    return latest;
  }
};

class SharedInfo : public CBase_SharedInfo {
public:
  int event_id;

  SharedInfo() {
    event_id = traceRegisterUserEvent("Contrib reduction");
  }
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
  // 8 bytes, so twice the entries fit the same cache footprint: 20 bits of
  // 16-byte entries filtered half of rmat25's edges and still lost to 16 bits
  // on cache misses. A vertex or distance too wide for 32 bits is not cached.
  struct SentEntry {
    uint32_t vertex = 0xffffffffu;
    uint32_t distance = 0;
  };
  std::vector<SentEntry> sent_cache; // --send-filter-bits
  int sent_cache_shift = 64;
  // Whether generate_updates() consults the table now: always under
  // --send-filter-bits, per regime under --send-filter auto.
  bool send_filter_on = false;
  long send_filtered = 0;
  long updates_processed_locally = 0; // number of update messages received
  long *partition_index;   // defines boundaries of indices for each pe
  long wasted_updates = 0; // number of updates that don't have the final answer
  long rejected_updates = 0; // number of updates that don't decrease a distance
                             // value/create more messages
  long absorbed_updates = 0; // folded into a better update by the source hold
  long folded_updates = 0;   // folded into a better update in a delivered batch
  std::vector<int> fold_table; // --batch-fold, reused between batches
  tram_proxy_t tram_proxy;
  tram_t *tram; // tram library
  SharedInfo *shared_local;
  std::priority_queue<Update, std::vector<Update>, ComparePairs>
      pq;          // heap of messages
  long *histogram; // local histogram of data, from 0 to max_size, divided into
                   // HISTO_BUCKET_COUNT buckets
  cost light_cut = 0;       // --lazy-heavy's L on this PE; 0 is off

  long tokens_created = 0;  // --lazy-heavy tokens queued
  long tokens_stale = 0;    // ... and found stale when admitted
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
  // Step 8a counters; see STAT_EXPANSIONS.
  long expansions = 0, heavy_created = 0, arrival_settled = 0;
  long lead_expansions[LEAD_CLASSES] = {0};
  long settled_deg[DEGREE_CLASSES] = {0};
  unsigned char *last_lead = nullptr; // lead class of each vertex's last expansion
  int frontier_bucket = 0;            // lowest live bucket, as last broadcast
  // A per-PE total says how much work a PE did over the whole run, which is
  // the wrong question for a frontier algorithm: the load moves, so a PE can
  // hold a fair share of the graph and still be idle for most of it. Counting
  // the rounds in which this PE processed nothing at all costs one comparison
  // per round and answers the temporal half directly.
  long controller_rounds = 0;
  long idle_rounds = 0;
  long max_admitted_drift = 0;
  long processed_at_last_round = 0;
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
  double initial_width = 0.0;
  bool idle_timers_registered = false;
  double bucket_multiplier;       // constant to calculate bucket
  // Original buckets merged into each bucket by coarsen_buckets(). The bucket
  // is the integer quotient of the original index, which is what keeps a merge
  // exact; a multiplier divided by the scale would round differently.
  int bucket_scale = 1;
  // ceil(2^64 / bucket_scale), so that the quotient of any 32-bit index by the
  // scale is one multiply (Lemire, Kaser and Kurz 2019) instead of an integer
  // division, which the counters put at a quarter of all cycles. Exact for
  // every index below 2^32; set with bucket_scale, and unused while it is 1.
  unsigned long bucket_scale_inverse = 0;
  double bucket_limit = HISTO_BUCKET_COUNT; // original index that clamps
  std::vector<Update> *pq_hold; // hold for heap messages
  long clamped_created_locally = 0; // updates ever charged to the overflow slot
  // Updates created under a higher clamp than this PE has yet been told about.
  // Neither counted nor processed while they wait; they are live either way,
  // charged to their creator's bucket, so the termination test still sees them.
  std::vector<Update> deferred_updates;
  long deferred_peak = 0;
  long deferred_total = 0;
  // Unflagged arrivals this PE would charge to the overflow index: see
  // --skew-defer. Counted on every pass through process_update(), so an update
  // still skewed when a drain re-offers it is counted again -- which cannot
  // happen at a skew of one broadcast, and would say so if it did.
  long skew_top_arrivals = 0;
  long bfs_created = 0;          // bfs created messages
  long bfs_processed = 0;         // bfs processed messages
  int updates_noted = 0; // updates that have either updated a vertex value, or
                         // are confirmed to not be an improvement
  int *dest_table; // destination table for faster pe calculation
  // get_dest_proc_fast() runs for every update created. M is a readonly, so
  // `vertex / M` and `V / M` were two 64-bit integer divisions per call; M is
  // a power of two, so the first is a shift and the second a constant.
  int dest_table_shift = 0;
  long dest_table_last = 0; // V / M - 1
  // dest_uniform[j] is the PE owning every vertex in [j * M, (j + 1) * M], or
  // -1 if that range crosses a partition boundary (or is the table's tail).
  // Partitions are contiguous and in PE order, so dest_table[j] ==
  // dest_table[j + 1] proves the whole range is one PE's. At most N of the
  // V / M ranges fail that, so nearly every lookup is a single load: no
  // partition_index scan and no data-dependent branch (step 7.6m).
  int *dest_uniform = nullptr;
  int my_pe = -1;           // CkMyPe(), which is a call into the runtime
  int current_phase = 0;
  long actual_edges = 0; // when graph is generated, here's how many edges
                         // actually got generated
  long bfs_noted = 0;
  long *info_array;
  long distance_changes = 0;
  long processed_at_contribution = 0;
#ifdef ACIC_IPDPS_DIAG
  long same_pe_changes = 0, cross_pe_changes = 0;
#endif
  long updates_in_tram = 0;

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
    const long dest_table_index = vertex >> dest_table_shift;
    const int uniform = dest_uniform[dest_table_index];
    if (__builtin_expect(uniform >= 0, 1))
      return uniform;
    // look up x/M and 1+x/M
    int xm_pe, xm_plus_one_pe;
    // if this points to the end of dest_table
    if (dest_table_index >= dest_table_last) {
      xm_pe = dest_table[dest_table_last];
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
    return get_dest_proc_fast(update_vertex(upd));
  }

  SsspChares(CProxy_HTram htram) { tram_proxy = htram; }

  void initiate_pointers() {
    tram = tram_proxy.ckLocalBranch();
    // No batch-done callback: the local-delivery shortcut it used to drive was
    // abandoned, and htram now tolerates a null one.
    tram->set_func_ptr_retarr(SsspChares::process_update_caller,
                              get_dest_proc_local_caller, nullptr, this);
    if (combine_mode == COMBINE_HOLD)
      tram->enableCombining(&hold_ops, this);
    // --bufsize-policy acceptance starts from the graph's degree (Main's
    // start_source() records the same value). Nothing is buffered yet, and
    // the readonlies it reads were fixed before any chare ran. Not in
    // initialize_data(): under mode 1 that can run before this does.
    tram->setBufferSize(initial_buffer_size());
    tram->setIdleFlushInterval(idle_flush_interval_seconds());
    // Step 8b: in the scale-free regime a delivery goes only to the PEs it
    // has items for (orkut at 8 nodes 1.1x). Below it the empty deliveries
    // stay, because they pace the heap passes that a high-diameter solve
    // relies on (mesh24 at 2 nodes 2.1x slower without them, job 20821390).
    tram->setSkipEmptyDeliveries(skip_empty_mode == SKIP_EMPTY_AUTO
                                     ? lazy_active()
                                     : skip_empty_mode == SKIP_EMPTY_ON);
    shared_local = shared.ckLocalBranch();
    control_local = controlProxy.ckLocalBranch();
  }

  ControlNode *control_local = nullptr;
  int applied_generation = 0; // --control node: last round this PE applied

  // --control node: apply a round the process has received and this PE has
  // not. Called where the PE runs anyway -- a delivery, a heap pass, an idle
  // pass -- and from the per-PE fallback message.
  inline void maybe_pickup() {
    if (control_mode == CONTROL_NODE &&
        control_local->generation.load(std::memory_order_acquire) !=
            applied_generation)
      pickup_round();
  }

  void pickup_fallback() {
    control_local->fallback_queued[CkMyRank()] = 0;
    pickup_round();
  }

  void pickup_round() {
    if (control_mode != CONTROL_NODE ||
        control_local->generation.load(std::memory_order_acquire) ==
            applied_generation)
      return;
    const ControlRound r = control_local->get();
    applied_generation = r.gen;
    current_thresholds(r.heap, r.tram, r.bfs, r.behind, r.phase, r.starved,
                       r.coarsen, r.extend, r.bufsize);
  }

  bool idle_triggered() {
    maybe_pickup();
    process_heap();
    // A heap that yielded has re-queued itself, so the PE is not idle yet.
    if (!heap_yielded &&
        (idle_flush_policy == IDLE_FLUSH_ON ||
         (idle_flush_policy == IDLE_FLUSH_STARVED && last_round_starved)))
      tram->flushIdle();
    return true;
  }
  bool heap_yielded = false;     // process_heap stopped with work left
  int last_round_starved = 1;    // the first round always is

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
    if (send_filter_bits > 0 || send_filter_auto) {
      const int bits =
          send_filter_bits > 0 ? send_filter_bits : SEND_FILTER_AUTO_BITS;
      sent_cache.assign(1UL << bits, SentEntry());
      sent_cache_shift = 64 - bits;
      send_filter_on = send_filter_bits > 0 ||
                       initial_buffer_size() >= SEND_FILTER_MIN_BUFFER;
    }
    CkAssert(M > 0 && (M & (M - 1)) == 0);
    dest_table_shift = __builtin_ctz(M);
    // Small verification graphs still have one table entry. A negative tail
    // index used to read dest_table[-1] for every edge when V < M.
    dest_table_last = std::max(0L, V / M - 1);
    my_pe = CkMyPe();
    dest_table = new int[(V + M - 1) / M];
    // long: j * M passes 2^31 once V does (sc27-plan.md, vertex-count audit).
    for (long i = 0, j = 0; i < V; j++, i = j * (long)M) {
      dest_table[j] = get_dest_proc(i);
    }
    const long dest_table_size = (V + M - 1) / M;
    dest_uniform = new int[dest_table_size];
    for (long j = 0; j < dest_table_size; j++)
      dest_uniform[j] = (j < dest_table_last && dest_table[j] == dest_table[j + 1])
                            ? dest_table[j]
                            : -1;
    distances = new cost[num_vertices];
    for (long i = 0; i < num_vertices; i++)
      distances[i] = lmax;
#ifdef ACIC_DIAG
    arrivals_per_vertex = new long[num_vertices];
    last_lead = new unsigned char[num_vertices];
    for (long i = 0; i < num_vertices; i++) {
      arrivals_per_vertex[i] = 0;
      last_lead[i] = 0;
    }
#endif
    vcount[HISTO_BUCKET_COUNT] += num_vertices;
    flush_rng = VertexRng(thisIndex, S);
    heap_threshold = initial_threshold;
    tram_threshold = initial_threshold + 2;
    bfs_threshold = heap_threshold;
    pq_hold = new std::vector<Update>[HISTO_BUCKET_COUNT];
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++)
      pq_hold[i].reserve(4096);
    info_array = new long[histo_reduction_width + 11];
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
    cost heaviest_edge = 0;
    std::vector<Edge> adjacency; // one scratch buffer, reused for every vertex
    local_graph.begin(num_vertices, 4 * num_vertices);
    for (long i = 0; i < num_vertices; i++) {
      long this_vertex = i + start_vertex;
      // See graphlib/generators.h: identical adjacency for any PE count.
      gen_mesh_vertex(this_vertex, side_length, S, adjacency);
      actual_edges += adjacency.size();
      for (size_t j = 0; j < adjacency.size(); j++)
        if (adjacency[j].distance > heaviest_edge)
          heaviest_edge = adjacency[j].distance;
      check_mesh_degree(this_vertex, side_length, adjacency.size());
      local_graph.append(adjacency);
    }
    local_graph.finish();
    publish_process_partition();
#ifdef INFO_PRINTS
    ckout << "PE " << CkMyPe() << " generated " << actual_edges << " edges"
          << endl;
#endif
    CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
    contribute(sizeof(cost), &heaviest_edge, CkReduction::max_long, cb);
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
    cost heaviest_edge = 0;
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
        for (size_t j = 0; j < adjacency.size(); j++)
          if (adjacency[j].distance > heaviest_edge)
            heaviest_edge = adjacency[j].distance;
      }
      local_graph.append(adjacency);
    }
    local_graph.finish();
    publish_process_partition();
    CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
    contribute(sizeof(cost), &heaviest_edge, CkReduction::max_long, cb);
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
    if (reader_tile_size == 0) {
      gapbs_read_slice(path, header, start_vertex, start_vertex + num_vertices,
                       graph_weights, row_offset, edges);
    } else {
      TileLayout layout(V, reader_tile_size, reader_tile_owners);
      row_offset.assign(1, 0);
      row_offset.reserve((size_t)num_vertices + 1);
      std::vector<long> slice_offsets;
      std::vector<Edge> slice_edges;
      for (long next = start_vertex; next < start_vertex + num_vertices;) {
        const long count = layout.contiguous(next, start_vertex + num_vertices);
        const long original = layout.original(next);
        gapbs_read_slice(path, header, original, original + count,
                         graph_weights, slice_offsets, slice_edges);
        const long base = (long)edges.size();
        for (long i = 1; i <= count; ++i)
          row_offset.push_back(base + slice_offsets[(size_t)i]);
        for (auto &edge : slice_edges) edge.end = layout.internal(edge.end);
        edges.insert(edges.end(), slice_edges.begin(), slice_edges.end());
        next += count;
      }
    }
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
   * The reduction every graph-construction path ends in: the heaviest edge
   * weight in the graph. Edges are sorted by weight within a row, so the
   * heaviest out-edge of a vertex is the last one.
   *
   * This used to be the sum of those out-edges, which Main stored in
   * SharedInfo::max_path and nothing ever read -- the reduction was a build
   * barrier carrying a dead value, and the comment claiming it became lmax was
   * wrong; lmax is numeric_limits<cost>::max(). The barrier is still needed and
   * still here, but it now carries the one distance-scale quantity the width
   * rule needs, at no extra collective and no extra pass over the rows.
   */
  void contribute_largest_outedges() {
    publish_process_partition();
    cost heaviest_edge = 0;
    for (long i = 0; i < num_vertices; i++) {
      long degree = local_graph.degree(i);
      if (degree > 0 && local_graph.edges(i)[degree - 1].distance > heaviest_edge)
        heaviest_edge = local_graph.edges(i)[degree - 1].distance;
    }
    CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
    contribute(sizeof(cost), &heaviest_edge, CkReduction::max_long, cb);
  }

  /**
   * --bucket-width-rule weight: bucket by distance rather than by |V|. The
   * width is the heaviest edge in the graph, so the histogram spans 2048 of
   * them; log(V) stays as a floor, for unit-weight inputs where 2048 hops of
   * span would leave the buckets too coarse to order anything.
   */
  void seed_bucket_width(cost max_edge_weight) {
    set_bucket_width(std::max(log((double)V), (double)max_edge_weight));
    contribute(CkCallback(CkReductionTarget(Main, width_seeded), mainProxy));
  }

  void get_graph(LongEdge *edges, long E, long *partition, int dividers) {
    actual_edges = E;
    initialize_data(partition, dividers);
    local_graph.build_from_edges(num_vertices, start_vertex, edges, E);
    contribute_largest_outedges();
  }

  void reset_for_source() {
#ifdef ACIC_WORK_COST
    work_cost::started = false;
#endif
    CkAssert(pq.empty() && deferred_updates.empty());
    if (process_share_active()) CkAssert(control_local->work.empty());
    for (int b = 0; b < HISTO_BUCKET_COUNT; ++b) {
      CkAssert(pq_hold[b].empty());
      histogram[b] = vcount[b] = 0;
    }
    vcount[HISTO_BUCKET_COUNT] = num_vertices;
    std::fill(distances, distances + num_vertices, lmax);
    updates_created_locally = updates_processed_locally = processed_at_contribution = 0;
    wasted_updates = rejected_updates = absorbed_updates = folded_updates = 0;
    send_filtered = tokens_created = tokens_stale = clamped_created_locally = 0;
    deferred_peak = deferred_total = skew_top_arrivals = bfs_created = bfs_processed = 0;
    updates_noted = bfs_noted = distance_changes = updates_in_tram = 0;
    heap_threshold = bfs_threshold = initial_threshold;
    tram_threshold = initial_threshold + 2;
    bucket_scale = 1; bucket_scale_inverse = 0; bucket_limit = HISTO_BUCKET_COUNT;
    current_phase = 0; last_round_starved = 1; pending_first_nonzero = 0;
    heap_queued = heap_yielded = clamp_coarsen_warned = false;
    flush_rng = VertexRng(thisIndex, S);
    std::fill(sent_cache.begin(), sent_cache.end(), SentEntry());
    send_filter_on = send_filter_bits > 0 ||
                    (send_filter_auto && initial_buffer_size() >= SEND_FILTER_MIN_BUFFER);
    set_bucket_width(initial_width);
    tram->changeThreshold(0, tram_threshold, 1.0);
    tram->setBufferSize(initial_buffer_size());
    tram->last_idle_flush = -1.0;
#ifdef ACIC_IPDPS_DIAG
    same_pe_changes = cross_pe_changes = 0;
#endif
#ifdef ACIC_DIAG
    std::fill(histo_created, histo_created + HISTO_BUCKET_COUNT, 0);
    std::fill(arrivals_per_vertex, arrivals_per_vertex + num_vertices, 0);
    std::fill(last_lead, last_lead + num_vertices, 0);
    batch_items = batch_absorbable = expansions = heavy_created = arrival_settled = 0;
    controller_rounds = idle_rounds = max_admitted_drift = processed_at_last_round = 0;
    frontier_bucket = 0;
    for (auto array : {deg_vertices, deg_edges, deg_arrivals, deg_rejects,
                       arr_vertices, arr_arrivals, settled_deg})
      std::fill(array, array + DEGREE_CLASSES, 0);
    std::fill(lead_expansions, lead_expansions + LEAD_CLASSES, 0);
#endif
    contribute(CkCallback(CkReductionTarget(Main, source_reset_done), mainProxy));
  }

  void start_papi() {
#ifdef ACIC_WORK_COST
    work_cost::ensure_start(CkMyPe());
#endif
#ifdef PAPI
    acic_prof::start(CkMyPe(), CkMyPe() == CkNodeFirst(CkMyNode()));
#endif
#ifdef ACIC_COMM_SHARE
    comm_share::work_tsc = comm_share::work_send_tsc = 0;
    htram_send_tsc = 0;
    comm_share::idle_tsc = comm_share::idle_t0 = 0;
    if (!idle_timers_registered) {
    idle_timers_registered = true;
    CcdCallOnConditionKeep(CcdPROCESSOR_BEGIN_IDLE,
                           (CcdCondFn)comm_share::idle_begin, nullptr);
    CcdCallOnConditionKeep(CcdPROCESSOR_END_IDLE,
                           (CcdCondFn)comm_share::idle_end, nullptr);
    }
    comm_share::window_s0 = CkWallTimer();
    comm_share::window_tsc0 = __rdtsc();
#endif
    traceBegin();
  }

  /**
   * Method that accepts initial update to source vertex.
   *
   * The source update is the one update nobody creates: Main injects it here
   * and it is then retired exactly like any other. Left uncounted it puts a
   * permanent -1 into the global histogram, in the bucket distance 0 falls in
   * -- bucket 0 -- and that is not a bookkeeping curiosity. The controller's
   * window starts at bucket 0, so a round in which the window holds no live
   * update sums to -1 rather than to 0, the "window is empty, open the
   * thresholds" rescue in reduce_histogram() does not fire, and the percentile
   * scan leaves both thresholds at the window's own origin. Nothing above the
   * origin is ever admitted again and the run cannot make progress or
   * terminate. Charging the update to the PE it starts on restores the
   * invariant both the rescue and the termination test read: a bucket's global
   * count is the number of live updates in it, and never negative.
   */
  void start_algo(Update new_vertex_and_distance) {
    histogram[charge_new_update(&new_vertex_and_distance)]++;
    updates_created_locally++;
    process_update(new_vertex_and_distance);
  }

  static void process_update_caller(void *p, Update *new_vertex_and_distances,
                                    int count) {
    // ckout << "PE " << CkMyPe() << " receiving " << count << " updates" <<
    // endl;
    COMM_SHARE_WORK
    SsspChares *self = (SsspChares *)p;
    self->maybe_pickup();
#ifdef ACIC_DIAG
    self->count_batch_duplicates(new_vertex_and_distances, count);
#endif
    if (batch_fold) {
      self->fold_batch(new_vertex_and_distances, count);
      return;
    }
    // distances[] is read at random by vertex; start the reads a few items
    // ahead of the one being applied.
    const int ahead = 8;
    for (int i = 0; i < count; i++) {
      if (i + ahead < count)
        __builtin_prefetch(
            &self->distances[update_vertex(new_vertex_and_distances[i + ahead]) -
                             self->start_vertex]);
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
      long key = update_vertex(items[i]);
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

  /**
   * --batch-fold. One pass keeps, for each destination vertex, the index of the
   * smallest-distance update seen so far; a loser is accounted for on the spot
   * and marked by setting its destination to -1, then a second pass processes
   * what is left in delivery order. The batch is this PE's own slice of the
   * message, so marking it in place touches nothing another PE reads.
   *
   * A folded update costs the receiver exactly what a rejected one would have:
   * its histogram bucket goes back and it counts as processed.
   */
  void fold_batch(Update *items, int count) {
    if (count <= 0)
      return;
    size_t slots = 64;
    while (slots < (size_t)count * 2)
      slots <<= 1;
    fold_table.assign(slots, -1);
    const size_t mask = slots - 1;
    for (int i = 0; i < count; i++) {
      // A loser is retired here rather than in process_update(), so the same
      // check has to happen before an item can become one.
      if (created_beyond_my_clamp(items[i])) {
        defer_until_extended(items[i]);
        items[i].dest_vertex = -1;
        continue;
      }
      long key = update_vertex(items[i]);
      size_t slot = (size_t)splitmix64((uint64_t)key) & mask;
      int j;
      while ((j = fold_table[slot]) != -1 && update_vertex(items[j]) != key)
        slot = (slot + 1) & mask;
      if (j == -1) {
        fold_table[slot] = i;
        continue;
      }
      int loser = i;
      if (items[i].distance < items[j].distance) {
        loser = j;
        fold_table[slot] = i;
      }
      histogram[bucket_of(items[loser])]--;
      updates_processed_locally++;
      folded_updates++;
      items[loser].dest_vertex = -1;
    }
    for (int i = 0; i < count; i++)
      if (items[i].dest_vertex != -1)
        process_update(items[i]);
  }

  // --combine hold. The key is the destination vertex and the fold keeps the
  // smaller distance, which is safe because relaxation is idempotent and
  // monotone: the loser could only ever have been rejected, or have caused a
  // relaxation the winner would redo.
  static uint64_t hold_key(const void *item) {
    return (uint64_t)update_vertex(*(const Update *)item);
  }
  static bool hold_min(void *held, const void *incoming, void *retired) {
    Update *h = (Update *)held;
    const Update *in = (const Update *)incoming;
    if (in->distance < h->distance) {
      std::memcpy(retired, h, sizeof(Update));
      *h = *in;
      return true;
    }
    std::memcpy(retired, in, sizeof(Update));
    return false;
  }
  static void hold_absorb(void *p, const void *retired) {
    ((SsspChares *)p)->absorb(*(const Update *)retired);
  }
  static const HoldOps hold_ops;

  /**
   * An update that will never be delivered, because the source hold folded it
   * into a better one for the same vertex. Everything the receiver would have
   * done to account for a rejected update happens here instead: the histogram
   * gives its bucket back and the update counts as processed. Without this the
   * termination test -- every created update processed -- can never be met.
   *
   * The bucket comes from bucket_of(), exactly as generate_updates() charged
   * it when it counted the update in -- which is the distance's bucket unless
   * the update was charged to the overflow slot, in which case it is that slot
   * whatever the clamp has since become.
   */
  void absorb(const Update &u) {
    histogram[bucket_of(u)]--;
    updates_processed_locally++;
    absorbed_updates++;
  }

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
    initial_width = width;
    bucket_multiplier = HISTO_BUCKET_COUNT / (HISTO_BUCKET_COUNT * width);
    light_cut = !lazy_active() ? 0
                : lazy_cut > 0 ? lazy_cut
                               : std::max<cost>(1, (cost)std::llround(width));
  }

  /**
   * Gets the histogram bucket for any given distance
   */
  int get_histo_bucket(cost distance) {
    double bucket = distance * bucket_multiplier;
    if (bucket >= bucket_limit)
      return HISTO_BUCKET_COUNT - 1;
    // long, not int: bucket_limit is 2048 * bucket_scale and --range-extend
    // raises it, so the pre-scale index passes INT_MAX before the scale cap
    // does. The quotient is under 2048 by construction either way.
    long result = (long)bucket;
    if (bucket_scale == 1)
      return (int)result;
    if ((unsigned long)result <= 0xffffffffUL)
      return (int)(((unsigned __int128)bucket_scale_inverse *
                    (unsigned long)result) >> 64);
    return (int)(result / bucket_scale);
  }

  /**
   * The bucket an existing update is charged to, which is not always the
   * bucket its distance falls in now. An update charged to the overflow slot
   * when it was created carries a flag saying so, and retires from that slot
   * however far the clamp has since moved; see UPDATE_OVERFLOW_BIT. Every
   * decrement of the histogram goes through here, because a decrement that
   * does not match its increment is not a rounding error -- it strands a count
   * in a bucket nothing will ever empty, and the controller then pins there.
   */
  int bucket_of(const Update &u) {
    return update_overflowed(u) ? HISTO_BUCKET_COUNT - 1
                                : get_histo_bucket(u.distance);
  }

  /**
   * True when this PE has not yet received an extension its creator already
   * had. A broadcast and a point-to-point message are not ordered against each
   * other, so a chare can be handed an update created under a clamp higher
   * than its own. The creator did not flag it, because under the creator's
   * clamp it was an ordinary bucket; this PE would clamp it, and would then
   * decrement the overflow slot against an increment that went to a real
   * bucket -- a count stranded in the one slot no merge ever moves, which is
   * the whole failure the freeze was written to prevent, arriving by a
   * different door. It is what made mesh20 finish in 0.28 s one run and not at
   * all the next.
   *
   * The condition detects itself: an update charged to the overflow slot
   * carries a flag saying so, so an *unflagged* update that this PE would
   * clamp can only have been created under a higher clamp than this PE's.
   * This PE is behind, and the answer is to wait rather than to guess.
   *
   * The wait is one broadcast. A chare contributes its histogram at the end of
   * current_thresholds(), so the round-n reduction cannot complete until every
   * chare has received broadcast n, and Main sends n+1 only after that: every
   * chare is at generation n before any chare reaches n+1.
   */
  bool created_beyond_my_clamp(const Update &u) {
    return !update_overflowed(u) &&
           (double)u.distance * bucket_multiplier >= bucket_limit;
  }

  /**
   * True when this PE would charge an unflagged update to the overflow index,
   * which its creator by construction did not: charge_new_update() flags
   * everything it sends to HISTO_BUCKET_COUNT - 1. So the creator was at a
   * different scale or clamp. created_beyond_my_clamp() is the clamp half of
   * this and a subset of it; the half it misses is the one that reaches the
   * shipped default, a receiver still at scale 1 handed an update from a
   * creator that has already coarsened. See --skew-defer.
   */
  bool charged_top_by_skew(const Update &u) {
    return !update_overflowed(u) &&
           get_histo_bucket(u.distance) == HISTO_BUCKET_COUNT - 1;
  }

  /** Hold an update until this PE has the clamp its creator had. */
  void defer_until_extended(const Update &u) {
    deferred_updates.push_back(u);
    if ((long)deferred_updates.size() > deferred_peak)
      deferred_peak = (long)deferred_updates.size();
  }

  /** Charge a new update to its bucket, flagging it if that is the overflow. */
  int charge_new_update(Update *u) {
    const int bucket = get_histo_bucket(u->distance);
    if (bucket == HISTO_BUCKET_COUNT - 1) {
      u->dest_vertex |= UPDATE_OVERFLOW_BIT;
      clamped_created_locally++;
    }
    return bucket;
  }

  /**
   * --bucket-policy adaptive: merge every k adjacent buckets into one. Every
   * structure indexed by bucket moves in ascending order -- bucket i's
   * contents go to i / k, which has already given its own away -- and htram's
   * holds follow. Updates already in flight stay consistent whichever width
   * their creator and their receiver use, because both land in the merged
   * bucket. Main has already divided the thresholds it sent with this.
   */
  bool clamp_coarsen_warned = false;

  void coarsen_buckets(int k, bool extend) {
    // The clamp bucket is the one bucket a merge cannot place. Its contents
    // mean "an original index at or past 2048 * scale", not an index, so
    // sending them to 2047 / k strands them: whoever retires those updates
    // recomputes the bucket from the distance at the new scale and finds a real
    // index somewhere above 2048 / k, leaving a live count at 2047 / k that
    // nothing will ever take away. If that count is the lowest one the
    // controller can see, the window pins there for the rest of the run.
    //
    // choose_coarsening() refuses while the reduced clamp count is positive,
    // but it reads the count as it stood when the chares contributed, and a
    // chare keeps retiring and creating updates between contributing and
    // receiving the broadcast this call is part of. That gap is not closed,
    // and this says so when it matters rather than leaving a run to hang:
    // a run that prints this and then stalls at first_nonzero = 2047 / k has
    // told the whole story.
    // ... unless the threshold is frozen, in which case the clamp bucket is
    // an overflow slot at a fixed distance rather than an index, merged_top
    // below leaves it out of the merge, and there is nothing to warn about.
    if (!clamp_freeze && histogram[HISTO_BUCKET_COUNT - 1] != 0 &&
        !clamp_coarsen_warned) {
      clamp_coarsen_warned = true;
      ckout << endl
            << "COARSEN_CLAMPED pe=" << CkMyPe() << " k=" << k
            << " scale=" << bucket_scale << " clamp_bucket="
            << histogram[HISTO_BUCKET_COUNT - 1] << " strands_at="
            << (HISTO_BUCKET_COUNT - 1) / k
            << ": merged buckets while updates were charged to the clamp "
               "bucket, whose real index the merge cannot know." << endl;
    }
    const int merged_top = clamp_freeze ? HISTO_BUCKET_COUNT - 1
                                       : HISTO_BUCKET_COUNT;
    for (int i = 1; i < merged_top; i++) {
      const int j = i / k;
      histogram[j] += histogram[i];
      histogram[i] = 0;
#ifdef VCOUNT
      vcount[j] += vcount[i];
      vcount[i] = 0;
#endif
#ifdef ACIC_DIAG
      histo_created[j] += histo_created[i];
      histo_created[i] = 0;
#endif
      if (!pq_hold[i].empty()) {
        pq_hold[j].insert(pq_hold[j].end(), pq_hold[i].begin(),
                          pq_hold[i].end());
        pq_hold[i].clear();
      }
    }
    bucket_scale *= k;
    bucket_scale_inverse = ~0UL / (unsigned long)bucket_scale + 1;
    if (!clamp_freeze)
      bucket_limit = (double)HISTO_BUCKET_COUNT * bucket_scale;
    else if (extend)
      // --range-extend. The merge above just freed the top half of the index
      // space, so the same k that bought it can be spent on distance instead:
      // the clamp moves out by k and every index stays in range. Updates
      // already charged to the overflow slot keep their flag and retire from
      // it, which is the whole reason the clamp is allowed to move at all.
      bucket_limit *= k;
    tram->coarsenBuckets(k, clamp_freeze);
  }

  /**
   * Expand a vertex whose queued distance is still current. With --lazy-heavy
   * only the light edges go now, and a token is left for the rest; see
   * lazy_cut.
   */
  void generate_updates(long local_index, bool bfs) {
    if (!bfs) COST_ADD(EXPANSIONS, 1);
#ifdef ACIC_DIAG
    if (!bfs) {
      const cost source_distance = distances[local_index];
      expansions++;
      // Natural widths above the lower edge of the frontier bucket.
      const double lead = (double)source_distance * bucket_multiplier -
                          (double)frontier_bucket * bucket_scale;
      const int lc = lead < 1.0 ? 0 : std::min(LEAD_CLASSES - 1,
                                               1 + (int)std::log2(lead));
      lead_expansions[lc]++;
      last_lead[local_index] = (unsigned char)lc;
    }
#endif
    const long degree = local_graph.degree(local_index);
    if (light_cut <= 0 || bfs) {
      relax_edges(local_index, 0, degree, bfs);
      return;
    }
    const long light_end = edges_up_to(local_index, light_cut);
    relax_edges(local_index, 0, light_end, bfs);
    if (light_end < degree)
      queue_token(local_index, distances[local_index], 0);
  }

  // Index of the first edge of local_index heavier than w; edges are sorted by
  // weight within a vertex (LocalCsr).
  long edges_up_to(long local_index, cost w) {
    const Edge *adjacency = local_graph.edges(local_index);
    const long degree = local_graph.degree(local_index);
    return std::upper_bound(adjacency, adjacency + degree, w,
                            [](cost x, const Edge &e) { return x < e.distance; }) -
           adjacency;
  }

  // The token for weight range level of local_index at distance d: charged to
  // the histogram at d + L 2^level, which no update it makes can undercut.
  void queue_token(long local_index, cost d, int level) {
    Update t;
    t.dest_vertex = (start_vertex + local_index) | UPDATE_TOKEN_BIT |
                    ((long)level << UPDATE_TOKEN_LEVEL_SHIFT);
    t.distance = d + range_low(level);
    const int bucket = charge_new_update(&t);
    histogram[bucket]++;
    updates_created_locally++;
    tokens_created++;
    if (bucket > heap_threshold)
      pq_hold[bucket].push_back(t);
    else
      pq.push(t);
  }

  // The lower end of token range `level`: L * G^level, saturating well above
  // any edge weight so the last range always reaches the end of the row.
  cost range_low(int level) const {
    cost lo = light_cut;
    for (int j = 0; j < level; j++) {
      if (lo > (cost)1 << 40)
        return lo;
      lo *= lazy_growth;
    }
    return lo;
  }

  // An admitted token: relax its range if its vertex has not moved since, and
  // leave the next range's token. The caller retires it from the histogram.
  void release_token(const Update &t) {
    const long local_index = update_vertex(t) - start_vertex;
    const int level = update_token_level(t);
    const cost lo = range_low(level);
    const cost d = t.distance - lo;
    if (distances[local_index] != d) {
      tokens_stale++;
      return;
    }
    const long degree = local_graph.degree(local_index);
    const long begin = edges_up_to(local_index, lo);
    // Five bits hold the level; the last range reaches the end of the row.
    const long end = (level >= 30 || range_low(level + 1) > ((cost)1 << 40))
                         ? degree
                         : edges_up_to(local_index, range_low(level + 1));
    relax_edges(local_index, begin, end, false);
    if (end < degree)
      queue_token(local_index, d, level + 1);
  }

  void relax_edges(long local_index, long first, long last, bool bfs) {
    const Edge *adjacency = local_graph.edges(local_index);
    send_relaxations(adjacency, first, last, distances[local_index], bfs);
  }

  void send_relaxations(const Edge *adjacency, long first, long last,
                        cost source_distance, bool bfs) {
    const long degree = last;
    if (!bfs) COST_ADD(EDGE_ATTEMPTS, last - first);
#ifdef ACIC_DIAG
    const double heavy_cut = 1.0 / bucket_multiplier;
#endif
    for (long i = first; i < degree; i++) {
#ifdef ACIC_DIAG
      if (adjacency[i].distance > heavy_cut)
        heavy_created++;
#endif
      // calculate distance pair for neighbor
      Update new_update;
      new_update.dest_vertex = adjacency[i].end;
      new_update.distance = source_distance + adjacency[i].distance;
      if (send_filter_on) {
        // The table is read at random; the targets a few edges on are
        // already known, so start those reads now.
        if (i + 8 < degree)
          __builtin_prefetch(&sent_cache[((unsigned long)adjacency[i + 8].end *
                                          0x9E3779B97F4A7C15UL) >>
                                         sent_cache_shift]);
        SentEntry &e = sent_cache[((unsigned long)new_update.dest_vertex *
                                   0x9E3779B97F4A7C15UL) >>
                                  sent_cache_shift];
        const unsigned long v = (unsigned long)new_update.dest_vertex;
        const unsigned long d = (unsigned long)new_update.distance;
        if (e.vertex == v && e.distance <= d) {
          send_filtered++;
          continue;
        }
        if (((v | d) >> 32) == 0) {
          e.vertex = (uint32_t)v;
          e.distance = (uint32_t)d;
        }
      }
      // we are going to send this, so add to the histogram and the send update
      // count
      int neighbor_bucket = charge_new_update(&new_update);
      histogram[neighbor_bucket]++;
#ifdef ACIC_DIAG
      histo_created[neighbor_bucket]++;
#endif
      updates_created_locally++;
      // Bucket 0 means "send now"; a bucket above the threshold hands the
      // item to the library's own per-destination hold, to be released when
      // changeThreshold() admits that bucket.
      // The destination is looked up once here and handed to the library,
      // which would otherwise look it up again through get_dest_proc.
      const int dest_pe = get_dest_proc_fast(update_vertex(new_update));
#ifdef ACIC_WORK_COST
      if (CkNodeOf(dest_pe) == CkMyNode()) COST_ADD(INTRA_PROCESS, 1);
      else if (CmiPhysicalNodeID(dest_pe) == CmiPhysicalNodeID(my_pe)) COST_ADD(INTRA_NODE, 1);
      else COST_ADD(INTER_NODE, 1);
#endif
#ifdef ACIC_IPDPS_DIAG
      if (dest_pe != my_pe) new_update.dest_vertex |= UPDATE_CROSS_PE_BIT;
#endif
      if (process_share_active() && CkNodeOf(dest_pe) == CkMyNode()) {
        process_shared_update(new_update);
        continue;
      }
#ifndef ALL_TO_TRAM_HOLD
      if ((neighbor_bucket > tram_threshold) && !bfs) {
        tram->sendItemPrioDeferredDest(new_update, neighbor_bucket, dest_pe);
      } else {
#ifndef LOCAL_TO_TRAM
        if (dest_pe == my_pe)
          process_update(new_update);
        else
          tram->sendItemPrioDeferredDest(new_update, 0, dest_pe);
#else
        tram->sendItemPrioDeferredDest(new_update, 0, dest_pe);
#endif
      }
#else
      tram->sendItemPrioDeferredDest(new_update, neighbor_bucket, dest_pe);
      if (neighbor_bucket <= tram_threshold)
        updates_in_tram++;
#endif
    }
  }


  void publish_process_partition() {
    if (!process_share_active()) return;
    auto *node = controlProxy.ckLocalBranch();
    auto &part = node->partitions[CkMyRank()];
    part.first = start_vertex;
    part.size = num_vertices;
    part.graph = &local_graph;
    part.distances = distances;
#ifdef ACIC_DIAG
    part.arrivals = arrivals_per_vertex;
    part.last_lead = last_lead;
#endif
    // All partitions are published before Main::begin's reduction completes.
  }

  ProcessPartition &process_partition(long vertex) {
    const int pe = get_dest_proc_fast(vertex);
    CkAssert(CkNodeOf(pe) == CkMyNode());
    auto &part = control_local->partitions[pe - CkNodeFirst(CkMyNode())];
    CkAssert(part.graph != nullptr && vertex >= part.first && vertex < part.first + part.size);
    return part;
  }

  void process_shared_update(const Update &u) {
    // Same skew rule as the owner path: never retire against a stale clamp.
    if (created_beyond_my_clamp(u)) {
      defer_until_extended(u);
      return;
    }
    const int bucket = bucket_of(u);
    auto &part = process_partition(update_vertex(u));
    const long index = update_vertex(u) - part.first;
    cost *address = part.distances + index;
    cost old = __atomic_load_n(address, __ATOMIC_RELAXED);
#ifdef ACIC_DIAG
    __atomic_fetch_add(part.arrivals + index, 1L, __ATOMIC_RELAXED);
    const int dc = degree_class(part.graph->degree(index));
    deg_arrivals[dc]++;
    if (old != lmax && get_histo_bucket(old) < frontier_bucket) {
      arrival_settled++;
      settled_deg[dc]++;
    }
#endif
    bool changed = false;
    while (u.distance < old) {
      COST_ADD(CAS_ATTEMPTS, 1);
      if (__atomic_compare_exchange_n(address, &old, u.distance, false,
                                      __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        changed = true;
        break;
      }
      COST_ADD(CAS_FAILURES, 1);
    }
    updates_noted++;
    if (changed) {
      distance_changes++;
#ifdef VCOUNT
      vcount[bucket]++;
      vcount[old == lmax ? HISTO_BUCKET_COUNT : get_histo_bucket(old)]--;
#endif
#ifdef ACIC_IPDPS_DIAG
      if (u.dest_vertex & UPDATE_CROSS_PE_BIT) cross_pe_changes++;
      else same_pe_changes++;
#endif
      if (part.graph->degree(index)) {
        const long original_bucket = update_overflowed(u)
            ? std::numeric_limits<long>::max()
            : (long)((double)u.distance * bucket_multiplier);
        COST_ADD(QUEUE_PUSHES, 1);
        control_local->work.push(CkMyRank(), u, original_bucket);
        return; // The worker that expands (or rejects) it retires its charge.
      }
    } else {
      rejected_updates++;
#ifdef ACIC_DIAG
      deg_rejects[dc]++;
#endif
    }
    wasted_updates++;
    histogram[bucket]--;
    updates_processed_locally++;
  }

  void process_shared_heap() {
    Update u;
    int processed = 0;
    while (processed < 100 && control_local->work.pop(CkMyRank(),
           [this](const Update &v) {
             return !created_beyond_my_clamp(v) && bucket_of(v) <= heap_threshold;
           }, u)) {
#ifdef ACIC_WORK_COST
      work_cost::ensure_start(CkMyPe());
#endif
      ++processed;
      COST_ADD(QUEUE_POPS, 1);
      const int bucket = bucket_of(u);
      auto &part = process_partition(update_vertex(u));
      const long index = update_vertex(u) - part.first;
      if (u.distance == __atomic_load_n(part.distances + index, __ATOMIC_RELAXED)) {
        COST_ADD(EXPANSIONS, 1);
#ifdef ACIC_DIAG
        expansions++;
        const double lead = (double)u.distance * bucket_multiplier -
                            (double)frontier_bucket * bucket_scale;
        const int lc = lead < 1.0 ? 0 : std::min(LEAD_CLASSES - 1, 1 + (int)std::log2(lead));
        lead_expansions[lc]++;
        __atomic_store_n(part.last_lead + index, (unsigned char)lc, __ATOMIC_RELAXED);
#endif
        // Expand the accepted snapshot, not a distance that can change halfway
        // through the adjacency scan. A concurrent improvement queues new work.
        send_relaxations(part.graph->edges(index), 0, part.graph->degree(index), u.distance, false);
      } else {
        COST_ADD(STALE_POPS, 1);
        rejected_updates++;
      }
      wasted_updates++;
      histogram[bucket]--;
      updates_processed_locally++;
    }
    if (processed == 100 && !heap_queued) {
      heap_yielded = true;
      heap_queued = true;
      thisProxy[thisIndex].process_heap();
    }
  }

  /**
   * Takes a distance update and immediately adds it to the local heap/pq
   */
  inline void process_update(Update new_vertex_and_distance) {
#ifdef ACIC_WORK_COST
    work_cost::ensure_start(CkMyPe());
#endif
    if (process_share_active()) {
      process_shared_update(new_vertex_and_distance);
      return;
    }
    // charged_top_by_skew() and bucket_of(), with the bucket computed once.
    const bool overflowed = update_overflowed(new_vertex_and_distance);
    const int distance_bucket =
        overflowed ? HISTO_BUCKET_COUNT - 1
                   : get_histo_bucket(new_vertex_and_distance.distance);
    if (!overflowed && distance_bucket == HISTO_BUCKET_COUNT - 1) {
      skew_top_arrivals++;
      if (skew_defer || created_beyond_my_clamp(new_vertex_and_distance)) {
        defer_until_extended(new_vertex_and_distance);
        return;
      }
    }
    long dest_vertex = update_vertex(new_vertex_and_distance);
    long local_index = dest_vertex - start_vertex;
    cost this_cost = new_vertex_and_distance.distance;
    int this_bucket = distance_bucket;
#ifdef ACIC_DIAG
    arrivals_per_vertex[local_index]++;
    const int deg_class = degree_class(local_graph.degree(local_index));
    deg_arrivals[deg_class]++;
    if (distances[local_index] != lmax &&
        get_histo_bucket(distances[local_index]) < frontier_bucket) {
      arrival_settled++;
      settled_deg[deg_class]++;
    }
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
#ifdef ACIC_IPDPS_DIAG
      if (new_vertex_and_distance.dest_vertex & UPDATE_CROSS_PE_BIT)
        cross_pe_changes++;
      else
        same_pe_changes++;
#endif
      updates_noted++;
      int pq_bucket;
      if (local_graph.degree(local_index) > 0) {
        COST_ADD(QUEUE_PUSHES, 1);
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
  // --warm-links: one message to a PE of every other process.
  void warm_links() {
    for (int q = 0; q < CkNumNodes(); q++)
      if (q != CkMyNode())
        thisProxy[CkNodeFirst(q) + CkMyRank() % CkNodeSize(q)].warm_ping();
  }
  void warm_ping() {}

  bool heap_queued = false; // --control node: a process_heap message is pending
  void process_heap() {
    heap_queued = false;
    maybe_pickup();
    COMM_SHARE_WORK
    heap_yielded = false;
    if (process_share_active()) {
      process_shared_heap();
      return;
    }
#ifdef PQ_HOLD_ONLY
    for (int i = 0; i <= heap_threshold; i++) // iterate to heap threshold
    {
      long items_processed = 0;
      items_processed = pq_hold[i].size();
      for (int j = 0; j < pq_hold[i].size();
           j++) // iterate pq bucket in reverse
      {
        Update new_vertex_and_distance = pq_hold[i][j];
        long dest_vertex = update_vertex(new_vertex_and_distance);
        cost new_distance = new_vertex_and_distance.distance;
        int this_histo_bucket = bucket_of(new_vertex_and_distance);
        long local_index = dest_vertex - start_vertex;
        COST_ADD(QUEUE_POPS, 1);
        if (new_distance == distances[local_index]) {
          // for all neighbors
          generate_updates(local_index, false);
        } else {
          COST_ADD(STALE_POPS, 1);
          rejected_updates++;
        }
        wasted_updates++;
        histogram[this_histo_bucket]--;
        updates_processed_locally++;
      }
      if (items_processed > 0) {
        pq_hold[i].clear();
        heap_yielded = true;
        arr[thisIndex].process_heap();
        break;
      }
    }
#else
    int heap_count = 0;
    while (pq.size() > 0) {
      if (++heap_count > 100) {
        heap_yielded = true;
        thisProxy[thisIndex].process_heap();
        break;
      } // give other eps a chance to run
      Update new_vertex_and_distance = pq.top();
      long dest_vertex = update_vertex(new_vertex_and_distance);
      cost new_distance = new_vertex_and_distance.distance;
      int this_histo_bucket = bucket_of(new_vertex_and_distance);
      if (this_histo_bucket > heap_threshold) {
        break;
      }
      pq.pop();
      if (update_is_token(new_vertex_and_distance)) {
        release_token(new_vertex_and_distance);
        histogram[this_histo_bucket]--;
        updates_processed_locally++;
        continue;
      }
      COST_ADD(QUEUE_POPS, 1);
      if (dest_vertex >= partition_index[thisIndex] &&
          dest_vertex < partition_index[thisIndex + 1]) {
        long local_index = dest_vertex - start_vertex;
        //  if the incoming distance is actually smaller
        if (new_distance == distances[local_index]) {
          generate_updates(local_index, false);
        } else {
          COST_ADD(STALE_POPS, 1);
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
    info_array[histo_reduction_width + 7] = histogram[HISTO_BUCKET_COUNT - 1];
    // Slot 7 is what is sitting in the overflow slot; slot 8 is how many have
    // ever been put there. --range-extend needs the second, because the first
    // does not fall when the clamp rises: an update charged to the overflow
    // slot retires from it whatever the clamp becomes, so a controller that
    // read the standing count would see its own raise do nothing and raise
    // again, every round, until the scale ran away. The arrival count's
    // round-over-round difference is the only thing that says whether the
    // range is still too small.
    info_array[histo_reduction_width + 8] = clamped_created_locally;
    info_array[histo_reduction_width + 9] = send_filtered;
    info_array[histo_reduction_width + 10] =
        updates_processed_locally != processed_at_contribution;
    processed_at_contribution = updates_processed_locally;
    if (control_mode == CONTROL_NODE)
      control_local->add(info_array, histo_reduction_width + 11);
    else
      contribute((histo_reduction_width + 11) * sizeof(long), info_array,
                 CkReduction::sum_long, cb);
  }

  /**
   * One line per PE, printed only when Main has seen the run stop making
   * progress. Answers where this PE's share of the outstanding work is: still
   * in a hold that no threshold has admitted, admitted but sitting in a buffer
   * nothing has flushed, delivered but parked above the heap threshold, or
   * simply not here. The bucket extremes are over all HISTO_BUCKET_COUNT
   * buckets rather than over the reduced window, which is the point -- work
   * the controller cannot see is exactly what it cannot aim a threshold at.
   *
   * O(HISTO_BUCKET_COUNT) plus a walk of htram's holds, run once per stall
   * report rather than once per round, so none of it is on a timed path.
   */
  void report_progress_state(int tag) {
    long pq_hold_items = 0, live = 0;
    int lowest = -1, highest = -1;
    // created_*: derived from histogram[], which is a DISTRIBUTED LEDGER --
    // incremented by the PE that creates an update, decremented by the PE that
    // retires it. histogram[b] > 0 here means this PE created something in
    // bucket b that nobody has retired, and it is most likely sitting on a
    // different PE. These fields say nothing about what this PE can run, and
    // reading them as though they did is what made the first pass at 7.6g
    // reach two wrong conclusions; see design/step76-default-deadlock.md.
    // Per-PE live is legitimately negative when a PE retires more than it
    // created. Only the `main` line, which sums across PEs, is an inventory.
    int hold_lowest = -1, hold_highest = -1;
    long holds_admissible = 0;
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++) {
      const long held_here = (long)pq_hold[i].size();
      pq_hold_items += held_here;
      if (held_here > 0) {
        if (hold_lowest < 0)
          hold_lowest = i;
        hold_highest = i;
        if (i <= heap_threshold)
          holds_admissible += held_here;
      }
      live += histogram[i];
      if (histogram[i] > 0) {
        if (lowest < 0)
          lowest = i;
        highest = i;
      }
    }
    // held_*: what this PE PHYSICALLY HOLDS and could be asked to run, which
    // is the question the per-PE line exists to answer. pq is a heap ordered
    // by distance, not by bucket, so its runnable head is found by asking
    // bucket_of() of the top rather than by scanning; pq_top_bucket of -1
    // means the queue is empty. A stalled PE with pq_top_bucket <= its own
    // heap_threshold, or held_admissible > 0, is holding work it is allowed
    // to run and is not running it -- that, and only that, is a drain failure.
    const int pq_top_bucket = pq.empty() ? -1 : bucket_of(pq.top());
    // pq_admissible counts queued updates at or below the threshold, wherever
    // they sit in the heap; if it is positive while pq_top_bucket is above the
    // threshold, process_heap() is stopping at a top that hides runnable work
    // (see --pq-overflow-last). pq_overflowed counts flagged updates queued.
    // A copy is walked, so this is O(n log n) -- stall reports only.
    long pq_admissible = 0, pq_overflowed = 0;
    {
      auto copy = pq;
      while (!copy.empty()) {
        const Update &u = copy.top();
        if (bucket_of(u) <= heap_threshold)
          pq_admissible++;
        if (update_overflowed(u))
          pq_overflowed++;
        copy.pop();
      }
    }
    long long held = 0, admitted = 0, buffered = 0;
    tram->pendingItems(&held, &admitted, &buffered);
    ckout << "PROGRESS_STALL " << tag << " pe=" << CkMyPe()
          << " created=" << updates_created_locally
          << " processed=" << updates_processed_locally
          << " noted=" << updates_noted
          // Renamed from live/lowest_live_bucket/highest_live_bucket/clamped.
          // The old names read as an inventory of this PE and are not one; the
          // created_ prefix is the whole warning.
          << " created_live=" << live
          << " created_lowest=" << lowest
          << " created_highest=" << highest
          << " created_clamped=" << histogram[HISTO_BUCKET_COUNT - 1]
          << " pq=" << (long)pq.size() << " pq_hold=" << pq_hold_items
          << " pq_top_bucket=" << pq_top_bucket
          << " pq_admissible=" << pq_admissible
          << " pq_overflowed=" << pq_overflowed
          << " held_lowest=" << hold_lowest
          << " held_highest=" << hold_highest
          << " held_admissible=" << holds_admissible
          << " tram_held=" << held << " tram_admitted=" << admitted
          << " tram_buffered=" << buffered
          << " heap_threshold=" << heap_threshold
          << " tram_threshold=" << tram_threshold
          << " bucket_scale=" << bucket_scale
          // Held for having been created beyond this PE's clamp. Nonzero here
          // at a stall would mean the drain in current_thresholds() is not
          // running, which is the one way --range-extend could lose an update.
          << " deferred=" << (long)deferred_updates.size()
          << " deferred_peak=" << deferred_peak
          << " deferred_total=" << deferred_total
          << " skew_top_arrivals=" << skew_top_arrivals
          // This PE's own copy of the two 7.6g flags. Both were first added as
          // plain globals, parsed in Main and so set only in process 0: at 8 x
          // 15 the "on" arm was on for 15 PEs of 120. They are readonlies now,
          // and a stall line says what the PE that stalled was running.
          << " skew_defer=" << skew_defer
          << " pq_overflow_last=" << pq_overflow_last
          << " pq_hold_top=" << (long)pq_hold[HISTO_BUCKET_COUNT - 1].size()
          << " bucket_limit=" << bucket_limit << endl;
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
                          int phase, int starved, int coarsen, int extend,
                          int buffer_size_now) {
    // Before the threshold change below, which releases held items against a
    // level that is a multiple of the buffer size.
    if (buffer_size_now > 0) {
      tram->setBufferSize(buffer_size_now);
      if (send_filter_auto && send_filter_bits == 0)
        send_filter_on = buffer_size_now >= SEND_FILTER_MIN_BUFFER;
    }
    if (coarsen > 1)
      coarsen_buckets(coarsen, extend != 0);
    // This PE now has whatever clamp the broadcast carried, so anything held
    // for having been created beyond the old one can go through. Re-checked
    // rather than assumed: process_update() simply defers again if this PE is
    // somehow still behind, which cannot happen at a skew of one broadcast.
    if (!deferred_updates.empty()) {
      std::vector<Update> ready;
      ready.swap(deferred_updates);
      deferred_total += (long)ready.size();
      for (size_t i = 0; i < ready.size(); i++)
        process_update(ready[i]);
    }
#ifdef ACIC_DIAG
    frontier_bucket = behind_first_nonzero + 1;
    controller_rounds++;
    if (updates_processed_locally == processed_at_last_round)
      idle_rounds++;
    processed_at_last_round = updates_processed_locally;
#endif
    heap_threshold = _heap_threshold;
    tram_threshold = _tram_threshold;
    bfs_threshold = _bfs_threshold;
    current_phase = phase;
    last_round_starved = starved;
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
#ifdef ACIC_DIAG
    max_admitted_drift =
        std::max(max_admitted_drift, (long)tram->admittedDrift());
#endif
#ifndef PQ_HOLD_ONLY
    if (control_mode == CONTROL_NODE)
      clear_pq_hold();
    else
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
    if (flush_policy == FLUSH_STALE ||
        (flush_policy == FLUSH_ADAPTIVE && starved))
      tram->flushStale();
    if (flush_rng.bounded((uint64_t)flush_round_interval) == 0)
      tram->tflush();
    //    tram->sanityCheck();
    //    tram->flush_everything();
    if (control_mode != CONTROL_NODE)
      arr[thisIndex].process_heap();
    else if (!heap_queued) {
      heap_queued = true;
      arr[thisIndex].process_heap();
    }
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
    const long long instructions = acic_prof::stop(CkMyPe());
#endif
    std::vector<long> msg_stats(stat_count, 0);
#ifdef ACIC_WORK_COST
    work_cost::stop();
    work_cost::counts[work_cost::CHANGES] = distance_changes;
    std::copy(work_cost::counts, work_cost::counts + work_cost::COUNT,
              msg_stats.begin() + STAT_COST_METRICS);
#endif
#ifdef ACIC_COMM_SHARE
    msg_stats[STAT_WINDOW_TSC] = (long)(__rdtsc() - comm_share::window_tsc0);
    msg_stats[STAT_WINDOW_US] =
        (long)(1e6 * (CkWallTimer() - comm_share::window_s0));
    msg_stats[STAT_WORK_TSC] = (long)comm_share::work_tsc;
    msg_stats[STAT_WORK_SEND_TSC] = (long)comm_share::work_send_tsc;
    msg_stats[STAT_SEND_TSC] = (long)htram_send_tsc;
    comm_share::idle_end(nullptr);
    msg_stats[STAT_IDLE_TSC] = (long)comm_share::idle_tsc;
#endif
    msg_stats[STAT_WASTED] = wasted_updates;
    msg_stats[STAT_REJECTED] = rejected_updates;
    for (int i = 0; i < HISTO_BUCKET_COUNT + 1; i++) {
      msg_stats[STAT_VCOUNT + i] = vcount[i];
    }
    msg_stats[STAT_NOTED] = updates_noted;
    msg_stats[STAT_EDGES] = actual_edges;
    msg_stats[STAT_DISTANCE_CHANGES] = distance_changes;
#ifdef ACIC_IPDPS_DIAG
    msg_stats[STAT_SAME_PE_CHANGES] = same_pe_changes;
    msg_stats[STAT_CROSS_PE_CHANGES] = cross_pe_changes;
#endif
    // Topology bytes actually held, summed over PEs. Reported from inside the
    // run because peak RSS cannot see it: this runtime reserves a fixed ~554 MB
    // regardless of graph or PE count, which swamps the graph until it passes a
    // few million vertices.
    msg_stats[STAT_GRAPH_BYTES] =
        (long)(local_graph.bytes() + sizeof(cost) * (size_t)num_vertices);
    msg_stats[STAT_ABSORBED] = absorbed_updates;
    msg_stats[STAT_FOLDED] = folded_updates;
    msg_stats[STAT_SKEW_TOP] = skew_top_arrivals;
    msg_stats[STAT_SEND_FILTERED] = send_filtered;
    msg_stats[STAT_TOKENS] = tokens_created;
    msg_stats[STAT_TOKENS_STALE] = tokens_stale;
#ifdef PAPI
    msg_stats[STAT_INSTRUCTIONS] = instructions;
#endif
#ifdef ACIC_DIAG
    msg_stats[STAT_BATCH_ITEMS] = batch_items;
    msg_stats[STAT_BATCH_ABSORBABLE] = batch_absorbable;
    msg_stats[STAT_ADMITTED_DRIFT] = max_admitted_drift;
    msg_stats[STAT_EXPANSIONS] = expansions;
    msg_stats[STAT_HEAVY_CREATED] = heavy_created;
    msg_stats[STAT_ARRIVAL_SETTLED] = arrival_settled;
    {
      const double heavy_cut = 1.0 / bucket_multiplier;
      for (long i = 0; i < num_vertices; i++) {
        const long degree = local_graph.degree(i);
        if (distances[i] == lmax || degree == 0)
          continue;
        msg_stats[STAT_REACHED_EXPANDABLE]++;
        msg_stats[STAT_REACHED_EDGES] += degree;
        const Edge *adjacency = local_graph.edges(i);
        for (long e = 0; e < degree; e++)
          if (adjacency[e].distance > heavy_cut)
            msg_stats[STAT_REACHED_HEAVY]++;
        msg_stats[STAT_LEAD_FINAL + last_lead[i]]++;
      }
      for (int i = 0; i < LEAD_CLASSES; i++)
        msg_stats[STAT_LEAD_EXPANSIONS + i] = lead_expansions[i];
      for (int i = 0; i < DEGREE_CLASSES; i++)
        msg_stats[STAT_SETTLED_DEG + i] = settled_deg[i];
    }
    for (int i = 0; i < HISTO_BUCKET_COUNT; i++) {
      msg_stats[STAT_HISTO_CREATED + i] = histo_created[i];
      msg_stats[STAT_HISTO_LIVE + i] = histogram[i];
    }
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
          << " batch_absorbable=" << batch_absorbable
          << " rounds=" << controller_rounds
          << " idle_rounds=" << idle_rounds << endl;
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
    TileLayout layout(V, reader_tile_size, reader_tile_owners);
    for (long i = 0; i < num_vertices; i++) {
      digest.add(layout.original(start_vertex + i), distances[i], lmax);
    }
    unsigned long long values[4] = {digest.h1, digest.h2, digest.reachable,
                                    digest.distance_sum};
    CkCallback cb(CkReductionTarget(Main, done_verify), mainProxy);
    contribute(4 * sizeof(unsigned long long), values,
               CkReduction::sum_ulong_long, cb);
  }

  void get_max_cost() {
    cost max_cost = 0;
    for (long i = 0; i < num_vertices; i++) {
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

const HoldOps SsspChares::hold_ops = {sizeof(Update), SsspChares::hold_key,
                                     SsspChares::hold_min,
                                     SsspChares::hold_absorb};

#include "sssp_smp.def.h"
