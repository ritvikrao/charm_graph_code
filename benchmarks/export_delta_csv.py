#!/usr/bin/env python3
"""Export every per-solve record from the Delta campaigns to flat CSV files.

  export_delta_csv.py [OUTDIR]        (default design/delta-data)

Writes OUTDIR/delta_solves.csv (one row per solve, every harness normalized to
one schema) and OUTDIR/delta_jobs.csv (one row per Slurm job, with sacct
metadata, the design document and heading that report it, and the first
commit that mentions it). See
OUTDIR/README.md for the columns.

Sources: runs.jsonl (onenode_ab and the per-study harnesses), run.py campaign
records (*.jsonl), and the D7 launch checks (rows.json) under the Delta
campaign directories. Logs with no structured record (.out only) are not
read. Values come from each record's own fields first, then from its launch
command, then from the directory's manifest.json.
"""
import ast, csv, json, re, subprocess, sys
from pathlib import Path

APP = Path(__file__).resolve().parent.parent
CAMPAIGN_ROOTS = sorted(Path('/work/hdd/mzu/rao1').glob('acic-*')) + [Path('/u/rao1/.tmp/ipdps27-onenode')]
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else APP / 'design' / 'delta-data'

COLUMNS = ['campaign', 'job', 'harness', 'record_dir', 'graph', 'source', 'role', 'phase', 'rep',
           'warmup', 'engine', 'arm', 'binary', 'nodes', 'rpn', 'workers', 'threads', 'cpus',
           'omp_bind', 'delta', 'bucket_width', 'heap_slice', 'chunk_band', 'leaf_prune',
           'process_queue', 'flags', 'seconds', 'valid', 'outcome', 'rounds', 'edge_attempts',
           'attempts_per_edge', 'wire_bytes_per_edge', 'node_bytes_per_edge', 'host', 'selection_job']
BINARY = re.compile(r'^(acic\w*|wasp\w*|gap_sssp\w*|gluon_sssp\w*|riken_sssp\w*|gemini_sssp\w*|havoqgt_sssp\w*|sssp_smp\w*)$')
GRAPH_EXT = re.compile(r'\.(wsg|sg|gr|bin|csv|el|wel)$')
ACIC_KNOBS = {'--bucket-width': 'bucket_width', '--heap-slice': 'heap_slice', '--chunk-band': 'chunk_band',
              '--leaf-prune': 'leaf_prune', '--process-queue': 'process_queue'}


def engine_of(binary):
    if not binary: return ''
    for prefix, name in (('acic', 'acic'), ('sssp_smp', 'acic'), ('wasp_feat', 'wasp-features'), ('wasp', 'wasp'),
                         ('gap', 'gap'), ('gluon', 'gluon'), ('riken', 'riken'), ('gemini', 'gemini'),
                         ('havoqgt', 'havoqgt')):
        if binary.startswith(prefix): return name
    return binary


def parse_command(cmd):
    """Binary, graph, srun -c, ACIC flags and a GAPBS/Wasp delta from a launch command."""
    out = {}
    if not cmd: return out
    argv = cmd
    if isinstance(cmd, str):
        try: argv = ast.literal_eval(cmd)
        except (ValueError, SyntaxError): argv = cmd.split()
    argv = [str(a) for a in argv]
    for i, a in enumerate(argv):
        if a == '-c' and i + 1 < len(argv) and 'cpus' not in out: out['cpus'] = argv[i + 1]
        if a.startswith('--cpus-per-task='): out.setdefault('cpus', a.split('=', 1)[1])
        if a.startswith('-t=') and a[3:].isdigit(): out['threads'] = a[3:]  # Gluon
        if a == '--ntasks-per-node' and i + 1 < len(argv): out.setdefault('rpn', argv[i + 1])
        if a.startswith('--ntasks-per-node='): out.setdefault('rpn', a.split('=', 1)[1])
        if Path(a).name == 'launch_acic.sh' and i + 2 < len(argv):  # launch_acic.sh RANKS_PER_NODE WORKERS_PER_NODE ...
            out['rpn'], out['workers'] = argv[i + 1], argv[i + 2]
    bi = next((i for i, a in enumerate(argv) if BINARY.match(Path(a).name)), None)
    if bi is None: return out
    out['binary'] = Path(argv[bi]).name
    rest = argv[bi + 1:]
    gi = next((i for i, a in enumerate(rest) if GRAPH_EXT.search(a)), None)
    if gi is not None:
        out['graph'] = GRAPH_EXT.sub('', Path(rest[gi]).name)
        # GAPBS and Wasp: BINARY GRAPH SOURCE DELTA
        if engine_of(out['binary']) in ('gap', 'wasp', 'wasp-features') and gi + 2 < len(rest):
            out['delta'] = rest[gi + 2]
    flags, i = [], 0
    while i < len(rest):
        a = rest[i]
        if a.startswith('--') or a.startswith('+'):
            val = rest[i + 1] if a.startswith('--') and i + 1 < len(rest) and not rest[i + 1].startswith(('--', '+')) else None
            if '=' in a: a, val = a.split('=', 1)
            flags.append(a if val is None else f'{a} {val}')
            if a in ACIC_KNOBS and val is not None: out[ACIC_KNOBS[a]] = val
            i += 2 if val is not None and '=' not in rest[i] else 1
        else:
            i += 1
    out['flags'] = ' '.join(flags)
    return out


def flags_info(flags):
    """ACIC knobs from a manifest's flag list."""
    if not flags: return {}
    return parse_command(['acic'] + list(flags))


def efficiency(r):
    e = r.get('efficiency')
    if isinstance(e, str):
        try: e = ast.literal_eval(e)
        except (ValueError, SyntaxError): e = None
    return e or {}


def job_of(path):
    ids = re.findall(r'(?<!\d)(\d{7,8})(?!\d)', str(path))
    return ids[-1] if ids else ''


def manifest_of(d):
    p = d / 'manifest.json'
    try: return json.loads(p.read_text())
    except (OSError, ValueError): return {}


def manifest_graph(m):
    g = m.get('graph') or (m.get('protocol') or {}).get('graph')
    if isinstance(g, dict): g = g.get('path')
    return GRAPH_EXT.sub('', Path(g).name) if g else ''


def manifest_host(m):
    return m.get('host') or m.get('hosts') or ''


def normalize(r, *, campaign, job, harness, record_dir, manifest, variants):
    cfg = r.get('config') or {}
    setting = r.get('setting') if isinstance(r.get('setting'), dict) else {}
    layout = r.get('layout') if isinstance(r.get('layout'), dict) else {}
    cmd = parse_command(r.get('command'))
    arm = r.get('variant') or r.get('label') or r.get('arm') or cfg.get('name') or r.get('mode') or r.get('engine') or ''
    var = variants.get(arm, {})
    vinfo = flags_info(var.get('flags'))
    if r.get('flags') and isinstance(r['flags'], list): vinfo = {**vinfo, **flags_info(r['flags'])}
    binary = cmd.get('binary') or var.get('binary') or r.get('binary') or ''
    engine = cfg.get('engine') or engine_of(binary) or engine_of(str(r.get('engine', ''))) or (
        'acic' if harness == 'onenode_ab' else str(r.get('arm') or r.get('engine') or ''))
    if harness == 'onenode_ab' and not binary: engine = 'acic'
    if harness.startswith('GAP') and not binary: engine = 'gap'
    e = efficiency(r)
    phase = str(r.get('phase', ''))
    rep = r.get('rep', '')
    valid = r.get('valid', r.get('digest_valid', ''))
    attempts = r.get('edge_attempts', e.get('edge_attempts', ''))
    pick = lambda key: (r.get(key) if r.get(key) not in (None, '') else
                        setting.get(key) if setting.get(key) not in (None, '') else
                        cfg.get(key) if cfg.get(key) not in (None, '') else
                        cmd.get(key) or vinfo.get(key) or '')
    width = pick('bucket_width') or r.get('width') or ''
    return {
        'campaign': campaign, 'job': r.get('job') or job, 'harness': harness, 'record_dir': record_dir,
        'graph': r.get('graph') or cmd.get('graph') or manifest_graph(manifest),
        'source': r.get('source', ''), 'role': r.get('role') or manifest.get('source_role', ''),
        'phase': phase, 'rep': rep,
        'warmup': int(rep == -1 or 'warmup' in phase),
        'engine': engine, 'arm': arm, 'binary': binary,
        'nodes': r.get('nodes', manifest.get('nodes', 1)),
        'rpn': r.get('rpn') or cfg.get('rpn') or layout.get('ranks') or var.get('rpn') or manifest.get('rpn') or cmd.get('rpn', ''),
        'workers': (layout.get('workers') or var.get('workers') or cmd.get('workers') or r.get('workers') or manifest.get('workers', ''))
                   if engine.startswith('acic') else '',
        'threads': '' if engine.startswith('acic') else (pick('threads') or baseline_threads(manifest, r, cfg, cmd)),
        'cpus': cfg.get('cpus') or cmd.get('cpus', ''),
        'omp_bind': cfg.get('omp_bind') or ('close' if harness == 'run.py' and engine in ('gap', 'wasp') else ''),
        'delta': pick('delta') if not engine.startswith('acic') else '',
        'bucket_width': width if engine.startswith('acic') else '',
        'heap_slice': (r.get('slice') or pick('heap_slice')) if engine.startswith('acic') else '',
        'chunk_band': (r.get('band') or pick('chunk_band')) if engine.startswith('acic') else '',
        'leaf_prune': pick('leaf_prune') if engine.startswith('acic') else '',
        'process_queue': pick('process_queue') if engine.startswith('acic') else '',
        'flags': cmd.get('flags') or (' '.join(var.get('flags', [])) if var else '') or
                 (' '.join(r['flags']) if isinstance(r.get('flags'), list) else '') or
                 (' '.join(cfg.get('flags', [])) if cfg.get('flags') else ''),
        'seconds': r.get('seconds', ''), 'valid': valid,
        'outcome': r.get('outcome') or ('ok' if valid is True else ''),
        'rounds': r.get('rounds', r.get('reductions', '')), 'edge_attempts': attempts,
        'attempts_per_edge': e.get('attempts_per_edge', r.get('scan_factor', '')) if e or 'scan_factor' in r else '',
        'wire_bytes_per_edge': e.get('wire_bytes_per_edge', ''),
        'node_bytes_per_edge': e.get('node_bytes_per_edge', ''),
        'host': r.get('hosts') or r.get('host') or manifest_host(manifest),
        'selection_job': r.get('selection_job') or '',
    }


def baseline_threads(manifest, r, cfg, cmd):
    """OpenMP threads of a GAPBS, Wasp or RIKEN launch whose record omits them."""
    if manifest.get('threads'): return manifest['threads']
    g = ((manifest.get('protocol') or {}).get('graphs') or {}).get(r.get('graph') or cmd.get('graph'))
    if isinstance(g, dict) and g.get('threads'): return g['threads']
    # run.py launches set OMP_NUM_THREADS to the step's cpus (config cpus or threads).
    if cfg and cmd.get('cpus') and cfg.get('engine') in ('gap', 'wasp', 'riken', 'riken-mpit'): return cmd['cpus']
    return ''


def record_files(root):
    for p in sorted(root.rglob('*.jsonl')):
        if p.name == 'runs.jsonl' or p.parent.name == 'logs':
            yield p
    for p in sorted(root.rglob('rows.json')):
        yield p


def harness_of(p):
    if p.name == 'rows.json': return 'launch_ab'
    if p.name != 'runs.jsonl': return 'run.py'
    if p.parent.name.startswith('AB-'): return 'onenode_ab'
    return re.sub(r'-?\d{7,8}$', '', p.parent.name) or 'runs'


def delta_campaign(root):
    return root.name if root.name.startswith('acic-') else 'ipdps27-onenode-tmp'


def main(roots=CAMPAIGN_ROOTS, outdir=OUT, prefix='delta', campaign_of=delta_campaign, superseded=None, failed_outcome=None):
    """Write OUTDIR/<prefix>_solves.csv and OUTDIR/<prefix>_jobs.csv from the records under ROOTS.

    failed_outcome: if set, the outcome of every row from a `*.failed` record directory (an A/B
    attempt whose audit failed and was rerun), so those rows cannot pass for audited ones."""
    superseded = SUPERSEDED if superseded is None else superseded
    rows = []
    for root in roots:
        campaign = campaign_of(root)
        for p in record_files(root):
            harness = harness_of(p)
            d = p.parent
            manifest = manifest_of(d) if harness != 'run.py' else {}
            variants = {v['label']: v for v in manifest.get('variants', []) if isinstance(v, dict) and 'label' in v}
            try:
                recs = json.loads(p.read_text()) if p.name == 'rows.json' else \
                       [json.loads(l) for l in p.read_text().splitlines() if l.strip()]
            except (OSError, ValueError) as err:
                print(f'skip {p}: {err}', file=sys.stderr); continue
            if p.name == 'rows.json':
                host = next((l.split('host=')[1].split()[0].split('.')[0] for l in (d / 'driver.out').read_text().splitlines()
                             if 'host=' in l), '') if (d / 'driver.out').exists() else ''
                # d7_launch_ab.sbatch arms: srun -c and OMP_PROC_BIND per arm.
                place = {'s23': ('128', 'close'), 'd7spread': ('128', 'spread'), 'd7packed': (None, 'close'),
                         's23_cores': ('128', 'close'), 's23_campenv': ('128', 'close'), 's23_spread': ('128', 'spread')}
                for r in recs:
                    cpus, bind = place.get(r['arm'], ('', ''))
                    r = dict(r, variant=f"{r['engine']}-{r['arm']}", host=host)
                    r['config'] = {'engine': r['engine'], 'threads': r['threads'], 'delta': r['delta'],
                                   'cpus': cpus or str(r['threads']), 'omp_bind': bind}
                    rows.append(normalize(r, campaign=campaign, job=job_of(d), harness=harness,
                                          record_dir=str(d), manifest={}, variants={}))
                continue
            for r in recs:
                row = normalize(r, campaign=campaign, job=job_of(p if harness == 'run.py' else d),
                                harness=harness, record_dir=str(d), manifest=manifest, variants=variants)
                if failed_outcome and d.name.endswith('.failed'): row['outcome'] = failed_outcome
                rows.append(row)
    outdir.mkdir(parents=True, exist_ok=True)
    # One row per job: sacct metadata and the current-state section that reports it.
    jobs = sorted({str(r['job']) for r in rows if r['job']})
    acct = {}
    for i in range(0, len(jobs), 200):
        out = subprocess.run(['sacct', '-X', '-n', '-P', '-j', ','.join(jobs[i:i + 200]),
                              '-o', 'JobID,JobName,Account,Partition,State,Start,End,Elapsed,NodeList'],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            f = line.split('|')
            if len(f) == 9: acct[f[0]] = f[1:]
    for r in rows:
        if not r['host'] and str(r['job']) in acct: r['host'] = acct[str(r['job'])][7]
    with (outdir / f'{prefix}_solves.csv').open('w', newline='') as f:
        w = csv.DictWriter(f, COLUMNS); w.writeheader(); w.writerows(rows)
    # The first design document (in this order) and heading that cites each job.
    sections = {}
    for doc in ('current-state.md', 'sc27-plan.md', 'optimization-ledger.md', 'configurations.md',
                'implementation.md', 'large-scale-plan.md', 'README.md'):
        heading = ''
        for line in (APP / 'design' / doc).read_text().splitlines():
            if line.startswith('#'): heading = line.lstrip('#').strip()
            for j in re.findall(r'(?<!\d)(\d{7,8})(?!\d)', line):
                sections.setdefault(j, f'{doc}: {heading}')
    # The earliest commit that mentions each job ID (git pickaxe over all history).
    commits = {}
    for j in jobs:
        out = subprocess.run(['git', '-C', str(APP), 'log', '--all', '--reverse', '--date=short',
                              '--format=%h %ad %s', f'-S{j}'], capture_output=True, text=True).stdout
        commits[j] = out.splitlines()[0][:200] if out else ''
    # A tuning job is reported through the comparison job that used its selection.
    for r in rows:
        t, c = str(r['selection_job']), str(r['job'])
        if t and t != c and t not in sections and c in sections:
            sections[t] = f'tune for job {c}; {sections[c]}'
    with (outdir / f'{prefix}_jobs.csv').open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['job', 'campaign', 'harnesses', 'solves', 'valid_solves', 'graphs', 'engines', 'arms', 'hosts',
                    'slurm_name', 'account', 'partition', 'state', 'start', 'end', 'elapsed', 'nodelist',
                    'reported_in', 'first_commit', 'status'])
        for j in jobs:
            rs = [r for r in rows if str(r['job']) == j]
            a = acct.get(j, [''] * 8)
            w.writerow([j, rs[0]['campaign'], ';'.join(sorted({r['harness'] for r in rs})), len(rs),
                        sum(1 for r in rs if r['valid'] is True), ';'.join(sorted({str(r['graph']) for r in rs})),
                        ';'.join(sorted({str(r['engine']) for r in rs})), ';'.join(sorted({str(r['arm']) for r in rs})),
                        ';'.join(sorted({str(r['host']) for r in rs if r['host']})), *a, sections.get(j, ''),
                        commits.get(j, ''), superseded.get(j, '')])
    print(f'{len(rows)} solves from {len(jobs)} jobs -> {outdir}')


# Jobs whose results a later job replaces (current-state.md says why).
SUPERSEDED = {j: 'superseded: D7 baselines at their own thread count (sec. 41)' for j in
              ('22582143', '22582308', '22582333', '22582508', '22582749', '22582937', '22583041', '22583102',
               '22583204', '22583270', '22583357', '22583538', '22583702', '22583809', '22623612', '22623748',
               '22622404', '22622406', '22622410', '22623800')}
SUPERSEDED.update({'22582655': 'cancelled: submitted before the --spread fix',
                   '22623917': 'timed out: grid3-30-z tune rerun as 22627665',
                   '22623910': 'cancelled: equal-PE rule'})

if __name__ == '__main__':
    main()
