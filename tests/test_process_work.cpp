#include "process_work.h"
#include <cassert>
#include <functional>
#include <thread>

void admission(bool nearest) {
  ProcessWork<int, std::greater<int>> q(4, nearest);
  q.push(0, 100);
  q.push(1, 2);
  int value;
  assert(q.pop(0, [](int v) { return v < 10; }, value) && value == 2);
  assert(!q.pop(0, [](int v) { return v < 10; }, value));
  assert(q.pop(3, [](int) { return true; }, value) && value == 100);
  assert(q.empty());
  // Reusing the drained queue must not leave stale hints from another source.
  q.push(2, 8);
  assert(q.pop(0, [](int) { return true; }, value) && value == 8);
  assert(q.empty());
}

void batch_admission(bool nearest, int capacity) {
  ProcessWork<int, std::greater<int>> q(4, nearest);
  int items[64];
  for (int i = 0; i < 70; ++i) q.push(0, i, i / 10);
  int next = 0;
  while (int taken = q.pop_batch(2, [](int v) { return v < 35; }, items, capacity)) {
    assert(taken <= capacity);
    for (int i = 0; i < taken; ++i) assert(items[i] == next++);
  }
  assert(next == 35 && !q.empty());
  // An ineligible head must not prevent trying another producer bin.
  q.push(1, 2);
  assert(q.pop_batch(0, [](int v) { return v < 35; }, items, capacity) == 1);
  assert(items[0] == 2);
  while (int taken = q.pop_batch(2, [](int) { return true; }, items, capacity))
    for (int i = 0; i < taken; ++i) assert(items[i] == next++);
  assert(next == 70 && q.empty());
  q.push(3, 7);
  assert(q.pop_batch(0, [](int) { return true; }, items, capacity) == 1);
  assert(items[0] == 7 && q.empty());

  // Original buckets dominate distance ordering even inside a batch.
  q.push(0, 100, 0);
  q.push(0, 1, 1);
  assert(q.pop_batch(0, [](int v) { return v < 10; }, items, capacity) == 0);
  assert(q.pop_batch(0, [](int) { return true; }, items, 2) == 2);
  assert(items[0] == 100 && items[1] == 1 && q.empty());
}

void concurrent_drain(bool nearest, int capacity) {
  constexpr int workers = 8, per_worker = 10000;
  ProcessWork<int, std::greater<int>> concurrent(workers, nearest);
  std::vector<std::atomic<int>> seen(workers * per_worker);
  for (auto &n : seen) n = 0;
  std::atomic<int> consumed{0};
  std::vector<std::thread> threads;
  for (int rank = 0; rank < workers; ++rank) {
    threads.emplace_back([&, rank] {
      int items[64];
      const auto drain = [&] {
        const int taken = concurrent.pop_batch(rank, [](int) { return true; }, items, capacity);
        for (int j = 0; j < taken; ++j) {
          assert(items[j] >= 0 && items[j] < workers * per_worker);
          seen[items[j]]++;
          consumed++;
        }
        return taken;
      };
      for (int i = 0; i < per_worker; ++i) {
        concurrent.push(rank, rank * per_worker + i);
        if (i % capacity == capacity - 1) drain();
      }
      while (consumed.load() < workers * per_worker) {
        if (!drain()) std::this_thread::yield();
      }
    });
  }
  for (auto &thread : threads) thread.join();
  for (auto &n : seen) assert(n == 1);
  assert(concurrent.empty());
}

int main() {
  for (bool nearest : {false, true}) {
    admission(nearest);
    for (int capacity : {1, 8, 32, 64}) {
      batch_admission(nearest, capacity);
      concurrent_drain(nearest, capacity);
    }
    ProcessWork<int, std::greater<int>> q(3, nearest);
    q.push(0, 100);
    q.push(1, 2);
    int value;
    assert(q.pop(0, [](int) { return true; }, value));
    assert(value == (nearest ? 2 : 100));
  }
  ProcessWork<int, std::greater<int>> q(3, true);
  int value;
  q.push(0, 100, 0);
  q.push(0, 1, 1); // Original bucket order is preserved inside the bin.
  q.push(1, 2, 0);
  assert(q.pop(0, [](int) { return true; }, value) && value == 2);
  assert(q.pop(0, [](int) { return true; }, value) && value == 100);
  assert(q.pop(0, [](int) { return true; }, value) && value == 1);
  q.push(0, 100);
  q.push(1, 2);
  // A head that is ineligible for this worker must not block another bin.
  assert(q.pop(1, [](int v) { return v > 10; }, value) && value == 100);
  q.push(1, 1); // Publish an improvement to an existing bin's head.
  assert(q.pop(0, [](int) { return true; }, value) && value == 1);
  assert(q.pop(0, [](int) { return true; }, value) && value == 2);
  assert(q.empty());
}
