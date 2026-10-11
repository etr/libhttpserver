"""Independent hyper-h2 wire observations against an installed public server."""
import hashlib
import http.client
import importlib.metadata
import socket
import ssl
import time
from pathlib import Path
import h2.config
import h2.connection
import h2.events
import h2.settings


def require(value, message):
    if not value:
        raise RuntimeError(message)


class Client:
    def __init__(self, port, fixtures, *, certificate=False, acknowledge_settings=True):
        require(importlib.metadata.version('h2') == '4.4.1', 'Python h2 must be 4.4.1')
        self.context = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
        self.context.set_alpn_protocols(['h2'])
        if certificate:
            self.context.load_cert_chain(str(fixtures / 'client-valid-chain.pem'), str(fixtures / 'client-valid-key.pem'))
        self.socket = self.context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=4), server_hostname='a.example')
        require(self.socket.selected_alpn_protocol() == 'h2', 'independent Python connection did not negotiate h2')
        self.connection = h2.connection.H2Connection(h2.config.H2Configuration(header_encoding='utf-8'))
        self.connection.initiate_connection()
        self.responses, self.pings, self.goaway, self.raw_resets, self.windows = {}, [], [], {}, []
        self.withheld = set()
        self.raw_only = set()
        self.settings_acks = 0
        self.acknowledge_settings = acknowledge_settings
        self.flush()

    def close(self):
        self.socket.close()

    def flush(self):
        data = self.connection.data_to_send()
        if not self.acknowledge_settings:
            retained = bytearray()
            while data:
                size = int.from_bytes(data[:3], 'big') + 9
                if data[3:5] != b'\x04\x01':
                    retained.extend(data[:size])
                data = data[size:]
            data = retained
        if data:
            self.socket.sendall(data)

    def read_exact(self, count):
        data = bytearray()
        while len(data) < count:
            chunk = self.socket.recv(count - len(data))
            if not chunk:
                raise EOFError('public H2 transport ended')
            data.extend(chunk)
        return bytes(data)

    def step(self):
        head = self.read_exact(9)
        payload = self.read_exact(int.from_bytes(head[:3], 'big'))
        stream = int.from_bytes(head[5:9], 'big') & 0x7fffffff
        if head[3] == 7:
            self.goaway.append({'last': int.from_bytes(payload[:4], 'big') & 0x7fffffff, 'code': int.from_bytes(payload[4:8], 'big')})
            return
        if head[3] == 3:
            self.raw_resets[stream] = int.from_bytes(payload[:4], 'big')
            if stream in self.raw_only:
                return
        for event in self.connection.receive_data(head + payload):
            if isinstance(event, h2.events.SettingsAcknowledged):
                self.settings_acks += 1
            if isinstance(event, h2.events.PingAckReceived):
                self.pings.append(event.ping_data)
            if isinstance(event, h2.events.WindowUpdated):
                self.windows.append((event.stream_id, event.delta))
            if not hasattr(event, 'stream_id') or not event.stream_id:
                continue
            result = self.responses.setdefault(event.stream_id, {'headers': [], 'trailers': [], 'body': b'', 'ended': False})
            if isinstance(event, h2.events.ResponseReceived):
                result['headers'] = event.headers
            if isinstance(event, h2.events.InformationalResponseReceived):
                result.setdefault('informational',[]).append(event.headers)
            if isinstance(event, h2.events.TrailersReceived):
                result['trailers'] = event.headers
            if isinstance(event, h2.events.DataReceived):
                result['body'] += event.data
                if event.stream_id not in self.withheld:
                    self.connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
            if isinstance(event, h2.events.StreamEnded):
                result['ended'] = True
            if isinstance(event, h2.events.StreamReset):
                result['reset'] = int(event.error_code)
                result['ended'] = True
        self.flush()

    def until(self, predicate, timeout=5):
        deadline = time.monotonic() + timeout
        while not predicate():
            require(time.monotonic() < deadline, 'public Python H2 observation deadline')
            self.socket.settimeout(max(.01, deadline - time.monotonic()))
            self.step()

    def request(self, path, method='GET', headers=(), ended=True, stream=None):
        stream = self.connection.get_next_available_stream_id() if stream is None else stream
        self.connection.send_headers(stream, [(':method', method), (':scheme', 'https'), (':authority', 'a.example'), (':path', path)] + list(headers), end_stream=ended)
        self.flush()
        return stream

    def raw_opening(self, path):
        stream = self.connection.get_next_available_stream_id()
        self.raw_only.add(stream)
        block = self.connection.encoder.encode([(':method', 'GET'), (':scheme', 'https'), (':authority', 'a.example'), (':path', path)])
        self.socket.sendall(len(block).to_bytes(3, 'big') + b'\x01\x05' + stream.to_bytes(4, 'big') + block)
        return stream

    def send(self, stream, data, ended=True):
        while data:
            self.until(lambda: self.connection.local_flow_control_window(stream) > 0)
            count = min(len(data), self.connection.local_flow_control_window(stream), self.connection.max_outbound_frame_size)
            self.connection.send_data(stream, data[:count], end_stream=ended and count == len(data))
            data = data[count:]
            self.flush()

    def wait(self, stream):
        self.until(lambda: self.responses.get(stream, {}).get('ended', False))
        return self.responses[stream]

    def get(self, path, status=200, headers=()):
        result = self.wait(self.request(path, headers=headers))
        require(dict(result['headers']).get(':status') == str(status), 'public H2 status differs: ' + path + repr(result))
        return result

    def ping(self):
        token = len(self.pings).to_bytes(8, 'big')
        self.connection.ping(token)
        self.flush()
        self.until(lambda: token in self.pings)

    def counts(self):
        return [int(n) for n in self.get('/counts')['body'].split()]


def h1_parity(port, fixtures):
    context = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
    context.set_alpn_protocols(['http/1.1'])
    with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=4), server_hostname='a.example') as peer:
        require(peer.selected_alpn_protocol() == 'http/1.1', 'H1 parity ALPN')
        peer.sendall(b'GET /installed HTTP/1.1\r\nHost: a.example\r\nConnection: close\r\n\r\n')
        response = http.client.HTTPResponse(peer)
        response.begin()
        require(response.status == 200 and response.read() == b'installed-v3-h2-ok\n', 'shared route H1 parity')


def masked(opcode, payload):
    require(len(payload) < 126, 'test message bound')
    key = b'\x01\x02\x03\x04'
    return bytes((128 | opcode, 128 | len(payload))) + key + bytes(value ^ key[index % 4] for index, value in enumerate(payload))


def basic(port, fixtures, command=None):
    rows = []
    def passed(case, **observed):
        rows.append(dict(case=case, status='passed', **observed))
    before = [int(n) for n in command('stats').split()] if command else None
    h1_parity(port, fixtures)
    c = Client(port, fixtures)
    try:
        require(c.get('/installed')['body'] == b'installed-v3-h2-ok\n', 'installed route body')
        passed('alpn_route_parity', alpn='h2', h1_alpn='http/1.1')
        if command:
            deadline = time.monotonic() + 3
            while True:
                after = [int(n) for n in command('stats').split()]
                if after[2] == before[2] + 2:
                    break
                require(time.monotonic() < deadline, 'H1/H2 request completion hooks did not settle')
            require(after[1] == before[1] + 2, 'H1/H2 request-received hook count')
        passed('hook_policy_parity', requests=2, completions=2)
        stream = c.request('/body', 'POST', [('x-order', 'one'), ('x-order', 'two')], False)
        c.until(lambda: c.connection.local_flow_control_window(stream) > 0)
        passed('header_admit_incremental', admitted_before_data=True)
        c.send(stream, b'first-', False)
        healthy = c.get('/installed')
        c.ping()
        require(healthy['body'] == b'installed-v3-h2-ok\n' and not c.responses.get(stream, {}).get('ended'), 'slow upload blocked sibling')
        passed('slow_upload_healthy_stream')
        c.send(stream, b'second', False)
        c.connection.send_headers(stream, [('x-trailer', 'end-one'), ('x-trailer', 'end-two')], end_stream=True)
        c.flush()
        result = c.wait(stream)
        require(result['body'] == b'first-second', 'request/response exact body')
        require([item for item in result['headers'] if item[0] == 'x-order'] == [('x-order', 'one'), ('x-order', 'two')], 'ordered repeated headers')
        require(result['trailers'] == [('x-trailer', 'end-one'), ('x-trailer', 'end-two')], 'ordered repeated trailers')
        for case in ('ordered_headers_trailers', 'request_body_trailers', 'response_body_trailers'):
            passed(case, body_sha256=hashlib.sha256(result['body']).hexdigest())
        for body, status, case in ((b'12345678', 200, 'route_sync_at_cap'), (b'123456789', 413, 'route_sync_over_cap')):
            stream = c.request('/sync', 'POST', ended=False)
            c.send(stream, body)
            result = c.wait(stream)
            require(dict(result['headers']).get(':status') == str(status), case)
            require(result['body'] == (body if status == 200 else b''), 'sync body')
            passed(case)
        rejected = c.request('/reject', 'POST', ended=False)
        require(dict(c.wait(rejected)['headers']).get(':status') == '403', 'header rejection')
        require(not any(stream == rejected for stream, _ in c.windows), 'rejected body admitted')
        passed('header_reject_before_body')
        for held_path, release_path, case in (('/suspend', '/resume', 'suspended_stream_control_progress'), ('/running', '/release-running', 'running_handler_does_not_own_wire_domain')):
            before = c.counts()[0]
            held = c.request(held_path)
            deadline = time.monotonic() + 3
            while c.counts()[0] == before:
                require(time.monotonic() < deadline, 'held handler did not enter')
            require(not c.responses.get(held, {}).get('ended'), 'held handler escaped barrier')
            require(c.get('/installed')['body'] == b'installed-v3-h2-ok\n', 'held handler blocked sibling')
            c.ping()
            c.get(release_path, 204)
            require(dict(c.wait(held)['headers']).get(':status') == '204', 'held completion')
            passed(case, held_entered=True, ping_ack=True)
            if held_path == '/suspend':
                passed('header_suspend_resume')
        before = c.counts()[0]
        held = c.request('/suspend')
        deadline = time.monotonic() + 3
        while c.counts()[0] == before:
            require(time.monotonic() < deadline, 'reset target never entered')
        c.connection.reset_stream(held)
        c.flush()
        c.get('/installed')
        deadline = time.monotonic() + 3
        while True:
            resumed = c.get('/resume', 204)
            if dict(resumed['headers']).get('x-signal') == '2':
                break
            require(time.monotonic() < deadline, 'late signal was not cancelled')
        for case in ('suspension_reset_late_signal', 'reset_stream_local_cancel', 'late_signal_after_retirement'):
            passed(case, late_signal='cancelled')
        require(c.get('/source')['body'] == b'source-data', 'producer data/end')
        failed = c.wait(c.request('/source-error'))
        require('reset' in failed, 'producer failure did not reset')
        require(c.get('/installed')['body'] == b'installed-v3-h2-ok\n', 'source failure affected sibling')
        passed('streaming_source_end_error')
        overlays = [c.request('/definition', headers=[('x-overlay', value)]) for value in ('one', 'two')]
        for stream, value in zip(overlays, ('one', 'two')):
            result = c.wait(stream)
            require(result['body'] == b'reused-definition' and dict(result['headers'])['x-overlay'] == value, 'definition overlay isolation')
            require(result['trailers'] == [('x-end', 'definition')], 'definition trailer')
        passed('response_definition_overlay_isolation')
        metadata = dict(c.get('/meta', 204)['headers'])
        require(metadata['x-protocol'] == 'h2' and metadata['x-peer'] == '127.0.0.1' and metadata['x-client-verified'] == 'no', 'native TLS peer metadata')
        passed('tls_peer_metadata')
        for path, status in (('/missing', 404), ('/throw', 500)):
            c.get(path, status)
        c.get('/installed')
        passed('custom_errors_exception_containment')
        require(c.connection.remote_settings.enable_connect_protocol == 1, 'RFC8441 setting not advertised')
        stream = c.connection.get_next_available_stream_id()
        c.connection.send_headers(stream, [(':method', 'CONNECT'), (':protocol', 'websocket'), (':scheme', 'https'), (':authority', 'a.example'), (':path', '/ws'), ('sec-websocket-version', '13')])
        c.flush()
        c.until(lambda: bool(c.responses.get(stream, {}).get('headers')))
        require(dict(c.responses[stream]['headers'])[':status'] == '200', 'extended CONNECT acceptance')
        c.send(stream, masked(1, b'hello'), False)
        require(c.get('/installed')['body'] == b'installed-v3-h2-ok\n', 'WS owns whole connection')
        c.ping()
        c.until(lambda: c.responses[stream]['body'] == b'\x81\x05hello')
        passed('rfc8441_echo_parallel', ping_ack=True)
        closes = c.counts()[3]
        c.send(stream, masked(8, b'\x03\xe8'), False)
        c.until(lambda: c.responses[stream].get('ended'))
        require('reset' not in c.responses[stream], 'clean WebSocket close was reset')
        c.connection.end_stream(stream)
        c.flush()
        deadline = time.monotonic() + 3
        while c.counts()[3] < closes + 1:
            require(time.monotonic() < deadline, 'WebSocket close callback absent')
        require(c.counts()[3] == closes + 1, 'WebSocket duplicate close callback')
        passed('rfc8441_close_once', close_count=1)
    finally:
        c.close()
    return rows


def timeouts(port, fixtures):
    rows = []
    c = Client(port, fixtures)
    try:
        c.get('/installed')
        before = c.counts()[0]
        held = c.request('/suspend')
        deadline = time.monotonic() + 3
        while c.counts()[0] == before:
            require(time.monotonic() < deadline, 'timeout target did not enter')
        started = time.monotonic()
        c.ping()
        result = c.wait(held)
        require(result.get('reset') == 8 and time.monotonic() - started < 2, 'suspension deadline scope')
        require(c.get('/installed')['body'] == b'installed-v3-h2-ok\n', 'suspension deadline closed connection')
        rows.append(dict(case='suspension_timeout', status='passed', elapsed=time.monotonic() - started))
        held = c.request('/body', 'POST', ended=False)
        c.until(lambda: c.connection.local_flow_control_window(held) > 0)
        started = time.monotonic()
        c.ping()
        result = c.wait(held)
        require(result.get('reset') == 8 and time.monotonic() - started < 2, 'body idle deadline scope')
        c.get('/installed')
        rows.append(dict(case='body_idle_timeout_local', status='passed', elapsed=time.monotonic() - started))
        prior = c.settings_acks
        c.connection.update_settings({h2.settings.SettingCodes.INITIAL_WINDOW_SIZE: 0})
        c.flush()
        c.until(lambda: c.settings_acks > prior)
        held = c.request('/large')
        c.until(lambda: bool(c.responses.get(held, {}).get('headers')))
        healthy = c.request('/installed')
        c.connection.increment_flow_control_window(64, healthy)
        c.flush()
        require(c.wait(healthy)['body'] == b'installed-v3-h2-ok\n', 'zero-window stream blocked healthy response')
        c.ping()
        result = c.wait(held)
        require(result.get('reset') == 8, 'write idle failed to reset blocked stream')
        rows.append(dict(case='slow_response_healthy_stream', status='passed', ping_ack=True))
        rows.append(dict(case='write_idle_timeout_local', status='passed'))
    finally:
        c.close()
    return rows


def drain(port, fixtures, finish, command=None):
    c = Client(port, fixtures)
    try:
        c.get('/installed')
        before = c.counts()[0]
        held = c.request('/suspend')
        deadline = time.monotonic() + 3
        while c.counts()[0] == before:
            require(time.monotonic() < deadline, 'drain target did not enter')
        if not finish:
            ws = c.connection.get_next_available_stream_id()
            c.connection.send_headers(ws, [(':method', 'CONNECT'), (':protocol', 'websocket'), (':scheme', 'https'),
                                           (':authority', 'a.example'), (':path', '/ws'), ('sec-websocket-version', '13')])
            c.flush(); c.until(lambda: bool(c.responses.get(ws, {}).get('headers')))
            require(dict(c.responses[ws]['headers']).get(':status') == '200', 'drain WS acceptance')
            closes = c.counts()[3]
        headers = [('x-finish', 'yes')] if finish else []
        draining = c.request('/handler-drain', headers=headers)
        result = c.wait(draining)
        head = dict(result['headers'])
        require(head.get(':status') == '204' and head.get('x-begin') == '0' and head.get('x-wait') == '9', 'counted handler drain acknowledgement')
        if command:
            require(command('ack') == 'HANDLER_DRAIN_RETURNED', 'drain handler return acknowledgement')
        c.until(lambda: len(c.goaway) >= 2)
        require(c.goaway[0] == {'last': 0x7fffffff, 'code': 0} and c.goaway[-1] == {'last': draining, 'code': 0}, 'staged GOAWAY cutoff')
        rows = [dict(case='handler_drain_ack', status='passed'), dict(case='counted_handler_wait_rejected', status='passed', outcome='would_deadlock')]
        if finish:
            require(dict(c.wait(held)['headers']).get(':status') == '204', 'accepted stream did not finish')
            rows.append(dict(case='goaway_finishes_accepted', status='passed'))
        else:
            late = c.request('/installed')
            require(c.wait(late).get('reset') == 7, 'opening after GOAWAY cutoff was not refused')
            rows.append(dict(case='goaway_accept_cutoff', status='passed', cutoff=draining))
            started = time.monotonic()
            try:
                c.until(lambda: False, 3)
            except (EOFError, ConnectionError):
                pass
            require(time.monotonic() - started < 3, 'native drain deadline did not close idle held connection')
            rows.append(dict(case='drain_deadline_without_waiter', status='passed', elapsed=time.monotonic() - started))
            if command:
                deadline = time.monotonic() + 3
                while True:
                    counters = [int(n) for n in command('stats').split()]
                    if counters[4:6] == [0, 0] and counters[1] == counters[2]:
                        break
                    require(time.monotonic() < deadline, 'native drain did not retire all request/WS handlers')
                require(counters[3] == closes + 1, 'drain WS close callback count')
            rows.append(dict(case='drain_cancels_requests_ws', status='passed', close_count=1))
        return rows
    finally:
        c.close()


def flow_credit(port, fixtures):
    c = Client(port, fixtures)
    rows = []
    try:
        c.get('/installed')
        for padded, reset in ((False, False), (True, True)):
            before = c.counts()[0]
            held = c.request('/held-body', 'POST', ended=False)
            deadline = time.monotonic() + 3
            while c.counts()[0] == before:
                require(time.monotonic() < deadline, 'credit target did not enter')
            c.until(lambda: c.connection.local_flow_control_window(held) >= 16384)
            baseline = c.connection.outbound_flow_control_window
            payload = b'p' * (4096 if padded else 16384)
            c.connection.send_data(held, payload, pad_length=7 if padded else None)
            c.flush()
            c.ping()
            require(c.connection.outbound_flow_control_window == baseline - len(payload), 'credit returned before application consumption or padding counted twice')
            require(c.connection.local_flow_control_window(held) == 16384 - len(payload), 'unconsumed stream credit increased')
            if reset:
                c.connection.reset_stream(held)
                c.flush()
                c.ping()
                require(c.connection.outbound_flow_control_window == baseline, 'reset unread credit was lost or duplicated')
                rows += [dict(case='padded_data_credit_exact', status='passed', padding=8), dict(case='reset_unread_credit_once', status='passed', unread=len(payload))]
            else:
                c.get('/resume', 204)
                c.connection.end_stream(held)
                c.flush()
                require(dict(c.wait(held)['headers']).get('x-consumed') == str(len(payload)), 'body consumption byte count')
                c.ping()
                require(c.connection.outbound_flow_control_window == baseline, 'consumed credit not exact')
                rows.append(dict(case='consumed_credit_exact', status='passed', consumed=len(payload)))
            c.get('/installed')
    finally:
        c.close()
    return rows


def running_reset(port, fixtures):
    c = Client(port, fixtures)
    try:
        before = c.counts()
        held = c.request('/running')
        deadline = time.monotonic() + 3
        while c.counts()[0] == before[0]:
            require(time.monotonic() < deadline, 'running reset target did not enter')
        c.connection.reset_stream(held)
        c.flush()
        c.ping()
        require(c.counts()[5] == before[5], 'running frame destroyed at reset')
        require(c.get('/installed')['body'] == b'installed-v3-h2-ok\n', 'running reset blocks sibling')
        c.get('/release-running', 204)
        deadline = time.monotonic() + 3
        while c.counts()[5] == before[5]:
            require(time.monotonic() < deadline, 'running frame did not retire after resume returned')
        require(c.counts()[5] == before[5] + 1, 'running frame destroyed more than once')
        return [dict(case='reset_running_resume_retains_frame', status='passed', destroyed=1)]
    finally:
        c.close()


def budget_limits(port, fixtures, mode, command):
    rows = []
    c = Client(port, fixtures)
    try:
        c.get('/installed')
        if mode in ('connection-limit', 'timer-limit'):
            rejected = False
            try:
                other = Client(port, fixtures)
                try:
                    other.get('/installed')
                finally:
                    other.close()
            except (ConnectionError, EOFError, ssl.SSLError):
                rejected = True
            require(rejected, 'configured admission limit accepted simultaneous connection')
            require(c.get('/installed')['body'] == b'installed-v3-h2-ok\n', 'admission refusal poisoned accepted connection')
            c.ping()
            c.close()
            deadline = time.monotonic() + 3
            recovered = None
            while time.monotonic() < deadline:
                try:
                    recovered = Client(port, fixtures)
                    recovered.get('/installed')
                    break
                except (ConnectionError, EOFError, ssl.SSLError):
                    if recovered:
                        recovered.close()
                    recovered = None
                    time.sleep(.002)
            require(recovered is not None, 'admission limit did not recover at retirement')
            recovered.close()
            rows.append(dict(case='listener_connection_limit_recovery' if mode == 'connection-limit' else 'timer_limit_retirement', status='passed'))
        elif mode == 'stream-limit':
            held = [c.request('/suspend'), c.request('/body', 'POST', ended=False)]
            c.ping()
            refused = c.raw_opening('/installed')
            c.until(lambda: refused in c.raw_resets)
            require(c.raw_resets[refused] == 7, 'stream limit did not refuse new opening')
            c.ping()
            c.connection.reset_stream(held[0]); c.connection.reset_stream(held[1]); c.flush()
            c.ping()
            require(c.wait(c.request('/installed', stream=refused+2))['body'] == b'installed-v3-h2-ok\n', 'stream seat failed to recover')
            rows.append(dict(case='stream_limit_control_progress', status='passed', ping_ack=True))
        elif mode == 'header-limit':
            before = c.counts()[0]
            try:
                refused = c.wait(c.request('/installed', headers=[('x-large', 'z'*4096)]))
                require('reset' in refused, 'header cap accepted oversized fields')
                c.ping(); c.get('/installed')
            except EOFError:
                require(c.goaway and c.goaway[-1]['code'] == 9, 'bounded HPACK decoding lacked COMPRESSION_ERROR: ' + repr(c.goaway))
            require(int(command('stats').split()[0]) == before, 'oversized head reached body handler')
            rows.append(dict(case='header_limit_bounded', status='passed', bytes=4096, goaway=c.goaway))
        elif mode == 'body-limit':
            held=[]
            for _ in range(8):
                stream=c.request('/body', 'POST', ended=False); held.append(stream); c.ping()
                if stream in c.raw_resets:
                    break
            require(any(stream in c.raw_resets for stream in held), 'body storage cap did not refuse before eight rings')
            c.ping()
            for stream in held:
                if stream not in c.raw_resets:
                    c.connection.reset_stream(stream)
            c.flush(); c.ping(); c.get('/installed')
            rows.append(dict(case='body_limit_bounded', status='passed', attempted=len(held), ping_ack=True))
        elif mode == 'output-limit':
            prior=c.settings_acks
            c.connection.update_settings({h2.settings.SettingCodes.INITIAL_WINDOW_SIZE: 0}); c.flush()
            c.until(lambda:c.settings_acks>prior)
            held=[c.request('/large') for _ in range(8)]
            deadline = time.monotonic() + 3
            while not any(stream in c.raw_resets for stream in held):
                require(time.monotonic() < deadline, 'output cap did not refuse saturated writers')
                c.ping()
            require(int(command('stats').split()[6])<=131072, 'accounted output exceeded configured cap')
            c.ping()
            for stream in held:
                if stream not in c.raw_resets:c.connection.reset_stream(stream)
            c.flush(); c.ping()
            c.connection.update_settings({h2.settings.SettingCodes.INITIAL_WINDOW_SIZE: 65535});c.flush()
            c.get('/installed')
            rows.append(dict(case='output_limit_control_progress', status='passed', ping_ack=True))
    finally:
        c.close()
    deadline=time.monotonic()+3
    while True:
        counters=[int(n) for n in command('stats').split()]
        if counters[4:7]==[0,0,0]:break
        require(time.monotonic()<deadline,'capacity not returned after actual connection retirement')
        time.sleep(.002)
    rows.append(dict(case='capacity_returns_to_baseline',status='passed',connections=0,streams=0,output_bytes=0))
    return rows


def queued_reset(port, fixtures, command):
    c=Client(port,fixtures)
    try:
        c.get('/installed')
        before=int(command('stats').split()[0])
        running=[c.request('/running') for _ in range(3)]
        deadline=time.monotonic()+3
        while int(command('stats').split()[0])<before+3:
            require(time.monotonic()<deadline,'three worker barriers did not enter')
            time.sleep(.001)
        target=c.connection.get_next_available_stream_id()
        c.connection.send_headers(target,[(':method','GET'),(':scheme','https'),(':authority','a.example'),(':path','/running')],end_stream=True)
        c.connection.reset_stream(target)
        c.flush()
        c.ping()
        require(int(command('stats').split()[0])==before+3,'cancelled queued handler entered application body')
        require(command('release')=='RELEASED','worker release acknowledgement')
        for stream in running:require(dict(c.wait(stream)['headers']).get(':status')=='204','running sibling completion')
        c.get('/installed')
        return [dict(case='reset_queued_resume_never_enters',status='passed',occupied_workers=3,queued_entry_count=0,ping_ack=True)]
    finally:c.close()


def disconnect(port,fixtures,command):
    c=Client(port,fixtures)
    c.get('/installed')
    prior=[int(n) for n in command('stats').split()]
    held=c.request('/suspend')
    body=c.request('/body','POST',ended=False)
    deadline=time.monotonic()+3
    while int(command('stats').split()[0])<prior[0]+2:
        require(time.monotonic()<deadline,'disconnect targets did not enter')
        time.sleep(.001)
    command('snapshot')
    c.ping();c.close()
    deadline=time.monotonic()+3
    while True:
        after=[int(n) for n in command('stats').split()]
        if after[4:7]==[0,0,0] and after[1]==after[2]:break
        require(time.monotonic()<deadline,'EOF did not settle every connection request exactly once')
        time.sleep(.001)
    require(after[1]-prior[1]==2 and after[2]-prior[2]==2,'EOF completion count differed from admitted requests')
    require(command('stale')=='STALE_GENERATIONS_INERT','stale generation dispatch did not settle')
    return [dict(case='disconnect_cancels_connection_streams',status='passed',cancelled=2,active_connections=0,active_streams=0)]


def idle_deadline(port,fixtures,kind):
    c=Client(port,fixtures,acknowledge_settings=kind!='settings')
    try:
        c.get('/installed')
        if kind=='header':
            stream=c.connection.get_next_available_stream_id()
            block=c.connection.encoder.encode([(':method','GET'),(':scheme','https'),(':authority','a.example'),(':path','/installed')])
            c.socket.sendall(len(block).to_bytes(3,'big')+b'\x01\x01'+stream.to_bytes(4,'big')+block)
        started=time.monotonic()
        c.until(lambda:bool(c.goaway),12 if kind=='settings' else 3)
        elapsed=time.monotonic()-started
        require(c.goaway[-1]['code']==(4 if kind=='settings' else 0),'idle deadline wire error differs')
        require((8<elapsed<12) if kind=='settings' else elapsed<2,'idle deadline absolute timing differs')
        return [dict(case=kind+'_timeout_idle',status='passed',elapsed=elapsed,goaway=c.goaway)]
    finally:c.close()
