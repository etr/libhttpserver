#!/usr/bin/env python3
"""Execution contracts for the selected native CI toolchain."""
import json
import argparse
import importlib.util
import hashlib
import io
import os
import re
import shlex
import shutil
import sys
from pathlib import Path
import subprocess
import tempfile
import tarfile
import textwrap
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


class FreeBSDToolchain(unittest.TestCase):
    def selector(self):
        path = ROOT / 'scripts/ci-freebsd-toolchain.py'
        self.assertTrue(path.is_file(), 'FreeBSD complete C++20 SDK selector is missing')
        spec = importlib.util.spec_from_file_location('freebsd_toolchain', path)
        selector = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(selector)
        return selector

    def test_vendor_build_keeps_freebsd_stable_abi_and_private_install(self):
        selector = self.selector()
        argv = selector.configure_command(Path('/source'), Path('/build'), Path('/sdk'), '/llvm/clang', '/llvm/clang++')
        for argument in ('-DLLVM_ENABLE_RUNTIMES=libcxx', '-DLIBCXX_CXX_ABI=libcxxrt',
                         '-DLIBCXX_CXX_ABI_INCLUDE_PATHS=/usr/include/c++/v1',
                         '-DLIBCXX_ABI_VERSION=1', '-DLIBCXX_ABI_UNSTABLE=OFF',
                         '-DLIBCXX_ENABLE_NEW_DELETE_DEFINITIONS=ON',
                         '-DLIBCXXABI_USE_LLVM_UNWINDER=OFF',
                         '-DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF',
                         '-DCMAKE_INSTALL_PREFIX=/sdk', '-DCMAKE_C_COMPILER=/llvm/clang',
                         '-DCMAKE_CXX_COMPILER=/llvm/clang++'):
            self.assertIn(argument, argv)
        self.assertNotIn('-fexperimental-library', ' '.join(argv))

    def test_source_digest_failure_stops_before_extraction(self):
        selector = self.selector()
        self.assertTrue(callable(getattr(selector, 'download_source', None)), 'verified source fetcher is missing')
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(selector.urllib.request, 'urlopen', return_value=io.BytesIO(b'tampered source')):
            with self.assertRaisesRegex(RuntimeError, 'SHA256 mismatch'):
                selector.download_source(Path(directory))
            self.assertEqual(['source.tar.xz'], [path.name for path in Path(directory).iterdir()])

    def test_verified_source_contains_all_runtime_build_inputs(self):
        selector = self.selector()
        self.assertTrue(callable(getattr(selector, 'download_source', None)), 'verified source fetcher is missing')
        data = io.BytesIO()
        root = 'llvm-project-' + selector.VERSION + '.src'
        with tarfile.open(fileobj=data, mode='w:xz') as archive:
            for part in ('cmake', 'llvm/cmake', 'runtimes', 'libcxx', 'libc', 'clang'):
                member = tarfile.TarInfo(root + '/' + part + '/input')
                member.size = 1
                archive.addfile(member, io.BytesIO(b'x'))
        content = data.getvalue()
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(selector.urllib.request, 'urlopen', return_value=io.BytesIO(content)), \
                patch.object(selector, 'SOURCE_SHA256', hashlib.sha256(content).hexdigest()):
            source = selector.download_source(Path(directory))
            for part in ('cmake', 'llvm/cmake', 'runtimes', 'libcxx', 'libc'):
                self.assertEqual((source / part / 'input').read_bytes(), b'x')
            self.assertFalse((source / 'clang').exists())

    def test_compiler_wrapper_keeps_headers_and_runtime_through_clean_environment(self):
        selector = self.selector()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            compiler = root / 'compiler'
            compiler.write_text('#!' + sys.executable + '\nimport json, sys\nprint(json.dumps(sys.argv[1:]))\n')
            compiler.chmod(0o755)
            prefix = root / 'selected sdk'
            wrapper = selector.write_compiler(prefix, str(compiler))
            for mode in ('', '-c', '-E', '-S', '-fsyntax-only', '-r', '-Wl,-r', '-static'):
                with self.subTest(mode=mode):
                    args = ([mode] if mode else []) + ['path with spaces.cpp', '-o', 'path with spaces.o']
                    result = subprocess.run([str(wrapper)] + args, env={'PATH': os.defpath}, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    actual = json.loads(result.stdout)
                    self.assertIn('-nostdinc++', actual)
                    self.assertEqual(actual[actual.index('-isystem') + 1], str(prefix / 'include/c++/v1'))
                    self.assertIn('path with spaces.cpp', actual)
                    if mode in ('-c', '-E', '-S', '-fsyntax-only', '-r', '-Wl,-r'):
                        self.assertNotIn('-lc++', actual)
                        self.assertFalse(any(flag.startswith('-Wl,-rpath,') for flag in actual))
                    else:
                        self.assertIn('-L' + str(prefix / 'lib'), actual)
                        self.assertIn('-lc++', actual)
                        self.assertEqual(any(flag.startswith('-Wl,-rpath,') for flag in actual), mode != '-static')


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
                client_python='selected-python', node='node', pkg_config_dir=[], toolchain_prefix=root / 'selected llvm')
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
            argv = observed[0][0]
            self.assertEqual(argv[argv.index('--toolchain-prefix') + 1], root / 'selected llvm')
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
            self.assertEqual(selected.get('V3_PACKAGE_TOOLCHAIN'), str(llvm))
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

    def test_workflows_pass_selected_prefix_even_when_sudo_cleans_environment(self):
        for workflow, job, entrypoint in (
                ('v3-package-validation.yml', 'macos', 'scripts/check-v3-installed-consumer.py'),
                ('v3-release-acceptance.yml', 'darwin', 'scripts/run-v3-release-ci.py')):
            text = (ROOT / '.github/workflows' / workflow).read_text()
            job_text = re.split(r'^  [a-z0-9_-]+:', text.split('  ' + job + ':', 1)[1], maxsplit=1, flags=re.M)[0]
            lines = job_text.splitlines()
            invocation = next(i for i, line in enumerate(lines) if entrypoint in line and '${prefix[@]}' in line)
            start = next(i for i in range(invocation, -1, -1) if 'run: |' in lines[i])
            block = textwrap.dedent('\n'.join(lines[start + 1:invocation + 1]))
            for privileged in (False, True):
                with self.subTest(workflow=workflow, privileged=privileged), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    result, env, llvm = self.prepare(root)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    selected = dict(line.split('=', 1) for line in Path(env['GITHUB_ENV']).read_text().splitlines())
                    # Exercise the workflow's shell and argv; only native tools are substituted.
                    python = root / 'tools/python3'
                    python.write_text('#!' + sys.executable + '\n' + textwrap.dedent("""\
                        import json, os, sys
                        if sys.argv[1] == '-c':
                            sys.exit(int(os.environ['PRIVILEGED']))
                        with open(os.environ['CAPTURE'], 'w') as stream:
                            json.dump({'argv': sys.argv[1:], 'toolchain_environment': os.environ.get('V3_PACKAGE_TOOLCHAIN')}, stream)
                        """))
                    python.chmod(0o755)
                    sudo = root / 'tools/sudo'
                    sudo.write_text('#!/bin/sh\nexec env -i CAPTURE="$CAPTURE" "$@"\n')
                    sudo.chmod(0o755)
                    result = subprocess.run(['bash', '-e', '-c', block], cwd=ROOT,
                        env=dict(env, **selected, PRIVILEGED=str(int(privileged)), CAPTURE=str(root / 'captured.json')),
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    captured = json.loads((root / 'captured.json').read_text())
                    argv = captured['argv']
                    self.assertEqual(argv[argv.index('--toolchain-prefix') + 1] if '--toolchain-prefix' in argv else None, str(llvm))
                    self.assertEqual(argv[argv.index('--compiler') + 1], selected['V3_CXX'])
                    self.assertEqual(argv[argv.index('--cc') + 1], selected['V3_CC'])
                    if privileged:
                        self.assertIsNone(captured['toolchain_environment'])

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
