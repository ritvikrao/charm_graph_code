#include "weighted_htram_smp.decl.h"
#include <iostream>
#include <cmath>
#include <stdio.h>
#include <map>
#include <stdlib.h>
#include <fstream>
#include <string>
#include <limits>

// htram
#include "NDMeshStreamer.h"
#include "TopoManager.h"
#include "htram_group.h"

// set data type for messages
using tram_proxy_t = CProxy_HTram;
using tram_t = HTram;

/* readonly */
tram_proxy_t tram_proxy;
CProxy_HTramRecv nodeGrpProxy;
CProxy_HTramNodeGrp srcNodeGrpProxy;
CProxy_Main mainProxy;
CProxy_WeightedArray arr;
int N;	  // number of processors
int V;	  // number of vertices
int imax; // integer maximum
int average;
// tram constants
int buffer_size = 1024; //meaningless for smp; size changed in htram_group.h
double flush_timer = 0.5;
bool enable_buffer_flushing = false;

class Main : public CBase_Main
{
	// CkVec<Node> graph;
private:
	// int not_returned;
	int start_vertex;
	int *partition_index;
	double start_time;
	double read_time;
	double total_time;
	double compute_begin;
	double compute_time;

public:
	/**
	 * Read in graph from csv (currently sequential)
	 */
	Main(CkArgMsg *m)
	{
		N = atoi(m->argv[1]); // read in number of processes
		if (!m->argv[1])
		{
			ckout << "Missing processor count" << endl;
			CkExit(0);
		}
		V = atoi(m->argv[2]); // read in number of vertices
		if (!m->argv[2])
		{
			ckout << "Missing vertex count" << endl;
			CkExit(0);
		}
		std::string file_name = m->argv[3]; // read file name
		if (!m->argv[3])
		{
			ckout << "Missing file name" << endl;
			CkExit(0);
		}
		int S = atoi(m->argv[4]); // randomize seed
		if (!m->argv[4])
		{
			ckout << "Missing random seed" << endl;
			CkExit(0);
		}
		start_vertex = atoi(m->argv[5]); // number of beginning vertex
		if (!m->argv[5])
		{
			ckout << "Missing start vertex" << endl;
			CkExit(0);
		}
		unsigned int seed = (unsigned int)S;
		srand(seed);
		int first_node = 0;
		imax = std::numeric_limits<int>::max();
		// create graph object
		partition_index = new int[N];
		start_time = CkWallTimer();
		// read file
		std::ifstream file(file_name);
		std::string readbuf;
		std::string delim = ",";
		// iterate through edge list
		ckout << "Loop begins" << endl;
		CkVec<LongEdge> edges;
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
			//ckout << "Edge begin " << node_num << " Edge end " << node_num_2 << " Edge length " << edge_distance << endl;
			// ckout << node_num << endl;
			// ckout << node_num_2 << endl;
			LongEdge new_edge;
			new_edge.begin = node_num;
			new_edge.end = node_num_2;
			new_edge.distance = edge_distance;
			edges.insertAtEnd(new_edge);
			// ckout << "One loop iteration complete" << endl;
		}
		ckout << "Loop complete" << endl;
		file.close();
		read_time = CkWallTimer() - start_time;
		// ckout << "File closed" << endl;
		// define readonly variables
		arr = CProxy_WeightedArray::ckNew(N);
		// create TRAM proxy
		CkGroupID updater_array_gid;
		updater_array_gid = arr.ckGetArrayID();
		tram_proxy = tram_proxy_t::ckNew(updater_array_gid, buffer_size, enable_buffer_flushing, flush_timer);
		nodeGrpProxy = CProxy_HTramRecv::ckNew();
    	srcNodeGrpProxy = CProxy_HTramNodeGrp::ckNew();
		mainProxy = thisProxy;
		arr.initiate_pointers();
		// assign nodes to location
		std::vector<LongEdge> edge_lists[N];
		// CkVec<CkVec<Node>> node_lists;
		int current_vertex = 0;
		int begin_chunk = 0;
		int end_chunk = 0;
		average = edges.size() / N;
		int rem = edges.size() % N;
		int last_v = -1;
		int curr_proc = 0;
		for (int i = 0; i < edges.size(); i++)
		{
			int dest_proc = i / average;
			if (dest_proc >= N)
				dest_proc = N - 1;
			else if (i % average == 0)
				partition_index[dest_proc] = edges[i].begin;
			edge_lists[dest_proc].insert(edge_lists[dest_proc].end(), edges[i]);
		}
		// reassign edges to move to correct pe
		for (int i=0; i<N-1; i++)
		{
			for(int j=edge_lists[i].size()-1; j>=0; --j)
			{
				//TODO
				if(edge_lists[i][j].begin>=partition_index[i+1])
				{
					edge_lists[i+1].insert(edge_lists[i+1].begin(), edge_lists[i][j]);
					edge_lists[i].erase(edge_lists[i].begin()+j);
				}
			}
		}
		// add nodes to node lists
		// send subgraphs to nodes
		for (int i = 0; i < N; i++)
		{
			arr[i].get_graph(edge_lists[i].data(), edge_lists[i].size(), partition_index, N);
		}
	}

	/**
	 * Start algorithm from source vertex
	 */
	void begin(int result)
	{
		// ready to begin algorithm
		/*
		for (int i = 0; i < N; i++)
		{
			ckout << "Start vertex for partition " << i << ": " << partition_index[i] << endl;
		}
		*/
		std::pair<int, int> new_edge;
		new_edge.first = start_vertex;
		new_edge.second = 0;
		// CkVec<Edge> dist_list;
		// dist_list.insertAtEnd(new_edge);
		int dest_proc = 0;
		for (int i = 0; i < N - 1; i++)
		{
			if (start_vertex > partition_index[i + 1])
				dest_proc++;
			else
				break;
		}
		//ckout << "Beginning" << endl;
		compute_begin = CkWallTimer();
		arr[dest_proc].update_distances(new_edge);
		// quiescence detection
		CkCallback cb(CkIndex_Main::print(), mainProxy);
		CkStartQD(cb);
		//}
	}

	/**
	 * Before printing distances, check if all the buffers are empty
	 * If not, flush the buffer (allowing the execution to continue)
	 * also restart qd
	 * If empty, end execution by printing the distances
	 */
	void print()
	{
		ckout << "Quiescence detected" << endl;
		arr.check_buffer();
	}

	/**
	 * returns when all buffers are checked
	 */
	void check_buffer_done(int* msg_stats, int N)
	{
		ckout << "Receives: " << msg_stats[1] << ", Sends: " << msg_stats[0] << endl;
		int net_messages = msg_stats[1] - msg_stats[0]; //receives - sends
		if (net_messages==1) //difference of 1 because of initial send
		{
			//ckout << "Real quiescence, terminate" << endl;
			compute_time = CkWallTimer() - compute_begin;
			arr.print_distances();
		}
		else
		{
			//ckout << "False quiescence, continue execution" << endl;
			CkCallback cb(CkIndex_Main::print(), mainProxy);
			CkStartQD(cb);
			arr.keep_going();
		}
	}

	void done(int result)
	{
		// ends program, prints that program is ended
		ckout << "Completed" << endl;
			total_time = CkWallTimer() - start_time;
			ckout << "Read time: " << read_time << endl;
			ckout << "Compute time: " << compute_time << endl;
			ckout << "Total time: " << total_time << endl;
		CkExit(0);
	}
};

/**
 * Array of chares for Dijkstra
 */
class WeightedArray : public CBase_WeightedArray
{
private:
	Node *local_graph;
	int start_vertex;
	int num_vertices;
	int send_updates;
	int recv_updates;
	int *partition_index;

public:
	WeightedArray()
	{
		send_updates=0;
		recv_updates=0;
	}

	void initiate_pointers()
	{
		tram_t *tram = tram_proxy.ckLocalBranch();
		tram->set_func_ptr(WeightedArray::update_distance_caller, this);
	}

	void get_graph(LongEdge* edges, int E, int *partition, int N)
	{
		partition_index = new int[N];
		for (int i = 0; i < N; i++)
		{
			partition_index[i] = partition[i];
		}
		start_vertex = edges[0].begin;
		num_vertices = 1 + edges[E - 1].begin - start_vertex;
		// ckout << "Num vertices chare " << thisIndex << ": " << num_vertices << endl;
		local_graph = new Node[num_vertices];
		for (int i = 0; i < num_vertices; i++)
		{
			Node new_node;
			new_node.home_process = thisIndex;
			new_node.index = i + start_vertex;
			new_node.distance = imax;
			CkVec<Edge> adj;
			new_node.adjacent = adj;
			local_graph[i] = new_node;
		}
		for (int i = 0; i < E; i++)
		{
			Edge new_edge;
			new_edge.end = edges[i].end;
			new_edge.distance = edges[i].distance;
			// ckout << "Processor " << thisIndex << " origin " << edges[i].begin << " destination " << new_edge.end << " weight " << new_edge.distance << endl;
			local_graph[edges[i].begin - start_vertex].adjacent.insertAtEnd(new_edge);
		}
		int now_done = 1;
		CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
		contribute(sizeof(int), &now_done, CkReduction::sum_int, cb);
		// mainProxy.begin();
	}

	static void update_distance_caller(void *p, std::pair<int, int> new_vertex_and_distance)
	{
		((WeightedArray *)p)->update_distances(new_vertex_and_distance);
	}

	// test function
	static void update_distance_test(void *p, int new_vertex_and_distance)
	{
		return;
	}

	/**
	 * Update distance. Consumes a vertex and a new distance, potentially updates the distance,
	 * and then keeps going
	 */
	void update_distances(std::pair<int, int> new_vertex_and_distance)
	{
		//add sends
		recv_updates++;
		
		// get local branch of tram proxy
		tram_t *tram = tram_proxy.ckLocalBranch();

		int local_index = new_vertex_and_distance.first - start_vertex;
		//ckout << "Incoming pair on PE " << thisIndex << ": " << new_vertex_and_distance.first << ", " << new_vertex_and_distance.second << endl;
		// if the incoming distance is actually smaller
		if (new_vertex_and_distance.second < local_graph[local_index].distance)
		{
			local_graph[local_index].distance = new_vertex_and_distance.second;
			// for all neighbors
			for (int i = 0; i < local_graph[local_index].adjacent.size(); i++)
			{
				// calculate distance pair for neighbor
				std::pair<int, int> updated_dist;
				updated_dist.first = local_graph[local_index].adjacent[i].end;
				updated_dist.second = local_graph[local_index].distance + local_graph[local_index].adjacent[i].distance;
				// calculate destination pe
				int dest_proc = 0;
				for (int j = 0; j < N - 1; j++)
				{
					// find first partition that begins at a higher edge count;
					if (updated_dist.first >= partition_index[j + 1])
						dest_proc++;
					else
						break;
				}
				//ckout << "Outgoing pair on PE " << thisIndex << ": " << updated_dist.first << ", " << updated_dist.second << endl;
				if (updated_dist.first > 0 && updated_dist.first < V)
				{
					// send buffer to pe
					tram->insertValue(updated_dist, dest_proc);
					send_updates++;
				}
				// arr[dest_proc].update_distances(updated_dist);
			}
		}
		// tram->tflush();
	}

	/**
	 * Checks if anything is in the buffer (false quiescence)
	 */
	void check_buffer()
	{
		//ckout << "Checking message stats" << endl;
		int msg_stats[2];
		msg_stats[0] = send_updates;
		msg_stats[1] = recv_updates;
		CkCallback cb(CkReductionTarget(Main, check_buffer_done), mainProxy);
		contribute(2*sizeof(int), msg_stats, CkReduction::sum_int, cb);
	}

	/**
	 * Called when some of the buffers aren't full, meaning we need to keep the algorithm going
	 */
	void keep_going()
	{
		tram_t *tram = tram_proxy.ckLocalBranch();
		tram->tflush();
	}

	/**
	 * Print out the final distances calculated by the algorithm
	 */
	void print_distances()
	{
		
		/* //enable only for smaller graphs
		for (int i = 0; i < num_vertices; i++)
		{
			ckout << "Partition " << thisIndex << " vertex num " << local_graph[i].index << " distance " << local_graph[i].distance << endl;
		}
		*/

		int done = 1; // placeholder
		CkCallback cb(CkReductionTarget(Main, done), mainProxy);
		contribute(sizeof(int), &done, CkReduction::sum_int, cb);
		// mainProxy.done();
	}
};

#include "weighted_htram_smp.def.h"
