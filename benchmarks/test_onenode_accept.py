import unittest
import contextlib
import io
import json
from pathlib import Path
import tempfile
from unittest.mock import patch
from onenode_accept import main, medians


class AcceptanceSamples(unittest.TestCase):
    def setUp(self):
        self.rows = [dict(source=s, rep=r, seconds=float(s+r), valid=True)
                     for s in [10, 20] for r in range(3)]

    def test_requires_every_held_out_cell(self):
        self.assertEqual(medians(self.rows, [10, 20], 3), {10: 11, 20: 21})
        with self.assertRaises(ValueError):
            medians(self.rows[:-1], [10, 20], 3)
        with self.assertRaises(ValueError):
            medians(self.rows, [10, 21], 3)

    def test_does_not_overwrite_duplicate_or_bad_runs(self):
        with self.assertRaises(ValueError):
            medians(self.rows + self.rows[:1], [10, 20], 3)
        for bad in [dict(valid=False), dict(seconds=float('nan')), dict(seconds=0)]:
            with self.assertRaises(ValueError):
                medians([dict(self.rows[0], **bad)] + self.rows[1:], [10, 20], 3)

    def test_warmup_does_not_fill_missing_measurement(self):
        warmup = dict(self.rows[-1], rep=-1)
        self.assertEqual(medians(self.rows + [warmup], [10, 20], 3), {10: 11, 20: 21})
        with self.assertRaises(ValueError):
            medians(self.rows[:-1] + [warmup], [10, 20], 3)


class CompleteAcceptance(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'graphs').mkdir()
        (self.root/'logs').mkdir()
        self.rows = [dict(source=s, rep=r, seconds=1., valid=True)
                     for s in range(10, 14) for r in range(3)]
        for graph in ['mesh', 'rmat']:
            (self.root/'graphs'/f'{graph}.reference.txt').write_text(
                '\n'.join(f'{s} test 1 2 3 4 5 6' for s in range(10, 14)))
        variants = [dict(label=label, binary=label, sha256=sha, flags=[])
                    for label, sha in [('frozen', 'f'), ('control', 'f'), ('candidate', 'c')]]
        for eight, gap, one, regression in [(1, 2, 3, 4), (5, 6, 7, 8)]:
            for graph, nodes, job in [('mesh', 8, eight), ('mesh', 1, one), ('rmat', 8, regression)]:
                directory = self.root/'logs'/f'AB-{graph}-{nodes}n-{job}'
                directory.mkdir()
                (directory/'summary.json').write_text('{}')
                (directory/'manifest.json').write_text(json.dumps(dict(variants=variants,
                    nodes=nodes, workers=120, rpn=8, sources=4, reps=3, source_role='test')))
                rows = [dict(r, variant=v['label'], graph=graph,
                    seconds=2. if nodes == 1 else 1.05 if v['label'] == 'control' else 1.)
                    for r in self.rows for v in variants]
                (directory/'runs.jsonl').write_text('\n'.join(map(json.dumps, rows)))
            rows = [dict(r, graph='mesh', phase='external', seconds=1.2,
                binary_sha256='g', config=dict(engine='gap', threads=8, delta=256)) for r in self.rows]
            (self.root/'logs'/f'external-1n-120w-{gap}.jsonl').write_text('\n'.join(map(json.dumps, rows)))

    def run_accept(self):
        output = io.StringIO()
        argv = ['accept', str(self.root), '--graphs', 'mesh', '--regressions', 'rmat',
                '--allocation', '1,2,3,4', '--allocation', '5,6,7,8']
        with patch('sys.argv', argv), contextlib.redirect_stdout(output):
            with self.assertRaises(SystemExit) as caught:
                main()
        return caught.exception.code, json.loads(output.getvalue())['status']

    def test_two_complete_allocations_pass(self):
        self.assertEqual(self.run_accept(), (0, 'PASS'))

    def test_second_allocation_is_required(self):
        (self.root/'logs'/'AB-mesh-8n-5'/'summary.json').unlink()
        self.assertEqual(self.run_accept(), (2, 'INCOMPLETE'))

    def test_changed_candidate_is_not_confirmation(self):
        path = self.root/'logs'/'AB-mesh-8n-5'/'manifest.json'
        manifest = json.loads(path.read_text())
        manifest['variants'][-1]['sha256'] = 'different'
        path.write_text(json.dumps(manifest))
        self.assertEqual(self.run_accept(), (2, 'INCOMPLETE'))

    def test_slow_held_out_source_fails(self):
        path = self.root/'logs'/'AB-mesh-8n-5'/'runs.jsonl'
        rows = [json.loads(r) for r in path.read_text().splitlines()]
        for r in rows:
            if r['variant'] == 'candidate' and r['source'] == 10:
                r['seconds'] = 2.
        path.write_text('\n'.join(map(json.dumps, rows)))
        self.assertEqual(self.run_accept(), (1, 'NO-GO'))


if __name__ == '__main__':
    unittest.main()
