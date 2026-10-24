#!/usr/bin/env python3
"""Archive provenance contracts, independent of Git, compilers and providers."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / 'scripts/lib/release_artifact.py'


class ArchiveContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if HELPER.is_file():
            spec = importlib.util.spec_from_file_location('artifact_identity', HELPER)
            cls.helper = importlib.util.module_from_spec(spec); spec.loader.exec_module(cls.helper)

    def setUp(self):
        self.assertTrue(HELPER.is_file(), 'verified release-artifact identity helper missing')
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name).resolve()
        self.source = self.directory / 'source'; self.source.mkdir()
        (self.source / 'configure.ac').write_text('packaged source')
        self.archive = self.directory / 'release.tar.gz'; self.archive.write_bytes(b'archive identity')
        self.path = self.directory / 'identity.json'
        self.document = {'schema_version': 1, 'source_root': str(self.source),
                         'source': {'head': 'd' * 40, 'snapshot_sha256': 'e' * 64},
                         'archive': {'path': str(self.archive), 'sha256': hashlib.sha256(self.archive.read_bytes()).hexdigest()},
                         'files_sha256': {'configure.ac': hashlib.sha256((self.source / 'configure.ac').read_bytes()).hexdigest()},
                         'tooling_bundle': {}}
        self.path.write_text(json.dumps(self.document))

    def test_explicit_identity_works_without_git_and_retains_real_revision(self):
        with patch.dict(os.environ, {'V3_RELEASE_IDENTITY': str(self.path)}):
            self.assertEqual(self.helper.source_identity(self.source)['head'], 'd' * 40)
            self.assertEqual(self.helper.revision(self.source), 'd' * 40)
            self.assertIn(b'archive', self.helper.working_diff(self.source))

    def test_archive_source_or_tooling_change_is_rejected(self):
        for changed in ('archive', 'source', 'bundle'):
            with self.subTest(changed=changed), patch.dict(os.environ, {'V3_RELEASE_IDENTITY': str(self.path)}):
                if changed == 'archive': self.archive.write_bytes(b'different archive')
                if changed == 'source': (self.source / 'configure.ac').write_text('different source')
                if changed == 'bundle':
                    self.document['tooling_bundle'] = {'specs/source.md': 'f' * 64}
                    self.path.write_text(json.dumps(self.document))
                with self.assertRaises(ValueError): self.helper.source_identity(self.source)
                self.archive.write_bytes(b'archive identity'); (self.source / 'configure.ac').write_text('packaged source')

    def test_wrong_root_unexpected_file_and_traversal_identity_are_rejected(self):
        with patch.dict(os.environ, {'V3_RELEASE_IDENTITY': str(self.path)}):
            with self.assertRaises(ValueError): self.helper.source_identity(self.directory)
            (self.source / 'unrecorded').write_text('unexpected source')
            with self.assertRaises(ValueError): self.helper.source_identity(self.source)
            (self.source / 'unrecorded').unlink()
            self.document['files_sha256']['../outside'] = 'f' * 64
            self.path.write_text(json.dumps(self.document))
            with self.assertRaises(ValueError): self.helper.source_identity(self.source)

    def test_parity_archive_receipts_can_live_outside_immutable_source(self):
        spec = importlib.util.spec_from_file_location('parity', ROOT / 'scripts/run-v3-behavior-parity.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        self.assertTrue(hasattr(module, 'receipt_directory'), 'archive parity directory adapter missing')
        outside = self.directory / 'parity-evidence'
        with patch.dict(os.environ, {'V3_RELEASE_IDENTITY': str(self.path)}):
            self.assertEqual(module.receipt_directory(self.source, outside), outside)
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaises(ValueError): module.receipt_directory(self.source, outside)
        self.assertFalse(outside.exists())

    def test_explicit_identity_path_does_not_require_process_global_environment(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertEqual(self.helper.verified(self.source, self.path), self.document)

    def test_http1_archive_runner_records_verified_snapshot_and_rejects_changed_source(self):
        for name in ('run-v3-http1-websocket-gates.sh', 'lib/release_artifact.py'):
            destination = self.source / 'scripts' / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / 'scripts' / name, destination)
        self.document['files_sha256'] = self.helper.source_files(self.source)
        self.path.write_text(json.dumps(self.document))
        build = self.directory / 'build'; (build / 'test').mkdir(parents=True)
        program = build / 'test/probe'; program.write_text('#!/bin/sh\nexit 0\n'); program.chmod(0o755)
        runner = self.source / 'scripts/run-v3-http1-websocket-gates.sh'
        argv = ['bash', str(runner), '--build-dir', str(build), '--tests', 'probe', '--compiler', sys.executable]
        with patch.dict(os.environ, {'V3_RELEASE_IDENTITY': str(self.path)}):
            result = subprocess.run(argv, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('d' * 40, (build / 'protocol-gate-logs/environment.log').read_text())
            (self.source / 'configure.ac').write_text('modified source')
            result = subprocess.run(argv, capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)

    def test_distribution_adapters_accept_verified_identity_instead_of_git(self):
        for name in ('check-v3-installed-consumer.py', 'run-v3-http3-full-gates.py',
                     'run-v3-http3-gates.py', 'run-v3-http3-websocket-gates.py', 'run-v3-quic-interop.py'):
            spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / 'scripts' / name)
            module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
            with self.subTest(runner=name), patch.dict(os.environ, {'V3_RELEASE_IDENTITY': str(self.path)}):
                self.assertTrue(hasattr(module, 'release_identity'), 'archive runner still requires Git: ' + name)
                self.assertEqual(module.release_identity.revision(self.source), 'd' * 40)
        for name in ('run-v3-protocol-fuzz.sh',):
            self.assertIn('release_artifact.py', (ROOT / 'scripts' / name).read_text())


class CanonicalFixtureContracts(unittest.TestCase):
    def check_fixture(self, script, *cases):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            target = root / 'native-tmp'; target.mkdir()
            alias = root / 'tmp'; alias.symlink_to(target, target_is_directory=True)
            self.assertNotEqual(alias, alias.resolve())
            result = subprocess.run([sys.executable, str(ROOT / script), *cases],
                                    env=dict(os.environ, TMPDIR=str(alias), TEMP=str(alias), TMP=str(alias)),
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_archive_parity_through_symlinked_tmpdir(self):
        self.check_fixture('test/integ/release_artifact_test.py', 'ArchiveContracts')

    def test_release_execution_through_symlinked_tmpdir(self):
        self.check_fixture('test/integ/release_gate_runner_test.py',
            'ReleaseContracts.test_packaged_families_execute_both_tls_lanes_and_independent_full_validation',
            'ReleaseContracts.test_packaged_family_cannot_pass_when_independent_validation_rejects',
            'ReleaseContracts.test_failed_stage_retains_its_original_command_log',
            'ReleaseContracts.test_package_receipt_survives_later_native_build_and_check',
            'ReleaseContracts.test_cxx_gate_bounds_each_native_test_before_waiting_for_aggregate')

    def test_benchmark_provenance_through_symlinked_tmpdir(self):
        self.check_fixture('scripts/test-v3-native-benchmark.py', 'ReceiptTest')

    def test_public_compile_and_receipts_through_symlinked_tmpdir(self):
        self.check_fixture('scripts/test-v3-packaged-adapters.py',
            'AdapterContracts.test_real_public_compile_artifacts_are_rechecked',
            'AdapterContracts.test_unused_selected_static_archive_cannot_supply_link_identity',
            'AdapterContracts.test_h1_requires_exact_versions_and_two_persistent_pipeline_responses',
            'AdapterContracts.test_unsafe_duplicate_and_symlink_archive_members_are_rejected')

    def test_selected_windows_shell_through_symlinked_tmpdir(self):
        self.check_fixture('test/integ/release_ci_test.py', 'RequestShellContracts')

    def test_platform_origin_and_tool_aliases_through_symlinked_tmpdir(self):
        self.check_fixture('test/integ/release_platform_bundle_test.py', 'PlatformBundles')


if __name__ == '__main__': unittest.main()
