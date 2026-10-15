#!/usr/bin/env python3
"""Task-local plateau receipt checks; no network dependencies required."""
import unittest
from pathlib import Path
from unittest.mock import patch
from resource_limits_probe import measure, validate_window

class Windows(unittest.TestCase):
    def test_idle_spin_is_rejected(self):
        with self.assertRaises(AssertionError):
            validate_window({'cpu_seconds': 1, 'wall_seconds': 1, 'rss_before': 0, 'rss_after': 0})

    def test_current_memory_growth_is_rejected(self):
        with self.assertRaises(AssertionError):
            validate_window({'cpu_seconds': 0, 'wall_seconds': 1, 'rss_before': 0, 'rss_after': 1048577})

    def test_bounded_idle_window_is_accepted(self):
        validate_window({'cpu_seconds': .1, 'wall_seconds': 1, 'rss_before': 1048576, 'rss_after': 1048576})

class Sampler(unittest.TestCase):
    def read_pair(self, raw, precise):
        fields = ['S'] + ['0'] * 10 + ['17', '23'] + ['0'] * 10
        def read(path, *args, **kwargs):
            if path.name == 'stat':
                return '123 (name with ) parentheses) ' + ' '.join(fields)
            if path.name == 'statm':
                return f'9000 {raw // 4096} 0 0 0 0 0'
            if path.name == 'smaps_rollup':
                if isinstance(precise, Exception): raise precise
                return precise if isinstance(precise, str) else f'Rss: {precise // 1024} kB\n'
            raise AssertionError(path)
        with patch.object(Path, 'read_text', read):
            return measure(123)

    def test_captured_exact_bound_is_accepted_despite_approximate_growth(self):
        before = self.read_pair(19529728, 20054016)
        after = self.read_pair(20840448, 21102592)
        validate_window(dict(cpu_seconds=after[0]-before[0], wall_seconds=1,
                             rss_before=before[1], rss_after=after[1]))
        self.assertEqual(1048576, after[1] - before[1])

    def test_precise_overbound_still_fails(self):
        before = self.read_pair(19529728, 20054016)
        after = self.read_pair(19529728, 20054016 + 1048576 + 4096)
        with self.assertRaises(AssertionError):
            validate_window(dict(cpu_seconds=0, wall_seconds=1,
                                 rss_before=before[1], rss_after=after[1]))

    def test_missing_or_invalid_precision_fails_explicitly(self):
        for value in (FileNotFoundError('missing rollup'), PermissionError('denied'), '',
                      'Rss: -1 kB\n', 'Rss: x kB\n', 'Rss: 1 MB\n',
                      'Rss: 1 kB extra\n', 'Rss: 1 kB\nRss: 2 kB\n'):
            with self.subTest(value=value), self.assertRaises((OSError, ValueError)):
                self.read_pair(19529728, value)

    def test_cpu_fields_after_parenthesized_comm_are_unchanged(self):
        import os
        cpu, rss = self.read_pair(19529728, 20054016)
        self.assertEqual(40 / os.sysconf('SC_CLK_TCK'), cpu)
        self.assertEqual(20054016, rss)

    def test_zero_precise_rss_is_valid(self):
        self.assertEqual(0, self.read_pair(19529728, 0)[1])


if __name__ == '__main__':
    unittest.main()
