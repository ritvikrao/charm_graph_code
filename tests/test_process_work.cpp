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

void concurrent_drain(bool nearest) {
  constexpr int workers = 8, per_worker = 10000;
  ProcessWork<int, std::greater<int>> concurrent(workers, nearest);
  std::vector<std::atomic<int>> seen(workers * per_worker);
  for (auto &n : seen) n = 0;
  std::atomic<int> consumed{0};
  std::vector<std::thread> threads;
  for (int rank = 0; rank < workers; ++rank) {
    threads.emplace_back([&, rank] {
      for (int i = 0; i < per_worker; ++i) {
        concurrent.push(rank, rank * per_worker + i);
        int item;
        if (concurrent.pop(rank, [](int) { return true; }, item)) {
          seen[item]++;
          consumed++;
        }
      }
      while (consumed.load() < workers * per_worker) {
        int item;
        if (concurrent.pop(rank, [](int) { return true; }, item)) {
          seen[item]++;
          consumed++;
        } else std::this_thread::yield();
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
    concurrent_drain(nearest);
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
