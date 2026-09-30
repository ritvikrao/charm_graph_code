#define ACIC_PROCESS_BUCKETS
#include "process_work.h"
#include <cassert>
#include <functional>
#include <thread>
#include <climits>
#include <random>

// The chunk queue's tests at its default band (4096), plus the array's own
// cases: bands further apart than the array spans, which spill, and order
// across the array and the spill.
int process_band_shift = 12;

using Queue = ProcessWork<int, std::greater<int>>;
void admission_and_reset() {
  Queue q(3, true);
  int items[64];
  // Partial chunks are private, remain live, and are drainable by their owner.
  q.push(0, 100, LONG_MAX);
  q.push(0, 2, LONG_MAX);
  assert(!q.empty());
  assert(q.pop_batch(1, [](int){return true;}, items, 8)==0);
  assert(q.pop_batch(0, [](int v){return v<10;}, items, 8)==1 && items[0]==2);
  assert(!q.empty());
  assert(q.pop_batch(0, [](int){return true;}, items, 8)==1 && items[0]==100);
  assert(q.empty());
  // Ineligible first chunks cannot hide eligible work in the same old bucket.
  for (int i=0;i<64;++i) q.push(0, 100+i, LONG_MAX);
  for (int i=0;i<64;++i) q.push(0, 63-i, LONG_MAX);
  int n=0;
  for (int k; (k=q.pop_batch(1, [](int v){return v<32;}, items, 8));)
    for(int j=0;j<k;++j) {assert(items[j]<32);++n;}
  assert(n==32 && !q.empty());
  for(int r=0;r<3;++r)
    while(int k=q.pop_batch(r, [](int){return true;}, items, 64)) n+=k;
  assert(n==128 && q.empty());
  for(int source=0;source<4;++source) {
    q.push(2, source);
    assert(q.pop_batch(2, [](int){return true;}, items, 1)==1 && items[0]==source);
    assert(q.empty());
  }
}
void overflow_priority() {
  Queue q(2,true);
  int items[64];
  for(int i=0;i<64;++i)q.push(0,i,LONG_MAX);
  q.push(0,1000000,LONG_MAX);
  assert(q.pop_batch(0,[](int){return true;},items,8)==8);
  for(int i=0;i<8;++i)assert(items[i]<64);
  int n=8;
  for(int rank=0;rank<2;++rank)while(int k=q.pop_batch(rank,[](int){return true;},items,64))n+=k;
  assert(n==65 && q.empty());
}
void concurrent_drain(int capacity) {
  constexpr int workers=8, each=10003;
  Queue q(workers,true);
  std::vector<std::atomic<int>> seen(workers*each);
  for(auto &x:seen)x=0;
  std::atomic<int> consumed{0};
  std::vector<std::thread> threads;
  for(int rank=0;rank<workers;++rank)threads.emplace_back([&,rank]{
    int items[64];
    auto drain=[&]{
      int n=q.pop_batch(rank,[](int){return true;},items,capacity);
      for(int i=0;i<n;++i){assert(items[i]>=0 && items[i]<workers*each);++seen[items[i]];++consumed;}
      return n;
    };
    for(int i=0;i<each;++i){q.push(rank,rank*each+i,i%19);if(i%97==0)drain();}
    while(consumed.load()!=workers*each)if(!drain())std::this_thread::yield();
  });
  for(auto &t:threads)t.join();
  for(auto &v:seen)assert(v==1);
  assert(q.empty());
}
void spill_order() {
  Queue q(1, false);
  int items[64];
  process_band_shift = 0; // one distance unit per band
  // 0, then bands far past the array's span (1 << 14), then one below 0's
  // window after the array has moved: every pop must still be nondecreasing.
  std::vector<int> values;
  for (int v : {5, 70000, 3, 20000, 1 << 20, 17000, 4, 9, 40000, 2})
    values.push_back(v);
  for (int v : values) q.push(0, v, 0);
  std::vector<int> got;
  while (int k = q.pop_batch(0, [](int){return true;}, items, 3))
    for (int i = 0; i < k; ++i) got.push_back(items[i]);
  assert(got.size() == values.size());
  for (size_t i = 1; i < got.size(); ++i) assert(got[i - 1] <= got[i]);
  assert(q.empty());
  // Interleaved pushes and pops over a moving window, single owner: output
  // is nondecreasing whenever nothing lower than the last pop is pushed.
  std::mt19937 rng(1);
  int floor = 0, n = 0;
  for (int round = 0; round < 20000; ++round) {
    for (int j = 0; j < 3; ++j) { q.push(0, floor + (int)(rng() % 50000), 0); ++n; }
    int k = q.pop_batch(0, [](int){return true;}, items, 2);
    for (int i = 0; i < k; ++i) { assert(items[i] >= floor); floor = items[i]; --n; }
  }
  while (int k = q.pop_batch(0, [](int){return true;}, items, 64))
    for (int i = 0; i < k; ++i) { assert(items[i] >= floor); floor = items[i]; --n; }
  assert(n == 0 && q.empty());
  process_band_shift = 12;
}
void admission_stops_at_band() {
  Queue q(2, true);
  int items[64];
  process_band_shift = 4;
  for (int v = 0; v < 400; ++v) q.push(0, 399 - v, 0);
  int n = 0, top = -1;
  for (int r = 0; r < 2; ++r)
    while (int k = q.pop_batch(r, [](int v){return v < 150;}, items, 8))
      for (int i = 0; i < k; ++i) { assert(items[i] < 150); top = std::max(top, items[i]); ++n; }
  assert(n == 150 && !q.empty());
  for (int r = 0; r < 2; ++r)
    while (int k = q.pop_batch(r, [](int){return true;}, items, 8)) n += k;
  assert(n == 400 && q.empty());
  process_band_shift = 12;
}
int main(){
  admission_and_reset(); overflow_priority();
  for (int n : {1, 8, 32, 64}) concurrent_drain(n);
  spill_order(); admission_stops_at_band();
  for (int shift : {0, 4, 8}) { process_band_shift = shift; for (int n : {1, 8}) concurrent_drain(n); }
  return 0;
}
