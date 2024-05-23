#include "graph_serial.decl.h"
#include <iostream>
#include <cmath>
#include <stdio.h>
#include <map>
#include <stdlib.h>
#include <fstream>
#include <string>
#include <limits>

CProxy_Main mainProxy;
int V;	  // number of vertices
int imax; // integer maximum

class Main : public CBase_Main
{
    private:
	double start_time;
	double read_time;
	int max_index;
    std::string file_name;

    public:
    Main(CkArgMsg *m)
    {
		V = atoi(m->argv[1]); // read in number of vertices
		if (!m->argv[1])
		{
			ckout << "Missing vertex count" << endl;
			CkExit(0);
		}
		file_name = m->argv[2]; // read file name
		if (!m->argv[2])
		{
			ckout << "Missing file name" << endl;
			CkExit(0);
		}
        int S = atoi(m->argv[3]); // randomize seed
		if (!m->argv[3])
		{
			ckout << "Missing random seed" << endl;
			CkExit(0);
		}
        mainProxy = thisProxy;
        unsigned int seed = (unsigned int)S;
		srand(seed);
        imax = std::numeric_limits<int>::max();
        // create graph object
		start_time = CkWallTimer();
		// read file
		std::ifstream file(file_name);
		std::string readbuf;
		std::string delim = ",";
		// iterate through edge list
		// ckout << "Loop begins" << endl;
		CkVec<LongEdge> edges;
		max_index = 0;
		// CkPrintf("Memory usage before file read: %f\n", CmiMemoryUsage()/(1024.0*1024.0));
		while (getline(file, readbuf))
		{
			// get nodes on each edge
			std::string token = readbuf.substr(0, readbuf.find(delim));
			std::string token2 = readbuf.substr(readbuf.find(delim) + 1, readbuf.length());
			// make random distance
			int edge_distance = rand() % 10 + 1;
			// string to int
			int node_num = std::stoi(token);
			int node_num_2 = std::stoi(token2);
			// ckout << "Edge begin " << node_num << " Edge end " << node_num_2 << " Edge length " << edge_distance << endl;
			// find the maximum vertex index
			if (node_num > max_index)
				max_index = node_num;
			if (node_num_2 > max_index)
				max_index = node_num_2;
			LongEdge new_edge;
			new_edge.begin = node_num;
			new_edge.end = node_num_2;
			new_edge.distance = edge_distance;
			edges.insertAtEnd(new_edge);
			// ckout << "One loop iteration complete" << endl;
		}
		// ckout << "Loop complete" << endl;
		file.close();
		read_time = CkWallTimer() - start_time;
        ckout << "Read time: " << read_time << endl;
        CkExit();
    }
};

#include "graph_serial.def.h"