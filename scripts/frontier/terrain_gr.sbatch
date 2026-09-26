#!/usr/bin/env bash
# Galois .gr (version 2) of an existing GLO-90 terrain graph, written straight
# from the tiles by terrain_graph2 --gr with no .wsg (to_galois.py on the
# 1.1 TB .wsg ran past the 2-hour limit, job 5546090). terrain_graph2's .gr is
# byte-identical to to_galois.py's on the Alps test; here a million random
# offsets and edges are also checked against the existing .wsg.
#
#   sbatch scripts/frontier/terrain_gr.sbatch CAMPAIGN [NAME] (REGION, SEED as prepare_terrain.sbatch)
#SBATCH --account=csc710
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --time=02:00:00
#SBATCH --job-name=acic-terrain-gr
set -euo pipefail
C=${1:?campaign}
NAME=${2:-terrain-ae-z}
REGION=${REGION:-"-50 50 -20 180"}
SEED=${SEED:-"40 75"}
PY=${ACIC_PYTHON:-$(dirname "$C")/venv/bin/python}
G=$C/graphs/$NAME
echo "job=$SLURM_JOB_ID host=$(hostname) name=$NAME region=$REGION seed=$SEED"
sha256sum "$C/bin/terrain_graph2"
[ -e "$G.gr.tmp" ] || lfs setstripe -c 64 -S 16M "$G.gr.tmp"
# shellcheck disable=SC2086
OMP_NUM_THREADS=56 OMP_PROC_BIND=spread /usr/bin/time -v "$C/bin/terrain_graph2" "$C/raw/dem/glo90/f32" $REGION $SEED 6 - \
  --arcsec 3 --gr "$G.gr.tmp" > "$G.gr.log" 2> "$G.gr.err" || { cat "$G.gr.err"; echo "GR FAILED"; exit 1; }
cat "$G.gr.log"; grep -E 'Maximum resident|Elapsed' "$G.gr.err"
diff <(sed -n 's/^sources //p' "$G.gr.log") "$G.sources" && echo "sources match $NAME.sources"
"$PY" - "$G.wsg" "$G.gr.tmp" <<'P'
import sys, struct, numpy as np
wsg, gr = sys.argv[1:]
d, m, n = struct.unpack('<Bqq', open(wsg, 'rb').read(17))
v, w, gn, gm = struct.unpack('<QQQQ', open(gr, 'rb').read(32))
assert (v, w, gn, gm) == (2, 4, n, m), (v, w, gn, gm, n, m)
off = np.memmap(wsg, '<i8', 'r', 17, (n + 1,))
rec = np.memmap(wsg, np.dtype([('v', '<i8'), ('w', '<i4'), ('p', '<i4')]), 'r', 17 + (n + 1) * 8, (m,))
gend = np.memmap(gr, '<u8', 'r', 32, (n,))
gdst = np.memmap(gr, '<u8', 'r', 32 + 8 * n, (m,))
gw = np.memmap(gr, '<i4', 'r', 32 + 8 * n + 8 * m, (m,))
rng = np.random.default_rng(20260926)
i = np.sort(rng.integers(0, n, 1000000)); e = np.sort(rng.integers(0, m, 1000000))
ok = (np.array_equal(gend[i], off[i + 1]) and gend[n - 1] == m and np.array_equal(gdst[e], rec['v'][e])
      and np.array_equal(gw[e], rec['w'][e]))
import os
ok = ok and os.path.getsize(gr) == 32 + 8 * n + 12 * m
print('GR SPOT CHECK', 'PASS' if ok else 'FAIL', f'n={n} m={m}')
sys.exit(0 if ok else 1)
P
mv "$G.gr.tmp" "$G.gr"; touch "$G.gr.done"
ls -la "$G.gr"; echo "GR COMPLETE $NAME"
