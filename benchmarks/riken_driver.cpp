// Driver around the unmodified RIKEN distributed solver. Reads the same .wsg
// as GAPBS/ACIC; normalization by a power of two is exact in binary32.
#include <mpi.h>
#include <limits>
#include <iostream>
#include "parameters.h"
#include "utils.hpp"
#include "primitives.hpp"
#include "graph_generator.hpp"
#include "graph_constructor.hpp"
#include "sssp.hpp"
#include "sssp_presol.hpp"
#include "common.h"

int main(int argc,char **argv) {
  if(argc!=6){std::cerr<<"usage: riken_sssp graph.wsg source delta_integer denominator presolve_iterations\n";return 2;}
  try {
    const std::string path=argv[1]; const bench::Header header(path);
    if(header.directed) throw std::runtime_error("RIKEN input must be undirected");
    const int64_t source=std::stoll(argv[2]), denominator=std::stoll(argv[4]);
    const double delta=std::stod(argv[3])/denominator;
    const int presolve_iterations=std::stoi(argv[5]);
    if(source<0||source>=header.n||denominator<=0||(denominator&(denominator-1))||delta<=0||delta>1)
      throw std::runtime_error("invalid source, normalization, or delta");
    char value[64];std::snprintf(value,sizeof(value),"%.17g",delta);setenv("DELTA_STEP",value,1);
    int scale=1;while((INT64_C(1)<<scale)<header.n)++scale;
    setup_globals(argc,argv,scale,16);
    {
      const int64_t first=header.n*mpi.rank_2d/mpi.size_2d;
      const int64_t last=header.n*(mpi.rank_2d+1)/mpi.size_2d;
      std::ifstream input(path,std::ios::binary);
      std::vector<int64_t> offsets(last-first+1);
      input.seekg(17+first*8);bench::read(input,offsets.data(),offsets.size());
      EdgeListStorage<WeightedEdge, 65536> list(offsets.back()-offsets.front());
      list.beginWrite();
      std::vector<bench::Edge> row;
      std::vector<WeightedEdge> pending;pending.reserve(65536);
      input.seekg(17+(header.n+1)*8+offsets.front()*8);
      for(int64_t u=first;u<last;++u){
        row.resize(offsets[u-first+1]-offsets[u-first]);bench::read(input,row.data(),row.size());
        for(const auto &e:row) if(u<e.v){
          if(e.w<=0||e.w>denominator) throw std::runtime_error("weight outside normalization range");
          WeightedEdge edge;edge.set(u,e.v,float(e.w)/float(denominator));pending.push_back(edge);
          if(pending.size()==65536){list.write(pending.data(),pending.size());pending.clear();}
        }
      }
      if(!pending.empty())list.write(pending.data(),pending.size());
      list.endWrite();
      SsspBase solver;
      double construction=MPI_Wtime();solver.construct(&list);construction=MPI_Wtime()-construction;
      const int64_t local_n=solver.graph_.pred_size();
      auto *pred=static_cast<int64_t*>(cache_aligned_xmalloc(local_n*sizeof(int64_t)));
      auto *dist=static_cast<float*>(cache_aligned_xmalloc(local_n*sizeof(float)));
      solver.prepare_sssp();
      double preprocessing=0;
      if(presolve_iterations>0){
        SsspPresolver presolver(solver);preprocessing=MPI_Wtime();
        presolver.presolve_sssp(presolve_iterations,pred,dist);preprocessing=MPI_Wtime()-preprocessing;
      }
      MPI_Barrier(mpi.comm_2d);double start=MPI_Wtime();
      // Marks the solve for a preloaded mpi_share.so (7.6o); a no-op otherwise.
      MPI_Pcontrol(1);
      solver.run_sssp(source,pred,dist);
      MPI_Pcontrol(0);
      double seconds=MPI_Wtime()-start,max_seconds=0;
      MPI_Reduce(&seconds,&max_seconds,1,MPI_DOUBLE,MPI_MAX,0,mpi.comm_2d);
      bench::Digest local;
      int bad=0;
      for(int64_t i=0;i<local_n;++i){
        const int64_t v=i*mpi.size_2d+mpi.rank_2d;if(v>=header.n)continue;
        if(dist[i]>=comp::infinity){local.add(v,bench::inf);continue;}
        const double integer=double(dist[i])*denominator;
        if(integer<0||integer>=16777216||integer!=std::floor(integer)){bad=1;continue;}
        local.add(v,int64_t(integer));
      }
      MPI_Allreduce(MPI_IN_PLACE,&bad,1,MPI_INT,MPI_MAX,mpi.comm_2d);
      if(bad) throw std::runtime_error("non-exact or out-of-range RIKEN distance");
      uint64_t values[4]={local.h1,local.h2,local.reachable,local.distance_sum},total[4]={};
      MPI_Reduce(values,total,4,MPI_UINT64_T,MPI_SUM,0,mpi.comm_2d);
      if(mpi.isMaster()){
        bench::Digest digest;digest.h1=total[0];digest.h2=total[1];digest.reachable=total[2];digest.distance_sum=total[3];
        std::printf("SETUP construction_seconds=%.9f presolve_seconds=%.9f presolve_iterations=%d\n",construction,preprocessing,presolve_iterations);
        bench::print(source,max_seconds,digest);
      }
      solver.end_sssp();free(pred);free(dist);
    }
    cleanup_globals();
    return 0;
  }catch(const std::exception &e){
    std::cerr<<e.what()<<'\n';int initialized=0;MPI_Initialized(&initialized);
    if(initialized)MPI_Abort(MPI_COMM_WORLD,1);
    return 1;
  }
}
