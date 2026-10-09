#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FuzzRunnerTest(unittest.TestCase):
    def test_unknown_or_empty_target_fails_before_compiler(self):
        for selection in ('', 'hpack bogus'):
            with tempfile.TemporaryDirectory() as build:
                result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                                         '--build-dir', build, '--targets', selection,
                                         '--compiler', '/missing/compiler'],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 2)
                self.assertIn('fuzz target', result.stderr)

    def test_selected_http2_missing_compiler_is_fatal(self):
        with tempfile.TemporaryDirectory() as build:
            result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                                     '--build-dir', build, '--targets', 'hpack http2_engine',
                                     '--compiler', '/missing/compiler'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertIn('libFuzzer compiler unavailable', result.stderr)

    def test_selected_quic_missing_compiler_is_fatal(self):
        with tempfile.TemporaryDirectory() as build:
            result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                                     '--build-dir', build, '--targets', 'quic_parser quic_state',
                                     '--compiler', '/missing/compiler'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertIn('libFuzzer compiler unavailable', result.stderr)

    def test_quic_state_requires_tls_off_configuration_before_building(self):
        for config in (None, '#define NATIVE_V3_TLS 1\n'):
            with tempfile.TemporaryDirectory() as build:
                args = ['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                        '--build-dir', build, '--targets', 'quic_state', '--compiler', '/usr/bin/true']
                if config is not None:
                    pathlib.Path(build, 'config.h').write_text(config)
                    args += ['--native-config-dir', build]
                result = subprocess.run(args, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 1)
                self.assertIn('TLS-off', result.stderr)
                self.assertFalse(pathlib.Path(build, 'probe.cpp').exists())

    def test_stateful_selections_validate_configuration_before_compilation(self):
        cases = [('quic_recovery', None, 'TLS-off'),
                 ('quic_recovery', '#define NATIVE_V3_TLS 1\n', 'TLS-off'),
                 ('quic_tls_callbacks', None, 'TLS-on'),
                 ('quic_tls_callbacks', '/* TLS disabled */\n', 'TLS-on'),
                 ('quic_tls_callbacks', '#define NATIVE_V3_TLS 1\n', 'provider flags')]
        for target, config, expected in cases:
            with self.subTest(target=target, config=config), tempfile.TemporaryDirectory() as build:
                args = ['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                        '--build-dir', build, '--targets', target, '--compiler', '/usr/bin/true']
                if config is not None:
                    pathlib.Path(build, 'config.h').write_text(config)
                    args += ['--native-config-dir', build]
                result = subprocess.run(args, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 1)
                self.assertIn(expected, result.stderr)
                self.assertFalse(pathlib.Path(build, 'probe.cpp').exists())

    def test_requested_unavailable_compiler_fails(self):
        with tempfile.TemporaryDirectory() as build:
            result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                                     '--build-dir', build, '--compiler', '/missing/compiler'],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('libFuzzer compiler unavailable', result.stderr)

    def test_callback_provider_flags_reject_unresolved_or_malformed_arguments(self):
        for flags in ('$(shell touch marker)', "'unterminated", ''):
            with self.subTest(flags=flags), tempfile.TemporaryDirectory() as build:
                pathlib.Path(build, 'config.h').write_text('#define NATIVE_V3_TLS 1\n')
                source = pathlib.Path(build, 'src')
                source.mkdir()
                (source / 'Makefile').write_text(
                    'V3_TLS_CFLAGS = ' + flags + '\nV3_TLS_LIBS = -lssl -lcrypto\n')
                result = subprocess.run([
                    'bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                    '--build-dir', build, '--targets', 'quic_tls_callbacks',
                    '--native-config-dir', build, '--compiler', '/usr/bin/true'],
                    cwd=build, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 1)
                self.assertIn('provider flags', result.stderr)
                self.assertFalse(pathlib.Path(build, 'probe.cpp').exists())
                self.assertFalse(pathlib.Path(build, 'marker').exists())


if __name__ == '__main__':
    unittest.main()
