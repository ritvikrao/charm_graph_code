import unittest
from work_cost_report import counters, production_attempts


class WorkCostRecords(unittest.TestCase):
    line = ('WORK_COST sample_period=1024 queue_pushes=3 queue_pops=3 '
            'expansions=2 stale_pops=1 cas_attempts=4 cas_failures=1')

    def test_complete_and_empty_frontiers(self):
        self.assertEqual(counters(self.line)['queue_pops'], 3)
        self.assertEqual(counters('WORK_COST queue_pushes=0 queue_pops=0 '
                                  'expansions=0 stale_pops=0 cas_attempts=1 cas_failures=0')['queue_pops'], 0)

    def test_production_ledger_includes_filtered_work_and_excludes_source(self):
        text = ('Wasted updates: 7\nSend-filtered updates: 3\n'
                'Absorbed updates: 2\nBatch-folded updates: 1\n')
        self.assertEqual(production_attempts(text, 10), 22)
        with self.assertRaises(ValueError):
            production_attempts(text + 'Lazy heavy: 5 tokens\n', 10)

    def test_incomplete_or_corrupt_counter_records_fail(self):
        for text in ['', self.line + '\n' + self.line,
                     self.line.replace('queue_pops=3', 'queue_pops=2'),
                     self.line.replace('stale_pops=1', 'stale_pops=0'),
                     self.line.replace('cas_failures=1', 'cas_failures=5'),
                     self.line.replace('expansions=2', 'expansions=nan'),
                     self.line + ' queue_pops=3']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                counters(text)


if __name__ == '__main__':
    unittest.main()
