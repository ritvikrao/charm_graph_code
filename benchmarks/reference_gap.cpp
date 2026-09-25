// Independent input parsing and Dijkstra using GAPBS graph structures.
#include "benchmark.h"
#include "command_line.h"
#include "common.h"
#include <queue>
#include <set>
#include <sstream>
int main(int argc,char **argv) try {
  // A third argument "any-range" drops the binary32 guard below. The guard is
  // for the RIKEN comparison, whose float weights cannot represent larger
  // integer distances; ACIC-only campaigns on continental road graphs exceed it.
  if(argc!=3&&!(argc==4&&std::string(argv[3])=="any-range"))
    throw std::runtime_error("usage: reference_gap graph.wsg source_count [any-range]");
  const bool guard=argc==3;
  char a0[]="reference",a1[]="-f";char *args[]={a0,a1,argv[1]};
  CLApp cli(3,args,"independent reference"); if(!cli.ParseArgs()) return 1;
  WeightedBuilder builder(cli);WGraph g=builder.MakeGraph();
  // Sources are drawn first, in order, so the choice is the serial one; the
  // Dijkstras then run concurrently (mesh30 is 1B vertices and 4.3B edges)
  // and the rows are printed in source order, identical to a serial run.
  const int count=std::stoi(argv[2]);
  std::set<int64_t> used;std::vector<int64_t> sources;
  for(int index=0;index<count;++index) {
    int64_t source;uint64_t trial=index;
    do {source=bench::mix(20260913+trial++)%g.num_nodes();}
    while(g.out_degree(source)==0||used.count(source));
    used.insert(source);sources.push_back(source);
  }
  std::vector<std::string> rows(count);std::vector<int64_t> maxima(count);
  #pragma omp parallel for schedule(dynamic,1)
  for(int index=0;index<count;++index) {
    const int64_t source=sources[index];
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
    std::ostringstream row;
    row<<source<<'\t'<<(index<2?"tune":"test")<<'\t'<<digest.h1<<'\t'<<digest.h2<<'\t'
       <<digest.reachable<<'\t'<<digest.distance_sum<<'\t'<<maximum<<'\t'<<arcs<<'\n';
    rows[index]=row.str();maxima[index]=maximum;
  }
  std::cout<<"source\trole\th1\th2\treachable\tdistance_sum\tmax_distance\treachable_arcs\n";
  for(int index=0;index<count;++index) {
    // RIKEN's binary32 arithmetic must represent every final integer distance.
    if(guard&&maxima[index]>=16777216) throw std::runtime_error("distance exceeds binary32 exact integer range; exclude from exact RIKEN comparison");
    std::cout<<rows[index];
  }
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
