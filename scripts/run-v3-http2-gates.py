#!/usr/bin/env python3
"""Bounded private-engine TLS interoperability and supplemental h2spec gate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = {'multiplexing', 'reset', 'upload_flow', 'response_flow', 'websocket'}


def inventory(text):
    namespace = section = None
    cases = {}
    for line in text.splitlines():
        if line.startswith('Generic tests'):
            namespace = 'generic'
        elif line.startswith(('HTTP/2', 'Hypertext Transfer Protocol')):
            namespace = 'http2'
        elif line.startswith('HPACK:'):
            namespace = 'hpack'
        heading = re.match(r'^\s+(\d+(?:\.\d+)*)\. ', line)
        if heading:
            section = heading[1]
        case = re.match(r'^\s+(\d+): (.+)$', line)
        if case:
            if not namespace or not section:
                raise ValueError('malformed h2spec inventory')
            key = f'{namespace}/{section}/{case[1]}'
            if key in cases:
                raise ValueError('duplicate h2spec inventory case')
            cases[key] = case[2]
    if not cases:
        raise ValueError('empty h2spec inventory')
    return cases


def exclusions(path, cases):
    result = {}
    for line in path.read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        fields = line.split('\t')
        if len(fields) != 4 or not all(fields) or fields[0] not in cases or fields[0] in result:
            raise ValueError('malformed or undiscovered h2spec exclusion: ' + line)
        if fields[1] != cases[fields[0]]:
            raise ValueError('exclusion does not match pinned inventory')
        result[fields[0]] = fields[1:]
    return result


def client_receipt(text):
    try:
        result = json.loads(text.strip().splitlines()[-1])
        valid = (set(result['scenarios']) == SCENARIOS and len(result['scenarios']) == len(SCENARIOS)
                 and result['alpn'] == 'h2' and result['cancel_count'] == 1
                 and result['uploaded_bytes'] == 163840 and result['response_bytes'] == 131072
                 and result['response_stalled_bytes'] == 32 and result['ws_clean_close'] is True
                 and 0 < result['upload_stalled_bytes'] <= 65535)
    except (ValueError, KeyError, IndexError, TypeError) as error:
        raise ValueError('malformed client receipt') from error
    if not valid:
        raise ValueError('incomplete client scenario coverage')
    return result


def executable(value, name):
    found = shutil.which(value)
    if not found:
        raise ValueError('missing ' + name + ': ' + value)
    return str(Path(found).absolute())


def run(command, path, timeout):
    with path.open('w') as log:
        log.write('COMMAND ' + json.dumps(command) + '\n'); log.flush()
        try:
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=timeout)
            log.write(result.stdout); log.write(f'\nEXIT {result.returncode}\n')
        except subprocess.TimeoutExpired as error:
            log.write(str(error)); raise ValueError('timeout: ' + str(path)) from error
    if result.returncode:
        raise ValueError(f'subprocess exit {result.returncode}: {path}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--log-dir', type=Path, required=True)
    parser.add_argument('--python', default='python3')
    parser.add_argument('--node', default='node')
    parser.add_argument('--h2spec', default='h2spec')
    args = parser.parse_args(); args.log_dir.mkdir(parents=True, exist_ok=True)
    fixture = args.build_dir.resolve() / 'test/http2_tls_fixture'
    if not fixture.is_file() or not os.access(fixture, os.X_OK):
        raise ValueError('missing fixture: ' + str(fixture))
    python = executable(args.python, 'Python client tool')
    node = executable(args.node, 'Node client tool')
    h2spec = executable(args.h2spec, 'h2spec')
    results = []
    for tool, client in [(python, 'http2_client_h2.py'), (node, 'http2_client_node.mjs')]:
        out = run([tool, str(ROOT / 'test/integ' / client), '--fixture', str(fixture)],
                  args.log_dir / (client + '.log'), 90)
        receipt = client_receipt(out)
        expected = ('hyper-h2', '4.4.1') if client.endswith('.py') else ('node:http2', '24.15.0')
        if (receipt.get('stack'), receipt.get('version')) != expected:
            raise ValueError('independent client identity mismatch')
        if client.endswith('.mjs') and not receipt.get('nghttp2'):
            raise ValueError('missing nghttp2 identity')
        results.append(receipt)
    identity = run([h2spec, '--version'], args.log_dir / 'h2spec-version.log', 15)
    if identity.strip() != 'Version: 2.6.0 (70ac2294010887f48b18e2d64f5cccd48421fad1)':
        raise ValueError('h2spec version/source identity mismatch')
    digest = hashlib.sha256(Path(h2spec).read_bytes()).hexdigest()
    listing = run([h2spec, '--strict', '--dryrun'], args.log_dir / 'h2spec-inventory.log', 10)
    cases = inventory(listing)
    excluded = exclusions(ROOT / 'test/conformance/http2/h2spec-exclusions.tsv', cases)
    err = (args.log_dir / 'fixture.stderr').open('w')
    proc = subprocess.Popen([str(fixture)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=err, text=True, bufsize=1)
    executed = []
    try:
        if not select.select([proc.stdout], [], [], 5)[0]:
            raise ValueError('fixture readiness timeout')
        ready = proc.stdout.readline().split()
        if len(ready) != 2 or ready[0] != 'READY':
            raise ValueError('fixture readiness malformed')
        for case in cases:
            if case in excluded:
                continue
            stem = case.replace('/', '-')
            report = args.log_dir.resolve() / (stem + '.xml')
            run([h2spec, case, '--strict', '--tls', '--insecure', '--host', '127.0.0.1',
                 '--port', ready[1], '--timeout', '2', '--junit-report', str(report)],
                args.log_dir / (stem + '.log'), 12)
            tree = ET.parse(report)
            checks = tree.findall('.//testcase')
            if (len(checks) != 1 or tree.findall('.//failure') or tree.findall('.//error') or tree.findall('.//skipped')
                    or checks[0].get('classname') != cases[case]
                    or checks[0].get('package') != case.rsplit('/', 1)[0]):
                raise ValueError('empty, failed or malformed h2spec result: ' + case)
            executed.append(case)
    finally:
        if proc.poll() is None:
            try:
                proc.stdin.write('STOP\n'); proc.stdin.flush()
            except BrokenPipeError:
                pass
        try:
            code = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(); raise ValueError('fixture failed to stop')
        finally:
            err.close()
        if code:
            raise ValueError('fixture failed: see fixture.stderr')
    if set(executed) | set(excluded) != set(cases) or not executed:
        raise ValueError('incomplete h2spec inventory coverage')
    receipt = {'clients': results, 'h2spec': {'identity': identity.strip(), 'sha256': digest,
                                           'inventory': cases, 'executed': executed, 'excluded': excluded}}
    (args.log_dir / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'PASS HTTP/2: two clients; {len(executed)} h2spec cases; {len(excluded)} RFC 9113 exclusions')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, ET.ParseError) as error:
        print(error, file=sys.stderr); sys.exit(1)
