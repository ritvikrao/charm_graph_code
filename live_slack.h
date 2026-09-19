#pragma once
#include <algorithm>
#include <cmath>

// Controller-only state. The slack is in ORIGINAL bucket widths so merging
// histogram bins cannot silently multiply it. Updates are damped and bounded;
// overflow/window-empty progress rules remain the solver's responsibility.
class LiveSlack {
  double ratio_ema = -1.0;
  int cooldown = 0;
public:
  double widths = 8.0;
  double idle_fraction = 1.0;
  double changes_per_retired = 0.0;
  int action = 0; // -1 tighten, +1 loosen, 0 hold

  void observe(long changes, long retired, long active, long pes) {
    action = 0;
    idle_fraction = 1.0 - std::clamp((double)active / std::max(1L, pes), 0.0, 1.0);
    if (retired <= 0 || changes < 0) return; // no trustworthy work sample
    changes_per_retired = (double)changes / retired;
    if (ratio_ema < 0.0) { ratio_ema = changes_per_retired; return; }
    const bool rising = changes_per_retired > std::max(0.02, ratio_ema * 1.15);
    ratio_ema = 0.75 * ratio_ema + 0.25 * changes_per_retired;
    if (cooldown > 0) { --cooldown; return; }
    if (retired < 64) return; // do not tune from the startup/tail's handful
    if (rising) {
      widths = std::max(1.0, widths * 0.5);
      action = -1;
    } else if (idle_fraction > 0.5) {
      widths = std::min(256.0, widths * 1.5);
      action = 1;
    }
    if (action) cooldown = 1; // let the preceding broadcast take effect
  }

  int threshold(int frontier, int scale, int top) const {
    return std::min(top, frontier + std::max(0, (int)std::floor(widths / std::max(1, scale))));
  }
};
