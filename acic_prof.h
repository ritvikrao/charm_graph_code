#pragma once
// Hardware-counter profile of the solve, for the PAPI build (step 7.6j).
//
// Two things per PE, both from one PAPI event set that runs only between
// start() and stop() -- the same window the Projections traces cover:
//
//  - Counters. Cycles, instructions, branch mispredictions, L2 data-cache
//    misses and cycles spent in the integer divider, totalled for the solve.
//  - Samples. Every ACIC_PROF_PERIOD cycles PAPI interrupts the PE and the
//    handler records the interrupted address in a per-thread table. Addresses
//    are symbolized offline, with inline chains, by
//    benchmarks/papi_profile_report.py; that is what splits process_heap()
//    into the pop, the edge read and the insert, which one entry method in a
//    trace cannot.
//
// Output goes to $ACIC_PROF_DIR: pe<N>.txt per PE, and maps.<N>.txt (a copy of
// /proc/self/maps) from the first PE of each process, so addresses in shared
// libraries can be resolved too. Without ACIC_PROF_DIR the counters are still
// counted and reported through the instruction stat, and nothing is written.
//
// Anvil's papi/6.0.0.1 bundles a libpfm4 that predates Zen 3 and disables its
// perf_event component; run with LIBPFM_FORCE_PMU=amd64_fam17h_zen2. The five
// events below have the same codes on Zen 2 and Zen 3.
//
// ACIC_PROF_EVENTS=K counts only the first K events, and ACIC_PROF_OFF=1 skips
// PAPI altogether; both exist to find out what the profile itself costs. Each
// PE also reports its context switches over the window (RUSAGE_THREAD), since
// per-thread counters make every switch dearer.
//
// That cost is not small here: on two Anvil nodes any per-thread counter at
// all multiplied voluntary switches by ten and the solve time by two to three.
// ACIC_PROF_TIMER=MICROSECONDS therefore samples without the PMU: a per-thread
// POSIX timer on the thread's own CPU clock sends it a signal, and the handler
// records the interrupted address exactly as the overflow handler does. No
// counters are read in this mode, so nothing is added to a context switch.

#include <papi.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

namespace acic_prof {

static const char *const kEvents[] = {
    "PAPI_TOT_CYC",  // must stay first: it is the sampling event
    "PAPI_TOT_INS",
    "PAPI_BR_MSP",
    "amd64_fam17h_zen2::CORE_TO_L2_CACHEABLE_REQUEST_ACCESS_STATUS:LS_RD_BLK_C",
    "amd64_fam17h_zen2::DIV_CYCLES_BUSY_COUNT",
};
static const char *const kEventLabels[] = {
    "cycles", "instructions", "branch_misses", "l2_data_misses", "div_cycles",
};
static const int kMaxEvents = sizeof(kEvents) / sizeof(kEvents[0]);

// Open addressing, written only from the signal handler of the owning thread
// and read only after the event set has stopped, so it needs no locking.
static const int kTableBits = 17;
static const uint64_t kTableSize = 1ull << kTableBits;

struct Thread {
  int eventset = PAPI_NULL;
  int nevents = 0;
  int event_index[kMaxEvents];  // position in the set, -1 if it would not add
  long long values[kMaxEvents];
  uint64_t keys[kTableSize];
  uint64_t counts[kTableSize];
  uint64_t samples = 0;
  uint64_t dropped = 0;
  long long period = 0;
  struct rusage ru0;
  bool timer_armed = false;
  timer_t timer;
};

static thread_local Thread *self = nullptr;

inline void record(Thread *t, uint64_t a) {
  t->samples++;
  uint64_t h = (a * 0x9E3779B97F4A7C15ull) >> (64 - kTableBits);
  for (uint64_t probe = 0; probe < 64; probe++) {
    uint64_t s = (h + probe) & (kTableSize - 1);
    if (t->keys[s] == a) {
      t->counts[s]++;
      return;
    }
    if (t->keys[s] == 0) {
      t->keys[s] = a;
      t->counts[s] = 1;
      return;
    }
  }
  t->dropped++;
}

inline void on_overflow(int, void *address, long long, void *) {
  if (Thread *t = self)
    record(t, (uint64_t)address);
}

inline void on_timer(int, siginfo_t *, void *context) {
  if (Thread *t = self)
    record(t, (uint64_t)((ucontext_t *)context)->uc_mcontext.gregs[REG_RIP]);
}

// Arm a timer on this thread's CPU clock that signals this thread.
inline bool start_timer(Thread *t, long usec) {
  static std::once_flag once;
  std::call_once(once, [] {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_timer;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN + 3, &sa, nullptr);
  });
  struct sigevent sev;
  memset(&sev, 0, sizeof sev);
  sev.sigev_notify = SIGEV_THREAD_ID;
  sev.sigev_signo = SIGRTMIN + 3;
  sev._sigev_un._tid = (pid_t)syscall(SYS_gettid);
  if (timer_create(CLOCK_THREAD_CPUTIME_ID, &sev, &t->timer) != 0)
    return false;
  struct itimerspec its;
  its.it_interval.tv_sec = usec / 1000000;
  its.it_interval.tv_nsec = (usec % 1000000) * 1000;
  its.it_value = its.it_interval;
  if (timer_settime(t->timer, 0, &its, nullptr) != 0) {
    timer_delete(t->timer);
    return false;
  }
  t->timer_armed = true;
  return true;
}

inline unsigned long thread_id() { return (unsigned long)pthread_self(); }

inline void init_library() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT) {
      fprintf(stderr, "acic_prof: PAPI_library_init failed\n");
      abort();
    }
    if (PAPI_thread_init(thread_id) != PAPI_OK) {
      fprintf(stderr, "acic_prof: PAPI_thread_init failed\n");
      abort();
    }
  });
}

// Called on the PE's own thread when compute starts.
inline void dump_maps(int pe, const char *dir) {
  std::ifstream in("/proc/self/maps");
  std::ofstream out(std::string(dir) + "/maps." + std::to_string(pe) + ".txt");
  out << in.rdbuf();
}

inline void start(int pe, bool first_in_process) {
  const char *timer = getenv("ACIC_PROF_TIMER");
  if (getenv("ACIC_PROF_OFF") || timer) {
    Thread *t = (Thread *)calloc(1, sizeof(Thread));
    t->eventset = PAPI_NULL;
    for (int i = 0; i < kMaxEvents; i++)
      t->event_index[i] = -1;
    const char *dir = getenv("ACIC_PROF_DIR");
    if (dir && timer && first_in_process)
      dump_maps(pe, dir);
    getrusage(RUSAGE_THREAD, &t->ru0);
    self = t;
    if (timer) {
      // The report reads `period` as cycles per sample; here it is
      // microseconds of this thread's CPU time, negated to tell them apart.
      t->period = -atol(timer);
      if (!start_timer(t, atol(timer)))
        fprintf(stderr, "acic_prof pe %d: timer_create failed\n", pe);
    }
    return;
  }
  init_library();
  PAPI_register_thread();
  Thread *t = (Thread *)calloc(1, sizeof(Thread));
  t->eventset = PAPI_NULL;
  int r = PAPI_create_eventset(&t->eventset);
  if (r != PAPI_OK) {
    fprintf(stderr, "acic_prof pe %d: create_eventset: %s\n", pe,
            PAPI_strerror(r));
    return;
  }
  const char *limit = getenv("ACIC_PROF_EVENTS");
  const int nwanted = limit ? atoi(limit) : kMaxEvents;
  for (int i = 0; i < kMaxEvents; i++) {
    t->event_index[i] = -1;
    if (i >= nwanted)
      continue;
    r = PAPI_add_named_event(t->eventset, kEvents[i]);
    if (r == PAPI_OK)
      t->event_index[i] = t->nevents++;
    else if (first_in_process)
      fprintf(stderr, "acic_prof pe %d: cannot add %s: %s\n", pe, kEvents[i],
              PAPI_strerror(r));
  }
  const char *dir = getenv("ACIC_PROF_DIR");
  const char *period = getenv("ACIC_PROF_PERIOD");
  t->period = period ? atoll(period) : 1000003;
  if (dir && t->period > 0 && t->event_index[0] == 0) {
    int code;
    PAPI_event_name_to_code((char *)kEvents[0], &code);
    r = PAPI_overflow(t->eventset, code, (int)t->period, 0, on_overflow);
    if (r != PAPI_OK) {
      fprintf(stderr, "acic_prof pe %d: overflow: %s\n", pe, PAPI_strerror(r));
      t->period = 0;
    }
  } else {
    t->period = 0;
  }
  if (dir && first_in_process)
    dump_maps(pe, dir);
  self = t;
  getrusage(RUSAGE_THREAD, &t->ru0);
  r = PAPI_start(t->eventset);
  if (r != PAPI_OK)
    fprintf(stderr, "acic_prof pe %d: start: %s\n", pe, PAPI_strerror(r));
}

// Called on the PE's own thread when compute ends. Returns instructions, or 0.
inline long long stop(int pe) {
  Thread *t = self;
  if (!t)
    return 0;
  long long raw[kMaxEvents] = {0};
  if (t->nevents > 0) {
    int r = PAPI_stop(t->eventset, raw);
    if (r != PAPI_OK)
      fprintf(stderr, "acic_prof pe %d: stop: %s\n", pe, PAPI_strerror(r));
  }
  if (t->timer_armed)
    timer_delete(t->timer);
  self = nullptr;
  struct rusage ru1;
  getrusage(RUSAGE_THREAD, &ru1);
  for (int i = 0; i < kMaxEvents; i++)
    t->values[i] = t->event_index[i] >= 0 ? raw[t->event_index[i]] : -1;
  const char *dir = getenv("ACIC_PROF_DIR");
  if (dir) {
    std::string path = std::string(dir) + "/pe" + std::to_string(pe) + ".txt";
    FILE *f = fopen(path.c_str(), "w");
    if (f) {
      fprintf(f, "pe %d\nperiod %lld\nsamples %llu\ndropped %llu\n", pe,
              t->period, (unsigned long long)t->samples,
              (unsigned long long)t->dropped);
      for (int i = 0; i < kMaxEvents; i++)
        fprintf(f, "counter %s %lld\n", kEventLabels[i], t->values[i]);
      fprintf(f, "counter voluntary_switches %ld\n",
              ru1.ru_nvcsw - t->ru0.ru_nvcsw);
      fprintf(f, "counter involuntary_switches %ld\n",
              ru1.ru_nivcsw - t->ru0.ru_nivcsw);
      for (uint64_t s = 0; s < kTableSize; s++)
        if (t->keys[s])
          fprintf(f, "S %llx %llu\n", (unsigned long long)t->keys[s],
                  (unsigned long long)t->counts[s]);
      fclose(f);
    }
  }
  return t->values[1] > 0 ? t->values[1] : 0;
}

}  // namespace acic_prof
