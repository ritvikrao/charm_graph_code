#pragma once

// Only PUP is needed here. This used to include NDMeshStreamer.h -- Charm++'s
// own TRAM -- which the retired non-SMP build depended on, and which dragged
// that whole module into every translation unit that touches a graph type.
#include "charm++.h"
#include "pup.h"

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
inline long update_vertex(const Update &u) {
	return u.dest_vertex & ~UPDATE_OVERFLOW_BIT;
}
inline bool update_overflowed(const Update &u) {
	return (u.dest_vertex & UPDATE_OVERFLOW_BIT) != 0;
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

