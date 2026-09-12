#ifndef GRAPHLIB_TYPES_H
#define GRAPHLIB_TYPES_H

/**
 * The payload types, and the one place that knows whether Charm++ is present.
 *
 * Under GRAPH_GEN_STANDALONE the headers build with no runtime at all, which is
 * what lets tools/graph_digest and tools/graph_convert be compiled by several
 * toolchains and compared -- the check that graph generation really is
 * bit-identical everywhere. Those stand-ins must stay layout- and
 * value-compatible with weighted_node_struct.h.
 */

#ifdef GRAPH_GEN_STANDALONE

#include <cstdio>
#include <cstdlib>

typedef long cost;

struct Edge {
  long end;
  cost distance;
};

struct LongEdge {
  long begin;
  long end;
  cost distance;
};

#define GRAPHLIB_ABORT(...)                                                    \
  do {                                                                         \
    std::fprintf(stderr, __VA_ARGS__);                                         \
    std::fprintf(stderr, "\n");                                                \
    std::abort();                                                              \
  } while (0)

#else

#include "weighted_node_struct.h"
#define GRAPHLIB_ABORT(...) CkAbort(__VA_ARGS__)

#endif

#endif // GRAPHLIB_TYPES_H
