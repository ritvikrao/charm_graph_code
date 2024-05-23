#include "graph_ckio.decl.h"
#include <iostream>
#include <sys/stat.h>
#include <cmath>
#include <stdio.h>
#include <map>
#include <stdlib.h>
#include <fstream>
#include <string>
#include <limits>
#include <sys/stat.h>

CProxy_Main mainProxy;
int N;	  // number of processors
int V;	  // number of vertices
int imax; // integer maximum
std::string global_fname;

void read_graph_file(Ck::IO::Session session, std::string fname)
{
    Ck::IO::FileReader fr(session);
    std::string readbuf;
	std::string delim = ",";
    CkVec<LongEdge> edges;
	int max_index = 0;
    while(!fr.eof())
    {
        Ck::IO::getline(fr, readbuf);
        // get nodes on each edge
        std::string token = readbuf.substr(0, readbuf.find(delim));
        std::string token2 = readbuf.substr(readbuf.find(delim) + 1, readbuf.length());
        // make random distance
        int edge_distance = rand() % 10 + 1;
        // string to int
        int node_num = std::stoi(token);
        int node_num_2 = std::stoi(token2);
        ckout << "Edge begin " << node_num << " Edge end " << node_num_2 << " Edge length " << edge_distance << endl;
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
    CkEnforce(fr.eof());
};

class Main : public CBase_Main
{
    Main_SDAG_CODE

    private:
	// int not_returned;
    
	double start_time;
	double read_time;
	int max_index;
    std::string file_name;
    int file_size;
    Ck::IO::Session session;
    Ck::IO::File f;
    struct stat sb;

    public:

    Main(CkArgMsg *m)
	{
		N = atoi(m->argv[1]); // read in number of chares
		if (!m->argv[1])
		{
			ckout << "Missing chare count" << endl;
			CkExit(0);
		}
		V = atoi(m->argv[2]); // read in number of vertices
		if (!m->argv[2])
		{
			ckout << "Missing vertex count" << endl;
			CkExit(0);
		}
		file_name = m->argv[3]; // read file name
		if (!m->argv[3])
		{
			ckout << "Missing file name" << endl;
			CkExit(0);
		}
        if(lstat(m->argv[3], &sb) == -1)
        {
            perror("lstat");
            exit(1);
        }
        file_size = sb.st_size;
		int S = atoi(m->argv[4]); // randomize seed
		if (!m->argv[4])
		{
			ckout << "Missing random seed" << endl;
			CkExit(0);
		}
        global_fname = file_name;
        mainProxy = thisProxy;
		unsigned int seed = (unsigned int)S;
		srand(seed);
		imax = std::numeric_limits<int>::max();
		start_time = CkWallTimer();
        thisProxy.run();
        delete m;
    }
};

#include "graph_ckio.def.h"