#!/usr/bin/env python3
"""Make gate and inspector-output fixtures; real binary proof runs separately."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parent.parent

class MakeGateFixtures(unittest.TestCase):
    """Run the production recipe with GNU make and controlled build/inspector outputs."""

    def gate(self, suffix='', mode='no', linker='CXX', dependency=None,
             symbols='', build_fails=False):
        make = shutil.which('gmake') or shutil.which('make')
        self.assertIsNotNone(make, 'GNU make is required for native linkage gate tests')
        recipe = (SOURCE / 'Makefile.am').read_text().split(
            '\ncheck-v3-native-linkage:\n', 1)[1].split('\n\n', 1)[0]
        if dependency is None:
            dependency = 'KERNEL32.dll' if suffix else '/usr/lib/libc++.1.dylib'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            for name in ['src', 'test', 'tools']:
                (root / name).mkdir()
            (root / 'src/Makefile').write_text(
                'libhttpserver.la:\n'
                f'\tlibtool --tag={linker} --mode=link -o libhttpserver.la archive\n')
            # Like Automake, expose only the configured executable target.
            (root / 'test/Makefile').write_text(
                'native_linkage$(EXEEXT):\n'
                + ('\t@exit 1\n' if build_fails else
                   '\t@mkdir -p .libs ../src/.libs\n'
                   '\t@printf "#!/bin/sh\\nexit 0\\n" > .libs/$@\n'
                   '\t@chmod +x .libs/$@\n'
                   '\t@touch ../src/.libs/libhttpserver_v3core.a\n'))
            inspectors = {'nm': symbols}
            if suffix:
                inspectors.update(uname='MINGW64_NT-10.0', objdump=(
                    'fixture.exe: file format pei-x86-64\n'
                    + '\n'.join(' DLL Name: ' + n for n in dependency.splitlines())))
            else:
                inspectors['otool'] = 'fixture:\n' + dependency
            for name, output in inspectors.items():
                path = root / 'tools' / name
                path.write_text("#!/bin/sh\ncat <<'OUTPUT'\n" + output + '\nOUTPUT\n')
                path.chmod(0o755)
            env = dict(os.environ, PATH=str(root / 'tools') + os.pathsep + os.environ['PATH'])
            return subprocess.run(
                [make, '--no-print-directory', '-f', '-', 'check-v3-native-linkage',
                 'top_srcdir=' + str(SOURCE), 'top_builddir=' + str(root),
                 'abs_top_builddir=' + str(root), 'EXEEXT=' + suffix,
                 'enable_v3_tls=' + mode],
                input='check-v3-native-linkage:\n' + recipe + '\n', cwd=root,
                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def test_configured_executable_suffix_reaches_real_audit(self):
        for suffix in ['', '.exe']:
            with self.subTest(suffix=suffix):
                result = self.gate(suffix=suffix)
                self.assertEqual(0, result.returncode, result.stdout)
                self.assertIn('A1 PASS', result.stdout)
                self.assertIn('A2 PASS', result.stdout)

    def test_configured_tls_mode_reaches_real_audit(self):
        for suffix, dependency in [('', '/tmp/libssl.3.dylib'), ('.exe', 'libssl-3.dll')]:
            for mode in ['yes', 'no']:
                with self.subTest(suffix=suffix, mode=mode):
                    result = self.gate(suffix=suffix, dependency=dependency, mode=mode)
                    self.assertEqual(mode == 'yes', result.returncode == 0, result.stdout)
                    self.assertIn('A2 PASS' if mode == 'yes' else 'TLS-off load dependency',
                                  result.stdout)

    def test_windows_seh_compiler_runtime_reaches_real_audit(self):
        for mode in ['yes', 'no']:
            with self.subTest(mode=mode):
                result = self.gate(suffix='.exe', mode=mode,
                                   dependency='KERNEL32.dll\nlibgcc_s_seh-1.dll')
                self.assertEqual(0, result.returncode, result.stdout)
                self.assertIn('A1 PASS', result.stdout)
                self.assertIn('A2 PASS', result.stdout)

    def test_cxx_linker_gate_stops_before_audit(self):
        result = self.gate(linker='CC')
        self.assertNotEqual(0, result.returncode)
        self.assertIn('aggregate must use the C++ linker', result.stdout)
        self.assertNotIn('A1 PASS', result.stdout)

    def test_build_failure_stops_before_audit(self):
        result = self.gate(build_fails=True)
        self.assertNotEqual(0, result.returncode)
        self.assertNotIn('A1 PASS', result.stdout)

    def test_static_provider_rejection_propagates(self):
        result = self.gate(symbols=' U _SSL_set_quic_tls_cbs')
        self.assertNotEqual(0, result.returncode)
        self.assertIn('A1 PASS', result.stdout)
        self.assertIn('TLS-off provider symbols', result.stdout)
        self.assertNotIn('A2 PASS', result.stdout)

class AuditFixtures(unittest.TestCase):
    def audit(self, dependency=None, symbols='', mode='no', binary=True, archive=True, windows=False, freebsd=False):
        if dependency is None:
            dependency = '/usr/lib/libc++.1.dylib'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test/.libs').mkdir(parents=True)
            (root / 'src/.libs').mkdir(parents=True)
            (root / 'tools').mkdir()
            if binary:
                for name in ['test/native_linkage', 'test/.libs/native_linkage']:
                    name += '.exe' if windows else ''
                    (root / name).write_text('#!/bin/sh\nexit 0\n')
                    (root / name).chmod(0o755)
            if archive:
                (root / 'src/.libs/libhttpserver_v3core.a').touch()
            tools = [('otool', 'fixture:\n' + dependency), ('nm', symbols)]
            if freebsd:
                sdk = root / 'sdk'
                (sdk / 'lib').mkdir(parents=True)
                (sdk / 'lib/libc++.so.1.0').touch()
                dependency = dependency.replace('SELECTED', str(sdk))
                tools = [('uname', 'FreeBSD'), ('ldd', dependency), ('nm', symbols)]
            if windows:
                tools += [('uname', 'MINGW64_NT-10.0'), ('objdump', 'fixture.exe: file format pei-x86-64\n' + '\n'.join(' DLL Name: ' + n for n in dependency.splitlines()))]
            for tool, output in tools:
                path = root / 'tools' / tool
                path.write_text('#!/bin/sh\ncat <<\'OUTPUT\'\n' + output + '\nOUTPUT\n')
                path.chmod(0o755)
            env = dict(os.environ, BUILD_DIR=str(root), SRC_DIR=str(SOURCE),
                       V3_TLS_MODE=mode, PATH=str(root / 'tools') + ':' + os.environ['PATH'])
            if freebsd:
                env['V3_PACKAGE_TOOLCHAIN'] = str(sdk)
            return subprocess.run(['bash', str(SOURCE / 'scripts/audit-v3-native-linkage.sh')],
                                  env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def test_off_rejects_listed_ssl_even_without_symbols(self):
        self.assertNotEqual(self.audit('/tmp/libssl.3.dylib').returncode, 0)

    def test_system_curl_dependency_is_not_platform_runtime(self):
        self.assertNotEqual(self.audit('/usr/lib/libcurl.4.dylib').returncode, 0)

    def test_unknown_provider_in_system_directory_is_rejected(self):
        self.assertNotEqual(self.audit('/usr/lib/libalternate_tls.dylib', mode='yes').returncode, 0)

    def test_on_rejects_other_provider(self):
        self.assertNotEqual(self.audit('/tmp/libgnutls.30.dylib', mode='yes').returncode, 0)

    def test_off_rejects_static_callback_references(self):
        self.assertNotEqual(self.audit(symbols='                 U _SSL_set_quic_tls_cbs').returncode, 0)

    def test_empty_dependency_inspection_fails_closed(self):
        self.assertNotEqual(self.audit(dependency='').returncode, 0)

    def test_missing_binary_fails_closed(self):
        self.assertNotEqual(self.audit(binary=False).returncode, 0)

    def test_missing_archive_fails_closed(self):
        self.assertNotEqual(self.audit(archive=False).returncode, 0)

    def test_native_windows_executable_and_readiness_runtime(self):
        self.assertEqual(0, self.audit('KERNEL32.dll\nWS2_32.dll\nlibstdc++-6.dll', windows=True).returncode)

    def test_windows_seh_runtime_lookalikes_are_rejected(self):
        for dependency in ['libgcc_s_seh-1.dll.extra', 'libgcc_s_seh-2.dll',
                           'libgcc_s_unknown-1.dll']:
            for mode in ['yes', 'no']:
                with self.subTest(dependency=dependency, mode=mode):
                    result = self.audit(dependency, mode=mode, windows=True)
                    self.assertNotEqual(0, result.returncode, result.stdout)
                    self.assertIn('unexpected third-party load dependency: ' + dependency,
                                  result.stdout)
                    self.assertNotIn('A2 PASS', result.stdout)

    def test_windows_import_symbols_and_msys_are_rejected(self):
        self.assertNotEqual(0, self.audit('KERNEL32.dll', symbols=' U __imp_SSL_new', windows=True).returncode)
        self.assertNotEqual(0, self.audit('msys-2.0.dll', windows=True).returncode)

    def test_freebsd_loader_is_platform_runtime(self):
        self.assertEqual(0, self.audit('/libexec/ld-elf.so.1\n/lib/libc.so.7').returncode)

    def test_freebsd_sdk_and_exact_system_cxxrt_are_accepted(self):
        result = self.audit('libc++.so.1 => SELECTED/lib/libc++.so.1.0 (0x123)\n'
                            'libcxxrt.so.1 => /lib/libcxxrt.so.1 (0x456)', freebsd=True)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_freebsd_lookalike_or_unselected_runtime_is_rejected(self):
        for dependency in ('libcxxrt.so.1 => /usr/local/lib/libcxxrt.so.1 (0x123)',
                           'libcxxrt.so.2 => /lib/libcxxrt.so.2 (0x123)',
                           'libc++.so.1 => /usr/lib/libc++.so.1 (0x123)',
                           'libc++.so.1 => SELECTED/lib/libc++.so.1.0.extra (0x123)',
                           'libcrypto.so.3 => SELECTED/lib/libcrypto.so.3 (0x123)'):
            with self.subTest(dependency=dependency):
                result = self.audit(dependency, freebsd=True)
                self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_only_selected_provider_is_allowed_on(self):
        self.assertEqual(self.audit('/tmp/libssl.3.dylib\n/tmp/libcrypto.3.dylib',
                                    '                 U _SSL_set_quic_tls_cbs', 'yes').returncode, 0)

if __name__ == '__main__':
    unittest.main()
