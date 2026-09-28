#ifndef GRAPHLIB_EDGE_PARTITION_H
#define GRAPHLIB_EDGE_PARTITION_H

/**
 * The equal-edge partition of a GAPBS file, computed from the file's offsets
 * array without holding it.
 *
 * PE 0 used to read all V+1 offsets into memory and, with reader tiles on, build
 * a second, tiled copy: 2 x 275 GB for terrain30-l-z (34.3B vertices), which
 * killed task 0 on a 512 GB node (job 5551488). The partition only needs
 * offsets at the PE boundaries, so each boundary is found by binary search over
 * offsets read on demand: O(PEs log V) 8-byte reads instead of V.
 *
 * partition_by_edges gives exactly the boundaries of the linear scan it
 * replaced (tests/test_edge_partition.cpp checks the two against each other).
 */

#include <algorithm>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "gapbs.h"
#include "tile_layout.h"

/**
 * offsets[v] of a GAPBS file, read with pread. A binary search ends with a run
 * of probes close together, so the last 64 KB block read is kept.
 */
class FileOffsets {
  static constexpr int64_t BLOCK = 8192; // entries (64 KB)
  int fd_ = -1;
  int64_t at_ = 0, entries_ = 0;
  std::string path_;
  mutable int64_t block_ = -1;
  mutable std::vector<int64_t> cache_;

public:
  FileOffsets(const std::string &path, const GapbsHeader &h)
      : at_(h.offsets_at()), entries_(h.num_nodes + 1), path_(path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0)
      GRAPHLIB_ABORT("graphlib: cannot open %s", path.c_str());
  }
  FileOffsets(const FileOffsets &) = delete;
  FileOffsets &operator=(const FileOffsets &) = delete;
  ~FileOffsets() {
    if (fd_ >= 0) ::close(fd_);
  }
  int64_t operator()(int64_t v) const {
    const int64_t block = v / BLOCK;
    if (block != block_) {
      const int64_t first = block * BLOCK;
      const int64_t count = std::min(BLOCK, entries_ - first);
      cache_.resize((size_t)count);
      const size_t bytes = (size_t)count * 8;
      size_t done = 0;
      while (done < bytes) {
        const ssize_t got = ::pread(fd_, (char *)cache_.data() + done, bytes - done,
                                    (off_t)(at_ + first * 8 + (int64_t)done));
        if (got <= 0)
          GRAPHLIB_ABORT("graphlib: short read of the offsets array in %s", path_.c_str());
        done += (size_t)got;
      }
      block_ = block;
    }
    return cache_[(size_t)(v - block_ * BLOCK)];
  }
};

/**
 * Offsets in the reader's tiled vertex order: what the offsets array would be
 * if the file's rows were permuted by TileLayout. Tiling moves whole tiles, so a
 * tiled offset is the tile's tiled start plus the vertex's offset within its
 * tile. That needs one entry per tile (about 64 x owners with --reader-tile
 * auto), read here, instead of a permuted copy of the whole array.
 */
template <typename Offsets>
class TiledOffsets {
  const Offsets &off_;
  const TileLayout &layout_;
  long vertices_, tile_;
  int64_t total_;
  std::vector<int64_t> first_; // per original tile: file offset of its first vertex
  std::vector<int64_t> base_;  // per original tile: tiled offset of its first vertex

public:
  TiledOffsets(const Offsets &off, const TileLayout &layout, long vertices,
               long tile, int owners)
      : off_(off), layout_(layout), vertices_(vertices), tile_(tile),
        total_(off(vertices)) {
    const long tiles = vertices / tile + (vertices % tile != 0);
    first_.resize((size_t)tiles);
    base_.resize((size_t)tiles);
    for (long t = 0; t < tiles; ++t) first_[(size_t)t] = off(t * tile);
    // TileLayout gives owner o the original tiles o, o + owners, ... in order,
    // and lays the owners out one after another.
    int64_t running = 0;
    for (int owner = 0; owner < owners; ++owner)
      for (long t = owner; t < tiles; t += owners) {
        base_[(size_t)t] = running;
        running += (t + 1 < tiles ? first_[(size_t)t + 1] : total_) - first_[(size_t)t];
      }
  }
  int64_t operator()(long v) const {
    if (v == vertices_) return total_;
    const long original = layout_.original(v);
    const long t = original / tile_;
    return base_[(size_t)t] + off_(original) - first_[(size_t)t];
  }
};

/**
 * Split vertices [begin, end) over PEs [first_pe, last_pe) into contiguous
 * ranges of about equal edge count, writing partition_index[first_pe ..
 * last_pe]. Each PE takes vertices until its edges reach ceil(edges / PEs),
 * leaving at least one vertex for every PE after it (so an edgeless graph still
 * spreads out). `off(v)` is the offsets array, in any form that is
 * non-decreasing in v.
 */
template <typename Offsets>
inline void partition_by_edges(const Offsets &off, int first_pe, int last_pe,
                               long begin, long end, long *partition_index) {
  const long pes = last_pe - first_pe;
  const long region_edges = (long)(off(end) - off(begin));
  const long per_pe = std::max(1L, (region_edges + pes - 1) / pes);
  long vertex = begin;
  for (int i = first_pe; i < last_pe; ++i) {
    partition_index[i] = vertex;
    const long cap = end - (last_pe - i - 1);
    if (vertex >= cap) continue;
    // The first v in [vertex, cap) with off(v) >= off(vertex) + per_pe, or cap.
    const int64_t target = off(vertex) + per_pe;
    long lo = vertex, hi = cap;
    while (lo < hi) {
      const long mid = lo + (hi - lo) / 2;
      if (off(mid) < target) lo = mid + 1;
      else hi = mid;
    }
    vertex = lo;
  }
  partition_index[last_pe] = end;
}

#endif
