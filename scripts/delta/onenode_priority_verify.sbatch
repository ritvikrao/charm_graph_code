#!/usr/bin/env bash
# R1 correctness precedes performance submission. Small shared-node fixture.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=00:05:00
#SBATCH --job-name=acic-R1-verify
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND
python3 - "$ROOT" "$APP" <<'PY'
import os, re, struct, subprocess, sys
from pathlib import Path
root, app = map(Path, sys.argv[1:])
sys.path.insert(0, str(app/'benchmarks'))
from work_cost_report import check_work_accounting
out = root/'logs'/f'R1-verify-{os.environ["SLURM_JOB_ID"]}'
out.mkdir(exist_ok=False)

def graph_file(name, adj):
    offsets = [0]
    for row in adj: offsets.append(offsets[-1]+len(row))
    path = out/(name+'.wsg')
    with path.open('wb') as f:
        f.write(struct.pack('=Bqq',0,offsets[-1],len(adj)))
        f.write(struct.pack('='+'q'*len(offsets),*offsets))
        for row in adj:
            for v,w in row: f.write(struct.pack('=ii',v,w))
    return path

n = 1025
adj = [[] for _ in range(n)]
for u in range(n-2):
    for v,w in [(u+1,1+u%17),(min(u+7,n-2),35)]:
        adj[u].append((v,w)); adj[v].append((u,w))
sparse = graph_file('sparse',adj)
dense = graph_file('dense',[[(v,1+(u+v)%17) for v in range(65) if u!=v] for u in range(65)])
solves = 0

def run(binary, share, tile, queue, graph=sparse, sources='0,512,1024,0', extra=()):
    global solves
    label = f'{binary}-{share}-{tile}-{queue}-{graph.stem}' + ('-range' if extra else '')
    log = out/(label+'.out')
    command = ['srun','-N','1','-n','2','--ntasks-per-node=2','-c','8','--cpu-bind=none',
        '--kill-on-bad-exit=1',str(root/'bin'/binary),'0',str(graph),'1','0','4','0.999','0.005',
        '--sources',sources,'--verify','--timeout','30','--process-share',share,
        '--reader-tile',tile,'--process-queue',queue,'--slack-control','off','--lazy-heavy','off',
        '+ppn','4','+lci_ndevices','4'] + list(extra)
    with log.open('w') as f:
        subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
    text = log.read_text()
    expected = len(sources.split(','))
    assert text.count('VERIFY PASS')==expected, log
    assert not any(x in text for x in ['PROGRESS_STALL','CONSERVATION VIOLATED','STALL_RESCUE']), log
    active = share=='on' or (share=='auto' and graph==sparse)
    assert f'Process queue: {queue}' + ('' if active else ' (inactive)') in text, log
    assert f'Process sharing: {"on" if active else "off"}' in text, log
    chunks = re.split(r'^SOURCE_RUN index=\d+ source=\d+\n',text,flags=re.M)[1:]
    assert len(chunks)==expected, log
    if 'diag' in binary:
        for chunk in chunks:
            try:
                check_work_accounting(chunk,n if graph==sparse else 65)
            except ValueError as error:
                raise ValueError(f'{log}: {error}') from error
    solves += expected

for binary in ['acic_r1_priority','acic_r1_priority_diag_v2']:
    for share in ['off','on']:
        for tile in ['off','auto']:
            for queue in ['local','nearest']:
                run(binary,share,tile,queue)
    for queue in ['local','nearest']:
        run(binary,'auto','auto',queue,dense,'0,32,64,0')
    run(binary,'on','auto','nearest',extra=['--bucket-width','1','--range-extend','on','--skew-defer','on'])
(out/'complete').write_text(f'{solves} ACIC solves passed; full serial verification, source resets, queue and edge accounting.\n')
print('R1 VERIFY PASS',solves,out)
PY
