#!/usr/bin/env python3
"""Dependency-free, real subprocess contracts for release receipts."""
import copy
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/run-v3-release-gates.py'


class ReleaseContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if SCRIPT.is_file():
            spec = importlib.util.spec_from_file_location('release', SCRIPT)
            cls.gate = importlib.util.module_from_spec(spec)
            sys.modules['release'] = cls.gate
            spec.loader.exec_module(cls.gate)

    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), 'release gate runner is missing')
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.binary = self.directory / 'library'
        self.binary.write_text('current packaged artifact')
        self.source = self.directory / 'source'; self.source.mkdir()
        (self.source / 'configure.ac').write_text('immutable source')
        source_files = {'configure.ac': self.gate.digest(self.source / 'configure.ac')}
        self.artifact_identity = {'schema_version': 1, 'source_root': str(self.source),
            'archive': self.gate.artifact(self.binary), 'source': {'head': 'd' * 40, 'snapshot_sha256': 'b' * 64},
            'files_sha256': source_files, 'tooling_bundle': {}}
        self.identity_path = self.directory / 'artifact-identity.json'
        self.identity_path.write_text(json.dumps(self.artifact_identity))
        self.identity = {'archive_sha256': self.gate.digest(self.binary), 'snapshot_sha256': 'b' * 64,
            'extracted_sha256': hashlib.sha256(json.dumps(source_files, sort_keys=True).encode()).hexdigest(),
            'tls': 'yes', 'sanitizer': 'none'}
        self.receipt = {'identity': self.identity.copy(), 'status': 'passed',
                        'counts': {'tests': 2, 'checks': 5, 'successes': 5, 'failures': 0, 'skipped': 0},
                        'artifacts': [self.gate.artifact(self.binary)],
                        'kind': 'installed public capability', 'witnesses': ['tls', 'h2', 'h3', 'credentials']}

    def test_inventory_cannot_omit_duplicate_or_change_required_gate(self):
        inventory = json.loads((ROOT / 'test/release/task185-gates.json').read_text())
        self.gate.validate_inventory(inventory)
        for edit in ('omit', 'duplicate', 'changed', 'exclusion'):
            bad = copy.deepcopy(inventory)
            if edit == 'omit': bad['gates'].pop()
            if edit == 'duplicate': bad['gates'].append(bad['gates'][0])
            if edit == 'changed': bad['gates'][0]['runner'] = 'true'
            if edit == 'exclusion': bad['exclusions'].append({'id': 'unknown', 'reason': 'optional'})
            with self.subTest(edit=edit), self.assertRaises(ValueError):
                self.gate.validate_inventory(bad)

    def test_packaged_example_guard_requires_http3_source_and_rejects_unknown_target(self):
        root = self.directory / 'examples-package'
        (root / 'scripts').mkdir(parents=True)
        (root / 'examples').mkdir()
        script = root / 'scripts/check-examples.sh'
        shutil.copyfile(ROOT / 'scripts/check-examples.sh', script)
        shutil.copyfile(ROOT / 'examples/Makefile.am', root / 'examples/Makefile.am')
        names = ('v3_external_event_loop', 'v3_routes', 'v3_protocol_options',
                 'v3_websocket_echo', 'v3_http3')
        for name in names:
            (root / 'examples' / (name + '.cpp')).touch()

        def check():
            return subprocess.run(['bash', str(script)], capture_output=True, text=True, timeout=5)

        accepted = check()
        self.assertEqual(accepted.returncode, 0, accepted.stdout + accepted.stderr)
        (root / 'examples/v3_http3.cpp').unlink()
        missing = check()
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn('missing native example: v3_http3', missing.stderr)
        (root / 'examples/v3_http3.cpp').touch()
        with (root / 'examples/Makefile.am').open('a') as stream:
            stream.write('\nnoinst_PROGRAMS += unrecognized_example\n')
        (root / 'examples/unrecognized_example.cpp').touch()
        unknown = check()
        self.assertNotEqual(unknown.returncode, 0)
        self.assertIn('native public example manifest differs', unknown.stderr)

    def test_tooling_specs_bundle_does_not_enable_source_checkout_documentation_gates(self):
        text = (ROOT / 'Makefile.am').read_text()
        recipe = re.search(r'^check-local:[^\n]*\n\t@(if .*?\n\tfi)', text, re.M | re.S)[1]
        recipe = recipe.replace('$(top_srcdir)', '${DOC_SOURCE}')
        recipe = recipe.replace('$(MAKE) $(AM_MAKEFLAGS)', "printf '%s\\n'")
        environment = dict(os.environ, DOC_SOURCE=str(self.source))
        (self.source / 'specs').mkdir()

        def check():
            return subprocess.run(['sh', '-c', recipe], env=environment,
                                  capture_output=True, text=True, timeout=5)

        packaged = check()
        self.assertEqual(packaged.returncode, 0, packaged.stderr)
        self.assertIn('skipping README/release-notes/doxygen gates', packaged.stdout)
        for worktree in (True, False):
            with self.subTest(worktree=worktree):
                git = self.source / '.git'
                if worktree: git.write_text('gitdir: /registered/worktree')
                else: git.mkdir()
                checkout = check()
                self.assertEqual(checkout.returncode, 0, checkout.stderr)
                self.assertEqual(checkout.stdout.splitlines(),
                                 ['check-readme', 'check-release-notes', 'check-doxygen'])
                if worktree: git.unlink()
                else: git.rmdir()

    def test_receipts_require_positive_complete_non_skipped_counts(self):
        self.gate.verify_receipt(self.receipt, self.identity)
        for counts in ({}, {'tests': 0}, {'failures': 1}, {'skipped': 1}, {'successes': 0}):
            bad = copy.deepcopy(self.receipt)
            if counts: bad['counts'].update(counts)
            else: bad['counts'] = counts
            with self.subTest(counts=counts), self.assertRaises(ValueError):
                self.gate.verify_receipt(bad, self.identity)
        self.receipt['status'] = 'failed'
        with self.assertRaises(ValueError): self.gate.verify_receipt(self.receipt, self.identity)

    def test_receipt_identity_binds_archive_snapshot_source_tls_and_instrumentation(self):
        for field in self.identity:
            bad = copy.deepcopy(self.receipt)
            bad['identity'][field] = 'historical'
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.gate.verify_receipt(bad, self.identity)

    def test_changed_or_missing_executable_or_library_is_rejected(self):
        self.binary.write_text('different installed library')
        with self.assertRaises(ValueError): self.gate.verify_receipt(self.receipt, self.identity)
        self.binary.unlink()
        with self.assertRaises(ValueError): self.gate.verify_receipt(self.receipt, self.identity)

    def test_private_fixture_cannot_satisfy_public_listener_or_credentials_gate(self):
        self.gate.verify_receipt(self.receipt, self.identity, public=True)
        for change in ('private', 'missing-h3', 'missing-credentials'):
            bad = copy.deepcopy(self.receipt)
            if change == 'private': bad['kind'] = 'private engine'
            else: bad['witnesses'].remove(change.removeprefix('missing-'))
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.gate.verify_receipt(bad, self.identity, public=True)

    def test_build_library_equivalence_and_consumer_dependencies_are_enforced(self):
        build = self.directory / 'build'; prefix = self.directory / 'prefix'
        (build / 'src/.libs').mkdir(parents=True); (prefix / 'lib').mkdir(parents=True)
        for directory in (build / 'src/.libs', prefix / 'lib'):
            (directory / 'libhttpserver.so').write_text('same packaged library')
        self.gate.verify_library_files(build, prefix)
        (build / 'src/.libs/libhttpserver.so').write_text('borrowed build library')
        with self.assertRaises(ValueError): self.gate.verify_library_files(build, prefix)
        matrix = self.gate.load_runner('lib/v3_package_matrix.py')
        (prefix / 'include').mkdir()
        header = prefix / 'include/public.hpp'; header.write_text('public')
        matrix.check_dependencies([str(header)], prefix, ROOT, build, False)
        with self.assertRaises(ValueError):
            matrix.check_dependencies([str(ROOT / 'src/httpserver.hpp')], prefix, ROOT, build, False)
        audit = self.gate.load_runner('audit-v3-installed-package.py')
        graph = {'nodes': [{'id': 'consumer'}, {'id': '/usr/lib/libmicrohttpd.so'}],
                 'edges': [{'parent': 'consumer', 'name': '/usr/lib/libmicrohttpd.so', 'child': '/usr/lib/libmicrohttpd.so'}]}
        self.assertTrue(audit.graph_violations(graph, 'strict', 'yes', 'aggregate', audit.load_policy(), prefix))

    def test_automake_receipts_reject_empty_failed_and_unexpected_skips(self):
        valid = '# TOTAL: 5\n# PASS: 5\n# SKIP: 0\n# XFAIL: 0\n# FAIL: 0\n# XPASS: 0\n# ERROR: 0\n'
        self.assertEqual(self.gate.automake_counts(valid)['tests'], 5)
        for bad in ('', valid.replace('PASS: 5', 'PASS: 4'), valid.replace('SKIP: 0', 'SKIP: 1'),
                    valid.replace('FAIL: 0', 'FAIL: 1'), valid.replace('TOTAL: 5', 'TOTAL: 0')):
            with self.subTest(output=bad), self.assertRaises(ValueError): self.gate.automake_counts(bad)

    def test_automake_accepts_aligned_native_summary_without_loosening_counts(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        build = self.directory / 'native-build'; (build / 'test').mkdir(parents=True)
        text = '# TOTAL: 7\n# PASS:  5\n# SKIP:  2\n# XFAIL: 0\n# FAIL:  0\n# XPASS: 0\n# ERROR: 0\n'
        (build / 'test/test-suite.log').write_text(text)
        for name in ('io_iocp_backend', 'io_kqueue_backend'):
            (build / ('test/' + name + '.trs')).write_text(':test-result: SKIP\n')
        self.assertEqual(checks.automake_result(build, 'Linux')['tests'], 5)
        for bad in (text.replace('# FAIL:  0', '# FAIL:  1'), text + '# PASS:  5\n', text.replace('# PASS:  5', '# PASS:  0')):
            (build / 'test/test-suite.log').write_text(bad)
            with self.subTest(output=bad), self.assertRaises(ValueError): checks.automake_result(build, 'Linux')

    def test_archive_extraction_rejects_traversal_and_multiple_roots(self):
        import io
        import tarfile
        for names in (['../outside'], ['one/configure', 'two/configure']):
            archive = self.directory / 'bad.tar.gz'
            with tarfile.open(archive, 'w:gz') as output:
                for name in names:
                    item = tarfile.TarInfo(name); item.size = 1
                    output.addfile(item, io.BytesIO(b'x'))
            with self.subTest(names=names), self.assertRaises(ValueError):
                self.gate.extract_archive(archive, self.directory / ('extracted-' + str(len(names))))

    def test_tsan_skip_is_only_permitted_for_named_non_race_allocation_case(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        output = 'close_allocation_failure_settles_once_and_wakes_parked_receive [SKIP] sanitizer owns operator new; fault injection is covered in normal and ASan/UBSan lanes\n2 tests executed\n5 checks\n-> 5 successes\n-> 0 failures\n-> 1 skipped\n'
        self.assertEqual(checks.native_counts(ROOT, 'websocket_drain_race', output, tsan=True)['skipped'], 0)
        for name, text in (('http1_response_outbox', output), ('websocket_drain_race', output.replace('close_allocation_failure_settles_once_and_wakes_parked_receive', 'unknown'))):
            with self.subTest(suite=name), self.assertRaises(ValueError): checks.native_counts(ROOT, name, text, tsan=True)

    def test_main_missing_tools_retains_failed_manifest_and_all_required_gates(self):
        output = self.directory / 'attempt'
        code = self.gate.main(['--output', str(output), '--provider-prefix', '/missing/provider',
                               '--compiler', '/missing/compiler', '--cc', '/missing/cc', '--make', '/missing/make'])
        self.assertEqual(code, 1)
        manifest = self.gate.read_json(output / 'manifest.json')
        self.assertEqual({r['id'] for r in manifest['stages']}, set(self.gate.LOCAL_GATES))
        self.assertFalse(self.gate.local_passed(manifest))
        self.assertTrue(manifest['preparation']['failure'])

    def test_only_named_nonlocal_backend_skips_are_allowed_in_local_automake(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        build = self.directory / 'local-build'; (build / 'test').mkdir(parents=True)
        (build / 'test/test-suite.log').write_text('# TOTAL: 5\n# PASS: 3\n# SKIP: 2\n# XFAIL: 0\n# FAIL: 0\n# XPASS: 0\n# ERROR: 0\n')
        for name in ('io_iocp_backend', 'io_kqueue_backend'):
            (build / ('test/' + name + '.trs')).write_text(':test-result: SKIP\n')
        self.assertEqual(checks.automake_result(build, 'Linux')['tests'], 3)
        (build / 'test/private_gate.trs').write_text(':test-result: SKIP\n')
        with self.assertRaises(ValueError): checks.automake_result(build, 'Linux')

    def test_tag_check_rejects_historical_self_consistent_gate_identity(self):
        manifest = {'schema_version': 1, 'inventory': self.gate.inventory_document(),
            'release_artifact': self.artifact_identity, 'artifact_identity_path': str(self.identity_path),
            'artifacts': self.receipt['artifacts'],
            'stages': [], 'platforms': []}
        for name in self.gate.LOCAL_GATES + self.gate.PLATFORMS:
            identity = self.gate.expected_identity(manifest, name)
            row = {'id': name, 'status': 'passed', 'artifacts': self.receipt['artifacts'],
                   'identity': identity, 'receipt': dict(self.receipt, identity=identity, gate=name)}
            manifest['stages' if name in self.gate.LOCAL_GATES else 'platforms'].append(row)
        manifest['stages'][0]['identity'] = dict(manifest['stages'][0]['identity'], archive_sha256='historical')
        manifest['stages'][0]['receipt']['identity'] = manifest['stages'][0]['identity']
        with self.assertRaises(ValueError): self.gate.check_tag_ready(manifest)

    def test_distribution_permits_only_exact_config_status_fixture_newline(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        checkout = self.directory / 'checkout'; archive = self.directory / 'archive'
        for root in (checkout, archive):
            (root / 'test').mkdir(parents=True)
            (root / 'test/test_content_large').write_bytes(b'fixture')
            (root / 'configure.ac').write_bytes(b'configuration')
        snapshot = {name: self.gate.digest(checkout / name) for name in ('test/test_content_large', 'configure.ac')}
        (archive / 'test/test_content_large').write_bytes(b'fixture\n')
        transformations = checks.verify_snapshot_packaging(checkout, snapshot, archive)
        self.assertEqual(set(transformations), {'test/test_content_large'})
        (archive / 'test/test_content_large').write_bytes(b'changed fixture\n')
        with self.assertRaises(ValueError): checks.verify_snapshot_packaging(checkout, snapshot, archive)

    def test_instrumented_header_fuzz_targets_require_current_source_and_bounds(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        directory = self.directory / 'fuzz'; directory.mkdir()
        (directory / 'capability.log').write_text('Done 2 runs in 0 second(s)\n')
        (directory / 'environment.log').write_text('clang 18')
        target = directory / 'hpack'; (target / 'seeds').mkdir(parents=True)
        (target / 'seeds/seed').write_text('seed')
        for name in ('fuzz', 'build.log'): (target / name).write_text('built')
        recipe = '-std=c++20 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined ' + str(ROOT / 'test/fuzz/hpack_fuzz.cpp')
        (target / 'compile.sh').write_text(recipe)
        (target / 'reproduce.sh').write_text('-runs=2000 -max_total_time=30 -timeout=5 -rss_limit_mb=512 -seed=128')
        (target / 'run.log').write_text('Done 2000 runs in 1 second(s)\n')
        self.assertEqual(checks.validate_fuzz(ROOT, directory, ['hpack'])['tests'], 2000)
        (target / 'compile.sh').write_text(recipe + ' uninstrumented.a')
        with self.assertRaises(RuntimeError): checks.validate_fuzz(ROOT, directory, ['hpack'])

    def test_command_provenance_includes_duration_and_sanitizer_environment(self):
        output = self.directory / 'command'; output.mkdir()
        command = self.gate.Commands(output)
        environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1',
                           TSAN_OPTIONS='halt_on_error=1:suppressions=/selected/tsan.supp', V3_RELEASE_IDENTITY='/selected/identity.json')
        command([sys.executable, '-c', 'print("retained")'], self.directory, environment)
        record = command.records[0]
        self.assertGreater(record['duration_seconds'], 0)
        self.assertEqual(record['environment']['ASAN_OPTIONS'], 'detect_leaks=0')
        self.assertEqual(record['environment']['V3_RELEASE_IDENTITY'], '/selected/identity.json')

    def test_platform_import_requires_same_artifact_and_rejects_duplicates(self):
        manifest = {'release_artifact': self.artifact_identity,
                    'platforms': [{'id': p, 'status': 'pending'} for p in self.gate.PLATFORMS]}
        identity = self.gate.expected_identity(manifest, 'FreeBSD')
        row = {'id': 'FreeBSD', 'status': 'passed', 'archive_sha256': self.identity['archive_sha256'],
               'identity': identity, 'receipt': dict(self.receipt, identity=identity), 'artifacts': self.receipt['artifacts']}
        path = self.directory / 'bsd.json'; path.write_text(json.dumps(row))
        self.gate.import_platform_receipts(manifest, [path])
        self.assertEqual(manifest['platforms'][0]['status'], 'passed')
        with self.assertRaises(ValueError): self.gate.import_platform_receipts(manifest, [path, path])
        row['archive_sha256'] = 'historical'; path.write_text(json.dumps(row))
        with self.assertRaises(ValueError): self.gate.import_platform_receipts(manifest, [path])

    def test_positive_native_suite_cannot_bypass_packaged_library_equivalence(self):
        from types import SimpleNamespace
        from v3_release_execution import ReleaseAttempt
        args = SimpleNamespace(output=self.directory / 'attempt', compiler=sys.executable, cc=sys.executable,
                               make='/bin/true', provider_prefix=self.directory, asan_options='detect_leaks=0', jobs=1)
        attempt = ReleaseAttempt(args, self.gate, {'stages': []}); attempt.source = ROOT
        lane = args.output / 'package/evidence/tls-no'; build = lane / 'build'; prefix = lane / 'prefix'
        (build / 'src/.libs').mkdir(parents=True); (build / 'test').mkdir(); (prefix / 'lib').mkdir(parents=True)
        (build / 'config.h').write_text('/* native TLS disabled */')
        (build / 'src/Makefile').write_text('abs_top_srcdir = ' + str(ROOT) + '\n')
        (build / 'src/.libs/libhttpserver.so').write_text('different library')
        (prefix / 'lib/libhttpserver.so').write_text('installed library')
        program = build / 'test/native_case'
        program.write_text('#!' + sys.executable + '\nprint("1 tests executed\\n2 checks\\n-> 2 successes\\n-> 0 failures\\n-> 0 skipped")\n')
        program.chmod(0o755)
        logs = self.directory / 'suite-logs'; logs.mkdir()
        with self.assertRaises(ValueError): attempt.suites(self.gate.Commands(logs), ['native_case'], 'no')

    def test_tag_check_rejects_receipt_from_another_gate(self):
        manifest = {'schema_version': 1, 'inventory': self.gate.inventory_document(),
            'release_artifact': self.artifact_identity, 'artifact_identity_path': str(self.identity_path),
            'artifacts': self.receipt['artifacts'], 'stages': [], 'platforms': []}
        for name in self.gate.LOCAL_GATES + self.gate.PLATFORMS:
            identity = self.gate.expected_identity(manifest, name)
            row = {'id': name, 'status': 'passed', 'artifacts': self.receipt['artifacts'],
                   'identity': identity, 'receipt': dict(self.receipt, identity=identity, gate=name)}
            manifest['stages' if name in self.gate.LOCAL_GATES else 'platforms'].append(row)
        manifest['stages'][0]['receipt']['gate'] = 'parity'
        with self.assertRaises(ValueError): self.gate.check_tag_ready(manifest)

    def test_cxx_gate_bounds_each_native_test_before_waiting_for_aggregate(self):
        from types import SimpleNamespace
        from v3_release_execution import ReleaseAttempt
        make = self.directory / 'make-tool'
        make.write_text('#!' + sys.executable + '\nfrom pathlib import Path\n'
                        'Path("test/integ").mkdir(parents=True,exist_ok=True)\n'
                        'Path("test/integ/native_case.log").write_text("actual failed case witness")\n'
                        'Path("test/integ/native_case.trs").write_text(":test-result: FAIL\\n")\n'
                        'raise SystemExit(2)\n'); make.chmod(0o755)
        args = SimpleNamespace(output=self.directory / 'attempt', compiler=sys.executable, cc=sys.executable,
            make=str(make), provider_prefix=self.directory, asan_options='detect_leaks=0', jobs=1, test_timeout_tool='/usr/bin/timeout')
        attempt = ReleaseAttempt(args, self.gate, {'stages': []}); attempt.source = ROOT
        lane = args.output / 'package/evidence/tls-no'; build = lane / 'build'
        (build / 'src').mkdir(parents=True); (build / 'config.h').write_text('/* TLS off */')
        (build / 'src/Makefile').write_text('abs_top_srcdir = ' + str(ROOT) + '\n')
        logs = self.directory / 'build-logs'; logs.mkdir()
        command = self.gate.Commands(logs)
        with self.assertRaises(ValueError): attempt.cxx(command, 'no')
        self.assertIn('LOG_COMPILER=/usr/bin/timeout --kill-after=5s 180s', command.records[0]['argv'])
        self.assertEqual((logs / 'native-tests/integ/native_case.log').read_text(), 'actual failed case witness')
        self.assertEqual((logs / 'native-tests/integ/native_case.trs').read_text(), ':test-result: FAIL\n')

    def test_resource_plateau_receipt_requires_every_normal_allocator_window(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        lines = []
        for name in ('HTTP upload', 'HTTP output', 'WebSocket input', 'WebSocket output'):
            lines += ['CPU ' + name + ' sample=' + str(i) + ' cpu_seconds=0.001 wall_seconds=1.0' for i in range(3)]
            lines += ['PLATEAU ' + name + ' retained=100/100 RSS=1000000/1000000 slack=1048576']
        output = '\n'.join(lines) + '\n'
        self.assertEqual(checks.native_counts(ROOT, 'native_protocol_memory', output)['tests'], 16)
        for bad in (output.replace('sample=2', 'sample=1'), output.replace('cpu_seconds=0.001', 'cpu_seconds=0.3'),
                    output.replace('wall_seconds=1.0', 'wall_seconds=inf'),
                    output.replace('slack=1048576', 'slack=9999999'), output.replace('RSS=1000000/1000000', 'RSS=1000000/9000000')):
            with self.subTest(output=bad), self.assertRaises(ValueError): checks.native_counts(ROOT, 'native_protocol_memory', bad)

    def test_rotation_requires_installed_wire_cells_instead_of_private_or_usage_output(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        for output in ('private rotation: old=1 active=2 failed replacement preserved=2\n',
                       'usage: v3_tls_rotation certificate-directory managed|external port\n'):
            with self.assertRaises(ValueError): checks.native_counts(ROOT, 'v3_tls_rotation', output)
        scenario = dict(case='installed_rotation_example', old_certificate_sha256='a' * 64,
                        new_certificate_sha256='b' * 64, held_requests=3, fresh_verified_requests=2)
        build, prefix = self.directory / 'build', self.directory / 'prefix'
        receipt = dict(status='passed', tls='yes', source=str(self.source), build=str(build), prefix=str(prefix),
                      cells=[dict(linkage=linkage, loop=loop, scenarios=[scenario.copy()])
                      for linkage in ('shared', 'static') for loop in ('managed', 'external')])
        self.assertEqual(checks.rotation_counts(receipt, self.source, build, prefix)['checks'], 20)
        for change in ('missing', 'duplicate', 'no-replacement', 'no-held-request', 'failed', 'tls-off', 'source', 'build', 'prefix'):
            bad = copy.deepcopy(receipt)
            if change == 'missing': bad['cells'].pop()
            if change == 'duplicate': bad['cells'][-1] = copy.deepcopy(bad['cells'][0])
            if change == 'no-replacement': bad['cells'][0]['scenarios'][0]['new_certificate_sha256'] = 'a' * 64
            if change == 'no-held-request': bad['cells'][0]['scenarios'][0]['held_requests'] = 0
            if change == 'failed': bad['status'] = 'failed'
            if change == 'tls-off': bad['tls'] = 'no'
            if change in ('source', 'build', 'prefix'): bad[change] = '/another-release'
            with self.subTest(change=change), self.assertRaises(ValueError):
                checks.rotation_counts(bad, self.source, build, prefix)

    def test_http1_receipts_require_the_exact_parser_and_native_case_transcripts(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        rows = [line.split('\t') for line in (ROOT / 'test/conformance/http1/cases.tsv').read_text().splitlines()
                if line and not line.startswith('#')]
        for suite in ('http1_conformance', 'native_http1_conformance'):
            lines = [('PASS native ' + row[0]) if suite.startswith('native_')
                     else ('PASS ' + row[0] + ' RFC ' + row[1]) for row in rows]
            output = '\n'.join(lines) + '\n'
            with self.subTest(suite=suite):
                try:
                    actual = checks.native_counts(ROOT, suite, output)
                except ValueError as error:
                    self.fail('valid corpus transcript rejected: ' + str(error))
                self.assertEqual(actual['tests'], len(rows))
                for bad in ('\n'.join(lines[1:]), output + lines[0] + '\n',
                            output.replace(rows[0][0], 'unknown-case'), output.replace('PASS', 'FAIL', 1)):
                    with self.assertRaises(ValueError):
                        checks.native_counts(ROOT, suite, bad)

    def test_resource_slow_peer_receipt_uses_each_protocols_exact_resume_and_idle_bounds(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        window = dict(cpu_seconds=.001, wall_seconds=1, rss_before=1000000, rss_after=1000000)
        receipt = dict(normal_allocator_only=True, rss_slack=1048576, idle_core_fraction=.2)
        for name, size in (('h2', 131072), ('h3', 524288)):
            receipt[name] = dict(cycles=[{'idle': window} for _ in range(3)],
                                 uploads=[{'idle': window} for _ in range(3)], resumed_bytes=size)
        path = self.directory / 'receipt.json'; path.write_text(json.dumps(receipt))
        self.assertEqual(checks.validate_resources(self.directory, self.gate.read_json)['tests'], 12)
        for edit in ('resume', 'cpu', 'infinite', 'missing'):
            bad = copy.deepcopy(receipt)
            if edit == 'resume': bad['h2']['resumed_bytes'] = 524288
            if edit == 'cpu': bad['h2']['cycles'][0]['idle']['cpu_seconds'] = .3
            if edit == 'infinite': bad['h2']['cycles'][0]['idle']['wall_seconds'] = float('inf')
            if edit == 'missing': bad['h3']['uploads'].pop()
            path.write_text(json.dumps(bad))
            with self.subTest(edit=edit), self.assertRaises((ValueError, AssertionError)):
                checks.validate_resources(self.directory, self.gate.read_json)

    def run_stage(self, name, code, validator=None, timeout=2):
        output = self.directory / name
        return self.gate.execute_stage(self.manifest, output, name,
            lambda command: self.gate.command_receipt(command, [sys.executable, '-c', code],
                self.directory, os.environ.copy(), validator, timeout))

    def test_exit_zero_child_with_missing_malformed_empty_failed_receipt_fails(self):
        for value in (None, '{', '{}', '{"status":"failed"}'):
            self.manifest = {'stages': []}
            def validate(_):
                path = self.directory / 'child.json'
                self.gate.verify_receipt(self.gate.read_json(path), self.identity)
            code = 'pass' if value is None else 'from pathlib import Path; Path("child.json").write_text(' + repr(value) + ')'
            row = self.run_stage('child-' + str(len(str(value))), code, validate)
            self.assertEqual(row['status'], 'failed')
            (self.directory / 'child.json').unlink(missing_ok=True)

    def test_timeout_nonzero_and_missing_tool_retain_prior_success_and_logs(self):
        self.manifest = {'stages': []}
        self.run_stage('ok', 'print("success witness")')
        failed = self.run_stage('bad', 'print("failure witness"); raise SystemExit(9)')
        timeout = self.run_stage('slow', 'import time; print("started",flush=True); time.sleep(5)', timeout=.15)
        missing = self.gate.execute_stage(self.manifest, self.directory / 'missing', 'missing',
            lambda cmd: self.gate.command_receipt(cmd, ['/no/such/tool'], self.directory, os.environ.copy()))
        self.assertEqual([r['status'] for r in self.manifest['stages']], ['passed', 'failed', 'failed', 'failed'])
        self.assertEqual(failed['commands'][0]['exit'], 9)
        self.assertIn('deadline', timeout['failure'])
        self.assertLess(timeout['commands'][0]['exit'], 0)
        for row in (failed, timeout, missing):
            self.assertTrue(row['commands'][0]['argv'])
            self.assertTrue(Path(row['commands'][0]['log']).is_file())
            self.gate.verify_files(row['artifacts'])

    def test_existing_output_is_rejected_and_preserved(self):
        output = self.directory / 'old'
        output.mkdir(); (output / 'original').write_text('receipt')
        with self.assertRaises(ValueError): self.gate.create_output(output)
        self.assertEqual((output / 'original').read_text(), 'receipt')

    def test_retention_failure_cannot_leave_a_stage_passed(self):
        manifest = {'stages': []}
        def execute(command):
            original = self.gate.artifact(self.binary)
            self.binary.write_text('changed between receipt and retention')
            return {'receipt': dict(self.receipt, artifacts=[original])}
        row = self.gate.execute_stage(manifest, self.directory / 'retention-failure',
                                      'package', execute, retain=True)
        self.assertEqual(row['status'], 'failed')
        self.assertIn('artifact changed while retaining evidence', row['failure'])

    def test_retention_rejects_conflicting_hashes_for_the_same_original_path(self):
        manifest = {'stages': []}
        original = self.gate.artifact(self.binary)
        self.binary.write_text('later bytes at the same path')
        current = self.gate.artifact(self.binary)
        row = self.gate.execute_stage(manifest, self.directory / 'conflicting-evidence',
            'package', lambda command: {'artifacts': [original, current]}, retain=True)
        self.assertEqual(row['status'], 'failed')
        self.assertIn('conflicting artifact records', row['failure'])

    def test_failed_stage_retains_its_original_command_log(self):
        manifest = {'stages': []}
        directory = self.directory / 'failed-stage'
        row = self.gate.execute_stage(manifest, directory, 'package',
            lambda command: self.gate.command_receipt(command,
                [sys.executable, '-c', 'print("failure witness"); raise SystemExit(9)'],
                self.directory, os.environ.copy()), retain=True)
        self.assertEqual(row['status'], 'failed')
        record = row['commands'][0]
        self.assertEqual(record['exit'], 9)
        self.assertNotEqual(record['execution_log'], record['log'])
        Path(record['execution_log']).write_text('later execution changed working log')
        self.assertIn('failure witness', Path(record['log']).read_text())
        self.gate.verify_files(row['artifacts'])

    def test_pending_platforms_preserve_local_pass_but_block_tag(self):
        manifest = self.complete_manifest()
        self.gate.check_tag_ready(manifest)
        for platforms in ([], [{'id': name, 'status': 'pending'} for name in self.gate.PLATFORMS]):
            with self.subTest(platforms=platforms):
                pending = dict(manifest, platforms=platforms)
                self.assertTrue(self.gate.local_passed(pending))
                with self.assertRaisesRegex(ValueError, 'required platform evidence pending or failed'):
                    self.gate.check_tag_ready(pending)

    def complete_manifest(self):
        manifest = {'schema_version': 1, 'inventory': self.gate.inventory_document(),
                    'release_artifact': self.artifact_identity, 'artifact_identity_path': str(self.identity_path),
                    'stages': [{'id': name, 'status': 'passed', 'artifacts': self.receipt['artifacts']}
                               for name in self.gate.LOCAL_GATES],
                    'platforms': [{'id': name, 'status': 'passed', 'artifacts': self.receipt['artifacts']}
                                  for name in self.gate.PLATFORMS],
                    'artifacts': self.receipt['artifacts']}
        # Gate envelopes must be verified again, not just aggregate statuses.
        for row in manifest['stages'] + manifest['platforms']:
            identity = self.gate.expected_identity(manifest, row['id'])
            receipt = dict(self.receipt, identity=identity, gate=row['id'])
            row.update(receipt=receipt, identity=identity)
        return manifest

    def test_complete_verified_inventory_passes_tag_and_artifact_change_fails(self):
        manifest = self.complete_manifest()
        tool = self.directory / 'compiler'; tool.write_text('selected compiler')
        manifest['tools'] = {'compiler': {'binary': self.gate.artifact(tool)}}
        self.gate.check_tag_ready(manifest)
        tool.write_text('changed compiler')
        with self.assertRaises(ValueError): self.gate.check_tag_ready(manifest)
        tool.write_text('selected compiler')
        self.binary.write_text('changed after release gates')
        with self.assertRaises(ValueError): self.gate.check_tag_ready(manifest)

    def packaged_attempt(self):
        from types import SimpleNamespace
        from v3_release_execution import ReleaseAttempt
        output = Path(tempfile.mkdtemp(dir=self.directory))
        args = SimpleNamespace(output=output, compiler=sys.executable, cc=sys.executable,
            make='/bin/true', provider_prefix=self.directory, asan_options='detect_leaks=1', jobs=1,
            python=sys.executable, node='/selected/node', go='/selected/go')
        manifest = {'stages': [], 'source': self.artifact_identity['source'],
                    'release_artifact': self.artifact_identity}
        attempt = ReleaseAttempt(args, self.gate, manifest); attempt.source = self.source
        attempt.env['V3_RELEASE_IDENTITY'] = str(self.identity_path)
        external = self.directory / 'selected-client-module'
        external.write_text('selected independent client source')
        attempt.env['RELEASE_CONTRACT_CLIENT_INPUT'] = str(external)
        scripts = self.source / 'scripts'; scripts.mkdir(exist_ok=True)
        go = self.directory / 'go-fixture'
        go.write_text('#!' + sys.executable + '\nimport sys\nfrom pathlib import Path\n'
                      'Path(sys.argv[sys.argv.index("-o")+1]).write_text("fixture executable")\n')
        go.chmod(0o755); args.go = str(go)
        (scripts / 'run-v3-packaged-adapters.py').write_text("""import argparse,hashlib,json,os
from pathlib import Path
p=argparse.ArgumentParser()
for key in ('family','archive','archive-sha256','source','build','prefix','provider-prefix',
            'tls','compiler','client-python','node','quic-go','run-id','output','validate'):
    p.add_argument('--'+key)
p.add_argument('--require-full-workload',action='store_true')
a=p.parse_args()
assert len(a.run_id)==32 and a.source and a.archive and a.archive_sha256
assert a.client_python and a.node and a.quic_go
if a.family=='native-throughput-latency': assert a.require_full_workload
out=Path(a.output)
if a.validate:
    r=json.loads(Path(a.validate).read_text())
    assert r['run_id']==a.run_id and r['tls']==a.tls
    assert not Path(a.source,'reject-validation').exists(), 'independent validation rejected'
else:
    out.mkdir(parents=True)
    child=out/'child.json';child.write_text(json.dumps({'cells':[{'case':'contract'}]}))
    external=Path(os.environ['RELEASE_CONTRACT_CLIENT_INPUT'])
    record={'path':str(external),'bytes':external.stat().st_size,'sha256':hashlib.sha256(external.read_bytes()).hexdigest()}
    (out/'receipt.json').write_text(json.dumps({'run_id':a.run_id,'tls':a.tls,
        'children':{'contract':{'path':str(child)}},'artifacts':[record]}))
""")
        for mode in ('no', 'yes'):
            build = output / ('package/evidence/tls-' + mode + '/build')
            (build / 'src').mkdir(parents=True)
            (build / 'config.h').write_text('#define NATIVE_V3_TLS 1' if mode == 'yes' else '/* off */')
            (build / 'src/Makefile').write_text('abs_top_srcdir = ' + str(self.source) + '\n')
        return attempt

    def test_packaged_families_execute_both_tls_lanes_and_independent_full_validation(self):
        from unittest.mock import patch
        for family in ('public-capabilities', 'native-throughput-latency'):
            with self.subTest(family=family):
                attempt = self.packaged_attempt()
                stage = attempt.output / family; stage.mkdir()
                commands = self.gate.Commands(stage)
                with patch.object(attempt, 'linkage', return_value=[self.gate.artifact(self.binary)]):
                    result = attempt.packaged_family(commands, family)
                self.assertEqual(result['counts']['tests'], 2)
                self.assertIn(self.gate.artifact(self.directory / 'selected-client-module'), result['artifacts'])
                invocations = [r['argv'] for r in commands.records if '--family' in r['argv']]
                self.assertEqual(len(invocations), 4)
                ids = [r[r.index('--run-id') + 1] for r in invocations]
                self.assertEqual(ids[0], ids[1]); self.assertEqual(ids[2], ids[3])
                self.assertNotEqual(ids[0], ids[2])
                if family == 'public-capabilities':
                    self.assertEqual(result['kind'], 'installed public capability')
                    self.assertEqual(set(result['witnesses']), {'tls', 'h2', 'h3', 'credentials'})
                else:
                    self.assertTrue(all('--require-full-workload' in r for r in invocations))
                import shutil
                shutil.rmtree(attempt.output); shutil.rmtree(self.source / 'scripts')

    def test_packaged_family_cannot_pass_when_independent_validation_rejects(self):
        from unittest.mock import patch
        attempt = self.packaged_attempt()
        (self.source / 'reject-validation').write_text('reject fixture')
        stage = attempt.output / 'public-capabilities'; stage.mkdir()
        commands = self.gate.Commands(stage)
        with patch.object(attempt, 'linkage', return_value=[self.gate.artifact(self.binary)]):
            with self.assertRaisesRegex(ValueError, 'command exited'):
                attempt.packaged_family(commands, 'public-capabilities')
        self.assertNotEqual(commands.records[-1]['exit'], 0)
        self.assertIn('independent validation rejected', Path(commands.records[-1]['log']).read_text())

    def test_package_selects_the_pinned_clients_for_nested_public_checks(self):
        from unittest.mock import patch
        from types import SimpleNamespace
        from v3_release_execution import ReleaseAttempt
        import v3_release_checks as checks
        args = SimpleNamespace(output=self.directory, compiler='cxx', cc='cc', make='make',
            provider_prefix=self.directory, asan_options='detect_leaks=1', jobs=1,
            python='/selected/python', node='/selected/node', pkg_config_dir=[], prerequisite_prefix=[])
        attempt = ReleaseAttempt(args, self.gate, {}); attempt.source = self.source
        with patch.object(attempt, 'command') as command, patch.object(checks, 'validate_package', return_value={}):
            attempt.package(None)
        argv = command.call_args.args[1]
        self.assertEqual(argv[argv.index('--client-python') + 1], '/selected/python')
        self.assertEqual(argv[argv.index('--node') + 1], '/selected/node')
        self.assertIn('--optimized', argv)

    def test_pending_platforms_do_not_hide_corrupted_local_release_evidence(self):
        manifest = self.complete_manifest()
        executable = self.directory / 'local-executable'; executable.write_text('selected bytes')
        record = self.gate.artifact(executable)
        manifest['stages'][0]['artifacts'] = [record]
        manifest['stages'][0]['receipt']['artifacts'] = [record]
        manifest['platforms'] = []
        executable.write_text('changed after successful gates')
        with self.assertRaisesRegex(ValueError, 'changed artifact'):
            self.gate.check_tag_ready(manifest)

    def test_expanded_package_audit_is_readable_with_a_bounded_json_limit(self):
        path = self.directory / 'audit.json'
        path.write_text('{"status":"passed","declarations":"' + 'x' * (40 * 1024 * 1024) + '"}')
        self.assertEqual(self.gate.read_json(path)['status'], 'passed')
        with path.open('wb') as stream: stream.truncate(self.gate.MAX_JSON + 1)
        with self.assertRaisesRegex(ValueError, 'oversized receipt'): self.gate.read_json(path)

    def test_isolated_public_execution_preserves_exact_client_arguments(self):
        from unittest.mock import patch
        attempt = self.packaged_attempt(); attempt.args.isolated_public_network = True
        argv = [sys.executable, '/selected/public-client.py', '--output', '/selected/output space']
        with patch.object(attempt, 'command') as command:
            attempt.public_command(None, argv, timeout=17)
        wrapped = command.call_args.args[1]
        self.assertEqual(wrapped[:3], ['unshare', '-Urn', 'sh'])
        self.assertEqual(wrapped[-len(argv):], argv)
        self.assertEqual(command.call_args.kwargs['timeout'], 17)

    def test_h3_passes_the_explicit_simulator_clock_receipt(self):
        from unittest.mock import patch
        from types import SimpleNamespace
        from v3_release_execution import ReleaseAttempt
        args = SimpleNamespace(output=self.directory, compiler='cxx', cc='cc', make='make',
            provider_prefix=self.directory, asan_options='detect_leaks=1', jobs=1,
            python='python', go='go', tshark='tshark', runner_python='python', runner_dir='runner',
            server_image='image', docker_direct_routing_compat=True,
            simulator_clock_receipt='/selected/clock.json')
        attempt = ReleaseAttempt(args, self.gate, {}); attempt.source = self.source
        with patch.object(attempt, 'normal_build', return_value=(self.directory, self.directory)), \
             patch.object(attempt, 'command', side_effect=ValueError('captured')) as command:
            with self.assertRaisesRegex(ValueError, 'captured'): attempt.h3(None)
        argv = command.call_args.args[1]
        self.assertEqual(argv[argv.index('--simulator-clock-receipt') + 1], '/selected/clock.json')

    def test_required_asan_execution_rejects_disabled_leak_detection(self):
        from unittest.mock import patch
        from types import SimpleNamespace
        from v3_release_execution import ReleaseAttempt
        args = SimpleNamespace(output=self.directory, compiler='cxx', cc='cc', make='make',
            provider_prefix=self.directory, asan_options='detect_leaks=0', jobs=1)
        attempt = ReleaseAttempt(args, self.gate, {}); attempt.source = self.source
        (self.directory / 'asan').mkdir()
        with patch.object(attempt, 'command', side_effect=ValueError('unexpected configure')):
            with self.assertRaisesRegex(ValueError, 'required leak and stack checking'):
                attempt.sanitized(None, 'asan', 'no', 'address,undefined')

    def test_tsan_timer_allocation_hook_has_only_its_named_existing_exclusion(self):
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        output = ('[SKIP] published_waiter_can_die_during_timeout_registration - sanitizer owns operator new; '
                  'timer-allocation reclaim hook runs in normal and ASan/UBSan lanes\n'
                  '4 tests executed\n1800 checks\n-> 1800 successes\n-> 0 failures\n-> 1 skipped\n')
        self.assertEqual(checks.native_counts(ROOT, 'task_resume_signal_lifetime', output, True)['checks'], 1800)
        for bad in (output.replace('published_waiter_can_die_during_timeout_registration', 'unknown_case'),
                    output.replace('-> 1 skipped', '-> 2 skipped'), output.replace('timer-allocation reclaim hook', 'unknown')):
            with self.subTest(output=bad), self.assertRaises(ValueError):
                checks.native_counts(ROOT, 'task_resume_signal_lifetime', bad, True)

    def test_package_receipt_survives_later_native_build_and_check(self):
        from types import SimpleNamespace
        from v3_release_execution import ReleaseAttempt
        output = self.directory / 'attempt'; output.mkdir()
        args = SimpleNamespace(output=output, compiler=sys.executable, cc=sys.executable,
            make='/bin/true', provider_prefix=self.directory, asan_options='detect_leaks=0', jobs=1)
        manifest = {'stages': [], 'source': self.artifact_identity['source'],
                    'release_artifact': self.artifact_identity}
        attempt = ReleaseAttempt(args, self.gate, manifest); attempt.source = self.source
        attempt.env['V3_RELEASE_IDENTITY'] = str(self.identity_path)
        build = output / 'package/evidence/tls-no/build'
        def package(command):
            (build / 'src').mkdir(parents=True); (build / 'test/.deps').mkdir(parents=True)
            (build / 'config.h').write_text('/* TLS off */')
            (build / 'src/Makefile').write_text('abs_top_srcdir = ' + str(self.source) + '\n')
            (build / 'test/.deps/case.Po').write_text('stub')
            return {'counts': copy.deepcopy(self.receipt['counts'])}
        row = attempt.stage('package', package)
        self.assertEqual(row['status'], 'passed')
        make = self.directory / 'native-make'
        make.write_text('#!' + sys.executable + '\nfrom pathlib import Path\n'
            'Path("test/.deps/case.Po").write_text("compiled dependencies")\n'
            'Path("test/test-suite.log").write_text("# TOTAL: 1\\n# PASS: 1\\n# SKIP: 0\\n# XFAIL: 0\\n# FAIL: 0\\n# XPASS: 0\\n# ERROR: 0\\n")\n')
        make.chmod(0o755); args.make = str(make); args.test_timeout_tool = '/usr/bin/timeout'
        logs = output / 'later-native'; logs.mkdir()
        native, _ = attempt.normal_build('no')
        attempt.native_check(self.gate.Commands(logs), native, attempt.env, 10)
        checks = self.gate.load_runner('lib/v3_release_checks.py')
        self.assertEqual(checks.automake_result(native, 'Linux')['tests'], 1)
        self.assertEqual((build / 'test/.deps/case.Po').read_text(), 'compiled dependencies')
        self.gate.verify_receipt(row['receipt'], row['identity'])
        self.gate.verify_files(row['artifacts'])
        frozen = next(record for record in row['receipt']['artifacts'] if record['path'].endswith('case.Po'))
        self.assertEqual(Path(frozen['path']).read_text(), 'stub')
        Path(frozen['path']).write_text('tampered evidence')
        with self.assertRaisesRegex(ValueError, 'changed artifact'):
            self.gate.verify_receipt(row['receipt'], row['identity'])


if __name__ == '__main__':
    unittest.main()
