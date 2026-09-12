Step 7.3 job outputs. Full Slurm output: per-repetition compute times, then the
diag_report.py table. Ratios are only comparable within a file. All 2^20 unless
noted, 16 PEs per node, on top of the step 7.1 default flush policy.

The bucket-width sweep, step 6's H1 sweep rerun (widths as multiples of the
log V / sqrt V rule; x1 is the rule):
sweep-rand-1n.out     job 22015476, cn022        uniform, RMAT  x1/16 .. x16
sweep-mesh-1n.out     job 22015477, cn047        mesh           x1/64 .. x16
sweep-rand-2n.out     job 22015478, cn068 + 1    uniform, RMAT  x1/16 .. x16
sweep-mesh-2n.out     job 22015479, cn022 + 1    mesh           x1/64 .. x16
coarse-1n.out         job 22015863, cn030        uniform, RMAT  x1, x16 .. x256
coarse-2n.out         job 22015866, cn[053,120]  uniform, RMAT  x1, x16 .. x256

--bucket-policy adaptive against fixed, targets 1, 2, 4, 8:
policy-1n.out         job 22016190, cn013
policy-2n.out         job 22016191, cn013 + 1
policy-1n-s22.out     job 22016192, cn013        2^22

Confirmation, 10 repetitions, with fixed-again (the baseline configuration,
run last in each repetition) as a control for the harness's position bias:
confirm-1n-s20.out    job 22016818, cn013        2^20, one node
confirm-2n-s22.out    job 22016819, cn048 + 1    2^22, two nodes

Correctness:
2node-gate-t1.out     job 22016397  scripts/verify_2node.sh, adaptive target 1
2node-gate-t4.out     job 22016399  adaptive target 4
2node-gate-default.out job 22017017  the committed code, default (adaptive, target 8)
2node-gate-fixed.out  job 22017018  the committed code, --bucket-policy fixed
