#pragma once
#include <algorithm>
#include <array>
#include <deque>

#ifndef ACIC_CHUNK_DISTANCE_WIDTH
#define ACIC_CHUNK_DISTANCE_WIDTH 4096
#endif

// Experimental producer-private FIFO chunks within bounded distance bands.
// Only worker r touches private_[r]; peers steal published chunks under its
// bin mutex. Transfers never retire a histogram charge. Partial chunks stay
// available to their owner, including after it receives a new admission epoch.
// ACIC_CHUNK_PARTIAL enables owner-serviced requests for short chunks; peers
// never read or lock private storage. It is a one-node road experiment.
// FIFO order within a bucket is intentional; this is not an exact min-heap.
template <class Item, class Compare, class Key = ProcessWorkIntegerKey> class ProcessWork {
  static constexpr int chunk_size = 64;
  // The original overflow bucket can span the whole remaining distance
  // range. Bound FIFO disorder there as well as in ordinary buckets.
  static constexpr long distance_width = ACIC_CHUNK_DISTANCE_WIDTH;
  static_assert(distance_width > 0, "chunk distance width must be positive");
  using Bucket = std::pair<long, long>; // original admission bucket, distance band
  struct Chunk {
    std::array<Item, chunk_size> items;
#ifdef ACIC_CHUNK_PARTIAL
    int used = chunk_size;
    int size() const { return used; }
#else
    int size() const { return chunk_size; }
#endif
  };
  struct alignas(64) Private {
    std::map<Bucket, std::deque<Item>> buckets;
    std::deque<Item> *cached = nullptr;
    Bucket cached_key{0, 0};
    size_t count = 0;
    std::atomic<bool> nonempty{false};
  };
  struct alignas(64) Bin {
    std::mutex lock;
    std::map<Bucket, std::deque<Chunk>> buckets;
    std::atomic<bool> nonempty{false};
    std::atomic<long> hint{std::numeric_limits<long>::max()};
#ifdef ACIC_CHUNK_PARTIAL
    // A hint only: delayed/lost requests affect sharing, never liveness.
    std::atomic<bool> requested{false};
#endif
  };
  int count;
  bool nearest;
  std::unique_ptr<Private[]> private_;
  std::unique_ptr<Bin[]> bins;

  void publish_hint(Bin &b) {
    b.hint.store(b.buckets.empty() ? std::numeric_limits<long>::max()
        : Key{}(b.buckets.begin()->second.front().items[0]), std::memory_order_release);
    b.nonempty.store(!b.buckets.empty(), std::memory_order_release);
  }
#ifdef ACIC_CHUNK_PARTIAL
  template <class Admitted>
  void share_partial(int rank, Admitted &admitted) {
    Bin &b = bins[rank];
    if (!b.requested.load(std::memory_order_relaxed) ||
        !b.requested.exchange(false, std::memory_order_relaxed)) return;
    COST_ADD(CHUNK_SHARE_CHECKS, 1);
    Private &p = private_[rank];
    if (p.buckets.empty() || b.nonempty.load(std::memory_order_acquire)) return;
    auto first = p.buckets.begin();
    auto &q = first->second;
    // Retain local work and bound each donation to half a chunk. Service at
    // most the earliest bucket, without scanning the whole private map.
    const int limit = std::min<size_t>(chunk_size / 2, q.size() / 2);
    if (!limit) return;
    Chunk chunk;
    chunk.used = 0;
    for (auto it = q.begin(); it != q.end() && chunk.used < limit;) {
      if (admitted(*it)) { chunk.items[chunk.used++] = *it; it = q.erase(it); }
      else ++it;
    }
    if (!chunk.used) return;
    // Private nonempty remains true until published nonempty is visible.
    std::lock_guard<std::mutex> guard(b.lock);
    b.buckets[first->first].push_back(chunk);
    publish_hint(b);
    COST_ADD(CHUNK_PUBLICATIONS, 1);
    COST_ADD(CHUNK_PUBLISHED_ITEMS, chunk.used);
    COST_ADD(CHUNK_PARTIAL_PUBLICATIONS, 1);
    p.count -= chunk.used;
    if (q.empty()) { p.buckets.erase(first); p.cached = nullptr; }
    if (!p.count) p.nonempty.store(false, std::memory_order_release);
  }

  void request_work(int rank) {
    for (int k = 1; k < count; ++k) {
      const int peer = (rank + k) % count;
      Bin &b = bins[peer];
      if (private_[peer].nonempty.load(std::memory_order_acquire) &&
          !b.nonempty.load(std::memory_order_acquire)) {
        bool expected = false;
        if (b.requested.compare_exchange_strong(expected, true, std::memory_order_relaxed))
          COST_ADD(CHUNK_SHARE_REQUESTS, 1);
      }
    }
  }
#endif
  template <class Admitted>
  int pop_private(int rank, Admitted &admitted, Item *items, int capacity) {
    Private &p = private_[rank];
    int taken = 0;
    while (taken < capacity && !p.buckets.empty()) {
      auto first = p.buckets.begin();
      auto &q = first->second;
      // A changed range/clamp can split an old overflow bucket. FIFO must
      // not hide an eligible item behind an ineligible one in that bucket.
      auto eligible = q.begin();
      if (!admitted(*eligible))
        eligible = std::find_if(std::next(eligible), q.end(), admitted);
      if (eligible == q.end()) break;
      items[taken++] = *eligible;
      q.erase(eligible);
      --p.count;
      if (q.empty()) { p.cached = nullptr; p.buckets.erase(first); }
    }
    if (p.count == 0) p.nonempty.store(false, std::memory_order_release);
    return taken;
  }
  template <class Admitted>
  bool take_chunk(int rank, int peer, Admitted &admitted) {
    COST_ADD(QUEUE_PROBES, 1);
    Bin &b = bins[peer];
    if (!b.nonempty.load(std::memory_order_acquire)) return false;
    std::unique_lock<std::mutex> guard(b.lock, std::try_to_lock);
    if (!guard) COST_ADD(LOCK_MISSES, 1);
    if (!guard || b.buckets.empty()) return false;
    auto first = b.buckets.begin();
    auto &chunks = first->second;
    auto eligible = chunks.begin();
    if (!admitted(eligible->items[0])) {
      eligible = std::find_if(chunks.begin(), chunks.end(), [&](const Chunk &c) {
        return std::any_of(c.items.begin(), c.items.begin() + c.size(), admitted);
      });
    }
    if (eligible == chunks.end()) return false;
    Private &p = private_[rank];
    auto &q = p.buckets[first->first];
    q.insert(q.end(), eligible->items.begin(), eligible->items.begin() + eligible->size());
    p.count += eligible->size();
    COST_ADD(CHUNK_TAKES, 1);
    COST_ADD(CHUNK_TAKEN_ITEMS, eligible->size());
    if (rank != peer) COST_ADD(CHUNK_PEER_TAKEN_ITEMS, eligible->size());
    p.nonempty.store(true, std::memory_order_release);
    first->second.erase(eligible);
    if (first->second.empty()) b.buckets.erase(first);
    publish_hint(b);
    return true;
  }
public:
  explicit ProcessWork(int n, bool priority = false)
      : count(n), nearest(priority), private_(new Private[n]), bins(new Bin[n]) {}
  void push(int rank, const Item &item, long original_bucket = 0) {
    COST_SCOPE(PUSH_CALLS);
    Private &p = private_[rank];
    const Bucket bucket{original_bucket, Key{}(item) / distance_width};
    if (!p.cached || p.cached_key != bucket) {
      p.cached_key = bucket;
      p.cached = &p.buckets[bucket];
    }
    auto &q = *p.cached;
    q.push_back(item);
    if (++p.count == 1) p.nonempty.store(true, std::memory_order_release);
    if (q.size() >= chunk_size) {
      Bin &b = bins[rank];
      std::lock_guard<std::mutex> guard(b.lock);
      auto &chunks = b.buckets[bucket];
      chunks.emplace_back();
      for (int i = 0; i < chunk_size; ++i) {
        chunks.back().items[i] = q.front();
        q.pop_front();
      }
      publish_hint(b);
      COST_ADD(CHUNK_PUBLICATIONS, 1);
      COST_ADD(CHUNK_PUBLISHED_ITEMS, chunk_size);
      p.count -= chunk_size;
      if (q.empty()) { p.buckets.erase(bucket); p.cached = nullptr; }
      if (p.count == 0) p.nonempty.store(false, std::memory_order_release);
    }
  }
  template <class Admitted>
  int pop_batch(int rank, Admitted admitted, Item *items, int capacity) {
    COST_SCOPE(POP_CALLS);
    if (capacity <= 0) return 0;
#ifdef ACIC_WORK_COST
    if ((work_cost::counts[work_cost::POP_CALLS] & 1023) == 1) {
      COST_ADD(CHUNK_PRIVATE_SAMPLES, 1);
      COST_ADD(CHUNK_PRIVATE_ITEMS, private_[rank].count);
      COST_ADD(CHUNK_PRIVATE_BANDS, private_[rank].buckets.size());
    }
#endif
#ifdef ACIC_CHUNK_PARTIAL
    share_partial(rank, admitted);
#endif
    int best = -1;
    long best_distance = std::numeric_limits<long>::max();
    if (nearest) {
      for (int k = 0; k < count; ++k) {
        const int peer = (rank + k) % count;
        long hint = bins[peer].hint.load(std::memory_order_acquire);
        if (hint < best_distance) { best_distance = hint; best = peer; }
      }
    }
    // Do not run a later private band while earlier published work waits.
    Private &p = private_[rank];
    if (best >= 0 && (p.buckets.empty() ||
        best_distance / distance_width < p.buckets.begin()->first.second)) {
      if (take_chunk(rank, best, admitted)) {
        const int taken = pop_private(rank, admitted, items, capacity);
        if (taken) return taken;
      }
    }
    const int taken = pop_private(rank, admitted, items, capacity);
    if (taken) return taken;
    if (best >= 0 && take_chunk(rank, best, admitted))
      return pop_private(rank, admitted, items, capacity);
    for (int k = 0; k < count; ++k) {
      const int peer = (rank + k) % count;
      if (peer != best && take_chunk(rank, peer, admitted))
        return pop_private(rank, admitted, items, capacity);
    }
#ifdef ACIC_CHUNK_PARTIAL
    request_work(rank);
#endif
    COST_ADD(CHUNK_FAILED_SEARCHES, 1);
    return 0;
  }
  template <class Admitted> bool pop(int rank, Admitted admitted, Item &item) {
    return pop_batch(rank, admitted, &item, 1) != 0;
  }
  bool empty() const {
    for (int i = 0; i < count; ++i)
      if (private_[i].nonempty.load(std::memory_order_acquire) ||
          bins[i].nonempty.load(std::memory_order_acquire)) return false;
    return true;
  }
};
