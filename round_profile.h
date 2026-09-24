#pragma once

// Diagnostic build only. Durations use each PE's own clock; no timestamps
// from different processes are subtracted. Nested scopes are inclusive, so
// controller_log is part of controller, and the threshold phases are part of
// thresholds. Output happens after Main has stopped the solve timer.
#ifdef ACIC_ROUND_PROFILE
#include <algorithm>
#include <sstream>

namespace round_profile {
enum Metric {
  controller, controller_log, broadcast_call, thresholds, threshold_setup,
  hints, tram_threshold, tram_flush, queue_dispatch, histogram_prepare,
  contribute_call, clear_hold, COUNT
};
static const char *const names[] = {
  "controller", "controller_log", "broadcast_call", "thresholds",
  "threshold_setup", "hints", "tram_threshold", "tram_flush",
  "queue_dispatch", "histogram_prepare", "contribute_call", "clear_hold"
};
struct Counter {
  unsigned long calls = 0;
  double seconds = 0, maximum = 0;
};
static thread_local Counter counters[COUNT];
static thread_local bool running = false;
inline void start() {
  for (auto &c : counters) c = Counter{};
  running = true;
}
struct Scope {
  Metric metric;
  double start;
  bool active;
  explicit Scope(Metric m) : metric(m), start(running ? CkWallTimer() : 0), active(running) {}
  ~Scope() {
    if (!active) return;
    const double elapsed = CkWallTimer() - start;
    Counter &c = counters[metric];
    ++c.calls;
    c.seconds += elapsed;
    c.maximum = std::max(c.maximum, elapsed);
  }
};
inline void dump() {
  running = false;
  std::ostringstream out;
  out.precision(12);
  out << "ROUND_PROFILE pe=" << CkMyPe();
  for (int i = 0; i < COUNT; ++i) {
    const Counter &c = counters[i];
    out << ' ' << names[i] << '=' << c.calls << ',' << c.seconds << ',' << c.maximum;
  }
  CkPrintf("%s\n", out.str().c_str());
}
} // namespace round_profile
#define ROUND_SCOPE(name) round_profile::Scope round_scope_##name(round_profile::name)
#else
#define ROUND_SCOPE(name) ((void)0)
#endif
