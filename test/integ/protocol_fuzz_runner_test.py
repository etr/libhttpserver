#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FuzzRunnerTest(unittest.TestCase):
    def test_requested_unavailable_compiler_fails(self):
        with tempfile.TemporaryDirectory() as build:
            result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                                     '--build-dir', build, '--compiler', '/missing/compiler'],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('libFuzzer compiler unavailable', result.stderr)


if __name__ == '__main__':
    unittest.main()
