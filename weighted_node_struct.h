#pragma once

// Only PUP is needed here. This used to include NDMeshStreamer.h -- Charm++'s
// own TRAM -- which the retired non-SMP build depended on, and which dragged
// that whole module into every translation unit that touches a graph type.
#include "charm++.h"
#include "pup.h"
#include <cstdint>

typedef long cost;

class Edge{
	public:
		long end;
		cost distance;
	Edge(){}
	void pup(PUP::er &p) 
	{
		p | end;
		p | distance;
	}
};

class Update{
	public:
		long dest_vertex;
		cost distance;
	Update(){}
	void pup(PUP::er &p) 
	{
		p | dest_vertex;
		p | distance;
	}
};

// Bit 62 of dest_vertex records that this update was charged to the top
// histogram bucket -- the overflow slot -- when it was created.
//
// It is there so that the clamp threshold can rise during a run. The histogram
// is incremented by the PE that creates an update and decremented by the PE
// that retires it, and neither can walk "its" live updates, so every merge has
// to land the decrement where the increment went. For an ordinary bucket the
// arithmetic does that on its own: floor(floor(x/s)/k) == floor(x/(s*k)). The
// overflow slot is the one index that is not an index -- it means "past the
// clamp" -- so if the clamp moves between creation and retirement the retiring
// PE computes a real bucket and the increment is stranded where nothing will
// take it away. That is the whole of the 7.6d clamp bug, and freezing the
// clamp is what closed it, at the price of a range no coarsening could extend.
//
// One bit retires that trade. A flagged update is retired from the overflow
// slot whatever the clamp is now, so the clamp may rise freely. The bit is
// carried in the top of the vertex field rather than in a field of its own
// because Update is the payload of every htram message: a field would take it
// from 16 bytes to 24 and make every A/B against a recorded run a comparison
// of message sizes. Vertex ids here are under 2^32 and the theoretical ceiling
// is 2^62; -1, which fold_batch uses as a tombstone, is not a taggable value
// and stays distinguishable from any tagged vertex.
static const long UPDATE_OVERFLOW_BIT = 1L << 62;
// --lazy-heavy (step 8d): a queue entry that stands for a range of a vertex's
// heavier edges rather than an update to it. Bit 61 marks it and bits 56-60
// hold the range. Tokens stay in the owning PE's queue and never reach htram.
static const long UPDATE_TOKEN_BIT = 1L << 61;
static const int UPDATE_TOKEN_LEVEL_SHIFT = 56;
static const long UPDATE_TOKEN_MASK = UPDATE_TOKEN_BIT | (31L << UPDATE_TOKEN_LEVEL_SHIFT);
inline long update_vertex(const Update &u) {
	return u.dest_vertex & ~(UPDATE_OVERFLOW_BIT | UPDATE_TOKEN_MASK);
}
inline bool update_is_token(const Update &u) {
	return (u.dest_vertex & UPDATE_TOKEN_BIT) != 0;
}
inline int update_token_level(const Update &u) {
	return (int)((u.dest_vertex >> UPDATE_TOKEN_LEVEL_SHIFT) & 31);
}
inline bool update_overflowed(const Update &u) {
	return (u.dest_vertex & UPDATE_OVERFLOW_BIT) != 0;
}

// The update as htram carries it when built with HTRAM_COMPACT_WIRE: 8 bytes
// instead of 16 plus a 4-byte destination padded to 24. The overflow flag
// moves from bit 62 of the vertex to bit 31, so vertex ids must be below 2^31
// and distances below 2^32 -- every input here is under 2^27 vertices and a
// largest distance of 5.5e7 (road-usa). A tentative distance can in principle
// exceed its final value by any amount, so the check is made on every item
// rather than once on the graph, and a value that does not fit stops the run:
// truncating it would deliver a distance shorter than the path it describes.
struct WireUpdate {
	uint32_t vertex;   // bit 31: UPDATE_OVERFLOW_BIT
	uint32_t distance;
};
inline WireUpdate wire_pack(const Update &u) {
	const unsigned long v = (unsigned long)update_vertex(u);
	if (__builtin_expect((v >> 31) | ((unsigned long)u.distance >> 32), 0))
		CkAbort("compact wire: vertex %ld or distance %ld does not fit in 31/32 bits",
		        update_vertex(u), (long)u.distance);
	WireUpdate w;
	w.vertex = (uint32_t)v | (update_overflowed(u) ? 0x80000000u : 0u);
	w.distance = (uint32_t)u.distance;
	return w;
}
inline Update wire_unpack(const WireUpdate &w) {
	Update u;
	u.dest_vertex = (long)(w.vertex & 0x7fffffffu) |
	                ((w.vertex >> 31) ? UPDATE_OVERFLOW_BIT : 0L);
	u.distance = (long)w.distance;
	return u;
}

class LongEdge
{
	public:
		long begin;
        long end;
        cost distance;
	LongEdge(){}
	void pup(PUP::er &p) 
	{
		p | begin;
		p | end;
		p | distance;
	}
};

