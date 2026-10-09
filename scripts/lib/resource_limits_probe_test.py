#!/usr/bin/env python3
"""Task-local plateau receipt checks; no network dependencies required."""
import unittest
from resource_limits_probe import validate_window

class Windows(unittest.TestCase):
    def test_idle_spin_is_rejected(self):
        with self.assertRaises(AssertionError):
            validate_window({'cpu_seconds': 1, 'wall_seconds': 1, 'rss_before': 0, 'rss_after': 0})

    def test_current_memory_growth_is_rejected(self):
        with self.assertRaises(AssertionError):
            validate_window({'cpu_seconds': 0, 'wall_seconds': 1, 'rss_before': 0, 'rss_after': 1048577})

    def test_bounded_idle_window_is_accepted(self):
        validate_window({'cpu_seconds': .1, 'wall_seconds': 1, 'rss_before': 1048576, 'rss_after': 1048576})

if __name__ == '__main__':
    unittest.main()
