/* Run a command with transparent huge pages disabled for it and its children
 * (PR_SET_THP_DISABLE survives exec). Used by the launch-state probes. */
#include <stdio.h>
#include <sys/prctl.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: nothp command [args...]\n"); return 2; }
  if (prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0)) { perror("prctl"); return 1; }
  execvp(argv[1], argv + 1);
  perror("execvp");
  return 1;
}
