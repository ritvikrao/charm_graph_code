# Where charmc lives. Machine-specific, so it is overridable three ways, in
# increasing precedence: the default below, an untracked config.mk in this
# directory, and a variable on make's command line. Before this the path was
# edited in place, which meant every machine carried an uncommittable local
# diff to a tracked file.
#
#   Delta:    /u/rao1/charm_reconverse/bin/charmc
#   Frontier: /ccs/home/rrao/charm_reconverse/bin/charmc
CHARMC_SMP ?= charmc
HTRAM_DIR  ?= ../htram
-include config.mk

CHARMCFLAGS = $(OPTS) -g -O3

# Flags required by any file that includes htram_group.h for the GRAPH/SSSP
# variant. These must match what libhtram_group_graph.a was compiled with.
GRAPH_FLAGS = -DGRAPH -DBUCKETS_BY_DEST -DHTRAM_GRAPH_TYPES_HEADER=\"$(CURDIR)/weighted_node_struct.h\"
SSSP_FLAGS  = $(CHARMCFLAGS) -DTRAM_SMP -DGROUPBY $(GRAPH_FLAGS) -I$(CURDIR) -I$(HTRAM_DIR)

BINARY = sssp_smp sssp_smp_diag sssp_smp_projections sssp_smp_papi
all: sssp_smp

.PHONY: all clean remove-out $(HTRAM_DIR)/libhtram_group_graph.a

run_sssp_smp: sssp_smp
	./sssp_smp 10000 160000 100 1 1 0.999 0.005 +p8 +ppn 8 ++local

SSSP_SRC = sssp_smp.cpp sssp_smp.ci weighted_node_struct.h \
           $(wildcard graphlib/*.h) $(wildcard graphlib/*.C)

sssp_smp: $(SSSP_SRC) $(HTRAM_DIR)/libhtram_group_graph.a
	$(CHARMC_SMP) $(SSSP_FLAGS) sssp_smp.ci
	$(CHARMC_SMP) $(SSSP_FLAGS) $(HTRAM_DIR)/libhtram_group_graph.a -language charm++ -o $@ sssp_smp.cpp -std=c++1z

# The diagnosis build for step 6 of the SC27 plan. It adds a cumulative
# per-bucket creation profile, a per-PE work line, and the batch-local
# combining ceiling -- all of which cost work on the relaxation path, which is
# why they are a separate binary. Everything it measures is a property of the
# graph and the algorithm rather than of the clock, so the structural numbers
# come from here and the timed A/Bs come from sssp_smp.
sssp_smp_diag: $(SSSP_SRC) $(HTRAM_DIR)/libhtram_group_graph.a
	$(CHARMC_SMP) $(SSSP_FLAGS) sssp_smp.ci
	$(CHARMC_SMP) $(SSSP_FLAGS) $(HTRAM_DIR)/libhtram_group_graph.a -language charm++ -o $@ sssp_smp.cpp -std=c++1z -DACIC_DIAG -DVCOUNT

sssp_smp_papi: $(SSSP_SRC) $(HTRAM_DIR)/libhtram_group_graph.a
	$(CHARMC_SMP) $(SSSP_FLAGS) sssp_smp.ci
	$(CHARMC_SMP) $(SSSP_FLAGS) $(HTRAM_DIR)/libhtram_group_graph.a -language charm++ -o $@ sssp_smp.cpp -std=c++1z -DPAPI -I/opt/cray/pe/papi/7.0.1.2/include -L/opt/cray/pe/papi/7.0.1.2/lib -lpapi

sssp_smp_projections: $(SSSP_SRC) $(HTRAM_DIR)/libhtram_group_graph.a
	$(CHARMC_SMP) $(SSSP_FLAGS) sssp_smp.ci
	$(CHARMC_SMP) $(SSSP_FLAGS) $(HTRAM_DIR)/libhtram_group_graph.a -language charm++ -o $@ sssp_smp.cpp -std=c++1z -tracemode projections

# Standalone tools. These build without Charm++ so they can run anywhere,
# including in CI and on a reviewer's laptop.
TOOL_FLAGS = -O2 -std=c++17 -DGRAPH_GEN_STANDALONE -I$(CURDIR)

graph_digest: tools/graph_digest.cpp $(wildcard graphlib/*.h)
	$(CXX) $(TOOL_FLAGS) $< -o $@

graph_convert: tools/graph_convert.cpp $(wildcard graphlib/*.h)
	$(CXX) $(TOOL_FLAGS) $< -o $@

tools: graph_digest graph_convert

# Build the htram graph library in the htram repo, pointing it at this
# directory for weighted_node_struct.h.
$(HTRAM_DIR)/libhtram_group_graph.a:
	$(MAKE) -C $(HTRAM_DIR) libhtram_group_graph.a \
	    GRAPH_INCLUDE=$(CURDIR) \
	    CHARMC_SMP="$(CHARMC_SMP)" OPTS="$(OPTS)"

clean:
	rm -f *.o *.decl.h *.def.h $(BINARY) graph_digest graph_convert charmrun* *.stamp

remove-out:
	rm -f *.out
