#include "live_slack.h"
#include <cassert>

int main() {
  LiveSlack slack;
  slack.observe(10, 100, 10, 10); // establish baseline
  slack.observe(30, 100, 1, 10);  // rising re-work outranks starvation
  assert(slack.widths == 4 && slack.action == -1);
  slack.observe(10, 100, 1, 10); // cooldown
  assert(slack.widths == 4);
  slack.observe(10, 100, 1, 10);
  assert(slack.widths == 6 && slack.action == 1);
  const double before = slack.widths;
  slack.observe(0, 0, 0, 10);
  assert(slack.widths == before && slack.action == 0);
  assert(slack.threshold(20, 2, 2047) == 23);
  assert(slack.threshold(2046, 1, 2047) == 2047);
  for (int i = 0; i < 1000; ++i) slack.observe(10, 100, 0, 10);
  assert(slack.widths == 256);
  assert(slack.threshold(0, 1024, 2047) == 0);
}
