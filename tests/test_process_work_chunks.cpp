#define ACIC_PROCESS_CHUNKS
#include "process_work.h"
#include <cassert>
#include <functional>
#include <thread>
#include <climits>

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
int main(){admission_and_reset();overflow_priority();for(int n:{1,8,32,64})concurrent_drain(n);}
