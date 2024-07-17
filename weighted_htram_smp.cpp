#include "NDMeshStreamer.h"
#include "TopoManager.h"
#include "htram_group.h"
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
#include <algorithm>

// htram

// set data type for messages
using tram_proxy_t = CProxy_HTram;
using tram_t = HTram;

/* readonly */
//tram_proxy_t tram_proxy;
CProxy_HTramRecv nodeGrpProxy;
CProxy_HTramNodeGrp srcNodeGrpProxy;
CProxy_Main mainProxy;
CProxy_SsspChares arr;
CProxy_SharedInfo shared;
int N;	  // number of processors
int V;	  // number of vertices
cost lmax; // integer maximum
int average; // average edge count per pe
int histo_bucket_count = 512; // number of buckets for the histogram needed for message prioritization
double reduction_delay = 0.01; // each histogram reduction happens at this interval
int initial_threshold = 3;
// tram constants
int buffer_size = 1024; // meaningless for smp; size changed in htram_group.h
double flush_timer = 0.01; // milliseconds
bool enable_buffer_flushing = false;

void fast_exit(void *obj, double time);

void start_reductions(void *obj, double time)
{
	arr.contribute_histogram(0);
}

struct ComparePairs
{
	bool operator()(const Update &lhs, const Update &rhs) const
	{
		// Compare the second integers of the pairs
		return lhs.distance > rhs.distance; // '>' for min heap, '<' for max heap
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
	int max_index;
	int threshold_change_counter;
	int previous_threshold;
	int reduction_counts = 0;
	int no_incoming = 0;
	std::vector<double> reduction_times;
	bool first_qd_done = false;
	bool second_qd_done = false;
	int activeBucketMax = 10;

public:

	double compute_begin;
	double compute_time;

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
		lmax = std::numeric_limits<cost>::max();
		// create graph object
		partition_index = new int[N + 1]; // last index=maximum index
		start_time = CkWallTimer();
		// read file
		std::ifstream file(file_name);
		std::string readbuf;
		std::string delim = ",";
		// iterate through edge list
		CkVec<LongEdge> edges;
		int *incoming_count = new int[V];
		for(int i=0; i<V; i++) incoming_count[i] = 0;
		max_index = 0;
		while (getline(file, readbuf))
		{
			// get nodes on each edge
			std::string token = readbuf.substr(0, readbuf.find(delim));
			std::string token2 = readbuf.substr(readbuf.find(delim) + 1, readbuf.length());
			// make random distance
			cost edge_distance = (cost) rand() % 1000 + 1;
			// string to int
			int node_num = std::stoi(token); //v
			int node_num_2 = std::stoi(token2); //w
			incoming_count[node_num_2]++;
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
		for(int i=0; i<max_index; i++)
		{
			if(incoming_count[i]==0) no_incoming++;
		}
		ckout << "Vertices with no incoming edges: " << no_incoming << endl;
		//ckout << "Loop complete" << endl;
		file.close();
		read_time = CkWallTimer() - start_time;
		// create TRAM proxy
		nodeGrpProxy = CProxy_HTramRecv::ckNew();
		srcNodeGrpProxy = CProxy_HTramNodeGrp::ckNew();

		CkCallback ignore_cb(CkCallback::ignore);
		tram_proxy_t tram_proxy = tram_proxy_t::ckNew(nodeGrpProxy.ckGetGroupID(), srcNodeGrpProxy.ckGetGroupID(), buffer_size, enable_buffer_flushing, flush_timer, false, true, ignore_cb);

		shared = CProxy_SharedInfo::ckNew();
		arr = CProxy_SsspChares::ckNew(tram_proxy.ckGetGroupID(), N);

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
	void begin(cost max_sum)
	{
		// ready to begin algorithm
		shared.max_path_value(max_sum);
		ckout << "The sum of the maximum out-edges is " << max_sum << endl;
		Update new_edge;
		new_edge.dest_vertex = start_vertex;
		new_edge.distance = 0;
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
		compute_begin = CkWallTimer();
		ckout << "Beginning at time: " << compute_begin << endl;
		// quiescence detection
		CkCallback cb(CkIndex_Main::quiescence_detected(), mainProxy);
		CkStartQD(cb);
		// temp callback to test flushing
		threshold_change_counter = 0;
		previous_threshold = initial_threshold;
		//CcdCallFnAfter(start_reductions, (void *) this, reduction_delay);
		CcdCallFnAfter(fast_exit, (void *) this, 10000.0); //end after 5 s
		arr[dest_proc].start_algo(new_edge);
	}

	/**
	 * Before printing distances, check if all the buffers are empty
	 * If not, flush the buffer (allowing the execution to continue)
	 * also restart qd
	 * If empty, end execution by printing the distances
	 */
	void quiescence_detected()
	{
		if (first_qd_done == false) 
		{
			first_qd_done = true;
			ckout << "First Quiescence detected at time: " << CkWallTimer() << endl;
			CkCallback cb(CkIndex_Main::quiescence_detected(), mainProxy);
			CkStartQD(cb);
			// Ask everyone to call flush
			arr.get_bucket_limit(initial_threshold, initial_threshold+2, 0); // dummy values.. hopefully no damage. Just get tflush called
 	  	}
 	  	else 
		{
			second_qd_done = true;
			ckout << "Second Quiescence detected at time: " << CkWallTimer() << "  starting reductions. "<< endl;
			arr.contribute_histogram(0);
 	  	}
	}

	/**
	 * Receive histo values from pes
	 * The idea is to get the distribution of update values, then 
	 * do local processing to select thresholds
	*/
	void reduce_histogram(int *histo_values, int histo_length)
	{
		reduction_times.push_back(CkWallTimer());
		reduction_counts++;
		int histogram_sum = 0;
		int first_nonzero = -1;
		int receives = histo_values[histo_bucket_count + 1];
		int sends = histo_values[histo_bucket_count];
		int bfs_processed = histo_values[histo_bucket_count+2];
		int done_vertex_count = histo_values[histo_bucket_count+3];
		//calculate the total histogram sum
		for(int i=0; i<histo_bucket_count; i++)
		{
			histogram_sum += histo_values[i];
			if((histo_values[i]>0)&&(first_nonzero==-1))
			{
				first_nonzero = i;
			}
		}
		ckout << "Receives: " << receives << " Sends: " 
		<< sends << " BFS Processed: " << bfs_processed << " Time: " << CkWallTimer() << endl;
		if (bfs_processed == done_vertex_count) // not quite correct.. this should be affter we are sure bfs_processed has converged.. maybe via qd
		{
		    ckout << "all reachable vertices are done. " << bfs_processed << ":" << done_vertex_count << "at time: " << CkWallTimer() << endl; 
		    compute_time = CkWallTimer() - compute_begin;
		    arr.print_distances();
		    return;
		}
		if(receives-sends==1)
		{
			ckout << "Receives and sends match" << endl;
			ckout << "Threshold: " << previous_threshold << endl;
			compute_time = CkWallTimer() - compute_begin;
			arr.print_distances();
			return;
		}
		//ckout << "Histogram sum = " << histogram_sum << " at time " << CkWallTimer() << endl;
		int selected_bucket = 0;
		int tram_bucket = 0;
		int active_counter = 0;
		//calculate target percentile
		double target_percent; //heap percentage
		double tram_percent; //tram percentage
		if(second_qd_done)
		{
			if(histogram_sum <= N * 100) 
			{
				target_percent = 1.0;
				tram_percent = 1.0;
			}
			else
			{
				target_percent = 0.05;
				tram_percent = 0.15;
			}
			//select bucket limit
			for(int i=0; i<histo_bucket_count; i++)
			{
				active_counter += histo_values[i];
				if((double) active_counter >= histogram_sum * target_percent) 
				{
					selected_bucket = i;
					break;
				}
				
			}
			active_counter = 0;
			for(int i=0; i<histo_bucket_count; i++)
			{
				active_counter += histo_values[i];
				if((double) active_counter >= histogram_sum * tram_percent)
				{
					tram_bucket = i;
					break;
				}
			}
			/*
			if ((selected_bucket-first_nonzero) > activeBucketMax)
 			  selected_bucket = first_nonzero + activeBucketMax;
 			
 			  if ((tram_bucket - selected_bucket) > 5)
 			  tram_bucket = selected_bucket + 5;  // tram_bucket no more than 5 buckets ahead of selected_bucket
 			*/
			//in case of floating point weirdness
			if(histogram_sum==0)
			{
				selected_bucket = histo_bucket_count - 1;
				tram_bucket = histo_bucket_count - 1;
				
			}
			if(selected_bucket != previous_threshold)
			{
				previous_threshold = selected_bucket;
				threshold_change_counter++;
				ckout << "Changed threshold to " << selected_bucket << " and tram threshold to" << 
				tram_bucket << " first nonzero: " << first_nonzero << " at time " << CkWallTimer() << endl;
				/*
				ckout << "Bucket counts: [" ;
				for(int i=0; i<histo_bucket_count; i+=32)
				{
					int counter = 0;
					for(int j=i; j<i+32; j++)
					{
						counter += histo_values[j];
					}
					ckout << counter << ", ";
				}
				ckout << "]" << endl;
				*/
				arr.get_bucket_limit(selected_bucket, tram_bucket, first_nonzero - 1);
			}
			arr.contribute_histogram(first_nonzero-1);
		}
		
		//start next reduction round
		//CcdCallFnAfter(start_reductions, (void *) this, reduction_delay);
		
	}

	/**
	 * returns when all buffers are checked
	 */
	void check_buffer_done(int *msg_stats, int N)
	{
		int net_messages = msg_stats[1] - msg_stats[0]; // receives - sends
		if (net_messages == 1)							// difference of 1 because of initial send
		{
			//ckout << "Real quiescence, terminate" << endl;
			compute_time = CkWallTimer() - compute_begin;
			//arr.stop_periodic_flush();
			arr.print_distances();
		}
		else
		{
			//ckout << "False quiescence, continue execution" << endl;
			CkCallback cb(CkIndex_Main::quiescence_detected(), mainProxy);
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
		ckout << "Number of threshold changes: " << threshold_change_counter << endl;
		ckout << "Number of reductions: " << reduction_counts << endl;
		/*
		int x = reduction_counts / 10;
		for(int i=0; i<reduction_counts; i++)
		{
			if(i%x==0)
			{
				ckout << "Time at reduction number " << i << "is: " << reduction_times[i] << endl;
			}
		}
		*/
		CkExit(0);
	}
};

void fast_exit(void *obj, double time)
{
	ckout << "Ending program now at time " << CkWallTimer() << endl;
	((Main*) obj)->compute_time = CkWallTimer() - ((Main*) obj)->compute_begin;
	arr.print_distances();
	CkExit(0);
}

/**
 * This holds information that needs to be broadcasted
 * but that is calculated after the Main method
*/
class SharedInfo : public CBase_SharedInfo
{
	public:
	cost max_path;
	int event_id;
	SharedInfo()
	{
		event_id = traceRegisterUserEvent("Contrib reduction");
	}

	void max_path_value(cost max_path_val)
	{
		max_path = max_path_val;
	}

};

/**
 * Array of chares for Dijkstra
 */
class SsspChares : public CBase_SsspChares
{
private:
	Node *local_graph; //structure to hold vertices assigned to this pe
	int start_vertex; //global index of lowest vertex assigned to this pe
	int num_vertices=0; //number of vertices assigned to this pe
	int send_updates; //number of update messages sent
	int recv_updates; //number of update messages received
	int *partition_index; //defines boundaries of indices for each pe
	int wasted_updates; //number of updates that don't have the final answer
	int rejected_updates; //number of updates that don't decrease a distance value/create more messages
	tram_proxy_t tram_proxy;
	tram_t *tram; //tram library
	SharedInfo *shared_local;
	std::priority_queue<Update, std::vector<Update>, ComparePairs> pq; //heap of messages
	int *histogram; //local histogram of data, from 0 to max_size, divided into histo_bucket_count buckets
	int *vcount;
	int bucket_limit;
	int tram_bucket_limit;
	double bucket_multiplier;
	std::vector<Update> *new_tram_hold;
	std::vector<Update> *local_hold;
	int bfs_created=0;
	int bfs_processed=0;

public:
	SsspChares(CkGroupID tram_id)
	{
		tram_proxy = CProxy_HTram(tram_id);
		send_updates = 0;
		recv_updates = 0;
	}

	void initiate_pointers()
	{
		tram = tram_proxy.ckLocalBranch();
		tram->set_func_ptr_retarr(SsspChares::process_update_caller, this);
		shared_local = shared.ckLocalBranch();
	}

	void get_graph(LongEdge *edges, int E, int *partition, int dividers)
	{
		histogram = new int[histo_bucket_count];
		vcount = new int[histo_bucket_count+1]; //histo buckets plus infty
		for(int i=0; i<histo_bucket_count; i++)
		{
			histogram[i] = 0;
			vcount[i] = 0;
		}
		vcount[histo_bucket_count] = 0;
		partition_index = new int[dividers];
		for (int i = 0; i < dividers; i++)
		{
			partition_index[i] = partition[i];
		}
		start_vertex = partition_index[thisIndex];
		num_vertices = partition_index[thisIndex + 1] - partition_index[thisIndex];
		local_graph = new Node[num_vertices];
		bucket_limit = initial_threshold;
		tram_bucket_limit = initial_threshold + 2;
		new_tram_hold = new std::vector<Update>[histo_bucket_count];
		local_hold = new std::vector<Update>[histo_bucket_count];
		bucket_multiplier = histo_bucket_count / (512 * log(V));
		int *largest_outedges = new int[num_vertices];
		if (num_vertices != 0)
		{
			for (int i = 0; i < num_vertices; i++)
			{
				Node new_node;
				new_node.home_process = thisIndex;
				new_node.index = i + start_vertex;
				new_node.distance = lmax;
				new_node.send_updates = false;
				CkVec<Edge> adj;
				new_node.adjacent = adj;
				local_graph[i] = new_node;
				wasted_updates = 0;
				rejected_updates = 0;
				vcount[histo_bucket_count]++;
				largest_outedges[i] = 0;
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
		//register idle call to process_heap
		CkCallWhenIdle(CkIndex_SsspChares::process_heap(), this);
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

	/**
	 * Method that accepts initial update to source vertex
	*/
	void start_algo(Update new_vertex_and_distance)
	{
		process_update(new_vertex_and_distance);
		arr[thisIndex].process_heap();
	}

	/**
	 * Gets the destination processor for a given vertex 
	*/
	int get_dest_proc(int vertex)
	{
		int dest_proc = 0;
		for (int j = 0; j < N; j++)
		{
			// find first partition that begins at a higher edge count;
			if (vertex >= partition_index[j] && vertex < partition_index[j + 1])
			{
				dest_proc = j;
				break;
			}
			if (j == N - 1)
			{
				dest_proc = N - 1;
			}
		}
		return dest_proc;
	}

	static void process_update_caller(void *p, Update *new_vertex_and_distances, int count)
	{
		//ckout << "PE " << CkMyPe() << " receiving " << count << " updates" << endl;
		for(int i=0; i<count; i++)
		{
			((SsspChares *)p)->process_update(new_vertex_and_distances[i]);
		}
		((SsspChares *)p)->process_heap();

	}

	/**
	 * Gets the histogram bucket for any given distance
	*/
	int get_histo_bucket(cost distance)
	{
		double bucket = distance * bucket_multiplier;
		int result = (int) bucket;
		if(result >= histo_bucket_count) return histo_bucket_count - 1;
		else return result;
	}

	void generate_updates(int local_index, bool bfs)
	{
		for (int i = 0; i < local_graph[local_index].adjacent.size(); i++)
		{ 
			// calculate distance pair for neighbor
			Update new_update;
			new_update.dest_vertex = local_graph[local_index].adjacent[i].end;
			new_update.distance = local_graph[local_index].distance + local_graph[local_index].adjacent[i].distance;
			//we are going to send this, so add to the histogram and the send update count
			int neighbor_bucket = get_histo_bucket(new_update.distance);
			histogram[neighbor_bucket]++;
			send_updates++;
			//if exceeds limit, put in hold
			if((neighbor_bucket > tram_bucket_limit) && !bfs)
			{
				new_tram_hold[neighbor_bucket].push_back(new_update);
			}
			else
			{
				//calculated dest proc
				int dest_proc = get_dest_proc(new_update.dest_vertex);
				if(dest_proc==CkMyPe())
				{
					process_update(new_update);
				}
				else tram->insertValue(new_update, dest_proc);
			}
		}
	}

	/**
	 * Update distances, but locally (the incoming pair comes from this PE)
	 * this is not an entry method
	 * returns true (runs when pe is idle)
	 */
	bool process_heap()
	{
		// add sends
		while (pq.size() > 0)
		{
			Update new_vertex_and_distance = pq.top();
			int dest_vertex = new_vertex_and_distance.dest_vertex;
			cost new_distance = new_vertex_and_distance.distance;
			int this_histo_bucket = get_histo_bucket(new_distance);
			if(this_histo_bucket > bucket_limit)
			{
				//if(CkMyPe()==40) ckout << "Exceeds limit" << endl;
				break;
			}
			pq.pop();
			if (dest_vertex >= partition_index[thisIndex] && dest_vertex < partition_index[thisIndex + 1])
			{
				// get local branch of tram proxy
				// tram_t *tram = tram_proxy.ckLocalBranch();

				int local_index = dest_vertex - start_vertex;
				//if (CkMyPe()==0) ckout << "Incoming local pair on PE " << thisIndex << ": " << new_vertex_and_distance.first << ", " << new_vertex_and_distance.second << endl;
				//  if the incoming distance is actually smaller
				if (new_distance < local_graph[local_index].distance)
				{
					ckout << "**Error: picked cost from heap smaller than vertex cost" << endl;
				}
				else if (new_distance == local_graph[local_index].distance)
				{
					if(local_graph[local_index].send_updates)
					{
						local_graph[local_index].send_updates = false;
						// for all neighbors
						generate_updates(local_index, false);
					}
					else 
					{
						ckout << "**Error: picked cost that is equal to vertex cost but send_updates is false" << endl;
					}
				}
				else
				{
					rejected_updates++;
				}
			}
			recv_updates++;
			wasted_updates++;
			histogram[this_histo_bucket]--;
		}
		return true;
	}

	/**
	 * Takes a distance update and immediately adds it to the local heap/pq
	 */
	void process_update(Update new_vertex_and_distance)
	{
		int dest_vertex = new_vertex_and_distance.dest_vertex;
		int local_index = dest_vertex - start_vertex;
		cost this_cost = new_vertex_and_distance.distance;
		int this_bucket = get_histo_bucket(this_cost);
		if (this_cost < local_graph[local_index].distance)
		{
			vcount[this_bucket]++;
			if(local_graph[local_index].distance == lmax)
			{ 
				local_graph[local_index].distance = this_cost;
				bfs_processed++;
				vcount[histo_bucket_count]--;
				generate_updates(local_index, true);
				recv_updates++;
				wasted_updates++;
				histogram[this_bucket]--;
			}
			else
			{ 
				vcount[get_histo_bucket(local_graph[local_index].distance)]--;
				local_graph[local_index].distance = this_cost;
				local_graph[local_index].send_updates = true;
				if(this_bucket > bucket_limit)
				{
					local_hold[this_bucket].push_back(new_vertex_and_distance);
				}
				else pq.push(new_vertex_and_distance);
			}
		}
		else
		{
			recv_updates++;
			wasted_updates++;
			rejected_updates++;
			histogram[this_bucket]--;
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
	 * Contribute to a reduction to get the overall histogram to pe 0/main chare
	*/
	void contribute_histogram(int behind_first_nonzero)
	{
		int donecount = 0;
		if (CkMyPe() == 0) ckout << "on 0 contributing histogram reduction at: " << CkWallTimer() << endl;
		traceUserEvent(shared_local -> event_id);
		CkCallback cb(CkReductionTarget(Main, reduce_histogram), mainProxy);
		int *info_array = new int[histo_bucket_count+4];
		std::copy(histogram, histogram + histo_bucket_count, info_array);
		info_array[histo_bucket_count] = send_updates;
		info_array[histo_bucket_count+1] = recv_updates;
		info_array[histo_bucket_count+2] = bfs_processed;
		for (int i=0; i<=behind_first_nonzero;i++) donecount += vcount[i];
		info_array[histo_bucket_count+3] = donecount;
		contribute((histo_bucket_count+4) * sizeof(int), info_array, CkReduction::sum_int, cb);
	}

	/**
	 * Called when some of the buffers aren't full, meaning we need to keep the algorithm going
	 */
	void keep_going()
	{
		// everything in the tram hold gets added to tram
		for(int i=0; i<histo_bucket_count; i++)
		{
			for(int j=0; j<new_tram_hold[i].size(); j++)
			{
				int dest_proc = get_dest_proc(new_tram_hold[i][j].dest_vertex);
				if(dest_proc==CkMyPe())
				{
					pq.push(new_tram_hold[i][j]);
				}
				else tram->insertValue(new_tram_hold[i][j], dest_proc);
			}
			new_tram_hold[i].clear();
		}
		for(int i=0; i<histo_bucket_count; i++)
		{
			for(int j=0; j<local_hold[i].size(); j++)
			{
				pq.push(local_hold[i][j]);
			}
			local_hold[i].clear();
		}
		tram->tflush();
		arr[thisIndex].process_heap();
	}

	/**
	 * Broadcasts bucket limit
	*/
	void get_bucket_limit(int bucket, int tram_bucket, int behind_first_nonzero)
	{
		//if(CkMyPe()==17) ckout << "Wasted on PE 17 = " << wasted_updates << " Rejected= " << rejected_updates << " Time: " << CkWallTimer() << endl;
		bucket_limit = bucket;
		tram_bucket_limit = tram_bucket;
		int counter = 0;
		//ckout << "Time when threshold received: " << CkWallTimer() << " PE: " << CkMyPe() << " size: " << tram_hold.size() << endl;
		//after every reduction, push out messages in hold that are in limit
		for(int i=0; i<=tram_bucket_limit; i++)
		{
			for(int j=0; j<new_tram_hold[i].size(); j++)
			{
				int dest_proc = get_dest_proc(new_tram_hold[i][j].dest_vertex);
				if(dest_proc==CkMyPe())
				{
					process_update(new_tram_hold[i][j]);
				}
				else tram->insertValue(new_tram_hold[i][j], dest_proc);
			}
			new_tram_hold[i].clear();
		}
		for(int i=0; i<=bucket_limit; i++)
		{
			for(int j=0; j<local_hold[i].size(); j++)
			{
				pq.push(local_hold[i][j]);
			}
			local_hold[i].clear();
		}
		//ckout << "Timer: " << CkWallTimer() << " PE: " << CkMyPe() << " size: " << tram_hold.size() << " count: " << counter << endl;
		tram->tflush();
		arr[thisIndex].process_heap();
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
