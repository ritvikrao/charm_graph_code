#include "graphlib/tile_layout.h"
#include <cassert>
#include <numeric>

int main() {
  for (long n : {1, 5, 23, 1023, 1024, 1025})
    for (long tile : {1, 4, 16, 10000})
      for (int owners : {1, 3, 8, 120}) {
        TileLayout layout(n, tile, owners);
        std::vector<long> expected;
        for (int owner = 0; owner < owners; ++owner)
          for (long first = owner * tile; first < n; first += tile * owners)
            for (long v = first; v < std::min(n, first + tile); ++v)
              expected.push_back(v);
        assert((long)expected.size() == n);
        for (long v = 0; v < n; ++v) {
          assert(layout.original(v) == expected[v]);
          assert(layout.internal(expected[v]) == v);
          const int owner = (expected[v] / tile) % owners;
          assert(v >= layout.owner_begin(owner) && v < layout.owner_end(owner));
          const long run = layout.contiguous(v, n);
          assert(run > 0 && run <= n - v);
          for (long k = 0; k < run; ++k)
            assert(layout.original(v + k) == expected[v] + k);
        }
      }
  TileLayout off(17, 0, 4);
  assert(off.original(9) == 9 && off.internal(9) == 9);
}
