// Compile against a pinned, unmodified GAPBS checkout. Only its main is replaced.
#include "benchmark.h"
#include <vector>
#ifdef ACIC_WORK_COST
#include "work_cost.h"
#include <array>
#include <omp.h>
static std::vector<std::array<long, work_cost::COUNT>> gap_worker_cost;
#endif
struct GapRound { size_t bucket; double ms; size_t frontier; };
static std::vector<GapRound> gap_rounds;
// Use upstream's existing per-iteration timer, retaining bucket fusion and
// scheduling verbatim. Store records instead of printing inside the solve.
static void gap_diag_step(size_t bucket, double ms, size_t frontier) {
  gap_rounds.push_back({bucket, ms, frontier});
}
#define PrintStep gap_diag_step
#define main gapbs_original_main
#ifdef ACIC_WORK_COST
#include "sssp_work_cost.inc"
#else
#include "sssp.cc"
#endif
#undef main
#undef PrintStep
#include "common.h"
#include <omp.h>
#include <chrono>

int main(int argc,char **argv) try {
  if(argc!=4 && !(argc==5 && std::string(argv[4])=="--diag"))
    throw std::runtime_error("usage: gap_sssp graph.wsg source delta [--diag]");
  const bool diag = argc == 5;
  const int source=std::stoi(argv[2]),delta=std::stoi(argv[3]);
  char a0[]="gap_sssp",a1[]="-f";
  char *args[]={a0,a1,argv[1]};
  CLDelta<WeightT> cli(3,args,"SSSP comparison");
  if(!cli.ParseArgs()) return 1;
  WeightedBuilder builder(cli); WGraph g=builder.MakeGraph();
  if(source<0||source>=g.num_nodes()||delta<=0) return 1;
#ifdef ACIC_WORK_COST
  gap_worker_cost.resize(omp_get_max_threads());
#endif
  const double start=omp_get_wtime();
  auto distances=DeltaStep(g,source,delta,diag);
  const double elapsed=omp_get_wtime()-start;
  bench::Digest digest;
  for(int64_t v=0;v<g.num_nodes();++v)
    digest.add(v,distances[v]==kDistInf?bench::inf:distances[v]);
  bench::print(source,elapsed,digest);
#ifdef ACIC_WORK_COST
  long sum[work_cost::COUNT] = {};
  for (const auto &worker : gap_worker_cost)
    for (int i = 0; i < work_cost::COUNT; ++i) sum[i] += worker[i];
  ++sum[work_cost::CHANGES]; // injected source, as in ACIC
  ++sum[work_cost::QUEUE_PUSHES];
  std::cout << work_cost::record(sum) << '\n';
#endif
  if (diag) {
    double bucket_seconds = 0;
    for (const auto &r : gap_rounds) bucket_seconds += r.ms * 1e-3;
    std::cout << "GAP_BUCKETS count=" << gap_rounds.size()
              << " seconds=" << bucket_seconds
              << " mean_seconds=" << (gap_rounds.empty() ? 0 : bucket_seconds / gap_rounds.size())
              << '\n';
    std::cout << "GAP_ROUND bucket,seconds,frontier\n";
    for (const auto &r : gap_rounds)
      std::cout << "GAP_ROUND " << r.bucket << ',' << r.ms * 1e-3 << ',' << r.frontier << '\n';
  }
  return 0;
} catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
