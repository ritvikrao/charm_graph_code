// The htram wire item packs and unpacks every representable update exactly,
// at the edges of its fields, with and without the overflow flag.
//   c++ -std=c++17 -Itests/stub -I. tests/test_wire.cpp -o test_wire && ./test_wire
//   c++ -std=c++17 -Itests/stub -I. -DACIC_WIRE64 tests/test_wire.cpp -o test_wire64 && ./test_wire64
#include "weighted_node_struct.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
#ifdef ACIC_WIRE64
  const int vertex_bits = 47, distance_bits = 48;
  static_assert(sizeof(WireUpdate) == 12, "12-byte item");
#else
  const int vertex_bits = 31, distance_bits = 32;
  static_assert(sizeof(WireUpdate) == 8, "8-byte item");
#endif
  std::vector<long> vertices, distances;
  for (int b = 0; b < vertex_bits; b++) {
    vertices.push_back(1L << b);
    vertices.push_back((1L << b) - 1);
    vertices.push_back((1L << b) + 1);
  }
  vertices.push_back((1L << vertex_bits) - 1);
  for (int b = 0; b < distance_bits; b++) {
    distances.push_back(1L << b);
    distances.push_back((1L << b) - 1);
  }
  distances.push_back((1L << distance_bits) - 1);
  long checked = 0;
  for (long v : vertices) {
    if (v < 0 || v >= (1L << vertex_bits)) continue;
    for (long d : distances) {
      if (d < 0 || d >= (1L << distance_bits)) continue;
      for (int flag = 0; flag < 2; flag++) {
        Update u;
        u.dest_vertex = v | (flag ? UPDATE_OVERFLOW_BIT : 0L);
        u.distance = d;
        const Update back = wire_unpack(wire_pack(u));
        assert(back.dest_vertex == u.dest_vertex);
        assert(back.distance == u.distance);
        assert(update_vertex(back) == v && update_overflowed(back) == (bool)flag);
        checked++;
      }
    }
  }
  std::printf("wire %d/%d bits: %ld round trips exact\n", vertex_bits, distance_bits, checked);
  return 0;
}
