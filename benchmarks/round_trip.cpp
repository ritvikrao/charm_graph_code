// Empty version of SSSP's array broadcast -> per-PE sum_long contribution ->
// expedited Main callback. This measures the unloaded round-trip floor, not
// communication time inside the real solve. Run with the same layout/runtime.
#include "round_trip.decl.h"
#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <vector>

CProxy_RoundMain roundMain;
int payload_longs;

class RoundMain : public CBase_RoundMain {
  CProxy_RoundWorker workers;
  int rounds, warmup, sequence = 0;
  double sent;
  std::vector<double> samples;
  void next() {
    sent = CkWallTimer();
    workers.thresholds(sequence, 0, 0, 0, 0, 0, 0, 0, 0);
  }
public:
  explicit RoundMain(CkArgMsg *m) {
    if (m->argc != 4) CkAbort("usage: round_trip PAYLOAD_LONGS ROUNDS WARMUP");
    payload_longs = std::atoi(m->argv[1]);
    rounds = std::atoi(m->argv[2]);
    warmup = std::atoi(m->argv[3]);
    if (payload_longs < 1 || payload_longs > 4096 || rounds < 1 || warmup < 0)
      CkAbort("invalid round_trip arguments");
    delete m;
    roundMain = thisProxy;
    samples.reserve(rounds);
    workers = CProxy_RoundWorker::ckNew(CkNumPes());
  }
  void ready() { next(); }
  void reduced(long *values, int n) {
    const double elapsed = CkWallTimer() - sent;
    if (n != payload_longs || values[0] != (long)CkNumPes() * (sequence + 1))
      CkAbort("round_trip reduction/sequence mismatch");
    for (int j = 1; j < n; ++j)
      if (values[j] != (long)CkNumPes() * j)
        CkAbort("round_trip payload mismatch");
    if (sequence >= warmup) samples.push_back(elapsed);
    if (++sequence < warmup + rounds) { next(); return; }
    std::sort(samples.begin(), samples.end());
    const double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / rounds;
    CkPrintf("ROUND_TRIP PASS pes=%d processes=%d payload_longs=%d rounds=%d warmup=%d mean_us=%.6f p50_us=%.6f p95_us=%.6f min_us=%.6f max_us=%.6f\n",
      CkNumPes(), CkNumNodes(), payload_longs, rounds, warmup, 1e6 * mean,
      1e6 * samples[rounds / 2], 1e6 * samples[(rounds - 1) * 95 / 100],
      1e6 * samples.front(), 1e6 * samples.back());
    CkExit();
  }
};

class RoundWorker : public CBase_RoundWorker {
  std::vector<long> values;
  int previous = -1;
public:
  RoundWorker() : values(payload_longs) {
    if (thisIndex != CkMyPe()) CkAbort("round_trip requires one worker per PE");
    for (int i = 0; i < payload_longs; ++i) values[i] = i;
    contribute(CkCallback(CkReductionTarget(RoundMain, ready), roundMain));
  }
  void thresholds(int round, int, int, int, int, int, int, int, int) {
    if (round != ++previous) CkAbort("round_trip broadcast sequence mismatch");
    values[0] = round + 1;
    contribute(payload_longs * sizeof(long), values.data(), CkReduction::sum_long,
               CkCallback(CkReductionTarget(RoundMain, reduced), roundMain));
  }
};

#include "round_trip.def.h"
