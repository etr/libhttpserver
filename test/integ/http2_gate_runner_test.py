#!/usr/bin/env python3
"""Fail-closed contracts for the bounded HTTP/2 integration gate."""
import importlib.util
import pathlib
import subprocess
import tempfile
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'scripts/run-v3-http2-gates.py'


class GateRunnerTest(unittest.TestCase):
    def test_missing_fixture_is_fatal(self):
        with tempfile.TemporaryDirectory() as scratch:
            result = subprocess.run(['python3', str(RUNNER), '--build-dir', scratch,
                                     '--log-dir', str(pathlib.Path(scratch) / 'logs')],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing fixture', result.stderr)

    def test_inventory_and_exclusions_fail_closed(self):
        spec = importlib.util.spec_from_file_location('gates', RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        inventory = module.inventory('HTTP/2 Protocol\n  6. Frame Definitions\n'
                                     '    6.3. PRIORITY\n      1: Zero stream\n'
                                     '      2: Short frame\n')
        self.assertEqual(set(inventory), {'http2/6.3/1', 'http2/6.3/2'})
        with self.assertRaises(ValueError):
            module.inventory('no cases')
        with tempfile.TemporaryDirectory() as scratch:
            path = pathlib.Path(scratch) / 'exclude.tsv'
            path.write_text('http2/6.3/99\tOld assertion\t6.3\tReason\n')
            with self.assertRaises(ValueError):
                module.exclusions(path, inventory)

    def test_selected_python_symlink_keeps_environment_identity(self):
        spec = importlib.util.spec_from_file_location('gates', RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as scratch:
            selected = pathlib.Path(scratch) / 'venv-python'
            selected.symlink_to(sys.executable)
            self.assertEqual(module.executable(str(selected), 'Python'), str(selected))

    def test_empty_client_receipt_is_fatal(self):
        spec = importlib.util.spec_from_file_location('gates', RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for receipt in ('{}', '{"scenarios": []}', 'not json'):
            with self.assertRaises(ValueError):
                module.client_receipt(receipt)


if __name__ == '__main__':
    unittest.main()
