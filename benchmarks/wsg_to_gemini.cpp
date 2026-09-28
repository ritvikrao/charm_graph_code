// Convert a GAPBS .wsg (graphlib/gapbs.h layout, narrow or wide ids) into the
// edge-list inputs of two external baselines.
//
//   wsg_to_gemini [--ids 32|64] in.wsg out.bin
//       Gemini (core/graph.hpp load_directed): a headerless array of packed
//       records {VertexId src; VertexId dst; uint32 weight} -- 12 bytes per
//       edge with 32-bit ids (stock VertexId), 20 bytes with 64-bit ids (a
//       -DGEMINI_VERTEX64 build). Gemini derives the edge count from the file
//       size and takes the vertex count on its command line, so a sidecar
//       out.bin.info records both, plus the id width, and gemini_sssp checks
//       it. Every directed edge of the .wsg is written once, in CSR order:
//       an undirected .wsg already stores both directions, which is what
//       load_directed (upstream toolkits/sssp.cpp) expects for a symmetric
//       graph. Default --ids: 32 when the vertex count fits, else 64.
//
//   wsg_to_gemini --text PARTS in.wsg out_prefix
//       HavoqGT's text edge list (parallel_edge_list_reader): lines
//       "src dst weight", split into PARTS files out_prefix.0 ... so that
//       upstream ingest_edge_list spreads them over ranks (it assigns whole
//       files to ranks). Ingest them with -u 0: both directions are present.
//       havoqgt_ingest reads the .wsg directly and does not need this; the
//       text path exists to cross-check it through the unmodified reader.
//
// Only the out-edge CSR is read (a directed .wsg's in-edge copy is ignored).
// Every destination must be in [0, n) and every weight positive. The work is
// split into edge-balanced vertex ranges processed in parallel with pread and
// pwrite through fixed-size buffers, so memory stays small at any graph size.
#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <omp.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

struct Header {
  bool directed = false;
  int64_t m = 0, n = 0;
  bool wide = false;
  int64_t record() const { return wide ? 16 : 8; }
  int64_t offsets_at() const { return 17; }
  int64_t edges_at() const { return 17 + (n + 1) * 8; }
};

[[noreturn]] void die(const std::string &message) {
  std::fprintf(stderr, "wsg_to_gemini: %s\n", message.c_str());
  std::exit(1);
}

void pread_all(int fd, void *buffer, size_t bytes, int64_t at) {
  char *p = static_cast<char *>(buffer);
  while (bytes > 0) {
    ssize_t got = ::pread(fd, p, bytes, at);
    if (got <= 0) die(std::string("short read: ") + (got < 0 ? std::strerror(errno) : "end of file"));
    p += got; bytes -= (size_t)got; at += got;
  }
}

void pwrite_all(int fd, const void *buffer, size_t bytes, int64_t at) {
  const char *p = static_cast<const char *>(buffer);
  while (bytes > 0) {
    ssize_t put = ::pwrite(fd, p, bytes, at);
    if (put <= 0) die(std::string("short write: ") + std::strerror(errno));
    p += put; bytes -= (size_t)put; at += put;
  }
}

Header read_header(int fd, int64_t file_bytes) {
  unsigned char head[17];
  pread_all(fd, head, 17, 0);
  Header h;
  h.directed = head[0] != 0;
  std::memcpy(&h.m, head + 1, 8);
  std::memcpy(&h.n, head + 9, 8);
  if (head[0] > 1 || h.n <= 0 || h.m < 0 || h.n > (int64_t)1 << 40)
    die("not a GAPBS .wsg header");
  for (int wide = 0; wide < 2; ++wide) {
    h.wide = wide;
    int64_t one = (h.n + 1) * 8 + h.m * h.record();
    if (17 + (h.directed ? 2 : 1) * one == file_bytes) return h;
  }
  die("file size matches neither the 32-bit nor the 64-bit id .wsg layout");
}

int64_t offset_at(int fd, const Header &h, int64_t v) {
  int64_t value;
  pread_all(fd, &value, 8, h.offsets_at() + v * 8);
  return value;
}

// First vertex whose row starts at or after edge `target` (rows of vertices
// before it hold only edges below target).
int64_t vertex_for_edge(int fd, const Header &h, int64_t target) {
  int64_t lo = 0, hi = h.n;
  while (lo < hi) {
    int64_t mid = lo + (hi - lo) / 2;
    if (offset_at(fd, h, mid) < target) lo = mid + 1; else hi = mid;
  }
  return lo;
}

// Visit the edges of vertices [first, last) in order through bounded
// buffers, calling emit(u, v, w) for each.
template <class Emit>
void scan(int fd, const Header &h, int64_t first, int64_t last, Emit &&emit) {
  const int64_t vblock = 1 << 20, eblock = 1 << 22;
  std::vector<int64_t> off;
  std::vector<unsigned char> raw;
  for (int64_t v0 = first; v0 < last; v0 += vblock) {
    const int64_t v1 = std::min(last, v0 + vblock);
    off.resize((size_t)(v1 - v0 + 1));
    pread_all(fd, off.data(), off.size() * 8, h.offsets_at() + v0 * 8);
    for (size_t i = 1; i < off.size(); ++i)
      if (off[i] < off[i - 1]) die("offsets are not monotone");
    if (off.front() < 0 || off.back() > h.m) die("offsets exceed the edge count");
    int64_t u = v0;
    for (int64_t e0 = off.front(); e0 < off.back(); e0 += eblock) {
      const int64_t e1 = std::min(off.back(), e0 + eblock);
      raw.resize((size_t)((e1 - e0) * h.record()));
      pread_all(fd, raw.data(), raw.size(), h.edges_at() + e0 * h.record());
      for (int64_t e = e0; e < e1; ++e) {
        while (off[(size_t)(u - v0 + 1)] <= e) ++u;
        const unsigned char *at = raw.data() + (e - e0) * h.record();
        int64_t v; int32_t w;
        if (h.wide) { std::memcpy(&v, at, 8); std::memcpy(&w, at + 8, 4); }
        else { int32_t v32; std::memcpy(&v32, at, 4); v = v32; std::memcpy(&w, at + 4, 4); }
        if (v < 0 || v >= h.n) die("destination out of range");
        if (w <= 0) die("nonpositive weight");
        emit(u, v, (uint32_t)w);
      }
    }
  }
}

template <class Id>
struct __attribute__((packed)) GeminiEdge { Id src, dst; uint32_t weight; };

template <class Id>
void write_gemini(int in, const Header &h, const std::vector<int64_t> &cut, int out) {
  const int parts = (int)cut.size() - 1;
  #pragma omp parallel for schedule(dynamic, 1)
  for (int p = 0; p < parts; ++p) {
    std::vector<GeminiEdge<Id>> buffer;
    buffer.reserve(1 << 20);
    int64_t next = -1; // edge index of buffer[0]
    auto flush = [&]() {
      if (!buffer.empty())
        pwrite_all(out, buffer.data(), buffer.size() * sizeof(GeminiEdge<Id>),
                   next * (int64_t)sizeof(GeminiEdge<Id>));
      next += (int64_t)buffer.size();
      buffer.clear();
    };
    if (cut[p] < cut[p + 1]) next = offset_at(in, h, cut[p]);
    scan(in, h, cut[p], cut[p + 1], [&](int64_t u, int64_t v, uint32_t w) {
      buffer.push_back(GeminiEdge<Id>{(Id)u, (Id)v, w});
      if (buffer.size() == buffer.capacity()) flush();
    });
    flush();
  }
}

void write_text(int in, const Header &h, const std::vector<int64_t> &cut, const std::string &prefix) {
  const int parts = (int)cut.size() - 1;
  #pragma omp parallel for schedule(dynamic, 1)
  for (int p = 0; p < parts; ++p) {
    const std::string path = prefix + "." + std::to_string(p);
    std::FILE *f = std::fopen(path.c_str(), "w");
    if (!f) die("cannot create " + path);
    std::vector<char> big(1 << 24);
    std::setvbuf(f, big.data(), _IOFBF, big.size());
    scan(in, h, cut[p], cut[p + 1], [&](int64_t u, int64_t v, uint32_t w) {
      std::fprintf(f, "%" PRId64 " %" PRId64 " %" PRIu32 "\n", u, v, w);
    });
    if (std::fclose(f) != 0) die("cannot write " + path);
  }
}

} // namespace

int main(int argc, char **argv) {
  int ids = 0, text_parts = 0;
  std::vector<std::string> positional;
  for (int a = 1; a < argc; ++a) {
    std::string arg = argv[a];
    if (arg == "--ids" && a + 1 < argc) ids = std::atoi(argv[++a]);
    else if (arg == "--text" && a + 1 < argc) text_parts = std::atoi(argv[++a]);
    else positional.push_back(arg);
  }
  if (positional.size() != 2 || (ids != 0 && ids != 32 && ids != 64) || text_parts < 0) {
    std::fprintf(stderr, "usage: wsg_to_gemini [--ids 32|64] in.wsg out.bin\n"
                         "       wsg_to_gemini --text PARTS in.wsg out_prefix\n");
    return 2;
  }
  const std::string in_path = positional[0], out_path = positional[1];
  int in = ::open(in_path.c_str(), O_RDONLY);
  if (in < 0) die("cannot open " + in_path);
  struct stat st{};
  if (::fstat(in, &st) != 0) die("cannot stat " + in_path);
  const Header h = read_header(in, st.st_size);
  if (offset_at(in, h, 0) != 0 || offset_at(in, h, h.n) != h.m) die("offsets do not span [0, m]");
  if (ids == 0) ids = h.n - 1 <= (int64_t)UINT32_MAX ? 32 : 64;
  if (ids == 32 && h.n - 1 > (int64_t)UINT32_MAX)
    die("vertex ids exceed 32 bits; use --ids 64 and gemini_sssp64");

  // Edge-balanced vertex ranges: enough of them to keep every thread busy.
  const int parts = text_parts ? text_parts : std::max(1, omp_get_max_threads() * 8);
  std::vector<int64_t> cut(parts + 1);
  cut[0] = 0; cut[parts] = h.n;
  for (int p = 1; p < parts; ++p) cut[p] = vertex_for_edge(in, h, h.m * p / parts);
  for (int p = 1; p <= parts; ++p) cut[p] = std::max(cut[p], cut[p - 1]);

  const double start = omp_get_wtime();
  if (text_parts) {
    write_text(in, h, cut, out_path);
    std::printf("wrote %d text parts %s.* vertices=%" PRId64 " edges=%" PRId64 " in %.1f s\n",
                text_parts, out_path.c_str(), h.n, h.m, omp_get_wtime() - start);
    return 0;
  }
  const std::string tmp = out_path + ".partial";
  int out = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) die("cannot create " + tmp);
  const int64_t record = ids == 32 ? 12 : 20;
  if (::ftruncate(out, h.m * record) != 0) die("cannot size " + tmp);
  if (ids == 32) write_gemini<uint32_t>(in, h, cut, out);
  else write_gemini<uint64_t>(in, h, cut, out);
  if (::fsync(out) != 0 || ::close(out) != 0) die("cannot finish " + tmp);
  if (std::rename(tmp.c_str(), out_path.c_str()) != 0) die("cannot rename " + tmp);
  std::FILE *info = std::fopen((out_path + ".info").c_str(), "w");
  if (!info) die("cannot write " + out_path + ".info");
  std::fprintf(info, "format=gemini-edgelist\nsource=%s\nsource_bytes=%lld\nvertices=%" PRId64
               "\nedges=%" PRId64 "\nid_bytes=%d\nweight=uint32\nrecord_bytes=%" PRId64
               "\ndirected=%d\nboth_directions=%d\n",
               in_path.c_str(), (long long)st.st_size, h.n, h.m, ids / 8, record,
               (int)h.directed, (int)!h.directed);
  std::fclose(info);
  std::printf("wrote %s vertices=%" PRId64 " edges=%" PRId64 " id_bytes=%d record_bytes=%" PRId64
              " in %.1f s\n", out_path.c_str(), h.n, h.m, ids / 8, record, omp_get_wtime() - start);
  return 0;
}
