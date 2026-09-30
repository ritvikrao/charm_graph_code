#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <utility>

// O1 (sc27-plan.md, Delta one node round 2): the chunk queue's producer-private
// FIFO buckets and published 64-item chunks, with each worker's buckets held in
// a circular array indexed by distance band instead of a std::map, as Wasp's
// buckets are. The band is a runtime power of two (--chunk-band), so one binary
// covers every family. Admission, histogram charges and termination are the
// chunk queue's: only storage and lookup change.
//
// A band lives in the array while every stored band is within kSlots of every
// other; anything further out, and the overflow bucket, goes to an ordered
// spill map that is compared against the array's lowest band on every access,
// so order never depends on which of the two holds an item.
extern int process_band_shift; // log2 of the band, in distance units

template <class T> struct BandFifo {
  std::vector<T> v;
  size_t head = 0;
  size_t size() const { return v.size() - head; }
  bool empty() const { return head == v.size(); }
  T &operator[](size_t i) { return v[head + i]; }
  void push_back(const T &x) { v.push_back(x); }
  void pop_front() {
    if (++head == v.size()) { v.clear(); head = 0; }
    else if (head >= 1024 && 2 * head >= v.size()) {
      v.erase(v.begin(), v.begin() + head);
      head = 0;
    }
  }
  void erase(size_t i) {
    if (i == 0) { pop_front(); return; }
    v.erase(v.begin() + head + i);
  }
};

// Keys are (0, band) for ordinary buckets and (1, band) for the overflow
// bucket, which sorts after all of them, as the chunk queue's original
// bucket LONG_MAX did.
template <class T> class BandQueue {
public:
  using Key = std::pair<long, long>;
private:
  static constexpr long kSlots = 1L << 14;
  static constexpr long kMask = kSlots - 1;
  std::unique_ptr<BandFifo<T>[]> slots{new BandFifo<T>[kSlots]};
  std::array<uint64_t, kSlots / 64> bits{};
  long lo = 0, hi = -1; // lowest and highest band stored in the array
  long used = 0;        // nonempty array slots
  std::map<Key, BandFifo<T>> spill;

  void set_bit(long band) { bits[(band & kMask) >> 6] |= 1ULL << (band & 63); }
  void clear_bit(long band) { bits[(band & kMask) >> 6] &= ~(1ULL << (band & 63)); }
  bool test_bit(long band) const { return bits[(band & kMask) >> 6] >> (band & 63) & 1; }
  // Whether band can take an array slot without two stored bands aliasing.
  bool admit(long band) {
    if (!used) { lo = hi = band; return true; }
    if (band < lo) { if (hi - band >= kSlots) return false; lo = band; return true; }
    if (band > hi) { if (band - lo >= kSlots) return false; hi = band; return true; }
    return true;
  }
  void advance_lo() {
    if (!used) { lo = 0; hi = -1; return; }
    long b = lo + 1;
    while (b <= hi) {
      const long idx = b & kMask;
      const uint64_t word = bits[idx >> 6] >> (idx & 63);
      if (word) { b += __builtin_ctzll(word); break; }
      b += 64 - (idx & 63);
    }
    lo = b;
  }
  bool array_first() const {
    return used && (spill.empty() || Key{0, lo} < spill.begin()->first);
  }
public:
  bool empty() const { return !used && spill.empty(); }
  // The container for key, created empty if absent. The caller adds to it
  // before the next call, so a slot marked nonempty is never left empty.
  BandFifo<T> &at(const Key &key) {
    if (key.first == 0) {
      if (used && key.second >= lo && key.second <= hi && test_bit(key.second))
        return slots[key.second & kMask];
      if (admit(key.second)) {
        set_bit(key.second);
        ++used;
        return slots[key.second & kMask];
      }
    }
    return spill[key];
  }
  Key front_key() const { return array_first() ? Key{0, lo} : spill.begin()->first; }
  BandFifo<T> &front() { return array_first() ? slots[lo & kMask] : spill.begin()->second; }
  // After the caller removes from front(): retire it if it emptied.
  void tidy_front() {
    if (array_first()) {
      if (slots[lo & kMask].empty()) { clear_bit(lo); --used; advance_lo(); }
    } else if (spill.begin()->second.empty()) {
      spill.erase(spill.begin());
    }
  }
  // After the caller removes from at(key): retire it if it emptied.
  void tidy(const Key &key) {
    if (key.first == 0 && used && key.second >= lo && key.second <= hi &&
        test_bit(key.second) && slots[key.second & kMask].empty()) {
      clear_bit(key.second);
      --used;
      if (key.second == lo) advance_lo();
      return;
    }
    auto it = spill.find(key);
    if (it != spill.end() && it->second.empty()) spill.erase(it);
  }
  size_t spilled() const { return spill.size(); }
};

template <class Item, class Compare, class Key = ProcessWorkIntegerKey> class ProcessWork {
  static constexpr int chunk_size = 64;
  using Band = typename BandQueue<Item>::Key;
  struct Chunk { std::array<Item, chunk_size> items; };
  struct alignas(64) Private {
    BandQueue<Item> buckets;
    size_t count = 0;
    std::atomic<bool> nonempty{false};
  };
  struct alignas(64) Bin {
    std::mutex lock;
    BandQueue<Chunk *> chunks;
    std::atomic<bool> nonempty{false};
    std::atomic<long> hint{std::numeric_limits<long>::max()};
  };
  int count;
  bool nearest;
  std::unique_ptr<Private[]> private_;
  std::unique_ptr<Bin[]> bins;

  static Band band_of(const Item &item, long original_bucket) {
    return {original_bucket == std::numeric_limits<long>::max() ? 1 : 0,
            Key{}(item) >> process_band_shift};
  }
  void publish_hint(Bin &b) {
    b.hint.store(b.chunks.empty() ? std::numeric_limits<long>::max()
        : Key{}(b.chunks.front()[0]->items[0]), std::memory_order_release);
    b.nonempty.store(!b.chunks.empty(), std::memory_order_release);
  }
  template <class Admitted>
  int pop_private(int rank, Admitted &admitted, Item *items, int capacity) {
    Private &p = private_[rank];
    int taken = 0;
    while (taken < capacity && !p.buckets.empty()) {
      auto &q = p.buckets.front();
      // A changed range/clamp can split an old overflow bucket, and a band
      // can hold several admission buckets. FIFO must not hide an eligible
      // item behind an ineligible one in the lowest band.
      size_t eligible = 0;
      const size_t n = q.size();
      while (eligible < n && !admitted(q[eligible])) ++eligible;
      if (eligible == n) break;
      items[taken++] = q[eligible];
      q.erase(eligible);
      --p.count;
      p.buckets.tidy_front();
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
    if (!guard || b.chunks.empty()) return false;
    const Band key = b.chunks.front_key();
    auto &chunks = b.chunks.front();
    size_t eligible = 0;
    const size_t n = chunks.size();
    if (!admitted(chunks[0]->items[0]))
      while (eligible < n && !std::any_of(chunks[eligible]->items.begin(),
                                          chunks[eligible]->items.end(), admitted))
        ++eligible;
    if (eligible == n) return false;
    Chunk *chunk = chunks[eligible];
    // Private nonempty before the bin can read empty, so empty() never sees
    // the chunk in neither place.
    Private &p = private_[rank];
    p.nonempty.store(true, std::memory_order_release);
    chunks.erase(eligible);
    b.chunks.tidy_front();
    publish_hint(b);
    guard.unlock();
    auto &q = p.buckets.at(key);
    for (const Item &item : chunk->items) q.push_back(item);
    delete chunk;
    p.count += chunk_size;
    COST_ADD(CHUNK_TAKES, 1);
    COST_ADD(CHUNK_TAKEN_ITEMS, chunk_size);
    if (rank != peer) COST_ADD(CHUNK_PEER_TAKEN_ITEMS, chunk_size);
    return true;
  }
public:
  explicit ProcessWork(int n, bool priority = false)
      : count(n), nearest(priority), private_(new Private[n]), bins(new Bin[n]) {}
  ~ProcessWork() {
    for (int i = 0; i < count; ++i)
      while (!bins[i].chunks.empty()) {
        auto &q = bins[i].chunks.front();
        delete q[0];
        q.pop_front();
        bins[i].chunks.tidy_front();
      }
  }
  void push(int rank, const Item &item, long original_bucket = 0) {
    COST_SCOPE(PUSH_CALLS);
    Private &p = private_[rank];
    const Band bucket = band_of(item, original_bucket);
    auto &q = p.buckets.at(bucket);
    q.push_back(item);
    if (++p.count == 1) p.nonempty.store(true, std::memory_order_release);
    if (q.size() >= chunk_size) {
      Chunk *chunk = new Chunk;
      for (int i = 0; i < chunk_size; ++i) {
        chunk->items[i] = q[0];
        q.pop_front();
      }
      p.buckets.tidy(bucket);
      {
        Bin &b = bins[rank];
        std::lock_guard<std::mutex> guard(b.lock);
        b.chunks.at(bucket).push_back(chunk);
        publish_hint(b);
      }
      COST_ADD(CHUNK_PUBLICATIONS, 1);
      COST_ADD(CHUNK_PUBLISHED_ITEMS, chunk_size);
      // Published nonempty is visible before private can read empty.
      p.count -= chunk_size;
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
      COST_ADD(CHUNK_PRIVATE_BANDS, private_[rank].buckets.spilled());
    }
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
        (best_distance >> process_band_shift) < p.buckets.front_key().second)) {
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
