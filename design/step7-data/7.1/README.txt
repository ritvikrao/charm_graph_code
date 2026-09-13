Step 7.1 A/B job outputs. Each file is the full Slurm output: per-repetition
compute times as they ran, then the diag_report.py table.
These runs predate the 2026-09-13 change to diag_report.py, so their ratio
column is headed "vs base" and holds variant/baseline: below 1 means faster
there, the reciprocal of the "speedup" the tool prints now and of the numbers
quoted in design/step7-flush-cadence.md.

1node-gated.out            job 22008792, cn022        fixed / interval1 / stale / adaptive
2node-gated.out            job 22008797, cn[022,053]  fixed / interval1 / stale / adaptive
1node-stale-only.out       job 22008504, cn116        fixed / interval1 / "adaptive"
2node-stale-only.out       job 22008532, cn[114,116]  fixed / interval1 / "adaptive"
1node-stale-only-15reps.out job 22008640, cn047       uniform and RMAT only, 15 reps

In the three *-stale-only files the variant printed as "adaptive" is the
first, ungated policy -- what the code now calls --flush-policy stale. The
gate was added after 2node-stale-only.out showed its cost on two nodes.

Ratios are only comparable within a file: the same configuration's absolute
time differs by up to 45% between nodes.
