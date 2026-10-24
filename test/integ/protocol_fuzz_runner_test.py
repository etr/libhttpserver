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

    def test_http3_and_qpack_missing_compiler_is_fatal(self):
        for target in ('http3_engine', 'qpack'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as build:
                result = subprocess.run([
                    'bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                    '--build-dir', build, '--targets', target,
                    '--compiler', '/missing/compiler'], capture_output=True,
                    text=True, timeout=10)
                self.assertEqual(result.returncode, 1)
                self.assertIn('libFuzzer compiler unavailable', result.stderr)

    def test_http3_and_qpack_require_tls_off_before_compilation(self):
        for target in ('http3_engine', 'qpack'):
            for config in (None, '#define NATIVE_V3_TLS 1\n'):
                with self.subTest(target=target, config=config), tempfile.TemporaryDirectory() as build:
                    args = ['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                            '--build-dir', build, '--targets', target, '--compiler', '/usr/bin/true']
                    if config is not None:
                        pathlib.Path(build, 'config.h').write_text(config)
                        args += ['--native-config-dir', build]
                    result = subprocess.run(args, capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 1)
                    self.assertIn('TLS-off', result.stderr)
                    self.assertFalse(pathlib.Path(build, 'probe.cpp').exists())

    def test_http3_qpack_build_and_invocation_contract(self):
        # This probe verifies shell arguments; the real sanitizer runs are separate.
        import json
        import os
        import shlex
        with tempfile.TemporaryDirectory() as directory:
            location = pathlib.Path(directory)
            compiler = location / 'compiler with spaces.py'
            compiler.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
if '--version' in sys.argv:
    print('test argument recorder'); sys.exit(0)
with open(os.environ['FUZZ_ARGUMENT_LOG'], 'a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\\n')
output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])
output.write_text("#!/usr/bin/env python3\\nimport json, os, sys\\nwith open(os.environ['FUZZ_RUN_LOG'], 'a') as log: log.write(json.dumps(sys.argv) + '\\\\n')\\n")
output.chmod(0o755)
''')
            compiler.chmod(0o755)
            config = location / 'native config'
            config.mkdir()
            (config / 'config.h').write_text('/* TLS disabled */\n')
            build = location / 'fuzz output'
            environment = dict(os.environ, FUZZ_ARGUMENT_LOG=str(location / 'compile.jsonl'),
                               FUZZ_RUN_LOG=str(location / 'run.jsonl'))
            result = subprocess.run([
                'bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                '--build-dir', str(build), '--targets', 'http3_engine qpack http1_head',
                '--compiler', str(compiler), '--native-config-dir', str(config),
                '--runs', '9', '--seconds', '2'], env=environment,
                capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            builds = [json.loads(line) for line in (location / 'compile.jsonl').read_text().splitlines()]
            runs = [json.loads(line) for line in (location / 'run.jsonl').read_text().splitlines()]
            for target, macro, length in [('http3_engine', '-DHTTP3_ENGINE_LIBFUZZER', 4096),
                                          ('qpack', '-DQPACK_LIBFUZZER', 512)]:
                arguments = next(args for args in builds if macro in args)
                self.assertNotIn('', arguments)
                self.assertIn('-fsanitize=fuzzer,address,undefined', arguments)
                self.assertIn('-I' + str(config), arguments)
                self.assertIn(str(ROOT / 'src/detail/http3_connection.cpp'), arguments)
                self.assertEqual(arguments[-2:], ['-o', str(build / target / 'fuzz')])
                self.assertEqual(shlex.split((build / target / 'compile.sh').read_text()),
                                 [str(compiler)] + arguments)
                self.assertTrue((build / target / 'sources.list').is_file())
                invocation = next(args for args in runs if args[0] == str(build / target / 'fuzz'))
                for flag in ('-runs=9', '-max_total_time=2', '-timeout=5', '-rss_limit_mb=512',
                             '-seed=128', '-max_len=' + str(length)):
                    self.assertIn(flag, invocation)
                self.assertEqual(invocation[1], str(build / target / 'seeds'))
                self.assertTrue(list((build / target / 'seeds').glob('*.seed')))
                self.assertTrue((build / target / 'reproduce.sh').is_file())
            head = next(args for args in builds if str(ROOT / 'test/fuzz/http1_head_fuzz.cpp') in args)
            self.assertNotIn('', head)
            self.assertIn('-fsanitize=fuzzer,address,undefined', head)
            self.assertEqual(head[-2:], ['-o', str(build / 'http1_head/fuzz')])
            self.assertEqual(shlex.split((build / 'http1_head/compile.sh').read_text()),
                             [str(compiler)] + head)
            self.assertEqual(len(builds), 4)  # Instrumentation probe and three selected targets.

    def test_callback_provider_arguments_preserve_boundaries_and_link_order(self):
        import json
        import os
        import shlex
        with tempfile.TemporaryDirectory() as directory:
            location = pathlib.Path(directory)
            compiler = location / 'compiler with spaces'
            compiler.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
if '--version' in sys.argv:
    print('test argument recorder'); sys.exit(0)
with open(os.environ['FUZZ_ARGUMENT_LOG'], 'a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\\n')
output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])
output.write_text('#!/bin/sh\\nexit 0\\n')
output.chmod(0o755)
''')
            compiler.chmod(0o755)
            config = location / 'native config'
            (config / 'src').mkdir(parents=True)
            (config / 'config.h').write_text('#define NATIVE_V3_TLS 1\n')
            cflags = ['-I' + str(location / 'provider headers'), '-DPROVIDER_VALUE=two words']
            libs = ['-L' + str(location / 'provider libs'), '-lssl', '-lcrypto']
            (config / 'src/Makefile').write_text('V3_TLS_CFLAGS = ' + shlex.join(cflags)
                + '\nV3_TLS_LIBS = ' + shlex.join(libs) + '\n')
            build = location / 'fuzz output'
            log = location / 'compile.jsonl'
            result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                '--build-dir', str(build), '--targets', 'quic_tls_callbacks',
                '--compiler', str(compiler), '--native-config-dir', str(config),
                '--runs', '1', '--seconds', '1'],
                env=dict(os.environ, FUZZ_ARGUMENT_LOG=str(log)),
                capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            arguments = json.loads(log.read_text().splitlines()[-1])
            self.assertNotIn('', arguments)
            start = arguments.index('-DQUIC_TLS_CALLBACKS_LIBFUZZER')
            self.assertEqual(arguments[start:start + 5],
                ['-DQUIC_TLS_CALLBACKS_LIBFUZZER', '-I' + str(ROOT / 'test'), '-I' + str(config)] + cflags)
            self.assertEqual(arguments[-5:], libs + ['-o', str(build / 'quic_tls_callbacks/fuzz')])
            self.assertEqual(shlex.split((build / 'quic_tls_callbacks/compile.sh').read_text()),
                             [str(compiler)] + arguments)
            self.assertEqual((build / 'quic_tls_callbacks/provider.log').read_text().splitlines(), cflags + libs)

    def test_seed_and_corpus_arguments_reject_invalid_inputs_before_compilation(self):
        for seed in ('0', '-1', '4294967296', '$(touch marker)', '12x'):
            with self.subTest(seed=seed), tempfile.TemporaryDirectory() as build:
                result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                    '--build-dir', build, '--seed', seed, '--compiler', '/missing/compiler'],
                    capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 2)
                self.assertIn('invalid fuzz seed', result.stderr)

    def test_restored_corpus_is_merged_without_following_links_or_overwriting_seeds(self):
        import os
        with tempfile.TemporaryDirectory() as directory:
            location = pathlib.Path(directory).resolve()
            compiler = location / 'compiler'
            compiler.write_text("#!/usr/bin/env python3\nimport pathlib,sys\n"
                "if '--version' in sys.argv: print('recorder'); sys.exit()\n"
                "p=pathlib.Path(sys.argv[sys.argv.index('-o')+1]); "
                "p.write_text('#!/bin/sh\\nexit 0\\n'); p.chmod(0o755)\n")
            compiler.chmod(0o755)
            corpus = location / 'corpus'
            (corpus / 'http1_head').mkdir(parents=True)
            restored = corpus / 'http1_head' / 'extra'
            restored.write_bytes(b'unique restored input')
            build = location / 'build'
            result = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                '--build-dir', str(build), '--compiler', str(compiler), '--targets', 'http1_head',
                '--seed', '987', '--corpus-dir', str(corpus)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            inputs = [p.read_bytes() for p in (corpus / 'http1_head').iterdir()]
            self.assertIn(b'unique restored input', inputs)
            for fixture in (ROOT / 'test/conformance/http1').glob('*.wire'):
                self.assertIn(fixture.read_bytes(), inputs)
            reproduction = (build / 'http1_head/reproduce.sh').read_text()
            self.assertIn('-seed=987', reproduction)
            self.assertIn(str(corpus / 'http1_head'), reproduction)
            restored.unlink()
            restored.symlink_to(ROOT / 'configure.ac')
            bad = subprocess.run(['bash', str(ROOT / 'scripts/run-v3-protocol-fuzz.sh'),
                '--build-dir', str(location / 'bad'), '--compiler', str(compiler),
                '--targets', 'http1_head', '--corpus-dir', str(corpus)],
                capture_output=True, text=True, timeout=10)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('symlink', bad.stderr)

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
