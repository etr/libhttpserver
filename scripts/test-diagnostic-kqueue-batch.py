#!/usr/bin/env python3
"""Offline contracts. Every executed child is a Python fixture, never a C++ test."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'scripts/diagnostic-kqueue-batch.py'
CASE = 'accept_read_write_cancel_release_close_match_poll_trace'
FAKE = '''import json, os, sys, time
from pathlib import Path
root = Path(os.environ['FIXTURE_ROOT'])
mode = os.environ['LHS_KQUEUE_DIAGNOSTIC_MODE']
with (root / 'launches').open('a') as out: out.write(mode + '\\n')
state = json.loads((root / 'batch/receipt.json').read_text())
assert state['attempts'] == len((root / 'launches').read_text().splitlines())
assert state['processes'][-1]['exit'] is None
behavior = os.environ.get('FIXTURE_BEHAVIOR', '')
if int(os.environ.get('FIXTURE_FAIL_AT', '1')) != state['attempts']: behavior = ''
if behavior == 'timeout': time.sleep(5)
if behavior == 'crash': os.kill(os.getpid(), 15)
trace = dict(token=os.environ['LHS_KQUEUE_DIAGNOSTIC_TOKEN'],
             poll=['entry-%d' % i for i in range(17)],
             kqueue=['entry-%d' % i for i in range(17)], differences=[])
if behavior == 'mismatch':
    trace['kqueue'][3] = 'different'
    trace['differences'] = [3]
if behavior == 'wrong-token': trace['token'] = 'stale'
if behavior == 'false-differences': trace['differences'] = [2]
if behavior != 'missing': print('KQUEUE_DIAGNOSTIC ' + json.dumps(trace), flush=True)
if behavior == 'duplicate': print('KQUEUE_DIAGNOSTIC ' + json.dumps(trace))
for i in range(48 if mode == 'full' else 1):
    print('Running test (%d): %s' % (i+1, 'accept_read_write_cancel_release_close_match_poll_trace' if i == 0 else 'context_%d' % i))
print('%d tests executed' % (48 if mode == 'full' else 1))
print('%d checks\\n-> 57 successes\\n-> 0 failures\\n-> 0 skipped' % (58 if behavior == 'wrong-total' else 57))
if behavior == 'incomplete': print('-> 1 failures')
print('fixture stderr retained', file=sys.stderr)
sys.exit(7 if behavior == 'nonzero' else 0)
'''


class BatchTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(DRIVER.is_file(), 'bounded diagnostic driver is missing')
        spec = importlib.util.spec_from_file_location('diagnostic_kqueue_batch', DRIVER)
        self.driver = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.driver)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.fake = self.root / 'fake.py'
        self.fake.write_text(FAKE)
        self.evidence = self.root / 'batch'

    def run_batch(self, behavior='', **kwargs):
        env = dict(os.environ, FIXTURE_ROOT=str(self.root), FIXTURE_BEHAVIOR=behavior)
        return self.driver.run_batch([sys.executable, str(self.fake)], self.evidence,
                                     self.root, env, **kwargs)

    def receipt(self):
        return json.loads((self.evidence / 'receipt.json').read_text())

    def launches(self):
        return (self.root / 'launches').read_text().splitlines()

    def test_full_context_once_then_63_focused_and_nonreproduction_only(self):
        self.assertEqual(self.run_batch(), 0)
        self.assertEqual(self.launches(), ['full'] + ['focused'] * 63)
        receipt = self.receipt()
        self.assertEqual(receipt['attempts'], 64)
        self.assertEqual(receipt['comparisons'], 64)
        self.assertEqual(receipt['status'], 'nonreproduction')
        self.assertEqual(receipt['cause'], 'UNKNOWN')
        self.assertFalse(receipt['release_acceptance'])
        self.assertEqual(len({p['token'] for p in receipt['processes']}), 64)
        self.assertIn('fixture stderr retained', (self.evidence / 'attempt-01.stderr').read_text())

    def test_mismatch_stops_even_with_zero_child_exit(self):
        self.assertNotEqual(self.run_batch('mismatch'), 0)
        self.assertEqual(self.launches(), ['full'])
        self.assertEqual(self.receipt()['status'], 'mismatch')
        self.assertEqual(self.receipt()['processes'][0]['exit'], 0)
        self.assertEqual(self.receipt()['processes'][0]['trace']['differences'], [3])

    def test_original_nonzero_status_is_propagated(self):
        self.assertEqual(self.run_batch('nonzero'), 7)
        self.assertEqual(self.receipt()['processes'][0]['exit'], 7)
        self.assertEqual(self.launches(), ['full'])

    def test_invalid_receipts_stop_without_further_launches(self):
        for behavior in ('missing', 'duplicate', 'wrong-token', 'false-differences', 'incomplete', 'wrong-total'):
            with self.subTest(behavior=behavior), tempfile.TemporaryDirectory() as temp:
                self.root = Path(temp)
                self.fake = self.root / 'fake.py'
                self.fake.write_text(FAKE)
                self.evidence = self.root / 'batch'
                self.assertNotEqual(self.run_batch(behavior), 0)
                self.assertEqual(self.launches(), ['full'])
                self.assertEqual(self.receipt()['status'], 'invalid-receipt')

    def test_timeout_is_terminal_and_retains_actual_signal_status(self):
        self.assertEqual(self.run_batch('timeout', process_seconds=0.1), 124)
        self.assertEqual(self.launches(), ['full'])
        row = self.receipt()['processes'][0]
        self.assertTrue(row['timed_out'])
        self.assertLess(row['exit'], 0)

    def test_crash_is_terminal_with_original_negative_status(self):
        self.assertEqual(self.run_batch('crash'), 143)
        self.assertEqual(self.receipt()['processes'][0]['exit'], -15)
        self.assertEqual(self.launches(), ['full'])

    def test_existing_receipt_is_never_resumed_or_reset(self):
        self.assertEqual(self.run_batch('nonzero'), 7)
        original = (self.evidence / 'receipt.json').read_bytes()
        self.assertNotEqual(self.run_batch(), 0)
        self.assertEqual((self.evidence / 'receipt.json').read_bytes(), original)
        self.assertEqual(self.launches(), ['full'])

    def test_existing_inflight_directory_refuses_entry(self):
        self.evidence.mkdir()
        self.assertNotEqual(self.run_batch(), 0)
        self.assertFalse((self.root / 'launches').exists())

    def test_live_second_driver_refuses_without_launching_another_child(self):
        code = ('import importlib.util, os, sys; from pathlib import Path; '
                's=importlib.util.spec_from_file_location("driver", sys.argv[1]); '
                'm=importlib.util.module_from_spec(s); s.loader.exec_module(m); '
                'raise SystemExit(m.run_batch([sys.executable, sys.argv[2]], Path(sys.argv[3]), '
                'Path(sys.argv[4]), os.environ, process_seconds=1))')
        env = dict(os.environ, FIXTURE_ROOT=str(self.root), FIXTURE_BEHAVIOR='timeout')
        first = subprocess.Popen([sys.executable, '-c', code, str(DRIVER), str(self.fake),
                                  str(self.evidence), str(self.root)], env=env,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 2
            while not (self.root / 'launches').exists() and first.poll() is None and time.monotonic() < deadline:
                time.sleep(0.005)
            self.assertTrue((self.root / 'launches').exists())
            self.assertEqual(self.run_batch(), 125)
            self.assertEqual(first.wait(timeout=3), 124)
            self.assertEqual(self.launches(), ['full'])
        finally:
            if first.poll() is None:
                first.kill()
                first.wait()
            first.stderr.close()

    def test_batch_deadline_stops_before_another_process(self):
        self.assertNotEqual(self.run_batch(batch_seconds=0), 0)
        self.assertFalse((self.root / 'launches').exists())
        self.assertEqual(self.receipt()['attempts'], 0)
        self.assertEqual(self.receipt()['status'], 'batch-timeout')

    def test_cannot_raise_either_time_budget(self):
        for option in ({'batch_seconds': 301}, {'process_seconds': 21}):
            with self.subTest(option=option):
                with self.assertRaises(ValueError):
                    self.run_batch(**option)
        self.assertFalse(self.evidence.exists())

    def test_focused_failure_stops_with_no_second_full_program(self):
        env = dict(os.environ, FIXTURE_ROOT=str(self.root), FIXTURE_BEHAVIOR='nonzero', FIXTURE_FAIL_AT='2')
        self.assertEqual(self.driver.run_batch([sys.executable, str(self.fake)], self.evidence, self.root, env), 7)
        self.assertEqual(self.launches(), ['full', 'focused'])
        self.assertEqual(self.receipt()['attempts'], 2)

    def test_launch_failure_is_terminal_with_null_child_exit(self):
        self.assertEqual(self.driver.run_batch([str(self.root / 'missing')], self.evidence, self.root, os.environ), 125)
        self.assertEqual(self.receipt()['attempts'], 1)
        self.assertIsNone(self.receipt()['processes'][0]['exit'])
        self.assertIn('launch_error', self.receipt()['processes'][0])


class WorkflowTest(unittest.TestCase):
    def setUp(self):
        self.workflow = yaml.safe_load((ROOT / '.github/workflows/release.yml').read_text())

    def test_manual_only_read_only_single_darwin_job_and_one_batch_call(self):
        self.assertEqual(set(self.workflow.get('on', self.workflow.get(True, {}))), {'workflow_dispatch'})
        self.assertEqual(self.workflow['permissions'], {'contents': 'read'})
        self.assertEqual(len(self.workflow['jobs']), 1)
        job = next(iter(self.workflow['jobs'].values()))
        self.assertEqual(job['runs-on'], 'macos-15')
        runs = '\n'.join(step.get('run', '') for step in job['steps'])
        self.assertEqual(runs.count('scripts/diagnostic-kqueue-batch.py'), 1)
        self.assertNotIn('continue-on-error', str(self.workflow))
        self.assertNotIn('make check', runs)
        self.assertNotIn('lib/c++:', runs)
        self.assertNotIn('lldb', runs)

    def test_wrong_sentinel_ref_or_rerun_refuses_before_setup(self):
        job = next(iter(self.workflow['jobs'].values()))
        guard = next((step['run'] for step in job['steps'] if step.get('name') == 'Bind the single diagnostic request'), None)
        self.assertIsNotNone(guard, 'single-request refusal guard is missing')
        for key, value in (('DIAGNOSTIC_SENTINEL', 'wrong'), ('GITHUB_REF', 'refs/heads/v3'), ('GITHUB_RUN_ATTEMPT', '2')):
            with self.subTest(key=key), tempfile.TemporaryDirectory() as temp:
                env = dict(os.environ, RUNNER_TEMP=temp, GITHUB_ENV=str(Path(temp) / 'env'),
                           GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_RUN_ATTEMPT='1',
                           GITHUB_REF='refs/heads/task/FEATURE-ci-kqueue-focused-diagnostics',
                           DIAGNOSTIC_SENTINEL='kqueue-focused-once-a9c80052')
                env[key] = value
                child = subprocess.run(['bash', '-euo', 'pipefail', '-c', guard], cwd=ROOT, env=env, capture_output=True)
                self.assertNotEqual(child.returncode, 0)


class SourceContractTest(unittest.TestCase):
    def test_semantic_helper_all_original_checks_and_startup_are_preserved(self):
        path = 'test/unit/io_kqueue_backend_test.cpp'
        original = subprocess.check_output(['git', 'show', 'a9c800522afe6d2abea308e0be9215fda1b2826e:' + path], cwd=ROOT, text=True)
        current = (ROOT / path).read_text()
        def helper(source):
            start = source.index('template<typename Backend>\nstd::vector<std::string> semantic_trace')
            return source[start:source.index('\ntemplate<typename Backend>', start + 1)]
        self.assertEqual(helper(current), helper(original))
        self.assertEqual([s.strip() for s in current.splitlines() if 'LT_CHECK' in s],
                         [s.strip() for s in original.splitlines() if 'LT_CHECK' in s])
        self.assertIn('LHS_KQUEUE_DIAGNOSTIC_MODE', current)
        self.assertIn('littletest::auto_test_runner(&' + CASE + ')', current)
        self.assertEqual(current.count('LT_BEGIN_AUTO_TEST(kqueue_suite,'), 48)
        self.assertIn('LT_BEGIN_AUTO_TEST_ENV()', current)
        self.assertIn('AUTORUN_TESTS()', current)
        case = current[current.index('LT_BEGIN_AUTO_TEST(kqueue_suite, ' + CASE):]
        self.assertLess(case.index('const auto kqueue = semantic_trace'), case.index('KQUEUE_DIAGNOSTIC'))
        self.assertLess(case.index('KQUEUE_DIAGNOSTIC'), case.index('LT_CHECK(poll == kqueue)'))


if __name__ == '__main__':
    unittest.main()
