#!/usr/bin/env python3
import json,glob,re,collections,statistics as st,sys
# Allocation A of the larger-input road/mesh study (design/large-scale-plan.md):
# per graph and node count, ACIC (frozen, tls) against Gluon, RIKEN and the
# one-node GAPBS/Wasp references, per held-out source (speedup = baseline / ACIC).
L='/lustre/orion/csc710/scratch/rrao/acic/campaign/logs/'
JOBS='5541369 5541370 5541371 5541660 5541426 5541428 5541429 5541663 5541664 5541665 5541666 5541667 5541668 5541713 5541714 5541715 5541716 5541717'.split()
ONE={'gap':{'road-usa-z':'5529591','mesh26-z':'5538465','mesh24-z':'5538410','road-na-z':'5541358','road-eu-z':'5541358','mesh28-z':'5541661','mesh30-z':'5541661'},
     'wasp':{'road-usa-z':'5536541','mesh26-z':'5536541','mesh24-z':'5538411','road-na-z':'5541359','road-eu-z':'5541359','mesh28-z':'5541662','mesh30-z':'5541662'}}
def lines(p):
    for l in open(p):
        try: yield json.loads(l)
        except Exception: pass
base=collections.defaultdict(list); err=collections.defaultdict(list); bad=collections.Counter(); src=collections.defaultdict(set)
for j in JOBS:
    for p in glob.glob(L+f'external-*n-56w-{j}.jsonl'):
        n=int(re.search(r'external-(\d+)n',p)[1])
        for r in lines(p):
            if r['phase']!='external': continue
            arm=r['config']['name']; arm='gluon' if arm.startswith('gluon') else arm
            if not r['valid']: bad[(r['graph'],n,arm,r['outcome'])]+=1; continue
            base[(r['graph'],n,arm,str(r['source']))].append(r['seconds']); src[(r['graph'],n,arm)].add(j)
            if r.get('exact') is False: err[(r['graph'],n)].append(r['relative_distance_error'])
acic=collections.defaultdict(list); abad=collections.Counter(); stalls=collections.Counter()
for j in JOBS:
    for d in glob.glob(L+f'AB-*-{j}'):
        g,n=re.search(r'AB-(.*)-(\d+)n-',d).groups(); n=int(n)
        for r in lines(d+'/runs.jsonl'):
            if r['rep']<0: continue
            if not r['valid']: abad[(g,n,r['variant'])]+=1; continue
            if r.get('progress_stall_lines') or r.get('stall_rescues'): stalls[(g,n,r['variant'])]+=1
            acic[(g,n,r['variant'],r['source'])].append(r['seconds']); src[(g,n,r['variant'])].add(j)
one=collections.defaultdict(list)
for eng,m in ONE.items():
    for g,j in m.items():
        for r in lines(glob.glob(L+f'external-1n-*-{j}.jsonl')[0]):
            if r['phase']=='external' and r['valid'] and r['graph']==g: one[(g,eng,str(r['source']))].append(r['seconds'])
out={}
cells=sorted({(g,n) for g,n,_,_ in acic}|{(g,n) for g,n,_,_ in base}, key=lambda x:(x[0][:4],int(re.sub(r'\D','',x[0]) or 0),x[0],x[1]))
for g,n in cells:
    print(f'\n## {g} @ {n}n')
    for arm in ['frozen','tls']:
        ss=sorted({s for gg,nn,a,s in acic if (gg,nn,a)==(g,n,arm)})
        if not ss: print(f'  {arm}: no ACIC data'); continue
        t={s:st.median(acic[(g,n,arm,s)]) for s in ss}
        row=[f'  {arm} ({",".join(sorted(src[(g,n,arm)]))}) t={min(t.values()):.3f}-{max(t.values()):.3f}s']
        for b in ['gluon','riken']:
            sp=[st.median(base[(g,n,b,s)])/t[s] for s in ss if base.get((g,n,b,s))]
            if sp: row.append(f'{b}[{",".join(sorted(src[(g,n,b)]))}] {min(sp):.1f}-{max(sp):.1f}x/{len(sp)}')
        for b in ['gap','wasp']:
            sp=[st.median(one[(g,b,s)])/t[s] for s in ss if one.get((g,b,s))]
            if sp: row.append(f'{b} {min(sp):.2f}-{max(sp):.2f}x/{len(sp)}')
        if arm=='tls':
            sp=[st.median(acic[(g,n,'frozen',s)])/t[s] for s in ss if acic.get((g,n,'frozen',s))]
            if sp: row.append(f'tls/frozen {min(sp):.2f}-{max(sp):.2f}x')
        print('  '.join(row))
    for b in ['gluon','riken']:
        ts=[st.median(v) for (gg,nn,a,s),v in base.items() if (gg,nn,a)==(g,n,b)]
        if ts: print(f'  {b} t={min(ts):.2f}-{max(ts):.2f}s ({len(ts)} src)')
    if err.get((g,n)): print(f'  riken inexact: rel err {min(err[(g,n)]):.2e}-{max(err[(g,n)]):.2e}')
    for k,v in list(bad.items())+list(abad.items()):
        if k[:2]==(g,n): print('  INVALID',k,v)
    for k,v in stalls.items():
        if k[:2]==(g,n): print('  stall/rescue rows',k,v)
