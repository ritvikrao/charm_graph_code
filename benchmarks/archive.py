#!/usr/bin/env python3
"""Archive compact provenance; keep large graphs and raw solver logs on scratch."""
import gzip
import hashlib
import json
from pathlib import Path
import sys

root, out = map(Path, sys.argv[1:3])
out.mkdir(parents=True, exist_ok=True)


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1048576), b''):
            digest.update(chunk)
    return digest.hexdigest()


graphs = {}
for meta in sorted((root/'graphs').glob('*.meta')):
    name = meta.stem
    graphs[name] = dict(metadata=meta.read_text(),
                        reference=(meta.with_suffix('.reference.txt')).read_text(),
                        sha256=sha(meta.with_suffix('.wsg')))
    converted = meta.with_suffix('.gr')
    if converted.exists():
        graphs[name]['gluon_sha256'] = sha(converted)
(out/'graphs.json').write_text(json.dumps(graphs, indent=2)+'\n')
choices = {p.name: json.loads(p.read_text()) for p in sorted((root/'logs').glob('*-selected.json'))}
(out/'selected-configurations.json').write_text(json.dumps(choices, indent=2)+'\n')
manifest = {str(p.relative_to(root)): dict(bytes=p.stat().st_size, sha256=sha(p))
            for p in sorted((root/'logs').iterdir()) if p.is_file()}
(out/'scratch-log-manifest.json').write_text(json.dumps(dict(campaign=str(root), files=manifest), indent=2)+'\n')
with gzip.open(out/'job-output.jsonl.gz', 'wt') as f:
    for p in sorted((root/'logs').glob('*.out')):
        f.write(json.dumps(dict(file=p.name, text=p.read_text(errors='replace')))+'\n')
validation = []
for pattern in ['tiny-*.jsonl', 'smoke-*.jsonl']:
    for p in sorted((root/'logs').glob(pattern)):
        validation += [json.loads(s) for s in p.read_text().splitlines()]
with gzip.open(out/'validation-runs.jsonl.gz', 'wt') as f:
    for r in validation:
        f.write(json.dumps(r, sort_keys=True)+'\n')
(out/'initial-build-manifest.txt').write_text((root/'bin'/'build-manifest.txt').read_text())
(out/'binary-hashes.json').write_text(json.dumps(
    {p.name: sha(p) for p in sorted((root/'bin').iterdir()) if p.is_file()}, indent=2)+'\n')
app = Path(__file__).resolve().parent.parent
sources = [p for p in (app/'benchmarks').iterdir() if p.is_file()]
sources += [app/'sssp_smp.cpp', app/'Makefile', app/'config.mk']
(out/'source-hashes.json').write_text(json.dumps(
    {str(p.relative_to(app)): sha(p) for p in sorted(sources)}, indent=2)+'\n')
print(f'Archived {len(graphs)} graph descriptions, {len(choices)} choices, and {len(manifest)} log hashes')
