#include "sssp_nonsmp.decl.h"
#include <iostream>
#include <cmath>
#include <stdio.h>
#include <map>
#include <stdlib.h>
#include <fstream>
#include <string>
#include <limits>
#include <queue>

// htram
#include "tramNonSmp.h"

// set data type for messages
using tram_proxy_t = CProxy_tramNonSmp<std::pair<int, int>>;
using tram_t = tramNonSmp<std::pair<int, int>>;

/* readonly */
tram_proxy_t tram_proxy;
CProxy_Main mainProxy;
CProxy_WeightedArray arr;
int N;	  // number of processors
int V;	  // number of vertices
int imax; // integer maximum
int average;
// tram constants
int buffer_size = 1024;
double flush_timer = 5;
bool enable_buffer_flushing = false;

struct ComparePairs
{
	bool operator()(const std::pair<int, int> &lhs, const std::pair<int, int> &rhs) const
	{
		// Compare the second integers of the pairs
		return lhs.second > rhs.second; // '>' for min heap, '<' for max heap
	}
};

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
	int max_index;

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
		partition_index = new int[N + 1];
		start_time = CkWallTimer();
		// read file
		std::ifstream file(file_name);
		std::string readbuf;
		std::string delim = ",";
		// iterate through edge list
		// ckout << "Loop begins" << endl;
		CkVec<LongEdge> edges;
		max_index = 0;
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
		// ckout << "File closed" << endl;
		// define readonly variables
		arr = CProxy_WeightedArray::ckNew(N);
		ckout << "Making htram at time " << CkWallTimer() << endl;
		// create TRAM proxy
		CkGroupID updater_array_gid;
		updater_array_gid = arr.ckGetArrayID();
		tram_proxy = tram_proxy_t::ckNew(updater_array_gid, buffer_size, enable_buffer_flushing, flush_timer);
		mainProxy = thisProxy;
		arr.initiate_pointers();
		// assign nodes to location
		std::vector<LongEdge> edge_lists[N];
		average = edges.size() / N;
		for (int i = 0; i < edges.size(); i++)
		{
			int dest_proc = i / average;
			if (dest_proc >= N)
				dest_proc = N - 1;
			else if (i % average == 0)
				partition_index[dest_proc] = edges[i].begin;
			edge_lists[dest_proc].insert(edge_lists[dest_proc].end(), edges[i]);
		}
		partition_index[N] = max_index + 1;
		// reassign edges to move to correct pe
		for (int i = 0; i < N - 1; i++)
		{
			for (int j = edge_lists[i].size() - 1; j >= 0; --j)
			{
				// TODO
				if (edge_lists[i][j].begin >= partition_index[i + 1])
				{
					edge_lists[i + 1].insert(edge_lists[i + 1].begin(), edge_lists[i][j]);
					edge_lists[i].erase(edge_lists[i].begin() + j);
				}
			}
		}
		// add nodes to node lists
		// send subgraphs to nodes
		for (int i = 0; i < N; i++)
		{
			arr[i].get_graph(edge_lists[i].data(), edge_lists[i].size(), partition_index, N + 1);
		}
	}

	/**
	 * Start algorithm from source vertex
	 */
	void begin(int result)
	{
		// ready to begin algorithm
		std::pair<int, int> new_edge;
		new_edge.first = start_vertex;
		new_edge.second = 0;
		// CkVec<Edge> dist_list;
		// dist_list.insertAtEnd(new_edge);
		int dest_proc = 0;
		for (int i = 0; i < N; i++)
		{
			if (start_vertex >= partition_index[i] && start_vertex < partition_index[i + 1])
			{
				dest_proc = i;
				break;
			}
			if (i == N - 1)
				dest_proc = N - 1;
		}
		// ckout << "Beginning" << endl;
		compute_begin = CkWallTimer();
		// quiescence detection
		CkCallback cb(CkIndex_Main::print(), mainProxy);
		CkStartQD(cb);
		ckout << "Registering callback at time " << CkWallTimer() << endl;
		arr[dest_proc].update_distances(new_edge);
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
		// ckout << "Quiescence detected" << endl;
		arr.check_buffer();
	}

	/**
	 * returns when all buffers are checked
	 */
	void check_buffer_done(int *msg_stats, int N)
	{
		// ckout << "Receives: " << msg_stats[1] << ", Sends: " << msg_stats[0] << endl;
		int net_messages = msg_stats[1] - msg_stats[0]; // receives - sends
		if (net_messages == 1)							// difference of 1 because of initial send
		{
			// ckout << "Real quiescence, terminate" << endl;
			compute_time = CkWallTimer() - compute_begin;
			arr.print_distances();
		}
		else
		{
			// ckout << "False quiescence, continue execution" << endl;
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
		ckout << "Wasted updates: " << result - V << endl;
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
	int wasted_updates;
	tram_t *tram;
	std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, ComparePairs> pq;

public:
	WeightedArray()
	{
		send_updates = 0;
		recv_updates = 0;
	}

	void initiate_pointers()
	{
		tram = tram_proxy.ckLocalBranch();
		tram->set_func_ptr(WeightedArray::update_distance_caller, this);
	}

	void get_graph(LongEdge *edges, int E, int *partition, int dividers)
	{
		partition_index = new int[dividers];
		for (int i = 0; i < dividers; i++)
		{
			partition_index[i] = partition[i];
		}
		start_vertex = partition_index[thisIndex];
		num_vertices = partition_index[thisIndex + 1] - partition_index[thisIndex];
		// ckout << "Num vertices chare " << thisIndex << ": " << num_vertices << endl;
		local_graph = new Node[num_vertices];
		if (num_vertices != 0)
		{
			for (int i = 0; i < num_vertices; i++)
			{
				Node new_node;
				new_node.home_process = thisIndex;
				new_node.index = i + start_vertex;
				new_node.distance = imax;
				CkVec<Edge> adj;
				new_node.adjacent = adj;
				local_graph[i] = new_node;
				wasted_updates = 0;
			}
			// ckout << "Pe " << CkMyPe() << " edge count: " << E << endl;
			for (int i = 0; i < E; i++)
			{
				Edge new_edge;
				new_edge.end = edges[i].end;
				new_edge.distance = edges[i].distance;
				// ckout << "Processor " << thisIndex << " origin " << edges[i].begin << " destination " << new_edge.end << " weight " << new_edge.distance << endl;
				local_graph[edges[i].begin - start_vertex].adjacent.insertAtEnd(new_edge);
			}
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
	 * Update distances, but locally (the incoming pair comes from this PE)
	 * this is not an entry method
	 */
	void update_distances_local()
	{
		// add sends
		// recv_updates++;
		while (pq.size() > 0)
		{
			std::pair<int, int> new_vertex_and_distance = pq.top();
			pq.pop();
			if (new_vertex_and_distance.first >= partition_index[thisIndex] && new_vertex_and_distance.first < partition_index[thisIndex + 1])
			{
				// get local branch of tram proxy
				// tram_t *tram = tram_proxy.ckLocalBranch();

				int local_index = new_vertex_and_distance.first - start_vertex;
				// if (CkMyPe()==1) ckout << "Incoming local pair on PE " << thisIndex << ": " << new_vertex_and_distance.first << ", " << new_vertex_and_distance.second << endl;
				//  if the incoming distance is actually smaller
				if (new_vertex_and_distance.second < local_graph[local_index].distance)
				{
					local_graph[local_index].distance = new_vertex_and_distance.second; // update distance
					// for all neighbors
					for (int i = 0; i < local_graph[local_index].adjacent.size(); i++)
					{
						// calculate distance pair for neighbor
						std::pair<int, int> updated_dist;
						updated_dist.first = local_graph[local_index].adjacent[i].end;
						updated_dist.second = local_graph[local_index].distance + local_graph[local_index].adjacent[i].distance;
						// calculate destination pe
						int dest_proc = 0;
						for (int j = 0; j < N; j++)
						{
							// find first partition that begins at a higher edge count;
							if (updated_dist.first >= partition_index[j] && updated_dist.first < partition_index[j + 1])
							{
								dest_proc = j;
								break;
							}
							if (j == N - 1)
								dest_proc = N - 1;
						}
						if (updated_dist.first > 0 && updated_dist.first < V)
						{
							// send buffer to pe
							if (dest_proc == CkMyPe())
							{
								// if (CkMyPe()==1) ckout << "Outgoing local pair on PE " << thisIndex << ": " << updated_dist.first << ", " << updated_dist.second << endl;
								pq.push(updated_dist);
							}
							else
							{
								// if (CkMyPe()==1) ckout << "Outgoing remote pair on PE " << thisIndex << ": " << updated_dist.first << ", " << updated_dist.second << endl;
								tram->insertValue(updated_dist, dest_proc);
								send_updates++;
							}
						}
						// arr[dest_proc].update_distances(updated_dist);
					}
				}
			}
		}
	}

	/**
	 * Update distance. Consumes a vertex and a new distance, potentially updates the distance,
	 * and then keeps going
	 */
	void update_distances(std::pair<int, int> new_vertex_and_distance)
	{
		// add sends
		// traceMemoryUsage();
		recv_updates++;
		if (new_vertex_and_distance.first >= partition_index[thisIndex] && new_vertex_and_distance.first < partition_index[thisIndex + 1])
		{
			// get local branch of tram proxy
			// tram_t *tram = tram_proxy.ckLocalBranch();

			int local_index = new_vertex_and_distance.first - start_vertex;
			wasted_updates++; // wasted update, except for the last one
			// if (CkMyPe()==1) ckout << "Incoming remote pair on PE " << thisIndex << ": " << new_vertex_and_distance.first << ", " << new_vertex_and_distance.second << endl;
			//  if the incoming distance is actually smaller
			if (new_vertex_and_distance.second < local_graph[local_index].distance)
			{
				local_graph[local_index].distance = new_vertex_and_distance.second; // update distance
				// for all neighbors
				for (int i = 0; i < local_graph[local_index].adjacent.size(); i++)
				{
					// calculate distance pair for neighbor
					std::pair<int, int> updated_dist;
					updated_dist.first = local_graph[local_index].adjacent[i].end;
					updated_dist.second = local_graph[local_index].distance + local_graph[local_index].adjacent[i].distance;
					// calculate destination pe
					int dest_proc = 0;
					for (int j = 0; j < N; j++)
					{
						// find first partition that begins at a higher edge count;
						if (updated_dist.first >= partition_index[j] && updated_dist.first < partition_index[j + 1])
						{
							dest_proc = j;
							break;
						}
						if (j == N - 1)
							dest_proc = N - 1;
					}
					if (updated_dist.first > 0 && updated_dist.first < V)
					{
						// send buffer to pe
						if (dest_proc == CkMyPe())
						{
							// if (CkMyPe()==1) ckout << "Outgoing local pair on PE " << thisIndex << ": " << updated_dist.first << ", " << updated_dist.second << endl;
							pq.push(updated_dist);
						}
						else
						{
							// if (CkMyPe()==1) ckout << "Outgoing remote pair on PE " << thisIndex << ": " << updated_dist.first << ", " << updated_dist.second << endl;
							tram->insertValue(updated_dist, dest_proc);
							send_updates++;
						}
					}
					// arr[dest_proc].update_distances(updated_dist);
				}
			}
		}
		if (pq.size() > 0)
		{
			update_distances_local();
		}
	}

	/**
	 * Checks if anything is in the buffer (false quiescence)
	 */
	void check_buffer()
	{
		// ckout << "Checking message stats" << endl;
		int msg_stats[2];
		msg_stats[0] = send_updates;
		msg_stats[1] = recv_updates;
		CkCallback cb(CkReductionTarget(Main, check_buffer_done), mainProxy);
		contribute(2 * sizeof(int), msg_stats, CkReduction::sum_int, cb);
	}

	/**
	 * Called when some of the buffers aren't full, meaning we need to keep the algorithm going
	 */
	void keep_going()
	{
		// tram_t *tram = tram_proxy.ckLocalBranch();
		tram->tflush();
	}

	/**
	 * Print out the final distances calculated by the algorithm
	 */
	void print_distances()
	{

		// enable only for smaller graphs
		/*
		for (int i = 0; i < num_vertices; i++)
		{
			ckout << "Partition " << thisIndex << " vertex num " << local_graph[i].index << " distance " << local_graph[i].distance << endl;
		}
		*/
		CkCallback cb(CkReductionTarget(Main, done), mainProxy);
		contribute(sizeof(int), &wasted_updates, CkReduction::sum_int, cb);
		// mainProxy.done();
	}
};

#include "sssp_nonsmp.def.h"
