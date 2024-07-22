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
#include <random>

#define INFO_PRINTS

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
int M = 1024; // divisor for dest_table (must be power of 2)
int num_global_edges;
int average_degree;
int generate_mode;
int S;
cost lmax; // integer maximum
int average; // average edge count per pe
int histo_bucket_count = 512; // number of buckets for the histogram needed for message prioritization
double reduction_delay = 0.1; // each histogram reduction happens at this interval
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
	int current_phase = 0; //0=initial, 1=bfs, 2=converged_bfs

public:

	double compute_begin;
	double compute_time;

	/**
	 * Read in graph from csv (currently sequential)
	 */
	Main(CkArgMsg *m)
	{
		N = CkNumPes();
		V = atoi(m->argv[1]); // read in number of vertices
		if (!m->argv[1])
		{
			ckout << "Missing vertex count" << endl;
			CkExit(0);
		}
		std::string file_name = m->argv[2]; // read file name or edge count
		if (!m->argv[2])
		{
			ckout << "Missing file name/edge count" << endl;
			CkExit(0);
		}
		S = atoi(m->argv[3]); // randomize seed
		if (!m->argv[3])
		{
			ckout << "Missing random seed" << endl;
			CkExit(0);
		}
		start_vertex = atoi(m->argv[4]); // number of beginning vertex
		if (!m->argv[4])
		{
			ckout << "Missing start vertex" << endl;
			CkExit(0);
		}
		generate_mode = atoi(m->argv[5]); //0 for read from csv, 1 for generation
		if (!m->argv[5])
		{
			ckout << "Missing generate mode" << endl;
			CkExit(0);
		}
		// create TRAM proxy
		nodeGrpProxy = CProxy_HTramRecv::ckNew();
		srcNodeGrpProxy = CProxy_HTramNodeGrp::ckNew();
		CkCallback ignore_cb(CkCallback::ignore);
		tram_proxy_t tram_proxy = tram_proxy_t::ckNew(nodeGrpProxy.ckGetGroupID(), 
		srcNodeGrpProxy.ckGetGroupID(), buffer_size, enable_buffer_flushing, flush_timer, false, true, ignore_cb);
		shared = CProxy_SharedInfo::ckNew();
		arr = CProxy_SsspChares::ckNew(tram_proxy.ckGetGroupID(), N);
		mainProxy = thisProxy;
		arr.initiate_pointers();
		partition_index = new int[N + 1]; // last index=maximum index
		lmax = std::numeric_limits<cost>::max();
		if(generate_mode==1)
		{
			num_global_edges = std::stoi(file_name);
			#ifdef INFO_PRINTS
			ckout << "Graph will be automatically generated with " << V << " vertices and " << num_global_edges << " edges" << endl;
			#endif
			average_degree = num_global_edges / V;
			//for each pe, generate a random vertex and edge count, and send to pes
			int remaining_vertices = V;
			int current_start_index = 0; //tracks start vertex for indices 
			std::mt19937 generator(S);
			std::uniform_int_distribution<int> edge_count_distribution(((num_global_edges * 4) / (N * 5)), (num_global_edges * 6 / (N * 5))); //average += 20%
			std::uniform_int_distribution<int> vertex_count_distribution(((V * 4) / (N * 5)), ((V * 6) / (N * 5))); //average += 20%
			int *vertex_counts = new int[N];
			int *edge_counts = new int[N];
			for(int i=0; i<N; i++)
			{
				partition_index[i] = current_start_index;
				int vertex_count = vertex_count_distribution(generator);
				int edge_count = edge_count_distribution(generator);
				if(i == N - 1) vertex_count = remaining_vertices; //make sure num_vertices = V
				remaining_vertices -= vertex_count;
				vertex_counts[i] = vertex_count;
				edge_counts[i] = edge_count;
				current_start_index += vertex_count;
			}
			partition_index[N] = V;
			for(int i=0; i<N; i++)
			{
				arr[i].generate_local_graph(vertex_counts[i], edge_counts[i], partition_index, N+1);
			}
		}
		else
		{
			#ifdef INFO_PRINTS
			ckout << "Graph will be read from file" << endl;
			#endif
			unsigned int seed = (unsigned int)S;
			srand(seed);
			int first_node = 0;
			// create graph object
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
			int edges_read = 0;
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
				edges_read++;
				//if(edges_read%10000000==0) ckout << "Read " << edges_read << " edges" << endl;
				// ckout << "One loop iteration complete" << endl;
			}
			average_degree = edges_read / V;
			for(int i=0; i<max_index; i++)
			{
				if(incoming_count[i]==0) no_incoming++;
			}
			#ifdef INFO_PRINTS
			ckout << "Vertices with no incoming edges: " << no_incoming << endl;
			ckout << "Max index: " << max_index << endl;
			#endif
			//ckout << "Loop complete" << endl;
			file.close();
			read_time = CkWallTimer() - start_time;
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
	}

	/**
	 * Start algorithm from source vertex
	 */
	void begin(cost max_sum)
	{
		// ready to begin algorithm
		shared.max_path_value(max_sum);
		#ifdef INFO_PRINTS
		ckout << "The sum of the maximum out-edges is " << max_sum << endl;
		#endif
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
		#ifdef INFO_PRINTS
		ckout << "Beginning at time: " << compute_begin << endl;
		#endif
		// quiescence detection
		//CkCallback cb(CkIndex_Main::quiescence_detected(), mainProxy);
		//CkStartQD(cb);
		// temp callback to test flushing
		threshold_change_counter = 0;
		previous_threshold = initial_threshold;
		CcdCallFnAfter(start_reductions, (void *) this, reduction_delay);
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
		/*
		if (first_qd_done == false) 
		{
			first_qd_done = true;
			#ifdef INFO_PRINTS
			ckout << "First Quiescence detected at time: " << CkWallTimer() << endl;
			#endif
			CkCallback cb(CkIndex_Main::quiescence_detected(), mainProxy);
			CkStartQD(cb);
			// Ask everyone to call flush
			arr.get_bucket_limit(initial_threshold, initial_threshold+2, 0); // dummy values.. hopefully no damage. Just get tflush called
 	  	}
 	  	else 
		{
			second_qd_done = true;
			#ifdef INFO_PRINTS
			ckout << "Second Quiescence detected at time: " << CkWallTimer() << "  starting reductions. "<< endl;
			#endif
			arr.contribute_histogram(0);
 	  	}
		*/
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
		int updates_processed = histo_values[histo_bucket_count + 1];
		int updates_created = histo_values[histo_bucket_count];
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
		#ifdef INFO_PRINTS
		ckout << "updates_processed: " << updates_processed << " updates_created: " 
		<< updates_created << " BFS Processed: " << bfs_processed << " Done vertex count: " << done_vertex_count << " Time: " << CkWallTimer() << endl;
		#endif
		/*
		if ((bfs_processed == done_vertex_count) && (bfs_processed > 1000)) // not quite correct.. this should be affter we are sure bfs_processed has converged.. maybe via qd
		{
		    ckout << "all reachable vertices are done. " << bfs_processed << ":" << done_vertex_count << " at time: " << CkWallTimer() << endl; 
		    compute_time = CkWallTimer() - compute_begin;
		    arr.print_distances();
		    return;
		}
		*/
		if((updates_processed-updates_created==1)&&(updates_created>1000))
		{
			ckout << "updates_processed and updates_created match" << endl;
			#ifdef INFO_PRINTS
			ckout << "Threshold: " << previous_threshold << endl;
			#endif
			compute_time = CkWallTimer() - compute_begin;
			arr.print_distances();
			return;
		}
		//ckout << "Histogram sum = " << histogram_sum << " at time " << CkWallTimer() << endl;
		int heap_threshold = 0;
		int tram_threshold = 0;
		int active_counter = 0;
		//calculate target percentile
		double target_percent; //heap percentage
		double tram_percent; //tram percentage
		if(histogram_sum <= N * 100) 
		{
			target_percent = 0.9999;
			tram_percent = 0.9999;
		}
		else
		{
			target_percent = 0.03;
			tram_percent = 0.10;
		}
		//select bucket limit
		for(int i=0; i<histo_bucket_count; i++)
		{
			active_counter += histo_values[i];
			if((double) active_counter >= histogram_sum * target_percent) 
			{
				heap_threshold = i;
				break;
			}
			
		}
		active_counter = 0;
		for(int i=0; i<histo_bucket_count; i++)
		{
			active_counter += histo_values[i];
			if((double) active_counter >= histogram_sum * tram_percent)
			{
				tram_threshold = i;
				break;
			}
		}
		/*
		if ((heap_threshold-first_nonzero) > activeBucketMax)
			heap_threshold = first_nonzero + activeBucketMax;
		
			if ((tram_threshold - heap_threshold) > 5)
			tram_threshold = heap_threshold + 5;  // tram_threshold no more than 5 buckets ahead of heap_threshold
		*/
		//in case of floating point weirdness
		if(histogram_sum==0)
		{
			heap_threshold = histo_bucket_count - 1;
			tram_threshold = histo_bucket_count - 1;
			
		}
		if(heap_threshold != previous_threshold)
		{
			previous_threshold = heap_threshold;
			threshold_change_counter++;
			#ifdef INFO_PRINTS
			ckout << "Changed threshold to " << heap_threshold << " and tram threshold to " << 
			tram_threshold << " first nonzero: " << first_nonzero << " at time " << CkWallTimer() << endl;
			#endif
			//arr.get_bucket_limit(heap_threshold, tram_threshold, first_nonzero - 1);
		}
		//arr.contribute_histogram(first_nonzero-1);
		arr.current_thresholds(heap_threshold, tram_threshold, first_nonzero - 1, current_phase);
		
		//start next reduction round
		//CcdCallFnAfter(start_reductions, (void *) this, reduction_delay);
		
	}

	/**
	 * returns when all buffers are checked
	 */
	void check_buffer_done(int *msg_stats, int N)
	{
		int net_messages = msg_stats[1] - msg_stats[0]; // updates_processed - updates_created
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
		ckout << "Wasted updates normalized to V: " << (double) (msg_stats[0] - V) / V << endl;
		ckout << "Rejected updates: " << msg_stats[1] << endl;
		ckout << "Rejected updates normalized to V: " << (double) msg_stats[1] / V << endl;
		ckout << "Number of threshold changes: " << threshold_change_counter << endl;
		ckout << "Number of reductions: " << reduction_counts << endl;
		ckout << "Updates noted: " << msg_stats[3+histo_bucket_count] << endl;
		int vcount_sum = 0;
		ckout << "Vcount: [ ";
		for(int i=0; i<histo_bucket_count+1; i++)
		{
			ckout << msg_stats[i+2] << ", ";
			vcount_sum += msg_stats[i+2];
		}
		ckout << endl;
		ckout << "Vcount sum: " << vcount_sum << endl;
		arr.get_max_cost();
	}

	void done_max_cost(cost max_cost)
	{
		ckout << "Maximum vertex cost, not counting unreachable: " << max_cost << endl;
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
	int updates_created_locally=0; //number of update messages sent
	int updates_processed_locally=0; //number of update messages received
	int *partition_index; //defines boundaries of indices for each pe
	int wasted_updates=0; //number of updates that don't have the final answer
	int rejected_updates=0; //number of updates that don't decrease a distance value/create more messages
	tram_proxy_t tram_proxy;
	tram_t *tram; //tram library
	SharedInfo *shared_local;
	std::priority_queue<Update, std::vector<Update>, ComparePairs> pq; //heap of messages
	int *histogram; //local histogram of data, from 0 to max_size, divided into histo_bucket_count buckets
	int *vcount; //array of vertex distances, calculated with same formula as histogram
	int heap_threshold; //highest bucket where messages can be pushed to heap
	int tram_threshold; //highest bucket where messages can be pushed to tram
	double bucket_multiplier; //constant to calculate bucket
	std::vector<Update> *tram_hold; //hold buffer for messages not in tram limit
	std::vector<Update> *pq_hold; //hold for heap messages
	int bfs_created=0; //bfs created messages
	int bfs_processed=0; //bfs processed messages
	int updates_noted=0;
	int *dest_table; //destination table for faster pe calculation
	std::vector<Update> *bfs_hold; // bfs hold (control distance explosion due to bfs)
	int current_phase=0;

public:

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

	int get_dest_proc_fast(int vertex)
	{
		//look up x/M and 1+x/M
		int xm_pe, xm_plus_one_pe;
		int dest_table_index = vertex / M;
		// if this points to the end of dest_table
		if(dest_table_index >= (V / M) - 1)
		{
			xm_pe = dest_table[(V / M) - 1];
			int dest_proc = xm_pe;
			for (int j = xm_pe; j < N; j++)
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
		xm_pe = dest_table[dest_table_index];
		xm_plus_one_pe = dest_table[dest_table_index+1];
		int dest_proc = xm_pe;
		for (int j = xm_pe; j <= xm_plus_one_pe; j++)
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

	SsspChares(CkGroupID tram_id)
	{
		tram_proxy = CProxy_HTram(tram_id);
	}

	void initiate_pointers()
	{
		tram = tram_proxy.ckLocalBranch();
		tram->set_func_ptr_retarr(SsspChares::process_update_caller, this);
		shared_local = shared.ckLocalBranch();
	}

	bool idle_triggered()
	{
		process_heap();
		return true;
	}

	void generate_local_graph(int _num_vertices, int _num_edges, int *partition, int dividers)
	{
		#ifdef INFO_PRINTS
		ckout << "Generating local graph on PE " << CkMyPe() << " with " << _num_vertices << " vertices and " << _num_edges << " edges" << endl;
		#endif
		num_vertices = _num_vertices;
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
		dest_table = new int[V / M];
		for(int i=0, j=0; i < V; j++, i=j*M)
		{
			dest_table[j] = get_dest_proc(i);
		}
		local_graph = new Node[num_vertices];
		heap_threshold = initial_threshold;
		tram_threshold = initial_threshold + 2;
		tram_hold = new std::vector<Update>[histo_bucket_count];
		pq_hold = new std::vector<Update>[histo_bucket_count];
		bfs_hold = new std::vector<Update>[histo_bucket_count];
		bucket_multiplier = histo_bucket_count / (512 * log(V));
		CkCallWhenIdle(CkIndex_SsspChares::idle_triggered(), this);
		int remaining_edges = _num_edges;
		cost *largest_outedges = new cost[num_vertices];
		std::mt19937 generator(S);
		std::uniform_int_distribution<int> edge_count_distribution(0,2*average_degree);
		std::uniform_int_distribution<int> edge_dest_distribution(0,V);
		std::uniform_int_distribution<int> edge_weight_distribution(1,1000);
		for(int i=0; i<num_vertices; i++)
		{
			Node new_node;
			new_node.home_process = thisIndex;
			new_node.index = i + start_vertex;
			new_node.distance = lmax;
			new_node.send_updates = false;
			CkVec<Edge> adj;
			new_node.adjacent = adj;
			vcount[histo_bucket_count]++;
			int largest_outedge = 0;
			if(remaining_edges>0)
			{
				int num_edges = edge_count_distribution(generator);
				if((num_edges > remaining_edges) || (i == num_vertices - 1)) num_edges = remaining_edges;
				remaining_edges -= num_edges;
				int *edge_destinations = new int[num_edges];
				for(int i=0; i<num_edges; i++)
				{
					edge_destinations[i] = -1;
				}
				for(int i=0; i<num_edges; i++)
				{
					Edge new_edge;
					bool repeated = true;
					int candidate_end = edge_dest_distribution(generator);
					//logic to keep destinations different
					while(repeated)
					{
						bool different = true;
						for(int j=0; j<i; j++)
						{
							if(edge_destinations[j] == candidate_end)
							{
								different = false;
								break;
							}
						}
						if(different)
						{
							new_edge.end = candidate_end;
							repeated = false;
							edge_destinations[i] = candidate_end;
						}
						else candidate_end = edge_dest_distribution(generator);
					}
					new_edge.distance = edge_weight_distribution(generator);
					if(new_edge.distance > largest_outedge) largest_outedge = new_edge.distance;
					new_node.adjacent.insertAtEnd(new_edge);
				}
			}
			local_graph[i] = new_node;
			largest_outedges[i] = largest_outedge;
		}
		int max_edges_sum = 0;
		for(int i=0; i<num_vertices; i++)
		{
			max_edges_sum += largest_outedges[i];
		}
		CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
		contribute(sizeof(cost), &max_edges_sum, CkReduction::sum_long, cb);
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
		dest_table = new int[V / M];
		for(int i=0, j=0; i < V; j++, i=j*M)
		{
			dest_table[j] = get_dest_proc(i);
		}
		local_graph = new Node[num_vertices];
		heap_threshold = initial_threshold;
		tram_threshold = initial_threshold + 2;
		tram_hold = new std::vector<Update>[histo_bucket_count];
		pq_hold = new std::vector<Update>[histo_bucket_count];
		bfs_hold = new std::vector<Update>[histo_bucket_count];
		bucket_multiplier = histo_bucket_count / (512 * log(V));
		cost *largest_outedges = new cost[num_vertices];
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
		CkCallWhenIdle(CkIndex_SsspChares::idle_triggered(), this);
		//reduce largest edge
		cost max_edges_sum = 0;
		if (num_vertices != 0)
		{
			for(int i=0; i<num_vertices; i++)
			{
				max_edges_sum += largest_outedges[i];
			}
		}
		CkCallback cb(CkReductionTarget(Main, begin), mainProxy);
		contribute(sizeof(cost), &max_edges_sum, CkReduction::sum_long, cb);
	}

	/**
	 * Method that accepts initial update to source vertex
	*/
	void start_algo(Update new_vertex_and_distance)
	{
		process_update(new_vertex_and_distance);
		//arr[thisIndex].process_heap();
	}

	static void process_update_caller(void *p, Update *new_vertex_and_distances, int count)
	{
		//ckout << "PE " << CkMyPe() << " receiving " << count << " updates" << endl;
		for(int i=0; i<count; i++)
		{
			((SsspChares *)p)->process_update(new_vertex_and_distances[i]);
		}
		//((SsspChares *)p)->process_heap();

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
			updates_created_locally++;
			//if exceeds limit, put in hold
			if((neighbor_bucket > tram_threshold) && !bfs)
			{
				tram_hold[neighbor_bucket].push_back(new_update);
			}
			else
			{
				//calculated dest proc
				int dest_proc = get_dest_proc_fast(new_update.dest_vertex);
				if(dest_proc==CkMyPe())
				{
					process_update(new_update);
				}
				else tram->insertValue(new_update, dest_proc);
			}
		}
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
			updates_noted++;
			if(local_graph[local_index].distance == lmax)
			{ 
				local_graph[local_index].distance = this_cost;
				vcount[histo_bucket_count]--;
				if (this_bucket > heap_threshold)
				{
					local_graph[local_index].send_updates = true;
			      	bfs_hold[this_bucket].push_back(new_vertex_and_distance);
				}
				else
				{
					bfs_processed++;
					generate_updates(local_index, true);
					wasted_updates++;
					histogram[this_bucket]--;
					updates_processed_locally++;
				}
			}
			else
			{ 
				vcount[get_histo_bucket(local_graph[local_index].distance)]--;
				local_graph[local_index].distance = this_cost;
				local_graph[local_index].send_updates = true;
				if(this_bucket > heap_threshold)
				{
					pq_hold[this_bucket].push_back(new_vertex_and_distance);
				}
				else pq.push(new_vertex_and_distance);
			}
		}
		else
		{
			wasted_updates++;
			rejected_updates++;
			histogram[this_bucket]--;
			updates_noted++;
			updates_processed_locally++;
		}
		
	}

	/**
	 * Update distances, but locally (the incoming pair comes from this PE)
	 * this is not an entry method
	 * returns true (runs when pe is idle)
	 */
	void process_heap()
	{
		int heap_count = 0;
		while (pq.size() > 0)
		{
			if (++heap_count > 500) { thisProxy[thisIndex].process_heap(); break; } // give other eps a chance to run  
			Update new_vertex_and_distance = pq.top();
			int dest_vertex = new_vertex_and_distance.dest_vertex;
			cost new_distance = new_vertex_and_distance.distance;
			int this_histo_bucket = get_histo_bucket(new_distance);
			if(this_histo_bucket > heap_threshold)
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
			wasted_updates++;
			histogram[this_histo_bucket]--;
			updates_processed_locally++;
		}
		//return true;
	}


	/**
	 * Checks if anything is in the buffer (false quiescence)
	 */
	void check_buffer()
	{
		// ckout << "Checking message stats" << endl;
		int msg_stats[2];
		msg_stats[0] = updates_created_locally;
		msg_stats[1] = updates_processed_locally;
		CkCallback cb(CkReductionTarget(Main, check_buffer_done), mainProxy);
		contribute(2 * sizeof(int), msg_stats, CkReduction::sum_int, cb);
	}

	/**
	 * Contribute to a reduction to get the overall histogram to pe 0/main chare
	*/
	void contribute_histogram(int behind_first_nonzero)
	{
		int donecount = 0;
		traceUserEvent(shared_local -> event_id);
		CkCallback cb(CkReductionTarget(Main, reduce_histogram), mainProxy);
		int *info_array = new int[histo_bucket_count+4];
		std::copy(histogram, histogram + histo_bucket_count, info_array);
		info_array[histo_bucket_count] = updates_created_locally;
		info_array[histo_bucket_count+1] = updates_processed_locally;
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
			for(int j=0; j<tram_hold[i].size(); j++)
			{
				int dest_proc = get_dest_proc_fast(tram_hold[i][j].dest_vertex);
				if(dest_proc==CkMyPe())
				{
					pq.push(tram_hold[i][j]);
				}
				else tram->insertValue(tram_hold[i][j], dest_proc);
			}
			tram_hold[i].clear();
		}
		for(int i=0; i<histo_bucket_count; i++)
		{
			for(int j=0; j<pq_hold[i].size(); j++)
			{
				pq.push(pq_hold[i][j]);
			}
			pq_hold[i].clear();
		}
		tram->tflush();
		//arr[thisIndex].process_heap();
	}

	/**
	 * Broadcasts bucket limit
	*/
	void current_thresholds(int _heap_threshold, int _tram_threshold, int behind_first_nonzero, int phase)
	{
		//if(CkMyPe()==17) ckout << "Wasted on PE 17 = " << wasted_updates << " Rejected= " << rejected_updates << " Time: " << CkWallTimer() << endl;
		heap_threshold = _heap_threshold;
		tram_threshold = _tram_threshold;
		current_phase = phase;
		int counter = 0;
		//ckout << "Time when threshold received: " << CkWallTimer() << " PE: " << CkMyPe() << " size: " << tram_hold.size() << endl;
		//after every reduction, push out messages in hold that are in limit
		for(int i=0; i<=tram_threshold; i++)
		{
			for(int j=0; j<tram_hold[i].size(); j++)
			{
				int dest_proc = get_dest_proc_fast(tram_hold[i][j].dest_vertex);
				if(dest_proc==CkMyPe())
				{
					process_update(tram_hold[i][j]);
				}
				else tram->insertValue(tram_hold[i][j], dest_proc);
			}
			tram_hold[i].clear();
		}
		for(int i=0; i<=heap_threshold; i++)
		{
			for(int j=0; j<pq_hold[i].size(); j++)
			{
				pq.push(pq_hold[i][j]);
			}
			pq_hold[i].clear();
		}
		for(int i=0; i<=heap_threshold; i++)
		{
			for(int j=0; j<bfs_hold[i].size(); j++)
			{ 
				int local_index = bfs_hold[i][j].dest_vertex - start_vertex;
				int this_cost = bfs_hold[i][j].distance;
				int this_bucket = get_histo_bucket(this_cost);
				if (this_cost == local_graph[local_index].distance)
				{
					generate_updates(local_index, true);
				}
				else if (this_cost > local_graph[local_index].distance)
				{
					rejected_updates++;
				}
				bfs_processed++;
				wasted_updates++;
				histogram[this_bucket]--;
				updates_processed_locally++;
			}
			bfs_hold[i].clear();
		}
		//ckout << "Timer: " << CkWallTimer() << " PE: " << CkMyPe() << " size: " << tram_hold.size() << " count: " << counter << endl;
		tram->tflush();
		arr[thisIndex].process_heap();
		contribute_histogram(behind_first_nonzero);
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
			ckout << "Partition " << thisIndex << " vertex num " << local_graph[i] << " distance " << local_graph[i].distance << endl;
		}
		*/
		int msg_stats[4+histo_bucket_count];
		msg_stats[0] = wasted_updates;
		msg_stats[1] = rejected_updates;
		for(int i=0; i<histo_bucket_count + 1; i++)
		{
			msg_stats[i+2] = vcount[i];
		}
		msg_stats[3+histo_bucket_count] = updates_noted;
		CkCallback cb(CkReductionTarget(Main, done), mainProxy);
		contribute((4+histo_bucket_count) * sizeof(int), msg_stats, CkReduction::sum_int, cb);
		// mainProxy.done();
	}

	void get_max_cost()
	{
		cost max_cost = 0;
		for (int i = 0; i < num_vertices; i++)
		{
			cost vertex_cost = local_graph[i].distance;
			if(vertex_cost != lmax)
			{
				if(vertex_cost > max_cost)
				{
					max_cost = vertex_cost;
				}
			}
		}
		CkCallback cb(CkReductionTarget(Main, done_max_cost), mainProxy);
		contribute(sizeof(cost), &max_cost, CkReduction::max_long, cb);
	}

	void stop_periodic_flush()
	{
		tram->stop_periodic_flush();
	}
};


#include "weighted_htram_smp.def.h"
