#!/usr/bin/env python3
"""Adversarial inspector-output fixtures; real binary proof runs separately."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parent.parent

class AuditFixtures(unittest.TestCase):
    def audit(self, dependency=None, symbols='', mode='no', binary=True, archive=True, windows=False):
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
            if windows:
                tools += [('uname', 'MINGW64_NT-10.0'), ('objdump', 'fixture.exe: file format pei-x86-64\n' + '\n'.join(' DLL Name: ' + n for n in dependency.splitlines()))]
            for tool, output in tools:
                path = root / 'tools' / tool
                path.write_text('#!/bin/sh\ncat <<\'OUTPUT\'\n' + output + '\nOUTPUT\n')
                path.chmod(0o755)
            env = dict(os.environ, BUILD_DIR=str(root), SRC_DIR=str(SOURCE),
                       V3_TLS_MODE=mode, PATH=str(root / 'tools') + ':' + os.environ['PATH'])
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

    def test_windows_import_symbols_and_msys_are_rejected(self):
        self.assertNotEqual(0, self.audit('KERNEL32.dll', symbols=' U __imp_SSL_new', windows=True).returncode)
        self.assertNotEqual(0, self.audit('msys-2.0.dll', windows=True).returncode)

    def test_freebsd_loader_is_platform_runtime(self):
        self.assertEqual(0, self.audit('/libexec/ld-elf.so.1\n/lib/libc.so.7').returncode)

    def test_only_selected_provider_is_allowed_on(self):
        self.assertEqual(self.audit('/tmp/libssl.3.dylib\n/tmp/libcrypto.3.dylib',
                                    '                 U _SSL_set_quic_tls_cbs', 'yes').returncode, 0)

if __name__ == '__main__':
    unittest.main()
