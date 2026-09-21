#pragma once
#include "work_cost.h"
#include <atomic>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <queue>
#include <vector>

// One producer bin per worker, with bounded batches stolen by peer workers.
// Keys remain original bucket indices, so coarsening never mutates another
// worker's queue. The caller checks admission against its current generation.
struct ProcessWorkIntegerKey {
  template <class Item> long operator()(const Item &item) const { return (long)item; }
};

template <class Item, class Compare, class Key = ProcessWorkIntegerKey> class ProcessWork {
  struct Bin {
    std::mutex lock;
    std::map<long, std::priority_queue<Item, std::vector<Item>, Compare>> buckets;
    std::atomic<bool> nonempty{false};
  };
  int count;
  std::unique_ptr<Bin[]> bins;
  // Separate cache lines avoid unrelated producers invalidating each other's
  // hints. The default policy allocates no hints and retains the old scan.
  struct alignas(64) Hint {
    std::atomic<long> distance{std::numeric_limits<long>::max()};
  };
  std::unique_ptr<Hint[]> hints;

  void publish_head(int rank) {
    if (hints) {
      const auto &buckets = bins[rank].buckets;
      hints[rank].distance.store(buckets.empty() ? std::numeric_limits<long>::max()
          : Key{}(buckets.begin()->second.top()), std::memory_order_release);
    }
  }

  template <class Admitted>
  int pop_bin(int rank, Admitted &admitted, Item *items, int capacity) {
    COST_ADD(QUEUE_PROBES, 1);
    Bin &bin = bins[rank];
    if (!bin.nonempty.load(std::memory_order_acquire)) return 0;
    std::unique_lock<std::mutex> guard(bin.lock, std::try_to_lock);
    if (!guard) COST_ADD(LOCK_MISSES, 1);
    if (!guard || bin.buckets.empty()) return 0;
    int taken = 0;
    while (taken < capacity && !bin.buckets.empty()) {
      auto first = bin.buckets.begin();
      // Never skip an ineligible head to reach a later original bucket.
      if (!admitted(first->second.top())) break;
      items[taken++] = first->second.top();
      first->second.pop();
      if (first->second.empty()) bin.buckets.erase(first);
    }
    if (taken) {
      publish_head(rank);
      bin.nonempty.store(!bin.buckets.empty(), std::memory_order_release);
    }
    return taken;
  }
public:
  explicit ProcessWork(int n, bool nearest = false)
      : count(n), bins(new Bin[n]), hints(nearest ? new Hint[n] : nullptr) {}
  void push(int rank, const Item &item, long original_bucket = 0) {
    COST_SCOPE(PUSH_CALLS);
    Bin &bin = bins[rank];
    std::lock_guard<std::mutex> guard(bin.lock);
    bin.buckets[original_bucket].push(item);
    publish_head(rank);
    bin.nonempty.store(true, std::memory_order_release);
  }
  template <class Admitted>
  int pop_batch(int rank, Admitted admitted, Item *items, int capacity) {
    COST_SCOPE(POP_CALLS);
    if (capacity <= 0) return 0;
    int best = -1;
    if (hints) {
      long distance = std::numeric_limits<long>::max();
      for (int k = 0; k < count; ++k) {
        const int peer = (rank + k) % count;
        // Hints only choose which lock to try first. A concurrent change can
        // make them stale; the real head and admission are checked under lock.
        const long head = hints[peer].distance.load(std::memory_order_acquire);
        if (head < distance) {
          distance = head;
          best = peer;
        }
      }
      if (best >= 0) {
        const int taken = pop_bin(best, admitted, items, capacity);
        if (taken) return taken;
      }
    }
    // Contention, stale hints and generation-specific admission must not
    // strand eligible work in another bin. Keep the existing bounded scan.
    for (int k = 0; k < count; ++k) {
      const int peer = (rank + k) % count;
      if (peer != best) {
        const int taken = pop_bin(peer, admitted, items, capacity);
        if (taken) return taken;
      }
    }
    return 0;
  }
  template <class Admitted>
  bool pop(int rank, Admitted admitted, Item &item) {
    return pop_batch(rank, admitted, &item, 1) != 0;
  }
  bool empty() const {
    for (int i = 0; i < count; ++i)
      if (bins[i].nonempty.load(std::memory_order_acquire)) return false;
    return true;
  }
};
