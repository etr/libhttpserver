#!/usr/bin/env python3
"""Dependency-free contracts for the explicit full HTTP/3 release gate."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/run-v3-http3-full-gates.py'


class FullGateContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not SCRIPT.is_file():
            return
        spec = importlib.util.spec_from_file_location('full_gate', SCRIPT)
        cls.gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.gate)

    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), 'full HTTP/3 conformance runner missing')
        self.inventory = json.loads((ROOT / 'test/conformance/http3/full-gates.json').read_text())

    def test_inventory_covers_required_families_categories_and_stages(self):
        self.gate.validate_inventory(self.inventory)
        for category in ('framing', 'settings', 'critical-streams', 'semantics', 'trailers',
                         'cancellation', 'goaway', 'qpack-codecs', 'qpack-dynamic',
                         'qpack-blocking', 'qpack-limits', 'ws-negotiation', 'ws-tunnels',
                         'malformed', 'slow-peers', 'transport', 'fuzz'):
            with self.subTest(category=category):
                bad = copy.deepcopy(self.inventory)
                bad['coverage'] = [c for c in bad['coverage'] if c['id'] != category]
                with self.assertRaises(self.gate.GateFailure):
                    self.gate.validate_inventory(bad)

    def test_inventory_rejects_duplicate_unknown_or_untraceable_evidence(self):
        for field, value in [('rfc', '9999'), ('stage', 'unknown'), ('suite', 'unknown'),
                             ('source', 'missing.cpp'), ('requirements', []), ('sections', '')]:
            with self.subTest(field=field):
                bad = copy.deepcopy(self.inventory)
                bad['coverage'][0][field] = value
                with self.assertRaises(self.gate.GateFailure):
                    self.gate.validate_inventory(bad)
        bad = copy.deepcopy(self.inventory)
        bad['coverage'].append(copy.deepcopy(bad['coverage'][0]))
        with self.assertRaises(self.gate.GateFailure):
            self.gate.validate_inventory(bad)

    def test_exclusions_cannot_hide_required_gates(self):
        for exclusion in [{'id': 'settings', 'reason': 'not tested'}, {'id': 'optional', 'reason': ''}]:
            bad = copy.deepcopy(self.inventory)
            bad['exclusions'].append(exclusion)
            with self.assertRaises(self.gate.GateFailure):
                self.gate.validate_inventory(bad)
        bad = copy.deepcopy(self.inventory)
        bad['quic_exclusions']['closure'] = 'transfer alone is enough'
        with self.assertRaises(self.gate.GateFailure):
            self.gate.validate_inventory(bad)

    def test_transport_evidence_cannot_satisfy_http3(self):
        bad = copy.deepcopy(self.inventory)
        bad['coverage'][0].update(stage='quic-interop', suite='task173')
        with self.assertRaises(self.gate.GateFailure):
            self.gate.validate_inventory(bad)

    def test_native_summary_rejects_failed_skipped_empty_or_malformed(self):
        valid = '3 tests executed\n4 checks\n-> 4 successes\n-> 0 failures\n-> 0 skipped\n'
        self.assertEqual(self.gate.native_summary(valid)['tests'], 3)
        for bad in ['', valid.replace('3 tests', '0 tests'), valid.replace('4 successes', '0 successes'),
                    valid.replace('0 failures', '1 failures'), valid.replace('0 skipped', '1 skipped'),
                    valid + valid]:
            with self.subTest(output=bad), self.assertRaises(self.gate.GateFailure):
                self.gate.native_summary(bad)

    def test_artifact_hashes_are_verified_and_reads_bounded(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'receipt.json'
            path.write_text('{}')
            record = self.gate.artifact(path)
            self.gate.verify_artifacts([record])
            path.write_text('{"forged": true}')
            with self.assertRaises(self.gate.GateFailure):
                self.gate.verify_artifacts([record])
            with self.assertRaises(self.gate.GateFailure):
                self.gate.read_json(path, limit=2)
            path.unlink()
            with self.assertRaises(self.gate.GateFailure):
                self.gate.verify_artifacts([record])

    def test_output_directory_is_fresh_and_private(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'evidence'
            self.gate.create_output(path)
            self.assertEqual(path.stat().st_mode & 0o777, 0o700)
            original = path / 'original'
            original.write_text('retain me')
            with self.assertRaises(self.gate.GateFailure):
                self.gate.create_output(path)
            self.assertEqual(original.read_text(), 'retain me')

    def test_process_failure_timeout_and_output_overflow_are_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            for index, (code, deadline, limit) in enumerate([
                ('raise SystemExit(77)', 2, 4096), ('raise SystemExit(1)', 2, 4096),
                ('import time; time.sleep(3)', .05, 4096), ('print("x" * 9000)', 2, 100)]):
                row = self.gate.run_process([sys.executable, '-c', code], ROOT,
                                            Path(tmp) / str(index), deadline, limit)
                self.assertEqual(row['status'], 'failed')
                self.assertTrue(row['failure'])
                self.assertFalse(row.get('running', False))

    def test_successful_process_does_not_mean_successful_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            row = self.gate.run_process([sys.executable, '-c', 'pass'], ROOT, Path(tmp) / 'empty', 2)
            self.assertEqual(row['returncode'], 0)
            with self.assertRaises(self.gate.GateFailure):
                self.gate.native_summary(Path(row['stdout']).read_text())

    def test_aggregate_requires_every_local_stage_and_defers_nonlocal(self):
        rows = [{'id': stage, 'status': 'passed', 'artifacts': []} for stage in self.gate.STAGES]
        self.gate.accept_stages(rows)
        for status in ('failed', 'skipped', 'deferred', None):
            bad = copy.deepcopy(rows)
            bad[0]['status'] = status
            with self.assertRaises(self.gate.GateFailure):
                self.gate.accept_stages(bad)
        with self.assertRaises(self.gate.GateFailure):
            self.gate.accept_stages(rows[1:])
        with self.assertRaises(self.gate.GateFailure):
            self.gate.accept_stages(rows + [rows[0]])
        self.assertTrue(all(r['status'] == 'deferred' for r in self.gate.NONLOCAL))

    def test_network_rejects_wrong_matrix_missing_clients_failed_cleanup_and_identity(self):
        for stage, receipt in [
            ('quic-interop', {'passed': True, 'matrix': {}, 'cleanup_returncode': 1}),
            ('http3-interop', {'status': 'passed', 'matrix': 'smoke', 'matrices': {}}),
            ('http3-interop', {'status': 'passed', 'matrix': 'task174', 'matrices': {'aioquic': {}}}),
            ('websocket-interop', {'status': 'passed', 'matrix': 'hq-interop'}),
            ('websocket-interop', {'status': 'passed', 'matrix': 'websocket', 'tool': {'version': '0'}})]:
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp)
                (path / 'manifest.json').write_text(json.dumps(receipt))
                with self.subTest(stage=stage), self.assertRaises(self.gate.GateFailure):
                    self.gate.validate_network(stage, path, {})

    def test_network_forwards_explicit_simulator_clock_provenance(self):
        args = types.SimpleNamespace(runner_python="python", runner_dir="runner", server_image="endpoint",
                                     docker_direct_routing_compat=False, simulator_clock_receipt="clock.json")
        with tempfile.TemporaryDirectory() as temp:
            with patch.object(self.gate, "checked", return_value={}) as checked, patch.object(self.gate, "validate_network", return_value={}):
                self.gate.network_stage("quic-interop", args, Path(temp) / "stage", {})
                command = checked.call_args.args[0]
                self.assertEqual(command[command.index("--simulator-clock-receipt") + 1], "clock.json")

    def test_network_child_success_requires_manifest_and_artifacts(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(self.gate.GateFailure):
                self.gate.validate_network('http3-interop', Path(tmp), {})

    def test_native_runner_rejects_missing_suite_nonzero_and_empty_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / 'build'
            (build / 'test').mkdir(parents=True)
            for index, script in enumerate(['exit 77', 'exit 1', 'exit 0', 'echo "0 tests executed"']):
                suite = build / 'test/http3_frame'
                suite.write_text('#!/bin/sh\n' + script + '\n')
                suite.chmod(0o700)
                with self.assertRaises(self.gate.GateFailure):
                    self.gate.run_native_suites(build, ['http3_frame'], Path(tmp) / str(index))
            with self.assertRaises(self.gate.GateFailure):
                self.gate.run_native_suites(build, ['missing'], Path(tmp) / 'missing')

    def test_fuzz_requires_native_instrumentation_capability_bounds_and_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = root / 'qpack'
            target.mkdir()
            (root / 'capability.log').write_text('Done 1 runs in 0 second(s)')
            (root / 'environment.log').write_text('clang 18')
            (target / 'fuzz').write_bytes(b'binary')
            (target / 'build.log').write_text('')
            (target / 'compile.sh').write_text('-std=c++20 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined ' + str(ROOT / 'src/detail/http3_qpack.cpp'))
            (target / 'reproduce.sh').write_text('-runs=2000 -max_total_time=30 -timeout=5 -rss_limit_mb=512 -seed=128')
            (target / 'run.log').write_text('Done 2000 runs in 1 second(s)')
            (target / 'seeds').mkdir()
            (target / 'seeds/a').write_bytes(b'a')
            self.gate.validate_fuzz(root, ['qpack'])
            # libFuzzer initializes two inputs even when its probe requests -runs=1.
            (root / 'capability.log').write_text('Done 2 runs in 0 second(s)\n')
            self.gate.validate_fuzz(root, ['qpack'])
            recipe = target / 'compile.sh'
            recipe.write_text(recipe.read_text().replace('-fsanitize=fuzzer,address,undefined',
                                                        r'-fsanitize=fuzzer\,address\,undefined'))
            self.gate.validate_fuzz(root, ['qpack'])
            for name, text in [('compile.sh', 'ordinary.a'), ('run.log', 'crashed'),
                               ('reproduce.sh', '-runs=1'), ('compile.sh', '-fsanitize=fuzzer,address,undefined ordinary.a')]:
                path = target / name
                previous = path.read_text()
                path.write_text(text)
                with self.subTest(name=name), self.assertRaises(self.gate.GateFailure):
                    self.gate.validate_fuzz(root, ['qpack'])
                path.write_text(previous)
            with self.assertRaises(self.gate.GateFailure):
                self.gate.validate_fuzz(root, ['qpack', 'http3_engine'])
            for capability in ('', 'Done 0 runs in 0 second(s)',
                               'Done 2 runs in 0 second(s)\nERROR: AddressSanitizer'):
                (root / 'capability.log').write_text(capability)
                with self.subTest(capability=capability), self.assertRaises(self.gate.GateFailure):
                    self.gate.validate_fuzz(root, ['qpack'])

    def test_failed_stage_preserves_prior_success_in_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            manifest = {'status': 'failed', 'stages': []}
            self.gate.execute_stage(manifest, path, 'native-off', lambda: {'tests': 3, 'artifacts': []})
            def fail():
                raise self.gate.GateFailure('original failure')
            with self.assertRaisesRegex(self.gate.GateFailure, 'original failure'):
                self.gate.execute_stage(manifest, path, 'native-on', fail)
            saved = json.loads((path / 'manifest.json').read_text())
            self.assertEqual([r['status'] for r in saved['stages']], ['passed', 'failed'])
            self.assertEqual(saved['stages'][1]['failure'], 'original failure')

    def test_preflight_failure_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            original = path / 'manifest.json'
            original.write_text('retained')
            result = self.gate.main(['--output', str(path)])
            self.assertEqual(result, 1)
            self.assertEqual(original.read_text(), 'retained')

    def test_supporting_evidence_cannot_be_unknown_or_hide_required_exclusions(self):
        for field, value in [('stage', 'unknown'), ('suite', 'unknown'), ('source', 'absent')]:
            bad = copy.deepcopy(self.inventory)
            row = next(r for r in bad['coverage'] if r['supporting_evidence'])
            row['supporting_evidence'][0][field] = value
            with self.subTest(field=field), self.assertRaises(self.gate.GateFailure):
                self.gate.validate_inventory(bad)
        bad = copy.deepcopy(self.inventory)
        bad['exclusions'].append({'id': 'http3_settings', 'reason': 'not applicable'})
        with self.assertRaises(self.gate.GateFailure):
            self.gate.validate_inventory(bad)

    def test_input_binary_changes_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'fixture'
            path.write_bytes(b'current binary')
            identities = [self.gate.artifact(path)]
            self.gate.verify_artifacts(identities)
            path.write_bytes(b'other binary')
            with self.assertRaises(self.gate.GateFailure):
                self.gate.verify_artifacts(identities)

    def test_new_output_retains_failed_preflight_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'evidence'
            self.assertEqual(self.gate.main(['--output', str(path)]), 1)
            receipt = self.gate.read_json(path / 'manifest.json')
            self.assertEqual(receipt['status'], 'failed')
            self.assertIn('required argument', receipt['failure'])

    def test_predecessor_runtime_failure_retains_failed_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'evidence'
            with patch.object(self.gate, 'preflight', return_value={}), \
                 patch.object(self.gate, 'source_identity', return_value={}), \
                 patch.object(self.gate, 'execute_stage', side_effect=RuntimeError('child receipt invalid')):
                self.assertEqual(self.gate.main(['--output', str(path)]), 1)
            receipt = self.gate.read_json(path / 'manifest.json')
            self.assertEqual(receipt['status'], 'failed')
            self.assertEqual(receipt['failure'], 'child receipt invalid')

    def test_shared_websocket_and_replay_transcripts_require_every_corpus_case(self):
        corpus = self.gate.corpus_names('websocket')
        valid = ''.join('PASS ' + name + ' RFC6455 5.6\n' for name in corpus)
        self.assertEqual(self.gate.suite_summary('websocket_conformance', valid)['tests'], len(corpus))
        replay_names = self.gate.corpus_names('http1') + corpus
        replay = ''.join('REPLAY ' + name + '\n' for name in replay_names)
        self.assertEqual(self.gate.suite_summary('protocol_fuzz_replay', replay)['tests'], len(replay_names))
        for suite, output in [('websocket_conformance', ''), ('websocket_conformance', valid.splitlines()[0]),
                              ('websocket_conformance', valid + valid), ('protocol_fuzz_replay', replay[:-10])]:
            with self.subTest(suite=suite), self.assertRaises(self.gate.GateFailure):
                self.gate.suite_summary(suite, output)

    def test_failed_process_retains_exact_command_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'attempt'
            row = self.gate.run_process([sys.executable, '-c', 'raise SystemExit(77)'], ROOT, path, 2)
            saved = self.gate.read_json(path / 'process.json')
            self.assertEqual(saved, row)
            self.assertEqual(saved['returncode'], 77)

    def test_native_binary_identity_and_actual_configuration_are_retained(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / 'build'
            (build / 'test').mkdir(parents=True)
            suite = build / 'test/http3_frame'
            suite.write_text('#!/bin/sh\nprintf "1 tests executed\n1 checks\n-> 1 successes\n-> 0 failures\n-> 0 skipped\n"\n')
            suite.chmod(0o700)
            result = self.gate.run_native_suites(build, ['http3_frame'], Path(tmp) / 'evidence')
            self.assertEqual(result['suites'][0]['binary'], self.gate.artifact(suite))

    def test_cxx20_configuration_accepts_configure_compiler_standard(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            (build / 'src').mkdir()
            (build / 'test').mkdir()
            for name in ('config.h', 'config.log', 'test/Makefile'):
                (build / name).write_text('/* TLS disabled */')
            makefile = build / 'src/Makefile'
            prefix = 'abs_top_srcdir = ' + str(ROOT) + '\n'
            makefile.write_text(prefix + 'CXX = clang++ -std=c++20\nAM_CXXFLAGS = -Wall\nCXXFLAGS = -O0 -g\n')
            result = self.gate.build_configuration(build, False, False)
            self.assertEqual(result['cxx'], 'clang++ -std=c++20')
            self.assertEqual(result['am_cxxflags'], '-Wall')
            self.assertEqual(result['cxxflags'], '-O0 -g')
            with self.assertRaisesRegex(self.gate.GateFailure, 'uninstrumented'):
                self.gate.build_configuration(build, False, True)
            makefile.write_text(prefix + 'CXX = clang++\nCXXFLAGS = -O0 -g\n')
            with self.assertRaisesRegex(self.gate.GateFailure, r'C\+\+20'):
                self.gate.build_configuration(build, False, False)

    def test_reusing_configured_build_preserves_prior_build_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / 'build'
            (build / 'test').mkdir(parents=True)
            (build / 'src').mkdir()
            (build / 'config.h').write_text('/* TLS disabled */')
            (build / 'config.log').write_text('configured with clang')
            (build / 'src/Makefile').write_text('abs_top_srcdir = ' + str(ROOT) + '\nCXXFLAGS = -std=c++20\nall:\n\t@echo configured source build\n')
            (build / 'Makefile').write_text('all:\n\t@echo configured build\nclean:\n\trm prior.log\n')
            (build / 'test/Makefile').write_text('http3_frame:\n\t@test -f $@\n')
            prior = build / 'prior.log'
            prior.write_text('prior verification')
            suite = build / 'test/http3_frame'
            suite.write_text('#!/bin/sh\nprintf "1 tests executed\n1 checks\n-> 1 successes\n-> 0 failures\n-> 0 skipped\n"\n')
            suite.chmod(0o700)
            self.gate.native_stage(build, ['http3_frame'], Path(tmp) / 'native-off')
            self.assertTrue(prior.is_file(), 'incremental builds must preserve prior evidence')

    def test_capture_preflight_requires_editcap_and_the_declared_decoder(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            tshark = path / 'tshark'
            tshark.write_text('#!/bin/sh\necho "TShark 4.6.4"\n')
            tshark.chmod(0o700)
            with patch.dict('os.environ', {'PATH': str(path)}):
                with self.assertRaisesRegex(self.gate.GateFailure, 'editcap'):
                    self.gate.capture_tools(tshark)
                editcap = path / 'editcap'
                editcap.write_text('#!/bin/sh\necho "Editcap 4.6.4"\n')
                editcap.chmod(0o700)
                tools = self.gate.capture_tools(tshark)
                self.assertIn('Editcap 4.6.4', tools['editcap']['stdout'])
                other = path / 'other-tshark'
                other.write_text(tshark.read_text())
                other.chmod(0o700)
                with self.assertRaises(self.gate.GateFailure):
                    self.gate.capture_tools(other)


if __name__ == '__main__':
    unittest.main()
