/* Share of a solve spent inside MPI, for the MPI baselines (step 7.6o).
 *
 * Preload with LD_PRELOAD. The program marks its timed region with
 * MPI_Pcontrol(1) ... MPI_Pcontrol(0) (riken_driver.cpp does); between the
 * two, every wrapped call below adds its wall time to a per-rank total. At
 * MPI_Finalize rank 0 prints one line:
 *
 *   MPI_SHARE ranks=R window_rank_seconds=W mpi_rank_seconds=M share=M/W
 *
 * summed over ranks. Only the calling thread is timed, which is all RIKEN's
 * MPI_THREAD_SINGLE driver has. Time a program spends polling between
 * MPI_Test* calls is outside MPI and is not counted, so the share is a lower
 * bound on communication and waiting.
 */
#include <mpi.h>
#include <stdarg.h>
#include <stdio.h>

static int on = 0;
static double window_start = 0, window_total = 0, in_mpi = 0;

#define TIMED(call)                         \
  do {                                      \
    double t0_ = on ? PMPI_Wtime() : 0;     \
    int rc_ = call;                         \
    if (on) in_mpi += PMPI_Wtime() - t0_;   \
    return rc_;                             \
  } while (0)

int MPI_Pcontrol(const int level, ...) {
  if (level && !on) {
    on = 1;
    window_start = PMPI_Wtime();
  } else if (!level && on) {
    on = 0;
    window_total += PMPI_Wtime() - window_start;
  }
  return MPI_SUCCESS;
}

int MPI_Finalize(void) {
  double local[2] = {window_total, in_mpi}, total[2] = {0, 0};
  int rank = 0, size = 1;
  PMPI_Comm_rank(MPI_COMM_WORLD, &rank);
  PMPI_Comm_size(MPI_COMM_WORLD, &size);
  PMPI_Reduce(local, total, 2, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  if (rank == 0) {
    printf("MPI_SHARE ranks=%d window_rank_seconds=%.6f mpi_rank_seconds=%.6f share=%.4f\n",
           size, total[0], total[1], total[0] > 0 ? total[1] / total[0] : 0.0);
    fflush(stdout);
  }
  return PMPI_Finalize();
}

int MPI_Send(const void *b, int c, MPI_Datatype t, int d, int g, MPI_Comm m) {
  TIMED(PMPI_Send(b, c, t, d, g, m));
}
int MPI_Recv(void *b, int c, MPI_Datatype t, int s, int g, MPI_Comm m, MPI_Status *st) {
  TIMED(PMPI_Recv(b, c, t, s, g, m, st));
}
int MPI_Isend(const void *b, int c, MPI_Datatype t, int d, int g, MPI_Comm m, MPI_Request *r) {
  TIMED(PMPI_Isend(b, c, t, d, g, m, r));
}
int MPI_Irecv(void *b, int c, MPI_Datatype t, int s, int g, MPI_Comm m, MPI_Request *r) {
  TIMED(PMPI_Irecv(b, c, t, s, g, m, r));
}
int MPI_Wait(MPI_Request *r, MPI_Status *st) { TIMED(PMPI_Wait(r, st)); }
int MPI_Waitall(int n, MPI_Request r[], MPI_Status st[]) { TIMED(PMPI_Waitall(n, r, st)); }
int MPI_Waitany(int n, MPI_Request r[], int *i, MPI_Status *st) { TIMED(PMPI_Waitany(n, r, i, st)); }
int MPI_Test(MPI_Request *r, int *f, MPI_Status *st) { TIMED(PMPI_Test(r, f, st)); }
int MPI_Testany(int n, MPI_Request r[], int *i, int *f, MPI_Status *st) {
  TIMED(PMPI_Testany(n, r, i, f, st));
}
int MPI_Testall(int n, MPI_Request r[], int *f, MPI_Status st[]) { TIMED(PMPI_Testall(n, r, f, st)); }
int MPI_Barrier(MPI_Comm m) { TIMED(PMPI_Barrier(m)); }
int MPI_Bcast(void *b, int c, MPI_Datatype t, int root, MPI_Comm m) { TIMED(PMPI_Bcast(b, c, t, root, m)); }
int MPI_Reduce(const void *s, void *r, int c, MPI_Datatype t, MPI_Op o, int root, MPI_Comm m) {
  TIMED(PMPI_Reduce(s, r, c, t, o, root, m));
}
int MPI_Allreduce(const void *s, void *r, int c, MPI_Datatype t, MPI_Op o, MPI_Comm m) {
  TIMED(PMPI_Allreduce(s, r, c, t, o, m));
}
int MPI_Allgather(const void *s, int sc, MPI_Datatype st, void *r, int rc, MPI_Datatype rt, MPI_Comm m) {
  TIMED(PMPI_Allgather(s, sc, st, r, rc, rt, m));
}
int MPI_Allgatherv(const void *s, int sc, MPI_Datatype st, void *r, const int rc[], const int d[],
                   MPI_Datatype rt, MPI_Comm m) {
  TIMED(PMPI_Allgatherv(s, sc, st, r, rc, d, rt, m));
}
int MPI_Alltoall(const void *s, int sc, MPI_Datatype st, void *r, int rc, MPI_Datatype rt, MPI_Comm m) {
  TIMED(PMPI_Alltoall(s, sc, st, r, rc, rt, m));
}
int MPI_Alltoallv(const void *s, const int sc[], const int sd[], MPI_Datatype st, void *r,
                  const int rc[], const int rd[], MPI_Datatype rt, MPI_Comm m) {
  TIMED(PMPI_Alltoallv(s, sc, sd, st, r, rc, rd, rt, m));
}
int MPI_Gatherv(const void *s, int sc, MPI_Datatype st, void *r, const int rc[], const int d[],
                MPI_Datatype rt, int root, MPI_Comm m) {
  TIMED(PMPI_Gatherv(s, sc, st, r, rc, d, rt, root, m));
}
int MPI_Scatter(const void *s, int sc, MPI_Datatype st, void *r, int rc, MPI_Datatype rt, int root,
                MPI_Comm m) {
  TIMED(PMPI_Scatter(s, sc, st, r, rc, rt, root, m));
}
int MPI_Reduce_scatter(const void *s, void *r, const int rc[], MPI_Datatype t, MPI_Op o, MPI_Comm m) {
  TIMED(PMPI_Reduce_scatter(s, r, rc, t, o, m));
}
int MPI_Reduce_scatter_block(const void *s, void *r, int rc, MPI_Datatype t, MPI_Op o, MPI_Comm m) {
  TIMED(PMPI_Reduce_scatter_block(s, r, rc, t, o, m));
}
