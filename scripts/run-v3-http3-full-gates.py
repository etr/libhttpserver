#!/usr/bin/env python3
"""Explicit, fail-closed TASK-178 HTTP/3 release lane; never provisions tools."""
import argparse
import collections
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import signal
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import v3_release_artifact as release_identity

ROOT = Path(__file__).resolve().parents[1]
INVENTORY = ROOT / 'test/conformance/http3/full-gates.json'
OFF_SUITES = '''http3_frame http3_settings http3_connection http3_headers http3_exchange
http3_streaming http3_cancellation http3_qpack http3_qpack_scheduling http3_drain
http3_fuzz_replay qpack_primitives qpack_field_section qpack_dynamic_table
qpack_instructions qpack_dynamic_corpus qpack_accounting qpack_fuzz_replay
http3_websocket_handshake http3_websocket http3_websocket_flow http3_websocket_lifecycle
http3_websocket_harness websocket_conformance protocol_fuzz_replay quic_packet
quic_frame quic_transport_parameters quic_fuzz_replay quic_codec_allocation quic_varint quic_stream_state
quic_reassembly quic_ack_state quic_recovery quic_repacketize quic_recovery_fuzz_replay
quic_flow_control quic_flow_storage quic_flow_recovery quic_send_schedule quic_congestion
quic_pacing quic_connection_identity quic_path_mtu quic_datagram_sizing
quic_amplification quic_connection_lifecycle'''.split()
ON_SUITES = '''quic_tls_callbacks_fuzz_replay quic_tls_session quic_tls_handshake
quic_recovery_tls http3_network_owner quic_interop_tls quic_interop_files'''.split()
FUZZ_OFF = 'quic_parser quic_state quic_recovery http3_engine qpack websocket_codec'.split()
FUZZ_ON = ['quic_tls_callbacks']
STAGES = ('native-off', 'native-on', 'sanitized-off', 'sanitized-on',
          'quic-interop', 'http3-interop', 'websocket-interop', 'fuzz-off', 'fuzz-on')
CATEGORIES = {'framing', 'settings', 'critical-streams', 'semantics', 'trailers',
              'cancellation', 'goaway', 'qpack-codecs', 'qpack-dynamic', 'qpack-blocking',
              'qpack-limits', 'ws-negotiation', 'ws-tunnels', 'malformed', 'slow-peers',
              'transport', 'fuzz'}
NONLOCAL = [{'platform': p, 'status': 'deferred', 'owner': 'CI / v3 PR'}
            for p in ('BSD', 'Windows', 'macOS', 'other nonlocal platforms')]
MAX_ARTIFACT = 64 * 1024 * 1024


class GateFailure(RuntimeError):
    pass


def need(condition, message):
    if not condition:
        raise GateFailure(message)


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def artifact(path):
    path = Path(path).resolve()
    need(path.is_file() and path.stat().st_size <= MAX_ARTIFACT, 'missing/oversized artifact: ' + str(path))
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest(path)}


def verify_artifacts(records):
    paths = [r['path'] for r in records]
    need(len(paths) == len(set(paths)), 'duplicated artifact evidence')
    for record in records:
        need(artifact(record['path']) == record, 'artifact changed: ' + record['path'])


def read_json(path, limit=4 * 1024 * 1024):
    path = Path(path)
    need(path.is_file() and path.stat().st_size <= limit, 'missing/oversized JSON: ' + str(path))
    try:
        return json.loads(path.read_text())
    except (ValueError, UnicodeError) as error:
        raise GateFailure('malformed JSON: ' + str(path)) from error


def load_runner(filename):
    spec = importlib.util.spec_from_file_location(filename.replace('-', '_'), ROOT / 'scripts' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_inventory(document):
    need(document.get('schema_version') == 1, 'unknown inventory schema')
    rows = document.get('coverage', [])
    ids = [r['id'] for r in rows]
    need(collections.Counter(ids) == collections.Counter(CATEGORIES), 'missing/duplicated coverage category')
    need({r['rfc'] for r in rows} >= {'9114', '9204', '9220'}, 'missing RFC family')
    for row in rows:
        need(row['rfc'] in ('9114', '9204', '9220', '9000'), 'unknown RFC')
        need(row['stage'] in STAGES and row['sections'] and row['requirements'], 'untraceable stage/requirement')
        need((ROOT / row['source']).is_file(), 'missing source evidence')
        suite = row['suite']
        if row['stage'].endswith('-interop'):
            expected = {'quic-interop': 'task173', 'http3-interop': 'task174', 'websocket-interop': 'websocket'}
            need(suite == expected[row['stage']], 'wrong matrix')
            need(row['rfc'] == '9000' or row['stage'] != 'quic-interop', 'hq-interop is not HTTP/3')
        elif row['stage'].startswith('fuzz-'):
            need(suite in FUZZ_OFF + FUZZ_ON, 'unknown fuzz target')
        else:
            need(suite in OFF_SUITES + ON_SUITES, 'unknown native suite')
        for supporting in row.get('supporting_evidence', []):
            need(supporting['stage'] in STAGES and (ROOT / supporting['source']).exists(), 'unknown supporting evidence')
            allowed = {'quic-interop': ['task173'], 'http3-interop': ['task174'], 'websocket-interop': ['websocket'],
                       'fuzz-off': FUZZ_OFF, 'fuzz-on': FUZZ_ON}.get(supporting['stage'], OFF_SUITES + ON_SUITES)
            need(supporting['suite'] in allowed, 'unknown supporting suite')
        need(row['kind'] in ('deterministic transcript', 'independent network witness', 'fuzz exploration'), 'unknown evidence kind')
    exclusions = document.get('exclusions', [])
    need(len({e['id'] for e in exclusions}) == len(exclusions), 'duplicate exclusion')
    need(all(e['id'] not in CATEGORIES | set(STAGES) | set(OFF_SUITES + ON_SUITES + FUZZ_OFF + FUZZ_ON) and e['reason'].strip() for e in exclusions), 'required/empty exclusion')
    matrix = read_json(ROOT / 'test/interop/quic/task173-matrix.json')
    need(document.get('quic_exclusions') == matrix['exclusions'], 'QUIC exclusions changed')
    need(document.get('stages') == list(STAGES), 'missing required stage')
    registration = (ROOT / 'test/Makefile.am').read_text()
    need(all(re.search(r'^' + suite + r'_SOURCES\s*=', registration, re.M) for suite in OFF_SUITES + ON_SUITES), 'unregistered suite')


def native_summary(output):
    result = {}
    for name, pattern in [('tests', r'^(\d+) tests executed$'), ('checks', r'^(\d+) checks$'),
                          ('successes', r'^-> (\d+) successes$'), ('failures', r'^-> (\d+) failures$'),
                          ('skipped', r'^-> (\d+) skipped$')]:
        values = re.findall(pattern, output, re.M)
        need(len(values) == 1, 'missing/duplicate native ' + name)
        result[name] = int(values[0])
    need(result['tests'] > 0 and result['checks'] > 0 and result['successes'] == result['checks']
         and result['failures'] == result['skipped'] == 0, 'failed/skipped/empty native results')
    return result


def create_output(path):
    need(not path.exists(), 'output already exists; preserve prior evidence')
    path.mkdir(parents=True, mode=0o700)


def run_process(command, cwd, output, timeout, limit=MAX_ARTIFACT):
    output.mkdir(mode=0o700)
    row = {'command': list(map(str, command)), 'cwd': str(cwd), 'status': 'failed',
           'stdout': str(output / 'stdout.log'), 'stderr': str(output / 'stderr.log'), 'timeout_seconds': timeout}
    start = time.monotonic()
    process = None
    try:
        with open(row['stdout'], 'wb') as stdout, open(row['stderr'], 'wb') as stderr:
            process = subprocess.Popen(row['command'], cwd=cwd, stdout=stdout, stderr=stderr, start_new_session=True)
            while process.poll() is None:
                need(time.monotonic() - start < timeout, 'stage deadline exceeded')
                need(sum(Path(row[k]).stat().st_size for k in ('stdout', 'stderr')) <= limit, 'stage output bound exceeded')
                time.sleep(.05)
            row['returncode'] = process.returncode
        need(sum(Path(row[k]).stat().st_size for k in ('stdout', 'stderr')) <= limit, 'stage output bound exceeded')
        need(row['returncode'] == 0, 'process exited ' + str(row['returncode']))
        row['status'] = 'passed'
    except (GateFailure, OSError) as error:
        row['failure'] = str(error)
    finally:
        if process and process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
            row['returncode'] = process.returncode
        row['duration_seconds'] = round(time.monotonic() - start, 3)
        (output / 'process.json').write_text(json.dumps(row, indent=2) + '\n')
    return row


def accept_stages(rows):
    need(collections.Counter(r['id'] for r in rows) == collections.Counter(STAGES), 'missing/duplicate local stage')
    need(all(r['status'] == 'passed' for r in rows), 'required local stage did not pass')
    verify_artifacts([a for row in rows for a in row['artifacts']])


def evidence_tree(directory):
    paths = [p for p in directory.rglob('*') if p.is_file() and 'runner' not in p.relative_to(directory).parts]
    need(len(paths) <= 10000, 'artifact count bound exceeded')
    return [artifact(p) for p in sorted(paths)]


def validate_network(stage, directory, identities):
    manifest = read_json(directory / 'manifest.json')
    evidence_tree(directory)  # Bound all reads before invoking predecessor adapters.
    if stage == 'quic-interop':
        runner = load_runner('run-v3-quic-interop.py')
        matrix = runner.load_matrix('task173')
        clock = manifest.get('simulator_clock_compatibility')
        if clock:
            need(runner.simulator_clock_configuration(clock['receipt_path']) == clock, 'simulator clock compatibility changed')
            need(manifest.get('images', {}).get(clock['image_id']) == clock['image_id'], 'simulator clock image changed')
        need(manifest.get('passed') is True and manifest.get('returncode') == 0
             and manifest.get('cleanup_returncode') == 0 and not manifest.get('cleanup_failure'), 'QUIC run/cleanup failed')
        need(manifest.get('matrix') == matrix and manifest.get('runner_commit') == matrix['runner_commit'], 'wrong QUIC matrix/revision')
        need(runner.validate_results(read_json(directory / 'results.json'), matrix), 'incomplete QUIC matrix')
        need(runner.validate_artifacts(directory / 'runner-logs', matrix) == manifest.get('capture_records'), 'QUIC captures changed')
        need(runner.validate_closure_artifacts(directory / 'runner-logs', matrix) == manifest.get('closure_witnesses'), 'missing QUIC closure witness')
        observation = manifest.get('runner_observation', {})
        copied = directory / 'runner'
        need(observation.get('module_sha256') == digest(ROOT / 'scripts/run-v3-quic-interop.py')
             == digest(copied / 'task196_observation.py'), 'QUIC observation module changed')
        original = copied / 'testcases_quic.original.py'
        need(observation.get('original_testcases_sha256') == digest(original)
             and observation.get('patched_testcases_sha256') == digest(copied / 'testcases_quic.py')
             and runner.runner_observation_patch(original.read_text()) == (copied / 'testcases_quic.py').read_text(), 'QUIC observation amendment changed')
        for name, record in manifest.get('artifacts', {}).items():
            need(artifact(directory / name)['sha256'] == record['sha256'], 'QUIC artifact hash mismatch')
        need(manifest.get('images', {}).get(identities['server_image']) == identities['server_image_id'], 'endpoint image changed')
        need(identities['endpoint_sha256'] in manifest.get('provenance', {}).get('tools', {}).get('endpoint_sha256', ''), 'endpoint binary changed')
        sources = manifest.get('provenance', {}).get('source_sha256', {})
    else:
        need(manifest.get('status') == 'passed', 'network manifest failed')
        expected = 'task174' if stage == 'http3-interop' else 'websocket'
        need(manifest.get('matrix') == expected, 'wrong HTTP/3 matrix')
        need(manifest.get('fixture_sha256') == identities.get('fixture_sha256') and identities.get('fixture_sha256'), 'fixture identity missing/changed')
        if stage == 'http3-interop':
            runner = load_runner('run-v3-http3-gates.py')
            matrices = manifest.get('matrices', {})
            need(set(matrices) == {'aioquic', 'quic-go'} and manifest.get('pins') == runner.PINS, 'missing independent HTTP/3 client/pins')
            need(manifest.get('go_client_sha256') == identities['go_client_sha256'], 'Go binary changed')
            need(manifest.get('parity_fixture_sha256') == identities['parity_sha256'], 'parity binary changed')
            expected_fields = runner.validate_parity(manifest.get('parity'))
            for client, receipt in matrices.items():
                runner.validate_receipt(receipt, client, 'task174')
                need(read_json(directory / client / 'receipt.json') == receipt, 'receipt changed')
                for case in receipt['cases']:
                    if case['case'] in ('streaming', 'slow_producer'):
                        need(runner.application_fields(case.get('headers')) == expected_fields, 'H1/H2/H3 parity mismatch')
                runner.validate_artifacts(directory / client, receipt, client)
        else:
            runner = load_runner('run-v3-http3-websocket-gates.py')
            need(manifest.get('tool', {}).get('version') == '1.3.0', 'wrong WebSocket client pin')
            receipt = manifest.get('receipt')
            runner.validate_receipt(receipt)
            need(read_json(directory / 'aioquic/receipt.json') == receipt, 'WebSocket receipt changed')
            runner.validate_artifacts(directory / 'aioquic', receipt)
        for client in ('aioquic', 'quic-go') if stage == 'http3-interop' else ('aioquic',):
            exits = read_json(directory / client / 'exits.json')
            need(exits.get('client') == exits.get('fixture') == 0 and not exits.get('cleanup_errors')
                 and not exits.get('fixture_reader_errors') and not exits.get('client_reader_errors'), 'network process/diagnostic cleanup failed')
        sources = manifest.get('source_files_sha256', {})
    need(sources and all((ROOT / name).is_file() and digest(ROOT / name) == sha for name, sha in sources.items()), 'network source identity changed')
    return {'matrix': manifest['matrix'], 'artifacts': evidence_tree(directory)}



def corpus_names(protocol):
    lines = (ROOT / 'test/conformance' / protocol / 'cases.tsv').read_text().splitlines()
    return [line.split('\t')[0] for line in lines if line and not line.startswith('#')]


def suite_summary(suite, output):
    if suite not in ('websocket_conformance', 'protocol_fuzz_replay'):
        return native_summary(output)
    expected = corpus_names('websocket')
    if suite == 'protocol_fuzz_replay':
        expected = corpus_names('http1') + expected
        observed = re.findall(r'^REPLAY (\S+)$', output, re.M)
    else:
        observed = re.findall(r'^PASS (\S+) RFC6455 [0-9./]+$', output, re.M)
    need(expected and collections.Counter(observed) == collections.Counter(expected), 'missing/duplicate corpus transcript')
    need(len(output.splitlines()) == len(expected), 'unexpected corpus output')
    return {'tests': len(expected), 'successes': len(expected), 'failures': 0, 'skipped': 0}


def run_native_suites(build, suites, directory):
    directory.mkdir(mode=0o700)
    results = []
    for suite in suites:
        executable = build / 'test' / suite
        need(executable.is_file(), 'required native suite missing: ' + suite)
        binary = artifact(executable)
        row = run_process([executable], build / 'test', directory / suite, 120)
        row['binary'] = binary
        verify_artifacts([binary])
        results.append({'suite': suite, **row})
        (directory / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        need(row['status'] == 'passed', suite + ': ' + row.get('failure', 'failed'))
        row['counts'] = suite_summary(suite, Path(row['stdout']).read_text())
        results[-1]['counts'] = row['counts']
    (directory / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return {'suites': results, 'artifacts': evidence_tree(directory)}


def validate_fuzz(directory, targets, header_only=()):
    evidence_tree(directory)
    capability = (directory / 'capability.log').read_text()
    probe_runs = re.findall(r'Done (\d+) runs in \d+ second\(s\)', capability)
    need(len(probe_runs) == 1 and int(probe_runs[0]) > 0
         and not re.search(r'ERROR:|runtime error:|SUMMARY:.*Sanitizer', capability), 'failed libFuzzer capability probe')
    need((directory / 'environment.log').stat().st_size > 0, 'missing compiler identity')
    rows = []
    for target in targets:
        need(target in FUZZ_OFF + FUZZ_ON, 'unknown required fuzz target')
        path = directory / target
        need(all((path / name).is_file() for name in ('compile.sh', 'reproduce.sh', 'run.log', 'build.log', 'fuzz')), 'incomplete fuzz evidence: ' + target)
        recipe = shlex.split((path / 'compile.sh').read_text())
        need(all(flag in recipe for flag in ('-std=c++20', '-fsanitize=fuzzer,address,undefined', '-fno-sanitize-recover=undefined'))
             and (any(str(ROOT / 'src/detail') + '/' in argument and argument.endswith('.cpp') for argument in recipe)
                  or (target in header_only and target in ('http1_body', 'hpack')
                      and str(ROOT / 'test/fuzz' / (target + '_fuzz.cpp')) in recipe))
             and not any(argument.endswith('.a') for argument in recipe), 'fuzz sources are not instrumented')
        replay = shlex.split((path / 'reproduce.sh').read_text())
        need(all(flag in replay for flag in ('-runs=2000', '-max_total_time=30', '-timeout=5', '-rss_limit_mb=512', '-seed=128')), 'wrong fuzz bounds')
        output = (path / 'run.log').read_text()
        completed = re.findall(r'Done (\d+) runs in (\d+) second\(s\)', output)
        need(len(completed) == 1 and int(completed[0][0]) > 0
             and not re.search(r'ERROR:|runtime error:|SUMMARY:.*Sanitizer', output), 'failed/empty fuzz exploration')
        need(any((path / 'seeds').iterdir()), 'empty fuzz corpus')
        if target == 'quic_tls_callbacks':
            provider = (path / 'provider.log').read_text()
            need('-lssl' in provider and '-lcrypto' in provider and '-I' in provider, 'missing provider linkage')
        rows.append({'target': target, 'completed_runs': int(completed[0][0]), 'seconds': int(completed[0][1])})
    return {'exploration': rows, 'artifacts': evidence_tree(directory)}


def save_manifest(directory, manifest):
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def execute_stage(manifest, directory, name, execute):
    row = {'id': name, 'status': 'failed', 'artifacts': []}
    manifest['stages'].append(row)
    start = time.monotonic()
    try:
        row.update(execute())
        row['status'] = 'passed'
    except Exception as error:
        row['failure'] = str(error)
        raise
    finally:
        row['duration_seconds'] = round(time.monotonic() - start, 3)
        save_manifest(directory, manifest)


def checked(command, cwd, output, timeout=1800):
    row = run_process(command, cwd, output, timeout)
    need(row['status'] == 'passed', row.get('failure', 'process failed') + '; see ' + str(output))
    return row


def source_identity():
    return release_identity.source_identity(ROOT)


def build_configuration(build, tls, sanitized):
    need((build / 'config.h').is_file() and (build / 'test/Makefile').is_file(), 'configured build missing: ' + str(build))
    need(('#define NATIVE_V3_TLS' in (build / 'config.h').read_text()) == tls, 'wrong native TLS configuration')
    makefile = (build / 'src/Makefile').read_text()
    srcdir = re.search(r'^abs_top_srcdir = (.+)$', makefile, re.M)
    need(srcdir and Path(srcdir[1]).resolve() == ROOT, 'build configured for another source tree')
    values = {}
    for name in ('CXX', 'AM_CXXFLAGS', 'CXXFLAGS'):
        match = re.search(r'^' + name + r' = (.*)$', makefile, re.M)
        values[name] = match[1] if match else ''
    need('-std=c++20' in ' '.join(values.values()).split(), 'missing C++20 configuration')
    if sanitized:
        need('-fsanitize=address,undefined' in values['CXXFLAGS'] and '-fno-sanitize-recover=undefined' in values['CXXFLAGS'], 'uninstrumented sanitizer configuration')
    return {'path': str(build), 'tls': tls, 'sanitized': sanitized,
            'cxx': values['CXX'], 'am_cxxflags': values['AM_CXXFLAGS'], 'cxxflags': values['CXXFLAGS'],
            'configuration': [artifact(build / p) for p in ('config.h', 'config.log', 'src/Makefile', 'test/Makefile')]}


def native_stage(build, suites, directory, fixtures=False):
    directory.mkdir(mode=0o700)
    commands = [checked(['make', '-j4', '-C', build / 'src'], ROOT, directory / 'build')]
    targets = suites + (['http3_udp_fixture', 'http3_semantic_parity_fixture', 'quic_interop_endpoint'] if fixtures else [])
    selection = [name + '=' + ' '.join(targets) for name in ('TESTS', 'check_PROGRAMS', 'noinst_PROGRAMS')]
    commands.append(checked(['make', '-j4', '-C', build / 'test', *selection, *targets], ROOT, directory / 'test-build'))
    results = run_native_suites(build, suites, directory / 'tests')
    results['commands'] = commands
    tls = '#define NATIVE_V3_TLS' in (build / 'config.h').read_text()
    results['configuration'] = build_configuration(build, tls, 'sanitized' in directory.name)
    results['artifacts'] = evidence_tree(directory)
    return results


def network_stage(stage, args, directory, identities):
    directory.mkdir(mode=0o700)
    evidence = directory / 'evidence'
    if stage == 'quic-interop':
        command = [args.runner_python, ROOT / 'scripts/run-v3-quic-interop.py', '--matrix', 'task173',
                   '--runner-dir', args.runner_dir, '--server-image', args.server_image, '--output', evidence]
        if args.docker_direct_routing_compat:
            command.append('--docker-direct-routing-compat')
        if getattr(args, 'simulator_clock_receipt', None):
            command += ['--simulator-clock-receipt', args.simulator_clock_receipt]
    else:
        filename = 'run-v3-http3-gates.py' if stage == 'http3-interop' else 'run-v3-http3-websocket-gates.py'
        command = [sys.executable, ROOT / 'scripts' / filename, '--build-dir', args.tls_on_build,
                   '--log-dir', evidence, '--python', args.python]
        if stage == 'http3-interop':
            command += ['--matrix', 'task174', '--go-client', args.go_client, '--tshark', args.tshark]
    row = checked(command, ROOT, directory / 'process', 1500 if stage == 'quic-interop' else 300)
    results = validate_network(stage, evidence, identities)
    results['command'] = row
    results['artifacts'] = evidence_tree(directory)
    return results


def fuzz_stage(args, build, targets, directory):
    directory.mkdir(mode=0o700)
    output = directory / 'evidence'
    command = [ROOT / 'scripts/run-v3-protocol-fuzz.sh', '--compiler', args.compiler,
               '--build-dir', output, '--native-config-dir', build, '--targets', ' '.join(targets), '--runs', '2000', '--seconds', '30']
    row = checked(command, ROOT, directory / 'process', 3600)
    results = validate_fuzz(output, targets)
    results.update(command=row, artifacts=evidence_tree(directory))
    return results


def resolve_tool(value):
    need(value, 'required tool path missing')
    path = shutil.which(str(value))
    need(path, 'required tool unavailable: ' + str(value))
    return str(Path(path).absolute())


def tool_receipt(command):
    result = subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True, text=True, timeout=30, check=True)
    need(len(result.stdout) + len(result.stderr) <= 1024 * 1024, 'tool identity output bound')
    return {'command': list(map(str, command)), 'stdout': result.stdout, 'stderr': result.stderr}



def capture_tools(tshark):
    actual = resolve_tool('tshark')
    need(Path(actual).resolve() == Path(tshark).resolve(), 'declared tshark differs from the QUIC runner PATH decoder')
    editcap = resolve_tool('editcap')
    return {'tshark': tool_receipt([actual, '--version']),
            'editcap': tool_receipt([editcap, '--version']), 'helper_binary': artifact(editcap)}


def preflight(args):
    for name in ('tls_off_build', 'tls_on_build', 'sanitized_off_build', 'sanitized_on_build',
                 'go_client', 'runner_dir', 'provider_prefix'):
        need(getattr(args, name), 'required argument missing: --' + name.replace('_', '-'))
        setattr(args, name, Path(getattr(args, name)).resolve())
    builds = [(args.tls_off_build, False, False), (args.tls_on_build, True, False),
              (args.sanitized_off_build, False, True), (args.sanitized_on_build, True, True)]
    need(len({b for b, _, _ in builds}) == 4, 'build directories must be distinct')
    need(not any(args.output == b or b in args.output.parents or args.output in b.parents for b, _, _ in builds), 'evidence and build directories overlap')
    configurations = [build_configuration(*build) for build in builds]
    for name in ('compiler', 'python', 'go', 'tshark', 'runner_python'):
        setattr(args, name, resolve_tool(getattr(args, name)))
    need(args.server_image, 'required argument missing: --server-image')
    aio = tool_receipt([args.python, '-c', 'import aioquic,json,sys;print(json.dumps({"version":aioquic.__version__,"executable":sys.executable,"python":sys.version}))'])
    need(json.loads(aio['stdout'])['version'] == '1.3.0', 'wrong aioquic version')
    captures = capture_tools(args.tshark)
    quic = load_runner('run-v3-quic-interop.py')
    quic.preflight(args.runner_dir, args.output / 'quic-interop/evidence')
    provider = args.provider_prefix
    need(all(str(provider) in (b / 'src/Makefile').read_text() for b in (args.tls_on_build, args.sanitized_on_build)), 'configured provider prefix mismatch')
    provider_files = [provider / 'lib/libssl.a', provider / 'lib/libcrypto.a', provider / 'include/openssl/opensslv.h']
    return {'builds': configurations, 'aioquic': aio, 'capture_tools': captures,
            'compiler': tool_receipt([args.compiler, '--version']), 'go': tool_receipt([args.go, 'version']),
            'tshark': tool_receipt([args.tshark, '--version']), 'runner_python': tool_receipt([args.runner_python, '--version']),
            'provider': [artifact(p) for p in provider_files],
            'provider_version': tool_receipt([provider / 'bin/openssl', 'version']),
            'tool_binaries': [artifact(path) for path in sorted({str(Path(getattr(args, name)).resolve()) for name in ('compiler', 'python', 'go', 'tshark', 'runner_python')})],
            'sanitizers': {'ASAN_OPTIONS': os.environ.get('ASAN_OPTIONS', ''), 'UBSAN_OPTIONS': os.environ.get('UBSAN_OPTIONS', ''),
                           'provider_instrumented': False, 'independent_clients_instrumented': False,
                           'leak_detection': 'disabled' if 'detect_leaks=0' in os.environ.get('ASAN_OPTIONS', '') else 'requested'}}


def build_endpoint_image(args, directory):
    context = directory / 'endpoint-context'
    context.mkdir(mode=0o700)
    endpoint = args.tls_on_build / 'test/quic_interop_endpoint'
    shutil.copyfile(endpoint, context / 'quic_interop_endpoint')
    (context / 'quic_interop_endpoint').chmod(0o700)
    shutil.copyfile(ROOT / 'test/interop/quic/run_endpoint.sh', context / 'run_endpoint.sh')
    (context / 'run_endpoint.sh').chmod(0o700)
    (context / 'Dockerfile').write_text('FROM martenseemann/quic-network-simulator-endpoint@sha256:3b2b9e6fa317da238c8140a7b6d2bf8d4d45a8464b873eecac3ff2a32520b71a\nCOPY quic_interop_endpoint /quic_interop_endpoint\nCOPY run_endpoint.sh /run_endpoint.sh\nENV LIBHTTPSERVER_INTEROP_TEST_MODE=1\nENTRYPOINT ["/run_endpoint.sh"]\n')
    row = checked(['docker', 'build', '--pull=false', '--network=none', '-t', args.server_image, context], ROOT, directory / 'image-build', 300)
    image_id = json.loads(tool_receipt(['docker', 'image', 'inspect', args.server_image])['stdout'])[0]['Id']
    endpoint_hash = digest(endpoint)
    observed = tool_receipt(['docker', 'run', '--rm', '--network', 'none', '--entrypoint', 'sha256sum', image_id, '/quic_interop_endpoint'])
    need(observed['stdout'].split()[0] == endpoint_hash, 'packaged endpoint differs from current build')
    return {'server_image': args.server_image, 'server_image_id': image_id,
            'endpoint_sha256': endpoint_hash, 'image_build': row}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    for name in ('tls-off-build', 'tls-on-build', 'sanitized-off-build', 'sanitized-on-build',
                 'compiler', 'python', 'go', 'go-client', 'tshark', 'runner-python', 'runner-dir', 'provider-prefix', 'server-image'):
        parser.add_argument('--' + name)
    parser.add_argument('--docker-direct-routing-compat', action='store_true')
    parser.add_argument('--simulator-clock-receipt')
    args = parser.parse_args(argv)
    args.output = args.output.resolve()
    manifest = {'schema_version': 1, 'task': 'TASK-178', 'status': 'failed', 'stages': [],
                'nonlocal_checks': NONLOCAL, 'command': [sys.executable, str(Path(__file__).resolve()), *(argv if argv is not None else sys.argv[1:])]}
    owned = False
    previous_umask = os.umask(0o077)
    try:
        create_output(args.output)
        owned = True
        inventory = read_json(INVENTORY)
        validate_inventory(inventory)
        manifest.update(inventory=inventory, inventory_identity=artifact(INVENTORY))
        manifest['tools'] = preflight(args)
        manifest['source'] = source_identity()
        save_manifest(args.output, manifest)
        for stage, build, suites in [
                ('native-off', args.tls_off_build, OFF_SUITES),
                ('native-on', args.tls_on_build, OFF_SUITES + ON_SUITES),
                ('sanitized-off', args.sanitized_off_build, OFF_SUITES),
                ('sanitized-on', args.sanitized_on_build, OFF_SUITES + ON_SUITES)]:
            execute_stage(manifest, args.output, stage,
                          lambda stage=stage, build=build, suites=suites: native_stage(build, suites, args.output / stage, stage == 'native-on'))
        manifest['tools']['go_build'] = checked(['env', 'GOPROXY=off', 'GOTOOLCHAIN=local', args.go, 'build',
                                                '-C', ROOT / 'test/integ/http3-client-go', '-o', args.go_client, '.'], ROOT, args.output / 'go-build', 300)
        go_identity = tool_receipt([args.go_client, '--identity'])
        need(json.loads(go_identity['stdout']).get('version') == 'v0.55.0', 'wrong quic-go version')
        manifest['tools']['quic_go'] = go_identity
        identities = {'fixture_sha256': digest(args.tls_on_build / 'test/http3_udp_fixture'),
                      'parity_sha256': digest(args.tls_on_build / 'test/http3_semantic_parity_fixture'),
                      'go_client_sha256': digest(args.go_client)}
        identities.update(build_endpoint_image(args, args.output))
        manifest['identities'] = identities
        for stage in ('quic-interop', 'http3-interop', 'websocket-interop'):
            execute_stage(manifest, args.output, stage,
                          lambda stage=stage: network_stage(stage, args, args.output / stage, identities))
        for stage, build, targets in [('fuzz-off', args.tls_off_build, FUZZ_OFF), ('fuzz-on', args.tls_on_build, FUZZ_ON)]:
            execute_stage(manifest, args.output, stage,
                          lambda stage=stage, build=build, targets=targets: fuzz_stage(args, build, targets, args.output / stage))
        need(manifest['source'] == source_identity(), 'source changed during gate')
        need(identities['fixture_sha256'] == digest(args.tls_on_build / 'test/http3_udp_fixture')
             and identities['go_client_sha256'] == digest(args.go_client)
             and identities['endpoint_sha256'] == digest(args.tls_on_build / 'test/quic_interop_endpoint'), 'input binaries changed during gate')
        verify_artifacts(manifest['tools']['provider'])
        verify_artifacts(manifest['tools']['tool_binaries'])
        verify_artifacts([manifest['tools']['capture_tools']['helper_binary']])
        accept_stages(manifest['stages'])
        manifest['status'] = 'passed'
        print('PASS: full local HTTP/3 gate; nonlocal checks deferred to CI / v3 PR')
        return 0
    except (RuntimeError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        manifest['failure'] = str(error)
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
    finally:
        if owned:
            save_manifest(args.output, manifest)
        os.umask(previous_umask)


if __name__ == '__main__':
    raise SystemExit(main())
