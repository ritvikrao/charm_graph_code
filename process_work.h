#pragma once
#include "work_cost.h"
#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include <queue>
#include <vector>

// One producer bin per worker, with bounded batches stolen by peer workers.
// Keys remain original bucket indices, so coarsening never mutates another
// worker's queue. The caller checks admission against its current generation.
template <class Item, class Compare> class ProcessWork {
  struct Bin {
    std::mutex lock;
    std::map<long, std::priority_queue<Item, std::vector<Item>, Compare>> buckets;
    std::atomic<bool> nonempty{false};
  };
  int count;
  std::unique_ptr<Bin[]> bins;
public:
  explicit ProcessWork(int n) : count(n), bins(new Bin[n]) {}
  void push(int rank, const Item &item, long original_bucket = 0) {
    COST_SCOPE(PUSH_CALLS);
    Bin &bin = bins[rank];
    std::lock_guard<std::mutex> guard(bin.lock);
    bin.buckets[original_bucket].push(item);
    bin.nonempty.store(true, std::memory_order_release);
  }
  template <class Admitted>
  bool pop(int rank, Admitted admitted, Item &item) {
    COST_SCOPE(POP_CALLS);
    for (int k = 0; k < count; ++k) {
      COST_ADD(QUEUE_PROBES, 1);
      Bin &bin = bins[(rank + k) % count];
      if (!bin.nonempty.load(std::memory_order_acquire)) continue;
      std::unique_lock<std::mutex> guard(bin.lock, std::try_to_lock);
      if (!guard) COST_ADD(LOCK_MISSES, 1);
      if (!guard || bin.buckets.empty()) continue;
      auto first = bin.buckets.begin();
      if (!admitted(first->second.top())) continue;
      item = first->second.top();
      first->second.pop();
      if (first->second.empty()) bin.buckets.erase(first);
      bin.nonempty.store(!bin.buckets.empty(), std::memory_order_release);
      return true;
    }
    return false;
  }
  bool empty() const {
    for (int i = 0; i < count; ++i)
      if (bins[i].nonempty.load(std::memory_order_acquire)) return false;
    return true;
  }
};
