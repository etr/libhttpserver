#!/usr/bin/env python3
"""Exercise configured staging recipes and real Automake/libtool installation."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parent.parent
MAKE = shutil.which('gmake') or shutil.which('make')


class StagedInstallRecipes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not MAKE:
            raise RuntimeError('GNU make is required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='v3-stage-install-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.build = Path(cls.temporary.name).resolve()
        cls.environment = {key: value for key, value in os.environ.items()
                           if key not in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL')}
        for tool in ('install', 'ranlib'):
            boundary = cls.build / f'native-{tool}-boundary.sh'
            boundary.write_text(
                '#!/bin/sh\n'
                'for destination do :; done\n'
                '# MSYS cannot pass an embedded drive specification to native tools.\n'
                'case "$destination" in\n'
                f'  /*[a-zA-Z]:/*) echo "native {tool} rejects embedded drive: $destination" >&2; exit 1 ;;\n'
                'esac\n'
                f'printf "%s\\n" "$destination" >> {tool}-destinations.log\n'
                f'exec {shlex.quote(Path(shutil.which(tool)).as_posix())} "$@"\n')
            boundary.chmod(0o755)
        result = subprocess.run(
            ['sh', (SOURCE / 'configure').as_posix(), '--disable-v3-tls',
             '--disable-examples', '--prefix=D:/ci/prefix',
             'RANLIB=' + (cls.build / 'native-ranlib-boundary.sh').as_posix()], cwd=cls.build,
            env=cls.environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout)
        cls.installer = cls.build / 'native-install-boundary.sh'
        # A tiny real library reaches Automake/libtool's archive and DLL install
        # paths without compiling the unrelated protocol engines in each fixture.
        (cls.build / 'src/stagefixture.cpp').write_text(
            '#ifdef _WIN32\n__declspec(dllexport)\n#endif\n'
            'int stagefixture() { return 1; }\n')
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        for arguments in [
                ['--mode=compile'] + compiler + ['-c', 'stagefixture.cpp', '-o', 'stagefixture.lo'],
                ['--mode=link'] + compiler + ['-o', 'libstagefixture.la', 'stagefixture.lo',
                 '-rpath', '/stagefixture', '-no-undefined']]:
            result = subprocess.run(['sh', '../libtool', '--tag=CXX'] + arguments,
                                    cwd=cls.build / 'src', env=cls.environment, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True)
            if result.returncode:
                raise RuntimeError(result.stdout)

    def setUp(self):
        for name in ('.hygiene-stage', '.install-stage', '.shared-check-stage', 'missing-stage'):
            shutil.rmtree(self.build / name, ignore_errors=True)
        for path in self.build.rglob('install-destinations.log'):
            path.unlink()

    def make(self, target, *variables, shared=False):
        # Exercise the production Makefile, recursive install rules and alias hook.
        # Only the fixture archive replaces the unrelated protocol engines.
        command = [MAKE, '--no-print-directory', target,
                   'lib_LTLIBRARIES=libstagefixture.la', 'noinst_LTLIBRARIES=', 'noinst_PROGRAMS=',
                   'INSTALL=' + self.installer.as_posix()]
        if shared:
            # Only check-local's staged-install body is under test here.
            text = (SOURCE / 'Makefile.am').read_text()
            prerequisites = text.split('\ncheck-local:', 1)[1].split('\n', 1)[0].split()
            for name in prerequisites:
                command.extend(['-o', name])
            command.append('AM_MAKEFLAGS=-o check-readme -o check-release-notes -o check-doxygen')
        return subprocess.run(command + list(variables), cwd=self.build,
                              env=self.environment, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)

    def assert_passed(self, result):
        self.assertEqual(0, result.returncode, result.stdout)

    def installed(self, path):
        return self.build / '.hygiene-stage' / path

    def install_count(self):
        return sum(line.endswith('/lib/pkgconfig')
                   for path in self.build.rglob('install-destinations.log')
                   for line in path.read_text().splitlines())

    def test_dos_prefix_standalone_checks_use_real_installed_headers(self):
        self.assert_passed(self.make('check-hygiene'))
        include = self.installed('D/ci/prefix/include')
        self.assertTrue((include / 'httpserverpp').is_file())
        self.assertTrue((include / 'httpserver/server/readiness.hpp').is_file())
        self.assertTrue(self.installed('D/ci/prefix/lib/libstagefixture.a').is_file())
        self.assertIn('prefix=D:/ci/prefix',
                      self.installed('D/ci/prefix/lib/pkgconfig/libhttpserver.pc').read_text())
        self.assertTrue(self.installed('D/ci/prefix/share/man/man3/libhttpserver.3').is_file())
        self.assertTrue(self.installed('D/ci/prefix/share/cmake/Modules/FindLibHttpServer.cmake').is_file())
        self.assert_passed(self.make('check-install-layout'))
        self.assertFalse((self.build / '.install-stage').exists())

    def test_dos_prefix_shared_check_local_installs_once_and_cleans_up(self):
        result = self.make('check-local', shared=True)
        self.assert_passed(result)
        self.assertIn('PASS: staged install layout', result.stdout)
        self.assertIn('PASS: no forbidden headers', result.stdout)
        self.assertEqual(1, self.install_count())
        self.assertFalse((self.build / '.shared-check-stage').exists())

    def test_standalone_hygiene_reuses_its_stamp(self):
        self.assert_passed(self.make('check-hygiene'))
        self.assert_passed(self.make('check-hygiene'))
        self.assertEqual(1, self.install_count())

    def test_unix_and_custom_dos_install_directories(self):
        for prefix, include, library, data, man in [
                ('/opt/package', '/custom/headers', '/custom/lib64', '/custom/data', '/custom/manual'),
                ('D:/ci/prefix', 'E:/custom/headers', 'F:/custom/lib64', 'G:/custom/data', 'H:/custom/manual')]:
            with self.subTest(prefix=prefix):
                variables = [f'prefix={prefix}', f'includedir={include}', f'libdir={library}',
                             f'datadir={data}', f'man3dir={man}']
                self.assert_passed(self.make('check-hygiene', *variables))
                relative = include.replace(':', '').lstrip('/')
                self.assertTrue(self.installed(relative + '/httpserver.hpp').is_file())
                self.assertTrue(self.installed(library.replace(':', '').lstrip('/') +
                                               '/pkgconfig/libhttpserver.pc').is_file())
                self.assertTrue(self.installed(data.replace(':', '').lstrip('/') +
                                               '/cmake/Modules/FindLibHttpServer.cmake').is_file())
                self.assertTrue(self.installed(man.replace(':', '').lstrip('/') +
                                               '/libhttpserver.3').is_file())
                self.assert_passed(self.make('check-install-layout', *variables))
                shutil.rmtree(self.build / '.hygiene-stage')

    def test_shared_hygiene_rejects_missing_and_incomplete_stage(self):
        stage = self.build / 'missing-stage'
        for exists in (False, True):
            if exists:
                stage.mkdir()
            result = self.make('check-hygiene', 'CHECK_HYGIENE_SHARED=yes',
                               'CHECK_HYGIENE_STAGE=' + stage.as_posix())
            self.assertNotEqual(0, result.returncode, result.stdout)
            self.assertIn('does not contain' if exists else 'does not exist', result.stdout)

    def test_install_failure_cleans_standalone_stages(self):
        for target, directory in [('check-hygiene', '.hygiene-stage'),
                                  ('check-install-layout', '.install-stage')]:
            with self.subTest(target=target):
                result = self.make(target, 'INSTALL=false')
                self.assertNotEqual(0, result.returncode, result.stdout)
                self.assertIn('FAIL: staged install failed', result.stdout)
                self.assertFalse((self.build / directory).exists())

    def test_installed_backend_header_leak_is_rejected(self):
        self.assert_passed(self.make('check-hygiene'))
        include = self.installed('D/ci/prefix/include')
        (include / 'microhttpd.h').write_text('/* forbidden provider fixture */\n')
        with (include / 'httpserver.hpp').open('a') as output:
            output.write('\n#include <microhttpd.h>\n')
        result = self.make('check-hygiene', 'CHECK_HYGIENE_SHARED=yes')
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertIn('FAIL: forbidden headers leaked', result.stdout)

    def test_layout_rejects_private_headers_missing_alias_and_broken_readiness(self):
        self.assert_passed(self.make('check-hygiene'))
        stage = self.build / '.hygiene-stage'
        include = self.installed('D/ci/prefix/include')
        variables = ['CHECK_INSTALL_SHARED=yes', 'CHECK_INSTALL_STAGE=' + stage.as_posix()]
        for name, message in [('detail', 'detail/ directory leaked'),
                              ('bad_impl.hpp', '*_impl.hpp file leaked')]:
            path = include / name
            if name == 'detail':
                path.mkdir()
            else:
                path.touch()
            result = self.make('check-install-layout', *variables)
            self.assertNotEqual(0, result.returncode, result.stdout)
            self.assertIn(message, result.stdout)
            if path.is_dir():
                path.rmdir()
            else:
                path.unlink()
        alias = include / 'httpserverpp'
        content = alias.read_bytes()
        alias.unlink()
        result = self.make('check-install-layout', *variables)
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertIn('alias not installed', result.stdout)
        alias.write_bytes(content)
        with (include / 'httpserver/server/readiness.hpp').open('a') as output:
            output.write('\n#error readiness fixture must fail\n')
        result = self.make('check-install-layout', *variables)
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertIn('readiness fixture must fail', result.stdout)


if __name__ == '__main__':
    unittest.main()
