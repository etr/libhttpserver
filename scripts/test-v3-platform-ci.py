#!/usr/bin/env python3
"""Execution contracts for the selected native CI toolchain."""
import json
import argparse
import importlib.util
import os
import shlex
import shutil
import sys
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


class SelectedPlatformEnvironment(unittest.TestCase):
    def test_native_platform_matrix_receives_matching_provider_cli(self):
        spec = importlib.util.spec_from_file_location('platform_ci_entrypoint', ROOT / 'scripts/run-v3-release-ci.py')
        entrypoint = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(entrypoint)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            selected = {'commit': 'a' * 40, 'version': '3.0.0', 'sha256': 'b' * 64,
                        'source': {}, 'files_sha256': {}}
            selection = root / 'selected.json'
            selection.write_text(json.dumps(selected))
            args = argparse.Namespace(selection=selection, output=root / 'output',
                archive=root / 'source.tar.gz', provider_prefix=root / 'provider',
                compiler='clang++', cc='clang', make='make', jobs=2,
                client_python='selected-python', node='node', pkg_config_dir=[], toolchain_prefix=None)
            observed = []

            class MatrixBoundary(Exception):
                pass

            def command(argv, cwd, env, **kwargs):
                observed.append((argv, env))
                raise MatrixBoundary()

            release = dict(source_root=str(root), source={}, files_sha256={})
            with patch.object(entrypoint.platform, 'system', return_value='Darwin'), \
                    patch.object(entrypoint, 'validate_request'), \
                    patch.object(entrypoint.identity, 'source_identity', return_value={}), \
                    patch.object(entrypoint, 'selected_archive', return_value=release), \
                    patch.object(entrypoint, 'Commands', return_value=command), \
                    patch.dict(os.environ, V3_OPENSSL='unselected-system-cli'):
                with self.assertRaises(MatrixBoundary):
                    entrypoint.platform_proof(args)
            self.assertEqual(len(observed), 1)
            cli = 'openssl.exe' if os.name == 'nt' else 'openssl'
            self.assertEqual(observed[0][1]['V3_OPENSSL'], str(root / 'provider/bin' / cli))
            self.assertEqual(observed[0][1]['LD_LIBRARY_PATH'], str(root / 'provider/lib'))


@unittest.skipIf(os.name == 'nt', 'macOS compiler wrapper uses POSIX executable scripts')
class MacOSToolchain(unittest.TestCase):
    def prepare(self, root, compiler_status=0):
        tools = root / 'tools'
        tools.mkdir()
        llvm = root / 'llvm keg'
        (llvm / 'bin').mkdir(parents=True)
        compiler = llvm / 'bin/clang++'
        compiler.write_text("""#!/usr/bin/env python3
import json, os, pathlib, sys
with open(os.environ['INVOCATIONS'], 'a') as stream:
    stream.write(json.dumps(sys.argv[1:]) + '\\n')
if any(mode in sys.argv for mode in ('-r', '-Wl,-r', '-c', '-S', '-E', '-fsyntax-only', '-static')) and any(arg.startswith('-Wl,-rpath,') for arg in sys.argv):
    sys.stderr.write('rpath requires a final dynamic link\\n')
    sys.exit(73)
if '-o' in sys.argv:
    output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])
    output.write_text('#!/bin/sh\\nexit 0\\n')
    output.chmod(0o755)
sys.exit(int(os.environ['COMPILER_STATUS']))
""")
        compiler.chmod(0o755)
        brew = tools / 'brew'
        brew.write_text('#!/bin/sh\nprintf "%s\\n" "$LLVM_PREFIX"\n')
        brew.chmod(0o755)
        env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ['PATH'],
                   LLVM_PREFIX=str(llvm), RUNNER_TEMP=str(root / 'runner'),
                   GITHUB_ENV=str(root / 'github-env'), INVOCATIONS=str(root / 'calls.jsonl'),
                   COMPILER_STATUS=str(compiler_status))
        (root / 'runner').mkdir()
        result = subprocess.run(['bash', str(ROOT / 'scripts/ci-macos-toolchain.sh')],
                                env=env, capture_output=True, text=True)
        return result, env, llvm

    def test_wrapper_selects_matching_library_and_preserves_arguments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result, env, llvm = self.prepare(root)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            selected = dict(line.split('=', 1) for line in Path(env['GITHUB_ENV']).read_text().splitlines())
            self.assertEqual(selected['V3_CC'], str(llvm / 'bin/clang'))
            result = subprocess.run([selected['V3_CXX'], 'path with spaces.cpp', '-o', str(root / 'consumer')], env=env)
            self.assertEqual(result.returncode, 0)
            calls = [json.loads(line) for line in Path(env['INVOCATIONS']).read_text().splitlines()]
            self.assertIn('-std=c++20', calls[1])
            self.assertIn('-L' + str(llvm / 'lib/c++'), calls[1])
            self.assertIn('-Wl,-rpath,' + str(llvm / 'lib/c++'), calls[1])
            self.assertEqual(calls[-1][-3:], ['path with spaces.cpp', '-o', str(root / 'consumer')])
            result = subprocess.run([selected['V3_CXX'], '-dynamiclib', 'input.o'], env=env)
            self.assertEqual(result.returncode, 0)
            calls = [json.loads(line) for line in Path(env['INVOCATIONS']).read_text().splitlines()]
            self.assertIn('-L' + str(llvm / 'lib/c++'), calls[-1])
            self.assertIn('-Wl,-rpath,' + str(llvm / 'lib/c++'), calls[-1])

    def test_nonfinal_modes_do_not_receive_runtime_linker_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result, env, llvm = self.prepare(root)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            selected = dict(line.split('=', 1) for line in Path(env['GITHUB_ENV']).read_text().splitlines())
            for mode in ('-r', '-Wl,-r', '-c', '-S', '-E', '-fsyntax-only', '-static'):
                with self.subTest(mode=mode):
                    result = subprocess.run([selected['V3_CXX'], mode, 'path with spaces.cpp'],
                                            env=env, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    calls = [json.loads(line) for line in Path(env['INVOCATIONS']).read_text().splitlines()]
                    self.assertIn('-stdlib=libc++', calls[-1])
                    self.assertIn('-D_LIBCPP_DISABLE_AVAILABILITY', calls[-1])
                    self.assertNotIn('-L' + str(llvm / 'lib/c++'), calls[-1])
                    self.assertNotIn('-Wl,-rpath,' + str(llvm / 'lib/c++'), calls[-1])
                    self.assertEqual(calls[-1][-2:], [mode, 'path with spaces.cpp'])

    def test_missing_cancellation_support_stops_before_exporting_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            result, env, _ = self.prepare(Path(directory), 19)
            self.assertEqual(result.returncode, 19, result.stdout + result.stderr)
            self.assertFalse(Path(env['GITHUB_ENV']).exists())


@unittest.skipIf(os.name == 'nt', 'competing compiler and symlink fixtures use POSIX paths')
class InstalledPackageFixtureEnvironment(unittest.TestCase):
    def test_fixture_entrypoint_honors_quoted_cxx_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            selected = root / 'selected compiler.py'
            selected.write_text("""import json, os, sys
if sys.argv[1] != '--selected-toolchain':
    sys.exit(72)
with open(os.environ['INVOCATIONS'], 'a') as stream:
    stream.write(json.dumps(sys.argv[2:]) + '\\n')
os.execv(os.environ['REAL_CXX'], [os.environ['REAL_CXX']] + sys.argv[2:])
""")
            competing = root / 'c++'
            competing.write_text('#!/bin/sh\nprintf "unselected compiler invoked\\n" >&2\nexit 71\n')
            competing.chmod(0o755)
            env = dict(os.environ, CXX=shlex.join([sys.executable, str(selected), '--selected-toolchain']),
                       PATH=str(root) + os.pathsep + os.environ['PATH'],
                       INVOCATIONS=str(root / 'calls.jsonl'), REAL_CXX=shutil.which('c++'))
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/test-v3-installed-package.py'),
                                     'AuditTest.test_native_public_api_is_available_through_umbrella',
                                     'AuditTest.test_current_umbrella_has_no_backend_types'],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = [json.loads(line) for line in Path(env['INVOCATIONS']).read_text().splitlines()]
            self.assertTrue(any('-fsyntax-only' in call for call in calls))
            self.assertTrue(any('-E' in call for call in calls))

    def test_pe_fixture_accepts_canonical_path_under_symlinked_tmpdir(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / 'real'
            target.mkdir()
            alias = root / 'alias'
            alias.symlink_to(target, target_is_directory=True)
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/test-v3-installed-package.py'),
                                     'AuditTest.test_pe_resolution_uses_explicit_roots_and_rejects_msys'],
                                    env=dict(os.environ, TMPDIR=str(alias)), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
