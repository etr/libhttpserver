#!/usr/bin/env python3
"""Independent bounded benchmark clients; no server implementation imports."""
import argparse
import asyncio
from concurrent.futures import ThreadPoolExecutor
import http.client
import importlib.metadata
import json
from pathlib import Path
import socket
import ssl
import time
import threading


def require(value, message):
    if not value: raise ValueError(message)


def check_response(status, headers, body, run_id, request, protocol, payload):
    require(status == 200 and body == b'x' * payload, 'wrong response status/content')
    require(all(headers.get(key) == value for key, value in
                {'x-run': run_id, 'x-request': request, 'x-protocol': protocol}.items()), 'wrong response identity/protocol')


class H1:
    def __init__(self, args):
        self.args = args
        self.connection = http.client.HTTPConnection('127.0.0.1', args.port, timeout=5)
        peer = socket.create_connection(('127.0.0.1', args.port), timeout=5)
        if args.protocol == 'h1-tls':
            context = ssl.create_default_context(cafile=str(args.fixtures / 'root.pem'))
            context.set_alpn_protocols(['http/1.1'])
            peer = context.wrap_socket(peer, server_hostname='a.example')
            require(peer.selected_alpn_protocol() == 'http/1.1', 'H1 TLS ALPN')
        self.connection.sock = peer

    def request(self, identifier):
        started = time.monotonic_ns()
        self.connection.request('GET', '/bench', headers={'Host': 'a.example', 'x-run': self.args.run_id, 'x-request': identifier})
        response = self.connection.getresponse()
        body = response.read(self.args.payload + 1)
        ended = time.monotonic_ns()
        require(response.version == 11 and not response.will_close, 'persistent HTTP/1.1 required')
        check_response(response.status, dict(response.getheaders()), body, self.args.run_id, identifier, self.args.protocol, self.args.payload)
        return dict(start_ns=started, end_ns=ended)

    def close(self): self.connection.close()


def h1(args, result):
    clients = []
    try:
        clients = [H1(args) for _ in range(args.concurrency)]
        lock = threading.Lock()
        with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
            def phase(count, prefix):
                rows = result['warmup' if prefix == 'w' else 'samples']
                def worker(index):
                    for request in range(index, count, args.concurrency):
                        if prefix == 'm':
                            with lock: result['attempted'] += 1
                        observed = dict(id=request, **clients[index].request(prefix + str(request)))
                        with lock: rows.append(observed)
                list(executor.map(worker, range(args.concurrency)))
                return rows
            result['warmup'] = phase(args.warmup, 'w')
            start = time.monotonic_ns()
            result['samples'] = phase(args.requests, 'm')
            result['epochs'] = [dict(start_ns=start, end_ns=max(row['end_ns'] for row in result['samples']))]
    finally:
        for client in clients: client.close()


def h2_epoch(args, result, base, count, epoch):
    require(importlib.metadata.version('h2') == '4.4.1', 'h2 must be 4.4.1')
    import h2.config
    import h2.connection
    import h2.events
    context = ssl.create_default_context(cafile=str(args.fixtures / 'root.pem'))
    context.set_alpn_protocols(['h2'])
    with context.wrap_socket(socket.create_connection(('127.0.0.1', args.port), timeout=5), server_hostname='a.example') as peer:
        require(peer.selected_alpn_protocol() == 'h2', 'H2 ALPN')
        connection = h2.connection.H2Connection(h2.config.H2Configuration(header_encoding='utf-8'))
        connection.initiate_connection(); peer.sendall(connection.data_to_send())
        def phase(count, prefix, offset):
            pending, rows, next_id = {}, result['warmup' if prefix == 'w' else 'samples'], 0
            first = len(rows)
            def fill():
                nonlocal next_id
                while len(pending) < args.concurrency and next_id < count:
                    stream = connection.get_next_available_stream_id()
                    request = prefix + str(offset + next_id)
                    if prefix == 'm': result['attempted'] += 1
                    started = time.monotonic_ns()
                    connection.send_headers(stream, [(':method', 'GET'), (':scheme', 'https'), (':authority', 'a.example'),
                        (':path', '/bench'), ('x-run', args.run_id), ('x-request', request)], end_stream=True)
                    pending[stream] = dict(id=offset + next_id, request=request, start_ns=started, body=bytearray(), headers={})
                    next_id += 1
                peer.sendall(connection.data_to_send())
            fill()
            while pending:
                data = peer.recv(65536); require(data, 'H2 premature EOF')
                for event in connection.receive_data(data):
                    require(not isinstance(event, (h2.events.StreamReset, h2.events.ConnectionTerminated)), 'H2 reset/GOAWAY')
                    row = pending.get(getattr(event, 'stream_id', None))
                    if row is None: continue
                    if isinstance(event, h2.events.ResponseReceived): row['headers'] = dict(event.headers)
                    if isinstance(event, h2.events.DataReceived):
                        row['body'].extend(event.data)
                        require(len(row['body']) <= args.payload, 'H2 body bound')
                        connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                    if isinstance(event, h2.events.StreamEnded):
                        ended = time.monotonic_ns()
                        check_response(int(row['headers'].get(':status', 0)), row['headers'], bytes(row['body']), args.run_id,
                                       row['request'], args.protocol, args.payload)
                        rows.append(dict(id=row['id'], epoch=epoch, start_ns=row['start_ns'], end_ns=ended))
                        del pending[event.stream_id]
                fill()
            return rows[first:]
        phase(args.concurrency, 'w', len(result['warmup']))
        start = time.monotonic_ns()
        measured = phase(count, 'm', base)
        result['epochs'].append(dict(start_ns=start, end_ns=max(row['end_ns'] for row in measured)))


def h2(args, result):
    # The public default allows 128 openings/second per connection. Retain
    # each connection for eight warmups and 64 measured multiplexed requests.
    for epoch, base in enumerate(range(0, args.requests, 64)):
        h2_epoch(args, result, base, min(64, args.requests - base), epoch)


async def h3(args, result):
    require(importlib.metadata.version('aioquic') == '1.3.0', 'aioquic must be 1.3.0')
    from aioquic.asyncio import connect, QuicConnectionProtocol
    from aioquic.h3.connection import H3Connection
    from aioquic.h3.events import HeadersReceived, DataReceived
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.quic.events import HandshakeCompleted, ConnectionTerminated, StreamReset
    class Client(QuicConnectionProtocol):
        def __init__(self, *a, **kw):
            super().__init__(*a, **kw)
            self.http = H3Connection(self._quic); self.pending = {}; self.handshake = None
        def quic_event_received(self, event):
            if isinstance(event, HandshakeCompleted): self.handshake = event
            if isinstance(event, (ConnectionTerminated, StreamReset)):
                for stream, row in self.pending.items():
                    if isinstance(event, ConnectionTerminated) or event.stream_id == stream:
                        if not row['done'].done(): row['done'].set_exception(ValueError('H3 termination/reset: ' + repr(event)))
            for item in self.http.handle_event(event):
                row = self.pending.get(getattr(item, 'stream_id', None))
                if row is None: continue
                if isinstance(item, HeadersReceived):
                    row['headers'].update({k.decode(): v.decode() for k, v in item.headers})
                if isinstance(item, DataReceived):
                    row['body'].extend(item.data)
                    if len(row['body']) > args.payload and not row['done'].done():
                        row['done'].set_exception(ValueError('H3 body bound'))
                if item.stream_ended and not row['done'].done():
                    row['end_ns'] = time.monotonic_ns(); row['done'].set_result(row)
        async def request(self, identifier, epoch=0):
            stream = self._quic.get_next_available_stream_id()
            row = dict(done=asyncio.get_running_loop().create_future(), headers={}, body=bytearray())
            self.pending[stream] = row
            if identifier.startswith('m'): result['attempted'] += 1
            started = time.monotonic_ns()
            self.http.send_headers(stream, [(b':method', b'GET'), (b':scheme', b'https'), (b':authority', b'a.example'),
                (b':path', b'/bench'), (b'x-run', args.run_id.encode()), (b'x-request', identifier.encode())], end_stream=True)
            self.transmit()
            try:
                await asyncio.wait_for(row['done'], 5)
                check_response(int(row['headers'].get(':status', 0)), row['headers'], bytes(row['body']),
                               args.run_id, identifier, args.protocol, args.payload)
                observed = dict(id=int(identifier[1:]), start_ns=started, end_ns=row['end_ns'])
                if identifier.startswith('m'): observed['epoch'] = epoch
                result['samples' if identifier.startswith('m') else 'warmup'].append(observed)
                return observed
            finally: self.pending.pop(stream, None)
    config = QuicConfiguration(is_client=True, alpn_protocols=['h3'], supported_versions=[1])
    config.server_name = 'a.example'; config.verify_mode = ssl.CERT_REQUIRED
    config.load_verify_locations(cafile=str(args.fixtures / 'root.pem'))
    # Bounded connection lifetime and the normal semantic data pool require
    # epochs; each connection retains four warmups/four measured streams.
    result['samples'], result['warmup'], result['epochs'] = [], [], []
    for base in range(0, args.requests, args.concurrency):
        async with connect('127.0.0.1', args.port, configuration=config, create_protocol=Client) as client:
            require(client.handshake.alpn_protocol == 'h3' and client._quic._version == 1 and
                    not client.handshake.early_data_accepted, 'QUIC identity')
            warm_base = len(result['warmup'])
            await asyncio.gather(*(client.request('w' + str(warm_base + i)) for i in range(args.concurrency)))
            start = time.monotonic_ns()
            epoch = len(result['epochs'])
            rows = await asyncio.gather(*(client.request('m' + str(i), epoch) for i in range(base, min(base + args.concurrency, args.requests))))
            result['epochs'].append(dict(start_ns=start, end_ns=max(row['end_ns'] for row in rows)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('port', 'warmup', 'requests', 'concurrency', 'payload', 'deadline'):
        parser.add_argument('--' + name, type=int, default={'warmup': 256, 'requests': 4096, 'concurrency': 8, 'payload': 1024, 'deadline': 300}.get(name))
    parser.add_argument('--protocol', choices=('h1', 'h1-tls', 'h2', 'h3'), required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(0 < args.requests <= 65536 and 0 < args.warmup <= 65536 and 0 < args.concurrency <= 8 and
            0 < args.payload <= 16384 and 0 < args.deadline <= 600, 'workload bounds')
    require(not args.output.exists(), 'sample output must be fresh')
    result = dict(run_id=args.run_id, protocol=args.protocol, concurrency=args.concurrency, requested=args.requests, attempted=0, completed=0, errors=0,
                  samples=[], warmup=[], epochs=[],
                  units=dict(timestamp='ns', duration='s', latency='ms', throughput='requests/s'),
                  clock=dict(name='monotonic_ns', resolution_ns=max(1, round(time.get_clock_info('monotonic').resolution * 1e9))))
    try:
        if args.protocol in ('h1', 'h1-tls'): h1(args, result)
        elif args.protocol == 'h2': h2(args, result)
        else: asyncio.run(asyncio.wait_for(h3(args, result), args.deadline))
        for row in result['samples']: row.update(epoch=row.get('epoch', 0), status=200, valid=True, protocol=args.protocol)
        result.update(completed=len(result['samples']), warmup_requested=len(result['warmup']), warmup_completed=len(result['warmup']))
        durations = sum(e['end_ns'] - e['start_ns'] for e in result['epochs']) / 1e9
        latencies = sorted((s['end_ns'] - s['start_ns']) / 1e6 for s in result['samples'])
        import math
        result['statistics'] = dict(duration_s=durations, throughput_rps=len(latencies) / durations,
            **{'p%d_ms' % rank: latencies[math.ceil(len(latencies) * rank / 100) - 1] for rank in (50, 95, 99)})
    except BaseException as error:
        result.update(errors=1, completed=len(result['samples']), warmup_completed=len(result['warmup']), error=str(error))
        raise
    finally:
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')


if __name__ == '__main__': main()
