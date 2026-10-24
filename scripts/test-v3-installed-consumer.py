#!/usr/bin/env python3
"""Package matrix and bounded subprocess regressions (not runtime receipts)."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import socket
import subprocess
import time
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import package_matrix as matrix
import package_commands as commands
import package_integration as integration
import package_process as process
import package_traffic as traffic


@unittest.skipIf(os.name == 'nt', 'POSIX make lookup fixture; native Windows is CI owned')
class InstalledMakeTest(unittest.TestCase):
    """Run the package lane through real Automake dependency bootstrapping."""
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name).resolve()
        cls.gnu_make = shutil.which('gmake') or shutil.which('make')
        version = subprocess.check_output([cls.gnu_make, '--version'], text=True)
        if 'GNU Make' not in version:
            raise RuntimeError('selected-make fixture requires GNU make')
        cls.source = cls.root / 'source'
        cls.source.mkdir()
        (cls.source / 'configure.ac').write_text('''AC_INIT([selected-make], [1.0])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CC
AC_ARG_ENABLE([v3-tls])
AC_ARG_ENABLE([examples])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
''')
        (cls.source / 'Makefile.am').write_text('''noinst_PROGRAMS = probe
probe_SOURCES = probe.c probe.h
all-local:
\t@printf '%s\\n' "$$MAKE" > all-make
install-exec-local:
\t@printf '%s\\n' "$$MAKE" > install-make
check-v3-native-linkage:
\t@printf '%s\\n' "$$MAKE" > check-make
\t@echo selected-make-fixture-complete
\t@exit 23
check-headers check-hygiene check-v3-installed-package-fixtures:
\t@:
''')
        (cls.source / 'probe.c').write_text('#include "probe.h"\nint main(void) { return PROBE; }\n')
        (cls.source / 'probe.h').write_text('#define PROBE 0\n')
        subprocess.run(['autoreconf', '--install', '--force'], cwd=cls.source,
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        spec = importlib.util.spec_from_file_location('installed_make_runner',
            Path(__file__).with_name('check-v3-installed-consumer.py'))
        cls.runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.runner)

    def exercise_selection(self, selection, *, broken_default=False, ambient_make=None):
        root = self.root / self._testMethodName
        root.mkdir()
        tools = root / 'tools'
        tools.mkdir()
        bad_make = tools / 'unselected-make'
        bad_make.write_text('#!/bin/sh\necho unselected-make >&2\nexit 1\n')
        bad_make.chmod(0o755)
        # Apple's developer-tool frontend dispatches by its invoked name.
        for name, target in (('make', bad_make if broken_default else self.gnu_make),
                             ('gmake', self.gnu_make)):
            launcher = tools / name
            launcher.write_text('#!/bin/sh\nexec ' + shlex.quote(str(target)) + ' "$@"\n')
            launcher.chmod(0o755)
        args = SimpleNamespace(make=selection, compiler=shutil.which('c++'), cc=shutil.which('cc'),
            jobs=1, prerequisite_prefix=[], pkg_config_dir=[], provider_prefix=root,
            toolchain_prefix=None, optimized=False)
        environment = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ['PATH'])
        environment.pop('MAKE', None)
        if ambient_make:
            environment['MAKE'] = str(bad_make)
        # Stop at the fixture's check target, after real configure/build/install.
        with patch.dict(os.environ, environment, clear=True), \
                self.assertRaisesRegex(RuntimeError, 'selected-make-fixture-complete'):
            self.runner.package_lane(args, self.source, root, 'no', matrix.load())
        build = root / 'tls-no/build'
        self.assertTrue((build / '.deps/probe.Po').is_file())
        self.assertIn('probe.h', (build / '.deps/probe.Po').read_text())
        for phase in ('all', 'install', 'check'):
            self.assertEqual((build / (phase + '-make')).read_text().strip(), selection)

    def test_selected_make_bootstraps_dependencies_with_unusable_default(self):
        self.exercise_selection('gmake', broken_default=True)

    def test_absolute_make_overrides_ambient_make(self):
        self.exercise_selection(self.gnu_make, broken_default=True, ambient_make=True)

    def test_default_make_overrides_ambient_make(self):
        self.exercise_selection('make', ambient_make=True)



@unittest.skipIf(os.name == 'nt', 'POSIX executable fixture; native Windows is CI owned')
class CompiledOperationsFixture(unittest.TestCase):
    """Execute the checker with a controlled symbol-producer boundary."""
    # Independent examples of the existing compiled-definition contract.
    symbols = (
        'httpserver::http::fields::append(',
        'httpserver::server::server_options::validate(',
        'httpserver::server::resource_budget::reserve(',
        'httpserver::server::route_pattern::parse(',
        'httpserver::server::make_sync_route(',
        'httpserver::manual_executor::run_one(',
        'httpserver::detail::current_executor_slot(',
        'httpserver::detail::task_frame_base::finish(',
        'httpserver::stop_token::cancellation_awaiter::await_suspend(',
        'httpserver::detail::fire_trigger(',
        'httpserver::exchange::admit_body(',
        'httpserver::body_reader::collect(',
        'httpserver::response_writer::finish(',
        'httpserver::response_definition::owned_bytes(',
        'httpserver::send_definition(',
        'httpserver::detail::send_cursor::pull(',
        'httpserver::detail::http1_head_parser::',
        'httpserver::detail::hpack_decoder::',
        'httpserver::detail::qpack_decoder::',
        'httpserver::websocket::options::validate(',
        'httpserver::detail::http2_dispatch::post(',
        'httpserver::detail::valid_http1_host(',
        'httpserver::detail::unescape_buf_raw(',
        'httpserver::detail::owned_completion_storage::claim_read(',
    )

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.checker = Path(__file__).with_name('check-v3-compiled-operations.py').resolve()
        self.selected = self.root / "selected tools' directory" / 'symbol tool'
        self.selected.parent.mkdir()
        self.calls = self.root / 'selected.json'
        self.default_calls = self.root / 'default-used'
        self.default = self.root / ('nm.exe' if os.name == 'nt' else 'nm')
        self.write_tool(self.default, f'from pathlib import Path; Path({str(self.default_calls)!r}).touch(); raise SystemExit(91)')
        self.archive = self.root / 'installed archive.a'
        self.archive.write_bytes(b'archive identity fixture')
        self.environment = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ['PATH'])
        self.selection = shlex.join([str(self.selected), '-B', '--selected-option'])
        self.producer()

    def write_tool(self, path, code):
        path.write_text('#!' + sys.executable + '\n' + code + '\n')
        path.chmod(0o755)

    def producer(self, symbols=None, status=0):
        text = '\n'.join('00000000 T ' + name for name in self.symbols) if symbols is None else symbols
        self.write_tool(self.selected, 'import json,sys; from pathlib import Path; '
            f'Path({str(self.calls)!r}).write_text(json.dumps(sys.argv[1:])); '
            f'print({text!r}); raise SystemExit({status})')

    def check(self, selection=None):
        return subprocess.run([sys.executable, str(self.checker), '--nm=' + (self.selection if selection is None else selection),
                               str(self.archive)], env=self.environment, text=True, capture_output=True)


class CompiledOperationsToolTest(CompiledOperationsFixture):
    def test_selected_command_preserves_options_spaces_demangling_and_archive(self):
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.calls.read_text()), ['-B', '--selected-option', '-C', str(self.archive)])
        self.assertFalse(self.default_calls.exists())

    def test_omitted_selection_preserves_default_nm_cli(self):
        self.producer()
        shutil.copyfile(self.selected, self.default)
        result = subprocess.run([sys.executable, str(self.checker), str(self.archive)],
                                env=self.environment, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.calls.read_text()), ['-C', str(self.archive)])

    def test_missing_or_undefined_definition_remains_fatal(self):
        for kind in ('missing', 'undefined', 'lowercase'):
            with self.subTest(kind=kind):
                lines = ['00000000 T ' + name for name in self.symbols[1:]]
                if kind != 'missing':
                    lines.append(('         U ' if kind == 'undefined' else '00000000 t ') + self.symbols[0])
                self.producer('\n'.join(lines))
                result = self.check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Missing compiled library operations:\n' + self.symbols[0], result.stderr)
                self.assertFalse(self.default_calls.exists())

    def test_nonzero_selected_tool_is_fatal_even_with_complete_definitions(self):
        self.producer(status=23)
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.calls.exists(), result.stderr)
        self.assertIn('23', result.stderr)
        self.assertFalse(self.default_calls.exists())

    def test_empty_output_remains_missing_definitions(self):
        self.producer('')
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing compiled library operations', result.stderr)
        self.assertFalse(self.default_calls.exists())

    def test_empty_malformed_or_unavailable_selection_never_uses_default(self):
        for selection in ('', '  ', "''", "'unclosed", str(self.root / 'absent-tool')):
            with self.subTest(selection=selection):
                result = self.check(selection)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.calls.exists())
                self.assertFalse(self.default_calls.exists())


@unittest.skipIf(os.name == 'nt', 'POSIX Autotools fixture; Windows is CI owned')
class ConfiguredSymbolEntryPointsTest(CompiledOperationsFixture):
    """Real generated Makefiles and lane subprocesses; no full library build."""
    def configure(self, source, build, environment):
        result = subprocess.run(['bash', str(source / 'configure'), '--disable-v3-tls', '--disable-examples',
                                 '--prefix=' + str(self.root / 'matrix/tls-no/prefix')],
                                cwd=build, env=environment, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_generated_owning_make_target_uses_configured_nm(self):
        source = Path(__file__).resolve().parent.parent
        build = self.root / 'build'
        build.mkdir()
        self.configure(source, build, dict(self.environment, NM=self.selection))
        result = subprocess.run([shutil.which('gmake') or shutil.which('make'), '-o', 'src/libhttpserver.la',
            'check-v3-compiled-operations'], cwd=build, env=self.environment,
            text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(self.calls.read_text()),
                         ['-B', '--selected-option', '-C', './src/.libs/libhttpserver.a'])
        self.assertFalse(self.default_calls.exists())

    def exercise_lane(self, *, missing=False, selection=None, error='23'):
        source = self.root / 'source'
        source.mkdir()
        # A small genuine configured project reaches the existing package lane
        # without replacing its compile, dependency inspection or symbol check.
        (source / 'configure.ac').write_text('AC_INIT([symbol-lane], [1])\n'
            'AM_INIT_AUTOMAKE([foreign])\nAC_PROG_CC\n'
            'AC_ARG_ENABLE([v3-tls])\nAC_ARG_ENABLE([examples])\n' +
            ('' if missing else 'AC_SUBST([NM], [' + shlex.quote(self.selection if selection is None else selection) + '])\n') +
            'AC_CONFIG_FILES([Makefile])\nAC_OUTPUT\n')
        (source / 'Makefile.am').write_text(('' if missing else 'NM = @NM@\n') +
            'all-local:\n\t$(CC) -c $(srcdir)/fixture.c -o fixture.o\n\tar cr libhttpserver.a fixture.o\n'
            'install-exec-local:\n\tmkdir -p $(prefix)/lib/pkgconfig\n'
            '\tcp libhttpserver.a $(prefix)/lib/libhttpserver.a\n'
            '\tprintf "Name: libhttpserver\\nDescription: fixture\\nVersion: 1\\nLibs: -lhttpserver\\n" > $(prefix)/lib/pkgconfig/libhttpserver.pc\n'
            'check-v3-native-linkage check-headers check-hygiene check-v3-installed-package-fixtures:\n\t@:\n')
        (source / 'fixture.c').write_text('int fixture(void) { return 1; }\n')
        for name, content in (('package_consumer.cpp', 'int main() { return 0; }\n'),
                              ('compiled_operations.cpp', 'int main() { return 0; }\n'),
                              ('compiled_operations_worker.cpp', 'int worker() { return 0; }\n')):
            path = source / 'test/installed' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        (source / 'examples').mkdir()
        (source / 'examples/host_poll.hpp').write_text('// fixture\n')
        (source / 'scripts').mkdir()
        shutil.copyfile(self.checker, source / 'scripts' / self.checker.name)
        generated = subprocess.run(['autoreconf', '--install', '--force'], cwd=source,
                                    text=True, capture_output=True)
        self.assertEqual(generated.returncode, 0, generated.stdout + generated.stderr)
        output = self.root / 'matrix'
        output.mkdir()
        spec = importlib.util.spec_from_file_location('configured_symbol_lane', self.checker.with_name('check-v3-installed-consumer.py'))
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        args = SimpleNamespace(compiler=shutil.which('c++'), cc=shutil.which('cc'),
            make=shutil.which('gmake') or shutil.which('make'), jobs=1, prerequisite_prefix=[],
            pkg_config_dir=[], provider_prefix=self.root, toolchain_prefix=None, optimized=False)
        inventory = matrix.load()
        inventory.update(linkages=['static'], loops=[])
        self.producer(status=23)  # Halt at the selected inspector with real failure evidence.
        with patch.dict(os.environ, dict(self.environment, NM='unrelated-ambient-nm'), clear=True):
            with self.assertRaisesRegex((RuntimeError, ValueError), error):
                runner.package_lane(args, source, output, 'no', inventory)
        self.assertFalse(self.default_calls.exists())
        return output

    def test_installed_lane_uses_generated_selection_over_ambient_nm(self):
        output = self.exercise_lane()
        self.assertEqual(json.loads(self.calls.read_text()), ['-B', '--selected-option', '-C',
            str(output / 'tls-no/prefix/lib/libhttpserver.a')])
        self.assertEqual((output / 'tls-no/prefix/lib/libhttpserver.a').read_bytes(),
                         (output / 'tls-no/build/libhttpserver.a').read_bytes())

    def test_installed_lane_missing_configured_nm_fails_without_default(self):
        self.exercise_lane(missing=True, error='configured NM')
        self.assertFalse(self.calls.exists())

    def test_installed_lane_empty_configured_nm_fails_without_default(self):
        self.exercise_lane(selection='', error='NM command is empty')
        self.assertFalse(self.calls.exists())


class InstalledShellTest(unittest.TestCase):
    """Controlled process boundary; these are not native Windows receipts."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.selected = self.root / 'MSYS tools'
        self.selected.mkdir()
        self.bash = self.selected / ('bash.exe' if os.name == 'nt' else 'bash')
        self.bash.write_text('selected shell fixture')
        self.bash.chmod(0o755)
        self.output = self.root / 'evidence'
        self.output.mkdir()
        spec = importlib.util.spec_from_file_location('installed_shell_runner', Path(__file__).with_name('check-v3-installed-consumer.py'))
        self.runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.runner)
        self.args = SimpleNamespace(compiler='selected-cxx', cc='selected-cc', make='make',
            prerequisite_prefix=[], pkg_config_dir=[], provider_prefix=self.root, optimized=False)
        self.popen = subprocess.Popen
        self.converted_configure = (self.root / 'configure').as_posix()
        self.conversion_requests = []

    def create_process(self, argv, **options):
        if str(argv[0]) not in ('bash', 'bash.exe', str(self.bash)):
            return self.popen(argv, **options)
        if argv[1:3] == ['-c', 'cygpath -u "$1"']:
            self.conversion_requests.append(argv[-1])
            return self.popen([sys.executable, '-c', f'print({self.converted_configure!r})'], **options)
        # Model only executable lookup at the OS boundary: unqualified bash
        # selects the competing Windows system shell before the selected PATH.
        selected = str(argv[0]) == str(self.bash)
        message = 'selected MSYS shell' if selected else 'Windows Subsystem for Linux has no installed distributions.'
        return self.popen([sys.executable, '-c', f'print({message!r}); raise SystemExit(23)'], **options)

    def test_windows_configure_uses_absolute_bash_from_selected_path(self):
        with patch.object(self.runner.platform, 'system', return_value='Windows'), \
                patch.dict(os.environ, PATH=str(self.selected), CPPFLAGS='unselected-includes'), \
                patch.object(subprocess, 'Popen', side_effect=self.create_process):
            with self.assertRaisesRegex(RuntimeError, 'selected MSYS shell'):
                self.runner.package_lane(self.args, self.root, self.output, 'no', matrix.load())
        record = json.loads((self.output / 'tls-no/commands.json').read_text())[-1]
        self.assertEqual(record['argv'][0], str(self.bash))
        self.assertEqual(record['argv'][1], (self.root / 'configure').as_posix())
        self.assertEqual(record['environment']['PATH'], str(self.selected))
        self.assertEqual(record['environment']['CXX'], 'selected-cxx')
        self.assertEqual(record['environment']['CC'], 'selected-cc')
        self.assertEqual(record['environment']['CPPFLAGS'], '')

    def test_windows_configure_uses_msys_source_path_without_changing_native_prefix(self):
        source = self.root / 'D:' / 'selected source'
        self.converted_configure = '/d/selected source/configure'
        with patch.object(self.runner.platform, 'system', return_value='Windows'), \
                patch.dict(os.environ, PATH=str(self.selected)), \
                patch.object(subprocess, 'Popen', side_effect=self.create_process):
            with self.assertRaisesRegex(RuntimeError, 'selected MSYS shell'):
                self.runner.package_lane(self.args, source, self.output, 'no', matrix.load())
        records = json.loads((self.output / 'tls-no/commands.json').read_text())
        self.assertEqual(records[-1]['argv'][1], self.converted_configure)
        self.assertEqual(self.conversion_requests, [(source / 'configure').as_posix()])
        self.assertEqual(records[-1]['argv'][-1], '--prefix=' + (self.output / 'tls-no/prefix').as_posix())

    def test_windows_missing_path_bash_fails_before_launching_competing_shell(self):
        self.bash.unlink()
        with patch.object(self.runner.platform, 'system', return_value='Windows'), \
                patch.dict(os.environ, PATH=str(self.selected)), \
                patch.object(subprocess, 'Popen', side_effect=self.create_process):
            with self.assertRaisesRegex(ValueError, 'bash.*PATH'):
                self.runner.package_lane(self.args, self.root, self.output, 'no', matrix.load())
        self.assertFalse((self.output / 'tls-no/commands.json').exists())


@unittest.skipIf(os.name == 'nt', 'ps observation uses POSIX process groups; Windows exercises native jobs')
class InstalledShellPSTest(InstalledShellTest):
    """Replay shell contracts with a temporary path alias and real ps observation."""
    def setUp(self):
        outer = tempfile.TemporaryDirectory()
        self.addCleanup(outer.cleanup)
        root = Path(outer.name).resolve()
        real = root / 'real'
        real.mkdir()
        alias = root / 'alias'
        alias.symlink_to(real, target_is_directory=True)
        with patch.object(tempfile, 'tempdir', str(alias)):
            super().setUp()
        observation = patch.object(commands.sys, 'platform', 'darwin')
        observation.start()
        self.addCleanup(observation.stop)


class InstalledInspectionFailureTest(unittest.TestCase):
    """Real lane/compile/inspector subprocesses with controlled producer tools.

    Inspector text is deliberately unrecognized, not a FreeBSD format claim.
    """
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.output = self.root / 'matrix'
        self.output.mkdir()
        self.source = self.root / 'source'
        (self.source / 'test/installed').mkdir(parents=True)
        (self.source / 'test/installed/package_consumer.cpp').write_text('int main() { return 0; }\n')
        (self.source / 'examples').mkdir()
        (self.source / 'examples/host_poll.hpp').write_text('// fixture\n')
        spec = importlib.util.spec_from_file_location('installed_inspection_runner',
            Path(__file__).with_name('check-v3-installed-consumer.py'))
        self.runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.runner)
        self.args = SimpleNamespace(compiler=os.environ.get('CXX', 'c++'), cc='cc', make='fixture-make',
            jobs=1, prerequisite_prefix=[], pkg_config_dir=[], provider_prefix=self.root / 'provider',
            toolchain_prefix=None, optimized=False)
        self.dynamic = 'Dynamic section with unrecognized dependency syntax\n' + 'raw dynamic entry\n' * 400
        self.diagnostic = 'inspector diagnostic\n'
        self.inspector_exit = 0
        self.popen = subprocess.Popen
        self.bash = Path(shutil.which('bash')).resolve()
        self.converted_configure = '/fixture source/configure'
        environment = patch.dict(os.environ)
        environment.start()
        self.addCleanup(environment.stop)

    def create_process(self, argv, **options):
        if argv[0] in ('bash', str(self.bash)):
            if argv[1:3] == ['-c', 'cygpath -u "$1"']:
                code = f'print({self.converted_configure!r})'
            else:
                prefix = self.output / 'tls-no/prefix/lib/pkgconfig'
                code = f'from pathlib import Path; p=Path({str(prefix)!r}); p.mkdir(parents=True); (p/"libhttpserver.pc").write_text("fixture metadata")'
        elif argv[0] == 'fixture-make':
            code = ''
        elif argv[0] == 'pkg-config':
            value = str(self.output / 'tls-no/prefix/lib/pkgconfig') if '--variable=pcfiledir' in argv else ''
            code = f'print({value!r})'
        elif argv[0] == 'readelf':
            code = f'import sys; sys.stdout.write({self.dynamic!r}); sys.stderr.write({self.diagnostic!r}); sys.exit({self.inspector_exit})'
        else:
            return self.popen(argv, **options)
        return self.popen([sys.executable, '-c', code], **options)

    def fail_lane(self):
        inspection_platform = SimpleNamespace(system=lambda: 'FreeBSD',
            platform=self.runner.audit.platform.platform)
        with patch.object(self.runner.audit, 'platform', inspection_platform), \
                patch.object(subprocess, 'Popen', side_effect=self.create_process):
            with self.assertRaises(self.runner.audit.AuditError) as caught:
                self.runner.package_lane(self.args, self.source, self.output, 'no', matrix.load())
        return caught.exception

    def test_forced_inspection_host_preserves_native_compilation(self):
        native_host = integration.platform.system()
        compile_hosts = []
        real_compile = self.runner.compile_probe

        def compile_probe(*args, **kwargs):
            compile_hosts.append((integration.platform.system(), self.runner.audit.platform.system()))
            return real_compile(*args, **kwargs)

        with patch.object(self.runner, 'compile_probe', side_effect=compile_probe):
            error = self.fail_lane()

        self.assertEqual(compile_hosts, [(native_host, 'FreeBSD')])
        self.assertEqual(str(error), 'empty ELF dependency inspection')
        consumer = self.output / ('tls-no/consumer/package_consumer' + ('.exe' if os.name == 'nt' else ''))
        self.assertTrue(consumer.is_file())
        self.assertTrue(Path(str(consumer) + '.map').is_file())

    def test_early_graph_failure_persists_full_output_and_failed_consumer(self):
        self.runner.audit.COMMANDS.append({'argv': ['stale-previous-lane-command']})
        error = self.fail_lane()
        self.assertEqual(str(error), 'empty ELF dependency inspection')
        receipt_path = self.output / 'tls-no/audit-failure.json'
        self.assertTrue(receipt_path.is_file(), 'early graph failure must persist inspection evidence')
        receipt = json.loads(receipt_path.read_text())
        consumer = self.output / ('tls-no/consumer/package_consumer' + ('.exe' if os.name == 'nt' else ''))
        self.assertEqual(receipt['violations'], [str(error)])
        self.assertEqual(receipt['failed_artifact'], dict(id=str(consumer),
            sha256=hashlib.sha256(consumer.read_bytes()).hexdigest()))
        command = receipt['commands'][-1]
        self.assertEqual(len(receipt['commands']), 1)
        self.assertEqual(command['argv'], ['readelf', '-d', str(consumer)])
        self.assertEqual(command['returncode'], 0)
        self.assertEqual(command['stdout'], self.dynamic)
        self.assertEqual(command['stderr'], self.diagnostic)
        selected_tool = shutil.which('readelf')
        if selected_tool:
            self.assertEqual(command['resolved_tool'], str(Path(selected_tool).resolve()))
        else:
            self.assertNotIn('resolved_tool', command)
        self.assertFalse((self.output / 'tls-no/audit.json').exists())

    def test_nonzero_inspector_remains_failed_with_actual_exit_evidence(self):
        self.inspector_exit = 7
        error = self.fail_lane()
        self.assertIn('command failed: readelf', str(error))
        receipt_path = self.output / 'tls-no/audit-failure.json'
        self.assertTrue(receipt_path.is_file(), 'nonzero inspector must persist its actual result')
        receipt = json.loads(receipt_path.read_text())
        self.assertEqual(receipt['violations'], [str(error)])
        self.assertEqual(receipt['commands'][-1]['returncode'], 7)
        self.assertEqual(receipt['commands'][-1]['stdout'], self.dynamic)
        self.assertEqual(receipt['commands'][-1]['stderr'], self.diagnostic)

    def test_failure_receipt_write_error_keeps_original_graph_failure(self):
        real_write = Path.write_text
        def write(path, *args, **kwargs):
            if path.name == 'audit-failure.json':
                raise OSError('evidence disk unavailable')
            return real_write(path, *args, **kwargs)
        with patch.object(Path, 'write_text', write):
            error = self.fail_lane()
        self.assertEqual(str(error), 'empty ELF dependency inspection')
        self.assertEqual(error.__notes__, ['could not persist audit failure evidence: evidence disk unavailable'])
        self.assertEqual(self.runner.audit.COMMANDS[-1]['stdout'], self.dynamic)


class InstalledInspectionWindowsShellTest(InstalledInspectionFailureTest):
    """Selected Windows shell branch with real host compilation and recording.

    The shell/converter outputs are controlled; this is not a Windows receipt.
    """
    def setUp(self):
        super().setUp()
        selected = self.root / 'selected tools'
        selected.mkdir()
        self.bash = selected / ('bash.exe' if os.name == 'nt' else 'bash')
        self.bash.write_text('controlled producer shell')
        self.bash.chmod(0o755)
        self.runner.platform = SimpleNamespace(system=lambda: 'Windows')
        os.environ['PATH'] = str(selected) + os.pathsep + os.environ['PATH']

    def fail_lane(self):
        error = super().fail_lane()
        records = json.loads((self.output / 'tls-no/commands.json').read_text())
        self.assertEqual(records[0]['argv'], [str(self.bash), '-c', 'cygpath -u "$1"',
            'configure-path', (self.source / 'configure').as_posix()])
        self.assertEqual(records[0]['exit'], 0)
        self.assertEqual(records[1]['argv'][0:2], [str(self.bash), self.converted_configure])
        self.assertEqual(records[1]['exit'], 0)
        return error


class MatrixTest(unittest.TestCase):
    def setUp(self):
        self.inventory = matrix.load()
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def test_optimized_package_configure_selects_release_flags_without_changing_default(self):
        spec = importlib.util.spec_from_file_location('installed_runner', Path(__file__).with_name('check-v3-installed-consumer.py'))
        runner = importlib.util.module_from_spec(spec); spec.loader.exec_module(runner)
        with tempfile.TemporaryDirectory() as temp:
            self.exercise_configure_flags(runner, Path(temp).resolve())

    def test_configure_flags_receipt_follows_successful_path_conversion(self):
        spec = importlib.util.spec_from_file_location('installed_flags_runner',
            Path(__file__).with_name('check-v3-installed-consumer.py'))
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        runner.platform = SimpleNamespace(system=lambda: 'Windows')
        popen = subprocess.Popen

        def create_process(argv, **options):
            if argv[1:3] == ['-c', 'cygpath -u "$1"']:
                configure = Path(os.path.relpath(argv[-1], options['cwd'])).as_posix()
                return popen([sys.executable, '-c', f'print({configure!r})'], **options)
            return popen(argv, **options)

        with tempfile.TemporaryDirectory() as temp, \
                patch.object(subprocess, 'Popen', side_effect=create_process):
            self.exercise_configure_flags(runner, Path(temp).resolve())

    def exercise_configure_flags(self, runner, root):
        (root / 'configure').write_text('printf "%s\\n" "$CXXFLAGS" > selected-flags\nexit 23\n')
        for optimized, flags in ((False, '-O0 -g'), (True, '-O3 -g')):
            with self.subTest(optimized=optimized):
                output = root / str(optimized); output.mkdir()
                args = SimpleNamespace(compiler='selected-cxx', cc='selected-cc', make='make',
                    prerequisite_prefix=[], pkg_config_dir=[], provider_prefix=root,
                    optimized=optimized)
                with self.assertRaises(RuntimeError):
                    runner.package_lane(args, root, output, 'no', self.inventory)
                self.assertEqual((output / 'tls-no/build/selected-flags').read_text().strip(), flags)
                records = json.loads((output / 'tls-no/commands.json').read_text())
                configure = [record for record in records if record['argv'][2:] == [
                    '--enable-v3-tls=no', '--disable-examples',
                    '--prefix=' + (output / 'tls-no/prefix').as_posix()]]
                self.assertEqual(len(configure), 1)
                self.assertEqual(configure[0]['exit'], 23)
                self.assertEqual(configure[0]['environment']['CXXFLAGS'], flags)
                self.assertEqual(records[-1], configure[0])
                if runner.platform.system() == 'Windows':
                    self.assertEqual(len(records), 2)
                    conversion = records[0]
                    self.assertEqual(conversion['argv'], [configure[0]['argv'][0], '-c',
                        'cygpath -u "$1"', 'configure-path', (root / 'configure').as_posix()])
                    self.assertEqual(conversion['exit'], 0)
                    self.assertEqual(Path(conversion['log']).read_text().strip(), configure[0]['argv'][1])
                else:
                    self.assertEqual(records, configure)

    def test_complete_matrix_and_identical_cases(self):
        cells = matrix.cells(self.inventory, 'Linux')
        self.assertEqual(8, len(cells))
        self.assertEqual(8, len({tuple(c[k] for k in ('tls', 'linkage', 'loop')) for c in cells}))
        self.assertTrue(all(c['cases'] == self.inventory['cases'] for c in cells))

    def test_missing_duplicate_or_wrong_platform_fails(self):
        for key in ('tls', 'linkages', 'loops'):
            for value in ([], ['no', 'no']):
                bad = copy.deepcopy(self.inventory)
                bad[key] = value
                with self.assertRaises(ValueError):
                    matrix.validate(bad)
        for platform in ('Linux', 'FreeBSD', 'Darwin', 'Windows'):
            bad = copy.deepcopy(self.inventory)
            del bad['platforms'][platform]
            with self.assertRaises(ValueError):
                matrix.validate(bad)
        with self.assertRaises(ValueError):
            matrix.cells(self.inventory, 'MSYS')

    def test_public_tls_inventory_requires_every_case_once(self):
        for value in ([], ['sni_a'], ['sni_a', 'sni_a']):
            bad = copy.deepcopy(self.inventory)
            bad['public_tls_scenarios'] = value
            with self.assertRaises(ValueError):
                matrix.validate(bad)

    def test_public_tls_result_rejects_missing_duplicate_and_unknown_cases(self):
        expected = ['a', 'b']
        matrix.check_scenarios([{'case': 'a'}, {'case': 'b'}], expected)
        for rows in ([], [{'case': 'a'}], [{'case': 'a'}, {'case': 'a'}],
                     [{'case': 'a'}, {'case': 'b'}, {'case': 'unknown'}]):
            with self.assertRaises(ValueError):
                matrix.check_scenarios(rows, expected)

    def test_public_tls_failed_cell_cannot_claim_inventory_success(self):
        for row in ({'case': 'a', 'status': 'failed'}, {'case': 'a', 'passed': False}):
            with self.assertRaises(ValueError):
                matrix.check_scenarios([row], ['a'])

    def test_duplicate_inventory_keys_cannot_hide_platform_cells(self):
        with self.assertRaises(ValueError):
            matrix.parse_inventory('{"platforms":{"Linux":"epoll","Linux":"poll"}}')

    def test_required_suites_are_platform_specific(self):
        names = matrix.suites(self.inventory, 'Windows', 'yes')
        self.assertTrue({'io_iocp_backend', 'io_completion_storage', 'external_http1_e2e',
                         'tls_io_loopback', 'native_http1_tls_parity'} <= set(names))
        self.assertNotIn('io_epoll_backend', names)
        self.assertNotIn('tls_io_loopback', matrix.suites(self.inventory, 'Linux', 'no'))

    def test_empty_skipped_failed_suite_is_rejected(self):
        good = 'Running test (1): a\nRunning test (2): b\nRunning test (3): c\n3 tests executed\n9 checks\n-> 9 successes\n-> 0 failures\n-> 0 skipped\n'
        self.assertEqual(3, matrix.suite_result(good, 0)['tests'])
        for output, status in ((good, 77), (good, 1), ('', 0), (good.replace('Running test (3): c\n', ''), 0),
                               (good.replace('3 tests', '0 tests'), 0),
                               (good.replace('0 skipped', '1 skipped'), 0),
                               (good.replace('0 failures', '1 failures'), 0)):
            with self.assertRaises(ValueError):
                matrix.suite_result(output, status)

    def test_shared_suite_requires_installed_library_and_static_forbids_it(self):
        prefix = self.root / 'prefix'
        consumer = {'id': str(self.root / 'consumer')}
        graph = {'nodes': [{'id': str(prefix / 'lib/libhttpserver.so')}, consumer], 'edges': []}
        matrix.check_linkage(graph, 'shared', prefix)
        with self.assertRaises(ValueError):
            matrix.check_linkage(graph, 'shared', self.root / 'elsewhere')
        with self.assertRaises(ValueError):
            matrix.check_linkage(graph, 'static', prefix)
        matrix.check_linkage({'nodes': [consumer], 'edges': []}, 'static', prefix)

    def test_public_header_contamination_is_rejected_in_private_probes(self):
        prefix, source, build = (self.root / name for name in ('prefix', 'source', 'build'))
        matrix.check_dependencies([str(prefix / 'include/httpserver.hpp'), str(source / 'src/httpserver/detail/io.hpp')],
                                  prefix, source, build, private=True)
        for private in (False, True):
            with self.assertRaises(ValueError):
                matrix.check_dependencies([str(source / 'src/httpserver/server/server.hpp')],
                                          prefix, source, build, private)

    def test_missing_configuration_case_or_different_outcome_is_rejected(self):
        for tls in ('yes', 'no'):
            output = '\n'.join('PACKAGE_CASE ' + case + ' ' +
                ('ok bound' if case == 'plaintext' else
                 'invalid_argument unbound' if case in ('tls_listener', 'http2') and tls == 'yes' else
                 'not_supported unbound' if case in ('tls_listener', 'http2') else
                 'invalid_argument unbound') for case in self.inventory['cases'])
            for loop in ('managed', 'external'):
                self.assertEqual(len(self.inventory['cases']),
                                 len(matrix.configuration_results(output, self.inventory, loop, tls=tls)))
                for broken in (output.replace('PACKAGE_CASE http3', 'MISSING http3'),
                               output.replace('invalid_argument unbound', 'ok bound')):
                    with self.assertRaises(ValueError):
                        matrix.configuration_results(broken, self.inventory, loop, tls=tls)

    def test_credentials_backed_supported_configuration_expectations(self):
        for tls in ('yes', 'no'):
            rows = '\n'.join('PACKAGE_SUPPORTED ' + name + (' ok bound' if tls == 'yes' else ' not_supported unbound')
                             for name in ('tls', 'h2', 'h3'))
            self.assertEqual(len(matrix.supported_configuration_results(rows, tls)), 3)
            for bad in ('', rows + '\n' + rows, rows.replace('bound', 'invalid')):
                with self.assertRaises(ValueError): matrix.supported_configuration_results(bad, tls)

    def test_private_external_probe_can_use_the_named_host_helper_only(self):
        prefix, source, build = (self.root / name for name in ('prefix', 'source', 'build'))
        matrix.check_dependencies([str(source / 'examples/host_poll.hpp')], prefix,
                                  source, build, private=True)
        with self.assertRaises(ValueError):
            matrix.check_dependencies([str(source / 'examples/other.hpp')], prefix,
                                      source, build, private=True)

    def test_parity_backend_replays_preserve_unique_release_observations(self):
        case = dict(id='h1.1-tls:routing.tseq:hello', case='routing.tseq:hello', mode='h1.1-tls')
        rows = ['TASK179_OBSERVATION ' + json.dumps(dict(case, driver='managed'))]
        rows += ['PACKAGE_OBSERVATION ' + json.dumps(dict(case, driver=driver)) for driver in ('external', 'poll-oracle')]
        self.assertEqual({'managed': 1, 'external': 1, 'poll-oracle': 1}, matrix.parity_observation_counts('\n'.join(rows)))
        for bad in (rows[:1], rows + rows[:1], rows[:2]):
            with self.assertRaises(ValueError):
                matrix.parity_observation_counts('\n'.join(bad))

    def test_backend_tls_witnesses_required(self):
        for output in ('', 'PACKAGE_BACKEND poll\n', 'PACKAGE_BACKEND epoll\n'):
            with self.assertRaises(ValueError):
                matrix.check_tls_witnesses(output, 'epoll')
        matrix.check_tls_witnesses('PACKAGE_BACKEND epoll\nPACKAGE_TLS managed\nPACKAGE_TLS external\n', 'epoll')


class TrafficTest(unittest.TestCase):
    def test_public_cells_retain_distinct_process_receipts(self):
        with patch.object(traffic, 'Consumer', side_effect=RuntimeError('intercept launch')) as launch:
            for linkage in ('shared', 'static'):
                for loop in ('managed', 'external'):
                    with self.assertRaisesRegex(RuntimeError, 'intercept launch'):
                        traffic.traffic(Path('/consumer/' + linkage), dict(tls='no', linkage=linkage, loop=loop),
                                        Path('/consumer'), {}, matrix.load())
        receipts = [call.args[3] for call in launch.call_args_list]
        self.assertEqual(4, len(set(receipts)))

    def connection(self, chunks):
        class Connection:
            def __init__(self):
                self.calls = 0
            def recv(self, size):
                self.calls += 1
                if self.calls > len(chunks):
                    raise AssertionError('response reader repeated EOF')
                return chunks[self.calls - 1]
        return Connection()

    def test_truncated_header_and_body_rejected(self):
        for chunks in ((b'HTTP/1.1 200 OK\r\nContent-', b''),
                       (b'HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nx', b'')):
            with self.assertRaises(RuntimeError):
                traffic.response(self.connection(chunks))

    def test_pipeline_leftover_preserved(self):
        response = b'HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx'
        body, pending, _ = traffic.response(self.connection([response + response]))
        self.assertEqual(b'x', body)
        self.assertEqual(response, pending)

    def test_negative_or_oversized_body_rejected(self):
        for size in ('-1', '20000'):
            with self.assertRaises(RuntimeError):
                traffic.response(self.connection([b'HTTP/1.1 200 OK\r\nContent-Length: ' + size.encode() + b'\r\n\r\n']))


class CommandsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.runner = commands.Commands(self.root)

    def command(self, code):
        return [sys.executable, '-u', '-c', code]

    def assert_descendant_retired(self, parent_code, timeout):
        ready = self.root / 'descendant.json'
        worker = ("import json, os, socket, time; from pathlib import Path; "
                  "s=socket.socket(); s.bind(('127.0.0.1', 0)); "
                  "Path('descendant.json').write_text(json.dumps([os.getpid(), s.getsockname()[1]])); "
                  "time.sleep(30)")
        code = ("import subprocess, sys, time; from pathlib import Path; "
                f"subprocess.Popen({self.command(worker)!r}); "
                "\nwhile not Path('descendant.json').exists(): time.sleep(.01)\n" + parent_code)
        retired = False
        try:
            with self.assertRaisesRegex(RuntimeError, 'deadline/output budget exceeded'):
                self.runner(self.command(code), self.root, os.environ, timeout=timeout)
            self.assertTrue(ready.exists(), 'descendant must start before command failure')
            _, port = json.loads(ready.read_text())
            with socket.socket() as probe:
                try:
                    probe.bind(('127.0.0.1', port))
                except OSError:
                    self.fail('descendant retained its socket after command failure')
            retired = True
            receipt = json.loads((self.root / 'commands.json').read_text())[0]
            self.assertIn('deadline/output budget exceeded', receipt['error'])
            self.assertTrue(Path(receipt['log']).exists())
        finally:
            # Keep RED runs from leaking the deliberately long-lived worker.
            if not retired and ready.exists():
                pid, _ = json.loads(ready.read_text())
                try:
                    os.kill(pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass

    def test_timeout_retires_descendants_and_retains_failure_receipt(self):
        self.assert_descendant_retired('time.sleep(30)', timeout=1)

    def test_timeout_retires_descendants_after_parent_has_exited(self):
        self.assert_descendant_retired('raise SystemExit(0)', timeout=1)

    def test_output_overflow_retires_descendants_and_bounds_log(self):
        self.assert_descendant_retired(
            "import os\nwhile True: os.write(1, b'x' * 65536)", timeout=5)
        self.assertLessEqual((self.root / 'command-0000.log').stat().st_size, 64 * 1024 * 1024)

    def test_fast_exiting_oversized_writer_cannot_bypass_output_budget(self):
        with self.assertRaisesRegex(RuntimeError, 'deadline/output budget exceeded'):
            self.runner(self.command("import os; os.write(1, b'x' * (65 * 1024 * 1024))"),
                        self.root, os.environ, timeout=5)
        self.assertLessEqual((self.root / 'command-0000.log').stat().st_size, 64 * 1024 * 1024)
        receipt = json.loads((self.root / 'commands.json').read_text())[0]
        self.assertIn('deadline/output budget exceeded', receipt['error'])

    def test_success_and_allowed_failure_keep_output_and_hash_receipts(self):
        for status in (0, 7):
            with self.subTest(status=status):
                output = self.runner(self.command(f"print('out'); print('err', file=__import__('sys').stderr); raise SystemExit({status})"),
                                     self.root, os.environ, timeout=5, allow_failure=True)
                self.assertEqual(['out', 'err'], output.splitlines())
                record = self.runner.records[-1]
                data = Path(record['log']).read_bytes()
                self.assertEqual(status, record['exit'])
                self.assertEqual(len(data), record['bytes'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), record['sha256'])

    @unittest.skipUnless(os.name == 'posix', 'POSIX zombie group permissions')
    def test_owned_zombie_permission_denial_preserves_completed_receipts(self):
        snapshot = commands._process_snapshot
        scans = []
        denied = []
        def observe(deadline):
            members = snapshot(deadline)
            scans.append(members)
            return members
        def signal_group(pid, sig):
            members = snapshot(time.monotonic() + 2)
            leader = members.get(pid)
            self.assertEqual((os.getpid(), pid, 'Z'), leader)
            self.assertTrue(all(state == 'Z' for _, group, state in members.values() if group == pid))
            denied.append(pid)
            raise PermissionError(1, 'zombie group has no signal targets')
        for fallback in ('native', 'no-waitid', 'ps'):
            for status in (0, 7):
                with self.subTest(fallback=fallback, status=status):
                    scans.clear()
                    previous_denials = len(denied)
                    waitid = getattr(os, 'waitid', None) if fallback == 'native' else None
                    platform = 'darwin' if fallback == 'ps' else sys.platform
                    with patch.object(commands, '_process_snapshot', observe), patch.object(os, 'killpg', signal_group):
                        with patch.object(os, 'waitid', waitid, create=True), patch.object(sys, 'platform', platform):
                            output = self.runner(self.command(f"print('completed'); raise SystemExit({status})"),
                                                 self.root, os.environ, timeout=2, allow_failure=True)
                    self.assertEqual('completed\n', output)
                    record = json.loads((self.root / 'commands.json').read_text())[-1]
                    data = Path(record['log']).read_bytes()
                    self.assertEqual(status, record['exit'])
                    self.assertEqual(len(data), record['bytes'])
                    self.assertEqual(hashlib.sha256(data).hexdigest(), record['sha256'])
                    self.assertNotIn('error', record)
                    self.assertGreater(len(denied), previous_denials, 'each case must reach owned-zombie EPERM')
                    self.assertGreaterEqual(len(scans), 3, 'permission proof must retain stable retirement scans')

    def zombie_tree(self):
        tree = commands._CommandTree(self.command('pass'), self.root, os.environ)
        self.addCleanup(tree.child.stdout.close)
        self.addCleanup(lambda: tree.child.wait(timeout=2))
        deadline = time.monotonic() + 2
        while not tree.exited(deadline):
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        self.assertIsNone(tree.child.returncode)
        return tree

    @unittest.skipUnless(os.name == 'posix', 'POSIX live group permissions')
    def test_live_group_permission_denial_fails_closed(self):
        tree = commands._CommandTree(self.command('import time; time.sleep(30)'),
                                     self.root, os.environ)
        try:
            self.assertFalse(tree.exited(time.monotonic() + 2))
            with patch.object(os, 'killpg', side_effect=PermissionError(1, 'live group denied')):
                with self.assertRaisesRegex(PermissionError, 'live group denied'):
                    tree._kill_group(time.monotonic() + 2)
            self.assertFalse(tree.exited(time.monotonic() + 2))
        finally:
            tree.child.kill(); tree.child.wait(timeout=2); tree.child.stdout.close()

    @unittest.skipUnless(os.name == 'posix', 'POSIX permission evidence boundary')
    def test_zombie_permission_denial_requires_complete_owned_retirement_evidence(self):
        failures = dict(unavailable='unavailable', malformed='malformed', unknown='unverified group denied',
                        live='unverified group denied', missing='ownership', changed='ownership',
                        reaped='ownership', deadline='cleanup deadline exceeded')
        for failure, message in failures.items():
            with self.subTest(failure=failure):
                tree = self.zombie_tree()
                snapshot = commands._process_snapshot
                pid = tree.child.pid
                elapsed = 0
                def evidence(deadline):
                    nonlocal elapsed
                    members = snapshot(deadline)
                    if failure in ('unavailable', 'malformed'):
                        raise RuntimeError(failure + ' command process snapshot')
                    if failure in ('unknown', 'live'):
                        members[pid + 1] = (pid, pid, '?' if failure == 'unknown' else 'S')
                    if failure == 'missing': del members[pid]
                    if failure == 'changed': members[pid] = (os.getpid() + 1, pid, 'Z')
                    if failure == 'reaped': tree.child.wait(timeout=2)
                    if failure == 'deadline': elapsed = 2
                    return members
                # The initial ownership check is real; corrupt only the post-denial proof.
                def deny(pid, sig):
                    commands._process_snapshot = evidence
                    raise PermissionError(1, 'unverified group denied')
                clock = (lambda: elapsed) if failure == 'deadline' else time.monotonic
                with patch.object(commands, '_process_snapshot', snapshot), patch.object(os, 'killpg', side_effect=deny) as kill:
                    with patch.object(commands.time, 'monotonic', clock):
                        with self.assertRaisesRegex((PermissionError, RuntimeError), message):
                            tree._kill_group(1 if failure == 'deadline' else time.monotonic() + 2)
                    if failure == 'reaped':
                        self.assertEqual(0, tree.child.returncode)
                        with self.assertRaisesRegex(RuntimeError, 'ownership'):
                            tree._kill_group(time.monotonic() + 2)
                        self.assertEqual(1, kill.call_count, 'lost ownership must prevent another signal')

    @unittest.skipUnless(os.name == 'posix', 'POSIX cleanup error context')
    def test_live_permission_failure_retains_original_command_timeout(self):
        with patch.object(os, 'killpg', side_effect=PermissionError(1, 'live group denied')):
            with self.assertRaisesRegex(RuntimeError, 'cleanup.*live group denied.*original command failure:.*deadline/output'):
                self.runner(self.command('import time; time.sleep(.5)'), self.root, os.environ, timeout=.01)
        receipt = json.loads((self.root / 'commands.json').read_text())[-1]
        self.assertIn('original command failure:', receipt['error'])
        self.assertNotIn('sha256', receipt)

    def assert_success_retires_socket_owner(self):
        ready = self.root / 'descendant.json'
        worker = ("import json,os,socket,time; from pathlib import Path; "
                  "s=socket.socket(); s.bind(('127.0.0.1',0)); "
                  "Path('descendant.json').write_text(json.dumps([os.getpid(),s.getsockname()[1]])); "
                  "os.close(1); os.close(2); time.sleep(30)")
        code = ("import subprocess,time; from pathlib import Path; "
                f"subprocess.Popen({self.command(worker)!r}); "
                "\nwhile not Path('descendant.json').exists(): time.sleep(.01)")
        try:
            self.runner(self.command(code), self.root, os.environ, timeout=2)
            _, port = json.loads(ready.read_text())
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', port))
        finally:
            if ready.exists():
                pid, _ = json.loads(ready.read_text())
                try: os.kill(pid, signal.SIGKILL)
                except ProcessLookupError: pass

    @unittest.skipUnless(os.name == 'posix', 'POSIX process-group retirement')
    def test_success_retires_socket_owner_after_leader_exit(self):
        self.assert_success_retires_socket_owner()

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux observation boundaries')
    def test_cleanup_discovery_failure_retains_original_error(self):
        original = Path.read_text
        def read(path, *args, **kwargs):
            if str(path).startswith('/proc/') and path.name == 'stat':
                raise PermissionError('denied process observation')
            return original(path, *args, **kwargs)
        with patch.object(Path, 'read_text', read):
            with self.assertRaisesRegex(RuntimeError, 'cleanup.*denied process observation'):
                self.runner(self.command('import time; time.sleep(30)'), self.root,
                            os.environ, timeout=.1)
        receipt = self.runner.records[-1]
        self.assertIn('deadline/output budget exceeded', receipt['error'])

    @unittest.skipUnless(os.name == 'posix', 'POSIX ownership anchor')
    def test_reaped_leader_never_signals_recycled_group(self):
        tree = commands._CommandTree(self.command('pass'), self.root, os.environ)
        self.addCleanup(tree.child.stdout.close)
        tree.child.wait(timeout=2)
        with patch.object(os, 'killpg') as kill:
            with self.assertRaisesRegex(RuntimeError, 'ownership'):
                tree.terminate()
        kill.assert_not_called()

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux process state')
    def test_new_live_member_delays_retirement_and_zombies_do_not(self):
        tree = commands._CommandTree(self.command('import time; time.sleep(30)'), self.root, os.environ)
        pid = tree.child.pid
        self.addCleanup(tree.child.stdout.close)
        original_read, original_list = Path.read_text, Path.iterdir
        scans = 0
        def entries(path):
            nonlocal scans
            result = list(original_list(path))
            if str(path) == '/proc':
                scans += 1
                if scans >= 2:
                    result.append(Path('/proc/999999'))
            return iter(result)
        def read(path, *args, **kwargs):
            if str(path) == '/proc/999999/stat':
                state = 'R' if scans == 2 else 'Z'
                return f'999999 (late member) {state} 1 {pid} {pid} '
            return original_read(path, *args, **kwargs)
        try:
            with patch.object(Path, 'read_text', read), patch.object(Path, 'iterdir', entries):
                tree.terminate()
            self.assertGreaterEqual(scans, 4, 'a new live member invalidates the retired scan')
            self.assertIsNotNone(tree.child.returncode)
        finally:
            if tree.child.returncode is None:
                tree.child.kill(); tree.child.wait(timeout=2)

    @unittest.skipUnless(os.name == 'posix', 'POSIX cleanup deadline')
    def test_cleanup_and_reader_join_share_one_deadline(self):
        real_join = commands.threading.Thread.join
        budgets = []
        def join(reader, timeout=None):
            budgets.append(timeout)
            return real_join(reader, timeout)
        with patch.object(commands.threading.Thread, 'join', join):
            with self.assertRaisesRegex(RuntimeError, 'deadline/output budget exceeded'):
                self.runner(self.command('import time; time.sleep(30)'), self.root,
                            os.environ, timeout=.1)
        self.assertTrue(budgets)
        self.assertLess(budgets[-1], 5, 'joining must consume the remaining cleanup budget')

    @unittest.skipUnless(os.name == 'posix', 'older Python exit observation')
    def test_exit_observation_without_waitid_keeps_leader_until_cleanup(self):
        if not hasattr(os, 'waitid'):
            self.skipTest('waitid already absent on this host')
        with patch.object(os, 'waitid', None):
            self.assert_success_retires_socket_owner()

    @unittest.skipUnless(os.name == 'posix', 'BSD/macOS snapshot fallback')
    def test_ps_fallback_keeps_unreaped_leader_and_retires_descendants(self):
        with patch.object(sys, 'platform', 'darwin', create=True), patch.object(os, 'waitid', None, create=True):
            self.assert_success_retires_socket_owner()

    @unittest.skipUnless(os.name == 'posix', 'BSD/macOS snapshot validation')
    def test_malformed_ps_snapshot_cannot_claim_retirement(self):
        popen = subprocess.Popen
        def launch(argv, *args, **kwargs):
            if argv[0] in ('/bin/ps', '/usr/bin/ps'):
                argv = self.command("print('malformed snapshot')")
            return popen(argv, *args, **kwargs)
        with patch.object(sys, 'platform', 'darwin', create=True), patch.object(subprocess, 'Popen', launch):
            with self.assertRaisesRegex(RuntimeError, 'cleanup.*snapshot'):
                self.runner(self.command('pass'), self.root, os.environ, timeout=2)

    def ps_output(self, data):
        popen = subprocess.Popen
        def launch(argv, *args, **kwargs):
            if argv[0] in ('/bin/ps', '/usr/bin/ps'):
                argv = self.command(f'import os; os.write(1, {data.encode("ascii")!r})')
            return popen(argv, *args, **kwargs)
        return patch.object(subprocess, 'Popen', launch)

    @unittest.skipUnless(os.name == 'posix', 'FreeBSD kernel process observation')
    def test_ps_kernel_pid_zero_preserves_owned_leader_and_live_descendant(self):
        data = '0 0 0 DLs\n2255 2178 2255 Z\n2258 2255 2255 S\n'
        with patch.object(sys, 'platform', 'freebsd14'), self.ps_output(data):
            members = commands._process_snapshot(time.monotonic() + 2)
        self.assertEqual({0: (0, 0, 'D'), 2255: (2178, 2255, 'Z'),
                          2258: (2255, 2255, 'S')}, members)

    @unittest.skipUnless(os.name == 'posix', 'FreeBSD kernel process validation')
    def test_ps_kernel_pid_zero_keeps_snapshot_validation_fail_closed(self):
        for data in ('', '0 0 0 DLs\n0 0 0 DLs', '0 1 0 DLs', '0 0 1 DLs',
                     '-1 0 0 DLs', '0 -1 0 DLs', '0 0 -1 DLs',
                     '0 0 0 !', '0 0 0 DQ', '0 0 DLs', '0 0 0 DLs command'):
            with self.subTest(data=data):
                with patch.object(sys, 'platform', 'freebsd14'), self.ps_output(data):
                    with self.assertRaisesRegex(RuntimeError, 'snapshot'):
                        commands._process_snapshot(time.monotonic() + 2)

    @unittest.skipUnless(os.name == 'posix', 'FreeBSD kernel row during real group cleanup')
    def test_ps_kernel_pid_zero_does_not_prevent_owned_descendant_retirement(self):
        popen, killpg = subprocess.Popen, os.killpg
        groups = []
        def launch(argv, *args, **kwargs):
            if argv[0] in ('/bin/ps', '/usr/bin/ps'):
                code = ("import subprocess,sys; "
                        f"data=subprocess.check_output({argv!r}); "
                        "kernel=any(line.split()[0] == b'0' for line in data.splitlines()); "
                        "sys.stdout.buffer.write((b'' if kernel else b'0 0 0 DLs\\n') + data)")
                argv = self.command(code)
            return popen(argv, *args, **kwargs)
        def signal_group(group, sig):
            self.assertGreater(group, 0, 'kernel PID/group zero must never be signalled')
            groups.append(group)
            return killpg(group, sig)
        with patch.object(sys, 'platform', 'freebsd14'), patch.object(os, 'waitid', None, create=True), \
                patch.object(subprocess, 'Popen', launch), patch.object(os, 'killpg', signal_group):
            self.assert_success_retires_socket_owner()
        self.assertTrue(groups, 'real owned group must be retired')
        record = json.loads((self.root / 'commands.json').read_text())[-1]
        data = Path(record['log']).read_bytes()
        self.assertEqual(0, record['exit'])
        self.assertEqual(hashlib.sha256(data).hexdigest(), record['sha256'])
        self.assertNotIn('error', record)

    @unittest.skipUnless(os.name == 'posix', 'Darwin unknown scheduling state')
    def test_ps_unknown_state_preserves_complete_numeric_membership(self):
        # Captured native rows: the unknown process belongs to a different group.
        data = ' 2255  2178  2255 Z<  \n 2258   910   892 ?<  \n'
        with patch.object(sys, 'platform', 'darwin'), self.ps_output(data):
            members = commands._process_snapshot(time.monotonic() + 2)
        self.assertEqual({2255: (2178, 2255, 'Z'), 2258: (910, 892, '?')}, members)

    @unittest.skipUnless(os.name == 'posix', 'unknown owned process cannot prove retirement')
    def test_ps_unknown_owned_member_cannot_justify_permission_denial(self):
        for unknown in ('leader', 'descendant'):
            with self.subTest(unknown=unknown):
                tree = self.zombie_tree()
                pid = tree.child.pid
                data = f'{pid} {os.getpid()} {pid} ' + ('?<' if unknown == 'leader' else 'Z') + '\n'
                if unknown == 'descendant':
                    data += f'{pid + 1} {pid} {pid} ?<\n'
                with patch.object(sys, 'platform', 'darwin'), patch.object(os, 'waitid', None, create=True), self.ps_output(data):
                    with patch.object(os, 'killpg', side_effect=PermissionError(1, 'unknown group denied')):
                        with self.assertRaisesRegex(PermissionError, 'unknown group denied'):
                            tree._kill_group(time.monotonic() + 2)
                self.assertIsNone(tree.child.returncode, 'unknown state cannot reap the ownership anchor')

    @unittest.skipUnless(os.name == 'posix', 'strict ps snapshot structure')
    def test_ps_unknown_state_does_not_relax_snapshot_validation(self):
        for data in ('pid 910 892 ?<', '2258 parent 892 ?<', '2258 910 group ?<',
                     '-1 910 892 ?<', '0 910 892 ?<', '2258 -1 892 ?<', '2258 910 -1 ?<',
                     '2258 910 ?<', '2258 910 892 ?< command', '2258 910 892 ?Q',
                     '2258 910 892 !<', '2258 910 892 ?<\n2258 910 892 Z'):
            with self.subTest(data=data):
                with patch.object(sys, 'platform', 'darwin'), self.ps_output(data + '\n'):
                    with self.assertRaisesRegex(RuntimeError, 'snapshot'):
                        commands._process_snapshot(time.monotonic() + 2)

    @unittest.skipUnless(os.name == 'posix', 'strict ps ownership anchor')
    def test_ps_unknown_state_cannot_hide_missing_or_changed_leader(self):
        tree = self.zombie_tree()
        pid = tree.child.pid
        for data in (f'{pid + 1} {pid} {pid} ?<',
                     f'{pid} {os.getpid() + 1} {pid} ?<', f'{pid} {os.getpid()} 0 ?<'):
            with self.subTest(data=data):
                with patch.object(sys, 'platform', 'darwin'), patch.object(os, 'waitid', None, create=True), self.ps_output(data + '\n'):
                    with patch.object(os, 'killpg') as kill:
                        with self.assertRaisesRegex(RuntimeError, 'ownership'):
                            tree._kill_group(time.monotonic() + 2)
                    kill.assert_not_called()

    @unittest.skipUnless(os.name == 'posix', 'documented BSD/macOS state modifiers')
    def test_ps_fallback_accepts_documented_state_modifiers(self):
        popen = subprocess.Popen
        def launch(argv, *args, **kwargs):
            if argv[0] in ('/bin/ps', '/usr/bin/ps'):
                code = ("import subprocess; "
                        f"data=subprocess.check_output({argv!r}).decode('ascii'); "
                        "print('\\n'.join(line.rstrip() + '>ACJ' for line in data.splitlines()))")
                argv = self.command(code)
            return popen(argv, *args, **kwargs)
        with patch.object(sys, 'platform', 'darwin'), patch.object(os, 'waitid', None, create=True), patch.object(subprocess, 'Popen', launch):
            self.assert_success_retires_socket_owner()

    @unittest.skipUnless(hasattr(os, 'waitid'), 'bounded fallback snapshot parsing')
    def test_ps_parsing_cannot_signal_after_cleanup_deadline(self):
        tree = commands._CommandTree(self.command('pass'), self.root, os.environ)
        self.addCleanup(tree.child.stdout.close)
        deadline = time.monotonic() + 2
        while not os.waitid(os.P_PID, tree.child.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT):
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        elapsed = 0
        original_buffer = commands.io.BytesIO
        class DelayedSnapshot(original_buffer):
            def getvalue(buffer):
                nonlocal elapsed
                elapsed = 2
                return super().getvalue()
        try:
            with patch.object(sys, 'platform', 'darwin'), patch.object(os, 'waitid', None), patch.object(commands.io, 'BytesIO', DelayedSnapshot), patch.object(commands.time, 'monotonic', lambda: elapsed), patch.object(os, 'killpg') as kill:
                with self.assertRaisesRegex(RuntimeError, 'cleanup deadline exceeded'):
                    tree.terminate(deadline=1)
                kill.assert_not_called()
        finally:
            tree.child.wait(timeout=2)

    @unittest.skipUnless(hasattr(os, 'waitid'), 'non-reaping exit observation')
    def test_zombie_only_group_can_be_absent_from_signal_lookup(self):
        tree = commands._CommandTree(self.command('pass'), self.root, os.environ)
        self.addCleanup(tree.child.stdout.close)
        deadline = time.monotonic() + 2
        while not os.waitid(os.P_PID, tree.child.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT):
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        try:
            with patch.object(os, 'killpg', side_effect=ProcessLookupError):
                tree.terminate()
            self.assertEqual(0, tree.child.returncode)
        finally:
            tree.child.wait(timeout=2)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux cleanup clock boundary')
    def test_discovery_consumes_cleanup_deadline_without_restarting_wait(self):
        tree = commands._CommandTree(self.command('import time; time.sleep(30)'), self.root, os.environ)
        self.addCleanup(tree.child.stdout.close)
        original_list = Path.iterdir
        elapsed = 0
        def entries(path):
            nonlocal elapsed
            if str(path) == '/proc': elapsed += 2
            return original_list(path)
        try:
            with patch.object(commands.time, 'monotonic', lambda: elapsed), patch.object(Path, 'iterdir', entries):
                with self.assertRaisesRegex(RuntimeError, 'cleanup deadline exceeded'):
                    tree.terminate(deadline=1)
        finally:
            tree.child.kill(); tree.child.wait(timeout=2)

    def test_nonzero_exit_retains_output_and_failure_receipt(self):
        with self.assertRaisesRegex(RuntimeError, 'matrix command failed:'):
            self.runner(self.command("print('failure detail'); raise SystemExit(7)"),
                        self.root, os.environ, timeout=5)
        receipt = json.loads((self.root / 'commands.json').read_text())[0]
        self.assertEqual(7, receipt['exit'])
        self.assertIn('failure detail', receipt['error'])
        self.assertIn(b'failure detail', Path(receipt['log']).read_bytes())


class ProcessTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def command(self, code):
        return [sys.executable, '-u', '-c', code]

    def test_bounded_startup_and_shutdown_with_concurrent_stderr(self):
        command = self.command("import sys; sys.stderr.write('x'*10000); print('1234', flush=True); input(); print('done')")
        with process.Consumer(command, self.root, None, self.root / 'pass.json', timeout=2) as child:
            self.assertEqual('1234', child.line())
            child.stop()
        receipt = json.loads((self.root / 'pass.json').read_text())
        self.assertEqual(0, receipt['exit'])
        self.assertIn('done', receipt['stdout'])

    def test_failed_startup_timeout_and_oversize_retain_evidence(self):
        cases = ["import time; time.sleep(10)", "print('bad')", "raise SystemExit(4)",
                 "import sys; sys.stdout.write('x'*1000000); sys.stdout.flush()"]
        for i, code in enumerate(cases):
            receipt = self.root / f'failure-{i}.json'
            with self.subTest(code=code), self.assertRaises(RuntimeError):
                with process.Consumer(self.command(code), self.root, None, receipt, timeout=.2) as child:
                    child.port()
            self.assertIn('error', json.loads(receipt.read_text()))
            self.assertLess(receipt.stat().st_size, 150000)

    def test_nonzero_shutdown_is_failure(self):
        receipt = self.root / 'stop.json'
        with self.assertRaises(RuntimeError):
            with process.Consumer(self.command("print('1234', flush=True); input(); raise SystemExit(8)"),
                                  self.root, None, receipt, timeout=1) as child:
                child.port()
                child.stop()
        self.assertEqual(8, json.loads(receipt.read_text())['exit'])


if __name__ == '__main__':
    unittest.main()
