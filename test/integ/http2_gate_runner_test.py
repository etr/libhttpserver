#!/usr/bin/env python3
"""Fail-closed contracts for the bounded HTTP/2 integration gate."""
import importlib.util
import json
import pathlib
import subprocess
import tempfile
import sys
import textwrap
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'scripts/run-v3-http2-gates.py'


class GateRunnerTest(unittest.TestCase):
    def run_case_gate(self, scratch, report_mode, stale_report=False):
        """Exercise the real gate with bounded substitutes only at process boundaries."""
        scratch = pathlib.Path(scratch)
        fixture = scratch / 'test/http2_tls_fixture'
        fixture.parent.mkdir()
        fixture.write_text(f'#!{sys.executable}\n'
                           'import sys\n'
                           'print("READY 12345", flush=True)\n'
                           'assert sys.stdin.readline() == "STOP\\n"\n')
        fixture.chmod(0o755)
        receipt = {'scenarios': ['multiplexing', 'reset', 'upload_flow', 'response_flow', 'websocket'],
                   'alpn': 'h2', 'cancel_count': 1, 'uploaded_bytes': 163840,
                   'response_bytes': 131072, 'response_stalled_bytes': 32,
                   'ws_clean_close': True, 'upload_stalled_bytes': 65535}
        listing = ('HTTP/2 Protocol\n  5. Stream States\n    5.3.1. Stream Dependencies\n'
                   '      1: Sends HEADERS frame that depends on itself\n'
                   '      2: Sends PRIORITY frame that depend on itself\n'
                   '  6. Frame Definitions\n    6.3. PRIORITY\n      1: Zero stream\n')
        report_text = ('<testsuites><testsuite><testcase classname="Zero stream" '
                       'package="http2/6.3"/></testsuite></testsuites>')
        tool_source = f'#!{sys.executable}\n' + textwrap.dedent(f'''
            import json
            from pathlib import Path
            import sys
            name = Path(sys.argv[0]).name
            if name != 'h2spec':
                receipt = {receipt!r}
                receipt.update({{'stack': 'hyper-h2', 'version': '4.4.1'}} if name == 'python-client'
                               else {{'stack': 'node:http2', 'version': '24.15.0', 'nghttp2': '1.0'}})
                print(json.dumps(receipt))
            elif '--version' in sys.argv:
                print('Version: 2.6.0 (70ac2294010887f48b18e2d64f5cccd48421fad1)')
            elif '--dryrun' in sys.argv:
                print({listing!r})
            elif {report_mode!r} == 'fresh':
                Path(sys.argv[sys.argv.index('--junit-report') + 1]).write_text({report_text!r})
        ''')
        for name in ('python-client', 'node-client', 'h2spec'):
            tool = scratch / name
            tool.write_text(tool_source)
            tool.chmod(0o755)
        logs = scratch / 'logs'
        logs.mkdir()
        if stale_report:
            (logs / 'http2-6.3-1.xml').write_text(report_text)
        result = subprocess.run([sys.executable, str(RUNNER), '--build-dir', str(scratch),
                                 '--log-dir', str(logs), '--python', str(scratch / 'python-client'),
                                 '--node', str(scratch / 'node-client'), '--h2spec', str(scratch / 'h2spec')],
                                capture_output=True, text=True, timeout=10)
        return result, logs

    def test_case_without_fresh_report_is_fatal(self):
        for stale in (False, True):
            with self.subTest(stale_report=stale), tempfile.TemporaryDirectory() as scratch:
                result, logs = self.run_case_gate(scratch, 'missing', stale_report=stale)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn('http2-6.3-1.xml', result.stderr)
                self.assertFalse((logs / 'receipt.json').exists())

    def test_fresh_case_report_creates_success_receipt(self):
        with tempfile.TemporaryDirectory() as scratch:
            result, logs = self.run_case_gate(scratch, 'fresh', stale_report=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            receipt = json.loads((logs / 'receipt.json').read_text())
            self.assertEqual(receipt['h2spec']['executed'], ['http2/6.3/1'])
            self.assertEqual(set(receipt['h2spec']['excluded']), {'http2/5.3.1/1', 'http2/5.3.1/2'})

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
