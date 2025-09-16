# Change Charm build locations based on where the charm build is
CHARMC =/ccs/home/rrao/charm/ofi-linux-x86_64-cxi-slurmpmi2cray-gcc/bin/charmc $(OPTS)
CHARMC_SMP =/Users/ritvik/charm/bin/charmc $(OPTS)
# delta: /work/hdd/rao1/charm/ofi-linux-x86_64-cxi-slurmpmi2cray-smp-gcc/bin/charmc
# frontier: /ccs/home/rrao/charm/ofi-linux-x86_64-cxi-slurmpmi2cray-smp-gcc/bin/charmc
# local: /Users/ritvik/charm/netlrts-darwin-x86_64-smp/bin/charmc

CHARMCFLAGS = $(OPTS) -g -O3

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

sssp_nonsmp: sssp_nonsmp.cpp sssp_nonsmp.ci weighted_node_struct.h libtramnonsmp.a
	$(CHARMC) sssp_nonsmp.ci -DTRAM_NON_SMP
	$(CHARMC) $(CHARMCFLAGS) libtramnonsmp.a -language charm++ -o $@ $< -std=c++1z -DTRAM_NON_SMP

sssp_nonsmp_projections: sssp_nonsmp.cpp sssp_nonsmp.ci weighted_node_struct.h libtramnonsmp.a
	$(CHARMC) sssp_nonsmp.ci -DTRAM_NON_SMP
	$(CHARMC) $(CHARMCFLAGS) libtramnonsmp.a -language charm++ -o $@ $< -std=c++1z -DTRAM_NON_SMP -tracemode projections

sssp_smp: sssp_smp.cpp sssp_smp.ci weighted_node_struct.h libhtram_group.a
	$(CHARMC_SMP) $(CHARMCFLAGS) sssp_smp.ci -DTRAM_SMP -DGROUPBY
	$(CHARMC_SMP) $(CHARMCFLAGS) libhtram_group.a -language charm++ -o $@ $< -std=c++1z -DTRAM_SMP -DGROUPBY

sssp_smp_papi: sssp_smp.cpp sssp_smp.ci weighted_node_struct.h libhtram_group.a
	$(CHARMC_SMP) sssp_smp.ci -DTRAM_SMP -DGROUPBY
	$(CHARMC_SMP) $(CHARMCFLAGS) libhtram_group.a -language charm++ -o $@ $< -std=c++1z -DTRAM_SMP -DGROUPBY -I/opt/cray/pe/papi/7.0.1.2/include -L/opt/cray/pe/papi/7.0.1.2/lib -lpapi

sssp_smp_projections: sssp_smp.cpp sssp_smp.ci weighted_node_struct.h libhtram_group.a
	$(CHARMC_SMP) sssp_smp.ci -DTRAM_SMP -DGROUPBY
	$(CHARMC_SMP) $(CHARMCFLAGS) libhtram_group.a -language charm++ -o $@ $< -std=c++1z -DTRAM_SMP -DGROUPBY -tracemode projections

libtramnonsmp.a : tramNonSmp.o
	$(CHARMC) tramNonSmp.o -o libtramnonsmp.a -language charm++

tramNonSmp.o : tramNonSmp.C tramNonSmp.def.h tramNonSmp.decl.h
	$(CHARMC) -c tramNonSmp.C -o tramNonSmp.o

tramNonSmp.def.h tramNonSmp.decl.h : tramNonSmp.ci
	$(CHARMC) tramNonSmp.ci

libhtram_group.a: htram_group.o
	$(CHARMC_SMP) $(CHARMCFLAGS) $< -o $@ -language charm++ -tracemode projections

htram_group.o: htram_group.C htram_group.def.h htram_group.decl.h
	$(CHARMC_SMP) $(CHARMCFLAGS) -c htram_group.C

htram_group.def.h htram_group.decl.h: htram_group.ci
	$(CHARMC_SMP) $(CHARMCFLAGS) htram_group.ci

clean:
	$(MAKE) clean-libs
	rm -f *.o *.decl.h *.def.h $(BINARY) charmrun* *.stamp
	rm -rf sssp_Smp_projections

remove-out:
	rm -f *.out

clean-libs:
	rm -f *.def.h *.decl.h
	rm -f *.log.gz *.projrc *.topo *.sts *.sum
	rm -f *.a
