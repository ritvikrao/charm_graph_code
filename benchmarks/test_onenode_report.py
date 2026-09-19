import unittest
from onenode_report import fields


class DiagnosticRecords(unittest.TestCase):
    def test_complete_record(self):
        self.assertEqual(fields('COMM_SHARE pes=960 solve_seconds=0.5\n', 'COMM_SHARE'),
                         {'pes': 960, 'solve_seconds': 0.5})

    def test_rank_output_without_newline(self):
        text = 'PE 123 batch_absorbable=332667COMM_SHARE pes=960 solve_seconds=7.999434\n'
        self.assertEqual(fields(text, 'COMM_SHARE'),
                         {'pes': 960, 'solve_seconds': 7.999434})

    def test_corrupt_or_ambiguous_record_fails(self):
        for text in ['no record', 'COMM_SHARE \n',
                     'COMM_SHARE pes=1\nCOMM_SHARE pes=2\n',
                     'COMM_SHARE pes=1 pes=2\n',
                     'COMM_SHARE pes=1e999\n',
                     'COMM_SHARE pes=1 interrupted output solve_seconds=2\n']:
            with self.assertRaises(ValueError):
                fields(text, 'COMM_SHARE')


if __name__ == '__main__':
    unittest.main()
