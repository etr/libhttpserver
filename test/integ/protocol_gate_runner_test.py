#!/usr/bin/env python3
"""Exercise the task-local runner's fail-closed contract."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'scripts/run-v3-http1-websocket-gates.sh'


class GateRunnerTest(unittest.TestCase):
    def invoke(self, build, *args):
        return subprocess.run(['bash', str(RUNNER), '--build-dir', str(build),
                               '--log-dir', str(build / 'logs'), *args],
                              capture_output=True, text=True, timeout=15)

    def test_missing_executable_is_named_and_fatal(self):
        with tempfile.TemporaryDirectory() as scratch:
            result = self.invoke(pathlib.Path(scratch), '--tests', 'absent')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing executable: absent', result.stderr)

    def test_executable_failure_is_preserved_in_log(self):
        with tempfile.TemporaryDirectory() as scratch:
            build = pathlib.Path(scratch)
            (build / 'test').mkdir()
            program = build / 'test/failing'
            program.write_text('#!/bin/sh\necho named-failure\nexit 7\n')
            program.chmod(0o755)
            result = self.invoke(build, '--tests', 'failing')
            self.assertEqual(result.returncode, 7)
            self.assertIn('named-failure', (build / 'logs/failing.log').read_text())

    def test_empty_suite_cannot_pass(self):
        with tempfile.TemporaryDirectory() as scratch:
            result = self.invoke(pathlib.Path(scratch), '--tests', '')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('empty test selection', result.stderr)

    def test_source_archive_preserves_executable_failure(self):
        with tempfile.TemporaryDirectory() as scratch:
            build = pathlib.Path(scratch)
            (build / 'scripts').mkdir()
            runner = build / 'scripts' / RUNNER.name
            runner.write_text(RUNNER.read_text())
            (build / 'test').mkdir()
            program = build / 'test/failing'
            program.write_text('#!/bin/sh\nexit 7\n')
            program.chmod(0o755)
            result = subprocess.run(['bash', str(runner), '--build-dir', str(build),
                                     '--tests', 'failing'], capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 7)

    def test_whitespace_suite_cannot_pass(self):
        with tempfile.TemporaryDirectory() as scratch:
            result = self.invoke(pathlib.Path(scratch), '--tests', ' \t ')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('empty test selection', result.stderr)

    def test_missing_independent_client_is_fatal(self):
        with tempfile.TemporaryDirectory() as scratch:
            result = self.invoke(pathlib.Path(scratch), '--tests', 'absent',
                                 '--independent', '--python', '/missing/python')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing Python client tool', result.stderr)


if __name__ == '__main__':
    unittest.main()
