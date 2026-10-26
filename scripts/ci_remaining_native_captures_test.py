"""Offline preparation contracts; never run an original native target."""
import ast
import importlib.util
import inspect
import json
import os
from pathlib import Path
import subprocess
import sys
import struct
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
BASE = 'b2ee4a2bd2a779921c9200a6afa8bc222fcc64d2'
DRIVER = ROOT / 'scripts/ci_remaining_native_captures.py'


def original(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT)


class DriverTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(DRIVER.is_file(), 'five-category driver is missing')
        spec = importlib.util.spec_from_file_location('remaining', DRIVER)
        self.driver = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.driver)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def prepared_context(self, host):
        """Exercise prepare and real package compilation through an offline OS boundary."""
        calls = []
        directory = self.directory / host
        directory.mkdir()
        provider = directory / 'provider'
        for name in ('include/openssl/opensslv.h', 'lib/libssl.a', 'bin/openssl'):
            path = provider / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('offline provider')
        args = SimpleNamespace(compiler='offline-c++', cc='offline-cc', make='gmake',
                               provider_prefix=provider, pkg_config_dir=[], toolchain_prefix=None)
        def command(argv, cwd, env, **kwargs):
            argv, cwd = list(map(str, argv)), Path(cwd)
            calls.append((argv, cwd, dict(env)))
            if 'cygpath' in ' '.join(argv):
                return str(ROOT / 'configure')
            if len(argv) > 1 and argv[1].endswith('/configure'):
                for name in ('config.h', 'config.log', 'Makefile', 'test/Makefile'):
                    path = cwd / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text('offline configure ' + env['CXXFLAGS'])
            if argv[0] == 'gmake' and 'src' in argv:
                libs = cwd / 'src/.libs'
                libs.mkdir(parents=True, exist_ok=True)
                (libs / 'libhttpserver_v3core.a').write_bytes(b'internal core')
                if 'install' in argv:
                    # Use the configured prefix, including the separate static lane.
                    configured = next(row for row in reversed(calls) if row[1] == cwd and
                                      len(row[0]) > 1 and row[0][1].endswith('/configure'))
                    prefix = Path(next(item.split('=', 1)[1] for item in configured[0] if item.startswith('--prefix=')))
                    for name in ('include/httpserver.hpp', 'lib/libhttpserver.a', 'lib/libhttpserver.dll.a', 'bin/libhttpserver.dll'):
                        path = prefix / name
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(b'installed artifact')
            if argv[0] == 'gmake' and 'test' in argv:
                for name in ('tls_psk', 'native_websocket_fixture', 'external_http1_e2e'):
                    if name in argv:
                        path = cwd / 'test' / name
                        path.write_bytes(b'offline selected target')
            if argv[0] == 'pkg-config':
                prefix = Path(env['PKG_CONFIG_LIBDIR'].split(os.pathsep)[0]).parents[1]
                if '--cflags' in argv:
                    return '-I' + str(prefix / 'include')
                if '--static' in argv:
                    return '-L' + str(prefix / 'lib') + ' -lhttpserver -pthread'
                return '-L' + str(prefix / 'lib') + ' -lhttpserver'
            if argv[0] == 'offline-c++' and '-o' in argv:
                output = Path(argv[argv.index('-o') + 1])
                output.write_bytes(b'offline compiler artifact')
                if '-MF' in argv:
                    header = next(item[2:] for item in argv if item.startswith('-I') and item.endswith('/include'))
                    Path(argv[argv.index('-MF') + 1]).write_text(str(output) + ': ' + str(Path(header) / 'httpserver.hpp'))
                else:
                    Path(str(output) + '.map').write_text(' '.join(argv))
            if argv[0] == str(provider / 'bin/openssl'):
                return 'OpenSSL 3.5.9 offline test'
            return 'offline command receipt'
        with patch.object(self.driver.platform, 'system', return_value=host), \
                patch.object(self.driver, 'Commands', return_value=command), \
                patch.object(self.driver.shutil, 'which', return_value='/usr/bin/true'), \
                patch.object(self.driver.compiler_sdk, 'capture', return_value={'offline_sdk': True}):
            programs, runtime = self.driver.prepare(args, ROOT, directory, {'PATH': '/usr/bin'})
        return calls, programs, runtime, json.loads((directory / 'context.json').read_text())

    def test_prepare_linux_keeps_effective_O3_and_native_internal_static_fixture(self):
        calls, programs, runtime, context = self.prepared_context('Linux')
        build_calls = [row for row in calls if row[0][0] == 'gmake']
        self.assertTrue(build_calls)
        for argv, cwd, env in build_calls:
            optimization = [flag for flag in env['CXXFLAGS'].split() if flag.startswith('-O')]
            self.assertEqual(optimization[-1], '-O3')
        self.assertIn((['gmake', '-j2', '-C', 'test', 'native_websocket_fixture'], self.directory / 'Linux/build'),
                      [(argv, cwd) for argv, cwd, _ in calls])
        self.assertFalse(context['compiles'])
        self.assertEqual(programs['wire_cleanup'], self.directory / 'Linux/build/test/native_websocket_fixture')
        makefile = (ROOT / 'test/Makefile.am').read_text()
        self.assertIn('native_websocket_fixture_LDADD = $(top_builddir)/src/libhttpserver_v3core.la', makefile)
        self.assertIn(str(self.directory / 'Linux/build/src/.libs/libhttpserver_v3core.a'), context['artifact_sha256'])

    def test_prepare_windows_uses_installed_shared_DLL_and_original_O0_sources(self):
        calls, programs, runtime, context = self.prepared_context('Windows')
        self.assertEqual(set(programs), {'consumer', 'native_http1_e2e'})
        self.assertTrue(any(argv == ['gmake', '-C', 'src', 'install'] for argv, _, _ in calls))
        self.assertTrue(any(argv == ['pkg-config', '--libs', 'libhttpserver'] for argv, _, _ in calls))
        links = [argv for argv, _, _ in calls if argv[0] == 'offline-c++' and '-c' not in argv and '-o' in argv]
        self.assertEqual(len(links), 2)
        self.assertTrue(all('-lhttpserver' in argv and '-lws2_32' in argv for argv in links))
        for row in context['compiles'].values():
            self.assertEqual(row['compile_options'], ['-O0', '-g'])
            self.assertTrue(all('/prefix/include/' in name for name in row['dependencies']))
        units = context['compiles']['native_http1_e2e']['sources']
        self.assertEqual(set(units), {str(ROOT / 'test' / unit) for unit in
                         self.driver.package_matrix.load()['suites']['native_http1_e2e']['sources']})
        self.assertTrue(any(name.endswith('libhttpserver.dll') for name in context['artifact_sha256']))

    def test_prepare_darwin_separates_native_PSK_and_installed_static_external_suite(self):
        calls, programs, runtime, context = self.prepared_context('Darwin')
        self.assertTrue(any(argv == ['gmake', '-j2', '-C', 'test', 'tls_psk'] and
                            cwd == self.directory / 'Darwin/build' for argv, cwd, _ in calls))
        self.assertTrue(all('-O0' in env['CXXFLAGS'] for argv, _, env in calls if argv[0] == 'gmake'))
        self.assertFalse(any(argv[0] == 'gmake' and 'external_http1_e2e' in argv for argv, _, _ in calls),
                         'external package suite must not link the internal core target')
        row = context['compiles']['external_http1_e2e']
        self.assertEqual(row['compile_options'], ['-O0', '-g'])
        self.assertEqual(set(row['sources']), {str(ROOT / 'test' / unit) for unit in
                         self.driver.package_matrix.load()['suites']['external_http1_e2e']['sources']})
        self.assertTrue(all('/plain-prefix/include/' in name for name in row['dependencies']))
        self.assertTrue(any(argv == ['pkg-config', '--libs', '--static', 'libhttpserver'] for argv, _, _ in calls))
        link = next(argv for argv, _, _ in calls if argv[0] == 'offline-c++' and '-c' not in argv and '-o' in argv)
        self.assertIn(str(self.directory / 'Darwin/plain-prefix/lib/libhttpserver.a'), link)
        self.assertNotIn('-lhttpserver', link)
        self.assertTrue(any(name.endswith('/plain-prefix/lib/libhttpserver.a') for name in context['artifact_sha256']))
        config = next(argv for argv, cwd, _ in calls if cwd == self.directory / 'Darwin/plain-build' and
                      len(argv) > 1 and argv[1].endswith('/configure'))
        self.assertTrue({'--disable-v3-tls', '--disable-shared', '--enable-static'}.issubset(config))

    def test_fixed_partitions_maximum_launches_and_actual_seconds(self):
        self.assertEqual(self.driver.PLATFORMS, {
            'Windows': ('consumer', 'native_http1_e2e'),
            'Darwin': ('tls_psk', 'external_http1_e2e'), 'Linux': ('wire_cleanup',)})
        targets = self.driver.TARGETS
        self.assertEqual(sum(value['attempts'] for value in targets.values()), 83)
        self.assertEqual(sum(value['seconds'] for value in targets.values()), 910)
        self.assertEqual(targets['consumer'], dict(attempts=64, seconds=300, cap=30, gate=30, count=None))
        self.assertEqual(targets['wire_cleanup'], dict(attempts=16, seconds=300, cap=60, gate=60, count=None))
        self.assertEqual(targets['native_http1_e2e'], dict(attempts=1, seconds=190, cap=190, gate=180, count=62))

    def test_attempts_are_durable_before_launch_and_fast_flows_reach_64(self):
        observations = []
        def launch(name, case, cap):
            ledger = json.loads((self.directory / 'budget.json').read_text())
            observations.append((name, ledger['attempts'], ledger['active_case'], cap))
            self.assertEqual(ledger['categories'][name]['attempts'], len(observations))
            with self.assertRaises(RuntimeError):
                self.driver.consume_permit(case, 'tls_psk', parent=os.getpid())
            with self.assertRaises(RuntimeError):
                self.driver.consume_permit(case, name, parent=os.getpid() + 1)
            self.driver.consume_permit(case, name, parent=os.getpid())
            with self.assertRaises(FileExistsError):
                self.driver.consume_permit(case, name, parent=os.getpid())
            return dict(passed=True)
        result = self.driver.run_batch(self.directory, ('consumer',), launch)
        self.assertEqual(len(observations), 64)
        self.assertEqual(result['attempts'], 64)
        self.assertTrue(result['passed'])
        self.assertEqual(result['state'], 'closed')
        with self.assertRaises(FileExistsError):
            self.driver.run_batch(self.directory, ('consumer',), launch)

    def test_actual_time_reduces_remaining_cap_without_nominal_precharge(self):
        now = [100.0]
        caps = []
        def launch(name, case, cap):
            caps.append(cap)
            now[0] += 29.5
            return dict(passed=True)
        with patch.object(self.driver.time, 'monotonic', side_effect=lambda: now[0]):
            result = self.driver.run_batch(self.directory, ('consumer',), launch)
        self.assertEqual(caps, [30] * 10 + [5.0])
        self.assertFalse(result['passed'], 'cap overrun must fail closed')
        self.assertEqual(result['categories']['consumer']['actual_seconds'], 324.5)
        self.assertEqual(result['attempts'], 11)

    def test_remaining_actual_allowance_exhaustion_stops_without_extra_launch(self):
        now = [0.0]
        caps = []
        def launch(name, case, cap):
            caps.append(cap)
            now[0] += cap
            return dict(passed=True)
        with patch.object(self.driver.time, 'monotonic', side_effect=lambda: now[0]):
            result = self.driver.run_batch(self.directory, ('wire_cleanup',), launch)
        self.assertEqual(caps, [60] * 5)
        self.assertTrue(result['passed'])
        self.assertEqual(result['categories']['wire_cleanup']['actual_seconds'], 300)

    def test_stop_first_failure_and_independent_categories_are_each_attempted(self):
        calls = []
        def launch(name, case, cap):
            calls.append(name)
            if name == 'consumer':
                raise RuntimeError('traffic failure')
            return dict(passed=False, error='original failure')
        result = self.driver.run_batch(self.directory, tuple(self.driver.TARGETS), launch)
        self.assertEqual(calls, list(self.driver.TARGETS))
        self.assertFalse(result['passed'])
        self.assertEqual(result['attempts'], 5)
        self.assertTrue(all(row['state'] == 'closed' for row in result['categories'].values()))

    def test_missing_invalid_outcomes_stop_first_and_fail_closed(self):
        for number, outcome in enumerate((None, {}, dict(passed='yes'), dict(passed=1))):
            result = self.driver.run_batch(self.directory / str(number), ('consumer',), lambda *_: outcome)
            self.assertFalse(result['passed'])
            self.assertEqual(result['attempts'], 1)

    def test_duplicate_reordered_unknown_selections_refused_before_launch(self):
        for names in ((), ('other',), ('consumer', 'consumer'), ('tls_psk', 'consumer')):
            with self.assertRaises(ValueError):
                self.driver.run_batch(self.directory, names, lambda *_: self.fail('unauthorized launch'))
        self.assertFalse((self.directory / 'once.json').exists())

    def test_target_environment_excludes_all_approval_and_auth_credentials(self):
        env = dict(PATH='/bin', HOME='/tmp', GITHUB_TOKEN='secret', GH_TOKEN='secret',
                   AWS_SECRET_ACCESS_KEY='secret', RANDOM_SECRET='secret',
                   CI_DIAGNOSTIC_APPROVED_SHA=BASE, GITHUB_REF='refs/heads/x')
        self.assertEqual(self.driver.target_environment(env), dict(PATH='/bin', HOME='/tmp'))

    def test_default_cli_refuses_preparation_execution(self):
        result = subprocess.run([sys.executable, str(DRIVER), '--output', str(self.directory / 'absent')],
                                capture_output=True, text=True, env=self.driver.target_environment(os.environ))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('approved native CI context required', result.stderr)
        self.assertFalse((self.directory / 'absent').exists())

    def test_raw_pipes_and_primary_exit_have_separate_receipts(self):
        result = self.driver.run_program([sys.executable, '-c', 'import sys; print("out"); print("err", file=sys.stderr)'],
                                        self.directory, os.environ, cap=2, gate=1, cleanup=.3)
        self.assertTrue(result['passed'], result)
        self.assertEqual((self.directory / 'stdout.bin').read_bytes(), b'out\n')
        self.assertEqual((self.directory / 'stderr.bin').read_bytes(), b'err\n')
        self.assertTrue(result['primary_exit_observed'] and result['stdout_eof'] and result['stderr_eof'])
        self.assertTrue(result['process_identity'])

    def test_original_gate_failure_survives_normal_exit_in_diagnostic_extension(self):
        result = self.driver.run_program([sys.executable, '-c', 'import time; print("fake summary", flush=True); time.sleep(.25)'],
                                        self.directory, os.environ, cap=.9, gate=.15, cleanup=.3)
        self.assertFalse(result['passed'])
        self.assertTrue(result['original_gate_failed'])
        self.assertEqual(result['original_exit'], 0)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'offline owned process proof')
    def test_owned_descendant_holding_pipe_is_retired_without_false_eof_success(self):
        code = ('import subprocess, sys; p = subprocess.Popen([sys.executable, "-c", '
                '"import time; time.sleep(10)"]); print(p.pid, flush=True)')
        result = self.driver.run_program([sys.executable, '-c', code], self.directory, os.environ,
                                        cap=1.5, gate=.3, cleanup=.4)
        pid = int((self.directory / 'stdout.bin').read_text())
        stat = Path('/proc') / str(pid) / 'stat'
        self.assertTrue(not stat.exists() or stat.read_text().rsplit(')', 1)[1].split()[0] == 'Z')
        self.assertFalse(result['passed'])
        self.assertTrue(result['primary_exit_observed'])
        snapshots = json.loads((self.directory / 'snapshots.json').read_text())
        self.assertTrue(any(row['primary_exit'] and not row['stdout_eof'] for row in snapshots))

    def test_suite_receipts_require_counts_and_original_failures(self):
        output = ''.join('Running test (%d): case_%d\n' % (i, i) for i in range(1, 5))
        output += '4 tests executed\n20 checks\n-> 0 failures\n-> 0 skipped\n'
        self.driver.check_suite(output, 0, 4)
        for bad, status in ((output, 6), (output.replace('4 tests executed', '3 tests executed'), 0),
                            (output.replace('-> 0 failures', '-> 1 failures'), 0), ('', 0)):
            with self.assertRaises(ValueError):
                self.driver.check_suite(bad, status, 4)

    def test_wire_receipt_requires_all_six_original_checks_and_exact_terminal_witness(self):
        labels = self.driver.WIRE_CHECKS
        self.assertEqual(len(labels), 6)
        output = ''.join(label + ' PASS\n' for label in labels)
        output += 'WIRE_FIXTURE_STDOUT_BEGIN\nSTATS 10 10\nSTOPPED\nWIRE_DECISION exit=0 failed=0 accepted=10 closed=10 failure_reasons=0\nWIRE_FIXTURE_STDOUT_END\n'
        result = self.driver.check_wire(output)
        self.assertEqual(result['accepted'], 10)
        for invalid in ('', output.replace(labels[0] + ' PASS', 'missing'), output.replace('exit=0', 'exit=1'),
                        output.replace('closed=10', 'closed=9'), output.replace('failed=0', 'failed=1'),
                        output.replace('STATS 10 10', ''), output + 'Drain DRAIN raw PASS\n'):
            with self.assertRaises(ValueError):
                self.driver.check_wire(invalid)

    def test_no_cdb_exit_code_can_stand_in_for_a_valid_dump(self):
        self.assertFalse(hasattr(self.driver, 'noninvasive_stack'))
        self.assertFalse(hasattr(self.driver, 'stack_command'))
        self.assertTrue(hasattr(self.driver, 'dump_worker'))
        self.assertTrue((ROOT / 'scripts/ci_remaining_native_captures_minidump.py').is_file())


    def test_source_and_first_attempt_guards_refuse_before_any_setup_or_target(self):
        for attempt, ref, approved, expected in (
                ('2', 'refs/heads/task/' + self.driver.IDENTITY, BASE, 'workflow retry'),
                ('1', 'refs/heads/v3', BASE, 'branch identity'),
                ('1', 'refs/heads/task/' + self.driver.IDENTITY, '0' * 40, 'source commit')):
            environment = dict(self.driver.target_environment(os.environ), GITHUB_ACTIONS='true',
                               GITHUB_RUN_ATTEMPT=attempt, GITHUB_REF=ref, CI_DIAGNOSTIC_APPROVED_SHA=approved)
            result = subprocess.run([sys.executable, str(DRIVER), '--output', str(self.directory / 'absent')],
                                    capture_output=True, text=True, env=environment)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(expected, result.stderr)
            self.assertFalse((self.directory / 'absent').exists())

    def test_dump_qualification_requires_api_callback_and_independent_payload_identity(self):
        path = self.directory / 'fake.dmp'
        path.write_bytes(PayloadContracts().payload())
        identity = dict(pid=9, created_unix=100, image='C:\\fake.exe')
        receipt = dict(status='CAPTURED', api_result=True, passed=True, dump_type=0x1000,
                       full_memory=False, owned_identity=identity,
                       callback=dict(io_started=True, io_finished=True, rejected=False, writes=1, max_end=512))
        result = self.driver.qualify_dump(dict(passed=True), receipt, path, identity)
        self.assertEqual(result['status'], 'captured')
        for change in (dict(status='UNAVAILABLE'), dict(api_result=False), dict(passed=False),
                       dict(callback={}), dict(owned_identity=dict(pid=10)), dict(full_memory=True)):
            candidate = dict(receipt, **change)
            with self.assertRaises(ValueError):
                self.driver.qualify_dump(dict(passed=True), candidate, path, identity)
        with self.assertRaises(ValueError):
            self.driver.qualify_dump(dict(passed=False), receipt, path, identity)
        path.write_bytes(b'cdb exited 0: unsupported detach')
        with self.assertRaises(ValueError):
            self.driver.qualify_dump(dict(passed=True), receipt, path, identity)

    def test_psk_receipt_requires_owning_worker_and_nontruncated_provider_progress(self):
        text = ('PSK_PROVIDER session=0x123 impl=0x456 thread=42 rc=-1 ssl_error=2 first=0 last=0 '
                'state=22 version=772 input_pending=0 output_pending=100 capacity=16384 result=1 failure=0\n'
                'PSK_WORKER session=0x123 request=0x55 thread=42 input=12 output=100 result=1 failure=0 stop=0\n'
                'PSK_LOOKUP phase=callback-enter id=0x66 thread=43 generation=1 version=1 status=0\n'
                'PSK_LANE phase=admitted id=0x77 thread=42 admitted=1 queued=1 live=2 status=0\n')
        self.driver.check_psk(text)
        for bad in ('', text.replace('thread=42 input=12', 'thread=99 input=12'),
                    text.replace('PSK_LOOKUP', 'missing'), text + 'PSK_PROVIDER truncated=1\n'):
            with self.assertRaises(ValueError):
                self.driver.check_psk(bad)


    def test_traffic_error_and_independent_original_shutdown_error_are_both_retained(self):
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

    def test_traffic_failure_never_adds_another_original_stop_operation(self):
        consumer = self.driver.CaptureConsumer([sys.executable, '-c',
            'import sys; print("12345", flush=True); sys.stdin.readline()'],
            self.directory, os.environ.copy(), self.directory / 'consumer.json')
        consumer.stop = lambda: self.fail('diagnostic added a stop operation')
        with self.assertRaisesRegex(RuntimeError, 'traffic mismatch'):
            with consumer:
                self.assertEqual(consumer.port(), 12345)
                raise RuntimeError('traffic mismatch')
        self.assertIsNone(json.loads((self.directory / 'consumer.json').read_text())['shutdown_error'])

    def test_stop_witness_does_not_hide_winsock_or_missing_shutdown_failure(self):
        self.driver.check_trace('STOP_CAPTURE bits=127 failed=0 running=0\n')
        for bad in ('', 'STOP_CAPTURE bits=63 failed=0 running=0\n',
                    'STOP_CAPTURE bits=127 failed=1 running=0\n',
                    'STOP_CAPTURE bits=127 failed=0 running=1\n',
                    'WSA_WAIT_ERROR code=6 stop_bits=7\nSTOP_CAPTURE bits=127 failed=0 running=0\n'):
            with self.assertRaises(ValueError):
                self.driver.check_trace(bad)



class FixtureStdoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        spec = importlib.util.spec_from_file_location('wire_capture', ROOT / 'test/integ/native_websocket_client.py')
        self.client = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.client)
        self.stub = self.directory / 'fixture-stub'
        self.stub.write_text('#!' + sys.executable + '\nimport time\nprint("READY 1234", flush=True)\n'
                             'print("STATS observed-before-stop", flush=True)\ntime.sleep(30)\n')
        self.stub.chmod(0o700)
        self.receipt = self.directory / 'fixture-stdout.bin'

    def start(self):
        self.assertIn('stdout_receipt', inspect.signature(self.client.start).parameters, 'no incremental receipt boundary')
        process, events, _ = self.client.start(str(self.stub), stdout_receipt=self.receipt)
        self.addCleanup(lambda: self.retire(process))
        self.assertEqual(events.get(timeout=2), 'STATS observed-before-stop')
        self.assertIn(b'STATS observed-before-stop', self.receipt.read_bytes())
        return process

    @staticmethod
    def retire(process):
        if process.poll() is None:
            process.kill()
        process.wait(timeout=2)
        process.task_remaining_output_reader.join(timeout=1)
        process.task122_error_reader.join(timeout=1)
        for pipe in (process.stdin, process.stdout, process.stderr):
            if not pipe.closed:
                pipe.close()

    def test_already_observed_stdout_survives_original_stop_wait_exception(self):
        process = self.start()
        original_wait = process.wait
        calls = []
        def wait(timeout):
            calls.append(timeout)
            if timeout == 10:
                raise subprocess.TimeoutExpired(str(self.stub), timeout)
            return original_wait(timeout=timeout)
        with patch.object(process, 'wait', side_effect=wait):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.client.stop(process)
        self.assertEqual(calls, [10, 5])
        self.assertIsNotNone(process.poll())
        self.assertEqual(self.receipt.read_bytes(), b'READY 1234\nSTATS observed-before-stop\n')

    @unittest.skipIf(os.name == 'nt', 'POSIX owned wrapper retirement contract')
    def test_already_observed_stdout_survives_outer_wrapper_retirement(self):
        spec = importlib.util.spec_from_file_location('capture_driver', DRIVER)
        driver = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(driver)
        wrapper = ('import importlib.util, time; '
                   's=importlib.util.spec_from_file_location("wire", ' + repr(str(ROOT / 'test/integ/native_websocket_client.py')) + '); '
                   'm=importlib.util.module_from_spec(s); s.loader.exec_module(m); '
                   'p,e,port=m.start(' + repr(str(self.stub)) + ', stdout_receipt=' + repr(str(self.receipt)) + '); '
                   'assert e.get(timeout=2)=="STATS observed-before-stop"; '
                   'print("observed", flush=True); time.sleep(30)')
        result = driver.run_program([sys.executable, '-c', wrapper], self.directory, {'PATH': os.environ['PATH']},
                                    cap=1.5, gate=1.5, cleanup=.3)
        self.assertFalse(result['passed'])
        self.assertIn(b'observed', (self.directory / 'stdout.bin').read_bytes())
        self.assertEqual(self.receipt.read_bytes(), b'READY 1234\nSTATS observed-before-stop\n')


class SourceContracts(unittest.TestCase):
    def test_original_traffic_and_configuration_modules_are_byte_identical(self):
        for path in ('scripts/lib/package_traffic.py', 'scripts/lib/package_process.py',
                     'scripts/lib/package_matrix.py', 'test/installed/v3-package-matrix.json',
                     'test/unit/tls_psk_test.cpp', 'test/integ/native_http1_e2e_test.cpp'):
            self.assertEqual((ROOT / path).read_bytes(), original(path))

    def test_error_only_winsock_capture_immediately_saves_error_and_adds_no_poll_clock_or_mutex(self):
        source = (ROOT / 'examples/host_poll.hpp').read_text()
        self.assertIn('const int saved_error = ::WSAGetLastError();', source)
        self.assertLess(source.index('const int saved_error'), source.index('capture_wait_error(fds'))
        wait = source[source.index('inline std::vector<srv::readiness_event> wait('):]
        self.assertEqual(wait.count('clock_type::now()'), 1)
        self.assertNotIn('mutex', wait)
        self.assertNotIn('capture_event', wait)
        self.assertIn('WSA_WAIT_ERROR', source)
        self.assertNotRegex(source, r'getsockopt\s*\([^;]*\bSO_ERROR\b')
        self.assertIn('socket_error=unavailable', source)
        self.assertIn('SO_TYPE', source)

    def test_consumer_original_control_flow_and_stop_order_are_retained(self):
        before = original('test/installed/package_consumer.cpp').decode().splitlines()
        after = (ROOT / 'test/installed/package_consumer.cpp').read_text()
        remaining = iter(after.splitlines())
        for line in before:
            self.assertTrue(any(candidate == line for candidate in remaining), line)
        self.assertIn('capture_stop_phase(1)', after)
        self.assertIn('capture_stop_receipt', after)

    def test_psk_original_handshake_calls_classify_before_any_provider_probe(self):
        source = (ROOT / 'src/detail/tls_session.cpp').read_text()
        self.assertEqual(source.count('SSL_do_handshake(impl_->ssl)'), 1)
        self.assertIn('ERR_peek_error()', source)
        self.assertIn('ERR_peek_last_error()', source)
        self.assertNotIn('ERR_get_error', source)
        self.assertIn('PSK_PROVIDER', source)
        self.assertIn('BIO_ctrl_pending', source)
        self.assertIn('SSL_get_state', source)
        backend = (ROOT / 'src/detail/tls_io_backend.cpp').read_text()
        self.assertIn('PSK_WORKER', backend)
        self.assertEqual(backend.count('step->session->handshake()'), 1)
        self.assertEqual(backend.count('step->session->drain(*step->output)'), 1)
        runtime = (ROOT / 'src/detail/tls_psk_runtime.cpp').read_text()
        self.assertIn('PSK_LOOKUP', runtime)
        self.assertIn('PSK_LANE', runtime)
        self.assertEqual(runtime.count('completed->changed.wait_until('), 1)
        self.assertEqual(runtime.count('result = callback(owned, context);'), 1)

    def test_external_original_four_assertions_and_5000ms_peer_close_gate_are_preserved(self):
        path = 'test/integ/external_http1_e2e_test.cpp'
        before = original(path).decode()
        after = (ROOT / path).read_text()
        self.assertEqual(after.count('LT_BEGIN_AUTO_TEST('), 4)
        self.assertEqual([line.strip() for line in after.splitlines() if 'LT_CHECK' in line or 'LT_ASSERT' in line],
                         [line.strip() for line in before.splitlines() if 'LT_CHECK' in line or 'LT_ASSERT' in line])
        self.assertIn('EXTERNAL_CLOSE_CAPTURE', after)
        self.assertIn('std::try_to_lock', after, 'failure snapshot must not wait on the host mutex')
        self.assertIn('unavailable', after)
        self.assertIn('peer_closed()', after)
        raw = (ROOT / 'test/integ/raw_http_client.hpp').read_bytes()
        self.assertEqual(raw, original('test/integ/raw_http_client.hpp'))
        self.assertIn(b'kExchangeBudget{5000}', raw)

    def test_close_snapshot_releases_mutex_before_formatting_or_IO(self):
        source = (ROOT / 'test/integ/external_http1_e2e_test.cpp').read_text()
        capture = source[source.index('    void capture_close('):source.index('    std::atomic<std::size_t> capture_sockets')]
        # Parse lexical scopes so a future rename cannot make a held try-lock look released.
        depth, lock_depth, unlocked = 0, None, False
        for line in capture.splitlines():
            depth += line.count('{')
            if 'std::unique_lock' in line:
                lock_depth = depth
            if 'std::to_string' in line or 'std::fprintf' in line:
                self.assertTrue(unlocked, 'diagnostic formatting/IO still holds the queue mutex')
            depth -= line.count('}')
            if lock_depth is not None and depth < lock_depth:
                unlocked = True
        self.assertTrue(unlocked)

    def test_wire_original_helper_ast_and_selected_six_checks_cleanup_only(self):
        path = 'test/integ/native_websocket_client.py'
        before = ast.parse(original(path))
        after = ast.parse((ROOT / path).read_text())
        names = ('wait_event', 'request', 'frame', 'Peer', 'raw_cases', 'backpressure', 'isolation_case',
                 'handler_drain_case', 'drain_case', 'external', 'external_backpressure')
        for name in names:
            a = next(node for node in before.body if getattr(node, 'name', None) == name)
            b = next(node for node in after.body if getattr(node, 'name', None) == name)
            self.assertEqual(ast.dump(a), ast.dump(b), name)
        text = (ROOT / path).read_text()
        self.assertIn('--first-wire-cycle-only', text)
        main = next(node for node in after.body if getattr(node, 'name', None) == 'main')
        selected = next(node for node in main.body if isinstance(node, ast.If) and
                        ast.unparse(node.test) == 'args.first_wire_cycle_only')
        called = [ast.unparse(node.func) for node in ast.walk(selected) if isinstance(node, ast.Call) and ast.unparse(node.func) != 'parser.error']
        self.assertEqual(called, ['start', 'raw_cases', 'backpressure', 'isolation_case', 'stop'])
        self.assertIn('task_remaining_stdout', text)
        old_stop = next(node for node in before.body if getattr(node, 'name', None) == 'stop')
        new_stop = next(node for node in after.body if getattr(node, 'name', None) == 'stop')
        self.assertEqual(ast.dump(old_stop.body[0]), ast.dump(new_stop.body[0]))
        for path in ('src/detail/tls_io_backend.cpp', 'src/detail/tls_psk_runtime.cpp'):
            old_lines = original(path).decode().splitlines()
            new_lines = (ROOT / path).read_text().splitlines()
            unchanged = lambda lines: [line.strip() for line in lines if any(word in line for word in
                ('std::lock_guard', 'std::unique_lock', '.wait(', '.wait_until(', '.notify_', 'sleep_for'))]
            self.assertEqual(unchanged(old_lines), unchanged(new_lines), path)

    def test_wire_terminal_original_predicate_is_evaluated_once_before_logging(self):
        source = (ROOT / 'test/integ/native_websocket_fixture.cpp').read_text()
        self.assertEqual(source.count('if (failed || accepted != closed)'), 1)
        decision = source[source.index('if (failed || accepted != closed)'):]
        self.assertIn('WIRE_DECISION', decision)
        self.assertIn('decision_failed', decision)
        self.assertIn('decision_accepted', decision)
        self.assertIn('decision_closed', decision)
        self.assertNotIn('return failed ||', source)

    def test_only_manual_three_fixed_native_jobs_and_source_attempt_guards(self):
        import yaml
        workflow = yaml.load((ROOT / '.github/workflows/v3-package-validation.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(set(workflow['on']), {'workflow_dispatch'})
        self.assertEqual(workflow['permissions'], {'contents': 'read'})
        self.assertEqual(set(workflow['jobs']), {'windows', 'darwin', 'linux'})
        self.assertEqual({job['runs-on'] for job in workflow['jobs'].values()}, {'windows-2022', 'macos-15', 'ubuntu-24.04'})
        for job in workflow['jobs'].values():
            commands = '\n'.join(step.get('run', '') for step in job['steps'])
            self.assertIn('FEATURE-ci-remaining-native-captures', commands)
            self.assertIn('GITHUB_RUN_ATTEMPT', commands)
            self.assertIn('APPROVED_SHA', commands)
            self.assertEqual(commands.count('scripts/ci_remaining_native_captures.py'), 1)
            self.assertNotIn('make check', commands)
            self.assertNotIn('check-v3-installed-consumer.py', commands)
            self.assertTrue(any(step.get('if') == 'always()' and 'upload-artifact@' in step.get('uses', '') for step in job['steps']))


class PayloadContracts(unittest.TestCase):
    def payload(self):
        data = bytearray(512)
        struct.pack_into('<4sIIIIIQ',data,0,b'MDMP',42899,3,32,0,0,0x1000)
        struct.pack_into('<III',data,32,3,52,68)
        struct.pack_into('<III',data,44,4,112,120)
        struct.pack_into('<III',data,56,15,24,232)
        struct.pack_into('<I',data,68,1)
        struct.pack_into('<I',data,72,42)
        struct.pack_into('<II',data,104,16,256)
        struct.pack_into('<II',data,112,16,272)
        struct.pack_into('<I',data,120,1)
        struct.pack_into('<QIIII',data,124,0x140000000,4096,0,0,288)
        name='C:\\fake.exe'.encode('utf-16-le')
        struct.pack_into('<I',data,288,len(name))
        data[292:292+len(name)]=name
        struct.pack_into('<IIIIII',data,232,24,3,9,100,0,0)
        return data
    def validate(self,data):
        with tempfile.TemporaryDirectory() as temporary:
            p=Path(temporary)/'fake.dmp'
            p.write_bytes(data)
            spec = importlib.util.spec_from_file_location('dump_payload', ROOT / 'scripts/ci_remaining_native_captures_minidump.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module.validate_dump(p,9,100,'C:\\fake.exe')
    def test_real_payload_shape_required(self):
        result=self.validate(self.payload())
        self.assertEqual(result['threads'][0]['id'],42)
        self.assertEqual(result['module_count'],1)
    def test_invalid_signature_rejected(self):
        data=self.payload(); data[0:4]=b'FAKE'
        with self.assertRaises(ValueError): self.validate(data)
    def test_empty_thread_stream_rejected(self):
        data=self.payload();struct.pack_into('<I',data,68,0)
        with self.assertRaises(ValueError): self.validate(data)
    def test_empty_module_stream_rejected(self):
        data=self.payload();struct.pack_into('<I',data,120,0)
        with self.assertRaises(ValueError): self.validate(data)
    def test_invalid_directory_rejected(self):
        data=self.payload();struct.pack_into('<I',data,12,500)
        with self.assertRaises(ValueError): self.validate(data)
    def test_bad_context_location_rejected(self):
        data=self.payload();struct.pack_into('<II',data,112,16,510)
        with self.assertRaises(ValueError): self.validate(data)
    def test_process_identity_mismatch_rejected(self):
        data=self.payload();struct.pack_into('<I',data,240,10)
        with self.assertRaises(ValueError): self.validate(data)
    def test_creation_identity_mismatch_rejected(self):
        data=self.payload();struct.pack_into('<I',data,244,101)
        with self.assertRaises(ValueError): self.validate(data)
    def test_full_memory_rejected(self):
        data=self.payload();struct.pack_into('<Q',data,24,2)
        with self.assertRaises(ValueError): self.validate(data)
    def test_module_image_identity_required(self):
        data=self.payload();data[292:294]=b'X\0'
        with self.assertRaises(ValueError): self.validate(data)


if __name__ == '__main__':
    unittest.main()
