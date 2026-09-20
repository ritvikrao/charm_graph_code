#pragma once
#include <sstream>

// R0: no vertex arrays or diagnostic atomics. Each worker owns its counters.
// Sampling covers inclusive queue calls, not an additive wall-time partition.
namespace work_cost {
enum Field {
  EDGE_ATTEMPTS, EXPANSIONS, CHANGES, QUEUE_PUSHES, QUEUE_POPS, STALE_POPS,
  CAS_ATTEMPTS, CAS_FAILURES, INTRA_PROCESS, INTRA_NODE, INTER_NODE,
  QUEUE_PROBES, LOCK_MISSES, CPU_NS,
  PUSH_CALLS, PUSH_SAMPLES, PUSH_TICKS,
  POP_CALLS, POP_SAMPLES, POP_TICKS,
  COUNT
};
inline const char *names[] = {
  "edge_attempts", "expansions", "changes", "queue_pushes", "queue_pops", "stale_pops",
  "cas_attempts", "cas_failures", "intra_process", "intra_node", "inter_node",
  "queue_probes", "lock_misses", "cpu_ns",
  "push_calls", "push_samples", "push_ticks", "pop_calls", "pop_samples", "pop_ticks"
};
inline std::string record(const long *values) {
  std::ostringstream out;
  out << "WORK_COST sample_period=1024";
  for (int i = 0; i < COUNT; ++i) out << ' ' << names[i] << '=' << values[i];
  return out.str();
}
}

#ifdef ACIC_WORK_COST
#include <ctime>
#include <x86intrin.h>
namespace work_cost {
inline thread_local long counts[COUNT] = {};
inline thread_local long cpu_start = 0;
inline thread_local unsigned sample_offset = 0;
inline thread_local bool started = false;
inline long cpu_ns() {
  timespec t;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) std::abort();
  return t.tv_sec * 1000000000L + t.tv_nsec;
}
inline void start(unsigned worker) {
  for (auto &v : counts) v = 0;
  sample_offset = (worker * 97U) & 1023U;
  cpu_start = cpu_ns();
  started = true;
}
// Array start messages and the timer broadcast are not ordered. A source or
// stolen expansion can arrive first; never erase its counters on timer pickup.
inline void ensure_start(unsigned worker) { if (!started) start(worker); }
inline void stop() { counts[CPU_NS] = cpu_ns() - cpu_start; }
struct Sample {
  Field base;
  unsigned long begin = 0;
  explicit Sample(Field b) : base(b) {
    if (((++counts[base] + sample_offset) & 1023) == 1) {
      ++counts[base + 1];
      begin = __rdtsc();
    }
  }
  ~Sample() { if (begin) counts[base + 2] += __rdtsc() - begin; }
};
}
#define COST_ADD(field, n) (work_cost::counts[work_cost::field] += (n))
#define COST_SCOPE(field) work_cost::Sample cost_sample_(work_cost::field)
#else
#define COST_ADD(field, n) ((void)0)
#define COST_SCOPE(field) ((void)0)
#endif
