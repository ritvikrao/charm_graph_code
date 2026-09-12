Step 7.2 job outputs. Full Slurm output: per-repetition compute times, then the
diag_report.py table. Ratios are only comparable within a file.

1node-hold.out                 job 22012010, cn046        off / hold
2node-hold.out                 job 22012013, cn[046,055]  off / hold
1node-fold.out                 job 22015239, cn039        off / fold / hold / hold+fold
2node-fold.out                 job 22015245, cn[046,055]  off / fold / hold / hold+fold
2node-fold-created-per-rep.txt updates created in every repetition of 2node-fold.out,
                               and batch-folded updates in rep 1
2node-gate-off.out             job 22012755  scripts/verify_2node.sh, --combine off
2node-gate-hold.out            job 22012756  --combine hold
2node-gate-hold-fold.out       job 22015254  --combine hold --batch-fold on
login-node-diagnosis.txt       2^18 on 8 PEs of a login node, structural counters only:
                               why-*    the controller-reach check
                               nofill-* the same runs against an htram built without
                                        ADD_FILLERS
