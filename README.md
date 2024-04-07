# charm_graph_code
Code for graph algorithms in charm++.

Implemented algorithms:
* Dijkstra's single-source shortest path
** Standard TRAM non-smp
** Standard TRAM smp
** HTRAM non-smp
** HTRAM smp

Dependencies: Charm++ (github.com/UIUC-PPL/charm)

Build:

```
make weighted_nonsmp
make weighted_smp
make weighted_htram_nonsmp
make weighted_htram_smp
```

Clean:
```
make clean
```

Run:
```
./charmrun +pN ./<executable> <num_chares> <num_vertices> <graph_file_name> <random_seed> <source_vertex>
```

Currently, the code accepts a list of edges, sorted by the source index of edge (ascending).
Edge weights used by the algorithm are randomly generated integers from 1 to 10 inclusive, based on the provided seed