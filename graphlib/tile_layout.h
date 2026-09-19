#pragma once
#include <algorithm>
#include <stdexcept>
#include <vector>

// A read-time relabeling: compact input tiles are dealt round-robin, then
// concatenated by owner. Internal IDs retain the existing fast destination
// table. Only input loading, source injection, and digest output use this map.
class TileLayout {
  long vertices_, tile_;
  int owners_;
  std::vector<long> starts_;
public:
  TileLayout(long vertices, long tile, int owners)
      : vertices_(vertices), tile_(tile), owners_(owners) {
    if (vertices < 0 || tile < 0 || owners < 1)
      throw std::invalid_argument("invalid tile layout");
    starts_.assign(owners + 1, 0);
    if (!tile) return;
    const long tiles = vertices / tile + (vertices % tile != 0);
    for (int owner = 0; owner < owners; ++owner) {
      const long count = tiles / owners + (owner < tiles % owners);
      long size = count * tile;
      if (tiles && owner == (tiles - 1) % owners)
        size -= tiles * tile - vertices;
      starts_[owner + 1] = starts_[owner] + size;
    }
  }
  long owner_begin(int owner) const { return starts_.at(owner); }
  long owner_end(int owner) const { return starts_.at(owner + 1); }
  long internal(long vertex) const {
    if (!tile_) return vertex;
    const long tile = vertex / tile_;
    return starts_[tile % owners_] + (tile / owners_) * tile_ + vertex % tile_;
  }
  long original(long vertex) const {
    if (!tile_) return vertex;
    const int owner = (int)(std::upper_bound(starts_.begin(), starts_.end(), vertex)
                            - starts_.begin()) - 1;
    const long local = vertex - starts_[owner];
    return ((local / tile_) * owners_ + owner) * tile_ + local % tile_;
  }
  long contiguous(long vertex, long end) const {
    if (!tile_) return end - vertex;
    const long old = original(vertex);
    return std::min({end - vertex, tile_ - old % tile_, vertices_ - old});
  }
};
