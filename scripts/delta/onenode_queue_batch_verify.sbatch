#!/usr/bin/env bash
# R1 batching correctness; use --nodes=2 to check distributed execution too.
#SBATCH --account=mzu-delta-cpu
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=00:03:00
#SBATCH --job-name=acic-R1-batch-verify
set -euo pipefail
ulimit -c 0
ROOT=${1:?campaign}
APP=${ACIC_BENCH_APP:-$HOME/charm_graph_code}
export SLURM_MPI_TYPE=${ACIC_SRUN_MPI:-cray_shasta} PMI_MAX_KVS_ENTRIES=100000 FI_CXI_RX_MATCH_MODE=hybrid
unset LCT_PMI_BACKEND
python3 - "$ROOT" "$APP" <<'PY'
import hashlib, json, os, re, struct, subprocess, sys
from pathlib import Path
root, app = map(Path, sys.argv[1:])
sys.path.insert(0, str(app/'benchmarks'))
from work_cost_report import check_work_accounting
out = root/'logs'/f'R1-batch-verify-{os.environ["SLURM_JOB_ID"]}'
out.mkdir(exist_ok=False)
nodes = int(os.environ['SLURM_NNODES'])
assert nodes in (1,2), nodes
binaries = os.environ.get('VERIFY_BINARIES','acic_r1_batch,acic_r1_batch_diag').split(',')
extra_flags = os.environ.get('VERIFY_EXTRA','').split()
(out/'manifest.json').write_text(json.dumps(dict(nodes=nodes,processes=2*nodes,
    workers_per_process=4,hosts=os.environ['SLURM_JOB_NODELIST'],
    extra_flags=extra_flags,binaries={b:hashlib.sha256((root/'bin'/b).read_bytes()).hexdigest() for b in binaries}),indent=2)+'\n')

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
inter_node_checks = 0

def run(binary, share, tile, queue, batch, graph=sparse, sources='0,512,1024,0', extra=()):
    global solves, inter_node_checks
    label = f'{binary}-{share}-{tile}-{queue}-b{batch}-{graph.stem}' + ('-range' if extra else '')
    log = out/(label+'.out')
    # ACIC_SRUN_EXTRA carries machine-specific launcher flags, e.g. Frontier's
    # --network=single_node_vni, which LCI needs when a job stays on one node.
    command = ['srun','-N',str(nodes),'-n',str(2*nodes),'--ntasks-per-node=2','-c','8','--cpu-bind=none',
        *os.environ.get('ACIC_SRUN_EXTRA','').split(),'--kill-on-bad-exit=1',str(root/'bin'/binary),'0',str(graph),'1','0','4','0.999','0.005',
        '--sources',sources,'--verify','--timeout','30','--process-share',share,
        '--reader-tile',tile,'--process-queue',queue,'--process-queue-batch',str(batch),'--slack-control','off','--lazy-heavy','off',
        '+ppn','4','+lci_ndevices','4'] + list(extra) + extra_flags
    with log.open('w') as f:
        subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
    text = log.read_text()
    expected = len(sources.split(','))
    assert f'Starting Reconverse with {2*nodes} processes, {8*nodes} PEs (1 PE = 1 thread), and 4 PEs per process' in text, log
    assert text.count('VERIFY PASS')==expected, log
    assert not any(x in text for x in ['PROGRESS_STALL','CONSERVATION VIOLATED','STALL_RESCUE']), log
    active = share=='on' or (share=='auto' and graph==sparse)
    assert f'Process queue: {queue}' + ('' if active else ' (inactive)') in text, log
    assert f'Process queue batch: {batch}' + ('' if active else ' (inactive)') in text, log
    assert f'Process sharing: {"on" if active else "off"}' in text, log
    chunks = re.split(r'^SOURCE_RUN index=\d+ source=\d+\n',text,flags=re.M)[1:]
    assert len(chunks)==expected, log
    assert re.findall(r'^SOURCE_RUN index=\d+ source=(\d+)$',text,flags=re.M)==sources.split(','), log
    if 'diag' in binary:
        for source, chunk in zip(sources.split(','), chunks):
            try:
                counts = check_work_accounting(chunk,n if graph==sparse else 65)
            except ValueError as error:
                raise ValueError(f'{log}: {error}') from error
            if nodes > 1 and (graph==dense or source!='1024'):
                assert counts['inter_node'] > 0, (log, source, 'no inter-node work')
                inter_node_checks += 1
    solves += expected

for binary in binaries:
    for batch in [1,8,32,64]:
        for tile in ['off','auto']:
            for queue in ['local','nearest']:
                run(binary,'on',tile,queue,batch)
    for queue in ['local','nearest']:
        run(binary,'off','auto',queue,32)
        run(binary,'auto','auto',queue,32,dense,'0,32,64,0')
        for batch in [8,32]:
            run(binary,'on','auto',queue,batch,extra=['--bucket-width','1','--range-extend','on','--skew-defer','on'])
        for batch in [32,64]:
            run(binary,'on','auto',queue,batch,dense,'0,32,64,0')
assert solves == 112*len(binaries), solves
assert inter_node_checks == (90*sum('diag' in b for b in binaries) if nodes > 1 else 0), inter_node_checks
(out/'complete').write_text(f'{solves} ACIC solves passed; full serial verification, source resets, queue and edge accounting.\n')
print('R1 BATCH VERIFY PASS',solves,'nodes',nodes,'inter-node checks',inter_node_checks,out)
PY
