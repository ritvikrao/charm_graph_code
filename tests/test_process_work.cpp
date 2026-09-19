#include "process_work.h"
#include <cassert>
#include <functional>
#include <thread>

int main() {
  ProcessWork<int, std::greater<int>> q(4);
  q.push(0, 100);
  q.push(1, 2);
  int value;
  assert(q.pop(0, [](int v) { return v < 10; }, value) && value == 2);
  assert(!q.pop(0, [](int v) { return v < 10; }, value));
  assert(q.pop(3, [](int) { return true; }, value) && value == 100);
  assert(q.empty());

  constexpr int workers = 8, per_worker = 10000;
  ProcessWork<int, std::greater<int>> concurrent(workers);
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
