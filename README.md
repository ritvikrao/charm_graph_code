# charm_graph_code
Code for graph algorithms in charm++.

Implemented algorithms:
* Dijkstra's single-source shortest path
    * Standard TRAM non-smp
    * Standard TRAM smp
    * HTRAM non-smp
    * HTRAM smp
Also, this repo includes code for reading edge lists using CkIO, and just a sequential read based on ifstream.

Dependencies: Charm++ (github.com/charmplusplus/charm)

Build:

```
make weighted_nonsmp
make weighted_smp
make weighted_htram_nonsmp
make weighted_htram_smp
make graph_ckio
make graph_serial
```

Clean:
```
make clean
```

Run weighted_htram_nonsmp:
```
./charmrun +p<N> ./<executable> <num_chares> <num_vertices> <graph_file_name> <random_seed> <source_vertex>
```

Run weighted_htram_smp (read files in, option 0):
```
./charmrun +p<N> +ppn <threads> ./<executable> <num_vertices> <graph_file_name> <random_seed> <source_vertex> <create_option> <p_tram> <p_pq>
```

Run weighted_htram_smp (automatically generate graph, option 1 or 2):
```
./charmrun +p<N> +ppn <threads> ./<executable> <num_vertices> <num_edges> <random_seed> <source_vertex> <create_option> <p_tram> <p_pq>
```


Run graph_ckio:
```
./charmrun +pN ./graph_ckio <num_chares> <num_vertices> <graph_file_name> <random_seed>
```

Run graph_serial:
```
./charmrun +p1 ./graph_serial <num_vertices> <graph_file_name> <random_seed>
```

Currently, the code accepts a list of edges, sorted by the source index of edge (ascending).
Edge weights used by the algorithm are randomly generated integers from 1 to 10 inclusive, based on the provided seed
Get num_vertices by scrolling down to the bottom of the edge list you're using
