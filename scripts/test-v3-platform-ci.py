#!/usr/bin/env python3
"""Execution contracts for the selected native CI toolchain."""
import json
import argparse
import importlib.util
import os
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
            result = subprocess.run([selected['V3_CXX'], '-c', 'path with spaces.cpp'], env=env)
            self.assertEqual(result.returncode, 0)
            calls = [json.loads(line) for line in Path(env['INVOCATIONS']).read_text().splitlines()]
            self.assertIn('-std=c++20', calls[1])
            self.assertIn('-L' + str(llvm / 'lib/c++'), calls[1])
            self.assertIn('-Wl,-rpath,' + str(llvm / 'lib/c++'), calls[1])
            self.assertEqual(calls[-1][-2:], ['-c', 'path with spaces.cpp'])

    def test_missing_cancellation_support_stops_before_exporting_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            result, env, _ = self.prepare(Path(directory), 19)
            self.assertEqual(result.returncode, 19, result.stdout + result.stderr)
            self.assertFalse(Path(env['GITHUB_ENV']).exists())


if __name__ == '__main__':
    unittest.main()
