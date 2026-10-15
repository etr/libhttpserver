#!/usr/bin/env python3
"""Validate TASK-179 per-case transport observations against the pinned corpus.

An outcome map is evidence, not permission to erase an expectation. Protocol
translations here are limited to HTTP framing, ICY and WebSocket mechanisms.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import release_artifact as release_identity

MODES = ('h1.0-tcp', 'h1.1-tcp', 'h1.0-tls', 'h1.1-tls', 'h2-tls', 'h3-quic')
KINDS = {'h1.0-tcp': 'tcp', 'h1.1-tcp': 'tcp', 'h1.0-tls': 'tls',
         'h1.1-tls': 'tls', 'h2-tls': 'tls', 'h3-quic': 'quic'}
CHANGES = {'framing', 'icy-status', 'websocket-handshake', 'http1.0-close-delimited'}


def table_rows(path):
    """Read the existing five-column parity inventory, including code pipes."""
    rows = []
    for line in path.read_text().splitlines():
        if not line.startswith('|'):
            continue
        columns = re.split(r'\|(?=(?:[^`]*`[^`]*`)*[^`]*$)', line)[1:-1]
        columns = [column.strip() for column in columns]
        if len(columns) == 5 and (columns[3].startswith('pinned') or columns[3] in ('inventory-only', 'MHD-artifact', 'delta')):
            rows.append(columns)
        elif len(columns) == 2 and columns[0] != 'Remaining documented HTTP/1 contract' \
                and not columns[0].startswith('---'):
            rows.append([columns[0], columns[1], '—', 'inventory-only', columns[1]])
    return rows


def capability_cases(capability):
    return capability.get('cases', [capability['case']] if capability.get('case') else [])


def corpus_cases(root):
    cases = {}
    for path in sorted((root / 'test/parity/transcripts').glob('*.tseq')):
        current = None
        for number, raw in enumerate(path.read_text().splitlines(), 1):
            line = raw.rstrip()
            if line.startswith('case '):
                current = path.name + ':' + line[5:]
                if current in cases:
                    raise ValueError('duplicate corpus case ' + current)
                cases[current] = []
            elif line.startswith('expect '):
                if current is None:
                    raise ValueError('expectation outside case')
                kind, value = line[7:].split(' ', 1)
                cases[current].append((kind, value, number))
    if not cases or any(not expects for expects in cases.values()):
        raise ValueError('empty corpus or case without expectations')
    return cases


def input_hashes(root, inventory):
    paths = sorted((root / 'test/parity/transcripts').glob('*.tseq'))
    paths += [root / inventory['source']]
    paths += sorted((root / 'src').rglob('*.cpp')) + sorted((root / 'src').rglob('*.hpp'))
    for suffix in ('*.cpp', '*.hpp', '*.py'):
        paths += sorted((root / 'test').rglob(suffix))
    for suffix in ('*.pem', '*.txt'):
        paths += sorted((root / 'test').rglob(suffix))
    paths += sorted((root / 'test').glob('test_content*'))
    paths += [path for path in (root / 'Makefile.am', root / 'src/Makefile.am', root / 'test/Makefile.am', root / 'configure.ac') if path.is_file()]
    paths += sorted((root / 'scripts').glob('*.py'))
    hashes = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    hashes['capabilities'] = hashlib.sha256(json.dumps(inventory, sort_keys=True).encode()).hexdigest()
    return hashes


def local_reference(root, reference):
    name, separator, anchor = reference.partition('#')
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file() or not separator or not anchor:
        return False
    text = path.read_text()
    if f'id="{anchor}"' in text or f"id='{anchor}'" in text:
        return True
    for line in text.splitlines():
        if line.startswith('#'):
            slug = re.sub(r'[^\w\- ]', '', line.lstrip('#').strip().lower()).replace(' ', '-')
            if slug == anchor:
                return True
    return False


def bytes_value(value):
    if value.startswith('"') and value.endswith('"'):
        value = value[1:-1]
    # The transcript grammar is deliberately smaller than Python string escapes.
    result = bytearray()
    at = 0
    escapes = {'r': 13, 'n': 10, 't': 9, '\\': 92, '"': 34}
    while at < len(value):
        char = value[at]
        at += 1
        if char != '\\':
            result.extend(char.encode())
        elif at < len(value) and value[at] == 'x':
            result.append(int(value[at + 1:at + 3], 16))
            at += 3
        else:
            result.append(escapes[value[at]])
            at += 1
    return bytes(result)


def check_expectations(root, expects, observation, changes):
    errors = []
    responses = observation['responses']
    index = -1
    multiplexed = observation['mode'] in ('h2-tls', 'h3-quic')
    close_delimited = observation['mode'].startswith('h1.0') and 'http1.0-close-delimited' in changes \
        and observation['case'] in ('file_resp.tseq:pipe_body', 'file_resp.tseq:deferred_body')
    extended = multiplexed and 'websocket-handshake' in changes \
        and observation['case'] == 'websocket.tseq:upgrade_handshake_fixed_key'
    for kind, value, line in expects:
        if kind in ('status', 'status_line'):
            index += 1
        if index < 0 or index >= len(responses):
            errors.append(f'line {line}: missing response')
            continue
        response = responses[index]
        headers = response['headers']
        body = bytes.fromhex(response['body_hex'])
        matched = False
        if kind == 'status':
            matched = response['status'] == int(value)
            if extended and value == '101':
                matched = response['status'] == 200
        elif kind == 'status_line':
            if multiplexed and 'icy-status' in changes and bytes_value(value).startswith(b'ICY '):
                matched = response['status'] == int(bytes_value(value).split()[1])
            else:
                matched = response['status_line'].encode() == bytes_value(value)
        elif kind == 'header':
            if value.startswith('~'):
                matched = not any(name.lower() == value[1:].lower() for name, _ in headers)
            else:
                name, expected = value.split(':', 1)
                candidates = [actual for key, actual in headers if key.lower() == name.lower()]
                pattern = '.*'.join(re.escape(part) for part in expected.lstrip().split('<*>'))
                matched = bool(candidates) and re.fullmatch(pattern, candidates[0], re.DOTALL) is not None
                if multiplexed and name.lower() == 'transfer-encoding' and 'framing' in changes:
                    matched = not candidates and response['framing'] == 'data'
                if close_delimited and name.lower() == 'transfer-encoding':
                    matched = not candidates and response['framing'] == 'close-delimited' and observation.get('closed') is True
                if extended and name.lower() in ('upgrade', 'sec-websocket-accept'):
                    matched = not candidates
        elif kind == 'header_order':
            cursor = iter(name.lower() for name, _ in headers)
            matched = all(any(actual == name.lower() for actual in cursor) for name in value.split())
        elif kind == 'body':
            matched = body == bytes_value(value)
        elif kind == 'body_hex':
            matched = body == bytes.fromhex(value)
        elif kind == 'body_len':
            matched = len(body) == int(value)
        elif kind == 'body_file':
            matched = body == (root / 'test/parity/transcripts' / value).read_bytes()
        elif kind == 'framing':
            matched = response['framing'] == value
            if multiplexed and 'framing' in changes:
                matched = response['framing'] == 'data' and response['ended']
            if close_delimited:
                matched = response['framing'] == 'close-delimited' and observation.get('closed') is True
        elif kind == 'connection':
            if value == 'close':
                matched = observation.get('closed') is True
                if extended:
                    ws = observation.get('websocket', {})
                    followup = observation.get('followup', {})
                    matched = response['ended'] is True and ws.get('clean_close') is True \
                        and ws.get('echo_hex') == '706172697479' and body.hex() == '8106706172697479880203e8' \
                        and followup.get('connection') == observation['connection'] and followup.get('status') == 200
            else:
                followup = observation.get('followup')
                matched = isinstance(followup, dict) and followup.get('connection') == observation['connection'] \
                    and type(followup.get('status')) is int and 200 <= followup['status'] < 300
                if close_delimited:
                    matched = observation.get('closed') is True
        elif kind == 'closer':
            matched = observation.get('closer') == value
        if not matched:
            errors.append(f'line {line}: {kind} {value} differs from observed response')
    if index + 1 != len(responses):
        errors.append('unexpected extra responses')
    if observation['case'] == 'websocket.tseq:upgrade_handshake_fixed_key':
        websocket = observation.get('websocket', {})
        if websocket.get('echo_hex') != '706172697479' or websocket.get('clean_close') is not True:
            errors.append('missing WebSocket echo or clean close witness')
    return errors


def check_receipt(root, inventory, receipt):
    errors = []
    try:
        if inventory['v'] != 1 or receipt['v'] != 1:
            raise ValueError('unsupported receipt version')
        cases = corpus_cases(root)
        capabilities = inventory['capabilities']
        identities = [item['id'] for item in capabilities]
        if not identities or len(identities) != len(set(identities)):
            errors.append('missing or duplicate capabilities')
        rows = table_rows(root / inventory['source'])
        if sorted(item['behavior'] for item in capabilities) != sorted(row[0] for row in rows):
            errors.append('capability inventory differs from documented inventory')
        mapped_cases = {case for item in capabilities for case in capability_cases(item)}
        if mapped_cases != set(cases):
            errors.append('corpus cases differ from capability inventory')
        if receipt['inputs'] != input_hashes(root, inventory):
            errors.append('stale or missing input hashes')
        commands = {}
        for command in receipt['commands']:
            if command['id'] in commands:
                errors.append('duplicate command receipt')
            commands[command['id']] = command
            log = root / command['log']
            if type(command['exit']) is not int or command['exit'] != 0 or not command['argv'] or not command['tool'] \
                    or not log.is_file() or not log.stat().st_size:
                errors.append('failed command or missing command evidence')
        observations = {}
        observed_cells = set()
        for observation in receipt['observations']:
            if observation['id'] in observations:
                errors.append('duplicate observation')
            observations[observation['id']] = observation
            identity = (observation['case'], observation['mode'])
            if identity in observed_cells:
                errors.append('duplicate case/mode observation')
            observed_cells.add(identity)
            if observation['command'] not in commands or observation.get('kind') != KINDS.get(observation['mode']) \
                    or not observation['connection'] or observation['case'] not in cases or not observation['responses']:
                errors.append('missing or malformed transport observation')
            for response in observation['responses']:
                if type(response['status']) is not int or not 100 <= response['status'] <= 599 \
                        or not isinstance(response['status_line'], str) or not isinstance(response['headers'], list) \
                        or not isinstance(response['trailers'], list) or response['ended'] is not True:
                    errors.append('malformed or unfinished response')
                for name, value in response['headers'] + response['trailers']:
                    if not isinstance(name, str) or not isinstance(value, str):
                        errors.append('malformed fields')
                bytes.fromhex(response['body_hex'])
                if observation['mode'] in ('h2-tls', 'h3-quic') and type(response['stream']) is not int:
                    errors.append('missing multiplexed stream identity')
            if observation['mode'] in ('h2-tls', 'h3-quic'):
                streams = [response['stream'] for response in observation['responses']]
                if len(streams) != len(set(streams)):
                    errors.append('multiplexed responses lack distinct stream correlation')
        supporting = {}
        for item in receipt.get('supporting', []):
            if item['id'] in supporting or item['id'] in observations:
                errors.append('duplicate supporting observation')
            supporting[item['id']] = item
            command = commands.get(item['command'])
            if command is None or item['kind'] not in ('semantic', 'engine-frame', 'provider-bio', 'tcp', 'tls', 'quic'):
                errors.append('missing supporting command or invalid evidence kind')
            elif item['suite'] == 'behavior-wire':
                witness = item.get('witness', {})
                if item['name'] != 'runtime_deny_refuses_next_request' or item['kind'] != KINDS.get(item.get('mode')) \
                        or witness.get('allow_status') != 200 or witness.get('mutation_status') != 204 \
                        or witness.get('refused') is not True or type(witness.get('application_bytes')) is not int \
                        or witness.get('application_bytes') != 0 or not witness.get('connection'):
                    errors.append('missing live policy refusal witness')
                if item.get('mode') in ('h2-tls', 'h3-quic') and type(witness.get('stream')) is not int:
                    errors.append('missing refused stream identity')
            else:
                log = (root / command['log']).read_text()
                if item['name'] not in executed_tests(log) or not matching_execution(command, item['suite'], log):
                    errors.append('named supporting test lacks matching suite execution provenance')
        outcomes = {}
        for outcome in receipt['outcomes']:
            key = (outcome['capability'], outcome['mode'])
            if key in outcomes:
                errors.append('duplicate outcome')
            outcomes[key] = outcome
        required = set()
        for capability in capabilities:
            if set(capability['cells']) != set(MODES):
                errors.append('missing protocol cells for ' + capability['id'])
            for mode, cell in capability['cells'].items():
                key = (capability['id'], mode)
                required.add(key)
                outcome = outcomes.get(key)
                if outcome is None:
                    errors.append('missing outcome ' + ':'.join(key))
                    continue
                if cell['applicable'] is False:
                    if outcome['classification'] != 'not-applicable' or not cell.get('rationale') \
                            or not local_reference(root, cell.get('reference', '')):
                        errors.append('unjustified applicability exclusion')
                    continue
                if cell['applicable'] is not True or outcome['classification'] not in ('preserved', 'migration'):
                    errors.append('unavailable, skipped or retained defect ' + ':'.join(key))
                    continue
                changes = outcome.get('changes', [])
                if changes and outcome['classification'] != 'migration':
                    errors.append('preserved cell contains migrations')
                if outcome['classification'] == 'migration' and (not outcome.get('rationale') \
                        or not local_reference(root, outcome.get('reference', ''))):
                    errors.append('missing migration reference or rationale')
                if not isinstance(changes, list) or not set(changes).issubset(CHANGES):
                    errors.append('semantic expectations cannot be waived')
                evidence = outcome['evidence']
                if not capability_cases(capability):
                    tests = capability.get('tests', [])
                    expected = {(test['suite'], test['name'], test['kind']) for test in tests
                                if mode in test.get('modes', MODES)}
                    actual = {(supporting[item]['suite'], supporting[item]['name'], supporting[item]['kind'])
                              for item in evidence if item in supporting and supporting[item].get('mode', mode) == mode}
                    if not expected or actual != expected or any(item not in supporting for item in evidence):
                        errors.append('missing or unrelated named capability evidence')
                    continue
                if not evidence or any(item not in observations for item in evidence):
                    errors.append('missing per-case evidence')
                    continue
                observed_cases = {observations[item]['case'] for item in evidence if observations[item]['mode'] == mode}
                if not set(capability_cases(capability)).issubset(observed_cases):
                    errors.append('missing case observation in capability cell')
                for item in evidence:
                    observation = observations[item]
                    if observation['mode'] != mode or observation['case'] not in capability_cases(capability):
                        errors.append('evidence belongs to a different cell')
                        continue
                    errors += [':'.join(key) + ': ' + error for error in
                               check_expectations(root, cases[observation['case']], observation, changes)]
        if set(outcomes) != required:
            errors.append('unexpected or missing outcomes')
        if not observations or not commands:
            errors.append('empty observation or command run')
    except (KeyError, TypeError, ValueError, OSError, IndexError) as error:
        errors.append('malformed audit input: ' + str(error))
    return errors


def named_execution(binary, suite, log):
    """Bind named checks to the invoked executable, including libtool wrappers."""
    actual = binary.parent / '.libs' / binary.name
    actual = actual if actual.is_file() else binary
    return {'suite': suite, 'executable': str(actual.resolve()),
            'sha256': hashlib.sha256(actual.read_bytes()).hexdigest(),
            'log_sha256': hashlib.sha256(log.encode()).hexdigest(),
            'tests': len(executed_tests(log)),
            'checks': int(re.search(r'^(\d+) checks$', log, re.MULTILINE)[1])}


def matching_execution(command, suite, log):
    proof = command.get('named_execution', {})
    argv = command['argv']
    cwd = Path(command.get('cwd', ''))
    if not isinstance(proof, dict) or not cwd.is_absolute() or not argv or proof.get('suite') != suite:
        return False
    binary = (cwd / argv[-1]).resolve()
    if binary.name != suite or not binary.is_file():
        return False
    if len(argv) != 1:
        # The sole supported launcher is the explicitly recorded peer_policy affinity.
        if suite != 'peer_policy' or len(argv) != 4 or Path(argv[0]).name != 'taskset' \
                or argv[1] != '-c' or not argv[2].isdigit():
            return False
    if not executed_tests(log):
        return False
    expected = named_execution(binary, suite, log)
    return proof == expected and type(proof.get('tests')) is int and type(proof.get('checks')) is int


def executed_tests(log):
    """Require completed named Littletest checks, never merely a zero exit."""
    running = re.findall(r'^Running test \((\d+)\): ([\w]+)$', log, re.MULTILINE)
    names = [name for _, name in running]
    completed = re.findall(r'^- Time spent during "([\w]+)":', log, re.MULTILINE)
    counts = [re.findall(pattern, log, re.MULTILINE) for pattern in
              (r'^(\d+) tests executed$', r'^(\d+) checks$', r'^-> (\d+) successes$',
               r'^-> (\d+) failures$', r'^-> (\d+) skipped$')]
    if any(len(values) != 1 for values in counts):
        return set()
    tests, checks, successes, failures, skipped = (int(values[0]) for values in counts)
    if checks < 1 or tests != len(names) or checks != successes or names != completed \
            or [int(number) for number, _ in running] != list(range(1, tests + 1)) \
            or len(set(names)) != len(names) or failures or skipped:
        return set()
    return set(names)


REGRESSIONS = ('behavior_replay', 'behavior_parity', 'routing_corpus', 'hooks_corpus', 'basic_auth_corpus',
    'digest_auth_corpus', 'forms_urlencoded_corpus', 'forms_multipart_corpus', 'ip_controls_corpus',
    'native_http1_parity', 'native_http1_e2e', 'native_server', 'route_sync', 'request_lifecycle',
    'response_sources', 'response_definition', 'http2_exchange', 'http2_streaming', 'http2_websocket',
    'http3_exchange', 'http3_streaming', 'http3_websocket', 'server_options_validate')
TLS_REGRESSIONS = ('tls_io', 'tls_io_race', 'tls_io_loopback', 'tls_selection', 'tls_mtls', 'tls_psk',
    'tls_psk_contract', 'tls_psk_runtime', 'tls_policy_concurrency', 'tls_policy_hostile_profiles')


def suite_command(binary):
    # Existing busy shared-mutex readers can indefinitely starve the two writers
    # on Linux. Keep all threads/checks, using explicit bounded local scheduling.
    if binary.name == 'peer_policy' and hasattr(os, 'sched_getaffinity') and shutil.which('taskset'):
        return [shutil.which('taskset'), '-c', str(min(os.sched_getaffinity(0))), binary]
    return [binary]


def receipt_directory(root, directory):
    root, directory = root.resolve(), directory.resolve()
    if directory.exists():
        raise ValueError('require a fresh receipt directory')
    if not directory.is_relative_to(root):
        if not os.environ.get('V3_RELEASE_IDENTITY'):
            raise ValueError('require a receipt directory inside the task worktree')
        release_identity.verified(root)
    return directory


def run_audit(root, inventory, tls_on, tls_off, python, openssl, directory):
    """Execute this fixed corpus and its named capability checks; retain failures."""
    root = root.resolve()
    directory = receipt_directory(root, directory)
    directory.mkdir(parents=True)
    receipt = {'v': 1, 'task': 'TASK-179', 'inputs': input_hashes(root, inventory),
               'commands': [], 'observations': [], 'supporting': [], 'outcomes': []}
    failure = None

    def execute(identity, argv, cwd=root, tool=None, timeout=300):
        log = directory / (identity + '.log')
        try:
            with log.open('w') as output:
                result = subprocess.run([str(arg) for arg in argv], cwd=cwd, stdout=output,
                                        stderr=subprocess.STDOUT, timeout=timeout, check=False)
            code = result.returncode
        except (OSError, subprocess.TimeoutExpired) as error:
            with log.open('a') as output:
                output.write(str(error) + '\n')
            code = 124
        command = {'id': identity, 'argv': [str(arg) for arg in argv], 'cwd': str(cwd), 'exit': code,
                   'log': str(log.relative_to(root)) if log.is_relative_to(root) else str(log), 'tool': tool or 'task-local executable'}
        receipt['commands'].append(command)
        if code:
            raise RuntimeError(identity + ' failed; see ' + command['log'])
        return log.read_text()

    try:
        versions = execute('tool-versions', [python, '-c',
            'import sys,ssl; from importlib.metadata import version; '
            'print(sys.version); print(ssl.OPENSSL_VERSION); print("h2",version("h2")); print("aioquic",version("aioquic"))'],
            tool=str(python))
        provider = execute('provider-version', [openssl, 'version'], tool=str(openssl))
        supporting_tests = {test['suite'] for cap in inventory['capabilities'] for test in cap.get('tests', [])
                            if test['suite'] != 'behavior-wire'}
        common = set(REGRESSIONS) | (supporting_tests - set(TLS_REGRESSIONS))
        for lane, build in (('off', tls_off), ('on', tls_on)):
            execute('build-' + lane + '-src', ['make', '-j8', '-C', build / 'src'], tool='configured C++20 compiler')
            targets = common | (set(TLS_REGRESSIONS) | {'native_http1_tls_parity', 'http2_tls_fixture', 'http3_udp_fixture'} if lane == 'on' else set())
            execute('build-' + lane + '-tests', ['make', '-j8', '-C', build / 'test', *sorted(targets)], tool='configured C++20 compiler')
            for suite in sorted(targets - {'native_http1_tls_parity', 'http2_tls_fixture', 'http3_udp_fixture'}):
                binary = build / 'test' / suite
                actual_binary = binary.parent / '.libs' / suite
                binary_hash = hashlib.sha256((actual_binary if actual_binary.is_file() else binary).read_bytes()).hexdigest()
                log = execute(lane + '-' + suite, suite_command(binary), cwd=build / 'test', tool='sha256:' + binary_hash)
                named = executed_tests(log)
                if not named:
                    raise RuntimeError(lane + '-' + suite + ' has failed, skipped, empty or incomplete named checks')
                receipt['commands'][-1]['named_execution'] = named_execution(binary, suite, log)
                if lane == 'on':
                    for name in sorted(named):
                        kinds = {test['kind'] for cap in inventory['capabilities'] for test in cap.get('tests', [])
                                 if test['suite'] == suite and test['name'] == name}
                        for kind in sorted(kinds):
                            receipt['supporting'].append({'id': suite + ':' + name + ':' + kind,
                                'command': lane + '-' + suite, 'suite': suite, 'name': name, 'kind': kind})
            execute('check-local-' + lane, ['make', '-C', build, 'check-local'], tool='Automake local build gates')
        log = execute('h1-corpus', [tls_on / 'test/native_http1_tls_parity'], cwd=tls_on / 'test', tool='native C++20 owner; ' + provider.strip())
        for line in log.splitlines():
            if line.startswith('TASK179_OBSERVATION '):
                receipt['observations'].append(json.loads(line[20:]))
            elif line.startswith('TASK179_SUPPORTING '):
                receipt['supporting'].append(json.loads(line[19:]))
        execute('test-certificate', [openssl, 'req', '-config', '/etc/ssl/openssl.cnf', '-x509', '-newkey', 'rsa:2048',
            '-nodes', '-keyout', directory / 'key.pem', '-out', directory / 'cert.pem', '-days', '2',
            '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'], tool=provider.strip())
        for protocol, fixture in (('h2', 'http2_tls_fixture'), ('h3', 'http3_udp_fixture')):
            output = directory / (protocol + '-observations.json')
            argv = [python, root / ('test/integ/behavior_parity_' + protocol + '.py'), '--fixture', tls_on / 'test' / fixture, '--output', output]
            if protocol == 'h3':
                argv += ['--certificate', directory / 'cert.pem', '--key', directory / 'key.pem']
            execute(protocol + '-corpus', argv, tool=versions.strip(), timeout=120)
            observed = json.loads(output.read_text())
            receipt['observations'] += observed['observations']
            receipt['supporting'] += observed['supporting']
        execute('client-contracts', [python, '-m', 'unittest', 'discover', '-s', 'test/integ', '-p', 'behavior_parity_client_test.py'], tool=versions.strip())
        execute('runner-contracts', [python, '-m', 'unittest', 'discover', '-s', 'test/integ', '-p', 'behavior_parity_runner_test.py'], tool=versions.strip())
    except (RuntimeError, ValueError, KeyError, OSError) as error:
        failure = str(error)
    finally:
        for cap in inventory['capabilities']:
            for mode, cell in cap['cells'].items():
                outcome = {'capability': cap['id'], 'mode': mode, 'classification': 'preserved', 'evidence': []}
                if not cell['applicable']:
                    outcome['classification'] = 'not-applicable'
                else:
                    outcome['evidence'] = [item['id'] for item in receipt['observations']
                                           if item['mode'] == mode and item['case'] in capability_cases(cap)]
                    if not capability_cases(cap):
                        expected = {(test['suite'], test['name'], test['kind']) for test in cap.get('tests', [])
                                    if mode in test.get('modes', MODES)}
                        outcome['evidence'] = [item['id'] for item in receipt['supporting']
                            if (item['suite'], item['name'], item['kind']) in expected and item.get('mode', mode) == mode]
                    changes = []
                    if mode in ('h2-tls', 'h3-quic') and capability_cases(cap):
                        changes = ['framing']
                        if 'shoutcast.tseq:icy_status_line' in capability_cases(cap):
                            changes.append('icy-status')
                        if 'websocket.tseq:upgrade_handshake_fixed_key' in capability_cases(cap):
                            changes.append('websocket-handshake')
                    if mode.startswith('h1.0') and any(case in capability_cases(cap) for case in ('file_resp.tseq:pipe_body', 'file_resp.tseq:deferred_body')):
                        changes.append('http1.0-close-delimited')
                    migration = cell.get('migration', cap.get('migration'))
                    if migration or changes:
                        outcome.update(classification='migration', changes=changes, **(migration or {
                            'reference': 'docs/task-179-behavior-parity.md#protocol-translations',
                            'rationale': 'Preserve application semantics through documented framing or handshake mechanism.'}))
                receipt['outcomes'].append(outcome)
        errors = check_receipt(root, inventory, receipt)
        if failure:
            errors.insert(0, failure)
        receipt['result'] = {'passed': not errors, 'errors': errors}
        (directory / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--inventory', type=Path, default=Path('test/parity/task179-capabilities.json'))
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--tls-on', type=Path)
    parser.add_argument('--tls-off', type=Path)
    parser.add_argument('--client-python', type=Path)
    parser.add_argument('--openssl', type=Path)
    parser.add_argument('--receipt-dir', type=Path)
    args = parser.parse_args()
    try:
        inventory = json.loads((args.root / args.inventory).read_text())
        if args.receipt_dir:
            if any(value is None for value in (args.tls_on, args.tls_off, args.client_python, args.openssl)):
                raise ValueError('execution requires TLS-on/off builds, pinned client Python and provider openssl')
            receipt = run_audit(args.root, inventory, args.tls_on.resolve(), args.tls_off.resolve(),
                                args.client_python.absolute(), args.openssl.resolve(), args.receipt_dir)
            errors = receipt['result']['errors']
        elif args.receipt:
            receipt = json.loads(args.receipt.read_text())
            errors = check_receipt(args.root, inventory, receipt)
        else:
            raise ValueError('provide --receipt for validation or --receipt-dir for execution')
    except (OSError, ValueError) as error:
        errors = [str(error)]
    print(json.dumps({'v': 1, 'task': 'TASK-179', 'passed': not errors, 'errors': errors}, indent=2))
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
