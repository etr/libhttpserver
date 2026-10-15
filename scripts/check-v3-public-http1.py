#!/usr/bin/env python3
"""Installed plaintext H1 and RFC6455 witnesses using public session routes."""
import argparse
import asyncio
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import sys
import tempfile
import time
sys.path.insert(0, str(Path(__file__).resolve().parent / 'lib'))
from native_benchmark import require
from package_commands import Commands, runtime_environment
from package_integration import compile_probe
from package_matrix import load, check_linkage, supported_configuration_results
from package_process import Consumer
from package_traffic import traffic, response


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec); spec.loader.exec_module(value)
    return value


def websocket(executable, loop, directory, env, codec):
    rows = []
    with Consumer([str(executable), loop], directory, env, directory / 'websocket-process.json') as child:
        port = child.port()
        def counts():
            child.process.stdin.write(b'stats\n'); child.process.stdin.flush()
            observed = [int(n) for n in child.line().split()]
            require(len(observed) == 8, 'missing WS receive counters')
            return observed
        def wait(predicate):
            deadline = time.monotonic() + 3
            while True:
                observed = counts()
                if predicate(observed): return observed
                require(time.monotonic() < deadline, 'WS cancellation/backpressure deadline')
                time.sleep(.01)
        def open_peer(path='/echo'):
            peer = codec.Peer(port)
            peer.socket.sendall(codec.request(path))
            head = peer.head()
            require(head.startswith(b'HTTP/1.1 101 '), 'missing public upgrade')
            accept = base64.b64encode(hashlib.sha1(b'dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
            require(b'Sec-WebSocket-Accept: ' + accept in head and b'Sec-WebSocket-Extensions:' not in head, 'wrong WS handshake')
            return peer
        peer = open_peer()
        try:
            rows.append(dict(case='upgrade', port=port, status=101))
            text, binary = b'public text', b'\x00\xffbinary'
            for kind, value in ((1, text), (2, binary)):
                peer.socket.sendall(codec.frame(kind, value)); require(peer.receive() == (kind, value), 'WS echo bytes')
            first = bytearray(codec.frame(1, b'fragment ')); first[0] &= 127
            peer.socket.sendall(first + codec.frame(9, b'ping') + codec.frame(0, b'\xe2\x82\xac done'))
            replies = [peer.receive(), peer.receive()]
            require((10, b'ping') in replies and (1, b'fragment \xe2\x82\xac done') in replies, 'fragmented/control exchange')
            rows.append(dict(case='text_binary_fragmented', text=text.decode(), binary_hex=binary.hex(), fragmented_hex=b'fragment \xe2\x82\xac done'.hex(), pong='ping'))
            peer.socket.sendall(codec.frame(8, b'\x03\xe8')); require(peer.receive() == (8, b'\x03\xe8'), 'WS clean close')
        finally: peer.close()
        wait(lambda c: c[0] == 1)
        refused = codec.Peer(port)
        try:
            refused.socket.sendall(codec.request('/echo', **{'Sec-WebSocket-Version': '12'}))
            require(refused.head().startswith(b'HTTP/1.1 426 '), 'invalid WS version accepted')
        finally: refused.close()
        rows.append(dict(case='refusal', status=426))
        peer = open_peer('/cancel')
        try:
            wait(lambda c: c[3] == 1 and c[4:7] == [0, 0, 0])
        finally: peer.close()
        observed = wait(lambda c: c[0] == 2 and c[4:7] == [1, 1, 1])
        require(observed[7] == 6, 'pending receive did not terminate with connection_closed')
        rows.append(dict(case='close_cancel', clean_close_code=1000, close_callbacks=observed[0],
                         receive_entered=observed[3], receive_completed=observed[4], receive_retired=observed[5],
                         cancelled_receive=observed[6], receive_outcome='connection_closed'))
        peer = codec.Peer(port)
        try:
            peer.socket.setsockopt(__import__('socket').SOL_SOCKET, __import__('socket').SO_RCVBUF, 4096)
            peer.socket.sendall(codec.request('/pressure'))
            require(peer.head().startswith(b'HTTP/1.1 101 '), 'pressure upgrade absent')
            before = wait(lambda c: c[1] > 0)
            time.sleep(.2)
            require(counts()[2] == 0, 'writable cancelled before peer close')
        finally: peer.close()
        after = wait(lambda c: c[0] == 3 and c[2] == 1)
        rows.append(dict(case='backpressure_cancel', blocked=after[1], cancelled=after[2], close_callbacks=after[0], output_limit=65546))
        child.stop()
    return rows


def supported(executable, cell, directory, env, fixtures, source, commands):
    rows, outputs = [], []
    for protocol in ('tls', 'h2', 'h3'):
        argv = [str(executable), cell['tls'], cell['loop'], str(fixtures), protocol]
        if cell['tls'] == 'no':
            outputs.append(commands(argv, directory, env))
            rows.append(dict(case=protocol, outcome='not_supported', bound_port=0))
            continue
        process = directory / (protocol + '-supported-process.json')
        with Consumer(argv, directory, env, process) as child:
            port = child.port()
            if protocol == 'tls':
                import socket, ssl
                context = ssl.create_default_context(cafile=str(fixtures / 'root.pem')); context.set_alpn_protocols(['http/1.1'])
                with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3), server_hostname='a.example') as peer:
                    peer.sendall(b'GET /installed HTTP/1.1\r\nHost: a.example\r\nConnection: close\r\n\r\n')
                    body, pending, head = response(peer)
                    require(body == b'installed-v3-ok\n' and not pending and peer.selected_alpn_protocol() == 'http/1.1', 'supported TLS exchange')
            elif protocol == 'h2':
                from public_http2_h2 import Client
                peer = Client(port, fixtures)
                try:
                    result = peer.get('/installed'); body = result['body']
                    require(body == b'installed-v3-ok\n', 'supported H2 exchange')
                finally: peer.close()
            else:
                client = module(source / 'test/installed/http3_aioquic.py', 'supported_h3')
                async def get():
                    from aioquic.asyncio import connect
                    from aioquic.quic.configuration import QuicConfiguration
                    config = QuicConfiguration(is_client=True, alpn_protocols=['h3'], server_name='a.example')
                    config.load_verify_locations(str(fixtures / 'root.pem'))
                    async with connect('127.0.0.1', port, configuration=config, create_protocol=client.Client) as peer:
                        state = peer.request('GET', '/installed')
                        result = await asyncio.wait_for(state['done'], 3)
                        require(dict(result['sections'][0])[b':status'] == b'200', 'supported H3 status')
                        return bytes(result['body'])
                body = asyncio.run(get()); require(body == b'installed-v3-ok\n', 'supported H3 exchange')
            rows.append(dict(case=protocol, outcome='ok', bound_port=port, body=body.decode(), alpn='http/1.1' if protocol == 'tls' else protocol))
            child.stop()
        outputs.append(json.loads(process.read_text())['stderr'])
    supported_configuration_results('\n'.join(outputs), cell['tls'])
    return rows


def run(args):
    args.output = args.output.resolve(); args.output.mkdir(parents=True, exist_ok=False)
    source, prefix, build, provider = [p.resolve() for p in (args.source, args.prefix, args.build, args.provider_prefix)]
    commands = Commands(args.output); env = runtime_environment(os.environ, prefix, provider, [])
    env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'))
    receipt = dict(version=1, predicate='installed-public-http1', status='failed', run_id=args.run_id,
                   tls=args.tls, source=str(source), prefix=str(prefix), build=str(build), cells=[])
    try:
        audit = module(source / 'scripts/audit-v3-installed-package.py', 'audit_h1')
        require(Path(commands(['pkg-config', '--variable=pcfiledir', 'libhttpserver'], args.output, env).strip()).resolve() == prefix / 'lib/pkgconfig', 'wrong pkg-config prefix')
        cflags = shlex.split(commands(['pkg-config', '--cflags', 'libhttpserver'], args.output, env))
        require(not audit.consumer_flag_violations(cflags, (source, build)), 'private compile flags')
        directory = Path(tempfile.mkdtemp(prefix='task199-installed-h1-')); receipt['consumer_directory'] = str(directory)
        units = ['test/installed/package_consumer.cpp', 'test/installed/websocket_consumer.cpp', 'examples/host_poll.hpp', 'test/integ/native_websocket_client.py']
        for name in units: shutil.copyfile(source / name, directory / Path(name).name)
        fixtures = directory / 'fixtures'; shutil.copytree(source / 'test/data/tls_credentials', fixtures)
        receipt['source_hashes'] = {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in units}
        receipt['library_hashes'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (prefix / 'lib').glob('libhttpserver.*') if p.is_file()}
        codec = module(directory / 'native_websocket_client.py', 'installed_wire_codec')
        for linkage in ('shared', 'static'):
            libraries = shlex.split(commands(['pkg-config', '--libs'] + (['--static'] if linkage == 'static' else []) + ['libhttpserver'], args.output, env))
            if linkage == 'static': libraries = [str(prefix / 'lib/libhttpserver.a') if p == '-lhttpserver' else p for p in libraries]
            libraries += ['-pthread', '-Wl,-rpath,' + str(prefix / 'lib'), '-Wl,-rpath,' + str(provider / 'lib')]
            executable, ws = directory / linkage, directory / (linkage + '-websocket')
            provenance = compile_probe(args.compiler, [directory / 'package_consumer.cpp'], cflags, libraries, executable, commands, env, source, build, prefix, False)
            ws_provenance = compile_probe(args.compiler, [directory / 'websocket_consumer.cpp'], cflags, libraries, ws, commands, env, source, build, prefix, False)
            graph = audit.dependency_graph([str(executable)]); check_linkage(graph, linkage, prefix)
            require(not audit.graph_violations(graph, 'strict', args.tls, 'aggregate', audit.load_policy(), provider), 'dependency graph')
            check_linkage(audit.dependency_graph([str(ws)]), linkage, prefix)
            for loop in ('managed', 'external'):
                out = args.output / (linkage + '-' + loop); out.mkdir()
                cell = dict(linkage=linkage, loop=loop, tls=args.tls, compile=provenance, websocket_compile=ws_provenance, dependencies=graph)
                cell.update(traffic(executable, cell.copy(), out, env, load()))
                cell['websocket'] = websocket(ws, loop, out, env, codec)
                cell['supported_configurations'] = supported(executable, cell, out, env, fixtures, source, commands)
                cell['status'] = 'passed'; receipt['cells'].append(cell)
                print('PASS installed H1/WS', args.tls, linkage, loop, flush=True)
        receipt['status'] = 'passed'
    except BaseException as error:
        receipt.update(status='failed', error=str(error)); raise
    finally: (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('source', 'build', 'prefix', 'provider-prefix', 'output'): parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--tls', choices=('yes', 'no'), required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--run-id', required=True)
    run(parser.parse_args())


if __name__ == '__main__': main()
