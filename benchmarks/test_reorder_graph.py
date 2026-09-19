#!/usr/bin/env python3
import unittest
import numpy as np
from reorder_graph import tiled_order, edge_partition


class TileTests(unittest.TestCase):
    def test_deal_tiles_with_partial_tail(self):
        order = np.arange(23)[::-1]
        expected = np.concatenate([order[0:4], order[12:16], order[4:8],
                                   order[16:20], order[8:12], order[20:23]])
        np.testing.assert_array_equal(tiled_order(order, 4, 3), expected)
        np.testing.assert_array_equal(np.sort(tiled_order(order, 4, 3)), np.arange(23))

    def test_limits(self):
        for n, tile, pes in [(0, 2, 3), (5, 10, 2), (5, 1, 10), (10, 2, 1)]:
            order = np.arange(n)
            np.testing.assert_array_equal(tiled_order(order, tile, pes), order)
        with self.assertRaises(ValueError):
            tiled_order(np.arange(3), 0, 2)

    def test_partition_matches_reader_loop(self):
        rng = np.random.default_rng(17)
        for n in [1, 3, 17, 103]:
            for pes in [1, 2, 8, 120]:
                for degrees in [np.zeros(n, dtype=np.int64), rng.integers(0, 50, n)]:
                    offsets = np.concatenate([[0], np.cumsum(degrees)])
                    per_pe = max(1, (int(offsets[-1]) + pes - 1) // pes)
                    vertex = 0
                    expected = []
                    for i in range(pes):
                        expected.append(vertex)
                        target = int(offsets[vertex]) + per_pe
                        while vertex < n and offsets[vertex] < target and n - vertex > pes - i - 1:
                            vertex += 1
                    expected.append(n)
                    np.testing.assert_array_equal(edge_partition(offsets, pes), expected)


if __name__ == '__main__':
    unittest.main()
