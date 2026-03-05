# Change Charm build locations based on where the charm build is
CHARMC =/ccs/home/rrao/charm/ofi-linux-x86_64-cxi-slurmpmi2cray-gcc/bin/charmc $(OPTS)
CHARMC_SMP =/Users/ritvik/clean_charm/charm/bin/charmc $(OPTS)
# delta: /work/hdd/rao1/charm/ofi-linux-x86_64-cxi-slurmpmi2cray-smp-gcc/bin/charmc
# frontier: /ccs/home/rrao/charm/ofi-linux-x86_64-cxi-slurmpmi2cray-smp-gcc/bin/charmc
# local: /Users/ritvik/charm/netlrts-darwin-x86_64-smp/bin/charmc

CHARMCFLAGS = $(OPTS) -g -O3

HTRAM_DIR = /Users/ritvik/htram

# Flags required by any file that includes htram_group.h for the GRAPH/SSSP variant.
# These must match what libhtram_group_graph.a was compiled with.
GRAPH_FLAGS = -DGRAPH -DBUCKETS_BY_DEST -DHTRAM_GRAPH_TYPES_HEADER=\"$(CURDIR)/weighted_node_struct.h\"

BINARY= graph_ckio graph_parallel graph_parallel_ckio weighted_nonsmp weighted_smp sssp_nonsmp sssp_smp sssp_nonsmp_projections sssp_smp_projections sssp_smp_papi
all: $(BINARY)

.PHONY = clean

run_sssp_smp: sssp_smp
	./sssp_smp 10000 160000 100 1 1 0.999 0.005 +p8 +ppn 8 ++local

graph_serial: graph_serial.cpp
	$(CHARMC) graph_serial.ci
	$(CHARMC) graph_serial.cpp -o $@

graph_ckio: graph_ckio.cpp
	$(CHARMC) graph_ckio.ci
	$(CHARMC) graph_ckio.cpp -o $@ -module CkIO

graph_parallel: graph_parallel.cpp
	$(CHARMC) graph_parallel.ci
	$(CHARMC) graph_parallel.cpp -o $@

graph_parallel_ckio: graph_parallel_ckio.cpp
	$(CHARMC) graph_parallel_ckio.ci
	$(CHARMC) graph_parallel_ckio.cpp -o $@ -module CkIO

weighted_nonsmp: weighted.cpp weighted.ci weighted_node_struct.h
	$(CHARMC) weighted.ci
	$(CHARMC) $(CHARMCFLAGS) $< -o $@ -module NDMeshStreamer

weighted_smp: weighted.cpp weighted.ci weighted_node_struct.h
	$(CHARMC_SMP) weighted.ci
	$(CHARMC_SMP) $(CHARMCFLAGS) $< -o $@ -module NDMeshStreamer

sssp_nonsmp: sssp_nonsmp.cpp sssp_nonsmp.ci weighted_node_struct.h $(HTRAM_DIR)/libtramnonsmp.a
	$(CHARMC) sssp_nonsmp.ci -DTRAM_NON_SMP -I$(HTRAM_DIR)
	$(CHARMC) $(CHARMCFLAGS) $(HTRAM_DIR)/libtramnonsmp.a -language charm++ -o $@ $< -std=c++1z -DTRAM_NON_SMP -I$(HTRAM_DIR)

sssp_nonsmp_projections: sssp_nonsmp.cpp sssp_nonsmp.ci weighted_node_struct.h $(HTRAM_DIR)/libtramnonsmp.a
	$(CHARMC) sssp_nonsmp.ci -DTRAM_NON_SMP -I$(HTRAM_DIR)
	$(CHARMC) $(CHARMCFLAGS) $(HTRAM_DIR)/libtramnonsmp.a -language charm++ -o $@ $< -std=c++1z -DTRAM_NON_SMP -tracemode projections -I$(HTRAM_DIR)

sssp_smp: sssp_smp.cpp sssp_smp.ci weighted_node_struct.h $(HTRAM_DIR)/libhtram_group_graph.a
	$(CHARMC_SMP) $(CHARMCFLAGS) sssp_smp.ci -DTRAM_SMP -DGROUPBY $(GRAPH_FLAGS) -I$(HTRAM_DIR)
	$(CHARMC_SMP) $(CHARMCFLAGS) $(HTRAM_DIR)/libhtram_group_graph.a -language charm++ -o $@ $< -std=c++1z -DTRAM_SMP -DGROUPBY $(GRAPH_FLAGS) -I$(HTRAM_DIR)

sssp_smp_papi: sssp_smp.cpp sssp_smp.ci weighted_node_struct.h $(HTRAM_DIR)/libhtram_group_graph.a
	$(CHARMC_SMP) sssp_smp.ci -DTRAM_SMP -DGROUPBY $(GRAPH_FLAGS) -I$(HTRAM_DIR)
	$(CHARMC_SMP) $(CHARMCFLAGS) $(HTRAM_DIR)/libhtram_group_graph.a -language charm++ -o $@ $< -std=c++1z -DTRAM_SMP -DGROUPBY $(GRAPH_FLAGS) -I$(HTRAM_DIR) -I/opt/cray/pe/papi/7.0.1.2/include -L/opt/cray/pe/papi/7.0.1.2/lib -lpapi

sssp_smp_projections: sssp_smp.cpp sssp_smp.ci weighted_node_struct.h $(HTRAM_DIR)/libhtram_group_graph.a
	$(CHARMC_SMP) sssp_smp.ci -DTRAM_SMP -DGROUPBY $(GRAPH_FLAGS) -I$(HTRAM_DIR)
	$(CHARMC_SMP) $(CHARMCFLAGS) $(HTRAM_DIR)/libhtram_group_graph.a -language charm++ -o $@ $< -std=c++1z -DTRAM_SMP -DGROUPBY $(GRAPH_FLAGS) -tracemode projections -I$(HTRAM_DIR)

# Build the htram graph library in the htram repo, pointing it at this directory
# for the weighted_node_struct.h header.
$(HTRAM_DIR)/libhtram_group_graph.a:
	$(MAKE) -C $(HTRAM_DIR) libhtram_group_graph.a \
	    GRAPH_INCLUDE=$(CURDIR) \
	    CHARMC_SMP="$(CHARMC_SMP)" OPTS="$(OPTS)"

# Build the non-SMP tram library in the htram repo.
$(HTRAM_DIR)/libtramnonsmp.a:
	$(MAKE) -C $(HTRAM_DIR) libtramnonsmp.a \
	    CHARMC="$(CHARMC)" OPTS="$(OPTS)"

.PHONY: $(HTRAM_DIR)/libhtram_group_graph.a $(HTRAM_DIR)/libtramnonsmp.a

clean:
	rm -f *.o *.decl.h *.def.h $(BINARY) charmrun* *.stamp

remove-out:
	rm -f *.out