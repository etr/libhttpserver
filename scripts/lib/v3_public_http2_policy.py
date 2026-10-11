"""TLS, policy and lifecycle observations over installed native listeners."""
import socket
import ssl
import time
from v3_public_http2_h2 import Client, require, h1_parity, masked


def policy(port, fixtures, mode, command):
    rows = []
    if mode == 'mtls':
        c = Client(port, fixtures, certificate=True)
        try:
            metadata = dict(c.get('/meta', 204)['headers'])
            require(metadata['x-client-verified'] == 'yes', 'H2 mutual TLS metadata')
        finally:
            c.close()
        refused = False
        try:
            c = Client(port, fixtures)
            try:
                c.get('/installed')
            finally:
                c.close()
        except (ssl.SSLError, ConnectionError, EOFError):
            refused = True
        require(refused, 'H2 mTLS admitted an anonymous client')
        return [dict(case='h2_mtls_metadata_and_refusal', status='passed')]
    if mode == 'h1-bound':
        require(command('rotate') == 'ROTATE 1', 'H1 binding allowed H2 live publication')
        h1_parity(port, fixtures)
        return [dict(case='h2_live_policy_rejects_disabled', status='passed')]
    if mode == 'mixed':
        ports = [int(n) for n in command('ports').split()]
        require(len(set(ports)) == 3 and all(ports), 'mixed listener inventory')
        for target in ports[:2]:
            c = Client(target, fixtures)
            try:
                c.get('/installed')
            finally:
                c.close()
        with socket.create_connection(('127.0.0.1', ports[2]), timeout=3) as plain:
            plain.sendall(b'GET /installed HTTP/1.1\r\nHost: a.example\r\nConnection: close\r\n\r\n')
            wire = b''
            while part := plain.recv(4096):
                wire += part
            require(b'200 OK' in wire and b'installed-v3-h2-ok\n' in wire, 'mixed plaintext route parity')
        return [dict(case='mixed_h1_h2_listeners', status='passed')]
    c = Client(port, fixtures)
    try:
        c.get('/installed')
        if mode == 'h2-only':
            require(c.socket.selected_alpn_protocol() == 'h2', 'H2-only selection')
            rows.append(dict(case='h2_only_alpn', status='passed'))
        elif mode == 'policy':
            before = int(command('stats').split()[1])
            require(command('deny') == 'POLICY deny', 'policy command')
            refused = c.wait(c.request('/installed'))
            require(refused.get('reset') is not None or dict(refused['headers']).get(':status') == '403', 'live H2 request policy')
            denied = False
            try:
                other = Client(port, fixtures)
                other.close()
            except (ssl.SSLError, ConnectionError, EOFError):
                denied = True
            require(denied, 'denied peer negotiated TLS')
            require(command('allow') == 'POLICY allow', 'policy restoration')
            c.get('/installed')
            require(int(command('stats').split()[1]) >= before + 1, 'restored request hook absent')
            rows.append(dict(case='h2_peer_policy_live_and_accept', status='passed'))
            require(command('rotate') == 'ROTATE 0', 'H2 compatible live rotation')
            c.get('/installed')
            other = Client(port, fixtures)
            try:
                other.get('/installed')
            finally:
                other.close()
            rows.append(dict(case='h2_live_policy_publication', status='passed'))
        elif mode == 'cipher':
            before = [int(n) for n in command('stats').split()]
            context = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
            context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
            context.set_ciphers('ECDHE-RSA-AES128-SHA256')
            context.set_alpn_protocols(['h2'])
            try:
                with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3), server_hostname='a.example') as peer:
                    peer.sendall(b'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n'+b'\0\0\0\x04\0\0\0\0\0')
                    require(peer.recv(16384) == b'', 'H2 accepted forbidden TLS1.2 CBC')
            except (ssl.SSLError, ConnectionError):
                pass
            after = [int(n) for n in command('stats').split()]
            require(before[1:3] == after[1:3], 'forbidden cipher reached application hooks')
            c.get('/installed')
            rows.append(dict(case='h2_forbidden_cipher_before_hooks', status='passed'))
        elif mode == 'stop':
            c.request('/handler-stop')
            require(command('ack') == 'HANDLER_STOP_RETURNED', 'handler stop acknowledgement')
            rows.append(dict(case='handler_stop_ack', status='passed'))
    finally:
        c.close()
    return rows


def websocket(port, fixtures, command):
    c = Client(port, fixtures)
    try:
        c.get('/installed')
        bad = c.connection.get_next_available_stream_id()
        c.connection.send_headers(bad, [(':method', 'CONNECT'), (':protocol', 'websocket'), (':scheme', 'https'),
                                       (':authority', 'a.example'), (':path', '/ws'), ('sec-websocket-version', '12')])
        c.flush()
        result = c.wait(bad)
        require(dict(result['headers']).get(':status') == '426', 'invalid RFC8441 negotiation was accepted')
        c.get('/installed')
        import h2.settings
        prior = c.settings_acks
        c.connection.update_settings({h2.settings.SettingCodes.INITIAL_WINDOW_SIZE: 0}); c.flush()
        c.until(lambda: c.settings_acks > prior)
        stream = c.connection.get_next_available_stream_id()
        c.connection.send_headers(stream, [(':method', 'CONNECT'), (':protocol', 'websocket'), (':scheme', 'https'),
                                         (':authority', 'a.example'), (':path', '/ws'), ('sec-websocket-version', '13')])
        c.flush(); c.until(lambda: bool(c.responses.get(stream, {}).get('headers')))
        c.withheld.add(stream)
        for index in range(64):
            c.send(stream, masked(2, bytes([index % 256]) * 120), False)
        healthy = c.request('/installed')
        c.connection.increment_flow_control_window(64, healthy); c.flush()
        require(c.wait(healthy)['body'] == b'installed-v3-h2-ok\n', 'WS pressure blocked sibling')
        c.ping()
        require(int(command('stats').split()[6]) <= 4 * 1024 * 1024, 'WS output grew beyond bounded native budget')
        before = int(command('stats').split()[3])
        c.connection.reset_stream(stream); c.flush(); c.ping()
        deadline = time.monotonic() + 3
        while int(command('stats').split()[3]) == before:
            require(time.monotonic() < deadline, 'WS reset did not close handler')
        require(int(command('stats').split()[3]) == before + 1, 'WS reset close count')
        closes = int(command('stats').split()[3])
        timed = c.connection.get_next_available_stream_id()
        c.connection.send_headers(timed, [(':method', 'CONNECT'), (':protocol', 'websocket'), (':scheme', 'https'),
                                        (':authority', 'a.example'), (':path', '/ws'), ('sec-websocket-version', '13')])
        c.flush(); c.until(lambda: bool(c.responses.get(timed, {}).get('headers')))
        require(dict(c.responses[timed]['headers']).get(':status') == '200', 'deadline WS acceptance')
        c.withheld.add(timed)
        for index in range(64):
            c.send(timed, masked(2, b'd' * 120), False)
        require(c.wait(timed).get('reset') == 8, 'stalled WS write deadline did not cancel its stream')
        deadline = time.monotonic() + 3
        while int(command('stats').split()[3]) == closes:
            require(time.monotonic() < deadline, 'WS deadline did not close handler')
        require(int(command('stats').split()[3]) == closes + 1, 'WS deadline close count')
        c.connection.update_settings({h2.settings.SettingCodes.INITIAL_WINDOW_SIZE: 65535}); c.flush()
        c.get('/installed')
        return [dict(case='rfc8441_invalid_negotiation', status='passed'),
                dict(case='rfc8441_bounded_backpressure_reset', status='passed', frames=64, ping_ack=True, close_count=1),
                dict(case='rfc8441_backpressure', status='passed', output_limit=4194304),
                dict(case='rfc8441_reset_deadline', status='passed', close_count=1)]

    finally:
        c.close()
