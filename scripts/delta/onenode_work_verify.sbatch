#!/usr/bin/env bash
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=00:05:00
#SBATCH --job-name=acic-R0-verify
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=cray_shasta PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND
python3 - "$ROOT" "$APP" <<'PY'
import os, struct, subprocess, sys
from pathlib import Path
root, app = map(Path, sys.argv[1:])
sys.path.insert(0, str(app/'benchmarks'))
from work_cost_report import counters
out = root/'logs'/f'R0-verify-{os.environ["SLURM_JOB_ID"]}'
out.mkdir(exist_ok=False)
n = 1025
adj = [[] for _ in range(n)]
for u in range(n-2):
    for v, w in [(u+1, 1+u%17), (min(u+7, n-2), 35)]:
        adj[u].append((v,w)); adj[v].append((u,w))
offsets = [0]
for row in adj: offsets.append(offsets[-1]+len(row))
graph=out/'fixture.wsg'
with graph.open('wb') as f:
    f.write(struct.pack('=Bqq',0,offsets[-1],n))
    f.write(struct.pack('='+'q'*len(offsets),*offsets))
    for row in adj:
        for v,w in row: f.write(struct.pack('=ii',v,w))
for binary in ['acic_r0_control','acic_r0_diag_v3']:
    for share in ['off','on']:
        for tile in ['off','auto']:
            log=out/f'{binary}-{share}-{tile}.out'
            command=['srun','-N','1','-n','2','--ntasks-per-node=2','-c','8','--cpu-bind=none',
                     '--kill-on-bad-exit=1',str(root/'bin'/binary),'0',str(graph),'1','0','4','0.999','0.005',
                     '--sources','0,512,1024,0','--verify','--timeout','30','--process-share',share,
                     '--reader-tile',tile,'--slack-control','off','+ppn','4','+lci_ndevices','4']
            with log.open('w') as f:
                subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
            text=log.read_text()
            assert text.count('VERIFY PASS')==4
            assert not any(x in text for x in ['PROGRESS_STALL','CONSERVATION VIOLATED','STALL_RESCUE'])
            if 'diag' in binary:
                lines=[l[l.index('WORK_COST '):] for l in text.splitlines() if 'WORK_COST ' in l]
                assert len(lines)==4
                for line in lines: counters(line)
for source in [0,512,1024]:
    logs=[]
    for binary in [root/'bin/gap_sssp',root/'build/gap_r0/gap_work_cost']:
        log=out/f'{binary.name}-{source}.out'
        with log.open('w') as f:
            subprocess.run(['srun','-N','1','-n','1','-c','4',str(binary),str(graph),str(source),'16'],
                env=dict(os.environ,OMP_NUM_THREADS='4'),stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
        text=log.read_text(); logs.append(text.split(' h1=')[1].split(' rss_kib=')[0])
        if binary.name=='gap_work_cost': counters(text)
    assert logs[0]==logs[1]
(out/'complete').write_text('32 ACIC solves and 6 GAPBS solves passed; diagnostic queue identities passed.\n')
print('R0 VERIFY PASS',out)
PY
