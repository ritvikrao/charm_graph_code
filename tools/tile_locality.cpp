// Print the --reader-tile locality measurement for a GAPBS file: for each
// owner count, the auto tile size and its 4x and 16x multiples, with the
// fraction of sampled edges that stay inside a tile, and the size the rule
// picks at a given target.
//
//   tile_locality FILE.wsg TARGET OWNERS [OWNERS...]
#include "graphlib/gapbs.h"
#include "graphlib/tile_locality.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s FILE.wsg TARGET OWNERS [OWNERS...]\n", argv[0]);
    return 2;
  }
  const std::string path = argv[1];
  const double target = std::atof(argv[2]);
  const GapbsHeader h = gapbs_read_header(path);
  for (int a = 3; a < argc; ++a) {
    const int owners = std::atoi(argv[a]);
    TileLocality m;
    const long chosen = choose_tile_by_locality(path, h, owners, target, &m);
    std::printf("file=%s vertices=%lld edges_sampled=%lld owners=%d", path.c_str(),
                (long long)h.num_nodes, (long long)m.edges, owners);
    for (size_t t = 0; t < m.tiles.size(); ++t)
      std::printf(" tile%ld=%.4f", m.tiles[t], m.inside[t]);
    std::printf(" chosen=%ld\n", chosen);
  }
  return 0;
}
