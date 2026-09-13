// Compile against a pinned, unmodified GAPBS checkout. Only its main is replaced.
#define main gapbs_original_main
#include "sssp.cc"
#undef main
#include "common.h"
#include <omp.h>
#include <chrono>

int main(int argc,char **argv) try {
  if(argc!=4) throw std::runtime_error("usage: gap_sssp graph.wsg source delta");
  const int source=std::stoi(argv[2]),delta=std::stoi(argv[3]);
  char a0[]="gap_sssp",a1[]="-f";
  char *args[]={a0,a1,argv[1]};
  CLDelta<WeightT> cli(3,args,"SSSP comparison");
  if(!cli.ParseArgs()) return 1;
  WeightedBuilder builder(cli); WGraph g=builder.MakeGraph();
  if(source<0||source>=g.num_nodes()||delta<=0) return 1;
  const double start=omp_get_wtime();
  auto distances=DeltaStep(g,source,delta,false);
  const double elapsed=omp_get_wtime()-start;
  bench::Digest digest;
  for(int64_t v=0;v<g.num_nodes();++v)
    digest.add(v,distances[v]==kDistInf?bench::inf:distances[v]);
  bench::print(source,elapsed,digest);
  return 0;
} catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
