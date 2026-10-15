"""Receipt adapters for the fixed TASK-185 packaged release inventory."""
import collections
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parents[2]


def need(condition, message):
    if not condition:
        raise ValueError(message)


def load_runner(name, root=ROOT):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_').replace('/', '_'), root / 'scripts' / name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def automake_counts(text):
    result = {}
    for field in ('TOTAL', 'PASS', 'SKIP', 'XFAIL', 'FAIL', 'XPASS', 'ERROR'):
        values = re.findall(r'^# ' + field + r':[ \t]+(\d+)$', text, re.M)
        need(len(values) == 1, 'missing/duplicate Automake count: ' + field)
        result[field] = int(values[0])
    need(result['TOTAL'] > 0 and result['TOTAL'] == result['PASS'] and
         all(result[k] == 0 for k in ('SKIP', 'XFAIL', 'FAIL', 'XPASS', 'ERROR')), 'empty/failed/skipped Automake receipt')
    return {'tests': result['TOTAL'], 'checks': result['TOTAL'], 'successes': result['PASS'], 'failures': 0, 'skipped': 0}


def automake_result(build, host):
    skipped = {p.stem for p in (build / 'test').glob('*.trs') if ':test-result: SKIP' in p.read_text()}
    allowed = {'Linux': {'io_iocp_backend', 'io_kqueue_backend'},
               'FreeBSD': {'io_iocp_backend', 'io_epoll_backend'},
               'Darwin': {'io_iocp_backend', 'io_epoll_backend'},
               'Windows': {'io_epoll_backend', 'io_kqueue_backend'}}
    need(skipped <= allowed.get(host, set()), 'unexpected skipped release suite')
    text = (build / 'test/test-suite.log').read_text()
    matches = re.findall(r'^# SKIP:[ \t]+(\d+)$', text, re.M)
    need(matches == [str(len(skipped))], 'skip counts differ from named platform evidence')
    total = re.findall(r'^# TOTAL:[ \t]+(\d+)$', text, re.M)
    need(len(total) == 1, 'missing Automake total')
    text = re.sub(r'^# TOTAL:[ \t]+\d+$', '# TOTAL: ' + str(int(total[0]) - len(skipped)), text, flags=re.M)
    text = re.sub(r'^# SKIP:[ \t]+\d+$', '# SKIP: 0', text, flags=re.M)
    result = automake_counts(text)
    result['nonlocal_suites'] = sorted(skipped)
    return result


def counts(tests, checks=None):
    checks = tests if checks is None else checks
    need(tests > 0 and checks > 0, 'empty scenario evidence')
    return dict(tests=tests, checks=checks, successes=checks, failures=0, skipped=0)


def verify_library_files(build, prefix):
    import release_artifact as identity
    records = []
    for name in ('libhttpserver.so', 'libhttpserver.a', 'libhttpserver.dylib'):
        built, installed = build / 'src/.libs' / name, prefix / 'lib' / name
        if built.exists():
            need(installed.is_file() and identity.sha256(built) == identity.sha256(installed),
                 'build-tree library differs from staged package: ' + name)
            records.extend((built.resolve(), installed.resolve()))
    need(records, 'no packaged library equivalence proof')
    return records


def extract_archive(archive, directory):
    with tarfile.open(archive) as stream:
        members = stream.getmembers()
        names = [Path(m.name) for m in members]
        need(names and len(members) < 20000 and sum(m.size for m in members) < 512 * 1024 * 1024,
             'archive exceeds release extraction bounds')
        need(all(not p.is_absolute() and '..' not in p.parts for p in names), 'archive path traversal')
        roots = {p.parts[0] for p in names if p.parts}
        need(len(roots) == 1, 'archive requires one source root')
        need(not directory.exists(), 'extracted directory already exists')
        directory.mkdir(mode=0o700)
        stream.extractall(directory, filter='data')
    return directory / roots.pop()


def memory_counts(output):
    names = {'HTTP upload', 'HTTP output', 'WebSocket input', 'WebSocket output'}
    cpu = re.findall(r'^CPU (.+) sample=([012]) cpu_seconds=(\S+) wall_seconds=(\S+)$', output, re.M)
    plateaus = re.findall(r'^PLATEAU (.+) retained=(\d+)/(\d+) RSS=(\d+)/(\d+) slack=(\d+)$', output, re.M)
    need(collections.Counter((name, sample) for name, sample, _, _ in cpu) ==
         collections.Counter((name, str(sample)) for name in names for sample in range(3)), 'incomplete/duplicate idle CPU windows')
    need(collections.Counter(row[0] for row in plateaus) == collections.Counter(names), 'incomplete/duplicate allocator plateaus')
    for _, _, used, wall in cpu:
        used, wall = float(used), float(wall)
        need(math.isfinite(used) and math.isfinite(wall) and used >= 0 and wall >= .99
             and used / wall <= .2, 'idle process CPU threshold failed')
    for _, first, second, before, after, slack in plateaus:
        need(int(slack) == 1048576 and int(after) <= int(before) + int(slack), 'normal allocator RSS threshold failed')
        need(max(int(first), int(second)) <= 17408 + 1024 + 2048 + 512 + 522, 'native queue budget bound failed')
    return counts(len(cpu) + len(plateaus))


def native_counts(root, name, output, tsan=False):
    if name == 'native_protocol_memory':
        return memory_counts(output)
    full = load_runner('run-v3-http3-full-gates.py', root)
    if name in ('websocket_conformance', 'protocol_fuzz_replay'):
        return full.suite_summary(name, output)
    if name in ('http1_conformance', 'native_http1_conformance'):
        cases = full.corpus_names('http1')
        pattern = r'^PASS native (\S+)$' if name.startswith('native_') else r'^PASS (\S+) RFC [0-9./]+$'
        observed = re.findall(pattern, output, re.M)
        need(collections.Counter(observed) == collections.Counter(cases)
             and len(output.splitlines()) == len(cases), 'incomplete HTTP/1 corpus')
        return counts(len(cases))
    allocation_skips = {
        'websocket_drain_race': ('close_allocation_failure_settles_once_and_wakes_parked_receive',
            'sanitizer owns operator new; fault injection is covered in normal and ASan/UBSan lanes'),
        'task_resume_signal_lifetime': ('published_waiter_can_die_during_timeout_registration',
            'sanitizer owns operator new; timer-allocation reclaim hook runs in normal and ASan/UBSan lanes')}
    if tsan and name in allocation_skips:
        case, reason = allocation_skips[name]
        need(case in output and reason in output
             and output.count('[SKIP]') == 1 and re.search(r'^-> 1 skipped$', output, re.M), 'missing named allocation skip')
        output = re.sub(r'^-> 1 skipped$', '-> 0 skipped', output, flags=re.M)
    try:
        return full.native_summary(output)
    except RuntimeError as error:
        raise ValueError(str(error)) from error


def rotation_counts(receipt, source, build, prefix):
    need(receipt.get('status') == 'passed' and receipt.get('tls') == 'yes', 'installed rotation execution failed')
    for key, expected in (('source', source), ('build', build), ('prefix', prefix)):
        need(receipt.get(key) == str(expected), 'rotation belongs to another ' + key)
    cells = receipt.get('cells', [])
    need(collections.Counter((cell['linkage'], cell['loop']) for cell in cells) ==
         collections.Counter((linkage, loop) for linkage in ('shared', 'static') for loop in ('managed', 'external')),
         'incomplete/duplicate installed rotation matrix')
    adapter = load_runner('lib/release_package_adapters.py')
    for cell in cells:
        rows = [row for row in cell['scenarios'] if row.get('case') == 'installed_rotation_example']
        need(len(rows) == 1, 'missing/duplicate installed rotation witness')
        adapter.validate_tls_rows(rows)
    return counts(4, 20)


def validate_package(root, directory, read_json):
    summary = read_json(directory / 'summary.json')
    need(summary.get('status') == 'passed' and set(summary.get('lanes', {})) == {'no', 'yes'}, 'package matrix incomplete')
    matrix = load_runner('lib/package_matrix.py', root)
    inventory = matrix.load()
    total_tests = total_checks = 0
    for mode in ('no', 'yes'):
        lane = directory / ('tls-' + mode)
        audit = read_json(lane / 'audit.json')
        need(audit.get('violations') == [] and audit.get('tls_mode') == mode, 'package audit failed/wrong TLS')
        for linkage in ('shared', 'static'):
            public = audit['public'][linkage]
            matrix.check_linkage(public['dependencies'], linkage, lane / 'prefix')
            matrix.check_dependencies(public['compile']['dependencies'], lane / 'prefix', root, lane / 'build')
            cells = public['cells']
            need(len(cells) == 2 and all(len(cell['configurations']) == len(inventory['cases']) for cell in cells), 'package configuration coverage missing')
            suites = read_json(lane / ('integration-' + linkage) / 'suites.json')
            need(set(suites) == set(matrix.suites(inventory, summary['host'], mode)), 'missing installed suite')
            for suite in suites.values():
                need(suite['status'] == 'passed' and suite['tests'] > 0 and suite['checks'] > 0
                     and suite['failures'] == suite['skipped'] == 0, 'failed/empty/skipped installed suite')
                total_tests += suite['tests']; total_checks += suite['checks']
    return counts(total_tests, total_checks)


def validate_h2(root, directory, read_json):
    runner = load_runner('run-v3-http2-gates.py', root)
    receipt = read_json(directory / 'receipt.json')
    clients = receipt['clients']
    need(len(clients) == 2 and {(c['stack'], c['version']) for c in clients} ==
         {('hyper-h2', '4.4.1'), ('node:http2', '24.15.0')}, 'missing pinned HTTP/2 client')
    for client in clients:
        runner.client_receipt(json.dumps(client))
    h2 = receipt['h2spec']
    cases = runner.inventory((directory / 'h2spec-inventory.log').read_text().split('\n', 1)[1])
    excluded = runner.exclusions(root / 'test/conformance/http2/h2spec-exclusions.tsv', cases)
    need(h2['inventory'] == cases and h2['excluded'] == excluded and h2['executed'] and
         len(h2['executed']) == len(set(h2['executed'])) and set(h2['executed']) == set(cases) - set(excluded), 'incomplete h2spec coverage')
    import xml.etree.ElementTree as ET
    for case in h2['executed']:
        tree = ET.parse(directory / (case.replace('/', '-') + '.xml'))
        need(len(tree.findall('.//testcase')) == 1 and not any(tree.findall('.//' + field)
             for field in ('failure', 'error', 'skipped')), 'failed/empty/skipped h2spec receipt')
    return counts(len(h2['executed']) + len(clients) * len(runner.SCENARIOS))


def validate_resources(directory, read_json):
    receipt = read_json(directory / 'receipt.json')
    need(receipt.get('normal_allocator_only') is True and receipt.get('rss_slack') == 1048576 and
         receipt.get('idle_core_fraction') == .2, 'wrong resource allocator/thresholds')
    probe = load_runner('lib/resource_limits_probe.py')
    for protocol, resumed in (('h2', 131072), ('h3', 524288)):
        result = receipt[protocol]
        need(len(result['cycles']) == len(result['uploads']) == 3 and result['resumed_bytes'] == resumed,
             'incomplete resource saturation/cancellation scenarios')
        for sample in result['cycles'] + result['uploads']:
            need(all(math.isfinite(sample['idle'][key]) for key in ('cpu_seconds', 'wall_seconds', 'rss_before', 'rss_after')),
                 'non-finite resource measurement')
            probe.validate_window(sample['idle'])
    return counts(12)


def validate_fuzz(root, directory, targets):
    # Keep the predecessor predicates, extended to the H1/H2/HPACK targets.
    full = load_runner('run-v3-http3-full-gates.py', root)
    extra = ['http1_head', 'http1_body', 'hpack', 'http2_engine']
    full.FUZZ_OFF += extra
    result = full.validate_fuzz(directory, targets, header_only=('http1_body', 'hpack'))
    return counts(sum(row['completed_runs'] for row in result['exploration']))


def verify_snapshot_packaging(checkout, snapshot, archive):
    import release_artifact as identity
    transformations = {}
    for name, sha in identity.source_files(archive).items():
        if name not in snapshot or snapshot[name] == sha:
            continue
        # AC_CONFIG_FILES emits a terminal newline for this existing data fixture.
        original = checkout / name
        if (name == 'test/test_content_large' and snapshot[name] == identity.sha256(original)
                and (archive / name).read_bytes() == original.read_bytes() + b'\n'):
            transformations[name] = {'source_sha256': snapshot[name], 'packaged_sha256': sha,
                                    'reason': 'AC_CONFIG_FILES terminal newline'}
        else:
            raise ValueError('packaged source differs from working snapshot: ' + name)
    return transformations
