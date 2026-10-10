"""Explicit installed-package coverage and provenance checks."""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def parse_inventory(text):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate inventory key: ' + key)
            result[key] = value
        return result
    return json.loads(text, object_pairs_hook=unique)


def load():
    inventory = parse_inventory((ROOT / 'test/installed/v3-package-matrix.json').read_text())
    validate(inventory)
    return inventory


def validate(inventory):
    for key, expected in (('tls', {'no', 'yes'}), ('linkages', {'shared', 'static'}),
                          ('loops', {'managed', 'external'})):
        if len(inventory[key]) != 2 or set(inventory[key]) != expected:
            raise ValueError('incomplete/duplicate matrix axis: ' + key)
    expected = {'Linux': 'epoll', 'FreeBSD': 'kqueue', 'Darwin': 'kqueue', 'Windows': 'iocp'}
    if inventory['platforms'] != expected:
        raise ValueError('incomplete platform/backend inventory')
    if not inventory['cases'] or len(set(inventory['cases'])) != len(inventory['cases']):
        raise ValueError('missing/duplicate configuration cases')
    required = {'native_http1_e2e', 'external_http1_e2e', 'external_readiness_adapter',
                'readiness_contract', 'io_backend_contract'}
    tls = {'tls_io_loopback', 'native_http1_tls_parity', 'tls_selection', 'tls_mtls',
           'tls_credentials_rotation', 'tls_psk_runtime'}
    if not required <= set(inventory['common_suites']) or not tls <= set(inventory['tls_suites']):
        raise ValueError('required suite absent')


def cells(inventory, platform):
    if platform not in inventory['platforms']:
        raise ValueError('unsupported native package host: ' + platform)
    return [dict(tls=tls, linkage=linkage, loop=loop, cases=inventory['cases'])
            for tls in inventory['tls'] for linkage in inventory['linkages'] for loop in inventory['loops']]


def suites(inventory, platform, tls):
    cells(inventory, platform)
    names = inventory['common_suites'] + ['io_' + inventory['platforms'][platform] + '_backend']
    if platform == 'Windows':
        names += ['io_completion_storage']
    if tls == 'yes':
        names += inventory['tls_suites']
    return {name: inventory['suites'][name] for name in names}


def suite_result(output, status):
    tests = re.findall(r'^(\d+) tests executed$', output, re.M)
    checks = re.findall(r'^(\d+) checks$', output, re.M)
    failures = re.findall(r'^-> (\d+) failures$', output, re.M)
    skipped = re.findall(r'^-> (\d+) skipped$', output, re.M)
    if status or not tests or not checks or len(tests) != len(failures) or len(tests) != len(skipped):
        raise ValueError('failed or incomplete suite receipt')
    if any(int(n) == 0 for n in tests + checks) or any(int(n) for n in failures + skipped):
        raise ValueError('empty, failed or skipped required suite')
    names = re.findall(r'^Running test \(\d+\): (\w+)$', output, re.M)
    if len(names) != sum(map(int, tests)):
        raise ValueError('missing suite case witnesses')
    return {'tests': sum(map(int, tests)), 'checks': sum(map(int, checks)), 'failures': 0, 'skipped': 0, 'case_names': names}


def check_linkage(graph, linkage, prefix):
    libraries = [Path(node['id']) for node in graph['nodes']
                 if re.match(r'libhttpserver(?:[.-])', Path(node['id']).name.lower())]
    if linkage == 'shared' and (not libraries or any(not p.is_relative_to(prefix.resolve()) for p in libraries)):
        raise ValueError('shared integration did not resolve installed library')
    if linkage == 'static' and libraries:
        raise ValueError('static integration loaded a shared/import library')


def check_dependencies(dependencies, prefix, source, build, private=False):
    for dependency in dependencies:
        path = Path(dependency).resolve()
        if path.is_relative_to(build.resolve()):
            raise ValueError('build header contamination: ' + dependency)
        if path.is_relative_to(source.resolve()):
            allowed = private and (path.is_relative_to(source / 'test') or path.is_relative_to(source / 'src/httpserver/detail')
                                   or path == source / 'examples/v3_host_poll.hpp')
            if not allowed:
                raise ValueError('source public header contamination: ' + dependency)
        if 'httpserver' in path.parts and not path.is_relative_to(prefix.resolve()) and not path.is_relative_to(source.resolve()):
            raise ValueError('unexpected library header: ' + dependency)


def check_tls_witnesses(output, backend):
    for witness in ('PACKAGE_BACKEND ' + backend, 'PACKAGE_TLS managed', 'PACKAGE_TLS external'):
        if witness not in output:
            raise ValueError('missing encrypted backend witness: ' + witness)


def configuration_results(output, inventory, loop="managed"):
    rows = re.findall(r'^PACKAGE_CASE (\w+) (\w+) (bound|unbound)$', output, re.M)
    results = {}
    for name, outcome, binding in rows:
        expected = ('ok', 'bound') if name == 'plaintext' else ('not_supported', 'unbound') if name in (
            'tls_listener', 'http2', 'http3', 'mixed_rejection') else ('invalid_argument', 'unbound')
        if name == 'provider_profile_mismatch' and loop == 'external':
            expected = ('not_supported', 'unbound')
        if name in results or (outcome, binding) != expected:
            raise ValueError('configuration outcome diverged: ' + name)
        results[name] = {'outcome': outcome, 'binding': binding}
    if set(results) != set(inventory['cases']):
        raise ValueError('configuration case inventory incomplete')
    return results


def parity_observation_counts(output):
    seen = {driver: set() for driver in ('managed', 'external', 'poll-oracle')}
    for prefix, payload in re.findall(r'^(TASK179_OBSERVATION|PACKAGE_OBSERVATION) (.+)$', output, re.M):
        row = json.loads(payload)
        driver = 'managed' if prefix == 'TASK179_OBSERVATION' else row['driver']
        identity = row['id']
        if driver not in seen or identity in seen[driver]:
            raise ValueError('duplicate/unknown backend observation')
        # The primary release corpus additionally contains WebSocket observations.
        if row['mode'].endswith('-tls') and not row['case'].startswith('websocket.tseq:'):
            seen[driver].add(identity)
    if not seen['managed'] or seen['managed'] != seen['external'] or seen['managed'] != seen['poll-oracle']:
        raise ValueError('encrypted transcript cases diverged across drivers')
    return {driver: len(cases) for driver, cases in seen.items()}
