#!/usr/bin/env python3
"""TASK-184 source/distribution cutover regressions; runtime gates run separately."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parent.parent
LEGACY = re.compile(r'microhttpd|\bMHD_|LIBMICROHTTPD|HAVE_(?:GNUTLS|BAUTH|DAUTH|WEBSOCKET)|-lgnutls', re.I)

class CutoverTest(unittest.TestCase):
    def test_native_only_build_templates(self):
        for name in ('configure.ac', 'src/Makefile.am', 'test/Makefile.am', 'examples/Makefile.am', 'libhttpserver.pc.in'):
            with self.subTest(name=name):
                self.assertIsNone(LEGACY.search((ROOT / name).read_text()))

    def test_legacy_implementation_and_headers_removed(self):
        for name in ('webserver.cpp', 'create_webserver.cpp', 'http_request.cpp', 'http_response.cpp',
                     'create_test_request.cpp', 'httpserver/webserver.hpp', 'httpserver/create_test_request.hpp',
                     'detail/daemon_lifecycle.cpp', 'detail/response_materializer.cpp'):
            with self.subTest(name=name):
                self.assertFalse((ROOT / 'src' / name).exists())
        for path in (ROOT / 'src').rglob('*'):
            if path.suffix in ('.hpp', '.cpp'):
                with self.subTest(path=path):
                    self.assertIsNone(LEGACY.search(path.read_text()))

    def test_all_native_headers_are_in_the_distribution_manifest(self):
        text = (ROOT / 'src/Makefile.am').read_text()
        text = '\n'.join(line.split('#', 1)[0] for line in text.splitlines())
        manifest = set(re.findall(r'[\w/.-]+\.hpp', text))
        headers = {str(path.relative_to(ROOT / 'src')) for path in (ROOT / 'src').rglob('*.hpp')}
        self.assertEqual(set(), headers - manifest)

    def test_native_test_header_dependencies_are_distributed(self):
        test = ROOT / 'test'
        text = (test / 'Makefile.am').read_text()
        text = '\n'.join(line.split('#', 1)[0] for line in text.splitlines())
        manifest = set(re.findall(r'[\w/.-]+\.(?:cpp|hpp)', text))
        visited, missing = set(), set()

        def visit(path):
            if path in visited or not path.is_file():
                return
            visited.add(path)
            if path.suffix == '.hpp' and str(path.relative_to(test)) not in manifest:
                missing.add(str(path.relative_to(test)))
            for include in re.findall(r'#\s*include\s*[<"]([^>"\n]+)[>"]', path.read_text()):
                for candidate in (path.parent / include, test / include, test / 'integ' / include):
                    candidate = candidate.resolve()
                    if candidate.is_file() and candidate.is_relative_to(test):
                        visit(candidate)
                        break

        for name in manifest:
            visit(test / name)
        self.assertEqual(set(), missing)

    def test_distribution_always_contains_tests_and_native_examples(self):
        self.assertIn('DIST_SUBDIRS = src doc test examples', (ROOT / 'Makefile.am').read_text())

    def test_native_windows_runtime_libraries_are_exported(self):
        configure = (ROOT / 'configure.ac').read_text()
        for library in ('-lws2_32', '-lmswsock', '-lbcrypt'):
            self.assertIn(library, configure)
        self.assertIn('libhttpserver_v3core_la_LIBADD = $(LHT_LIBDEPS)', (ROOT / 'src/Makefile.am').read_text())

    def test_example_checks_use_only_native_programs(self):
        for name in ('scripts/check-examples.sh', 'scripts/verify-installed-examples.sh'):
            script = (ROOT / name).read_text()
            self.assertNotIn('hello_world.cpp', script)
            self.assertNotIn('-lmicrohttpd', script)
            self.assertIn('example_inventory' if name.endswith('check-examples.sh')
                          else 'example_consumers.py', script)
        makefile = (ROOT / 'Makefile.am').read_text()
        self.assertNotIn('lint-deprecated-cookie-overload', makefile)
        self.assertNotIn('lint-no-tautological-asserts', makefile)

    def test_version_and_soname_gate_advance_together(self):
        self.assertIn('m4_define([libhttpserver_MAJOR_VERSION],[3])', (ROOT / 'configure.ac').read_text())
        gate = (ROOT / 'scripts/check-soversion.sh').read_text()
        self.assertIn('libhttpserver.so.3.0.0', gate)
        self.assertNotIn('libhttpserver.so.2', gate)

    def test_packaging_has_no_legacy_prerequisites(self):
        for path in [ROOT / 'scripts/prepare-v3-package-ci.py'] + list((ROOT / '.github/workflows').glob('*.yml')):
            with self.subTest(path=path):
                self.assertIsNone(LEGACY.search(path.read_text()))
        matrix = (ROOT / 'scripts/check-v3-installed-consumer.py').read_text()
        self.assertNotIn('pre-cutover', (ROOT / 'scripts/lib/package_integration.py').read_text())
        self.assertNotIn('pre-cutover', matrix)
        self.assertNotIn('strict-rejection', matrix)
        self.assertIn("'strict', args.provider_prefix", matrix)

if __name__ == '__main__':
    unittest.main()
