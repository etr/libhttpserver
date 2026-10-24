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
            compiler = root / 'compiler with spaces'
            compiler.write_text('#!' + sys.executable + '\nimport json, sys\nprint(json.dumps(sys.argv[1:]))\n')
            compiler.chmod(0o755)
            prefix = root / 'selected sdk'
            wrapper = selector.write_compiler(prefix, str(compiler))
            # The wrapper's env shebang must use the selected Bash even in the clean environment.
            environment = {'PATH': str(Path(shutil.which('bash')).parent) + os.pathsep + os.defpath}
            for mode in ('', '-c', '-E', '-S', '-fsyntax-only', '-r', '-Wl,-r', '--version', '-static', '-shared'):
                with self.subTest(mode=mode):
                    args = ([mode] if mode else []) + ['path with spaces.cpp', '-o', 'path with spaces.o']
                    result = subprocess.run([str(wrapper)] + args, env=environment, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    actual = json.loads(result.stdout)
                    expected = ['-nostdinc++', '-isystem', str(prefix / 'include/c++/v1')] + args
                    if mode not in ('-c', '-E', '-S', '-fsyntax-only', '-r', '-Wl,-r', '--version'):
                        expected += ['-nostdlib++', '-L' + str(prefix / 'lib'), '-lc++']
                        if mode != '-static':
                            expected += ['-Wl,-rpath,' + str(prefix / 'lib')]
                    self.assertEqual(actual, expected)


class SelectedPlatformEnvironment(unittest.TestCase):
    class NativeChecksComplete(Exception):
        pass

    def native_checks(self, root, inherited_prefix=None, selected=True, dependency_selected=True,
                      selected_tools=False, retain_tools=False):
        """Run the archive orchestrator, real child/make and the production audit.

        Only archive identity, the expensive package producer and native inspectors
        are fixtures; this does not establish native FreeBSD ABI compatibility.
        """
        spec = importlib.util.spec_from_file_location('platform_native_checks', ROOT / 'scripts/run-v3-release-ci.py')
        entrypoint = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(entrypoint)
        sdk = root / 'selected sdk'
        (sdk / 'lib').mkdir(parents=True)
        (sdk / 'lib/libc++.so.1.0').touch()
        source = root / 'source'
        (source / 'scripts').mkdir(parents=True)
        producer = source / 'scripts/check-v3-installed-consumer.py'
        producer.write_text(textwrap.dedent('''\
            import json, os, pathlib, shutil, sys
            output = pathlib.Path(sys.argv[sys.argv.index('--output') + 1])
            shutil.copytree(pathlib.Path(__file__).parents[2] / 'fixture', output)
            (output / 'child.json').write_text(json.dumps({'argv': sys.argv[1:],
                'prefix': os.environ.get('V3_PACKAGE_TOOLCHAIN')}))
            # Like the package producer, selecting a runtime in this child cannot
            # update the archive orchestrator's later make environment.
            if '--toolchain-prefix' in sys.argv:
                os.environ['V3_PACKAGE_TOOLCHAIN'] = sys.argv[sys.argv.index('--toolchain-prefix') + 1]
            '''))
        recipe = (ROOT / 'Makefile.am').read_text().split(
            '\ncheck-v3-native-linkage:\n', 1)[1].split('\n\n', 1)[0]
        for mode in ('no', 'yes'):
            build = root / 'fixture' / ('tls-' + mode) / 'build'
            (build / 'src/.libs').mkdir(parents=True)
            (build / 'test').mkdir()
            (build / 'src/.libs/libhttpserver_v3core.a').touch()
            (build / 'test/native_linkage').write_text('#!/bin/sh\nexit 0\n')
            (build / 'test/native_linkage').chmod(0o755)
            (build / 'test/Makefile').write_text('TEST_LOGS = native_linkage.log\nnative_linkage:;@:\n')
            (build / 'src/Makefile').write_text(
                'libhttpserver.la:\n\tlibtool --tag=CXX --mode=link -o libhttpserver.la archive\n')
            (build / 'Makefile').write_text(
                'all:;@:\ncheck: check-v3-native-linkage\n'
                'top_srcdir := ' + str(ROOT) + '\ntop_builddir := .\nabs_top_builddir := $(CURDIR)\n'
                'enable_v3_tls := ' + mode + '\ncheck-v3-native-linkage:\n' + recipe + '\n')
        tools = root / 'tools'
        tools.mkdir()
        # ldd uses whitespace-delimited paths; the alias resolves to the quoted SDK.
        (root / 'sdk').symlink_to(sdk, target_is_directory=True)
        dependency = root / ('sdk/lib/libc++.so.1.0' if dependency_selected else 'other/libc++.so.1.0')
        for name, output in (('uname', 'FreeBSD'), ('nm', ''), ('ldd',
                'libc++.so.1 => ' + str(dependency) + ' (0x1)\nlibcxxrt.so.1 => /lib/libcxxrt.so.1 (0x2)')):
            tool = tools / name
            tool.write_text("#!/bin/sh\ncat <<'OUTPUT'\n" + output + '\nOUTPUT\n')
            tool.chmod(0o755)
        if not dependency_selected:
            dependency.parent.mkdir()
            dependency.touch()
        selection = root / 'selected.json'
        selection.write_text(json.dumps(dict(commit='a' * 40, version='3.0.0', sha256='b' * 64,
                                            source={}, files_sha256={})))
        args = argparse.Namespace(selection=selection, output=root / 'output', archive=root / 'source.tar.gz',
            provider_prefix=root / 'provider', compiler=str(sdk / 'bin/clang++'), cc='/llvm/bin/clang',
            make=os.environ.get('MAKE') or shutil.which('gmake') or shutil.which('make'), jobs=2, client_python=sys.executable,
            node='node', pkg_config_dir=[], toolchain_prefix=sdk if selected else None)
        release = dict(source_root=str(source), source={}, files_sha256={})
        counts = dict(tests=1, checks=1)
        gate = argparse.Namespace(expected_identity=lambda *args: {}, read_json=None)
        env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ['PATH'])
        env.pop('V3_PACKAGE_TOOLCHAIN', None)
        if inherited_prefix is not None:
            env['V3_PACKAGE_TOOLCHAIN'] = str(inherited_prefix)
        if selected_tools:
            compiler = root / 'selected compiler.py'
            compiler.write_text(textwrap.dedent('''\
                import json, os, shlex, sys
                assert sys.argv[1:3] == ['--selected-toolchain', 'argument with spaces'], sys.argv
                with open(os.environ['COMPILER_CALLS'], 'a') as stream:
                    stream.write(json.dumps(sys.argv[3:]) + '\\n')
                command = shlex.split(os.environ['CALLER_CXX']) + sys.argv[3:]
                os.execvp(command[0], command)
                '''))
            make = root / 'selected-make'
            make.symlink_to(shutil.which('gmake') or shutil.which('make'))
            python = root / 'selected python'
            python.symlink_to(sys.executable)
            args.compiler = shlex.join([str(python), str(compiler), '--selected-toolchain', 'argument with spaces'])
            args.cc = str(root / 'selected cc')
            args.make = str(make)
            probe = root / 'compile probe.py'
            probe.write_text(textwrap.dedent('''\
                import os, shlex, subprocess
                assert os.environ['CC'] == os.environ['EXPECTED_CC'], os.environ.get('CC')
                assert os.environ['MAKE'] == os.environ['EXPECTED_MAKE'], os.environ.get('MAKE')
                subprocess.run(shlex.split(os.environ['CXX']) + ['-std=c++20', '-fsyntax-only',
                    '-x', 'c++', '-I' + os.environ['PUBLIC_HEADERS'], '-'],
                    input='#include <httpserver.hpp>\\nhttpserver::server::server_options options;\\n',
                    text=True, check=True)
                '''))
            for mode in ('no', 'yes'):
                makefile = root / 'fixture' / ('tls-' + mode) / 'build/Makefile'
                with makefile.open('a') as stream:
                    stream.write('check: selected-tools\nselected-tools:\n\t' + shlex.join([sys.executable, str(probe)]) + '\n')
            env.update(CXX='unselected-cxx', CC='unselected-cc', MAKE='unselected-make',
                       EXPECTED_CC=args.cc, EXPECTED_MAKE=args.make, PUBLIC_HEADERS=str(ROOT / 'src'),
                       CALLER_CXX=os.environ.get('CXX', 'c++'), COMPILER_CALLS=str(root / 'compiler-calls.jsonl'))
        if retain_tools:
            compiler_alias = root / 'compiler alias.py'
            compiler_alias.symlink_to(compiler)
            args.compiler = shlex.join([str(python), str(compiler_alias), '--selected-toolchain', 'argument with spaces'])
            cc = Path(args.cc)
            cc.symlink_to(shutil.which('cc'))
            args.cc = shlex.join([str(cc), '-DSELECTED_CC=1'])
            env['EXPECTED_CC'] = args.cc
            # Exercise a spaced make executable at the retention boundary. GNU
            # make's recursive MAKE variable needs a shell-quoted command.
            make_alias = root / 'selected make'
            make_alias.symlink_to(make)
            args.make = str(make_alias)
            for mode in ('no', 'yes'):
                makefile = root / 'fixture' / ('tls-' + mode) / 'build/Makefile'
                with makefile.open('a') as stream:
                    stream.write('MAKE := ' + shlex.quote(str(make_alias)) + '\n')
            env['EXPECTED_MAKE'] = shlex.quote(str(make_alias))
            args.node = sys.executable
            args.archive.touch()
            header = args.provider_prefix / 'include/openssl/opensslv.h'
            header.parent.mkdir(parents=True)
            header.write_text('selected provider header')
            gate.artifact = lambda path: dict(path=str(path), bytes=path.stat().st_size,
                                              sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        with patch.object(entrypoint.platform, 'system', return_value='FreeBSD'), \
                patch.object(entrypoint, 'validate_request'), \
                patch.object(entrypoint.identity, 'source_identity', return_value={}), \
                patch.object(entrypoint, 'selected_archive', return_value=release), \
                patch.object(entrypoint, 'gate_runner', return_value=gate), \
                patch.object(entrypoint.checks, 'validate_package', return_value=counts), \
                patch.object(entrypoint.checks, 'automake_result', side_effect=[counts, counts if retain_tools else self.NativeChecksComplete()]), \
                patch.object(entrypoint, 'validate_origin'), \
                patch.dict(os.environ, env, clear=True):
            try:
                entrypoint.platform_proof(args)
            finally:
                self.assertEqual(os.environ, env, 'archive proof must keep its selected SDK environment local')

    @unittest.skipIf(os.name == 'nt', 'selected tools fixture requires POSIX scripts and GNU make')
    def test_explicit_tools_reach_real_make_and_python_compiler_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(self.NativeChecksComplete):
                self.native_checks(root, selected_tools=True)
            calls = [json.loads(line) for line in (root / 'compiler-calls.jsonl').read_text().splitlines()]
            self.assertEqual(len(calls), 2, 'both TLS modes must compile through the selected CXX command')
            self.assertTrue(all('-fsyntax-only' in call for call in calls))

    @unittest.skipIf(os.name == 'nt', 'selected make fixture requires POSIX scripts and GNU make')
    def test_native_fixture_uses_callers_selected_make_for_test_enumeration(self):
        with tempfile.TemporaryDirectory() as directory:
            tools = Path(directory)
            requested = os.environ.get('MAKE') or shutil.which('gmake') or shutil.which('make')
            selected = shutil.which(requested)
            self.assertIsNotNone(selected, 'caller-selected GNU make must be available')
            (tools / 'gmake').symlink_to(selected)
            for caller_make in (requested, 'gmake'):
                with self.subTest(make=caller_make), tempfile.TemporaryDirectory() as fixture, \
                        patch.dict(os.environ, MAKE=caller_make,
                                   PATH=str(tools) + os.pathsep + os.environ['PATH']):
                    root = Path(fixture)
                    make = root / 'caller-make'
                    real_make = shutil.which(os.environ['MAKE'])
                    self.assertIsNotNone(real_make, 'caller-selected GNU make must be available')
                    calls = root / 'make-calls.jsonl'
                    make.write_text('#!' + sys.executable + '\nimport json, os, sys\n' +
                        'with open(' + repr(str(calls)) + ', "a") as stream: stream.write(json.dumps(sys.argv[1:]) + "\\n")\n' +
                        'os.execv(' + repr(real_make) + ', [' + repr(real_make) + '] + sys.argv[1:])\n')
                    make.chmod(0o755)
                    with patch.dict(os.environ, MAKE=str(make)), self.assertRaises(self.NativeChecksComplete):
                        self.native_checks(root)
                    self.assertTrue(calls.is_file(), 'native fixture bypassed the caller-selected make')
                    invocations = [json.loads(line) for line in calls.read_text().splitlines()]
                    self.assertEqual(sum('--eval' in argv for argv in invocations), 2)

    @unittest.skipIf(os.name == 'nt', 'selected tool retention fixture uses POSIX executable aliases')
    def test_archive_retains_compound_compilers_and_spaced_make_exact_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.native_checks(root, selected_tools=True, retain_tools=True)
            sys.path.insert(0, str(ROOT / 'scripts/lib'))
            from release_platform import Catalog
            catalog = Catalog(root / 'output/bundle/FreeBSD')
            for path in (root / 'selected python', root / 'compiler alias.py',
                         root / 'selected cc', root / 'selected make'):
                with self.subTest(path=path):
                    self.assertEqual(catalog.path(str(path)).read_bytes(), path.read_bytes())
                    self.assertEqual(catalog.path(str(path.resolve())).read_bytes(), path.read_bytes())

    @unittest.skipIf(os.name == 'nt', 'FreeBSD audit fixtures require POSIX scripts and GNU make')
    def test_explicit_sdk_reaches_both_later_make_checks(self):
        for inherited in (None, '/unselected/sdk'):
            with self.subTest(inherited=inherited), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                with self.assertRaises(self.NativeChecksComplete):
                    self.native_checks(root, inherited_prefix=inherited)
                commands = json.loads((root / 'output/commands.json').read_text())
                checks = [command for command in commands if 'check' in command['argv']]
                self.assertEqual(len(checks), 2)
                for command in checks:
                    self.assertEqual(command['exit'], 0)
                    self.assertEqual(command['environment']['V3_PACKAGE_TOOLCHAIN'], str(root / 'selected sdk'))
                    self.assertIn('A2 PASS', Path(command['log']).read_text())
                child = json.loads((root / 'output/matrix/child.json').read_text())
                self.assertEqual(child['prefix'], str(root / 'selected sdk'))
                self.assertEqual(child['argv'][child['argv'].index('--toolchain-prefix') + 1], str(root / 'selected sdk'))
                self.assertEqual(child['argv'][child['argv'].index('--compiler') + 1], str(root / 'selected sdk/bin/clang++'))
                self.assertEqual(child['argv'][child['argv'].index('--cc') + 1], '/llvm/bin/clang')

    @unittest.skipIf(os.name == 'nt', 'FreeBSD audit fixtures require POSIX scripts and GNU make')
    def test_archive_checks_preserve_inherited_sdk_without_cli_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(self.NativeChecksComplete):
                self.native_checks(root, inherited_prefix=root / 'selected sdk', selected=False)

    @unittest.skipIf(os.name == 'nt', 'FreeBSD audit fixtures require POSIX scripts and GNU make')
    def test_archive_checks_reject_runtime_outside_explicit_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'unselected FreeBSD C[+][+] runtime'):
                self.native_checks(Path(directory), dependency_selected=False)

    @unittest.skipIf(os.name == 'nt', 'FreeBSD audit fixtures require POSIX scripts and GNU make')
    def test_archive_checks_reject_missing_sdk_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'selected FreeBSD C[+][+] runtime prefix missing'):
                self.native_checks(Path(directory), selected=False)

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

    def execute_selected_workflow(self, root, workflow, job, entrypoint, privileged, preflight_failure=False):
        text = (ROOT / '.github/workflows' / workflow).read_text()
        job_text = re.split(r'^  [a-z0-9_-]+:', text.split('  ' + job + ':', 1)[1], maxsplit=1, flags=re.M)[0]
        lines = job_text.splitlines()
        invocation = next(i for i, line in enumerate(lines) if entrypoint in line and '${prefix[@]}' in line)
        start = next(i for i in range(invocation, -1, -1) if 'run: |' in lines[i])
        block = textwrap.dedent('\n'.join(lines[start + 1:invocation + 1]))
        result, env, llvm = self.prepare(root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        selected = dict(line.split('=', 1) for line in Path(env['GITHUB_ENV']).read_text().splitlines())
        # Exercise the workflow's shell and argv; only native tools are substituted.
        boundary = textwrap.dedent('''\
            import json, os, pathlib, sys
            state = pathlib.Path({state!r})
            def record(value):
                with open({capture!r}, 'a') as stream:
                    stream.write(json.dumps(value) + '\\n')
            ''').format(state=str(root / 'alias'), capture=str(root / 'boundary.jsonl'))
        ifconfig = root / 'tools/ifconfig'
        ifconfig.write_text('#!' + sys.executable + '\n' + boundary + textwrap.dedent('''\
            args = sys.argv[1:]
            record(['ifconfig'] + args)
            if args == ['lo0', 'inet']:
                print('lo0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST>')
                print('    inet 127.0.0.1 netmask 0xff000000')
                print('    inet 127.0.0.20 netmask 0xffffffff')
                if state.exists(): print('    inet 127.0.0.2 netmask 0xffffffff')
            elif args == ['lo0', 'inet', '127.0.0.2', 'netmask', '255.255.255.255', 'alias']:
                assert not state.exists(), 'preexisting alias must be preserved'
                state.touch()
            elif args == ['lo0', 'inet', '127.0.0.2', '-alias']:
                assert state.exists(), 'cleanup needs an owned alias'
                state.unlink()
            else:
                raise AssertionError(args)
            '''))
        ifconfig.chmod(0o755)
        # Like DarwinPeerAliasLifecycle, execute the actual stdin preflight
        # against alias state instead of Linux's permissive loopback binding.
        (root / 'socket.py').write_text(boundary + 'preflight_failure = ' + repr(preflight_failure) + '\n' +
            textwrap.dedent('''\
                class socket:
                    def __enter__(self): return self
                    def __exit__(self, *args): self.close()
                    def close(self): pass
                    def bind(self, address):
                        assert address in (('127.0.0.1', 0), ('127.0.0.2', 0)), address
                        record(['bind', *address])
                        if address[0] == '127.0.0.2' and (not state.exists() or preflight_failure):
                            raise OSError(49, 'Cannot assign requested address')
                '''))
        python = root / 'tools/python3'
        python.write_text('#!' + sys.executable + '\n' + boundary + textwrap.dedent("""\
            if sys.argv[1:] == ['-']:
                os.execv(sys.executable, [sys.executable, '-'])
            if sys.argv[1] == '-c':
                sys.exit(int(os.environ['PRIVILEGED']))
            if sys.argv[1] == 'scripts/run-v3-release-ci.py':
                assert state.exists(), 'proof requires the second peer source address'
            record(['proof'])
            with open(os.environ['CAPTURE'], 'w') as stream:
                json.dump({'argv': sys.argv[1:], 'toolchain_environment': os.environ.get('V3_PACKAGE_TOOLCHAIN')}, stream)
            """))
        python.chmod(0o755)
        sudo = root / 'tools/sudo'
        # Only ifconfig receives an absolute controlled tool; proof still runs
        # through env -i and the workflow's explicit PATH handoff.
        sudo.write_text('#!/bin/sh\nif [ "$1" = ifconfig ]; then\nshift\nexec env -i ' +
                        shlex.quote(str(ifconfig)) + ' "$@"\nfi\nexec env -i CAPTURE="$CAPTURE" "$@"\n')
        sudo.chmod(0o755)
        result = subprocess.run(['bash', '-e', '-c', block], cwd=ROOT,
            env=dict(env, **selected, PRIVILEGED=str(int(privileged)), CAPTURE=str(root / 'captured.json'),
                     PYTHONPATH=str(root)), capture_output=True, text=True, timeout=10)
        return result, selected, llvm

    def test_workflows_pass_selected_prefix_even_when_sudo_cleans_environment(self):
        for workflow, job, entrypoint in (
                ('v3-package-validation.yml', 'macos', 'scripts/check-v3-installed-consumer.py'),
                ('v3-release-acceptance.yml', 'darwin', 'scripts/run-v3-release-ci.py')):
            for privileged in (False, True):
                with self.subTest(workflow=workflow, privileged=privileged), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    result, selected, llvm = self.execute_selected_workflow(root, workflow, job, entrypoint, privileged)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    captured = json.loads((root / 'captured.json').read_text())
                    argv = captured['argv']
                    self.assertEqual(argv[argv.index('--toolchain-prefix') + 1] if '--toolchain-prefix' in argv else None, str(llvm))
                    self.assertEqual(argv[argv.index('--compiler') + 1], selected['V3_CXX'])
                    self.assertEqual(argv[argv.index('--cc') + 1], selected['V3_CC'])
                    self.assertEqual(argv[argv.index('--make') + 1] if '--make' in argv else None, 'gmake')
                    if privileged:
                        self.assertIsNone(captured['toolchain_environment'])
                    else:
                        self.assertEqual(captured['toolchain_environment'], str(llvm))
                    if workflow == 'v3-release-acceptance.yml':
                        calls = [json.loads(line) for line in (root / 'boundary.jsonl').read_text().splitlines()]
                        self.assertEqual(calls, [
                            ['ifconfig', 'lo0', 'inet'],
                            ['ifconfig', 'lo0', 'inet', '127.0.0.2', 'netmask', '255.255.255.255', 'alias'],
                            ['bind', '127.0.0.1', 0],
                            ['bind', '127.0.0.2', 0],
                            ['proof'],
                            ['ifconfig', 'lo0', 'inet', '127.0.0.2', '-alias'],
                        ])
                        self.assertFalse((root / 'alias').exists())

    def test_failed_actual_peer_bind_prevents_selected_proof_and_cleans_owned_alias(self):
        for privileged in (False, True):
            with self.subTest(privileged=privileged), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                result, _, _ = self.execute_selected_workflow(root, 'v3-release-acceptance.yml',
                    'darwin', 'scripts/run-v3-release-ci.py', privileged, preflight_failure=True)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('both loopback source addresses must bind before proof', result.stderr)
                self.assertFalse((root / 'captured.json').exists(), 'failed preflight must not invoke proof')
                calls = [json.loads(line) for line in (root / 'boundary.jsonl').read_text().splitlines()]
                self.assertIn(['bind', '127.0.0.1', 0], calls)
                self.assertIn(['bind', '127.0.0.2', 0], calls)
                self.assertNotIn(['proof'], calls)
                self.assertEqual(calls[-1], ['ifconfig', 'lo0', 'inet', '127.0.0.2', '-alias'])
                self.assertFalse((root / 'alias').exists())

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
    def test_quoted_fixture_preserves_callers_compound_compiler(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            compiler = root / 'caller compiler.py'
            compiler.write_text(textwrap.dedent('''\
                import json, os, shlex, sys
                assert sys.argv[1:3] == ['--caller-selection', 'argument with spaces'], sys.argv
                with open(os.environ['CALLER_INVOCATIONS'], 'a') as stream:
                    stream.write(json.dumps(sys.argv[3:]) + '\\n')
                command = shlex.split(os.environ['CALLER_CXX']) + sys.argv[3:]
                os.execvp(command[0], command)
                '''))
            env = dict(os.environ, CXX=shlex.join([sys.executable, str(compiler), '--caller-selection', 'argument with spaces']),
                       CALLER_CXX=os.environ.get('CXX') or shlex.join([shutil.which('c++')]),
                       CALLER_INVOCATIONS=str(root / 'calls.jsonl'))
            result = subprocess.run([sys.executable, str(Path(__file__).resolve()),
                                    'InstalledPackageFixtureEnvironment.test_fixture_entrypoint_honors_quoted_cxx_command'],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((root / 'calls.jsonl').exists(), 'quoted fixture bypassed the caller-selected compiler')
            calls = [json.loads(line) for line in (root / 'calls.jsonl').read_text().splitlines()]
            self.assertTrue(any('-fsyntax-only' in call for call in calls))
            self.assertTrue(any('-E' in call for call in calls))

    def test_fixture_entrypoint_honors_quoted_cxx_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            selected = root / 'selected compiler.py'
            selected.write_text("""import json, os, sys
if sys.argv[1] != '--selected-toolchain':
    sys.exit(72)
with open(os.environ['INVOCATIONS'], 'a') as stream:
    stream.write(json.dumps(sys.argv[2:]) + '\\n')
import shlex
command = shlex.split(os.environ['REAL_CXX']) + sys.argv[2:]
os.execvp(command[0], command)
""")
            competing = root / 'c++'
            competing.write_text('#!/bin/sh\nprintf "unselected compiler invoked\\n" >&2\nexit 71\n')
            competing.chmod(0o755)
            real_cxx = shlex.split(os.environ.get('CXX', 'c++'))
            real_cxx[0] = shutil.which(real_cxx[0]) or real_cxx[0]
            env = dict(os.environ, CXX=shlex.join([sys.executable, str(selected), '--selected-toolchain']),
                       PATH=str(root) + os.pathsep + os.environ['PATH'],
                       INVOCATIONS=str(root / 'calls.jsonl'), REAL_CXX=shlex.join(real_cxx))
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
