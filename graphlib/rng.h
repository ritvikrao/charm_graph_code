#ifndef GRAPHLIB_RNG_H
#define GRAPHLIB_RNG_H

/**
 * Deterministic, portable pseudo-randomness for graph generation.
 *
 * This deliberately avoids <random>. The standard engines and, especially,
 * std::uniform_int_distribution are only reproducible within a single standard
 * library implementation: libc++ and libstdc++ return different sequences from
 * the same engine and the same seed. That makes "the same graph" mean different
 * things on a Mac laptop, on Frontier, and on a CI runner. Everything here uses
 * only fixed-width integer arithmetic whose result the language pins down --
 * no floating point, so no libm and no fused-multiply-add to worry about
 * either. scripts/check_generator_portability.sh enforces it.
 */

#include <cstdint>

// SplitMix64. Used as both a seed mixer and a standalone hash.
inline uint64_t splitmix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

inline uint64_t hash_pair(uint64_t a, uint64_t b, uint64_t seed) {
  return splitmix64(splitmix64(a ^ splitmix64(seed)) ^ (b * 0x9E3779B97F4A7C15ULL));
}

// High 64 bits of a 64x64 -> 128 bit product.
inline uint64_t mul_hi_64(uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
  return (uint64_t)(((__uint128_t)a * (__uint128_t)b) >> 64);
#else
  uint64_t a_lo = (uint32_t)a, a_hi = a >> 32;
  uint64_t b_lo = (uint32_t)b, b_hi = b >> 32;
  uint64_t lo_lo = a_lo * b_lo;
  uint64_t hi_lo = a_hi * b_lo;
  uint64_t lo_hi = a_lo * b_hi;
  uint64_t hi_hi = a_hi * b_hi;
  uint64_t cross = (lo_lo >> 32) + (uint32_t)hi_lo + lo_hi;
  return hi_hi + (cross >> 32) + (hi_lo >> 32);
#endif
}

/**
 * A stream seeded only by (index, seed) and never by position, so the caller
 * can jump straight to element i. That is what lets any PE generate exactly
 * its own slice of a graph and get the same slice every time, whatever the PE
 * count.
 *
 * The stream label distinguishes independent uses of the same index space: a
 * vertex's adjacency and a vertex's label permutation must not share draws.
 */
struct IndexRng {
  uint64_t state;

  // The stream is folded into the seed rather than xored in separately, so
  // that stream 0 reproduces the original single-stream formula exactly.
  // Mixing splitmix64(stream) in as its own term looks equivalent and is not:
  // splitmix64(0) is not 0, so it would silently move every graph ever
  // generated -- which is what it did, until the golden digests caught it.
  IndexRng(long index, int seed, uint64_t stream = 0)
      : state(splitmix64((uint64_t)index ^
                         splitmix64((uint64_t)(uint32_t)seed + 0x5DEECE66DULL +
                                    stream))) {}

  uint64_t next() {
    state += 0x9E3779B97F4A7C15ULL;
    return splitmix64(state);
  }

  // Uniform over [0, n). Lemire's multiply-shift; the bias is below n / 2^64.
  uint64_t bounded(uint64_t n) { return mul_hi_64(next(), n); }
};

// The original name, kept because the partition-size draw in Main and every
// recorded golden digest depend on this exact stream.
typedef IndexRng VertexRng;

/**
 * A bijection on [0, 2^bits). Composed from three maps that are each
 * individually invertible mod 2^bits -- add a constant, xor with a right
 * shift of itself, multiply by an odd number -- so the composition is a
 * permutation, not a hash that happens to spread out.
 *
 * RMAT needs this. The generator builds a vertex id one bit per level from the
 * quadrant it descends into, which makes low-numbered vertices the hubs by
 * construction: vertex 0 is always the heaviest. Without relabelling, a
 * contiguous partitioning puts every hub on the first PE, and a load-balance
 * measurement would be measuring the generator. Graph500 requires the same
 * relabelling for the same reason.
 */
inline uint64_t permute_id(uint64_t x, int bits, uint64_t key) {
  const uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
  const unsigned shift = (bits > 1) ? (unsigned)(bits / 2) : 1;
  for (int round = 0; round < 4; round++) {
    x = (x + splitmix64(key + (uint64_t)round)) & mask;
    x = (x ^ (x >> shift)) & mask;
    x = (x * (splitmix64(key + 64 + (uint64_t)round) | 1ULL)) & mask;
  }
  return x;
}

#endif // GRAPHLIB_RNG_H
