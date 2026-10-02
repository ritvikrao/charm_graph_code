#!/usr/bin/env bash
# D7 launch check: why D7's Wasp on road-eu-z (0.33-0.40 s at t64 d1024 and
# t96 d4096) is slower than sec. 23's identical binary, graph and setting
# (0.24 s). Interleaves sec. 23's exact launch, both D7 launches and one-change
# variants on the training sources, three repetitions, every solve's digest
# checked against the campaign reference.
#
#   sbatch scripts/delta/d7_launch_ab.sbatch CAMPAIGN
#SBATCH --account=rfp-delta-cpu
#SBATCH --partition=cpu-interactive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=16
#SBATCH --cpus-per-task=8
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --time=00:25:00
#SBATCH --job-name=acic-d7l
#SBATCH --output=%x-%j.out
set -euo pipefail
CAMPAIGN=${1:?campaign}
OUT=$CAMPAIGN/logs/d7-launch-$SLURM_JOB_ID
mkdir -p "$OUT"
exec > >(tee "$OUT/driver.out") 2>&1
echo "job=$SLURM_JOB_ID host=$(hostname)"
lscpu | grep -E 'Model name|NUMA node\(s\)|Socket|Thread'
python3 - "$CAMPAIGN" "$OUT" <<'PY'
import itertools, json, os, re, subprocess, sys, statistics
root, out = sys.argv[1], sys.argv[2]
binary = {'wasp': f'{root}/bin/wasp_sssp', 'gap': f'{root}/bin/gap_sssp'}
# (name, srun binding, -c, OMP_PROC_BIND, extra env)
campaign_env = dict(PMI_MAX_KVS_ENTRIES='100000', FI_CXI_RX_MATCH_MODE='hybrid', NO_AFFINITY='1')
arms = [
    ('s23',         'none',  '128', 'close',  {}),               # sec. 23 exactly
    ('d7spread',    'cores', '128', 'spread', campaign_env),     # D7 as run
    ('d7packed',    'cores', 'T',   'close',  campaign_env),     # D7 before aed4cd6
    ('s23_cores',   'cores', '128', 'close',  {}),               # one change: srun binding
    ('s23_campenv', 'none',  '128', 'close',  campaign_env),     # one change: campaign env
    ('s23_spread',  'none',  '128', 'spread', {}),               # one change: OMP spread
]
cells = [('wasp', 'road-eu-z', 64, 1024), ('wasp', 'road-eu-z', 96, 4096),
         ('wasp', 'road-usa-z', 64, 32768), ('gap', 'road-usa-z', 64, 32768)]
refs = {}
for g in ('road-eu-z', 'road-usa-z'):
    for line in open(f'{root}/graphs/{g}.reference.txt'):
        f = line.split()
        if f and f[0].isdigit() and f[1] == 'tune':
            refs.setdefault(g, {})[f[0]] = (f[2], f[3])
rows = []
for rep in range(3):
    for (engine, graph, threads, delta), src in itertools.product(cells, (0, 1)):
        source = sorted(refs[graph])[src]
        order = arms[rep % len(arms):] + arms[:rep % len(arms)]
        for name, bind, cpus, omp, extra in order:
            env = dict(os.environ, OMP_DYNAMIC='FALSE', OMP_PLACES='cores',
                       OMP_PROC_BIND=omp, OMP_NUM_THREADS=str(threads), **extra)
            c = str(threads) if cpus == 'T' else cpus
            cmd = ['srun', '-N1', '-n1', '--ntasks-per-node=1', '-c', c, f'--cpu-bind={bind}',
                   '--unbuffered', '--kill-on-bad-exit=1', '--time=5',
                   binary[engine], f'{root}/graphs/{graph}.wsg', source, str(delta)]
            p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=300)
            m = re.search(r'BENCH source=(\d+) solve_seconds=([0-9.]+) h1=(\d+) h2=(\d+)', p.stdout)
            ok = bool(m) and (m.group(3), m.group(4)) == refs[graph][source]
            row = dict(arm=name, engine=engine, graph=graph, threads=threads, delta=delta,
                       source=source, rep=rep, valid=ok, seconds=float(m.group(2)) if m else None)
            rows.append(row)
            print(json.dumps(row), flush=True)
            if not ok:
                print(p.stdout[-2000:], p.stderr[-2000:], flush=True)
json.dump(rows, open(f'{out}/rows.json', 'w'), indent=1)
print('\nmedian seconds (valid solves), per cell and arm:')
for cell in cells:
    for src in sorted(refs[cell[1]]):
        line = []
        for name, *_ in arms:
            t = [r['seconds'] for r in rows if (r['engine'], r['graph'], r['threads'], r['delta']) == cell
                 and r['source'] == src and r['arm'] == name and r['valid']]
            line.append(f'{name}={statistics.median(t):.3f}' if t else f'{name}=NA')
        print(*cell, src, *line)
print('invalid:', sum(not r['valid'] for r in rows), 'of', len(rows))
print('D7 LAUNCH AB COMPLETE')
PY
