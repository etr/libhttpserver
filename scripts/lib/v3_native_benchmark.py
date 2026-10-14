"""Bounded native benchmark measurement and immutable evidence validation."""
from bisect import bisect_right
import hashlib
import json
import math
from pathlib import Path

UNITS = dict(timestamp='ns', duration='s', latency='ms', throughput='requests/s')
DEFAULTS = dict(warmup=256, requests=4096, concurrency=8, payload=1024, workers=4, h3_concurrency=4, deadline=300)


def require(value, message):
    if not value:
        raise ValueError(message)


def parse_json(text):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, 'duplicate JSON key: ' + key)
            result[key] = value
        return result
    def invalid(value):
        raise ValueError('nonfinite JSON number: ' + value)
    def finite_float(value):
        parsed = float(value)
        require(math.isfinite(parsed), 'nonfinite JSON number: ' + value)
        return parsed
    return json.loads(text, object_pairs_hook=pairs, parse_constant=invalid, parse_float=finite_float)


def required_cells(tls):
    require(tls in ('yes', 'no'), 'unknown TLS mode')
    protocols = ('h1', 'h1-tls', 'h2', 'h3') if tls == 'yes' else ('h1',)
    return [(linkage, loop, protocol) for linkage in ('shared', 'static')
            for loop in ('managed', 'external') for protocol in protocols]


def effective_concurrency(protocol, workload):
    return min(workload['concurrency'], workload['h3_concurrency']) if protocol == 'h3' else workload['concurrency']


def epoch_workload(protocol, requests, concurrency, warmup):
    if protocol not in ('h2', 'h3'): return 1, warmup
    epochs = math.ceil(requests / (64 if protocol == 'h2' else concurrency))
    return epochs, epochs * concurrency


def validate_cells(cells, tls):
    require(sorted(cells) == sorted(required_cells(tls)), 'missing, duplicate or unknown benchmark cells')


def positive_integer(value):
    return type(value) is int and value > 0


def percentile(values, rank):
    require(bool(values), 'no measured samples')
    return sorted(values)[math.ceil(len(values) * rank / 100) - 1]


def statistics(samples, epochs):
    duration = sum(epoch['end_ns'] - epoch['start_ns'] for epoch in epochs) / 1e9
    require(duration > 0 and math.isfinite(duration), 'invalid measured duration')
    latencies = [(sample['end_ns'] - sample['start_ns']) / 1e6 for sample in samples]
    return dict(duration_s=duration, throughput_rps=len(samples) / duration,
                **{'p%d_ms' % rank: percentile(latencies, rank) for rank in (50, 95, 99)})


def validate_observation(value, run_id, protocol):
    require(value['run_id'] == run_id and value['protocol'] == protocol, 'stale run/protocol')
    require(value['units'] == UNITS, 'wrong units')
    require(value['clock']['name'] == 'monotonic_ns' and positive_integer(value['clock']['resolution_ns']), 'wrong clock')
    count = value['requested']
    require(positive_integer(count) and count <= 65536, 'request bound')
    require(value['attempted'] == count == value['completed'] and value['errors'] == 0, 'incomplete or failed requests')
    samples, warmup, epochs = value['samples'], value['warmup'], value['epochs']
    require(len(samples) == count and {row['id'] for row in samples} == set(range(count)), 'sample membership')
    require(positive_integer(value['warmup_requested']) and value['warmup_requested'] <= 65536, 'warmup bound')
    require(value['warmup_requested'] == value['warmup_completed'] == len(warmup), 'incomplete warmup')
    require({row['id'] for row in warmup} == set(range(len(warmup))), 'warmup membership')
    require(epochs and len(epochs) <= count, 'missing measured epochs')
    previous = 0
    for epoch in epochs:
        require(positive_integer(epoch['start_ns']) and positive_integer(epoch['end_ns']) and
                previous < epoch['start_ns'] < epoch['end_ns'], 'invalid epoch clock')
        previous = epoch['end_ns']
    members_by_epoch = [[] for _ in epochs]
    for sample in samples:
        require(type(sample['id']) is int, 'invalid request ID')
        require(type(sample['epoch']) is int and 0 <= sample['epoch'] < len(epochs), 'unknown sample epoch')
        members_by_epoch[sample['epoch']].append(sample)
        epoch = epochs[sample['epoch']]
        require(positive_integer(sample['start_ns']) and positive_integer(sample['end_ns']) and
                epoch['start_ns'] <= sample['start_ns'] < sample['end_ns'] <= epoch['end_ns'], 'invalid request clock')
        require(sample['status'] == 200 and sample['valid'] is True and sample['protocol'] == protocol, 'invalid response')
    for index, epoch in enumerate(epochs):
        members = members_by_epoch[index]
        require(members and max(row['end_ns'] for row in members) == epoch['end_ns'], 'epoch completion boundary')
    epoch_ends = [epoch['end_ns'] for epoch in epochs]
    for row in warmup:
        require(positive_integer(row['start_ns']) and positive_integer(row['end_ns']) and row['start_ns'] < row['end_ns'], 'warmup clock')
        following = bisect_right(epoch_ends, row['start_ns'])
        require(following == len(epochs) or row['end_ns'] <= epochs[following]['start_ns'], 'warmup contamination')
    expected = statistics(samples, epochs)
    validate_statistics(value['statistics'], expected)
    return expected


def validate_statistics(actual, expected):
    require(set(actual) == set(expected), 'missing statistics or units')
    for key, result in expected.items():
        number = actual[key]
        require(type(number) in (int, float) and math.isfinite(number) and number > 0 and
                math.isclose(number, result, rel_tol=1e-12), 'statistics disagreement: ' + key)


def artifact(path):
    path = Path(path).resolve()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def verify_artifact(record):
    try:
        require(artifact(record['path']) == record, 'changed artifact: ' + record['path'])
    except OSError as error:
        raise ValueError('missing artifact: ' + record['path']) from error


def validate_server(server, observation):
    require(server.get('exit') == 0 and 'error' not in server, 'server exit/cleanup failure')
    lines = [line[7:] for line in server['stdout'].splitlines() if line.startswith('COUNTS ')]
    require(len(lines) == 1, 'missing or duplicate server observation')
    counts = parse_json(lines[0])
    require(counts == dict(run_id=observation['run_id'], warmup=observation['warmup_completed'],
                           measured=observation['completed'], errors=0), 'server/client counts differ')


def validate_provenance(receipt, expected_identity):
    records = {record['path']: record for record in receipt['artifacts']}
    require(len(records) == len(receipt['artifacts']), 'duplicate artifact paths')

    def bound(path, expected=None):
        path = str(Path(path).resolve())
        require(path in records, 'unbound provenance artifact: ' + path)
        if expected is not None:
            require(records[path]['sha256'] == expected, 'provenance digest disagreement: ' + path)
        return records[path]

    directory = Path(receipt['consumer_directory'])
    copies = {'consumer.cpp': 'test/installed/v3_benchmark_consumer.cpp',
              'v3_host_poll.hpp': 'examples/v3_host_poll.hpp',
              'client.py': 'test/installed/v3_benchmark_client.py'}
    local = {str(directory / name): expected_identity['source'][source] for name, source in copies.items()}
    for path, sha256 in local.items(): bound(path, sha256)
    for name, sha256 in expected_identity['fixtures'].items(): bound(directory / 'fixtures' / name, sha256)
    require(set(receipt['consumers']) == {'shared', 'static'}, 'missing consumer provenance')
    for linkage, consumer in receipt['consumers'].items():
        compiled = consumer['compile']
        bound(directory / linkage, compiled['artifact_sha256'])
        require(compiled['private_test_headers'] is False, 'private consumer headers')
        require(compiled['sources'] == {str(directory / 'consumer.cpp'): local[str(directory / 'consumer.cpp')]},
                'wrong consumer sources')
        deps = compiled['dependency_sha256']
        require(set(compiled['dependencies']) == set(deps), 'missing dependency digests')
        resolved = {str(Path(path).resolve()): sha256 for path, sha256 in deps.items()}
        require(all(path in resolved for path in local if not path.endswith('client.py')), 'missing local dependencies')
        require(any(path in expected_identity['package'] for path in resolved), 'missing installed headers')
        for path, sha256 in resolved.items():
            # Installed headers are bound by the independently supplied package identity.
            expected = expected_identity['package'].get(path, local.get(path))
            require(expected is not None and sha256 == expected, 'unexpected consumer dependency: ' + path)
            require(artifact(path)['sha256'] == sha256, 'changed consumer dependency: ' + path)
            if path in local: bound(path, sha256)
        require(Path(compiled['link_map']).resolve() == (directory / (linkage + '.map')).resolve(), 'wrong link map')
        bound(compiled['link_map'])
    output = Path(receipt['cells'][0]['observation']['path']).parent.parent
    command_record = bound(output / 'commands.json')
    commands = parse_json(Path(command_record['path']).read_text())
    require(len(commands) >= 4, 'missing tool commands')
    for command in commands:
        require(command['exit'] == 0 and 'error' not in command and command['argv'], 'failed provenance command')
        bound(command['log'], command['sha256'])
        require(records[str(Path(command['log']).resolve())]['bytes'] == command['bytes'], 'wrong command length')
    require(set(receipt['tools']) == {'compiler', 'pkg_config', 'clients'}, 'missing tool versions')
    for key, command in zip(('compiler', 'pkg_config', 'clients'), commands):
        require(receipt['tools'][key] and receipt['tools'][key] == Path(command['log']).read_text(), 'wrong tool version')
    inputs = receipt['client_inputs']
    require(inputs and [item['path'] for item in inputs] == parse_json(Path(commands[3]['log']).read_text()),
            'missing client input provenance')
    for record in inputs:
        require(bound(record['path']) == record, 'unbound client input')


def validate_receipt(receipt, expected_identity, expected_run_id=None):
    require(receipt['schema_version'] == 1 and receipt['status'] == 'passed', 'failed or unknown receipt')
    require(receipt['identity'] == expected_identity, 'stale source/package/configuration identity')
    require('workload' not in receipt or receipt['workload'] == expected_identity.get('workload'), 'stale workload')
    require(len(receipt['run_id']) == 32 and all(c in '0123456789abcdef' for c in receipt['run_id']), 'invalid run identifier')
    if expected_run_id is not None:
        require(receipt['run_id'] == expected_run_id, 'reused run identifier')
    require(receipt['tls'] == expected_identity['tls'], 'contradictory TLS mode')
    validate_cells([tuple(cell[k] for k in ('linkage', 'loop', 'protocol')) for cell in receipt['cells']], expected_identity['tls'])
    require(receipt['artifacts'], 'missing immutable artifacts')
    sample_paths = [cell['observation']['path'] for cell in receipt['cells']]
    require(len(sample_paths) == len(set(sample_paths)), 'reused samples across cells')
    for record in receipt['artifacts']: verify_artifact(record)
    validate_provenance(receipt, expected_identity)
    for cell in receipt['cells']:
        require(cell['client_exit'] == 0 and cell['fresh_execution'] is True, 'missing live client execution')
        for key in ('observation', 'server'):
            require(cell[key] in receipt['artifacts'], 'unbound observation artifact')
        observation = parse_json(Path(cell['observation']['path']).read_text())
        workload = expected_identity.get('workload')
        if workload is not None:
            require(observation['requested'] == workload['requests'], 'incomplete workload')
            concurrency = effective_concurrency(cell['protocol'], workload)
            require(cell['concurrency'] == concurrency and observation['concurrency'] == concurrency, 'wrong effective concurrency')
            expected_epochs, expected_warmup = epoch_workload(cell['protocol'], workload['requests'], concurrency, workload['warmup'])
            require(observation['warmup_requested'] == expected_warmup, 'wrong warmup workload')
            require(len(observation['epochs']) == expected_epochs, 'wrong epoch count')
        expected = validate_observation(observation, receipt['run_id'], cell['protocol'])
        validate_statistics(cell['statistics'], expected)
        validate_server(parse_json(Path(cell['server']['path']).read_text()), observation)
    return receipt
