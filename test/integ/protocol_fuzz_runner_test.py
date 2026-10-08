#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FuzzRunnerTest(unittest.TestCase):
    def test_unknown_or_empty_target_fails_before_compiler(self):
        for selection in ('', 'hpack bogus'):
            with tempfile.TemporaryDirectory() as build:
                result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                                         '--build-dir', build, '--targets', selection,
                                         '--compiler', '/missing/compiler'],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 2)
                self.assertIn('fuzz target', result.stderr)

    def test_selected_http2_missing_compiler_is_fatal(self):
        with tempfile.TemporaryDirectory() as build:
            result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                                     '--build-dir', build, '--targets', 'hpack http2_engine',
                                     '--compiler', '/missing/compiler'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertIn('libFuzzer compiler unavailable', result.stderr)

    def test_requested_unavailable_compiler_fails(self):
        with tempfile.TemporaryDirectory() as build:
            result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                                     '--build-dir', build, '--compiler', '/missing/compiler'],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('libFuzzer compiler unavailable', result.stderr)


if __name__ == '__main__':
    unittest.main()
