#!/usr/bin/env python3
"""Measurement contracts; synthetic fixtures never supply live acceptance evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import v3_native_benchmark as bench


def observation():
    return dict(run_id='a' * 32, protocol='h1', units=dict(timestamp='ns', duration='s', latency='ms', throughput='requests/s'),
                clock=dict(name='monotonic_ns', resolution_ns=1), requested=4, attempted=4, completed=4, errors=0,
                warmup_requested=1, warmup_completed=1, epochs=[dict(start_ns=100, end_ns=1400)],
                samples=[dict(id=i, epoch=0, start_ns=100 + i * 100, end_ns=1100 + i * 100,
                              status=200, valid=True, protocol='h1') for i in range(4)],
                warmup=[dict(id=0, start_ns=1, end_ns=50)],
                statistics=dict(duration_s=.0000013, throughput_rps=4 / .0000013, p50_ms=.001, p95_ms=.001, p99_ms=.001))


class MeasurementTest(unittest.TestCase):
    def test_complete_samples(self):
        self.assertEqual(observation()['statistics'], bench.validate_observation(observation(), 'a' * 32, 'h1'))

    def test_invalid_measurement_cannot_pass(self):
        changes = [('samples', []), ('requested', 5), ('attempted', 5), ('completed', 3), ('errors', 1),
                   ('protocol', 'h2'), ('run_id', 'b' * 32), ('warmup_completed', 0)]
        for key, value in changes:
            bad = observation(); bad[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                bench.validate_observation(bad, 'a' * 32, 'h1')
        for change in ('duplicate', 'extra', 'reverse', 'zero', 'warmup', 'status', 'body', 'protocol', 'unit', 'nan', 'percentile', 'throughput', 'clock'):
            bad = observation()
            if change == 'duplicate': bad['samples'][1]['id'] = 0
            if change == 'extra': bad['samples'].append(copy.deepcopy(bad['samples'][0]))
            if change == 'reverse': bad['samples'][0]['end_ns'] = 99
            if change == 'zero': bad['epochs'][0]['end_ns'] = 100
            if change == 'warmup': bad['warmup'][0]['end_ns'] = 101
            if change == 'status': bad['samples'][0]['status'] = 500
            if change == 'body': bad['samples'][0]['valid'] = False
            if change == 'protocol': bad['samples'][0]['protocol'] = 'h2'
            if change == 'unit': bad['units']['latency'] = 's'
            if change == 'nan': bad['statistics']['p50_ms'] = float('nan')
            if change == 'percentile': bad['statistics']['p99_ms'] = .002
            if change == 'throughput': bad['statistics']['throughput_rps'] = 400001
            if change == 'clock': bad['clock']['resolution_ns'] = 0
            with self.subTest(change=change), self.assertRaises(ValueError):
                bench.validate_observation(bad, 'a' * 32, 'h1')

    def test_nearest_rank_without_trimming(self):
        self.assertEqual(99, bench.percentile(list(range(1, 101)), 99))
        self.assertEqual(2, bench.percentile([1, 2, 3], 50))

    def test_full_epoch_workloads_preserve_actual_warmup_totals(self):
        self.assertEqual((64, 512), bench.epoch_workload('h2', 4096, 8, 256))
        self.assertEqual((1024, 4096), bench.epoch_workload('h3', 4096, 4, 256))
        self.assertEqual(4, bench.effective_concurrency('h3', bench.DEFAULTS))
        self.assertEqual(8, bench.effective_concurrency('h2', bench.DEFAULTS))
        self.assertEqual((1, 256), bench.epoch_workload('h1', 4096, 8, 256))

    def test_epoch_duration_excludes_setup_and_warmup_gaps(self):
        value = observation()
        value['epochs'] = [dict(start_ns=100, end_ns=1200), dict(start_ns=10000, end_ns=11300)]
        for i in (2, 3):
            value['samples'][i].update(epoch=1, start_ns=10000 + (i - 2) * 300, end_ns=11000 + (i - 2) * 300)
        value['statistics'] = bench.statistics(value['samples'], value['epochs'])
        self.assertEqual(.0000024, bench.validate_observation(value, 'a' * 32, 'h1')['duration_s'])

    def test_missing_unknown_and_duplicate_cells(self):
        cells = bench.required_cells('yes')
        bench.validate_cells(cells, 'yes')
        for bad in (cells[:-1], cells + [cells[0]], cells + [('shared', 'managed', 'bogus')]):
            with self.assertRaises(ValueError): bench.validate_cells(bad, 'yes')

    def test_strict_json(self):
        for data in ('{"x":1,"x":2}', '{', '{"x":NaN}'):
            with self.assertRaises(ValueError): bench.parse_json(data)

    def test_artifacts_and_expected_identity(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'sample'; path.write_text('fresh')
            record = bench.artifact(path)
            bench.verify_artifact(record)
            path.write_text('changed')
            with self.assertRaises(ValueError): bench.verify_artifact(record)
            path.unlink()
            with self.assertRaises(ValueError): bench.verify_artifact(record)

    def test_process_success_requires_live_server_counts(self):
        good = dict(exit=0, stdout='1234\nCOUNTS ' + json.dumps(dict(run_id='a' * 32, warmup=1, measured=4, errors=0)) + '\n', stderr='')
        bench.validate_server(good, observation())
        for change in ('exit', 'missing', 'counts', 'run', 'errors', 'cleanup'):
            bad = copy.deepcopy(good)
            if change == 'exit': bad['exit'] = 1
            if change == 'missing': bad['stdout'] = '1234\n'
            if change == 'counts': bad['stdout'] = bad['stdout'].replace('"measured": 4', '"measured": 0')
            if change == 'run': bad['stdout'] = bad['stdout'].replace('a' * 32, 'b' * 32)
            if change == 'errors': bad['stdout'] = bad['stdout'].replace('"errors": 0', '"errors": 1')
            if change == 'cleanup': bad['error'] = 'timeout'
            with self.subTest(change=change), self.assertRaises(ValueError): bench.validate_server(bad, observation())

    def test_compile_probe_defaults_and_override(self):
        import v3_package_integration as integration
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); unit = root / 'consumer.cpp'; unit.write_text('')
            executable = root / 'consumer'; executable.write_text('')
            commands = []
            def command(argv, *_):
                commands.append(argv)
                if '-MF' in argv: Path(argv[argv.index('-MF') + 1]).write_text('consumer.o: ' + str(unit))
            with patch.object(integration.matrix, 'check_dependencies'):
                integration.compile_probe('c++', [unit], [], [], executable, command, {}, root, root, root, False)
                self.assertIn('-O0', commands[0]); self.assertIn('-g', commands[0])
                commands.clear()
                integration.compile_probe('c++', [unit], [], [], executable, command, {}, root, root, root, False, compile_options=['-O2'])
                self.assertIn('-O2', commands[0]); self.assertNotIn('-O0', commands[0])


class ReceiptTest(unittest.TestCase):
    def receipt(self, root, tls='no'):
        root = Path(root)
        consumer = root / 'consumer'; consumer.mkdir()
        files = {'consumer.cpp': 'consumer', 'v3_host_poll.hpp': 'poll', 'client.py': 'client',
                 'fixtures/root.pem': 'certificate', 'shared': 'shared binary', 'static': 'static binary',
                 'shared.map': 'shared map', 'static.map': 'static map', 'header.hpp': 'installed header',
                 'client-input.py': 'client dependency'}
        for name, content in files.items():
            path = consumer / name; path.parent.mkdir(exist_ok=True); path.write_text(content)
        sha = lambda path: bench.artifact(path)['sha256']
        identity = dict(source={name: sha(consumer / copy) for name, copy in (
            ('test/installed/v3_benchmark_consumer.cpp', 'consumer.cpp'),
            ('examples/v3_host_poll.hpp', 'v3_host_poll.hpp'),
            ('test/installed/v3_benchmark_client.py', 'client.py'))},
            package={str(consumer / 'header.hpp'): sha(consumer / 'header.hpp')},
            fixtures={'root.pem': sha(consumer / 'fixtures/root.pem')}, tls=tls)
        consumers = {}
        for linkage in ('shared', 'static'):
            sources = {str(consumer / 'consumer.cpp'): sha(consumer / 'consumer.cpp')}
            dependencies = {str(consumer / name): sha(consumer / name) for name in
                            ('consumer.cpp', 'v3_host_poll.hpp', 'header.hpp')}
            consumers[linkage] = dict(compile=dict(sources=sources, dependencies=list(dependencies),
                dependency_sha256=dependencies, artifact_sha256=sha(consumer / linkage),
                link_map=str(consumer / (linkage + '.map')), private_test_headers=False))
        commands = []
        for index, output in enumerate(('compiler version', '1', 'client version',
                                       json.dumps([str(consumer / 'client-input.py')]))):
            log = root / ('command-%04d.log' % index); log.write_text(output)
            commands.append(dict(argv=['tool'], exit=0, log=str(log), sha256=sha(log), bytes=log.stat().st_size))
        (root / 'commands.json').write_text(json.dumps(commands))
        cells = []
        for index, (linkage, loop, protocol) in enumerate(bench.required_cells(tls)):
            observed = observation(); observed['protocol'] = protocol
            for row in observed['samples']: row['protocol'] = protocol
            cell_root = root / ('cell-%d' % index); cell_root.mkdir()
            samples = cell_root / 'samples.json'; samples.write_text(json.dumps(observed))
            server = cell_root / 'server.json'
            server.write_text(json.dumps(dict(exit=0, stdout='1\nCOUNTS ' + json.dumps(dict(
                run_id='a' * 32, warmup=1, measured=4, errors=0)))))
            cells.append(dict(linkage=linkage, loop=loop, protocol=protocol, observation=bench.artifact(samples),
                server=bench.artifact(server), statistics=observed['statistics'], client_exit=0, fresh_execution=True))
        return dict(schema_version=1, status='passed', run_id='a' * 32, tls=tls, identity=identity,
                    consumer_directory=str(consumer), consumers=consumers,
                    tools=dict(compiler='compiler version', pkg_config='1', clients='client version'),
                    client_inputs=[bench.artifact(consumer / 'client-input.py')], cells=cells,
                    artifacts=[bench.artifact(path) for path in root.rglob('*') if path.is_file()])

    def test_receipt_requires_bound_live_execution_and_current_inputs(self):
        with tempfile.TemporaryDirectory() as root:
            value = self.receipt(root); expected = copy.deepcopy(value['identity'])
            bench.validate_receipt(value, expected, 'a' * 32)
            for change in ('identity', 'run', 'live', 'missing', 'exit', 'artifact', 'config', 'reused'):
                bad = copy.deepcopy(value)
                if change == 'identity': bad['identity']['source'] = 'stale'
                if change == 'run': bad['run_id'] = 'b' * 32
                if change == 'live': bad['cells'][0]['fresh_execution'] = False
                if change == 'missing': del bad['cells'][0]['server']
                if change == 'exit': bad['cells'][0]['client_exit'] = 1
                if change == 'artifact': bad['artifacts'] = []
                if change == 'config': bad['workload'] = {'requests':100}
                if change == 'reused': bad['cells'][1]['observation'] = bad['cells'][0]['observation']
                with self.subTest(change=change), self.assertRaises((ValueError, KeyError)):
                    bench.validate_receipt(bad, expected, 'a' * 32)

    def test_tls_identity_cannot_be_downgraded_to_an_h1_only_matrix(self):
        with tempfile.TemporaryDirectory() as root:
            value = self.receipt(root, 'yes'); expected = copy.deepcopy(value['identity'])
            bench.validate_receipt(value, expected)
            value['tls'] = 'no'
            value['cells'] = [cell for cell in value['cells'] if cell['protocol'] == 'h1']
            with self.assertRaises(ValueError): bench.validate_receipt(value, expected)
        with tempfile.TemporaryDirectory() as root:
            value = self.receipt(root); expected = copy.deepcopy(value['identity']); value['tls'] = 'yes'
            with self.assertRaises(ValueError): bench.validate_receipt(value, expected)

    def test_exported_statistics_must_match_raw_samples(self):
        with tempfile.TemporaryDirectory() as root:
            value = self.receipt(root)
            for key in observation()['statistics']:
                for alteration in ('different', 'missing', 'nan', 'infinite', 'units'):
                    bad = copy.deepcopy(value); stats = bad['cells'][0]['statistics']
                    if alteration == 'different': stats[key] = 1e99
                    if alteration == 'missing': del stats[key]
                    if alteration == 'nan': stats[key] = float('nan')
                    if alteration == 'infinite': stats[key] = float('inf')
                    if alteration == 'units': stats[key.replace('_ms', '_s') + '_wrong_units'] = stats.pop(key)
                    with self.subTest(key=key, alteration=alteration), self.assertRaises(ValueError):
                        bench.validate_receipt(bad, value['identity'])

    def test_required_provenance_cannot_be_pruned_or_contradicted(self):
        with tempfile.TemporaryDirectory() as root:
            value = self.receipt(root)
            for key in ('consumers', 'consumer_directory', 'client_inputs', 'tools'):
                bad = copy.deepcopy(value); del bad[key]
                with self.subTest(missing=key), self.assertRaises((ValueError, KeyError)):
                    bench.validate_receipt(bad, value['identity'])
            for record in value['artifacts']:
                if Path(record['path']).name in ('samples.json', 'server.json', 'header.hpp'): continue
                bad = copy.deepcopy(value); bad['artifacts'].remove(record)
                with self.subTest(pruned=record['path']), self.assertRaises(ValueError):
                    bench.validate_receipt(bad, value['identity'])
            for change in ('executable', 'source', 'header', 'dependencies', 'link_map', 'client_inputs', 'tools'):
                bad = copy.deepcopy(value); compiled = bad['consumers']['shared']['compile']
                if change == 'executable': compiled['artifact_sha256'] = '0' * 64
                if change == 'source': compiled['sources'][str(Path(value['consumer_directory']) / 'consumer.cpp')] = '0' * 64
                if change == 'header': compiled['dependency_sha256'][str(Path(value['consumer_directory']) / 'header.hpp')] = '0' * 64
                if change == 'dependencies': compiled['dependencies'] = []
                if change == 'link_map': compiled['link_map'] = str(Path(root) / 'absent.map')
                if change == 'client_inputs': bad['client_inputs'] = []
                if change == 'tools': bad['tools'] = {}
                with self.subTest(change=change), self.assertRaises((ValueError, KeyError)):
                    bench.validate_receipt(bad, value['identity'])

    def test_runner_preflight_missing_client_and_normal_build_required(self):
        spec = importlib.util.spec_from_file_location('benchmark_runner', Path(__file__).with_name('run-v3-native-benchmarks.py'))
        runner = importlib.util.module_from_spec(spec); spec.loader.exec_module(runner)
        with self.assertRaises(ValueError): runner.normal_build('CXXFLAGS = -O0 -g', {})
        with self.assertRaises(ValueError): runner.normal_build('CXXFLAGS = -O2 -fsanitize=address', {})
        with self.assertRaises(ValueError): runner.normal_build('CXXFLAGS = -O2', {'LD_PRELOAD':'allocator.so'})
        runner.normal_build('CXXFLAGS = -O2', {})
        runner.normal_build('CXXFLAGS = -O0 -O2', {})
        runner.normal_build('CXXFLAGS = -O3 -O2', {})
        for flags in ('-O2 -O0', '-O3 -O1', '-O2 -Og', '-O2 -Os', '-O2 -Oz', '-O2 -O'):
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                runner.normal_build('CXXFLAGS = ' + flags, {})


class ClientTest(unittest.TestCase):
    def client(self):
        spec = importlib.util.spec_from_file_location('benchmark_client', Path(__file__).parent.parent / 'test/installed/v3_benchmark_client.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        return module

    def test_response_status_body_and_identity(self):
        client = self.client()
        expected = {'x-run': 'a' * 32, 'x-request': 'm0', 'x-protocol': 'h2'}
        client.check_response(200, expected, b'x' * 16, 'a' * 32, 'm0', 'h2', 16)
        for status, headers, body in ((500, expected, b'x' * 16), (200, expected, b'bad'),
                                      (200, dict(expected, **{'x-run': 'b' * 32}), b'x' * 16),
                                      (200, dict(expected, **{'x-protocol': 'h1'}), b'x' * 16)):
            with self.assertRaises(ValueError): client.check_response(status, headers, body, 'a' * 32, 'm0', 'h2', 16)

    def test_failed_response_retains_partial_measured_accounting(self):
        import http.server
        import subprocess
        import threading
        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'
            def do_GET(self):
                identifier = self.headers['x-request']
                self.send_response(500 if identifier == 'm1' else 200)
                self.send_header('x-run', self.headers['x-run']); self.send_header('x-request', identifier)
                self.send_header('x-protocol', 'h1'); self.send_header('Content-Length', '16')
                self.end_headers(); self.wfile.write(b'x' * 16)
            def log_message(self, *_): pass
        with tempfile.TemporaryDirectory() as root:
            server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
            thread = threading.Thread(target=server.serve_forever); thread.start()
            try:
                output = Path(root) / 'failed.json'
                child = subprocess.run([sys.executable, str(Path(__file__).parent.parent / 'test/installed/v3_benchmark_client.py'),
                    '--port', str(server.server_port), '--protocol', 'h1', '--run-id', 'a' * 32,
                    '--fixtures', root, '--warmup', '1', '--requests', '4', '--concurrency', '1',
                    '--payload', '16', '--output', str(output)], capture_output=True, timeout=10)
                value = bench.parse_json(output.read_text())
                self.assertNotEqual(0, child.returncode)
                self.assertEqual((2, 1, 1), (value['attempted'], value['completed'], value['errors']))
            finally:
                server.shutdown(); server.server_close(); thread.join()

    def test_live_h1_subprocess_completions(self):
        import http.server
        import subprocess
        import threading
        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'
            def do_GET(self):
                self.send_response(200)
                for key in ('x-run', 'x-request'): self.send_header(key, self.headers[key])
                self.send_header('x-protocol', 'h1'); self.send_header('Content-Length', '16')
                self.end_headers(); self.wfile.write(b'x' * 16)
            def log_message(self, *_): pass
        with tempfile.TemporaryDirectory() as root:
            server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
            thread = threading.Thread(target=server.serve_forever); thread.start()
            try:
                output = Path(root) / 'observation.json'
                subprocess.run([sys.executable, str(Path(__file__).parent.parent / 'test/installed/v3_benchmark_client.py'),
                                '--port', str(server.server_port), '--protocol', 'h1', '--run-id', 'a' * 32,
                                '--fixtures', root, '--warmup', '2', '--requests', '4', '--concurrency', '2',
                                '--payload', '16', '--output', str(output)], check=True, timeout=10)
                self.assertEqual(4, bench.validate_observation(bench.parse_json(output.read_text()), 'a' * 32, 'h1')
                                 and bench.parse_json(output.read_text())['completed'])
            finally:
                server.shutdown(); server.server_close(); thread.join()


if __name__ == '__main__': unittest.main()
