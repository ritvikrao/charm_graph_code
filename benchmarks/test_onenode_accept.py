import unittest
from onenode_accept import medians


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


if __name__ == '__main__':
    unittest.main()
