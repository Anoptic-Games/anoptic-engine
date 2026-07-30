#!/usr/bin/env python3
"""Deterministic tests for the Windows FPS driver's paired-comparison contract."""
import unittest

import bench_fps_win64 as bench


def _result(fps, dwm=12.0):
    return {"avg_fps": float(fps), "dwm_gpu": dwm}


class BalancedOrderTests(unittest.TestCase):
    def test_abba_baab_cycle_balances_first_position(self):
        orders = bench._balanced_pair_orders(8)
        self.assertEqual(orders, [("A", "B"), ("B", "A"), ("B", "A"), ("A", "B"),
                                  ("A", "B"), ("B", "A"), ("B", "A"), ("A", "B")])
        self.assertEqual(sum(order[0] == "A" for order in orders), 4)
        self.assertEqual(sum(order[0] == "B" for order in orders), 4)

    def test_odd_or_single_pair_is_rejected(self):
        for pairs in (0, 1, 3):
            with self.assertRaises(ValueError):
                bench._balanced_pair_orders(pairs)


class PairedStatisticsTests(unittest.TestCase):
    def test_balanced_pairs_cancel_linear_session_drift(self):
        clock, pairs = 100.0, []
        for order in bench._balanced_pair_orders(4):
            results = {}
            for revision in order:
                results[revision] = _result(clock)
                clock += 1.0
            pairs.append((results["A"], results["B"]))
        summary = bench.paired_summary(pairs)
        self.assertAlmostEqual(summary["a_fps"], summary["b_fps"])
        self.assertAlmostEqual(summary["delta_pct"], 0.0)
        self.assertLess(summary["ci_low_pct"], 0.0)
        self.assertGreater(summary["ci_high_pct"], 0.0)

    def test_constant_effect_excludes_zero(self):
        pairs = [(_result(100.0, 10.0), _result(102.0, 11.0)) for _ in range(6)]
        summary = bench.paired_summary(pairs)
        self.assertAlmostEqual(summary["delta_pct"], 2.0)
        self.assertAlmostEqual(summary["ci_low_pct"], 2.0)
        self.assertAlmostEqual(summary["ci_high_pct"], 2.0)
        self.assertAlmostEqual(summary["a_dwm"], 10.0)
        self.assertAlmostEqual(summary["b_dwm"], 11.0)

    def test_incomplete_dwm_sampling_is_reported_unavailable(self):
        pairs = [(_result(100.0), _result(100.0)) for _ in range(4)]
        pairs[2][0]["dwm_gpu"] = None
        pairs[3][1]["dwm_gpu"] = None
        summary = bench.paired_summary(pairs)
        self.assertIsNone(summary["a_dwm"])
        self.assertIsNone(summary["b_dwm"])


if __name__ == "__main__":
    unittest.main()
