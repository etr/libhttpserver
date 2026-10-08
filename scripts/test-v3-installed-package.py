#!/usr/bin/env python3
"""Adversarial policy fixtures; these are not installed-package proof."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('audit-v3-installed-package.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.policy = audit.load_policy()

    def graph(self, links):
        for name in links:
            (self.root / name).touch()
        def inspect(path):
            return [(dep, str(self.root / dep)) for dep in links[Path(path).name]]
        return audit.dependency_graph([str(self.root / next(iter(links)))], inspect)

    def violations(self, graph, mode='pre-cutover', tls='no', surface='aggregate', declarations=None):
        return audit.graph_violations(graph, mode, tls, surface, self.policy if declarations is None else declarations, self.root)

    def installed_audit(self, mode='strict', source_legacy=False, installed_legacy=False,
                        legacy_dependency=False, legacy_symbol=False):
        root = self.root.resolve()
        source, prefix, build = (root / name for name in ('source', 'prefix', 'build'))
        for directory in (source / 'scripts', source / 'src', prefix / 'include', prefix / 'lib/pkgconfig',
                          build / 'src/.libs', build / 'test'):
            directory.mkdir(parents=True, exist_ok=True)
        (source / 'scripts/v3-installed-dependency-policy.json').write_text(json.dumps(self.policy))
        (source / 'configure.ac').write_text('AC_INIT([fixture], [3])\n')
        (source / 'src/Makefile.am').write_text('LIBADD = -lmicrohttpd\n' if source_legacy else 'LIBADD =\n')
        metadata = 'Requires.private: libmicrohttpd\n' if installed_legacy else 'Requires.private:\n'
        (source / 'libhttpserver.pc.in').write_text(metadata)
        (prefix / 'lib/pkgconfig/libhttpserver.pc').write_text(metadata)
        (build / 'libhttpserver.pc').write_text(metadata)
        (build / 'config.log').write_text('fixture configuration\n')
        (build / 'src/Makefile').write_text('fixture build\n')
        (prefix / 'include/httpserver.hpp').write_text('struct fixture_server {};\n')
        (prefix / 'include/httpserverpp').write_text('#include <httpserver.hpp>\n')
        shared = prefix / 'lib/libhttpserver.dylib'
        archive = prefix / 'lib/libhttpserver.a'
        native = build / 'test/v3_native_linkage'
        native_archive = build / 'src/.libs/libhttpserver_v3core.a'
        consumer = root / 'consumer'
        legacy = root / 'libmicrohttpd.dylib'
        for artifact in (shared, archive, native, native_archive, consumer, legacy):
            artifact.touch()

        # Replace only external inspection tools; audit orchestration, header
        # compilation, graph traversal and symbol policy all execute normally.
        real_run = subprocess.run
        def inspect(command, **kwargs):
            if command[:2] == ['otool', '-L']:
                dependency = str(legacy) if legacy_dependency and command[-1] == str(shared) else '/usr/lib/libSystem.B.dylib'
                output = command[-1] + ':\n\t' + dependency + ' (compatibility version 1.0.0, current version 1.0.0)\n'
            elif command[:2] == ['otool', '-l']:
                output = ''
            elif command[:2] == ['nm', '-u']:
                output = 'unused.o:\n U _MHD_start_daemon\n' if legacy_symbol and command[-1] == str(archive) else 'used.o:\n U _fixture_runtime\n'
            else:
                return real_run(command, **kwargs)
            return subprocess.CompletedProcess(command, 0, stdout=output, stderr='')

        with patch.object(audit, '__file__', str(source / 'scripts/audit-v3-installed-package.py')), \
                patch.object(audit.platform, 'system', return_value='Darwin'), \
                patch.object(subprocess, 'run', side_effect=inspect):
            return audit.audit(prefix, build, consumer, 'no', mode, self.root / 'provider')

    def test_strict_installed_audit_accepts_legacy_free_provenance(self):
        try:
            receipt = self.installed_audit()
        except audit.AuditError as error:
            self.fail('strict audit must assess legacy-free provenance: ' + str(error))
        self.assertEqual([], receipt['violations'])
        self.assertNotIn('libmicrohttpd', receipt['provenance']['installed/libhttpserver.pc'])
        self.assertNotIn('-lmicrohttpd', receipt['provenance']['src/Makefile.am'])

    def test_strict_installed_audit_rejects_actual_legacy_dependency(self):
        try:
            receipt = self.installed_audit(legacy_dependency=True)
        except audit.AuditError as error:
            self.fail('strict audit must assess dependency graph: ' + str(error))
        self.assertTrue(any('undeclared dependency (aggregate/strict)' in v and 'libmicrohttpd' in v
                            for v in receipt['violations']))

    def test_strict_installed_audit_rejects_unused_legacy_archive_member(self):
        try:
            receipt = self.installed_audit(legacy_symbol=True)
        except audit.AuditError as error:
            self.fail('strict audit must assess archive members: ' + str(error))
        self.assertIn('legacy archive symbol: MHD_start_daemon', receipt['violations'])

    def test_pre_cutover_installed_audit_requires_source_and_package_legacy_provenance(self):
        for source_legacy, installed_legacy in ((False, True), (True, False)):
            with self.subTest(source_legacy=source_legacy, installed_legacy=installed_legacy):
                with self.assertRaisesRegex(audit.AuditError, 'transitional declaration provenance missing'):
                    self.installed_audit('pre-cutover', source_legacy=source_legacy, installed_legacy=installed_legacy)

    def test_pre_cutover_installed_audit_accepts_declared_legacy_dependency(self):
        receipt = self.installed_audit('pre-cutover', source_legacy=True, installed_legacy=True, legacy_dependency=True)
        self.assertEqual([], receipt['violations'])

    def test_declared_legacy_closure_and_strict_rejection(self):
        graph = self.graph({'consumer': ['libhttpserver.dylib'], 'libhttpserver.dylib': ['libmicrohttpd.dylib'],
                            'libmicrohttpd.dylib': ['libgnutls.dylib'], 'libgnutls.dylib': ['libnettle.dylib'],
                            'libnettle.dylib': []})
        self.assertEqual([], self.violations(graph))
        self.assertTrue(self.violations(graph, mode='strict'))
        self.assertTrue(self.violations(graph, surface='native'))
        self.assertTrue(self.violations(graph, declarations={'legacy_edges': {}}))

    def test_undeclared_direct_and_transitive_dependencies(self):
        for dependency in ('libcurl.dylib', 'libsurprise.dylib', 'libnettle.dylib'):
            with self.subTest(dependency=dependency):
                graph = self.graph({'consumer': [dependency], dependency: []})
                self.assertTrue(self.violations(graph))
        graph = self.graph({'consumer': ['libmicrohttpd.dylib'], 'libmicrohttpd.dylib': ['libcurl.dylib'], 'libcurl.dylib': []})
        self.assertTrue(self.violations(graph))

    def test_selected_native_provider_only(self):
        graph = self.graph({'consumer': ['libssl.dylib'], 'libssl.dylib': ['libcrypto.dylib'], 'libcrypto.dylib': []})
        self.assertEqual([], self.violations(graph, mode='strict', tls='yes', surface='native'))
        self.assertEqual([], self.violations(graph, mode='strict', tls='yes', surface='aggregate'))
        self.assertTrue(self.violations(graph, tls='no', surface='native'))
        self.assertTrue(audit.graph_violations(graph, 'strict', 'yes', 'native', self.policy, self.root / 'other-provider'))

    def test_unknown_system_library_is_not_allowed(self):
        graph = {'nodes': [{'id': 'consumer'}, {'id': '/usr/lib/libsurprise.dylib'}],
                 'edges': [{'parent': 'consumer', 'name': '/usr/lib/libsurprise.dylib', 'child': '/usr/lib/libsurprise.dylib'}]}
        self.assertTrue(self.violations(graph))

    def test_missing_edge_and_unresolved_dependency_fail_closed(self):
        graph = self.graph({'consumer': ['libSystem.B.dylib'], 'libSystem.B.dylib': []})
        missing_edges = dict(graph, edges=[])
        self.assertTrue(self.violations(missing_edges))
        graph['edges'][0]['child'] = 'missing'
        self.assertTrue(self.violations(graph))
        with self.assertRaises(audit.AuditError):
            audit.dependency_graph(['consumer'], lambda path: [('libmissing', '/no/such/library')])

    def test_cycles_and_symlink_aliases_retain_edges(self):
        (self.root / 'alias').symlink_to('libssl.dylib')
        graph = self.graph({'consumer': ['alias', 'libssl.dylib'], 'libssl.dylib': ['libcrypto.dylib'], 'libcrypto.dylib': ['libssl.dylib']})
        self.assertEqual(3, len(graph['nodes']))
        self.assertEqual(4, len(graph['edges']))
        self.assertEqual([], self.violations(graph, mode='strict', tls='yes', surface='native'))

    def test_inspector_errors_and_malformed_output(self):
        for output in ('', 'nonsense', '/tmp/a:\n garbage', '/tmp/a:\n libfoo (bad)'):
            with self.subTest(output=output), self.assertRaises(audit.AuditError):
                audit.parse_otool(output, '/tmp/a')
        self.assertEqual(['/usr/lib/libSystem.B.dylib'], audit.parse_otool(
            '/tmp/a:\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)\n', '/tmp/a'))
        with self.assertRaises(audit.AuditError):
            audit.run(['false'])
        with self.assertRaises(audit.AuditError):
            audit.dependency_graph(['consumer'], lambda path: None)

    def test_dylib_install_identity_alias_is_not_a_dependency(self):
        target = self.root / 'libfoo.1.0.dylib'
        target.touch()
        alias = self.root / 'libfoo.1.dylib'
        alias.symlink_to(target.name)
        output = str(target) + ':\n\t' + str(alias) + ' (compatibility version 1.0.0, current version 1.0.0)\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)\n'
        self.assertEqual(['/usr/lib/libSystem.B.dylib'], audit.parse_otool(output, target))

    def test_declared_legacy_platform_support_edges(self):
        graph = self.graph({'consumer': ['libgnutls.dylib'], 'libgnutls.dylib': ['libintl.dylib', 'libunistring.dylib', 'libidn2.dylib'],
                            'libintl.dylib': ['CoreServices'], 'libunistring.dylib': ['libiconv.dylib', 'CoreFoundation', 'CoreServices'],
                            'libidn2.dylib': ['CoreFoundation'], 'libiconv.dylib': [], 'CoreFoundation': [], 'CoreServices': []})
        self.assertEqual([], self.violations(graph))
        self.assertTrue(self.violations(graph, mode='strict'))

    def test_archive_symbols_even_unused_members(self):
        for symbol in ('MHD_start_daemon', 'gnutls_init', 'curl_easy_init'):
            self.assertTrue(audit.symbol_violations('unused.o:\n U _' + symbol, 'native', 'yes', 'pre-cutover'))
        self.assertTrue(audit.symbol_violations('unused.o:\n U _SSL_new', 'native', 'no', 'pre-cutover'))
        self.assertEqual([], audit.symbol_violations('used.o:\n U _SSL_new', 'native', 'yes', 'strict'))
        self.assertTrue(audit.symbol_violations('unused.o:\n U MHD_start_daemon', 'aggregate', 'no', 'strict'))

    def test_effective_headers_and_private_installation(self):
        self.assertEqual([], audit.header_violations('class session {};', ['/usr/include/c++/vector']))
        for declaration in ('struct MHD_WebSocketStream;', 'SSL_CTX *ctx;', 'gnutls_session_t session;'):
            self.assertTrue(audit.header_violations(declaration, []))
        for path in ('/opt/include/openssl/ssl.h', '/opt/include/microhttpd.h', '/opt/include/gnutls/gnutls.h',
                     '/prefix/include/httpserver/detail/foo.hpp', '/usr/include/sys/socket.h'):
            self.assertTrue(audit.header_violations('', [path]))
        self.assertTrue(audit.header_violations('', ['/source/src/httpserver/http.hpp'], forbidden_roots=['/source']))
        self.assertTrue(audit.layout_violations(['httpserver/detail/foo.hpp']))
        self.assertTrue(audit.layout_violations(['httpserver/foo_impl.hpp']))

    def test_consumer_flags_cannot_hide_backend_or_import_source(self):
        for flags in (['-DHTTPSERVER_COMPILATION'], ['-DHAVE_GNUTLS=1'], ['-DNATIVE_V3_TLS'],
                      ['-I/source/src'], ['-I/build/generated'], ['-include', '/source/config.h']):
            self.assertTrue(audit.consumer_flag_violations(flags, ['/source', '/build']))
        self.assertEqual([], audit.consumer_flag_violations(['-I/prefix/include', '-L/provider/lib', '-lhttpserver'], ['/source', '/build']))

    def test_sanitized_environment_drops_ambient_consumer_configuration(self):
        from unittest.mock import patch
        with patch.dict('os.environ', {'CPATH': '/source/src', 'CPPFLAGS': '-DHTTPSERVER_COMPILATION',
                                      'PKG_CONFIG_PATH': '/source', 'PKG_CONFIG_SYSROOT_DIR': '/fake', 'CXXFLAGS': '-O3'}):
            env = audit.clean_environment()
            self.assertFalse({'CPATH', 'CPPFLAGS', 'PKG_CONFIG_PATH', 'PKG_CONFIG_SYSROOT_DIR', 'CXXFLAGS'} & env.keys())

    def test_installed_gated_fragment_uses_umbrella_without_internal_macros(self):
        include = self.root / 'prefix/include'
        (include / 'httpserver').mkdir(parents=True)
        (include / 'httpserver.hpp').write_text('#define _HTTPSERVER_HPP_INSIDE_\n#include <httpserver/fragment.hpp>\n#undef _HTTPSERVER_HPP_INSIDE_\n')
        (include / 'httpserverpp').symlink_to('httpserver.hpp')
        (include / 'httpserver/fragment.hpp').write_text('#ifndef _HTTPSERVER_HPP_INSIDE_\n#error umbrella required\n#endif\nstruct fragment {};\n')
        headers, violations = audit.audit_headers(self.root / 'prefix', self.root / 'source', self.root / 'build', 'c++')
        self.assertEqual([], violations)
        self.assertIn('httpserver/fragment.hpp', headers['declaration_sha256'])
        self.assertIn('httpserver.hpp', headers['include_traces'])
        preprocess = [r for r in audit.COMMANDS if '-E' in r['argv']]
        self.assertIn('stdout_sha256', preprocess[-1])
        self.assertNotIn('stdout', preprocess[-1])

    def test_installed_test_request_builder_is_available_through_umbrella(self):
        source = Path(__file__).resolve().parent.parent
        audit.run(['c++', '-std=c++20', '-fsyntax-only', '-x', 'c++', '-I' + str(source / 'src'), '-'],
                  input='#include <httpserver.hpp>\nhttpserver::create_test_request builder;\n')

    def test_static_consumer_uses_same_declared_legacy_policy(self):
        graph = self.graph({'static_consumer': ['libmicrohttpd.dylib'], 'libmicrohttpd.dylib': []})
        self.assertEqual([], self.violations(graph))
        self.assertTrue(self.violations(graph, mode='strict'))

    def test_installed_header_symlink_cannot_import_source(self):
        include = self.root / 'prefix/include'
        include.mkdir(parents=True)
        external = self.root / 'source.hpp'
        external.write_text('struct outside {};\n')
        (include / 'httpserver.hpp').symlink_to(external)
        (include / 'httpserverpp').symlink_to('httpserver.hpp')
        with self.assertRaises(audit.AuditError):
            audit.audit_headers(self.root / 'prefix', self.root / 'source', self.root / 'build', 'c++')

    def test_runtime_name_outside_platform_location_is_rejected(self):
        graph = self.graph({'consumer': ['libc++.1.dylib'], 'libc++.1.dylib': []})
        self.assertTrue(self.violations(graph, mode='strict'))

    def test_linux_loader_without_arrow_is_resolved(self):
        from unittest.mock import patch
        dynamic = 'Dynamic section at offset 0x1 contains 2 entries:\n (NEEDED) Shared library: [libc.so.6]\n (NEEDED) Shared library: [ld-linux-aarch64.so.1]\n'
        resolved = 'libc.so.6 => /usr/lib/libc.so.6 (0x1)\n /lib/ld-linux-aarch64.so.1 (0x2)\n'
        with patch.object(audit.platform, 'system', return_value='Linux'), patch.object(audit, 'run', side_effect=[dynamic, resolved]):
            self.assertEqual([('libc.so.6', '/usr/lib/libc.so.6'), ('ld-linux-aarch64.so.1', '/lib/ld-linux-aarch64.so.1')], audit.platform_inspector('/usr/lib/example.so'))
        with patch.object(audit.platform, 'system', return_value='Linux'), patch.object(audit, 'run', return_value=''):
            with self.assertRaises(audit.AuditError):
                audit.platform_inspector('/usr/lib/example.so')

    def test_static_linker_materializes_declared_provider_closure(self):
        graph = self.graph({'static_consumer': ['libgnutls.dylib', 'libgmp.dylib', 'libintl.dylib', 'libz.dylib', 'CoreServices'],
                            'libgnutls.dylib': [], 'libgmp.dylib': [], 'libintl.dylib': [], 'libz.dylib': [], 'CoreServices': []})
        self.assertEqual([], self.violations(graph))
        self.assertTrue(self.violations(graph, mode='strict'))
        graph = self.graph({'static_consumer': ['libcurl.dylib'], 'libcurl.dylib': []})
        self.assertTrue(self.violations(graph))

    def test_current_umbrella_has_no_backend_types(self):
        # Compile the real current umbrella as a consumer, comments removed by cpp.
        source = Path(__file__).resolve().parent.parent
        result = audit.run(['c++', '-std=c++20', '-E', '-x', 'c++', '-I' + str(source / 'src'), '-'],
                           input='#include <httpserver.hpp>\n')
        self.assertEqual([], audit.header_violations(result, []))


if __name__ == '__main__':
    unittest.main()
