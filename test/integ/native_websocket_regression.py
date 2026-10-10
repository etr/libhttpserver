#!/usr/bin/env python3
"""Bounded public wire regressions for TASK-189; shares the unchanged raw codec."""
import argparse
import base64
import hashlib
import queue
import os
import socket
import time
import native_websocket_client as client


def handshake(peer, path):
    peer.socket.sendall(client.request(path))
    head = peer.head()
    rows = head.split(b'\r\n')
    assert rows[0] == b'HTTP/1.1 101 Switching Protocols', head
    fields = dict(row.split(b': ', 1) for row in rows[1:])
    accept = base64.b64encode(hashlib.sha1(
        b'dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
    assert fields[b'Upgrade'] == b'websocket'
    assert fields[b'Connection'] == b'Upgrade'
    assert fields[b'Sec-WebSocket-Accept'] == accept
    assert not any(name in fields for name in
                   (b'Content-Length', b'Transfer-Encoding', b'Sec-WebSocket-Extensions'))


def rejected(binary):
    process, events, port = client.start(binary)
    peer = None
    try:
        peer = client.Peer(port)
        # Even a rejected application send must allow the complete 101 to flush.
        handshake(peer, '/reject-send')
        try:
            event = client.wait_event(events, 'SEND REJECTED ', deadline=2)
        except queue.Empty as error:
            raise AssertionError('permanent send rejection must terminate without writable retry') from error
        assert event == 'SEND REJECTED outgoing message limit exceeded', event
        assert peer.receive() == (8, b'\x03\xe8rejected')
        peer.socket.sendall(client.frame(8, b'\x03\xe8rejected'))
        client.wait_event(events, 'CLOSE 1 ')
    finally:
        if peer is not None:
            peer.close()
        client.stop(process)
    print('Permanent send rejection / complete 101 / clean close PASS', flush=True)


def flood(binary):
    process, events, port = client.start(binary)
    try:
        # Original 64 ordered MiB workload, original deadlines and stalled-reader predicate.
        peer = client.Peer(port)
        try:
            peer.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            handshake(peer, '/flood')
            stable = client.wait_event(events, 'BLOCKED ').split()[1]
            until = time.monotonic() + 0.25
            while time.monotonic() < until:
                try:
                    line = events.get(timeout=0.05)
                    if line.startswith('BLOCKED '):
                        stable = line.split()[1]
                    if stable and line == 'RESUMED ' + stable:
                        stable = None
                except queue.Empty:
                    if stable:
                        break
            assert stable, 'writable did not remain outstanding with reads stalled'
            peer.socket.sendall(client.frame(9, b'control'))
            peer.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1048576)
            pong_at = None
            for index in range(64):
                opcode, payload = peer.receive()
                if opcode == 10:
                    assert payload == b'control' and pong_at is None
                    pong_at = index
                    opcode, payload = peer.receive()
                assert opcode == 2 and len(payload) == 1048576
                assert payload == bytes([index]) * 1048576, index
            if pong_at is None:
                assert peer.receive() == (10, b'control')
                pong_at = 64
            assert pong_at <= 3, ('reserved control capacity stalled behind queued data', pong_at)
            client.wait_event(events, 'SENT 64')
            peer.socket.sendall(client.frame(8, b'\x03\xe8'))
            assert peer.receive() == (8, b'\x03\xe8')
            client.wait_event(events, 'CLOSE 1 ')
        finally:
            peer.close()
    finally:
        client.stop(process)
    print('Fresh full-size flood / reserved ping control PASS', flush=True)


def close_stalled(binary):
    process, events, port = client.start(binary)
    peer = None
    try:
        peer = client.Peer(port)
        peer.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        handshake(peer, '/flood')
        client.wait_event(events, 'BLOCKED ')
        peer.socket.sendall(client.frame(8, b'\x03\xe8'))
        peer.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1048576)
        for index in range(65):
            opcode, payload = peer.receive()
            if opcode == 8:
                assert payload == b'\x03\xe8'
                break
            assert opcode == 2 and payload == bytes([index]) * 1048576, index
        else:
            raise AssertionError('Close did not progress after draining a pinned data frame')
        client.wait_event(events, 'CLOSE 1 ')
    finally:
        if peer is not None:
            peer.close()
        client.stop(process)
    print('Close while writable stalled / complete pinned frame / one close notification PASS', flush=True)


def handler_stop(binary):
    process, events, port = client.start(binary)
    peer = None
    try:
        peer = client.Peer(port)
        peer.socket.sendall(b'GET /stop-handler HTTP/1.1\r\nHost: localhost\r\n\r\n')
        assert client.wait_event(events, 'STOP HANDLER ', deadline=2) == 'STOP HANDLER returned'
        assert peer.socket.recv(1) == b''
    finally:
        if peer is not None:
            peer.close()
        client.stop(process)
    print('Handler-safe request_stop / joined cleanup PASS', flush=True)


def protocol(binary):
    process, events, port = client.start(binary)
    peer = None
    try:
        peer = client.Peer(port)
        handshake(peer, '/echo')
        first = bytearray(client.frame(1, b'frag'))
        first[0] &= 0x7f
        peer.socket.sendall(first + client.frame(9, b'interleave') + client.frame(0, b'ment'))
        replies = [peer.receive(), peer.receive()]
        assert (10, b'interleave') in replies and (1, b'fragment') in replies
        exact = b'x' * 1048576
        peer.socket.sendall(client.frame(2, exact))
        assert peer.receive() == (2, exact), 'exact message limit was not preserved'
        try:
            peer.socket.sendall(client.frame(2, exact + b'x'))
            assert peer.socket.recv(1) == b'', 'excess input message remained admissible'
        except (ConnectionResetError, BrokenPipeError):
            pass  # A terminal protocol rejection may reset unread peer bytes.
        client.wait_event(events, 'CLOSE 0 ')
    finally:
        if peer is not None:
            peer.close()
        client.stop(process)
    process, events, port = client.start(binary)
    peer = None
    try:
        peer = client.Peer(port)
        handshake(peer, '/echo')
        peer.close()  # receive() is parked; peer cancellation must retire it.
        client.wait_event(events, 'CLOSE 0 ')
    finally:
        if peer is not None:
            peer.close()
        client.stop(process)
    print('Fragmentation / interleaved control / exact-excess input / parked receive cancellation PASS', flush=True)


def admission(binary):
    old = os.environ.get('LHS_WS_LOW_BUDGET')
    os.environ['LHS_WS_LOW_BUDGET'] = '1'
    try:
        process, events, port = client.start(binary)
    finally:
        if old is None:
            del os.environ['LHS_WS_LOW_BUDGET']
        else:
            os.environ['LHS_WS_LOW_BUDGET'] = old
    peer = None
    try:
        peer = client.Peer(port)
        peer.socket.sendall(client.request())
        assert peer.head().startswith(b'HTTP/1.1 400 ')
        assert client.wait_event(events, 'REFUSED 400 ') == 'REFUSED 400 WebSocket queue storage exhausted'
        assert peer.socket.recv(1) == b''
    finally:
        if peer is not None:
            peer.close()
        client.stop(process)
    print('Low shared budget rejects admission / no session or stranded wait PASS', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('fixture')
    parser.add_argument('--case', choices=('rejected', 'flood', 'protocol', 'admission', 'close', 'stop'))
    args = parser.parse_args()
    if args.case in (None, 'rejected'):
        rejected(args.fixture)
    if args.case in (None, 'flood'):
        flood(args.fixture)
    if args.case in (None, 'protocol'):
        protocol(args.fixture)
    if args.case in (None, 'admission'):
        admission(args.fixture)
    if args.case in (None, 'close'):
        close_stalled(args.fixture)
    if args.case in (None, 'stop'):
        handler_stop(args.fixture)


if __name__ == '__main__':
    main()
