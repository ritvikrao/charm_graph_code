#include "NDMeshStreamer.h"

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

class Node{
	public:
		long index;
		int home_process;
		cost distance;
		CkVec<Edge> adjacent;
		bool send_updates;
	
	Node(){}
	explicit operator bool() const
      	{ return index > -1; }
	void pup(PUP::er &p) 
	{
		p | index;
		p | home_process;
		p | distance;
		p | adjacent;
		p | send_updates;
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


template <>
struct is_PUPbytes<std::pair<int,int>> {
  static const bool value = true;
};

