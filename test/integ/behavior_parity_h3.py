#!/usr/bin/env python3
"""TASK-179 corpus over the private UDP owner and independent aioquic 1.3.0."""
import argparse
import asyncio
from importlib.metadata import version
import json
from pathlib import Path
import select
import subprocess
import time
from aioquic.asyncio import connect
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import StreamReset
from aioquic.h3.connection import MessageError
from http3_semantic_aioquic import SemanticClient, ObservedH3, done
from http3_client_aioquic import StreamFailure
from behavior_parity_h2 import AUDIT, ROOT, request_cases, multiplexed_fields, digest_authorization, masked


class HeadAwareH3(ObservedH3):
    def __init__(self, quic):
        super().__init__(quic)
        self.head_streams = set()

    def _check_content_length(self, stream):
        # aioquic 1.3.0 lacks outgoing HEAD context. RFC 9110 permits the
        # representation's length but forbids a response body for HEAD.
        if stream.stream_id in self.head_streams:
            if stream.content_length:
                raise MessageError('HEAD response contained DATA')
            return
        super()._check_content_length(stream)


class ParityClient(SemanticClient):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, h3_factory=HeadAwareH3, **kwargs)

    def send_request(self, request):
        stream = self._quic.get_next_available_stream_id()
        state = {'done': asyncio.get_running_loop().create_future(), 'headers': [], 'trailers': [],
                 'body': bytearray(), 'stream': stream, 'fin_count': 0}
        self.pending[stream] = state
        if request['method'] == 'HEAD':
            self.h3.head_streams.add(stream)
        fields = [(':method', request['method']), (':scheme', 'https'), (':path', request['target']),
                  (':authority', dict(request['fields']).get('host', 'localhost'))] + multiplexed_fields(request)
        self.h3.send_headers(stream, [(name.encode(), value.encode()) for name, value in fields],
                             end_stream=not request['body'] and not request.get('websocket'))
        for at in range(0, len(request['body']), 3):
            part = request['body'][at:at + 3]
            self.h3.send_data(stream, part, end_stream=at + len(part) == len(request['body']))
        self.transmit()
        return state


def observed(state):
    fields = [(name.decode(), value.decode()) for name, value in state['headers']]
    return {'status': int(dict(fields)[':status']), 'status_line': '',
            'headers': [(name, value) for name, value in fields if not name.startswith(':')],
            'trailers': [(name.decode(), value.decode()) for name, value in state['trailers']],
            'body_hex': bytes(state['body']).hex(), 'framing': 'data',
            'ended': state['fin_count'] == 1, 'stream': state['stream']}


async def run(args):
    assert version('aioquic') == '1.3.0'
    observations = []
    policy = []
    cases = AUDIT.corpus_cases(ROOT)
    for path in sorted((ROOT / 'test/parity/transcripts').glob('*.tseq')):
        profile, tests = request_cases(path)
        log = args.output.parent / ('h3-' + path.stem + '-fixture.log')
        with log.open('w') as errors:
            proc = subprocess.Popen([args.fixture, args.certificate, args.key, str(args.output.parent / ('h3-' + path.stem + '.pcap')),
                                     '--behavior-profile', profile], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=errors, text=True, bufsize=1)
            try:
                if not select.select([proc.stdout], [], [], 5)[0]:
                    raise RuntimeError('HTTP/3 readiness deadline')
                line = proc.stdout.readline().split()
                assert len(line) == 2 and line[0] == 'READY', line
                config = QuicConfiguration(is_client=True, alpn_protocols=['h3'], supported_versions=[1])
                config.load_verify_locations(cafile=args.certificate)
                config.server_name = 'localhost'
                async with connect('127.0.0.1', int(line[1]), configuration=config, create_protocol=ParityClient) as client:
                    assert client.handshake.alpn_protocol == 'h3'
                    for test in tests:
                        websocket = None
                        if test['options'].get('curl_auth') == 'digest':
                            challenge = observed(await done(client.send_request(test['requests'][0])))
                            assert challenge['status'] == 401
                            test['requests'][0]['fields'].append(('authorization', digest_authorization(dict(challenge['headers'])['www-authenticate'])))
                        if path.stem == 'websocket':
                            state = client.send_request({'method': 'CONNECT', 'target': '/ws', 'body': b'', 'websocket': True,
                                                         'fields': [(':protocol', 'websocket'), ('sec-websocket-version', '13')]})
                            deadline = time.monotonic() + 5
                            while not state['headers']:
                                if time.monotonic() > deadline:
                                    raise RuntimeError('Extended CONNECT response deadline')
                                await asyncio.sleep(.005)
                            assert dict(state['headers'])[b':status'] == b'200' and not state['done'].done()
                            client.h3.send_data(state['stream'], masked(1, b'parity'), end_stream=False)
                            client.transmit()
                            while len(state['body']) < 8:
                                if time.monotonic() > deadline:
                                    raise RuntimeError('WebSocket echo deadline')
                                await asyncio.sleep(.005)
                            assert state['body'] == b'\x81\x06parity'
                            client.h3.send_data(state['stream'], masked(8, b'\x03\xe8'), end_stream=True)
                            client.transmit()
                            states = [state]
                            websocket = {'echo_hex': b'parity'.hex(), 'clean_close': True}
                        else:
                            states = [client.send_request(request) for request in test['requests']]
                        responses = [observed(await done(state)) for state in states]
                        followup = observed(await done(client.send_request({'method': 'GET', 'target': '/__smoke', 'fields': [], 'body': b''})))
                        observation = {'id': 'h3-quic:' + test['id'], 'command': 'h3-corpus', 'mode': 'h3-quic', 'kind': 'quic',
                                       'case': test['id'], 'connection': path.stem, 'responses': responses,
                                       'followup': {'connection': path.stem, 'status': followup['status']}}
                        observations.append(observation)
                        changes = ['framing', 'icy-status']
                        if websocket:
                            observation['websocket'] = websocket
                            changes.append('websocket-handshake')
                        failures = AUDIT.check_expectations(ROOT, cases[test['id']], observation, changes)
                        if failures:
                            raise AssertionError(test['id'] + ': ' + '; '.join(failures))
                    if profile == 'ip_controls':
                        changed = observed(await done(client.send_request({'method': 'GET', 'target': '/__deny', 'fields': [], 'body': b''})))
                        assert changed['status'] == 204
                        refused = client.send_request({'method': 'GET', 'target': '/__smoke', 'fields': [], 'body': b''})
                        try:
                            await done(refused)
                            raise AssertionError('runtime deny admitted the next request')
                        except StreamFailure as failure:
                            assert isinstance(failure.event, StreamReset)
                            assert not refused['headers'] and not refused['body']
                        policy.append({'id': 'h3-quic:runtime-policy', 'command': 'h3-corpus', 'suite': 'behavior-wire',
                            'name': 'runtime_deny_refuses_next_request', 'kind': 'quic', 'mode': 'h3-quic',
                            'witness': {'allow_status': 200, 'mutation_status': 204, 'refused': True,
                                        'application_bytes': 0, 'stream': refused['stream'], 'connection': path.stem}})
            finally:
                proc.stdin.close()
                proc.stdin = None
                try:
                    output, _ = proc.communicate(timeout=5)
                    errors.write(output)
                    if proc.returncode:
                        raise RuntimeError('HTTP/3 fixture failed: ' + str(proc.returncode))
                finally:
                    if proc.poll() is None:
                        proc.kill()
                        proc.wait()
                    args.output.write_text(json.dumps({'stack': 'aioquic', 'version': version('aioquic'), 'observations': observations, 'supporting': policy}, indent=2) + '\n')
    print(json.dumps({'cases': len(observations), 'stack': 'aioquic', 'version': version('aioquic')}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--certificate', required=True)
    parser.add_argument('--key', required=True)
    parser.add_argument('--output', type=Path, required=True)
    asyncio.run(run(parser.parse_args()))


if __name__ == '__main__':
    main()
