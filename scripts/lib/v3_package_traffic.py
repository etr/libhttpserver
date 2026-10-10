"""HTTP/1.0/1.1, buffered POST, persistent and pipelined installed traffic."""
import json
import socket
from v3_package_process import Consumer
from v3_package_matrix import configuration_results

BODY = b'installed-v3-ok\n'


def response(connection, pending=b''):
    data = pending
    while b'\r\n\r\n' not in data:
        chunk = connection.recv(4096)
        data += chunk
        if len(data) > 16384 or not chunk:
            raise RuntimeError('missing/oversized response head')
    head, body = data.split(b'\r\n\r\n', 1)
    headers = dict(line.lower().split(b':', 1) for line in head.split(b'\r\n')[1:])
    size = int(headers[b'content-length'].strip())
    if not 0 <= size <= 16384:
        raise RuntimeError('oversized response body')
    while len(body) < size:
        chunk = connection.recv(4096)
        if not chunk:
            raise RuntimeError('truncated response')
        body += chunk
    if not head.startswith((b'HTTP/1.0 200 ', b'HTTP/1.1 200 ')):
        raise RuntimeError('incorrect response status: ' + repr(head))
    return body[:size], body[size:], head.decode()


def request(version='1.1', close=False, body=None):
    return ((b'POST' if body else b'GET') + b' /installed HTTP/' + version.encode() +
            b'\r\nHost: localhost\r\nConnection: ' + (b'close' if close else b'keep-alive') + b'\r\n' +
            (b'Content-Length: ' + str(len(body)).encode() + b'\r\n' if body else b'') + b'\r\n' + (body or b''))


def traffic(executable, cell, cwd, env, inventory):
    receipt = cwd / (cell['linkage'] + '-' + cell['loop'] + '-process.json')
    results = []
    with Consumer([str(executable), cell['tls'], cell['loop']], cwd, env, receipt) as child:
        port = child.port()
        for name, version, payload in (('http10', '1.0', None), ('http11', '1.1', None), ('post', '1.1', b'bounded-post-body')):
            with socket.create_connection(('127.0.0.1', port), timeout=5) as connection:
                connection.sendall(request(version, True, payload))
                body, pending, head = response(connection)
                if not head.startswith('HTTP/' + version + ' 200 '):
                    raise RuntimeError('response version mismatch: ' + head)
                if body != (payload or BODY) or pending:
                    raise RuntimeError('incorrect installed body: ' + repr(body))
                if connection.recv(1):
                    raise RuntimeError('Connection: close did not close')
                results.append(dict(case=name, response=head, body=body.decode()))
        with socket.create_connection(('127.0.0.1', port), timeout=5) as connection:
            for _ in range(2):
                connection.sendall(request())
                body, pending, _ = response(connection)
                if body != BODY or pending:
                    raise RuntimeError('persistent response mismatch')
            results.append(dict(case='persistent', responses=2))
            connection.sendall(request() + request(close=True))
            pending = b''
            for _ in range(2):
                body, pending, _ = response(connection, pending)
                if body != BODY:
                    raise RuntimeError('pipeline response mismatch')
            if pending or connection.recv(1):
                raise RuntimeError('pipeline did not close cleanly')
            results.append(dict(case='pipeline', responses=2))
        child.stop()
        results.append(dict(case='shutdown', exit=0))
    process = json.loads(receipt.read_text())
    configurations = configuration_results(process['stderr'], inventory, cell['loop'], tls=cell['tls'])
    if cell['loop'] == 'external' and 'PACKAGE_EXTERNAL ' not in process['stderr']:
        raise RuntimeError('external dispatch witness absent')
    return dict(**cell, port=port, traffic=results, configurations=configurations, process=str(receipt), exit=0)
