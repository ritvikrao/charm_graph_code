#ifndef GRAPHLIB_WEIGHTS_H
#define GRAPHLIB_WEIGHTS_H

/**
 * Edge weights, kept orthogonal to topology.
 *
 * A weight is a pure function of the ordered pair (u, v) and the seed, not of
 * the per-vertex generation stream. That separation is the point: "RMAT is hard
 * because of its topology" and "RMAT is hard because of its weight
 * distribution" are different claims, and the bucket width in ACIC is tuned
 * against weights in [1, 1000], so a reviewer will ask which one is doing the
 * work. Holding the weight rule fixed while the topology changes, and vice
 * versa, is the only way to answer.
 *
 * UNIFORM with max 1000 is the 2024 configuration and the default; every
 * recorded golden digest depends on it.
 *
 * LOGNORMAL and DEGREE_CORRELATED are named in the plan's evaluation section
 * and are not implemented here. Both need care this file does not yet take:
 * a lognormal drawn with exp() and log() would be at the mercy of the host's
 * libm and would break the cross-machine reproducibility the rest of graphlib
 * guarantees, so it needs an integer quantile table; and a degree-correlated
 * weight needs the endpoint's degree, which an edge-indexed generator does not
 * know locally. Adding either is a change to this file alone.
 */

#include "rng.h"

#include "types.h"

enum WeightMode {
  WEIGHT_UNIFORM = 0, // uniform over [1, max_weight]
  WEIGHT_UNIT = 1,    // every edge costs 1: turns SSSP into BFS
};

// Edge weights live in [1, MAX_EDGE_WEIGHT], matching the 2024 configuration.
#define MAX_EDGE_WEIGHT 1000

struct WeightAssigner {
  int mode = WEIGHT_UNIFORM;
  int seed = 0;
  long max_weight = MAX_EDGE_WEIGHT;

  WeightAssigner() {}
  explicit WeightAssigner(int s, int m = WEIGHT_UNIFORM,
                          long max_w = MAX_EDGE_WEIGHT)
      : mode(m), seed(s), max_weight(max_w) {}

  cost operator()(long u, long v) const {
    if (mode == WEIGHT_UNIT)
      return (cost)1;
    return (cost)(hash_pair((uint64_t)u, (uint64_t)v, (uint64_t)seed) %
                  (uint64_t)max_weight) +
           1;
  }
};

// The original free function, kept because it is what every golden digest and
// the CSV reader were recorded against.
inline cost edge_weight(long u, long v, int seed) {
  return WeightAssigner(seed)(u, v);
}

#endif // GRAPHLIB_WEIGHTS_H
