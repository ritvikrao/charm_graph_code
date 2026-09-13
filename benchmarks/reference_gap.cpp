// Independent input parsing and Dijkstra using GAPBS graph structures.
#include "benchmark.h"
#include "command_line.h"
#include "common.h"
#include <queue>
#include <set>
int main(int argc,char **argv) try {
  if(argc!=3) throw std::runtime_error("usage: reference_gap graph.wsg source_count");
  char a0[]="reference",a1[]="-f";char *args[]={a0,a1,argv[1]};
  CLApp cli(3,args,"independent reference"); if(!cli.ParseArgs()) return 1;
  WeightedBuilder builder(cli);WGraph g=builder.MakeGraph();
  std::set<int64_t> used;
  std::cout<<"source\trole\th1\th2\treachable\tdistance_sum\tmax_distance\treachable_arcs\n";
  for(int index=0;index<std::stoi(argv[2]);++index) {
    int64_t source;uint64_t trial=index;
    do {source=bench::mix(20260913+trial++)%g.num_nodes();}
    while(g.out_degree(source)==0||used.count(source));
    used.insert(source);
    std::vector<int64_t> dist(g.num_nodes(),bench::inf);
    using Entry=std::pair<int64_t,int64_t>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> q;
    dist[source]=0;q.emplace(0,source);
    while(!q.empty()) {
      const auto [d,u]=q.top();q.pop();if(d!=dist[u])continue;
      for(auto e:g.out_neigh(u)) if(d+e.w<dist[e.v]){dist[e.v]=d+e.w;q.emplace(dist[e.v],e.v);}
    }
    bench::Digest digest;int64_t maximum=0,arcs=0;
    for(int64_t v=0;v<g.num_nodes();++v){digest.add(v,dist[v]);if(dist[v]!=bench::inf){maximum=std::max(maximum,dist[v]);arcs+=g.out_degree(v);}}
    // RIKEN's binary32 arithmetic must represent every final integer distance.
    if(maximum>=16777216) throw std::runtime_error("distance exceeds binary32 exact integer range; exclude from exact RIKEN comparison");
    std::cout<<source<<'\t'<<(index<2?"tune":"test")<<'\t'<<digest.h1<<'\t'<<digest.h2<<'\t'
      <<digest.reachable<<'\t'<<digest.distance_sum<<'\t'<<maximum<<'\t'<<arcs<<'\n';
  }
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
