// Produce a canonical undirected, minimum-parallel-edge benchmark graph.
// Topology generation reuses graphlib; validation uses GAPBS's independent
// reader and serial Dijkstra (reference_gap.cpp).
#include "common.h"
#include "graphlib/graphlib.h"
#include <iostream>
#include <sstream>
#include <tuple>

struct PairEdge { int32_t u,v,w; };
int main(int argc,char **argv) try {
  if(argc!=6) throw std::runtime_error("usage: prepare_graph gen|dimacs|snap mode_or_n size seed output.wsg (text on stdin)");
  const std::string kind=argv[1], out=argv[5];
  int64_t n=std::stoll(argv[2]);
  const int64_t size=std::stoll(argv[3]);
  const int seed=std::stoi(argv[4]);
  std::vector<PairEdge> input;
  auto add=[&](int64_t u,int64_t v,int64_t w){
    if(u<0||v<0||u>=n||v>=n||w<=0||w>INT32_MAX)
      throw std::runtime_error("invalid input edge");
    if(u!=v) input.push_back({int32_t(std::min(u,v)),int32_t(std::max(u,v)),int32_t(w)});
  };
  if(kind=="gen") {
    GraphSpec spec; spec.mode=int(n); n=size;
    spec.num_vertices=n; spec.num_edges=n*16; spec.average_degree=16;
    spec.seed=seed; spec.weights=WeightAssigner(seed);
    LocalCsr g; build_full_csr<LongEdge>(spec,g);
    input.reserve(g.num_edges());
    for(int64_t u=0;u<n;++u)
      for(long j=0;j<g.degree(u);++j) add(u,g.edges(u)[j].end,g.edges(u)[j].distance);
  } else {
    std::string line;
    while(std::getline(std::cin,line)) {
      if(line.empty()||line[0]=='#'||line[0]=='c') continue;
      std::istringstream s(line);
      int64_t u,v,w;
      if(kind=="dimacs") {
        char type; s>>type;
        if(type=='p') { std::string sp; int64_t m; s>>sp>>n>>m; input.reserve(m); continue; }
        if(type!='a'||!(s>>u>>v>>w)) throw std::runtime_error("invalid DIMACS row");
        add(u-1,v-1,w);
      } else if(kind=="snap") {
        if(!(s>>u>>v)) throw std::runtime_error("invalid SNAP row");
        // SNAP labels need not be contiguous. Compact them after reading.
        if(u<0||v<0||u>INT32_MAX||v>INT32_MAX) throw std::runtime_error("SNAP ID outside int32");
        w=1+bench::mix(bench::mix(std::min(u,v)^uint64_t(seed)) ^ uint64_t(std::max(u,v)))%1000;
        if(u!=v) input.push_back({int32_t(std::min(u,v)),int32_t(std::max(u,v)),int32_t(w)});
      } else throw std::runtime_error("unknown graph kind");
    }
  }
  if(kind=="snap") {
    std::vector<int32_t> ids;ids.reserve(input.size()*2);
    for(auto e:input){ids.push_back(e.u);ids.push_back(e.v);}
    std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    if(int64_t(ids.size())!=n) throw std::runtime_error("SNAP vertex count differs from expected");
    for(auto &e:input){e.u=std::lower_bound(ids.begin(),ids.end(),e.u)-ids.begin();e.v=std::lower_bound(ids.begin(),ids.end(),e.v)-ids.begin();}
  }
  std::sort(input.begin(),input.end(),[](auto a,auto b){return std::tie(a.u,a.v,a.w)<std::tie(b.u,b.v,b.w);});
  input.erase(std::unique(input.begin(),input.end(),[](auto a,auto b){return a.u==b.u&&a.v==b.v;}),input.end());
  int64_t m=input.size()*2;
  std::vector<int64_t> offsets(n+1,0);
  int64_t max_weight=0;
  for(auto e:input){++offsets[e.u+1];++offsets[e.v+1];max_weight=std::max(max_weight,int64_t(e.w));}
  for(int64_t v=1;v<=n;++v) offsets[v]+=offsets[v-1];
  auto next=offsets;
  std::vector<bench::Edge> edges(m);
  for(auto e:input){edges[next[e.u]++]={e.v,e.w};edges[next[e.v]++]={e.u,e.w};}
  for(int64_t v=0;v<n;++v)
    std::sort(edges.begin()+offsets[v],edges.begin()+offsets[v+1],[](auto a,auto b){return a.v<b.v;});
  std::ofstream f(out,std::ios::binary); uint8_t directed=0;
  f.write(reinterpret_cast<char*>(&directed),1);
  f.write(reinterpret_cast<char*>(&m),8); f.write(reinterpret_cast<char*>(&n),8);
  f.write(reinterpret_cast<char*>(offsets.data()),offsets.size()*8);
  f.write(reinterpret_cast<char*>(edges.data()),edges.size()*8);
  if(!f) throw std::runtime_error("graph write failed");
  int64_t denominator=1; while(denominator<max_weight) denominator*=2;
  std::cout<<"vertices="<<n<<" arcs="<<m<<" max_weight="<<max_weight
           <<" riken_denominator="<<denominator<<"\n";
} catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
