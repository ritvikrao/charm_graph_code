#ifndef GRAPHLIB_H
#define GRAPHLIB_H

/**
 * graphlib v1: graph inputs for the ACIC engine.
 *
 *   rng.h          portable deterministic randomness; no <random>, no libm
 *   weights.h      edge weights as a function of (u, v), orthogonal to topology
 *   generators.h   uniform, 2-D mesh, RMAT/Kronecker
 *   gapbs.h        GAPBS .sg / .wsg serialized-graph reader and writer
 *   csr.h          flat CSR for one PE's slice
 *   edge_source.h  GraphSpec, and how a source reaches the PE that owns a row
 *   reference.h    serial Dijkstra and the order-independent distance digest
 *
 * Everything here is header-only and builds without Charm++ when
 * GRAPH_GEN_STANDALONE is defined, which is what lets tools/graph_digest and
 * tools/graph_convert run anywhere -- in CI, and on a reviewer's laptop.
 */

#include "csr.h"
#include "edge_source.h"
#include "gapbs.h"
#include "generators.h"
#include "reference.h"
#include "rng.h"
#include "weights.h"

#endif // GRAPHLIB_H
