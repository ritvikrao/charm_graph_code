// Build a HavoqGT graph store straight from a GAPBS .wsg (graphlib/gapbs.h
// layout, 32- or 64-bit ids). It replaces only upstream ingest_edge_list's
// input side: that tool parses "src dst weight" text through
// parallel_edge_list_reader, which assigns whole files to ranks and so needs
// the edge list pre-split into many text files. Here each rank reads an
// edge-balanced vertex range of the CSR itself and hands the same edge tuples
// (uint64 src, uint64 dst, uint32 weight) to the unchanged
// delegate_partitioned_graph constructor, with the same defaults as upstream
// (delegate threshold 2^20, one partition pass, chunk 8192).
//
// Every directed edge of the .wsg is ingested once: an undirected .wsg
// already stores both directions (upstream's -u 0). max_vertex is n - 1, so
// isolated vertices at the top of the id range exist in the graph too, and
// the store records n ("acic_vertices") and the weight width
// ("acic_weight_bytes") for the patched run_sssp to check.
//
// Built by the ACIC havoqgt.patch CMake rule (target havoqgt_ingest).
//
//   srun -n P havoqgt_ingest -o /dev/shm/g [-d 1048576] [-p 1] [-c 8192] [-O] [-b backup] graph.wsg
//
// The store is partitioned for exactly P ranks (and HavoqGT's node-local
// communicator): run_sssp must use the same rank count and layout.
#include <havoqgt/delegate_partitioned_graph.hpp>
#include <havoqgt/distributed_db.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

using namespace havoqgt;

typedef delegate_partitioned_graph<distributed_db::allocator<>> graph_type;
typedef uint32_t                                  edge_data_type;  // = run_sssp
typedef distributed_db::allocator<edge_data_type> edge_data_allocator_type;

namespace {

[[noreturn]] void fail(const std::string &message) {
  std::cerr << "havoqgt_ingest[" << comm_world().rank() << "]: " << message
            << std::endl;
  MPI_Abort(MPI_COMM_WORLD, 1);
  std::abort();
}

void pread_all(int fd, void *buffer, size_t bytes, int64_t at) {
  char *p = static_cast<char *>(buffer);
  while (bytes > 0) {
    ssize_t got = ::pread(fd, p, bytes, at);
    if (got <= 0) fail(std::string("short read of the .wsg: ") +
                       (got < 0 ? std::strerror(errno) : "end of file"));
    p += got;
    bytes -= size_t(got);
    at += got;
  }
}

// This rank's share of a .wsg: vertices [first, last) and their out-edges.
class wsg_edge_list {
 public:
  typedef std::tuple<uint64_t, uint64_t, edge_data_type> value_type;
  typedef value_type                                     edge_type;
  typedef edge_data_type                                 edge_data_value_type;

  class input_iterator_type {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type        = edge_type;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const edge_type *;
    using reference         = const edge_type &;

    input_iterator_type(const wsg_edge_list *list, uint64_t edge)
        : m_list(list), m_edge(edge), m_row(0) {
      settle();
    }
    const edge_type &operator*() const { return m_current; }
    const edge_type *operator->() const { return &m_current; }
    input_iterator_type &operator++() {
      ++m_edge;
      settle();
      return *this;
    }
    input_iterator_type operator++(int) {
      input_iterator_type before = *this;
      ++*this;
      return before;
    }
    friend bool operator==(const input_iterator_type &x,
                           const input_iterator_type &y) {
      return x.m_edge == y.m_edge;
    }
    friend bool operator!=(const input_iterator_type &x,
                           const input_iterator_type &y) {
      return x.m_edge != y.m_edge;
    }

   private:
    void settle() {
      if (m_edge >= m_list->size()) return;
      while (m_list->m_offsets[m_row + 1] <= m_edge) ++m_row;
      m_current = edge_type(m_list->m_first + m_row, m_list->m_dst[m_edge],
                            m_list->m_weight[m_edge]);
    }
    const wsg_edge_list *m_list;
    uint64_t             m_edge;  // local edge index
    uint64_t             m_row;   // local vertex index
    edge_type            m_current;
  };

  explicit wsg_edge_list(const std::string &path) {
    const int rank = comm_world().rank(), size = comm_world().size();
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) fail("cannot open " + path);
    struct stat st {};
    if (::fstat(fd, &st) != 0) fail("cannot stat " + path);
    unsigned char head[17];
    pread_all(fd, head, 17, 0);
    std::memcpy(&m_m, head + 1, 8);
    std::memcpy(&m_n, head + 9, 8);
    const bool directed = head[0] != 0;
    if (head[0] > 1 || m_n <= 0 || m_m < 0 || m_n > (int64_t(1) << 40))
      fail(path + " does not have a GAPBS .wsg header");
    bool matched = false;
    for (int wide = 0; wide < 2 && !matched; ++wide) {
      m_record = wide ? 16 : 8;
      const int64_t one = (m_n + 1) * 8 + m_m * m_record;
      matched = 17 + (directed ? 2 : 1) * one == int64_t(st.st_size);
    }
    if (!matched)
      fail(path + " size matches neither the 32- nor the 64-bit id layout");
    // Edge-balanced vertex ranges: rank r starts at the first vertex whose
    // row begins at or after edge m*r/P.
    auto offset_at = [&](int64_t v) {
      int64_t value;
      pread_all(fd, &value, 8, 17 + v * 8);
      return value;
    };
    auto cut = [&](int r) {
      if (r == 0) return int64_t(0);
      if (r == size) return m_n;
      const int64_t target = int64_t((__int128)m_m * r / size);
      int64_t lo = 0, hi = m_n;
      while (lo < hi) {
        const int64_t mid = lo + (hi - lo) / 2;
        if (offset_at(mid) < target) lo = mid + 1; else hi = mid;
      }
      return lo;
    };
    m_first = cut(rank);
    const int64_t last = std::max(m_first, cut(rank + 1));
    std::vector<int64_t> rows(size_t(last - m_first + 1));
    pread_all(fd, rows.data(), rows.size() * 8, 17 + m_first * 8);
    for (size_t i = 1; i < rows.size(); ++i)
      if (rows[i] < rows[i - 1]) fail("offsets are not monotone");
    if (rows.front() < 0 || rows.back() > m_m) fail("offsets exceed num_edges");
    m_offsets.resize(rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
      m_offsets[i] = uint64_t(rows[i] - rows.front());
    const uint64_t count = m_offsets.back();
    m_dst.resize(count);
    m_weight.resize(count);
    const int64_t edges_at = 17 + (m_n + 1) * 8;
    const uint64_t block = 1 << 22;
    std::vector<unsigned char> raw;
    for (uint64_t e0 = 0; e0 < count; e0 += block) {
      const uint64_t e1 = std::min(count, e0 + block);
      raw.resize((e1 - e0) * m_record);
      pread_all(fd, raw.data(), raw.size(),
                edges_at + (rows.front() + int64_t(e0)) * m_record);
      for (uint64_t e = e0; e < e1; ++e) {
        const unsigned char *at = raw.data() + (e - e0) * m_record;
        int64_t v;
        int32_t w;
        if (m_record == 16) {
          std::memcpy(&v, at, 8);
          std::memcpy(&w, at + 8, 4);
        } else {
          int32_t v32;
          std::memcpy(&v32, at, 4);
          v = v32;
          std::memcpy(&w, at + 4, 4);
        }
        if (v < 0 || v >= m_n) fail("destination out of range");
        if (w <= 0) fail("nonpositive weight");
        m_dst[e]    = uint64_t(v);
        m_weight[e] = edge_data_type(w);
      }
    }
    ::close(fd);
  }

  input_iterator_type begin() const { return input_iterator_type(this, 0); }
  input_iterator_type end() const { return input_iterator_type(this, size()); }
  uint64_t size() const { return m_dst.size(); }  // local edges, as upstream
  uint64_t max_vertex_id() const { return uint64_t(m_n - 1); }
  uint64_t vertices() const { return uint64_t(m_n); }
  uint64_t global_edges() const { return uint64_t(m_m); }
  bool has_edge_data() const { return true; }
  int record_bytes() const { return m_record; }

 private:
  int64_t                     m_n = 0, m_m = 0, m_first = 0;
  int                         m_record = 8;
  std::vector<uint64_t>       m_offsets;  // local CSR row starts
  std::vector<uint64_t>       m_dst;
  std::vector<edge_data_type> m_weight;
};

}  // namespace

int main(int argc, char **argv) {
  init(&argc, &argv);
  {
    const int rank = comm_world().rank(), size = comm_world().size();
    std::string output, backup;
    uint64_t    delegate_threshold = 1048576, passes = 1, chunk = 8 * 1024;
    bool        overwrite = false;
    int         c;
    while ((c = getopt(argc, argv, "o:b:d:p:c:Oh")) != -1) {
      switch (c) {
        case 'o': output = optarg; break;
        case 'b': backup = optarg; break;
        case 'd': delegate_threshold = strtoull(optarg, nullptr, 10); break;
        case 'p': passes = strtoull(optarg, nullptr, 10); break;
        case 'c': chunk = strtoull(optarg, nullptr, 10); break;
        case 'O': overwrite = true; break;
        default: output.clear(); optind = argc + 1; break;
      }
    }
    if (output.empty() || optind != argc - 1) {
      if (rank == 0)
        std::cerr << "usage: havoqgt_ingest -o STORE [-d delegate_threshold] "
                     "[-p passes] [-c chunk] [-O overwrite] [-b backup] "
                     "graph.wsg\n";
      MPI_Abort(MPI_COMM_WORLD, 2);
    }
    const std::string input = argv[optind];
    if (std::filesystem::exists(output)) {
      if (!overwrite) {
        if (rank == 0)
          std::cerr << "havoqgt_ingest: " << output
                    << " exists; pass -O to replace it\n";
        MPI_Abort(MPI_COMM_WORLD, 2);
      }
      distributed_db::remove(output);
    }
    comm_world().barrier();

    double start = MPI_Wtime();
    wsg_edge_list edges(input);
    comm_world().barrier();
    const double read_seconds = MPI_Wtime() - start;
    if (rank == 0)
      std::cout << "HAVOQGT_INGEST read " << input << " vertices="
                << edges.vertices() << " edges=" << edges.global_edges()
                << " id_bytes=" << edges.record_bytes() / 2
                << " ranks=" << size << " in " << read_seconds << " s"
                << std::endl;

    {
      distributed_db ddb(db_create(), output.c_str());
      auto edge_data_ptr =
          ddb.get_manager()
              ->construct<graph_type::edge_data<edge_data_type,
                                                edge_data_allocator_type>>(
                  "graph_edge_data_obj")(ddb.get_allocator());
      graph_type *graph = ddb.get_manager()->construct<graph_type>(
          "graph_obj")(ddb.get_allocator(), MPI_COMM_WORLD, edges,
                       edges.max_vertex_id(), delegate_threshold, passes,
                       chunk, *edge_data_ptr);
      ddb.get_manager()->construct<uint64_t>("acic_vertices")(edges.vertices());
      ddb.get_manager()->construct<uint64_t>("acic_edges")(edges.global_edges());
      ddb.get_manager()->construct<uint64_t>("acic_weight_bytes")(
          sizeof(edge_data_type));
      (void)graph;
      comm_world().barrier();
    }
    comm_world().barrier();
    if (rank == 0)
      std::cout << "HAVOQGT_INGEST store=" << output << " built in "
                << MPI_Wtime() - start << " s" << std::endl;
    if (!backup.empty()) distributed_db::transfer(output, backup);
    comm_world().barrier();
  }
  MPI_Finalize();  // ygm::detail::init_final no longer does (abort-path hang)
  return 0;
}
