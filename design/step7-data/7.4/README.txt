Step 7.4 job outputs. Full Slurm output: per-repetition compute times, then the
diag_report.py table. These runs predate the 2026-09-13 change to
diag_report.py, so their ratio column is headed "vs base" and holds
variant/baseline -- below 1 means faster there. The tool now prints
baseline/variant under the heading "speedup", which is how
design/step7-idle-flush.md quotes the same runs, so the two are reciprocals of
each other. Ratios are only comparable within a file. All 2^20, 16 PEs per
node, on top of the step 7.1 and 7.3 defaults.

The four-variant sweep -- off, on, starved, off-again -- 10 repetitions:
sweep-1n.out          job 22030830, cn039        one node
sweep-2n.out          job 22030804, cn039 + 1    two nodes

Confirmation, off / starved / off-again only, 20 repetitions. off-again is the
baseline configuration run last in each repetition, so its ratio measures the
harness's own position bias:
confirm-1n.out        job 22030961, cn129        one node
confirm-2n.out        job 22030962, cn017 + 1    two nodes

Correctness, scripts/verify_2node.sh on cn[110,114]:
2node-gate-on.out     job 22030819  --idle-flush on
2node-gate-starved.out job 22030820  --idle-flush starved
2node-gate-default.out job 22031288, cn[030,078]  the committed code, default
