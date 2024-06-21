#include "weighted_htram_smp.decl.h"
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
CProxy_SharedInfo shared;
int N;	  // number of processors
int V;	  // number of vertices
int imax; // integer maximum
int average; // average edge count per pe
int histo_bucket_count = 128; // number of buckets for the histogram needed for message prioritization
double reduction_delay = 0.1; // each histogram reduction happens at this interval
// tram constants
int buffer_size = 1024; // meaningless for smp; size changed in htram_group.h
double flush_timer = 5; // milliseconds
bool enable_buffer_flushing = false;

void quick_exit(void *obj, double time)
{
	ckout << "Ending program now at time " << CkWallTimer() << endl;
	arr.stop_periodic_flush();
	CkExit(0);
}

void start_reductions(void *obj, double time)
{
	arr.contribute_histogram();
}

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
		partition_index = new int[N + 1]; // last index=maximum index
		start_time = CkWallTimer();
		// read file
		std::ifstream file(file_name);
		std::string readbuf;
		std::string delim = ",";
		// iterate through edge list
		//ckout << "Loop begins" << endl;
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
		//ckout << "Loop complete" << endl;
		file.close();
		read_time = CkWallTimer() - start_time;
		// CkPrintf("Memory usage after file read: %f\n", CmiMemoryUsage()/(1024.0*1024.0));
		//  ckout << "File closed" << endl;
		//  define readonly variables
		shared = CProxy_SharedInfo::ckNew();
		arr = CProxy_WeightedArray::ckNew(N);
		//ckout << "Making htram at time " << CkWallTimer() << endl;
		// create TRAM proxy
		CkGroupID updater_array_gid;
		updater_array_gid = arr.ckGetArrayID();
		tram_proxy = tram_proxy_t::ckNew(updater_array_gid, buffer_size, enable_buffer_flushing, flush_timer, true);
		nodeGrpProxy = CProxy_HTramRecv::ckNew();
		srcNodeGrpProxy = CProxy_HTramNodeGrp::ckNew();
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
		// CkPrintf("Memory usage before building graph: %f\n", CmiMemoryUsage()/(1024.0*1024.0));
	}

	/**
	 * Start algorithm from source vertex
	 */
	void begin(int max_sum)
	{
		// ready to begin algorithm
		// CkPrintf("Memory usage after building graph: %f\n", CmiMemoryUsage()/(1024.0*1024.0));
		shared.max_path_value(max_sum);
		//ckout << "The sum of the maximum out-edges is " << max_sum << endl;
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
		// temp callback to test flushing
		//ckout << "Registering callback at time " << CkWallTimer() << endl;
		CcdCallFnAfter(start_reductions, (void *) this, reduction_delay);
		// CkPrintf("Memory usage before algorithm: %f\n", CmiMemoryUsage()/(1024.0*1024.0));
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
		ckout << "Quiescence detected at time: " << CkWallTimer() << endl;
		// CkPrintf("Memory usage at quiescence: %f\n", CmiMemoryUsage()/(1024.0*1024.0));
		arr.check_buffer();
	}

	/**
	 * Receive histo values from pes
	 * The idea is to get the distribution of update values, then 
	 * do local processing to select thresholds
	*/
	void reduce_histogram(int *histo_values, int histo_length)
	{
		//histo_length = histo_bucket_count always
		int histogram_sum = 0;
		//calculate the total histogram sum
		for(int i=0; i<histo_length; i++)
		{
			histogram_sum += histo_values[i];
		}
		//ckout << "Histogram sum = " << histogram_sum << endl;
		int selected_bucket = 0;
		int tram_bucket = 0;
		int active_counter = 0;
		//calculate target percentile
		double target_percent; //heap percentage
		double tram_percent; //tram percentage
		if(histogram_sum <= 100) 
		{
			target_percent = 1.0;
			tram_percent = 1.0;
		}
		else
		{
			target_percent = 0.1;
			tram_percent = 0.3;
		}
		//select bucket limit
		for(int i=0; i<histo_length; i++)
		{
			active_counter += histo_values[i];
			if((double) active_counter > histogram_sum * target_percent) break;
			else selected_bucket++;
		}
		active_counter = 0;
		for(int i=0; i<histo_length; i++)
		{
			active_counter += histo_values[i];
			if((double) active_counter > histogram_sum * tram_percent) break;
			else tram_bucket++;
		}
		//in case of floating point weirdness
		if(histogram_sum==0)
		{
			selected_bucket = histo_bucket_count - 1;
			tram_bucket = histo_bucket_count - 1;
			
		}
		//send percentile value to chares
		arr.get_bucket_limit(selected_bucket, tram_bucket);
		//start next reduction round
		CcdCallFnAfter(start_reductions, (void *) this, reduction_delay);
		
	}

	/**
	 * returns when all buffers are checked
	 */
	void check_buffer_done(int *msg_stats, int N)
	{
		ckout << "Receives: " << msg_stats[1] << ", Sends: " << msg_stats[0] << endl;
		int net_messages = msg_stats[1] - msg_stats[0]; // receives - sends
		if (net_messages == 1)							// difference of 1 because of initial send
		{
			ckout << "Real quiescence, terminate" << endl;
			compute_time = CkWallTimer() - compute_begin;
			arr.stop_periodic_flush();
			arr.print_distances();
		}
		else
		{
			ckout << "False quiescence, continue execution" << endl;
			CkCallback cb(CkIndex_Main::print(), mainProxy);
			CkStartQD(cb);
			arr.keep_going();
		}
	}

	void done(int *msg_stats, int N)
	{
		// ends program, prints that program is ended
		// ckout << "Completed" << endl;
		// CkPrintf("Memory usage at end: %f\n", CmiMemoryUsage()/(1024.0*1024.0));
		total_time = CkWallTimer() - start_time;
		ckout << "Read time: " << read_time << endl;
		ckout << "Compute time: " << compute_time << endl;
		ckout << "Total time: " << total_time << endl;
		ckout << "Wasted updates: " << msg_stats[0] - V << endl;
		ckout << "Rejected updates: " << msg_stats[1] << endl;
		CkExit(0);
	}
};

/**
 * This holds information that needs to be broadcasted
 * but that is calculated after the Main method
*/
class SharedInfo : public CBase_SharedInfo
{
	public:
	int max_path;
	SharedInfo(){}

	void max_path_value(int max_path_val)
	{
		max_path = max_path_val;
	}

};

/**
 * Array of chares for Dijkstra
 */
class WeightedArray : public CBase_WeightedArray
{
private:
	Node *local_graph; //structure to hold vertices assigned to this pe
	int start_vertex; //global index of lowest vertex assigned to this pe
	int num_vertices; //number of vertices assigned to this pe
	int send_updates; //number of update messages sent
	int recv_updates; //number of update messages received
	int *partition_index; //defines boundaries of indices for each pe
	int wasted_updates; //number of updates that don't have the final answer
	int rejected_updates; //number of updates that don't decrease a distance value/create more messages
	tram_t *tram; //tram library
	SharedInfo *shared_local;
	std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, ComparePairs> pq; //heap of messages
	int *histogram; //local histogram of data, from 0 to max_size, divided into histo_bucket_count buckets
	int bucket_limit;
	int tram_bucket_limit;
	std::vector<std::pair<int,int>> tram_hold;
	std::vector<int> tram_hold_dests;

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
		shared_local = shared.ckLocalBranch();
	}

	void get_graph(LongEdge *edges, int E, int *partition, int dividers)
	{
		histogram = new int[histo_bucket_count];
		for(int i=0; i<histo_bucket_count; i++)
		{
			histogram[i] = 0;
		}
		partition_index = new int[dividers];
		for (int i = 0; i < dividers; i++)
		{
			partition_index[i] = partition[i];
		}
		start_vertex = partition_index[thisIndex];
		num_vertices = partition_index[thisIndex + 1] - partition_index[thisIndex];
		local_graph = new Node[num_vertices];
		int largest_outedges[num_vertices] = {0};
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
				rejected_updates = 0;
			}
			for (int i = 0; i < E; i++)
			{
				Edge new_edge;
				new_edge.end = edges[i].end;
				new_edge.distance = edges[i].distance;
				int new_edge_origin = edges[i].begin - start_vertex;
				local_graph[new_edge_origin].adjacent.insertAtEnd(new_edge);
				if(edges[i].distance > largest_outedges[new_edge_origin]) largest_outedges[new_edge_origin] = edges[i].distance;
			}
		}
		//register idle call to update_distances_local
		CkCallWhenIdle(CkIndex_WeightedArray::update_distances_local(), this);
		//reduce largest edge
		int max_edges_sum = 0;
		if (num_vertices != 0)
		{
			for(int i=0; i<num_vertices; i++)
			{
				max_edges_sum += largest_outedges[i];
			}
		}
		CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
		contribute(sizeof(int), &max_edges_sum, CkReduction::sum_int, cb);
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
	 * Gets the histogram bucket for any given distance
	*/
	int get_histo_bucket(int distance)
	{
		//double percentile = (1.0 * distance) / max_path;
		//double bucket_to_choose = percentile * histo_bucket_count * (V/100); //V/100 to space out buckets
		//int intpart = (int) bucket_to_choose;
		int max_path = shared_local -> max_path;
		double bucket = (distance * ((double) histo_bucket_count) * (V/10)) / max_path;
		int result = (int) bucket;
		//if(CkMyPe()==40) ckout << "Distance= " << distance << endl;
		//if(CkMyPe()==40) ckout << "Bucket= " << result << endl;
		if(result >= histo_bucket_count) return histo_bucket_count - 1;
		else return result;
	}

	/**
	 * Update distances, but locally (the incoming pair comes from this PE)
	 * this is not an entry method
	 * returns true (runs when pe is idle)
	 */
	bool update_distances_local()
	{
		// add sends
		while (pq.size() > 0)
		{
			std::pair<int, int> new_vertex_and_distance = pq.top();
			if(get_histo_bucket(new_vertex_and_distance.second) > bucket_limit)
			{
				//if(CkMyPe()==40) ckout << "Exceeds limit" << endl;
				break;
			}
			pq.pop();
			recv_updates++;
			//if(CkMyPe()==40) ckout << "Within limit" << endl;
			histogram[get_histo_bucket(new_vertex_and_distance.second)]--; //remove from histogram
			wasted_updates++; // wasted update, except for the last one (accounted for by starting from -1)
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
							//we are going to send this, so add to the histogram and the send update count
							histogram[get_histo_bucket(updated_dist.second)]++;
							send_updates++;
							// send buffer to pe
							if (dest_proc == CkMyPe())
							{
								// if (CkMyPe()==1) ckout << "Outgoing local pair on PE " << thisIndex << ": " << updated_dist.first << ", " << updated_dist.second << endl;
								pq.push(updated_dist);
							}
							else
							{
								if(get_histo_bucket(updated_dist.second) > tram_bucket_limit)
								{
									tram_hold.push_back(updated_dist);
									tram_hold_dests.push_back(dest_proc);
								}
								else
								{
									tram->insertValue(updated_dist, dest_proc);
								}
							}
						}
					}
				}
				else rejected_updates++;
			}
		}
		return true;
	}

	/**
	 * Takes a distance update and immediately adds it to the local heap/pq
	 */
	void update_distances(std::pair<int, int> new_vertex_and_distance)
	{
		// stat updates
		pq.push(new_vertex_and_distance);
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
	 * Contribute to a reduction to get the overall histogram to pe 0/main chare
	*/
	void contribute_histogram()
	{
		CkCallback cb(CkReductionTarget(Main, reduce_histogram), mainProxy);
		contribute(histo_bucket_count * sizeof(int), histogram, CkReduction::sum_int, cb);
	}

	/**
	 * Called when some of the buffers aren't full, meaning we need to keep the algorithm going
	 */
	void keep_going()
	{
		// everything in the tram hold gets added to tram
		for(int i=0; i<tram_hold.size(); i++)
		{
			tram->insertValue(tram_hold[i], tram_hold_dests[i]);
		}
		tram_hold.clear();
		tram->tflush();
	}

	/**
	 * Broadcasts bucket limit
	*/
	void get_bucket_limit(int bucket, int tram_bucket)
	{
		bucket_limit = bucket;
		tram_bucket_limit = tram_bucket;
	}

	/**
	 * Print out the final distances calculated by the algorithm
	 */
	void print_distances()
	{
		/*
		 //enable only for smaller graphs
		for (int i = 0; i < num_vertices; i++)
		{
			ckout << "Partition " << thisIndex << " vertex num " << local_graph[i].index << " distance " << local_graph[i].distance << endl;
		}
		*/
		int msg_stats[2];
		msg_stats[0] = wasted_updates;
		msg_stats[1] = rejected_updates;
		CkCallback cb(CkReductionTarget(Main, done), mainProxy);
		contribute(2 * sizeof(int), msg_stats, CkReduction::sum_int, cb);
		// mainProxy.done();
	}

	void stop_periodic_flush()
	{
		tram->stop_periodic_flush();
	}
};


#include "weighted_htram_smp.def.h"
