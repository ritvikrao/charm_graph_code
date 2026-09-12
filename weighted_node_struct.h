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

