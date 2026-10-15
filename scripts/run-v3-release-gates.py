#!/usr/bin/env python3
"""Run the fixed v3 release inventory on fresh packaged sources; never tag."""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
from v3_package_commands import Commands
from v3_release_checks import (automake_counts, extract_archive, load_runner, verify_library_files)

ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ('FreeBSD', 'Darwin', 'Windows')
GATE_RUNNERS = {
    'package': 'scripts/check-v3-installed-consumer.py',
    'cxx20-off': 'make check', 'cxx20-on': 'make check',
    'http1-websocket': 'scripts/run-v3-http1-websocket-gates.sh',
    'http2': 'scripts/run-v3-http2-gates.py',
    'http3-full': 'scripts/run-v3-http3-full-gates.py',
    'tls': 'test/Makefile.am TLS suites',
    'parity': 'scripts/run-v3-behavior-parity.py',
    'resource-plateaus': 'native_protocol_memory resource_limits',
    'resource-slow-peers': 'scripts/lib/resource_limits_probe.py',
    'asan-ubsan-off': 'make check', 'asan-ubsan-on': 'make check',
    'tsan-off': 'test/Makefile.am owner suites', 'tsan-on': 'test/Makefile.am TLS owner suites',
    'fuzz-off': 'scripts/run-v3-protocol-fuzz.sh', 'fuzz-on': 'scripts/run-v3-protocol-fuzz.sh',
    'public-capabilities': 'installed public TLS/H2/H3/credential witnesses',
    'native-throughput-latency': 'native v3 benchmark receipts',
}
LOCAL_GATES = tuple(GATE_RUNNERS)
EXCLUSIONS = [
    {'id': 'tsan-allocation-injection', 'reason': 'TSan owns operator new; only websocket_drain_race fault injection and task_resume_signal_lifetime timer-allocation reclaim interposition are omitted; normal and ASan coverage remain required.'},
    {'id': 'h2spec-rfc9113', 'reason': 'Only the pinned test/conformance/http2/h2spec-exclusions.tsv rows; independent clients remain required.'},
    {'id': 'quic-optional', 'reason': 'Only test/interop/quic/task173-matrix.json exclusions; mandatory authenticated closure and path predicates remain required.'},
]
MAX_JSON = 64 * 1024 * 1024


def need(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def artifact(path):
    path = Path(path).resolve()
    need(path.is_file(), 'missing artifact: ' + str(path))
    return {'path': str(path), 'sha256': digest(path), 'bytes': path.stat().st_size}


def verify_files(records):
    need(isinstance(records, list) and records, 'empty artifact evidence')
    for record in records:
        need(artifact(record['path']) == record, 'changed artifact: ' + record['path'])


def read_json(path):
    path = Path(path)
    need(path.is_file() and path.stat().st_size <= MAX_JSON, 'missing/oversized receipt: ' + str(path))
    try:
        return json.loads(path.read_text())
    except (ValueError, UnicodeError) as error:
        raise ValueError('malformed receipt: ' + str(path)) from error


def inventory_document():
    return {'schema_version': 1, 'gates': [
        {'id': name, 'runner': runner, 'requirements': ['PRD-V3N-REQ-' + number for number in
             ('001', '002', '003', '004', '005', '006', '007', '008', '038')]}
        for name, runner in GATE_RUNNERS.items()], 'platforms': list(PLATFORMS), 'exclusions': EXCLUSIONS}


def validate_inventory(document):
    need(document == inventory_document(), 'missing/duplicated/changed required gate or unknown exclusion')


def verify_receipt(receipt, identity, public=False):
    need(isinstance(receipt, dict) and receipt.get('status') == 'passed', 'missing/failed required receipt')
    need(receipt.get('identity') == identity and identity, 'historical/wrong archive, source, TLS or sanitizer identity')
    counts = receipt.get('counts', {})
    need(all(type(counts.get(k)) is int for k in ('tests', 'checks', 'successes', 'failures', 'skipped')),
         'missing/malformed counts')
    need(counts['tests'] > 0 and counts['checks'] > 0 and counts['successes'] == counts['checks']
         and counts['failures'] == counts['skipped'] == 0, 'empty/failed/skipped required receipts')
    verify_files(receipt.get('artifacts', []))
    if public:
        need(receipt.get('kind') == 'installed public capability' and
             set(receipt.get('witnesses', [])) >= {'tls', 'h2', 'h3', 'credentials'},
             'private fixture cannot prove installed public capabilities')


def create_output(path):
    need(not path.exists(), 'output already exists; preserve prior evidence')
    path.mkdir(parents=True, mode=0o700)


def save(path, document):
    path.write_text(json.dumps(document, indent=2) + '\n')


def command_receipt(command, argv, cwd, env, validator=None, timeout=1800):
    output = command(argv, cwd, env, timeout=timeout, allow_failure=True)
    need(command.records[-1]['exit'] == 0, 'command exited ' + str(command.records[-1]['exit']))
    return validator(output) if validator else {}


def retain_stage(row, directory):
    """Freeze receipt bytes before a later gate reuses configured build trees."""
    retained = directory.parent / 'retained' / directory.name
    create_output(retained)
    records = row['artifacts'] + row.get('receipt', {}).get('artifacts', [])
    originals = {}
    for record in records:
        previous = originals.setdefault(record['path'], record)
        need(previous == record, 'conflicting artifact records: ' + record['path'])
    need(len(originals) < 20000, 'evidence tree exceeds count bound')
    copies = {}
    for original, record in originals.items():
        source = Path(original)
        destination = retained / source.relative_to(source.anchor)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        frozen = artifact(destination)
        need((frozen['sha256'], frozen['bytes']) == (record['sha256'], record['bytes']),
             'artifact changed while retaining evidence: ' + original)
        copies[original] = frozen
    row['retained_paths'] = {original: record['path'] for original, record in copies.items()}
    row['artifacts'] = [copies[record['path']] for record in row['artifacts']]
    if 'receipt' in row:
        row['receipt']['artifacts'] = [copies[record['path']] for record in row['receipt']['artifacts']]
    for command in row['commands']:
        command['execution_log'] = command['log']
        command['log'] = copies[command['log']]['path']


def execute_stage(manifest, directory, name, execute, retain=False):
    create_output(directory)
    command = Commands(directory)
    row = {'id': name, 'status': 'failed', 'owner': 'TASK-185 release gates', 'commands': command.records, 'artifacts': []}
    manifest['stages'].append(row)
    save(directory.parent / 'manifest.json', manifest)
    started = time.monotonic()
    try:
        row.update(execute(command) or {})
        row['status'] = 'passed'
    except Exception as error:
        row['failure'] = str(error)
    finally:
        row['duration_seconds'] = round(time.monotonic() - started, 3)
        for record in command.records:
            path = Path(record['log'])
            if path.is_file():
                record.update(sha256=digest(path), bytes=path.stat().st_size)
        row['artifacts'] += [artifact(p) for p in sorted(directory.rglob('*')) if p.is_file()]
        if retain:
            try:
                retain_stage(row, directory)
            except Exception as error:
                row.update(status='failed', failure='cannot retain stage evidence: ' + str(error))
        save(directory.parent / 'manifest.json', manifest)
    return row


def local_passed(manifest):
    rows = manifest.get('stages', [])
    return (collections.Counter(r['id'] for r in rows) == collections.Counter(LOCAL_GATES)
            and all(r['status'] == 'passed' for r in rows))


def expected_identity(manifest, name):
    release = manifest['release_artifact']
    mode = {'cxx20-off': 'no', 'http1-websocket': 'no', 'resource-plateaus': 'no',
            'asan-ubsan-off': 'no', 'tsan-off': 'no', 'fuzz-off': 'no',
            'cxx20-on': 'yes', 'http2': 'yes', 'tls': 'yes', 'resource-slow-peers': 'yes',
            'asan-ubsan-on': 'yes', 'tsan-on': 'yes', 'fuzz-on': 'yes'}.get(name, 'both')
    sanitizer = ('address,undefined' if name.startswith('asan-') else
                 'thread' if name.startswith('tsan-') else
                 'fuzzer,address,undefined' if name.startswith('fuzz-') else 'none')
    return {'archive_sha256': release['archive']['sha256'], 'snapshot_sha256': release['source']['snapshot_sha256'],
            'extracted_sha256': hashlib.sha256(json.dumps(release['files_sha256'], sort_keys=True).encode()).hexdigest(),
            'tls': mode, 'sanitizer': sanitizer}


def verify_local_receipts(manifest):
    need(manifest.get('schema_version') == 1, 'wrong release receipt schema')
    validate_inventory(manifest.get('inventory'))
    need(local_passed(manifest), 'required local release gate missing or unsuccessful')
    verify_files(manifest.get('artifacts', []))
    for tool in manifest.get('tools', {}).values():
        if 'binary' in tool: verify_files([tool['binary']])
    import v3_release_artifact as release_identity
    previous = os.environ.get('V3_RELEASE_IDENTITY')
    try:
        os.environ['V3_RELEASE_IDENTITY'] = str(manifest['artifact_identity_path'])
        need(release_identity.verified(Path(manifest['release_artifact']['source_root'])) == manifest['release_artifact'],
             'release source/identity changed')
    finally:
        if previous is None: os.environ.pop('V3_RELEASE_IDENTITY', None)
        else: os.environ['V3_RELEASE_IDENTITY'] = previous
    verify_gate_rows(manifest, manifest['stages'])


def verify_gate_rows(manifest, rows):
    for row in rows:
        verify_files(row['artifacts'])
        expected = expected_identity(manifest, row['id'])
        need(row.get('identity') == expected, 'gate does not belong to selected packaged release')
        need(row.get('receipt', {}).get('gate') == row['id'], 'wrong required gate receipt')
        verify_receipt(row.get('receipt'), expected, public=row['id'] == 'public-capabilities')


def check_tag_ready(manifest):
    verify_local_receipts(manifest)
    platforms = manifest.get('platforms', [])
    need(collections.Counter(r['id'] for r in platforms) == collections.Counter(PLATFORMS)
         and all(r['status'] == 'passed' for r in platforms), 'required platform evidence pending or failed; owner CI / v3 PR')
    verify_gate_rows(manifest, platforms)


def import_platform_receipts(manifest, paths):
    observed = set()
    for path in paths:
        receipt = read_json(path)
        name = receipt.get('id')
        need(name in PLATFORMS and name not in observed, 'unknown/duplicated platform receipt')
        observed.add(name)
        need(receipt.get('archive_sha256') == manifest['release_artifact']['archive']['sha256'], 'platform archive hash mismatch')
        expected = expected_identity(manifest, name)
        need(receipt.get('identity') == expected and receipt.get('status') == 'passed', 'wrong/failed platform identity')
        verify_receipt(receipt.get('receipt'), expected)
        verify_files(receipt.get('artifacts', []))
        receipt['artifacts'].append(artifact(path))
        manifest['platforms'] = [receipt if r['id'] == name else r for r in manifest['platforms']]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-tag-ready', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--provider-prefix', type=Path)
    parser.add_argument('--compiler')
    parser.add_argument('--cc')
    parser.add_argument('--make')
    parser.add_argument('--test-timeout-tool', default='timeout')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--asan-options', default='detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1')
    for name in ('python', 'node', 'h2spec', 'go', 'tshark', 'runner-python', 'runner-dir', 'server-image', 'simulator-clock-receipt'):
        parser.add_argument('--' + name)
    parser.add_argument('--prerequisite-prefix', type=Path, action='append', default=[])
    parser.add_argument('--pkg-config-dir', type=Path, action='append', default=[])
    parser.add_argument('--platform-receipt', type=Path, action='append', default=[])
    parser.add_argument('--tool-artifact', type=Path, action='append', default=[])
    parser.add_argument('--docker-direct-routing-compat', action='store_true')
    parser.add_argument('--isolated-public-network', action='store_true')
    args = parser.parse_args(argv)
    if args.check_tag_ready:
        try:
            manifest = read_json(args.check_tag_ready)
            import_platform_receipts(manifest, args.platform_receipt)
            check_tag_ready(manifest)
            print('PASS: full release inventory verified; tag prerequisite satisfied')
            return 0
        except Exception as error:
            print('FAIL: ' + str(error), file=sys.stderr)
            return 1
    if not all((args.output, args.provider_prefix, args.compiler, args.cc, args.make)) or not 1 <= args.jobs <= 32:
        parser.error('execution requires --output, --provider-prefix, --compiler, --cc, --make and jobs 1..32')
    args.output = args.output.resolve(); args.provider_prefix = args.provider_prefix.resolve()
    try:
        create_output(args.output)
    except ValueError as error:
        print('FAIL: ' + str(error), file=sys.stderr); return 1
    manifest = {'schema_version': 1, 'task': 'TASK-185', 'inventory': read_json(ROOT / 'test/release/task185-gates.json'),
                'stages': [], 'platforms': [{'id': p, 'status': 'pending', 'owner': 'CI / v3 PR', 'artifacts': []} for p in PLATFORMS],
                'artifacts': [], 'status': 'failed', 'tag_ready': False,
                'command': [sys.executable, str(Path(__file__).resolve()), *(argv if argv is not None else sys.argv[1:])]}
    save(args.output / 'manifest.json', manifest)
    try:
        validate_inventory(manifest['inventory'])
        from v3_release_execution import ReleaseAttempt
        ReleaseAttempt(args, sys.modules[__name__], manifest).execute()
        manifest['artifact_identity_path'] = str(args.output / 'artifact-identity.json')
        import_platform_receipts(manifest, args.platform_receipt)
        manifest['local_passed'] = local_passed(manifest)
        if manifest['local_passed']:
            try:
                verify_local_receipts(manifest)
            except Exception as error:
                manifest['local_passed'] = False
                manifest['local_verification_failure'] = str(error)
        manifest['status'] = 'passed' if manifest['local_passed'] else 'failed'
        try:
            check_tag_ready(manifest)
            manifest['tag_ready'] = True
        except Exception as error:
            manifest['tag_blocker'] = str(error)
        return 0 if manifest['local_passed'] else 1
    except Exception as error:
        manifest['failure'] = str(error)
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
    finally:
        save(args.output / 'manifest.json', manifest)


if __name__ == '__main__':
    raise SystemExit(main())
