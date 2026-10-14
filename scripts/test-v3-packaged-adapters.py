#!/usr/bin/env python3
"""Dependency-free rejection contracts; synthetic records are never live evidence."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import v3_native_benchmark as bench
import v3_package_matrix as matrix

SPEC = importlib.util.find_spec('v3_release_package_adapters')
adapter = importlib.util.module_from_spec(SPEC) if SPEC else None
if SPEC:
    SPEC.loader.exec_module(adapter)


class AdapterContracts(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(adapter, 'packaged adapter implementation missing')
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        (self.source / 'unit.cpp').write_text('public source\n')
        self.archive = self.root / 'source.tar.gz'
        with tarfile.open(self.archive, 'w:gz') as archive:
            archive.add(self.source, arcname='libhttpserver-3.0.0')
        self.digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()

    def test_archive_extraction_without_git_is_bound(self):
        identity = adapter.archive_identity(self.archive, self.digest, self.source)
        self.assertEqual(identity['members']['unit.cpp'], hashlib.sha256(b'public source\n').hexdigest())

    def test_archive_digest_and_extracted_member_tampering_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'archive'):
            adapter.archive_identity(self.archive, '0' * 64, self.source)
        (self.source / 'unit.cpp').write_text('different source')
        with self.assertRaisesRegex(ValueError, 'extracted'):
            adapter.archive_identity(self.archive, self.digest, self.source)

    def test_unsafe_duplicate_and_symlink_archive_members_are_rejected(self):
        for name, kind in [('pkg/../outside', tarfile.REGTYPE), ('pkg/link', tarfile.SYMTYPE), ('/absolute', tarfile.REGTYPE), ('pkg/file', tarfile.REGTYPE)]:
            with self.subTest(name=name, kind=kind):
                with tarfile.open(self.archive, 'w:gz') as archive:
                    info = tarfile.TarInfo(name); info.type = kind; info.size = 1 if kind == tarfile.REGTYPE else 0
                    archive.addfile(info, io.BytesIO(b'x') if info.size else None)
                    if name == 'pkg/file': archive.addfile(info, io.BytesIO(b'x'))
                digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
                with self.assertRaises(ValueError): adapter.archive_identity(self.archive, digest, self.source)

    def test_inventory_preserves_complete_public_families(self):
        inventory = adapter.required_inventory('public-capabilities', 'yes')
        self.assertEqual(inventory['tls'], matrix.PUBLIC_TLS_CASES)
        self.assertIn('public_rfc9220_writable_backpressure', inventory['h3']['configuration'])
        self.assertIn('backpressure_cancel', inventory['h1_websocket'])
        self.assertEqual(inventory['supported_configurations'], ['tls', 'h2', 'h3'])
        self.assertEqual(len(adapter.required_inventory('native-throughput-latency', 'yes')), 16)
        self.assertEqual(len(adapter.required_inventory('native-throughput-latency', 'no')), 4)

    def test_failed_stale_and_pruned_receipts_cannot_pass(self):
        for bad in ({}, {'schema_version': 1, 'status': 'failed'}, {'schema_version': 1, 'status': 'passed', 'run_id': 'old'}):
            path = self.root / 'receipt.json'; path.write_text(json.dumps(bad))
            with self.assertRaises((ValueError, KeyError)):
                adapter.validate_public_receipt(path, {}, 'a' * 32)

    def test_strict_json_rejects_duplicates_and_nonfinite_numbers(self):
        for content in ('{"status":"passed","status":"failed"}', '{"value":NaN}'):
            path = self.root / 'receipt.json'; path.write_text(content)
            with self.assertRaises(ValueError): adapter.validate_public_receipt(path, {}, 'a' * 32)

    def test_full_workload_gate_rejects_reduced_measurements(self):
        with self.assertRaisesRegex(ValueError, 'full workload'):
            adapter.check_workload(dict(bench.DEFAULTS, requests=16, warmup=4), True)
        adapter.check_workload(bench.DEFAULTS, True)

    def test_unknown_family_or_tls_mode_rejected(self):
        for family, tls in [('other', 'yes'), ('public-capabilities', 'maybe')]:
            with self.assertRaises(ValueError): adapter.required_inventory(family, tls)

    def test_existing_output_is_preserved_before_execution(self):
        output = self.root / 'output'; output.mkdir(); marker = output / 'marker'; marker.write_text('retained')
        with self.assertRaises(FileExistsError): adapter.run_public_capabilities({}, output, 'a' * 32)
        self.assertEqual(marker.read_text(), 'retained')

    def provenance(self):
        directory = self.root / 'consumer'; directory.mkdir()
        prefix = self.root / 'prefix'; (prefix / 'include/httpserver').mkdir(parents=True)
        (prefix / 'lib').mkdir()
        header = prefix / 'include/httpserver/server.hpp'; header.write_text('public header')
        library = prefix / 'lib/libhttpserver.a'; library.write_text('installed archive')
        unit = directory / 'consumer.cpp'; unit.write_text('public source\n')
        executable = directory / 'static'; executable.write_text('consumer')
        depfile = directory / 'static-0.d'; depfile.write_text(f'static.o: {unit} {header}\n')
        linkmap = directory / 'static.map'; linkmap.write_text('LOAD ' + str(library) + '\n')
        obj = directory / 'static-0.o'; obj.write_text('object')
        log = self.root / 'command.log'; log.write_text('compile/link ok')
        commands = [dict(argv=['c++', '-std=c++20', '-MMD', '-MF', str(depfile), '-c', str(unit), '-o', str(obj)],
                         exit=0, log=str(log), sha256=bench.artifact(log)['sha256'], bytes=log.stat().st_size),
                    dict(argv=['c++', str(obj), str(library), '-Wl,-Map,' + str(linkmap), '-o', str(executable)],
                         exit=0, log=str(log), sha256=bench.artifact(log)['sha256'], bytes=log.stat().st_size)]
        compiled = dict(private_test_headers=False, sources={str(unit): bench.artifact(unit)['sha256']},
                        dependencies=[str(unit), str(header)], dependency_sha256={str(p): bench.artifact(p)['sha256'] for p in (unit, header)},
                        artifact_sha256=bench.artifact(executable)['sha256'], link_map=str(linkmap))
        identity = dict(source=str(self.source), build=str(self.root / 'build'), prefix=str(prefix), compiler='c++',
                        archive={'members': {'test/installed/consumer.cpp': bench.artifact(unit)['sha256']}},
                        package={str(p): bench.artifact(p)['sha256'] for p in (header, library)})
        artifacts = {str(p): bench.artifact(p) for p in directory.iterdir() if p.is_file()}
        return compiled, identity, artifacts, commands

    def test_real_public_compile_artifacts_are_rechecked(self):
        compiled, identity, artifacts, commands = self.provenance()
        adapter.validate_compile(compiled, 'static', identity, artifacts, commands)
        Path(compiled['link_map']).unlink()
        with self.assertRaises(ValueError): adapter.validate_compile(compiled, 'static', identity, artifacts, commands)

    def test_private_empty_or_inconsistent_compile_records_are_rejected(self):
        compiled, identity, artifacts, commands = self.provenance()
        for key, value in [('private_test_headers', True), ('dependencies', []), ('sources', {}), ('dependency_sha256', {})]:
            bad = copy.deepcopy(compiled); bad[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                adapter.validate_compile(bad, 'static', identity, artifacts, commands)
        for flag in ('-DHTTPSERVER_COMPILATION', '-D_HTTPSERVER_HPP_INSIDE_', '-include', '-I' + str(self.source), '-I' + identity['build']):
            bad = copy.deepcopy(commands); bad[0]['argv'].append(flag)
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                adapter.validate_compile(compiled, 'static', identity, artifacts, bad)

    def test_copied_private_implementation_is_not_public_proof(self):
        compiled, identity, artifacts, commands = self.provenance()
        sha = next(iter(compiled['sources'].values()))
        identity['archive']['members'] = {'src/detail/private.cpp': sha}
        with self.assertRaises(ValueError): adapter.validate_compile(compiled, 'static', identity, artifacts, commands)

    def test_changed_installed_header_is_rejected_even_with_old_expected_identity(self):
        compiled, identity, artifacts, commands = self.provenance()
        Path(next(p for p in identity['package'] if '/include/' in p)).write_text('changed public header')
        with self.assertRaises(ValueError): adapter.validate_compile(compiled, 'static', identity, artifacts, commands)

    def test_empty_linkmap_and_foreign_dependency_are_rejected(self):
        compiled, identity, artifacts, commands = self.provenance()
        foreign = self.root / 'private.hpp'; foreign.write_text('private header')
        bad = copy.deepcopy(compiled)
        bad['dependencies'].append(str(foreign)); bad['dependency_sha256'][str(foreign)] = bench.artifact(foreign)['sha256']
        artifacts[str(foreign)] = bench.artifact(foreign)
        with self.assertRaises(ValueError): adapter.validate_compile(bad, 'static', identity, artifacts, commands)
        Path(compiled['link_map']).write_text('')
        artifacts[compiled['link_map']] = bench.artifact(compiled['link_map'])
        with self.assertRaises(ValueError): adapter.validate_compile(compiled, 'static', identity, artifacts, commands)

    def test_artifact_catalog_rejects_pruning_and_command_tampering(self):
        from v3_release_package_provenance import catalog, validate_catalog
        output = self.root / 'evidence'; output.mkdir()
        log = output / 'command.log'; log.write_text('live output')
        command = dict(argv=['client'], exit=0, log=str(log), sha256=bench.artifact(log)['sha256'], bytes=log.stat().st_size)
        (output / 'commands.json').write_text(json.dumps([command]))
        path = output / 'receipt.json'
        receipt = dict(artifact_roots=[str(output)], artifact_extras=[], artifacts=catalog([output]))
        validate_catalog(receipt, path)
        bad = copy.deepcopy(receipt); bad['artifacts'].pop()
        with self.assertRaises(ValueError): validate_catalog(bad, path)
        for key, value in [('exit', 1), ('error', 'timeout'), ('sha256', '0' * 64), ('bytes', 0)]:
            broken = dict(command, **{key: value}); (output / 'commands.json').write_text(json.dumps([broken]))
            receipt['artifacts'] = catalog([output])
            with self.subTest(key=key), self.assertRaises(ValueError): validate_catalog(receipt, path)

    def test_static_library_selection_and_depfiles_are_required(self):
        compiled, identity, artifacts, commands = self.provenance()
        bad = copy.deepcopy(commands); bad[1]['argv'][2] = '/other/libhttpserver.a'
        with self.assertRaises(ValueError): adapter.validate_compile(compiled, 'static', identity, artifacts, bad)
        Path(commands[0]['argv'][commands[0]['argv'].index('-MF') + 1]).unlink()
        with self.assertRaises(ValueError): adapter.validate_compile(compiled, 'static', identity, artifacts, commands)

    def test_process_requires_readiness_and_clean_exit(self):
        for bad in [dict(exit=0, stdout='', command=['server']), dict(exit=1, stdout='1234\n', command=['server']),
                    dict(exit=0, stdout='0\n', command=['server']), dict(exit=0, stdout='1234\n', command=['server'], error='timeout')]:
            with self.subTest(bad=bad), self.assertRaises(ValueError): adapter.validate_process(bad, require_port=True)
        adapter.validate_process(dict(exit=0, stdout='1234\n', stderr='', command=['server']), require_port=True)

    def test_benchmark_failure_preserves_supplied_run_id(self):
        from types import SimpleNamespace
        runner = adapter.load_runner(Path(__file__).parent.parent, 'run-v3-native-benchmarks.py')
        output = self.root / 'benchmark'
        args = SimpleNamespace(output=output, tls='yes', source=self.root / 'missing',
                               build=self.root / 'build', prefix=self.root / 'prefix',
                               run_id='b' * 32, **bench.DEFAULTS)
        with self.assertRaises((OSError, AttributeError)):
            runner.run(args)
        self.assertEqual(bench.parse_json((output / 'receipt.json').read_text())['run_id'], args.run_id)

    def test_tls_exchange_retains_actual_response_observation(self):
        from unittest.mock import patch
        runner = adapter.load_runner(Path(__file__).parent.parent, 'check-v3-public-tls.py')
        class Peer:
            def sendall(self, data): pass
        wire = []
        with patch.object(runner, 'response', return_value=(b'installed-v3-tls-ok\n', b'', 'HTTP/1.1 200 OK')):
            runner.exchange(Peer(), observations=wire)
        self.assertEqual(wire, [dict(protocol='1.1', head='HTTP/1.1 200 OK', body='installed-v3-tls-ok\n')])

    def test_h2_route_name_alone_cannot_supply_wire_proof(self):
        from v3_release_package_adapters import validate_h2_wire
        valid = dict(case='alpn_route_parity', alpn='h2', h1_alpn='http/1.1', body='installed-v3-h2-ok\n', response_status=200)
        validate_h2_wire(valid)
        for key, value in [('body', ''), ('response_status', 500), ('alpn', 'http/1.1')]:
            with self.subTest(key=key), self.assertRaises(ValueError): validate_h2_wire(dict(valid, **{key: value}))
        with self.assertRaises(ValueError): validate_h2_wire(dict(case='alpn_route_parity'))

    def test_provider_only_fixture_client_can_compile_from_identified_archive(self):
        from v3_release_package_provenance import validate_fixture_client
        unit = self.source / 'test/installed/v3_tls_client.cpp'; unit.parent.mkdir(parents=True); unit.write_text('provider-only client')
        executable = self.root / 'credential-fixtures'; executable.write_text('compiled client')
        identity = dict(source=str(self.source), compiler='c++', archive={'members': {'test/installed/v3_tls_client.cpp': bench.artifact(unit)['sha256']}})
        artifacts = {str(p): bench.artifact(p) for p in (unit, executable)}
        command = dict(argv=['c++', '-std=c++20', str(unit), '-o', str(executable)])
        client = dict(path=str(executable), sha256=bench.artifact(executable)['sha256'])
        validate_fixture_client(client, identity, artifacts, [command])
        broken = copy.deepcopy(identity); broken['archive']['members']['test/installed/v3_tls_client.cpp'] = '0' * 64
        with self.assertRaises(ValueError): validate_fixture_client(client, broken, artifacts, [command])

    def test_selected_process_tools_are_included_in_execution_artifacts(self):
        identity = dict(tools={'quic_go': {'path': '/selected/quic-go'}, 'node': {'path': '/selected/node'}},
                        clients={'aioquic': {'files': {'/client/module.py': 'hash'}}})
        self.assertEqual(set(adapter.identity_artifacts(identity)), {'/selected/quic-go', '/selected/node', '/client/module.py'})

    def test_catalog_rejects_exit_zero_without_server_readiness(self):
        from v3_release_package_provenance import catalog, validate_catalog
        output = self.root / 'evidence'; output.mkdir()
        server = output / 'server'; server.write_text('identified executable')
        log = output / 'command.log'; log.write_text('compiled')
        command = dict(argv=['compiler'], exit=0, log=str(log), sha256=bench.artifact(log)['sha256'], bytes=log.stat().st_size)
        (output / 'commands.json').write_text(json.dumps([command]))
        process = dict(command=[str(server)], exit=0, stdout='', stderr='')
        (output / 'process.json').write_text(json.dumps(process))
        receipt = dict(artifact_roots=[str(output)], artifact_extras=[], artifacts=catalog([output]))
        with self.assertRaises(ValueError): validate_catalog(receipt, output / 'receipt.json')

    def test_readonly_receipt_copy_keeps_original_artifact_catalog_binding(self):
        from v3_release_package_provenance import catalog, validate_catalog
        output = self.root / 'evidence'; output.mkdir()
        original = output / 'receipt.json'; original.write_text('{"original":true}')
        log = output / 'command.log'; log.write_text('actual command output')
        command = dict(argv=[sys.executable], exit=0, log=str(log), sha256=bench.artifact(log)['sha256'], bytes=log.stat().st_size)
        (output / 'commands.json').write_text(json.dumps([command]))
        records = catalog([output], excluded=[original])
        receipt = dict(artifact_roots=[str(output)], artifact_extras=[], artifacts=records)
        validate_catalog(receipt, self.root / 'readonly-copy.json')
        self.assertEqual(original.read_text(), '{"original":true}')

    def test_h3_lifecycle_keeps_its_lazy_h2_client_binding(self):
        import inspect
        runner = adapter.load_runner(Path(__file__).parent.parent, 'check-v3-public-http3.py')
        self.assertNotIn('H2Client', inspect.getclosurevars(runner.scenarios).unbound,
                         'aggregate/semantic HTTP2 witnesses cannot execute with an unbound client')

    def test_tls_configuration_commands_do_not_overwrite_profile_logs(self):
        import os
        from v3_package_commands import Commands
        runner = adapter.load_runner(Path(__file__).parent.parent, 'check-v3-public-tls.py')
        executable = self.root / 'tls-off-probe'
        executable.write_text('#!/bin/sh\necho PUBLIC_TLS_CONFIGURATION 14\n'); executable.chmod(0o755)
        directory = self.root / 'cell'; directory.mkdir()
        runner.scenarios(executable, 'managed', 'no', directory, os.environ, self.root)
        Commands(directory)([sys.executable, '-c', 'print("profile output")'], directory, os.environ)
        path = directory / 'credential-commands/commands.json'
        self.assertTrue(path.is_file(), 'TLS configuration command was overwritten by profile commands')
        commands = bench.parse_json(path.read_text())
        self.assertEqual(Path(commands[0]['log']).read_text().strip(), 'PUBLIC_TLS_CONFIGURATION 14')

    def test_provenance_cannot_claim_a_different_compiler(self):
        compiled, identity, artifacts, commands = self.provenance()
        commands[0]['argv'][0] = '/different/compiler'
        with self.assertRaises(ValueError): adapter.validate_compile(compiled, 'static', identity, artifacts, commands)

    def test_ws_http_status_is_distinct_from_execution_status(self):
        from v3_release_package_adapters import validate_ws_rows
        rows = [dict(case='upgrade', status=101, port=1234),
                dict(case='text_binary_fragmented', text='public text', binary_hex='00ff62696e617279', fragmented_hex='667261676d656e7420e282ac20646f6e65', pong='ping'),
                dict(case='refusal', status=426), dict(case='close_cancel', clean_close_code=1000, close_callbacks=2, cancelled_receive=1),
                dict(case='backpressure_cancel', blocked=2, cancelled=1, close_callbacks=3, output_limit=65546)]
        validate_ws_rows(rows)
        for index, key, value in [(0,'status',200),(0,'port',0),(1,'text',''),(1,'fragmented_hex',''),
                                  (2,'status',101),(3,'close_callbacks',0),(4,'cancelled',0),(4,'cancelled',True),(4,'blocked',True),(4,'status','failed')]:
            broken = copy.deepcopy(rows); broken[index][key] = value
            with self.subTest(index=index,key=key), self.assertRaises(ValueError): validate_ws_rows(broken)

    def test_provider_identity_binds_the_fixture_openssl_executable(self):
        provider = self.root / 'provider'; (provider / 'bin').mkdir(parents=True)
        executable = provider / 'bin/openssl'; executable.write_text('provider tool')
        self.assertEqual(adapter.provider_identity(provider), {str(executable): bench.artifact(executable)['sha256']})

    def test_installed_identity_binds_libtool_metadata_and_symlink_names(self):
        prefix = self.root / 'prefix'; (prefix / 'lib').mkdir(parents=True)
        archive = prefix / 'lib/libhttpserver.a'; archive.write_text('archive')
        metadata = prefix / 'lib/libhttpserver.la'; metadata.write_text('metadata')
        link = prefix / 'lib/libhttpserver.so'; link.symlink_to(archive.name)
        self.assertEqual(set(adapter.package_identity(prefix)), {str(archive), str(metadata), str(link)})

    def test_tls_negative_controls_require_decoded_rejection_and_exact_client(self):
        from v3_release_package_provenance import tls_rejection
        record = dict(command=['/consumer/independent-client', '1234', '13', 'psk', 'a.example', 'http/1.1', 'wrong-key', 'once', '/consumer/fixtures'],
                      exit=2, stdout='{"connected":false,"early_sent":0}\n')
        self.assertTrue(tls_rejection(record, record['stdout']))
        for key, value in [('exit', 1), ('error', 'timeout'), ('stdout', '{"connected":true}'), ('command', ['other-client'])]:
            bad = dict(record, **{key: value})
            self.assertFalse(tls_rejection(bad, bad['stdout']))

    def test_runtime_inspection_rejects_unrelated_executable_as_shared_consumer(self):
        from v3_release_package_provenance import validate_runtime
        identity = dict(source=str(Path(__file__).parent.parent), prefix=str(self.root / 'prefix'), tls='no')
        with self.assertRaises(ValueError): validate_runtime(sys.executable, 'shared', identity)

    def test_installed_h1_gate_is_public_and_dependency_free_to_import(self):
        runner_path = Path(__file__).parent / 'check-v3-public-http1.py'
        self.assertTrue(runner_path.is_file(), 'installed H1/WS runner missing')
        runner = adapter.load_runner(Path(__file__).parent.parent, 'check-v3-public-http1.py')
        self.assertTrue(callable(runner.run))
        self.assertTrue((Path(__file__).parent.parent / 'test/installed/v3_websocket_consumer.cpp').is_file())

    def test_archive_child_runners_have_callable_entry_points(self):
        for filename in ('check-v3-public-tls.py', 'check-v3-public-http2.py', 'check-v3-public-http3.py'):
            spec = importlib.util.spec_from_file_location(filename, Path(__file__).parent / filename)
            module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
            self.assertTrue(callable(getattr(module, 'run', None)), filename + ' must be package callable')


if __name__ == '__main__': unittest.main()
