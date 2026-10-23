#!/usr/bin/env python3
"""Adversarial policy fixtures; these are not installed-package proof."""
import hashlib
import importlib.util
import json
import os
import shlex
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import package_integration as integration

CXX = os.environ.get('CXX', 'c++')

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('audit-v3-installed-package.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class CompileProbeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name).resolve()
        self.source, self.build, self.prefix = (self.root / name for name in ('source', 'build', 'prefix'))
        self.unit = self.root / 'consumer.cpp'
        self.unit.write_text('int main() { return 0; }\n')
        self.header = self.prefix / 'include/httpserver.hpp'
        self.header.parent.mkdir(parents=True)
        self.header.write_text('struct installed_server {};\n')
        self.executable = self.root / 'consumer'
        self.calls = []

    def command(self, argv, cwd, env):
        self.calls.append((argv, cwd, env))
        if '-MF' in argv:
            Path(argv[argv.index('-MF') + 1]).write_text(
                'consumer.o: ' + shlex.join([str(self.unit), str(self.header)]) + '\n')
        Path(argv[argv.index('-o') + 1]).write_text('compiled artifact')

    def test_compile_probe_uses_host_link_map_syntax_preserving_arguments_and_receipt(self):
        compiler = 'cache-wrapper "compiler path/c++" --driver-mode=g++'
        flags = ['-I' + str(self.prefix / 'include'), '-DFIXTURE=1']
        libraries = ['-L' + str(self.prefix / 'lib'), '-lhttpserver', '-pthread', '-lssl', '-lcrypto']
        env = {'PATH': 'selected-toolchain'}
        map_path = str(self.executable) + '.map'
        for host, option in (('Darwin', '-Wl,-map,'), ('Linux', '-Wl,-Map,'),
                             ('FreeBSD', '-Wl,-Map,'), ('Windows', '-Wl,-Map,')):
            for options in (None, ['-O2', '-DNDEBUG']):
                with self.subTest(host=host, compile_options=options), patch('platform.system', return_value=host):
                    self.calls.clear()
                    receipt = integration.compile_probe(compiler, [self.unit], flags, libraries,
                        self.executable, self.command, env, self.source, self.build, self.prefix, False,
                        compile_options=options)
                    obj = str(self.root / 'consumer-0.o')
                    expected_options = ['-O0', '-g'] if options is None else options
                    self.assertEqual(self.calls, [
                        (shlex.split(compiler) + ['-std=c++20'] + expected_options + flags +
                         ['-MMD', '-MF', str(self.root / 'consumer-0.d'), '-c', str(self.unit), '-o', obj], self.root, env),
                        (shlex.split(compiler) + [obj] + libraries + [option + map_path, '-o', str(self.executable)], self.root, env)])
                    self.assertEqual(receipt['link_map'], map_path)
                    self.assertEqual(receipt['compile_options'], expected_options)
                    self.assertEqual(receipt['sources'], {str(self.unit): hashlib.sha256(self.unit.read_bytes()).hexdigest()})
                    self.assertEqual(receipt['dependency_sha256'], {
                        str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in (self.unit, self.header)})
                    self.assertEqual(receipt['dependencies'], sorted([str(self.unit), str(self.header)]))
                    self.assertEqual(receipt['artifact_sha256'], hashlib.sha256(self.executable.read_bytes()).hexdigest())
                    self.assertFalse(receipt['private_test_headers'])

    def test_compile_probe_rejects_source_header_before_linking(self):
        with patch('platform.system', return_value='Darwin'), self.assertRaisesRegex(ValueError, 'source public header contamination'):
            integration.compile_probe('c++', [self.unit], [], [], self.executable, self.command,
                {}, self.root, self.build, self.prefix, False)
        self.assertEqual(len(self.calls), 1)


class AuditTest(unittest.TestCase):
    def setUp(self):
        environment = patch.dict(os.environ)
        environment.start()
        self.addCleanup(environment.stop)
        os.environ.pop('V3_PACKAGE_PREREQUISITES', None)
        os.environ.pop('V3_PACKAGE_TOOLCHAIN', None)
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

    def violations(self, graph, mode='strict', tls='no', surface='aggregate', declarations=None):
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
        (source / 'configure').write_text('generated fixture configure\n')
        (source / 'src/Makefile.am').write_text('LIBADD = -lmicrohttpd\n' if source_legacy else 'LIBADD =\n')
        metadata = 'Requires.private: libmicrohttpd\n' if installed_legacy else 'Requires.private:\n'
        (source / 'libhttpserver.pc.in').write_text(metadata)
        (prefix / 'lib/pkgconfig/libhttpserver.pc').write_text(metadata)
        (build / 'libhttpserver.pc').write_text(metadata)
        (build / 'config.log').write_text('fixture configuration\n')
        (build / 'src/Makefile').write_text('fixture build\n')
        for name in ('config.h', 'Makefile', 'test/Makefile', 'examples/Makefile'):
            (build / name).parent.mkdir(parents=True, exist_ok=True)
            (build / name).write_text('fixture generated build\n')
        (prefix / 'lib/libhttpserver.la').write_text('dependency_libs=\"\"\n')
        (prefix / 'include/httpserver.hpp').write_text('struct fixture_server {};\n')
        (prefix / 'include/httpserverpp').write_text('#include <httpserver.hpp>\n')
        shared = prefix / 'lib/libhttpserver.dylib'
        archive = prefix / 'lib/libhttpserver.a'
        native = build / 'test/native_linkage'
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
            elif command[0] == 'nm':
                output = 'unused.o:\n U _MHD_start_daemon\n' if legacy_symbol and command[-1] == str(archive) else 'used.o:\n U _fixture_runtime\n'
            else:
                return real_run(command, **kwargs)
            return subprocess.CompletedProcess(command, 0, stdout=output, stderr='')

        with patch.object(audit, '__file__', str(source / 'scripts/audit-v3-installed-package.py')), \
                patch.object(audit.platform, 'system', return_value='Darwin'), \
                patch.object(subprocess, 'run', side_effect=inspect):
            return audit.audit(prefix, build, consumer, 'no', mode, self.root / 'provider', compiler=CXX)

    def test_strict_rejects_configure_flags_probes_and_generated_definitions(self):
        for text in ('AC_CHECK_HEADER([microhttpd.h])', 'AC_CHECK_LIB([microhttpd], [MHD_start_daemon])',
                     'CPPFLAGS=-I/legacy/microhttpd', 'LIBS=-lmicrohttpd_ws',
                     '#define HAVE_BAUTH 1', '#define HAVE_GNUTLS 1', 'LIBMICROHTTPD_CFLAGS=-I/old'):
            with self.subTest(text=text):
                self.assertTrue(audit.provenance_violations({'fixture': text}))
        self.assertEqual([], audit.provenance_violations({'fixture': 'LIBS=-lpthread -lws2_32'}))

    def test_strict_installed_audit_rejects_dormant_source_and_metadata_paths(self):
        for options in ({'source_legacy': True}, {'installed_legacy': True}):
            with self.subTest(options=options):
                receipt = self.installed_audit(**options)
                self.assertTrue(any('legacy provenance' in v for v in receipt['violations']))

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

    def test_no_policy_mode_accepts_legacy_closure(self):
        graph = self.graph({'consumer': ['libhttpserver.dylib'], 'libhttpserver.dylib': ['libmicrohttpd.dylib'],
                            'libmicrohttpd.dylib': ['libgnutls.dylib'], 'libgnutls.dylib': []})
        for mode in ('pre-cutover', 'strict'):
            self.assertTrue(self.violations(graph, mode=mode))
            self.assertTrue(self.violations(graph, mode=mode, surface='native'))

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

    def test_selected_darwin_unwind_accepts_observed_llvm_closure_only(self):
        prefix = Path('/opt/homebrew/Cellar/llvm/23.1.0')
        cxx = str(prefix / 'lib/c++/libc++.1.0.dylib')
        abi = str(prefix / 'lib/c++/libc++abi.1.0.dylib')
        unwind = str(prefix / 'lib/unwind/libunwind.1.0.dylib')
        graph = {'nodes': [{'id': path} for path in ('consumer', cxx, abi, unwind)],
                 'edges': [{'parent': parent, 'name': child, 'child': child} for parent, child in
                           [('consumer', cxx), (cxx, abi), (cxx, unwind), (abi, unwind)]]}
        with patch.object(audit.platform, 'system', return_value='Darwin'):
            for selected in ('', str(prefix.parent / '22.0.0'), str(prefix)):
                with self.subTest(selected=selected), patch.dict(os.environ, V3_PACKAGE_TOOLCHAIN=selected):
                    errors = self.violations(graph)
                    if selected == str(prefix):
                        self.assertEqual([], errors)
                    else:
                        self.assertEqual([f'undeclared dependency (aggregate/strict): {parent} -> {unwind}'
                                          for parent in (cxx, abi)], errors)

    def test_selected_darwin_unwind_requires_canonical_directory_and_numeric_name(self):
        prefix = self.root / 'llvm/23.1.0'
        directory = prefix / 'lib/unwind'
        directory.mkdir(parents=True)
        target = directory / 'libunwind.1.0.dylib'
        target.write_text('selected runtime')
        alias = self.root / 'selected llvm'
        alias.symlink_to(prefix, target_is_directory=True)
        link = directory / 'libunwind.dylib'
        link.symlink_to(target.name)
        with patch.object(audit.platform, 'system', return_value='Darwin'), \
                patch.dict(os.environ, V3_PACKAGE_TOOLCHAIN=str(alias)):
            self.assertTrue(audit.runtime(str(target)))
            self.assertTrue(audit.runtime(str(link)))
            for path in (prefix / 'lib/libunwind.1.0.dylib', prefix / 'lib/c++/libunwind.1.0.dylib',
                         prefix.parent / 'other/lib/unwind/libunwind.1.0.dylib',
                         directory / '../libunwind.1.0.dylib', '/usr/lib/libunwind.1.dylib',
                         directory / 'libunwind.bad.dylib',
                         directory / 'libunwind.1.dylib.extra', directory / 'libunwind.1..0.dylib',
                         directory / 'libcurl.4.dylib'):
                with self.subTest(path=str(path)):
                    self.assertFalse(audit.runtime(str(path)))
            outside = self.root / 'libunwind.2.dylib'
            outside.touch()
            escape = directory / 'libunwind.2.dylib'
            escape.symlink_to(outside)
            self.assertFalse(audit.runtime(str(escape)))
            target.unlink()
            directory.rename(prefix / 'lib/sibling')
            directory.symlink_to(prefix / 'lib/sibling', target_is_directory=True)
            (directory / target.name).touch()
            self.assertFalse(audit.runtime(str(directory / target.name)))

    def test_selected_unwind_still_inspects_hashes_and_rejects_transitive_provider(self):
        prefix = self.root / 'llvm'
        directory = prefix / 'lib/unwind'
        directory.mkdir(parents=True)
        consumer, unwind, forbidden = self.root / 'consumer', directory / 'libunwind.1.dylib', self.root / 'libcrypto.dylib'
        links = {str(consumer): [str(unwind)], str(unwind): [str(forbidden)], str(forbidden): []}
        for path in links:
            Path(path).write_text('inspected ' + path)
        inspected = []
        def inspect(path):
            inspected.append(path)
            return [(child, child) for child in links[path]]
        with patch.object(audit.platform, 'system', return_value='Darwin'), \
                patch.dict(os.environ, V3_PACKAGE_TOOLCHAIN=str(prefix)):
            graph = audit.dependency_graph([str(consumer)], inspect)
            self.assertEqual(set(links), set(inspected))
            self.assertEqual({path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in links},
                             {node['id']: node['sha256'] for node in graph['nodes']})
            self.assertEqual(['unselected native provider: ' + str(forbidden)], audit.graph_violations(
                                 graph, 'strict', 'yes', 'aggregate', self.policy, self.root / 'provider'))
            forbidden.rename(self.root / 'libmicrohttpd.dylib')
            with self.assertRaisesRegex(audit.AuditError, 'unresolved dependency'):
                audit.dependency_graph([str(consumer)], inspect)
        with patch.object(audit.platform, 'system', return_value='Linux'), \
                patch.dict(os.environ, V3_PACKAGE_TOOLCHAIN=str(prefix)):
            self.assertFalse(audit.runtime(str(unwind)))

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

    def test_legacy_platform_support_closure_is_rejected(self):
        graph = self.graph({'consumer': ['libgnutls.dylib'], 'libgnutls.dylib': ['libintl.dylib', 'libunistring.dylib', 'libidn2.dylib'],
                            'libintl.dylib': ['CoreServices'], 'libunistring.dylib': ['libiconv.dylib', 'CoreFoundation', 'CoreServices'],
                            'libidn2.dylib': ['CoreFoundation'], 'libiconv.dylib': [], 'CoreFoundation': [], 'CoreServices': []})
        self.assertTrue(self.violations(graph))
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
        headers, violations = audit.audit_headers(self.root / 'prefix', self.root / 'source', self.root / 'build', CXX)
        self.assertEqual([], violations)
        self.assertIn('httpserver/fragment.hpp', headers['declaration_sha256'])
        self.assertIn('httpserver.hpp', headers['include_traces'])
        preprocess = [r for r in audit.COMMANDS if '-E' in r['argv']]
        self.assertIn('stdout_sha256', preprocess[-1])
        self.assertNotIn('stdout', preprocess[-1])

    def test_native_public_api_is_available_through_umbrella(self):
        source = Path(__file__).resolve().parent.parent
        audit.run(shlex.split(CXX) + ['-std=c++20', '-fsyntax-only', '-x', 'c++', '-I' + str(source / 'src'), '-'],
                  input='#include <httpserver.hpp>\nhttpserver::server::server_options options;\n')

    def test_named_static_suite_rejects_legacy_metadata_closure(self):
        graph = self.graph({'native_http1_e2e': ['libgnutls.dylib'], 'libgnutls.dylib': []})
        graph['consumer_roles'] = {graph['roots'][0]: 'static_consumer'}
        self.assertTrue(self.violations(graph))
        self.assertTrue(self.violations(graph, mode='strict'))

    def test_static_consumer_rejects_legacy_libraries(self):
        graph = self.graph({'static_consumer': ['libmicrohttpd.dylib'], 'libmicrohttpd.dylib': []})
        self.assertTrue(self.violations(graph))
        self.assertTrue(self.violations(graph, mode='strict'))

    def test_installed_header_symlink_cannot_import_source(self):
        include = self.root / 'prefix/include'
        include.mkdir(parents=True)
        external = self.root / 'source.hpp'
        external.write_text('struct outside {};\n')
        (include / 'httpserver.hpp').symlink_to(external)
        (include / 'httpserverpp').symlink_to('httpserver.hpp')
        with self.assertRaises(audit.AuditError):
            audit.audit_headers(self.root / 'prefix', self.root / 'source', self.root / 'build', CXX)

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

    def test_static_linker_rejects_legacy_provider_closure(self):
        graph = self.graph({'static_consumer': ['libgnutls.dylib', 'libgmp.dylib', 'libintl.dylib', 'libz.dylib', 'CoreServices'],
                            'libgnutls.dylib': [], 'libgmp.dylib': [], 'libintl.dylib': [], 'libz.dylib': [], 'CoreServices': []})
        self.assertTrue(self.violations(graph))
        self.assertTrue(self.violations(graph, mode='strict'))
        graph = self.graph({'static_consumer': ['libcurl.dylib'], 'libcurl.dylib': []})
        self.assertTrue(self.violations(graph))

    def test_selected_prerequisite_location_cannot_allow_legacy_names(self):
        graph = self.graph({'consumer': ['libmicrohttpd.so'], 'libmicrohttpd.so': []})
        with patch.dict('os.environ', {'V3_PACKAGE_PREREQUISITES': str(self.root / 'selected')}):
            self.assertTrue(self.violations(graph))
        with patch.dict('os.environ', {'V3_PACKAGE_PREREQUISITES': str(self.root)}):
            self.assertTrue(self.violations(graph))

    def test_windows_header_trace_separators_cannot_hide_private_backend(self):
        for include in (r'C:\prefix\include\openssl\ssl.h',
                        r'C:\prefix\include\httpserver\detail\io.hpp'):
            self.assertTrue(audit.header_violations('', [include]))

    def test_case_insensitive_pe_system_leaf_provenance(self):
        with patch.object(audit.platform, 'system', return_value='Windows'), \
                patch.dict('os.environ', {'SystemRoot': str(self.root / 'Windows')}):
            graph = audit.dependency_graph([audit.resolve_pe('api-ms-win-core-file-l1-1-0.dll')])
            self.assertEqual('OS/API-set platform leaf', graph['nodes'][0]['provenance'])
            self.assertEqual([], graph['edges'])
            self.assertFalse(audit.runtime(str(self.root / 'kernel32.dll')))

    def test_pe_import_parser_fails_closed(self):
        self.assertEqual(['kernel32.dll', 'libssl-3-x64.dll'], audit.parse_pe(
            'example.exe: file format pei-x86-64\n DLL Name: KERNEL32.dll\n DLL Name: libssl-3-x64.dll\n'))
        for output in ('', 'not a PE', 'file format pei-x86-64\n DLL Name: '):
            with self.assertRaises(audit.AuditError):
                audit.parse_pe(output)

    def test_windows_families_and_import_symbols(self):
        for name, family in (('libhttpserver-3.dll', 'httpserver'), ('libssl-3-x64.dll', 'ssl'),
                             ('LIBCRYPTO-3-X64.DLL', 'crypto'), ('consumer.exe', 'consumer')):
            self.assertEqual(family, audit.family(name))
        for symbol in ('__imp_SSL_new', '__imp__MHD_start_daemon', '_gnutls_init@4'):
            self.assertTrue(audit.symbol_violations(' U ' + symbol, 'native', 'no', 'strict'))

    def test_windows_import_library_cannot_replace_static_archive(self):
        (self.root / 'bin').mkdir()
        (self.root / 'lib').mkdir()
        (self.root / 'bin/libhttpserver-3.dll').touch()
        (self.root / 'lib/libhttpserver.dll.a').touch()
        with self.assertRaises(audit.AuditError):
            audit.package_artifacts(self.root)
        (self.root / 'lib/libhttpserver.a').touch()
        shared, archives = audit.package_artifacts(self.root)
        self.assertEqual(['libhttpserver-3.dll'], [p.name for p in shared])
        self.assertEqual(['libhttpserver.a'], [p.name for p in archives])

    def test_pe_resolution_uses_explicit_roots_and_rejects_msys(self):
        root = self.root / 'toolchain'
        root.mkdir()
        (root / 'libstdc++-6.dll').touch()
        with patch.object(audit.platform, 'system', return_value='Windows'), \
                patch.dict('os.environ', {'V3_PACKAGE_DLL_DIRS': str(root), 'V3_PACKAGE_TOOLCHAIN': str(root)}):
            self.assertEqual(str((root / 'libstdc++-6.dll').resolve()), audit.resolve_pe('LIBSTDC++-6.DLL'))
            self.assertTrue(audit.runtime(str(root / 'libstdc++-6.dll')))
            with self.assertRaises(audit.AuditError):
                audit.resolve_pe('unresolved.dll')
            (root / 'msys-2.0.dll').touch()
            self.assertFalse(audit.runtime(str(root / 'msys-2.0.dll')))
            self.assertTrue(self.violations(self.graph({'consumer.exe': ['msys-2.0.dll'], 'msys-2.0.dll': []})))

    def test_freebsd_runtime_leaf_with_dynamic_section(self):
        with patch.object(audit.platform, 'system', return_value='FreeBSD'), \
                patch.object(audit, 'run', return_value='Dynamic section at offset 0 contains 1 entry:\n (NULL) 0x0'):
            self.assertEqual([], audit.platform_inspector('/lib/libc.so.7'))
        self.assertTrue(audit.runtime('/libexec/ld-elf.so.1'))
        self.assertFalse(audit.runtime('/tmp/ld-elf.so.1'))

    def test_current_umbrella_has_no_backend_types(self):
        # Compile the real current umbrella as a consumer, comments removed by cpp.
        source = Path(__file__).resolve().parent.parent
        result = audit.run(shlex.split(CXX) + ['-std=c++20', '-E', '-x', 'c++', '-I' + str(source / 'src'), '-'],
                           input='#include <httpserver.hpp>\n')
        self.assertEqual([], audit.header_violations(result, []))


if __name__ == '__main__':
    unittest.main()
