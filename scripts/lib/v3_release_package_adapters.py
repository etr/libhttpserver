"""Two archive/install-bound release adapters. No release-stage registry."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import shutil
import subprocess
import sys
import tarfile
from types import SimpleNamespace

import v3_native_benchmark as bench
import v3_package_matrix as matrix
from v3_package_commands import Commands
from v3_release_package_provenance import (catalog, validate_catalog, validate_compile,
    validate_process, validate_child_provenance, validate_runtime)
from v3_public_http2_inventory import PYTHON, POLICY, NODE, ONESHOTS, validate_matrix as validate_h2
from v3_public_http3_inventory import BASIC, SEMANTICS, CREDENTIALS, LIFECYCLE, CONFIGURATION, validate_matrix as validate_h3

require = bench.require
PUBLIC = 'public-capabilities'
BENCHMARK = 'native-throughput-latency'
H1_WS = ['upgrade', 'text_binary_fragmented', 'refusal', 'close_cancel', 'backpressure_cancel']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def archive_identity(archive, expected_sha256, source):
    archive, source = Path(archive).resolve(), Path(source).resolve()
    require(digest(archive) == expected_sha256, 'archive digest mismatch')
    hashes, names, roots = {}, set(), set()
    with tarfile.open(archive, 'r:*') as distribution:
        for member in distribution:
            name = PurePosixPath(member.name)
            require(not name.is_absolute() and '..' not in name.parts and '\\' not in member.name, 'unsafe archive member')
            require(member.name not in names and (member.isdir() or member.isfile()), 'duplicate/linked archive member')
            names.add(member.name); roots.add(name.parts[0])
            if member.isfile():
                require(len(name.parts) > 1, 'archive root file')
                relative = str(PurePosixPath(*name.parts[1:]))
                expected = hashlib.sha256(distribution.extractfile(member).read()).hexdigest()
                extracted = source / relative
                require(extracted.is_file() and not extracted.is_symlink() and extracted.resolve().is_relative_to(source), 'missing/unsafe extracted source')
                require(digest(extracted) == expected, 'changed extracted member: ' + relative)
                hashes[relative] = expected
    require(len(roots) == 1 and hashes, 'empty or inconsistent archive roots')
    return dict(path=str(archive), sha256=expected_sha256, members=hashes)


def required_inventory(family, tls):
    require(family in (PUBLIC, BENCHMARK) and tls in ('yes', 'no'), 'unknown adapter family/TLS')
    if family == BENCHMARK: return bench.required_cells(tls)
    return dict(supported_configurations=['tls', 'h2', 'h3'], h1=['http10', 'http11', 'post', 'persistent', 'pipeline', 'shutdown'], h1_websocket=H1_WS,
                tls=matrix.PUBLIC_TLS_CASES if tls == 'yes' else ['tls_off_same_source'],
                h2=dict(python=PYTHON | POLICY, node=NODE, oneshots=ONESHOTS) if tls == 'yes' else ['tls_off_h2_rejected'],
                h3=dict(basic=BASIC, semantics=SEMANTICS, credentials=CREDENTIALS, lifecycle=LIFECYCLE, configuration=CONFIGURATION)
                if tls == 'yes' else ['tls_off_h3_rejected'])


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)


def inputs_namespace(inputs, output=None, run_id=None, workload=None):
    values = dict(inputs) if isinstance(inputs, dict) else vars(inputs).copy()
    for key in ('archive', 'source', 'build', 'prefix', 'provider_prefix', 'quic_go'):
        values[key] = Path(values[key]).resolve()
    if output is not None: values['output'] = Path(output).resolve()
    if run_id is not None: values['run_id'] = run_id
    values.update(bench.DEFAULTS if workload is None else workload)
    values.setdefault('sanitize', '')
    return SimpleNamespace(**values)


def load_runner(source, filename):
    spec = importlib.util.spec_from_file_location(filename.replace('-', '_'), Path(source) / 'scripts' / filename)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def tool_identity(command):
    executable = shutil.which(command)
    require(executable is not None, 'missing explicitly selected tool: ' + command)
    return dict(path=str(Path(executable).resolve()), sha256=digest(executable))


def package_identity(prefix):
    return {str(p): digest(p) for p in Path(prefix).rglob('*') if p.is_file() and
            (p.suffix in ('.h', '.hpp', '.a', '.la', '.pc') or '.so' in p.name or '.dylib' in p.name or '.dll' in p.name)}


def provider_identity(prefix):
    return {str(p): digest(p) for p in Path(prefix).rglob('*') if p.is_file() and
            (p.suffix in ('.h', '.a', '.pc') or '.so' in p.name or p.parent == Path(prefix) / 'bin')}


def identity(inputs, workload=None):
    args = inputs_namespace(inputs, workload=workload)
    archive = archive_identity(args.archive, args.archive_sha256, args.source)
    package = package_identity(args.prefix)
    require(package and str(args.prefix / 'lib/libhttpserver.a') in package, 'missing installed headers/library')
    provider = provider_identity(args.provider_prefix)
    tools = {key: tool_identity(str(value)) for key, value in (
        ('compiler', shlex.split(args.compiler)[0]), ('client_python', args.client_python), ('node', args.node),
        ('quic_go', args.quic_go), ('pkg_config', 'pkg-config'))}
    script = "import importlib,importlib.metadata as m,json,pathlib; names=('h2','hpack','hyperframe','aioquic','cryptography'); print(json.dumps({n:{'version':m.version(n),'files':[str(p.resolve()) for p in pathlib.Path(importlib.import_module(n).__file__).parent.rglob('*') if p.is_file() and p.suffix in ('.py','.so')]} for n in names}))"
    process = subprocess.run([args.client_python, '-c', script], check=True, capture_output=True, text=True, timeout=15)
    clients = bench.parse_json(process.stdout)
    require(clients['h2']['version'] == '4.4.1' and clients['aioquic']['version'] == '1.3.0', 'Python client pin differs')
    for value in clients.values(): value['files'] = {p: digest(p) for p in value['files']}
    node_version = subprocess.run([args.node, '--version'], check=True, capture_output=True, text=True, timeout=10).stdout.strip()
    require(node_version == 'v24.15.0', 'Node client pin differs')
    quic = bench.parse_json(subprocess.run([str(args.quic_go), '--identity'], check=True, capture_output=True, text=True, timeout=10).stdout)
    require('v0.55.0' in canonical(quic), 'quic-go client pin differs')
    return dict(archive=archive, source=str(args.source), build=str(args.build), prefix=str(args.prefix),
                tls=args.tls, package=package, provider=provider, provider_prefix=str(args.provider_prefix), tools=tools, compiler=args.compiler,
                clients=clients, node_version=node_version, quic_go=quic,
                configuration={str(args.build / name): digest(args.build / name) for name in ('config.status', 'config.log', 'src/Makefile')},
                workload=dict(bench.DEFAULTS if workload is None else workload))


def identity_artifacts(expected):
    paths = [tool['path'] for tool in expected['tools'].values()]
    paths += [path for client in expected['clients'].values() for path in client['files']]
    if expected.get('source') and 'test/installed/v3_tls_client.cpp' in expected.get('archive', {}).get('members', {}):
        paths.append(str(Path(expected['source']) / 'test/installed/v3_tls_client.cpp'))
    return sorted(set(paths))


def check_workload(workload, require_full_workload):
    require(set(workload) == set(bench.DEFAULTS), 'incomplete workload')
    require(all(type(value) is int and value > 0 for value in workload.values()), 'invalid workload')
    limits = dict(warmup=65536, requests=65536, concurrency=8, payload=16384, workers=32, h3_concurrency=4, deadline=600)
    require(all(value <= limits[key] for key, value in workload.items()), 'workload bounds')
    require(not require_full_workload or workload == bench.DEFAULTS, 'reduced evidence cannot satisfy full workload')


def verify_identity(expected):
    archive = expected['archive']
    require(archive_identity(archive['path'], archive['sha256'], expected['source']) == archive, 'changed archive extraction')
    for group in ('package', 'provider', 'configuration'):
        require(expected[group] or group == 'provider', 'missing expected identity')
        for path, sha256 in expected[group].items(): require(digest(path) == sha256, 'changed ' + group + ' identity')
    for tool in expected['tools'].values(): require(digest(tool['path']) == tool['sha256'], 'changed tool identity')
    for client in expected['clients'].values():
        for path, sha256 in client['files'].items(): require(digest(path) == sha256, 'changed client identity')


def validate_base(path, expected, run_id, family):
    path = Path(path).resolve()
    receipt = bench.parse_json(path.read_text())
    require(receipt['schema_version'] == 1 and receipt['status'] == 'passed', 'failed adapter receipt')
    require(receipt['family'] == family and receipt['run_id'] == run_id, 'stale family/run ID')
    require(len(run_id) == 32 and all(c in '0123456789abcdef' for c in run_id), 'invalid run ID')
    require(receipt['identity'] == expected, 'stale archive/source/package/provider/tool/configuration identity')
    verify_identity(expected)
    inventory = bench.parse_json(canonical(required_inventory(family, expected['tls'])))
    require(receipt['inventory'] == inventory and receipt['inventory_sha256'] == hashlib.sha256(canonical(inventory).encode()).hexdigest(), 'incomplete required inventory')
    require(set(identity_artifacts(expected)) <= set(receipt['artifact_extras']), 'pruned tool/client artifacts')
    artifacts, commands = validate_catalog(receipt, path)
    require(Path(receipt['artifact_roots'][0]).is_absolute(), 'wrong artifact root')
    return receipt, artifacts, commands


def validate_h2_wire(row):
    require(row.get('alpn') == 'h2' and row.get('h1_alpn') == 'http/1.1' and
            row.get('response_status') == 200 and row.get('body') == 'installed-v3-h2-ok\n', 'missing meaningful H2 response witness')


def validate_ws_rows(rows):
    normalized = []
    for row in rows:
        value = row.copy()
        if row['case'] in ('upgrade', 'refusal'):
            require(row.get('status') == (101 if row['case'] == 'upgrade' else 426), 'wrong WebSocket HTTP status')
            value['status'] = 'passed'
        normalized.append(value)
    matrix.check_scenarios(normalized, H1_WS)
    by_case = {row['case']: row for row in rows}
    require(bench.positive_integer(by_case['upgrade']['port']) and by_case['upgrade']['port'] <= 65535, 'unbound WS port')
    exchange = by_case['text_binary_fragmented']
    require(exchange['text'] == 'public text' and exchange['binary_hex'] == '00ff62696e617279' and
            exchange['fragmented_hex'] == '667261676d656e7420e282ac20646f6e65' and exchange['pong'] == 'ping', 'wrong WS exchange bytes')
    close, pressure = by_case['close_cancel'], by_case['backpressure_cancel']
    require(close['clean_close_code'] == 1000 and close['close_callbacks'] == 2 and close['cancelled_receive'] == 1, 'missing close/cancellation')
    require(bench.positive_integer(pressure['blocked']) and type(pressure['cancelled']) is int and pressure['cancelled'] == 1 and pressure['close_callbacks'] == 3 and pressure['output_limit'] == 65546, 'missing bounded WS pressure cancellation')


def validate_public_receipt(path, expected_identity, expected_run_id):
    receipt, artifacts, commands = validate_base(path, expected_identity, expected_run_id, PUBLIC)
    require(set(receipt['children']) == {'h1', 'tls', 'h2', 'h3'}, 'missing public family')
    for family, record in receipt['children'].items():
        require(artifacts.get(record['path']) == record, 'unbound child receipt')
        child = bench.parse_json(Path(record['path']).read_text())
        require(child['run_id'] == expected_run_id, 'stale child run ID')
        validate_child_provenance(child, expected_identity, artifacts, commands)
        if expected_identity['tls'] == 'yes':
            if family == 'h2':
                validate_h2(child)
                for cell in child['cells']:
                    basic = next(r for r in cell['families'] if r.get('client') == 'hyper-h2' and r['family'] == 'basic')
                    validate_h2_wire(next(r for r in basic['scenarios'] if r['case'] == 'alpn_route_parity'))
            elif family == 'h3': validate_h3(child)
        for cell in child['cells']:
            if family == 'tls':
                matrix.check_scenarios(cell['scenarios'], receipt['inventory']['tls'])
                configuration = next(row for row in cell['scenarios'] if row['case'] == ('partial_bind_rollback' if expected_identity['tls'] == 'yes' else 'tls_off_same_source'))
                command = configuration['command_receipt']
                require(command in commands and command['exit'] == 0 and artifacts.get(command['log'], {}).get('sha256') == command['sha256'], 'missing TLS configuration command')
                require(('ROLLBACK_OK' if expected_identity['tls'] == 'yes' else 'PUBLIC_TLS_CONFIGURATION 14') in Path(command['log']).read_text(), 'missing TLS configuration observation')
                if expected_identity['tls'] == 'yes':
                    wire = cell.get('wire', [])
                    require(wire and all(r['body'] == 'installed-v3-tls-ok\n' and ' 200 ' in r['head'] for r in wire), 'missing meaningful TLS response witness')
                    acme = next(r for r in cell['scenarios'] if r['case'] == 'acme_actual443_exact')
                    require(acme['local_port'] == 443 and acme['client']['certificate_messages'] == 1, 'ACME witness requires actual TCP443')
            elif family == 'h1':
                matrix.check_scenarios(cell['traffic'], receipt['inventory']['h1'])
                validate_ws_rows(cell['websocket'])
                supported = cell['supported_configurations']
                matrix.check_scenarios(supported, receipt['inventory']['supported_configurations'])
                for row in supported:
                    require(row['outcome'] == ('ok' if expected_identity['tls'] == 'yes' else 'not_supported'), 'wrong supported configuration outcome')
                    if expected_identity['tls'] == 'yes':
                        require(bench.positive_integer(row['bound_port']) and row['bound_port'] <= 65535 and row['body'] == 'installed-v3-ok\n' and row['alpn'] == ('http/1.1' if row['case'] == 'tls' else row['case']), 'supported configuration lacks independent exchange')
                    else: require(row['bound_port'] == 0, 'TLS-off supported configuration bound')
                require(bench.positive_integer(cell['port']) and cell['port'] <= 65535 and all(r.get('responses', 1) > 0 for r in cell['traffic']), 'unbound H1/no exchange')
                for row in cell['traffic']:
                    if row['case'] in ('http10', 'http11', 'post'):
                        require(' 200 ' in row['response'] and row['body'] == ('bounded-post-body' if row['case'] == 'post' else 'installed-v3-ok\n'), 'wrong H1 response bytes')
                require(cell['websocket'][1]['text'] == 'public text' and cell['websocket'][1]['binary_hex'] == '00ff62696e617279', 'wrong H1 WS bytes')
                require(cell['websocket'][-1]['blocked'] > 0 and cell['websocket'][-1]['cancelled'] == 1, 'missing bounded WS cancellation')
            elif expected_identity['tls'] == 'no':
                rows = cell['families'] if family == 'h2' else cell['configuration']
                matrix.check_scenarios(rows, receipt['inventory'][family])
    return receipt


def validate_benchmark_receipt(path, expected_identity, expected_run_id, require_full_workload=False):
    receipt, artifacts, commands = validate_base(path, expected_identity, expected_run_id, BENCHMARK)
    check_workload(expected_identity['workload'], require_full_workload)
    require(receipt['full_workload'] == (expected_identity['workload'] == bench.DEFAULTS), 'false workload classification')
    require(set(receipt['children']) == {'benchmark'}, 'missing benchmark child')
    record = receipt['children']['benchmark']
    require(artifacts.get(record['path']) == record, 'unbound benchmark receipt')
    runner = load_runner(expected_identity['source'], 'run-v3-native-benchmarks.py')
    args = inputs_namespace(receipt['inputs'], workload=expected_identity['workload'])
    child = runner.validate(record['path'], runner.identity(args), expected_run_id)
    for linkage, consumer in child['consumers'].items():
        validate_compile(consumer['compile'], linkage, expected_identity, artifacts, commands)
        validate_runtime(consumer['compile']['link_map'][:-4], linkage, expected_identity)
    return receipt


def execute(inputs, output, run_id, family, workload=None):
    output = Path(output).resolve(); output.mkdir(parents=True, exist_ok=False)
    args = inputs_namespace(inputs, output, run_id, workload)
    require(len(run_id) == 32 and all(c in '0123456789abcdef' for c in run_id), 'invalid run ID')
    receipt = dict(schema_version=1, family=family, status='failed', run_id=run_id, children={},
                   nonlocal_checks='BSD/macOS/Windows unexecuted locally; CI and v3 PR owned')
    try:
        check_workload({key: getattr(args, key) for key in bench.DEFAULTS}, False)
        receipt['identity'] = identity(args, workload)
        receipt['inputs'] = {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items() if key not in ('output', 'run_id')}
        receipt['inventory'] = bench.parse_json(canonical(required_inventory(family, args.tls)))
        receipt['inventory_sha256'] = hashlib.sha256(canonical(receipt['inventory']).encode()).hexdigest()
        command = Commands(output)
        families = [('h1', 'check-v3-public-http1.py'), ('tls', 'check-v3-public-tls.py'), ('h2', 'check-v3-public-http2.py'), ('h3', 'check-v3-public-http3.py')] if family == PUBLIC else [('benchmark', 'run-v3-native-benchmarks.py')]
        roots = [str(output)]; extras = identity_artifacts(receipt['identity'])
        for name, script in families:
            child_output = output / name
            argv = [args.client_python, args.source / 'scripts' / script, '--source', args.source,
                    '--build', args.build, '--prefix', args.prefix, '--provider-prefix', args.provider_prefix,
                    '--tls', args.tls, '--compiler', args.compiler, '--output', child_output, '--run-id', run_id]
            if name == 'h2': argv += ['--node', args.node]
            if name == 'h3': argv += ['--quic-go', args.quic_go]
            if name == 'benchmark':
                argv += ['--client-python', args.client_python]
                for key in bench.DEFAULTS: argv += ['--' + key.replace('_', '-'), getattr(args, key)]
            command(argv, output, os.environ, timeout=1800 if name == 'h3' else 900)
            child_path = child_output / 'receipt.json'
            child = bench.parse_json(child_path.read_text())
            receipt['children'][name] = bench.artifact(child_path)
            roots.append(child['consumer_directory'])
            extras.extend(row['path'] for row in child.get('client_inputs', []))
        require(receipt['identity'] == identity(args, workload), 'inputs changed during execution')
        receipt['artifact_roots'], receipt['artifact_extras'] = roots, sorted(set(extras))
        receipt['artifacts'] = catalog(roots, extras)
        receipt['full_workload'] = receipt['identity']['workload'] == bench.DEFAULTS
        receipt['status'] = 'passed'
        path = output / 'receipt.json'
        path.write_text(json.dumps(receipt, indent=2, allow_nan=False) + '\n')
        validator = validate_public_receipt if family == PUBLIC else validate_benchmark_receipt
        validator(path, receipt['identity'], run_id)
    except BaseException as error:
        receipt.update(status='failed', error=str(error))
        raise
    finally:
        if receipt['status'] != 'passed':
            (output / 'receipt.json').write_text(json.dumps(receipt, indent=2, allow_nan=False) + '\n')
    return receipt


def run_public_capabilities(inputs, output, run_id):
    return execute(inputs, output, run_id, PUBLIC)


def run_native_benchmarks(inputs, output, run_id, workload):
    return execute(inputs, output, run_id, BENCHMARK, workload)
