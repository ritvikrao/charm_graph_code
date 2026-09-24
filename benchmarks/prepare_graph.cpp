// Produce a canonical undirected, minimum-parallel-edge benchmark graph.
// Topology generation reuses graphlib; validation uses GAPBS's independent
// reader and serial Dijkstra (reference_gap.cpp).
#include "common.h"
#include "graphlib/graphlib.h"
#include <iostream>
#include <sstream>
#include <tuple>

struct PairEdge { int32_t u,v,w; };

// `meshz`: the Morton-ordered 2-D mesh, streamed. Byte-identical to
// `prepare_graph gen 2 SIDE^2 SEED` followed by `reorder_graph.py ... mesh
// SIDE`, without either step's memory: the canonical path sorts every edge and
// reorder_graph.py lexsorts them again, which does not fit a node past about
// mesh28. The id is reorder_graph.py's morton(row, column) -- row bit b at bit
// 2b, column bit b at bit 2b+1 -- which for a power-of-two side is already its
// argsort rank. An undirected edge keeps the lighter of its two directed
// generator weights (computed on row-major ids, as gen does), and each row is
// sorted by neighbour, as both paths write it.
static uint64_t even_bits(uint64_t z){  // bits 0,2,4,... packed down
  z&=0x5555555555555555ULL;
  z=(z|(z>>1))&0x3333333333333333ULL; z=(z|(z>>2))&0x0F0F0F0F0F0F0F0FULL;
  z=(z|(z>>4))&0x00FF00FF00FF00FFULL; z=(z|(z>>8))&0x0000FFFF0000FFFFULL;
  return (z|(z>>16))&0x00000000FFFFFFFFULL;
}
static int morton_mesh(int64_t n,int seed,const std::string &out){
  int64_t side=1; while(side*side<n) side<<=1;
  if(side*side!=n) throw std::runtime_error("meshz needs a vertex count that is a power of four");
  const uint64_t X=0x5555555555555555ULL, Y=~X;        // row bits, column bits
  const WeightAssigner weight(seed);
  const int64_t m=4*side*(side-1);                     // directed edges
  std::ofstream f(out,std::ios::binary); uint8_t directed=0;
  f.write(reinterpret_cast<char*>(&directed),1);
  f.write(reinterpret_cast<const char*>(&m),8); f.write(reinterpret_cast<char*>(&n),8);
  std::vector<int64_t> buf; buf.reserve(1<<20);
  auto degree=[&](uint64_t z){
    const int64_t r=even_bits(z), c=even_bits(z>>1);
    return 4-(r==0)-(r==side-1)-(c==0)-(c==side-1);
  };
  int64_t at=0; buf.push_back(0);
  for(uint64_t z=0;z<uint64_t(n);++z){
    at+=degree(z); buf.push_back(at);
    if(buf.size()==buf.capacity()){f.write(reinterpret_cast<char*>(buf.data()),buf.size()*8);buf.clear();}
  }
  f.write(reinterpret_cast<char*>(buf.data()),buf.size()*8);
  if(at!=m) throw std::runtime_error("meshz degree sum differs from 4 side (side - 1)");
  std::vector<bench::Edge> row; row.reserve(1<<21);
  int64_t max_weight=0;
  for(uint64_t z=0;z<uint64_t(n);++z){
    const int64_t r=even_bits(z), c=even_bits(z>>1), u=r*side+c;
    bench::Edge nb[4]; int k=0;
    auto add=[&](uint64_t nz,int64_t v){
      const int64_t w=std::min(weight(u,v),weight(v,u));
      max_weight=std::max(max_weight,w);
      nb[k++]={int32_t(nz),int32_t(w)};
    };
    // Step one coordinate in interleaved form; the other's bits are kept.
    if(r>0)      add((((z&X)-1)&X)|(z&Y),u-side);
    if(r<side-1) add((((z|Y)+1)&X)|(z&Y),u+side);
    if(c>0)      add((((z&Y)-1)&Y)|(z&X),u-1);
    if(c<side-1) add((((z|X)+1)&Y)|(z&X),u+1);
    std::sort(nb,nb+k,[](auto a,auto b){return a.v<b.v;});
    row.insert(row.end(),nb,nb+k);
    if(row.size()+4>row.capacity()){f.write(reinterpret_cast<char*>(row.data()),row.size()*8);row.clear();}
  }
  f.write(reinterpret_cast<char*>(row.data()),row.size()*8);
  if(!f) throw std::runtime_error("graph write failed");
  int64_t denominator=1; while(denominator<max_weight) denominator*=2;
  std::cout<<"vertices="<<n<<" arcs="<<m<<" max_weight="<<max_weight
           <<" riken_denominator="<<denominator<<"\n";
  return 0;
}

int main(int argc,char **argv) try {
  if(argc!=6) throw std::runtime_error("usage: prepare_graph gen|dimacs|snap|meshz mode_or_n size seed output.wsg (text on stdin)");
  const std::string kind=argv[1], out=argv[5];
  int64_t n=std::stoll(argv[2]);
  const int64_t size=std::stoll(argv[3]);
  const int seed=std::stoi(argv[4]);
  if(kind=="meshz") {
    if(size>(int64_t(1)<<31)) throw std::runtime_error("meshz writes int32 ids: at most 2^31 vertices");
    return morton_mesh(size,seed,out);
  }
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
