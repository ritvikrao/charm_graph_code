// partition_by_edges against the linear scan it replaced (sssp_smp.cpp before
// the change, copied below), on in-memory offsets and through FileOffsets /
// TiledOffsets on a real GAPBS file.
//   g++ -O2 -std=c++17 -DGRAPH_GEN_STANDALONE -I. tests/test_edge_partition.cpp && ./a.out [TMPDIR]
#include "graphlib/edge_partition.h"
#include <cassert>
#include <cstdio>
#include <random>

// The old code: the whole (tiled) offsets array in memory, and a linear scan.
static void old_partition(const std::vector<int64_t> &offsets, int first_pe,
                          int last_pe, long begin, long end, long *partition_index) {
  const long pes = last_pe - first_pe;
  const long region_edges = offsets[(size_t)end] - offsets[(size_t)begin];
  const long per_pe = std::max(1L, (region_edges + pes - 1) / pes);
  long vertex = begin;
  for (int i = first_pe; i < last_pe; ++i) {
    partition_index[i] = vertex;
    const long target = (long)offsets[(size_t)vertex] + per_pe;
    while (vertex < end && (long)offsets[(size_t)vertex] < target &&
           end - vertex > last_pe - i - 1)
      ++vertex;
  }
  partition_index[last_pe] = end;
}

static std::vector<int64_t> old_tiled(const std::vector<int64_t> &offsets,
                                      const TileLayout &layout, long V) {
  std::vector<int64_t> tiled((size_t)V + 1, 0);
  for (long v = 0; v < V; ++v) {
    const long original = layout.original(v);
    tiled[(size_t)v + 1] = tiled[(size_t)v] + offsets[(size_t)original + 1] - offsets[(size_t)original];
  }
  return tiled;
}

struct Memory {
  const std::vector<int64_t> &a;
  int64_t operator()(long v) const { return a[(size_t)v]; }
};

// Mixed degrees: runs of isolated vertices, ordinary rows, and a few hubs.
static std::vector<int64_t> random_offsets(long V, std::mt19937_64 &rng) {
  std::vector<int64_t> off((size_t)V + 1, 0);
  for (long v = 0; v < V; ++v) {
    const int kind = (int)(rng() % 10);
    long d = kind < 2 ? 0 : kind < 9 ? (long)(rng() % 9) : (long)(rng() % 5000);
    off[(size_t)v + 1] = off[(size_t)v] + d;
  }
  return off;
}

static void write_gapbs(const char *path, const std::vector<int64_t> &off) {
  const long V = (long)off.size() - 1;
  const int64_t m = off.back();
  std::FILE *f = std::fopen(path, "wb");
  assert(f);
  const unsigned char directed = 0;
  std::fwrite(&directed, 1, 1, f);
  std::fwrite(&m, 8, 1, f);
  const int64_t n = V;
  std::fwrite(&n, 8, 1, f);
  std::fwrite(off.data(), 8, off.size(), f);
  std::vector<int32_t> rec((size_t)(2 * m), 0); // narrow {v, w}; contents unused
  std::fwrite(rec.data(), 4, rec.size(), f);
  std::fclose(f);
}

// One layout: the old and new partitions must agree PE for PE.
static void check(const std::vector<int64_t> &off, long V, long tile, int owners,
                  int per_owner, const FileOffsets *file) {
  const int N = owners * per_owner;
  std::vector<long> want((size_t)N + 1, -1), got((size_t)N + 1, -2), got_file((size_t)N + 1, -3);
  if (tile == 0) {
    old_partition(off, 0, N, 0, V, want.data());
    partition_by_edges(Memory{off}, 0, N, 0, V, got.data());
    if (file) partition_by_edges(*file, 0, N, 0, V, got_file.data());
  } else {
    TileLayout layout(V, tile, owners);
    const std::vector<int64_t> tiled = old_tiled(off, layout, V);
    Memory mem{off};
    TiledOffsets<Memory> lazy(mem, layout, V, tile, owners);
    for (long v = 0; v <= V; ++v) assert(lazy(v) == tiled[(size_t)v]);
    for (int o = 0; o < owners; ++o) {
      const int first = o * per_owner;
      old_partition(tiled, first, first + per_owner, layout.owner_begin(o), layout.owner_end(o), want.data());
      partition_by_edges(lazy, first, first + per_owner, layout.owner_begin(o), layout.owner_end(o), got.data());
    }
    if (file) {
      TiledOffsets<FileOffsets> ftiled(*file, layout, V, tile, owners);
      for (int o = 0; o < owners; ++o) {
        const int first = o * per_owner;
        partition_by_edges(ftiled, first, first + per_owner, layout.owner_begin(o), layout.owner_end(o), got_file.data());
      }
    }
  }
  assert(want == got);
  if (file) assert(want == got_file);
}

int main(int argc, char **argv) {
  std::mt19937_64 rng(20260928);
  long cases = 0;
  for (long V : {1L, 2L, 7L, 64L, 1000L, 20000L}) {
    for (int trial = 0; trial < 4; ++trial) {
      std::vector<int64_t> off = random_offsets(V, rng);
      if (trial == 3) std::fill(off.begin(), off.end(), 0); // edgeless
      for (int owners : {1, 3, 8})
        for (int per_owner : {1, 4, 7})
          for (long tile : {0L, 1L, 5L, 64L, std::max(1L, V / (64L * owners))}) {
            check(off, V, tile, owners, per_owner, nullptr);
            ++cases;
          }
    }
  }
  // Through the file: V past FileOffsets' 8192-entry block, so probes cross
  // blocks, and every layout the in-memory cases use.
  const std::string where = argc > 1 ? argv[1] : "/tmp";
  const std::string file_path = where + "/test_edge_partition.wsg";
  const char *path = file_path.c_str();
  for (long V : {5L, 30000L}) {
    std::vector<int64_t> off = random_offsets(V, rng);
    write_gapbs(path, off);
    GapbsHeader h = gapbs_read_header(path);
    assert(h.num_nodes == V && !h.wide);
    FileOffsets file(path, h);
    for (long v = 0; v <= V; v += 1 + V / 997) assert(file(v) == off[(size_t)v]);
    assert(file(V) == off.back());
    for (int owners : {1, 3, 8})
      for (int per_owner : {1, 7})
        for (long tile : {0L, 1L, 64L, std::max(1L, V / (64L * owners))}) {
          check(off, V, tile, owners, per_owner, &file);
          ++cases;
        }
  }
  std::remove(path);
  std::printf("edge partition: %ld layouts match the linear scan\n", cases);
}
