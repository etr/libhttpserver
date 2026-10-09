#!/usr/bin/env python3
"""TASK-179 corpus over the existing private HTTP/2 TLS owner and pinned h2."""
import argparse
import hashlib
import importlib.util
from importlib.metadata import version
import json
from pathlib import Path
import subprocess
from urllib.request import parse_http_list, parse_keqv_list

import h2.events
from http2_client_h2 import Client, masked

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('audit', ROOT / 'scripts/run-v3-behavior-parity.py')
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def request_cases(path):
    profile = None
    cases = []
    current = None
    for line in path.read_text().splitlines():
        line = line.rstrip()
        if line.startswith('profile '):
            profile = line[8:]
        elif line.startswith('case '):
            current = {'id': path.name + ':' + line[5:], 'wire': b'', 'options': {}}
            cases.append(current)
        elif line.startswith('send '):
            current['wire'] += AUDIT.bytes_value(line[5:])
        elif line.startswith('send_hex '):
            current['wire'] += bytes.fromhex(line[9:])
        elif line.startswith('option '):
            key, value = line[7:].split(' ', 1)
            current['options'][key] = value
    for case in cases:
        wire = case.pop('wire')
        requests = []
        while wire:
            head, body = wire.split(b'\r\n\r\n', 1)
            lines = head.split(b'\r\n')
            method, target, _ = lines[0].decode().split()
            fields = [(name.decode().lower(), value.decode().lstrip())
                      for name, value in (line.split(b':', 1) for line in lines[1:])]
            length = int(dict(fields).get('content-length', '0'))
            if length > len(body):
                raise ValueError('truncated corpus request')
            requests.append({'method': method, 'target': target, 'fields': fields, 'body': body[:length]})
            wire = body[length:]
        case['requests'] = requests
    return profile, cases


def multiplexed_fields(request):
    forbidden = {'host', 'connection', 'keep-alive', 'proxy-connection', 'transfer-encoding', 'upgrade'}
    for name, value in request['fields']:
        if name == 'connection':
            forbidden.update(token.strip().lower() for token in value.split(','))
    return [(name, value) for name, value in request['fields'] if name not in forbidden]


def digest_authorization(challenge):
    scheme, fields = challenge.split(' ', 1)
    assert scheme == 'Digest'
    fields = parse_keqv_list(parse_http_list(fields))
    assert fields['algorithm'] == 'MD5' and fields['qop'] == 'auth'
    digest = lambda text: hashlib.md5(text.encode(), usedforsecurity=False).hexdigest()
    cnonce, nc = 'task179-independent-client', '00000001'
    response = digest(digest('bob:' + fields['realm'] + ':builder') + ':' + fields['nonce'] + ':' + nc + ':' + cnonce + ':auth:' + digest('GET:/digest'))
    return ('Digest username="bob", realm="' + fields['realm'] + '", nonce="' + fields['nonce'] +
            '", uri="/digest", response="' + response + '", algorithm=MD5, qop=auth, nc=' + nc +
            ', cnonce="' + cnonce + '", opaque="' + fields['opaque'] + '"')


class ParityClient(Client):
    def __init__(self, fixture, profile):
        self.proc = subprocess.Popen([fixture, '--behavior-profile', profile], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, text=True, bufsize=1)
        self.sock = None

    def send_request(self, request):
        stream = self.h2.get_next_available_stream_id()
        fields = [(':method', request['method']), (':scheme', 'https'), (':path', request['target']),
                  (':authority', dict(request['fields']).get('host', 'localhost'))] + multiplexed_fields(request)
        self.h2.send_headers(stream, fields, end_stream=not request['body'])
        self.flush()
        if request['body']:
            at = 0
            while at < len(request['body']):
                self.wait(lambda: self.h2.local_flow_control_window(stream) > 0)
                size = min(len(request['body']) - at, self.h2.local_flow_control_window(stream), self.h2.max_outbound_frame_size)
                self.h2.send_data(stream, request['body'][at:at + size], end_stream=at + size == len(request['body']))
                at += size
                self.flush()
        return stream

    def observed(self, stream):
        self.wait(lambda: stream in self.ended)
        heads, trailers = [], []
        for event in self.events:
            if getattr(event, 'stream_id', None) != stream:
                continue
            if isinstance(event, h2.events.ResponseReceived):
                heads.extend((name, value) for name, value in event.headers if not name.startswith(':'))
            elif isinstance(event, h2.events.TrailersReceived):
                trailers.extend(event.headers)
        return {'status': int(self.status[stream]), 'status_line': '', 'headers': heads, 'trailers': trailers,
                'body_hex': bytes(self.data.get(stream, b'')).hex(), 'framing': 'data', 'ended': True, 'stream': stream}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert version('h2') == '4.4.1'
    observations = []
    policy = []
    cases = AUDIT.corpus_cases(ROOT)
    for path in sorted((ROOT / 'test/parity/transcripts').glob('*.tseq')):
        profile, tests = request_cases(path)
        client = ParityClient(args.fixture, profile)
        try:
            client.connect()
            for test in tests:
                websocket = None
                if test['options'].get('curl_auth') == 'digest':
                    challenge = client.observed(client.send_request(test['requests'][0]))
                    assert challenge['status'] == 401
                    fields = dict(challenge['headers'])
                    test['requests'][0]['fields'].append(('authorization', digest_authorization(fields['www-authenticate'])))
                if path.stem == 'websocket':
                    stream = client.request('/ws', 'CONNECT', False,
                                            [(':protocol', 'websocket'), ('sec-websocket-version', '13')])
                    client.wait(lambda: stream in client.status)
                    assert client.status[stream] == '200' and stream not in client.ended
                    client.h2.send_data(stream, masked(1, b'parity'))
                    client.flush()
                    client.wait(lambda: len(client.data.get(stream, b'')) >= 8)
                    assert client.data[stream] == b'\x81\x06parity'
                    client.h2.send_data(stream, masked(8, b'\x03\xe8'), end_stream=True)
                    client.flush()
                    streams = [stream]
                    websocket = {'echo_hex': b'parity'.hex(), 'clean_close': True}
                else:
                    streams = [client.send_request(request) for request in test['requests']]
                responses = [client.observed(stream) for stream in streams]
                followup = client.observed(client.request('/__smoke'))
                observation = {'id': 'h2-tls:' + test['id'], 'command': 'h2-corpus', 'mode': 'h2-tls', 'kind': 'tls',
                               'case': test['id'], 'connection': path.stem, 'responses': responses,
                               'followup': {'connection': path.stem, 'status': followup['status']}}
                observations.append(observation)
                changes = ['framing', 'icy-status']
                if websocket:
                    observation['websocket'] = websocket
                    changes.append('websocket-handshake')
                errors = AUDIT.check_expectations(ROOT, cases[test['id']], observation, changes)
                if errors:
                    raise AssertionError(test['id'] + ': ' + '; '.join(errors))
            if profile == 'ip_controls':
                changed = client.observed(client.request('/__deny'))
                assert changed['status'] == 204
                refused = client.request('/__smoke')
                client.wait(lambda: any(isinstance(event, h2.events.StreamReset) and event.stream_id == refused
                                        for event in client.events))
                assert refused not in client.status and not client.data.get(refused)
                policy.append({'id': 'h2-tls:runtime-policy', 'command': 'h2-corpus', 'suite': 'behavior-wire',
                    'name': 'runtime_deny_refuses_next_request', 'kind': 'tls', 'mode': 'h2-tls',
                    'witness': {'allow_status': 200, 'mutation_status': 204, 'refused': True,
                                'application_bytes': 0, 'stream': refused, 'connection': path.stem}})
        finally:
            try:
                client.close()
            finally:
                args.output.write_text(json.dumps({'stack': 'hyper-h2', 'version': version('h2'), 'observations': observations, 'supporting': policy}, indent=2) + '\n')
    print(json.dumps({'cases': len(observations), 'stack': 'hyper-h2', 'version': version('h2')}))


if __name__ == '__main__':
    main()
