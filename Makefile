# Change Charm build locations based on where the charm build is
CHARMC =/u/rao1/charm/mpi-linux-x86_64/bin/charmc $(OPTS)
CHARMC_SMP =/u/rao1/charm/mpi-linux-x86_64-smp/bin/charmc $(OPTS)

CHARMCFLAGS = $(OPTS) -O3

BINARY=weighted_nonsmp weighted_smp weighted_htram_nonsmp weighted_htram_nonsmp
all: $(BINARY)

.PHONY = clean

weighted_nonsmp: weighted.cpp weighted.ci weighted_node_struct.h
	$(CHARMC) weighted.ci
	$(CHARMC) $(CHARMCFLAGS) $< -o $@ -module NDMeshStreamer

weighted_smp: weighted.cpp weighted.ci weighted_node_struct.h
	$(CHARMC_SMP) weighted.ci
	$(CHARMC_SMP) $(CHARMCFLAGS) $< -o $@ -module NDMeshStreamer

weighted_htram_nonsmp: weighted_htram.cpp weighted_htram.ci weighted_node_struct.h libtramnonsmp.a
	$(CHARMC) weighted_htram.ci -DTRAM_NON_SMP
	$(CHARMC) $(CHARMCFLAGS) libtramnonsmp.a -language charm++ weighted_htram.cpp -std=c++1z -DTRAM_NON_SMP

weighted_htram_smp: weighted_htram.cpp weighted_htram.ci weighted_node_struct.h libhtram_group.a
	$(CHARMC_SMP) weighted_htram.ci -DTRAM_SMP -DGROUPBY
	$(CHARMC) $(CHARMCFLAGS) libhtram_group.a -language charm++ -o $@ $< -std=c++1z -DTRAM_SMP -DGROUPBY

libtramnonsmp.a : tramNonSmp.o
	$(CHARMC) tramNonSmp.o -o libtramnonsmp.a -language charm++

tramNonSmp.o : tramNonSmp.C tramNonSmp.def.h tramNonSmp.decl.h
	$(CHARMC) -c tramNonSmp.C -o tramNonSmp.o -g

tramNonSmp.def.h tramNonSmp.decl.h : tramNonSmp.ci
	$(CHARMC) tramNonSmp.ci

libhtram_group.a: htram_group.o
	$(CHARMC_SMP) $< -o $@ -language charm++

htram_group.o: htram_group.C htram_group.def.h htram_group.decl.h
	$(CHARMC_SMP) -c htram_group.C

htram_group.def.h htram_group.decl.h: htram_group.ci
	$(CHARMC_SMP) htram_group.ci

clean:
	$(MAKE) clean-libs
	rm -f *.o *.decl.h *.def.h $(BINARY) charmrun* *.stamp
	rm histo_nonsmp
	rm histo_smp

clean-libs:
	rm -f *.def.h *.decl.h
	rm -f *.log.gz *.projrc *.topo *.sts *.sum
	rm libhtram.a libtramnonsmp.a