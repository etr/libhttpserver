"""Offline contracts only: never invoke any of the four selected native targets."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = '37095c23751960b6e1adcad95d7b3c4c34c64351'
SPEC = importlib.util.spec_from_file_location('capture', ROOT / 'scripts/ci_native_missing_receipts.py')


class DriverTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(Path(SPEC.origin).is_file(), 'four-target driver missing')
        self.driver = importlib.util.module_from_spec(SPEC)
        SPEC.loader.exec_module(self.driver)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def test_budget_is_charged_durably_before_each_launch_and_never_reset(self):
        observations = []
        def launch(target, directory):
            ledger = json.loads((self.directory / 'budget.json').read_text())
            observations.append((target, ledger['charged_seconds'], ledger['attempts']))
            return {'passed': True}
        result = self.driver.run_batch(self.directory, tuple(self.driver.TARGETS), launch)
        self.assertEqual(observations, [('consumer', 30, 1), ('native_http1_e2e', 220, 2),
                                        ('tls_psk', 280, 3), ('quic_interop_tls', 340, 4)])
        self.assertEqual(result['state'], 'closed')
        with self.assertRaises(FileExistsError):
            self.driver.run_batch(self.directory, ('consumer',), launch)
        self.assertEqual(len(observations), 4)

    def test_independent_failures_and_exceptions_do_not_retry_or_skip_targets(self):
        observed = []
        def launch(target, directory):
            observed.append(target)
            if target == 'consumer':
                raise RuntimeError('spawn failed after permit charge')
            return {'passed': False, 'original_exit': 6}
        result = self.driver.run_batch(self.directory, tuple(self.driver.TARGETS), launch)
        self.assertEqual(observed, list(self.driver.TARGETS))
        self.assertFalse(result['passed'])
        self.assertEqual(result['attempts'], 4)
        self.assertEqual(json.loads((self.directory / 'native_http1_e2e/outcome.json').read_text())['original_exit'], 6)

    def test_unknown_duplicate_or_out_of_order_target_selection_fails_before_launch(self):
        for names in [('other',), ('consumer', 'consumer'), ('tls_psk', 'consumer')]:
            with self.assertRaises(ValueError):
                self.driver.run_batch(self.directory, names, lambda *_: self.fail('must not launch'))
        self.assertFalse((self.directory / 'once.json').exists())

    def test_empty_or_invalid_target_outcome_fails_closed(self):
        for index, invalid in enumerate((None, {}, {'passed': 'yes'}, {'passed': 1})):
            result = self.driver.run_batch(self.directory / str(index), ('consumer',), lambda *_: invalid)
            self.assertFalse(result['passed'])

    def test_abandoned_budget_refuses_resume(self):
        (self.directory / 'once.json').write_text('{}')
        with self.assertRaises(FileExistsError):
            self.driver.run_batch(self.directory, ('consumer',), lambda *_: self.fail('must not launch'))

    def test_worker_permit_is_parent_bound_target_bound_and_single_use(self):
        def launch(target, directory):
            with self.assertRaises(RuntimeError):
                self.driver.consume_permit(directory, 'tls_psk', parent=os.getpid())
            self.driver.consume_permit(directory, target, parent=os.getpid())
            with self.assertRaises(FileExistsError):
                self.driver.consume_permit(directory, target, parent=os.getpid())
            return {'passed': True}
        self.driver.run_batch(self.directory, ('consumer',), launch)

    def test_secrets_are_absent_from_target_environment(self):
        env = self.driver.target_environment(dict(PATH='/bin', TMPDIR='/tmp/a', SYSTEMROOT='C:/Windows',
                                                 GITHUB_TOKEN='secret', GH_TOKEN='secret2',
                                                 AWS_SECRET_ACCESS_KEY='secret3', RANDOM_SECRET='secret4'))
        self.assertEqual(env, dict(PATH='/bin', TMPDIR='/tmp/a', SYSTEMROOT='C:/Windows'))

    def test_default_cli_refuses_local_execution_before_creating_any_budget(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/ci_native_missing_receipts.py'),
                                 '--output', str(self.directory / 'not-created')],
                                capture_output=True, text=True, env=self.driver.target_environment(os.environ))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('approved native CI context required', result.stderr)
        self.assertFalse((self.directory / 'not-created').exists())

    def test_stack_command_is_noninvasive_nonsuspending_and_pid_scoped(self):
        self.assertTrue(hasattr(self.driver, 'stack_command'), 'bounded optional stack command missing')
        argv = self.driver.stack_command('cdb.exe', 123, self.directory)
        self.assertIn('-pvr', argv)
        self.assertIn('-pd', argv)
        self.assertEqual(argv[argv.index('-p') + 1], '123')
        self.assertEqual(argv[argv.index('-c') + 1], '~* k; qd')
        self.assertEqual(argv[argv.index('-y') + 1], str(self.directory))
        self.assertNotIn('-pn', argv)

    def test_original_gate_failure_survives_exit_in_diagnostic_extension(self):
        code = 'import time; print("fake original summary", flush=True); time.sleep(.25)'
        result = self.driver.run_program([sys.executable, '-c', code], self.directory, os.environ.copy(),
                                         cap=.9, gate=.15, cleanup=.3)
        self.assertFalse(result['passed'])
        self.assertTrue(result['original_gate_failed'])
        self.assertEqual(result['original_exit'], 0)
        self.assertIn(b'fake original summary', (self.directory / 'stdout.bin').read_bytes())

    def test_raw_pipes_and_primary_exit_are_recorded_separately(self):
        result = self.driver.run_program([sys.executable, '-c',
            'import sys; print("out"); print("err", file=sys.stderr)'], self.directory, os.environ.copy(),
            cap=2, gate=1, cleanup=.3)
        self.assertTrue(result['passed'], result)
        self.assertEqual((self.directory / 'stdout.bin').read_bytes(), b'out\n')
        self.assertEqual((self.directory / 'stderr.bin').read_bytes(), b'err\n')
        self.assertTrue(result['primary_exit_observed'])
        self.assertTrue(result['stdout_eof'] and result['stderr_eof'])
        self.assertGreater(result['pid'], 0)
        self.assertTrue(result['process_identity'])

    @unittest.skipUnless(sys.platform.startswith('linux'), 'offline Linux owned-tree test')
    def test_descendant_holding_pipe_is_retired_after_primary_exit(self):
        code = ('import subprocess, sys; p = subprocess.Popen([sys.executable, "-c", '
                '"import time; time.sleep(10)"]); print(p.pid, flush=True)')
        result = self.driver.run_program([sys.executable, '-c', code], self.directory, os.environ.copy(),
                                         cap=1.5, gate=.3, cleanup=.4)
        pid = int((self.directory / 'stdout.bin').read_text())
        stat = Path('/proc') / str(pid) / 'stat'
        self.assertTrue(not stat.exists() or stat.read_text().rsplit(')', 1)[1].split()[0] == 'Z')
        self.assertFalse(result['passed'])
        self.assertTrue(result['primary_exit_observed'])
        snapshots = json.loads((self.directory / 'snapshots.json').read_text())
        self.assertTrue(any(row['primary_exit'] and not row['stdout_eof'] for row in snapshots))

    def test_timeout_keeps_raw_bytes_and_reports_failure(self):
        result = self.driver.run_program([sys.executable, '-c',
            'import time; print("before-timeout", flush=True); time.sleep(10)'], self.directory,
            os.environ.copy(), cap=.6, gate=.25, cleanup=.2)
        self.assertFalse(result['passed'])
        self.assertTrue(result['original_gate_failed'])
        self.assertIn(b'before-timeout', (self.directory / 'stdout.bin').read_bytes())
        self.assertIsNotNone(result['original_exit'])

    def test_original_traffic_error_and_independent_shutdown6_both_survive(self):
        consumer = self.driver.CaptureConsumer([sys.executable, '-c',
            'import sys; print("12345", flush=True); sys.stdin.readline(); sys.exit(6)'],
            self.directory, os.environ.copy(), self.directory / 'consumer.json')
        with self.assertRaisesRegex(RuntimeError, 'traffic mismatch'):
            with consumer:
                self.assertEqual(consumer.port(), 12345)
                consumer.process.stdin.write(b'stop\n')
                consumer.process.stdin.close()
                consumer.process.wait(timeout=1)
                raise RuntimeError('traffic mismatch')
        receipt = json.loads((self.directory / 'consumer.json').read_text())
        self.assertEqual((receipt['exit'], receipt['traffic_error'], receipt['shutdown_error']),
                         (6, 'traffic mismatch', 'consumer shutdown failed: 6'))
        self.assertEqual((self.directory / 'consumer-stdout.bin').read_bytes(), b'12345\n')

    def test_traffic_failure_does_not_add_a_second_stop_operation(self):
        consumer = self.driver.CaptureConsumer([sys.executable, '-c',
            'import sys; print("12345", flush=True); sys.stdin.readline()'],
            self.directory, os.environ.copy(), self.directory / 'consumer.json')
        consumer.stop = lambda: self.fail('diagnostic added a stop operation')
        with self.assertRaisesRegex(RuntimeError, 'traffic mismatch'):
            with consumer:
                self.assertEqual(consumer.port(), 12345)
                raise RuntimeError('traffic mismatch')
        receipt = json.loads((self.directory / 'consumer.json').read_text())
        self.assertIsNone(receipt['shutdown_error'])

    def test_wait_receipt_rejects_absent_corrupt_duplicate_and_original_failure(self):
        labels = ['dispatch_begin', 'dispatch_end', 'main_stopping_before', 'main_stopping_after',
                  'request_stop_begin', 'request_stop_end', 'host_stop_begin', 'host_stop_end', 'main_joined']
        trace = 'WAIT_CAPTURE clock=QPC frequency=10000000 rows=9 dropped=0\n' + ''.join(
            'WAIT_CAPTURE seq=%d ticks=%d %s\n' % (i, i, label) for i, label in enumerate(labels, 1))
        self.driver.check_trace(trace)
        for bad in ('', trace.replace('rows=9', 'rows=8'), trace.replace('seq=2', 'seq=1'),
                    trace.replace('host_stop_end', 'host_catch'), trace.replace('dispatch_end', 'wait_failure'),
                    trace.replace('dispatch_end', 'not_dispatch')):
            with self.assertRaises(ValueError):
                self.driver.check_trace(bad)

    def test_suite_receipts_require_original_counts_and_complete_raw_case_witnesses(self):
        output = ''.join('Running test (%d): case_%d\n' % (i, i) for i in range(1, 5))
        output += '4 tests executed\n20 checks\n-> 0 failures\n-> 0 skipped\n'
        self.driver.check_suite(output, 0, 4)
        for bad, status in [(output, 6), (output.replace('4 tests executed', '3 tests executed'), 0),
                            (output.replace('-> 0 failures', '-> 1 failures'), 0), ('', 0)]:
            with self.assertRaises(ValueError):
                self.driver.check_suite(bad, status, 4)


class WorkflowTests(unittest.TestCase):
    def test_only_manual_two_native_jobs_and_pinned_setup_are_retained(self):
        import yaml
        workflow = yaml.load((ROOT / '.github/workflows/v3-package-validation.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(set(workflow['on']), {'workflow_dispatch'})
        self.assertEqual(set(workflow['jobs']), {'windows', 'darwin'})
        self.assertEqual({job['runs-on'] for job in workflow['jobs'].values()}, {'windows-2022', 'macos-15'})
        for job in workflow['jobs'].values():
            self.assertEqual(job['timeout-minutes'], '90')
            checkout = next(step for step in job['steps'] if step.get('uses', '').startswith('actions/checkout@'))
            self.assertEqual(checkout.get('with', {}).get('fetch-depth'), '0', 'base proof requires checkout history')
            commands = '\n'.join(step.get('run', '') for step in job['steps'])
            self.assertNotIn('make check', commands)
            self.assertNotIn('check-v3-installed-consumer.py', commands)
            self.assertEqual(commands.count('ci_native_missing_receipts.py'), 1)
            self.assertTrue(any(step.get('if') == 'always()' and 'upload-artifact@' in step.get('uses', '') for step in job['steps']))


class PreservationTests(unittest.TestCase):
    def test_original_traffic_and_configuration_modules_are_unchanged(self):
        for path in ('scripts/lib/package_traffic.py', 'scripts/lib/package_process.py',
                     'scripts/lib/package_matrix.py', 'test/installed/v3-package-matrix.json'):
            self.assertEqual((ROOT / path).read_bytes(), subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT))

    def test_winsock_error_survives_destructive_clock_and_probe(self):
        fixture = ROOT / 'scripts/ci_native_missing_receipts_fixtures/wait_capture_test.cpp'
        self.assertTrue(fixture.is_file(), 'destructive Winsock fixture missing')
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / 'wait-test'
            result = subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                '-I' + str(fixture.parent), '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'examples'),
                str(fixture), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('rc=-1 error=10038 fd_count=1', result.stderr)
            self.assertIn('rc=-1 error=10004 fd_count=1', result.stderr)
            self.assertIn('validity_rc=-1 validity_error=10009', result.stderr)

    def test_consumer_original_flow_and_exit6_preserved_in_order(self):
        before = subprocess.check_output(['git', 'show', BASE + ':test/installed/package_consumer.cpp'], cwd=ROOT).decode()
        after = (ROOT / 'test/installed/package_consumer.cpp').read_text()
        self.assertIn('capture_event("main_stopping_before")', after)
        remaining = iter(after.splitlines())
        for line in before.splitlines():
            self.assertTrue(any(candidate == line for candidate in remaining), line)

    def test_original_test_counts_assertions_and_root_security_flags_preserved(self):
        paths = [('test/unit/tls_psk_test.cpp', 14), ('test/unit/quic_interop_tls_test.cpp', 4),
                 ('test/integ/native_http1_e2e_test.cpp', 62)]
        for path, count in paths:
            after = (ROOT / path).read_text().replace('LT_CHECK(capture_psk_connected);', 'LT_CHECK(psk.connect());').replace('LT_CHECK(capture_cert_connected);', 'LT_CHECK(certificate.connect());').replace('LT_CHECK(capture_cert_exchanged);', 'LT_CHECK(certificate.exchange());')
            self.assertEqual(after.count('LT_BEGIN_AUTO_TEST('), count)
            before = subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT).decode()
            self.assertEqual([line.strip() for line in after.splitlines() if 'LT_CHECK' in line or 'LT_ASSERT' in line],
                             [line.strip() for line in before.splitlines() if 'LT_CHECK' in line or 'LT_ASSERT' in line])
        root = (ROOT / 'test/support/quic_interop_files.cpp').read_text()
        self.assertIn('O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC', root)
        self.assertIn('const int saved_errno = root_ < 0 ? errno : 0;', root)
        self.assertLess(root.index('const int saved_errno'), root.index('ROOT_CAPTURE'))
        self.assertIn('throw std::runtime_error("interop root unavailable")', root)

    def test_psk_diagnostics_peek_provider_queue_and_report_each_missing_state(self):
        source = (ROOT / 'test/unit/tls_psk_test.cpp').read_text()
        self.assertIn('ERR_peek_error()', source)
        self.assertNotIn('ERR_get_error()', source)
        for label in ('cancel-held', 'timeout-held', 'mixed-psk', 'mixed-certificate'):
            self.assertIn('"' + label + '"', source)
        self.assertIn('PSK_CAPTURE', source)
        for operation in ('psk.connect()', 'certificate.connect()', 'certificate.exchange()'):
            self.assertEqual(source.count(operation), 1)
        self.assertIn('capture_cert_exchanged', source)
        self.assertIn('provider_thread=owner', source)


if __name__ == '__main__':
    unittest.main()
