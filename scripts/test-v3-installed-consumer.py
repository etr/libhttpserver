#!/usr/bin/env python3
"""Package matrix and bounded subprocess regressions (not runtime receipts)."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import socket
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import v3_package_matrix as matrix
import v3_package_commands as commands
import v3_package_process as process
import v3_package_traffic as traffic


class MatrixTest(unittest.TestCase):
    def setUp(self):
        self.inventory = matrix.load()

    def test_complete_matrix_and_identical_cases(self):
        cells = matrix.cells(self.inventory, 'Linux')
        self.assertEqual(8, len(cells))
        self.assertEqual(8, len({tuple(c[k] for k in ('tls', 'linkage', 'loop')) for c in cells}))
        self.assertTrue(all(c['cases'] == self.inventory['cases'] for c in cells))

    def test_missing_duplicate_or_wrong_platform_fails(self):
        for key in ('tls', 'linkages', 'loops'):
            for value in ([], ['no', 'no']):
                bad = copy.deepcopy(self.inventory)
                bad[key] = value
                with self.assertRaises(ValueError):
                    matrix.validate(bad)
        for platform in ('Linux', 'FreeBSD', 'Darwin', 'Windows'):
            bad = copy.deepcopy(self.inventory)
            del bad['platforms'][platform]
            with self.assertRaises(ValueError):
                matrix.validate(bad)
        with self.assertRaises(ValueError):
            matrix.cells(self.inventory, 'MSYS')

    def test_public_tls_inventory_requires_every_case_once(self):
        for value in ([], ['sni_a'], ['sni_a', 'sni_a']):
            bad = copy.deepcopy(self.inventory)
            bad['public_tls_scenarios'] = value
            with self.assertRaises(ValueError):
                matrix.validate(bad)

    def test_public_tls_result_rejects_missing_duplicate_and_unknown_cases(self):
        expected = ['a', 'b']
        matrix.check_scenarios([{'case': 'a'}, {'case': 'b'}], expected)
        for rows in ([], [{'case': 'a'}], [{'case': 'a'}, {'case': 'a'}],
                     [{'case': 'a'}, {'case': 'b'}, {'case': 'unknown'}]):
            with self.assertRaises(ValueError):
                matrix.check_scenarios(rows, expected)

    def test_public_tls_failed_cell_cannot_claim_inventory_success(self):
        for row in ({'case': 'a', 'status': 'failed'}, {'case': 'a', 'passed': False}):
            with self.assertRaises(ValueError):
                matrix.check_scenarios([row], ['a'])

    def test_duplicate_inventory_keys_cannot_hide_platform_cells(self):
        with self.assertRaises(ValueError):
            matrix.parse_inventory('{"platforms":{"Linux":"epoll","Linux":"poll"}}')

    def test_required_suites_are_platform_specific(self):
        names = matrix.suites(self.inventory, 'Windows', 'yes')
        self.assertTrue({'io_iocp_backend', 'io_completion_storage', 'external_http1_e2e',
                         'tls_io_loopback', 'native_http1_tls_parity'} <= set(names))
        self.assertNotIn('io_epoll_backend', names)
        self.assertNotIn('tls_io_loopback', matrix.suites(self.inventory, 'Linux', 'no'))

    def test_empty_skipped_failed_suite_is_rejected(self):
        good = 'Running test (1): a\nRunning test (2): b\nRunning test (3): c\n3 tests executed\n9 checks\n-> 9 successes\n-> 0 failures\n-> 0 skipped\n'
        self.assertEqual(3, matrix.suite_result(good, 0)['tests'])
        for output, status in ((good, 77), (good, 1), ('', 0), (good.replace('Running test (3): c\n', ''), 0),
                               (good.replace('3 tests', '0 tests'), 0),
                               (good.replace('0 skipped', '1 skipped'), 0),
                               (good.replace('0 failures', '1 failures'), 0)):
            with self.assertRaises(ValueError):
                matrix.suite_result(output, status)

    def test_shared_suite_requires_installed_library_and_static_forbids_it(self):
        graph = {'nodes': [{'id': '/prefix/lib/libhttpserver.so'}, {'id': '/consumer'}], 'edges': []}
        matrix.check_linkage(graph, 'shared', Path('/prefix'))
        with self.assertRaises(ValueError):
            matrix.check_linkage(graph, 'shared', Path('/elsewhere'))
        with self.assertRaises(ValueError):
            matrix.check_linkage(graph, 'static', Path('/prefix'))
        matrix.check_linkage({'nodes': [{'id': '/consumer'}], 'edges': []}, 'static', Path('/prefix'))

    def test_public_header_contamination_is_rejected_in_private_probes(self):
        matrix.check_dependencies(['/prefix/include/httpserver.hpp', '/source/src/httpserver/detail/io.hpp'],
                                  Path('/prefix'), Path('/source'), Path('/build'), private=True)
        for private in (False, True):
            with self.assertRaises(ValueError):
                matrix.check_dependencies(['/source/src/httpserver/server/server.hpp'],
                                          Path('/prefix'), Path('/source'), Path('/build'), private)

    def test_missing_configuration_case_or_different_outcome_is_rejected(self):
        for tls in ('yes', 'no'):
            output = '\n'.join('PACKAGE_CASE ' + case + ' ' +
                ('ok bound' if case == 'plaintext' else
                 'invalid_argument unbound' if case in ('tls_listener', 'http2') and tls == 'yes' else
                 'not_supported unbound' if case in ('tls_listener', 'http2', 'http3', 'mixed_rejection') else
                 'invalid_argument unbound') for case in self.inventory['cases'])
            for loop in ('managed', 'external'):
                self.assertEqual(len(self.inventory['cases']),
                                 len(matrix.configuration_results(output, self.inventory, loop, tls=tls)))
                for broken in (output.replace('PACKAGE_CASE http3', 'MISSING http3'),
                               output.replace('not_supported unbound', 'ok bound')):
                    with self.assertRaises(ValueError):
                        matrix.configuration_results(broken, self.inventory, loop, tls=tls)

    def test_private_external_probe_can_use_the_named_host_helper_only(self):
        matrix.check_dependencies(['/source/examples/v3_host_poll.hpp'], Path('/prefix'),
                                  Path('/source'), Path('/build'), private=True)
        with self.assertRaises(ValueError):
            matrix.check_dependencies(['/source/examples/other.hpp'], Path('/prefix'),
                                      Path('/source'), Path('/build'), private=True)

    def test_parity_backend_replays_preserve_unique_release_observations(self):
        case = dict(id='h1.1-tls:routing.tseq:hello', case='routing.tseq:hello', mode='h1.1-tls')
        rows = ['TASK179_OBSERVATION ' + json.dumps(dict(case, driver='managed'))]
        rows += ['PACKAGE_OBSERVATION ' + json.dumps(dict(case, driver=driver)) for driver in ('external', 'poll-oracle')]
        self.assertEqual({'managed': 1, 'external': 1, 'poll-oracle': 1}, matrix.parity_observation_counts('\n'.join(rows)))
        for bad in (rows[:1], rows + rows[:1], rows[:2]):
            with self.assertRaises(ValueError):
                matrix.parity_observation_counts('\n'.join(bad))

    def test_backend_tls_witnesses_required(self):
        for output in ('', 'PACKAGE_BACKEND poll\n', 'PACKAGE_BACKEND epoll\n'):
            with self.assertRaises(ValueError):
                matrix.check_tls_witnesses(output, 'epoll')
        matrix.check_tls_witnesses('PACKAGE_BACKEND epoll\nPACKAGE_TLS managed\nPACKAGE_TLS external\n', 'epoll')


class TrafficTest(unittest.TestCase):
    def test_public_cells_retain_distinct_process_receipts(self):
        with patch.object(traffic, 'Consumer', side_effect=RuntimeError('intercept launch')) as launch:
            for linkage in ('shared', 'static'):
                for loop in ('managed', 'external'):
                    with self.assertRaisesRegex(RuntimeError, 'intercept launch'):
                        traffic.traffic(Path('/consumer/' + linkage), dict(tls='no', linkage=linkage, loop=loop),
                                        Path('/consumer'), {}, matrix.load())
        receipts = [call.args[3] for call in launch.call_args_list]
        self.assertEqual(4, len(set(receipts)))

    def connection(self, chunks):
        class Connection:
            def __init__(self):
                self.calls = 0
            def recv(self, size):
                self.calls += 1
                if self.calls > len(chunks):
                    raise AssertionError('response reader repeated EOF')
                return chunks[self.calls - 1]
        return Connection()

    def test_truncated_header_and_body_rejected(self):
        for chunks in ((b'HTTP/1.1 200 OK\r\nContent-', b''),
                       (b'HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nx', b'')):
            with self.assertRaises(RuntimeError):
                traffic.response(self.connection(chunks))

    def test_pipeline_leftover_preserved(self):
        response = b'HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx'
        body, pending, _ = traffic.response(self.connection([response + response]))
        self.assertEqual(b'x', body)
        self.assertEqual(response, pending)

    def test_negative_or_oversized_body_rejected(self):
        for size in ('-1', '20000'):
            with self.assertRaises(RuntimeError):
                traffic.response(self.connection([b'HTTP/1.1 200 OK\r\nContent-Length: ' + size.encode() + b'\r\n\r\n']))


class CommandsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.runner = commands.Commands(self.root)

    def command(self, code):
        return [sys.executable, '-u', '-c', code]

    def assert_descendant_retired(self, parent_code, timeout):
        ready = self.root / 'descendant.json'
        worker = ("import json, os, socket, time; from pathlib import Path; "
                  "s=socket.socket(); s.bind(('127.0.0.1', 0)); "
                  "Path('descendant.json').write_text(json.dumps([os.getpid(), s.getsockname()[1]])); "
                  "time.sleep(30)")
        code = ("import subprocess, sys, time; from pathlib import Path; "
                f"subprocess.Popen({self.command(worker)!r}); "
                "\nwhile not Path('descendant.json').exists(): time.sleep(.01)\n" + parent_code)
        retired = False
        try:
            with self.assertRaisesRegex(RuntimeError, 'deadline/output budget exceeded'):
                self.runner(self.command(code), self.root, os.environ, timeout=timeout)
            self.assertTrue(ready.exists(), 'descendant must start before command failure')
            _, port = json.loads(ready.read_text())
            with socket.socket() as probe:
                try:
                    probe.bind(('127.0.0.1', port))
                except OSError:
                    self.fail('descendant retained its socket after command failure')
            retired = True
            receipt = json.loads((self.root / 'commands.json').read_text())[0]
            self.assertIn('deadline/output budget exceeded', receipt['error'])
            self.assertTrue(Path(receipt['log']).exists())
        finally:
            # Keep RED runs from leaking the deliberately long-lived worker.
            if not retired and ready.exists():
                pid, _ = json.loads(ready.read_text())
                try:
                    os.kill(pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass

    def test_timeout_retires_descendants_and_retains_failure_receipt(self):
        self.assert_descendant_retired('time.sleep(30)', timeout=1)

    def test_timeout_retires_descendants_after_parent_has_exited(self):
        self.assert_descendant_retired('raise SystemExit(0)', timeout=1)

    def test_output_overflow_retires_descendants_and_bounds_log(self):
        self.assert_descendant_retired(
            "import os\nwhile True: os.write(1, b'x' * 65536)", timeout=5)
        self.assertLessEqual((self.root / 'command-0000.log').stat().st_size, 64 * 1024 * 1024)

    def test_fast_exiting_oversized_writer_cannot_bypass_output_budget(self):
        with self.assertRaisesRegex(RuntimeError, 'deadline/output budget exceeded'):
            self.runner(self.command("import os; os.write(1, b'x' * (65 * 1024 * 1024))"),
                        self.root, os.environ, timeout=5)
        self.assertLessEqual((self.root / 'command-0000.log').stat().st_size, 64 * 1024 * 1024)
        receipt = json.loads((self.root / 'commands.json').read_text())[0]
        self.assertIn('deadline/output budget exceeded', receipt['error'])

    def test_success_and_allowed_failure_keep_output_and_hash_receipts(self):
        for status in (0, 7):
            with self.subTest(status=status):
                output = self.runner(self.command(f"print('out'); print('err', file=__import__('sys').stderr); raise SystemExit({status})"),
                                     self.root, os.environ, timeout=5, allow_failure=True)
                self.assertEqual(['out', 'err'], output.splitlines())
                record = self.runner.records[-1]
                data = Path(record['log']).read_bytes()
                self.assertEqual(status, record['exit'])
                self.assertEqual(len(data), record['bytes'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), record['sha256'])

    def test_nonzero_exit_retains_output_and_failure_receipt(self):
        with self.assertRaisesRegex(RuntimeError, 'matrix command failed:'):
            self.runner(self.command("print('failure detail'); raise SystemExit(7)"),
                        self.root, os.environ, timeout=5)
        receipt = json.loads((self.root / 'commands.json').read_text())[0]
        self.assertEqual(7, receipt['exit'])
        self.assertIn('failure detail', receipt['error'])
        self.assertIn(b'failure detail', Path(receipt['log']).read_bytes())


class ProcessTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def command(self, code):
        return [sys.executable, '-u', '-c', code]

    def test_bounded_startup_and_shutdown_with_concurrent_stderr(self):
        command = self.command("import sys; sys.stderr.write('x'*10000); print('1234', flush=True); input(); print('done')")
        with process.Consumer(command, self.root, None, self.root / 'pass.json', timeout=2) as child:
            self.assertEqual('1234', child.line())
            child.stop()
        receipt = json.loads((self.root / 'pass.json').read_text())
        self.assertEqual(0, receipt['exit'])
        self.assertIn('done', receipt['stdout'])

    def test_failed_startup_timeout_and_oversize_retain_evidence(self):
        cases = ["import time; time.sleep(10)", "print('bad')", "raise SystemExit(4)",
                 "import sys; sys.stdout.write('x'*1000000); sys.stdout.flush()"]
        for i, code in enumerate(cases):
            receipt = self.root / f'failure-{i}.json'
            with self.subTest(code=code), self.assertRaises(RuntimeError):
                with process.Consumer(self.command(code), self.root, None, receipt, timeout=.2) as child:
                    child.port()
            self.assertIn('error', json.loads(receipt.read_text()))
            self.assertLess(receipt.stat().st_size, 150000)

    def test_nonzero_shutdown_is_failure(self):
        receipt = self.root / 'stop.json'
        with self.assertRaises(RuntimeError):
            with process.Consumer(self.command("print('1234', flush=True); input(); raise SystemExit(8)"),
                                  self.root, None, receipt, timeout=1) as child:
                child.port()
                child.stop()
        self.assertEqual(8, json.loads(receipt.read_text())['exit'])


if __name__ == '__main__':
    unittest.main()
