#!/usr/bin/env python3
"""Serial + Bellman-certificate gates for private queues, empty PEs and resets."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('campaign',type=Path)
ap.add_argument('binaries',nargs='+')
ap.add_argument('--heap-slice',type=int,default=8)
a=ap.parse_args();root=a.campaign;app=Path(__file__).resolve().parents[1]
out=root/'logs'/('chunk-gate-'+os.environ['SLURM_JOB_ID']);out.mkdir()
for shape,n in [('empty',16),('path',1025),('disconnected',2051),('mesh',4096)]:
    adj=[[] for _ in range(n)]
    def add(u,v,w):adj[u].append((v,w));adj[v].append((u,w))
    if shape in ['path','disconnected']:
        for v in range(n-2):
            if shape=='disconnected' and v==1023:continue
            add(v,v+1,1+(v*97)%1000)
    if shape=='mesh':
        for v in range(n):
            if v%64<63:add(v,v+1,1+(v*31)%1000)
            if v+64<n:add(v,v+64,1+(v*71)%1000)
    offsets=[0]
    for row in adj:offsets.append(offsets[-1]+len(row))
    graph=out/(shape+'.wsg')
    with graph.open('wb') as f:
        f.write(struct.pack('=Bqq',0,offsets[-1],n));f.write(struct.pack('='+'q'*len(offsets),*offsets))
        for row in adj:
            for v,w in row:f.write(struct.pack('=ii',v,w))
    for binary in a.binaries:
        log=out/(binary+'-'+shape+'.out')
        sources=f'0,{n//2},{n-1},0'
        cmd=['srun','-N1','-n16','--ntasks-per-node=16','-c8','--cpu-bind=none','--unbuffered','--kill-on-bad-exit=1','--time=5','bash',str(app/'benchmarks/launch_acic.sh'),'16','112',str(root/'bin'/binary),'0',str(graph),'1','0','4','0.999','0.005','--sources',sources,'--verify','--certify','--result-digest','--timeout','60','--process-share','on','--reader-tile','16','--process-queue','nearest','--process-queue-batch','8','--heap-slice',str(a.heap_slice),'--bucket-width','1','--slack-control','off','+old-scheduler']
        with log.open('w') as stream:subprocess.run(cmd,stdout=stream,stderr=subprocess.STDOUT,check=True)
        t=log.read_text()
        assert len(re.findall(r'^VERIFY PASS',t,re.M))==4,(log,'serial')
        assert len(re.findall(r'^CERTIFY PASS',t,re.M))==4,(log,'certifier')
        assert not re.search(r'PROGRESS_STALL|STALL_RESCUE|CONSERVATION VIOLATED|VERIFY FAIL|CERTIFY FAIL|TRUNCATED',t),log
        if binary.endswith('_cost'):
            for c in re.findall(r'WORK_COST ([^\n]+)',t):
                d=dict(kv.split('=') for kv in c.split())
                assert int(d['queue_pushes'])==int(d['queue_pops'])
                assert int(d['expansions'])+int(d['stale_pops'])==int(d['queue_pops'])
        (out/(binary+'-'+shape+'.command.json')).write_text(json.dumps(cmd)+'\n')
        print('PASS',binary,shape,'4 serial + 4 certificates',flush=True)
print('GATE COMPLETE',out,flush=True)
