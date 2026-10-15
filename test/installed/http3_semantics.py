"""Same route predicates through independent H1, H2 and H3 wire clients."""

import asyncio
import hashlib
import http.client
import re
import socket
import ssl

CASES = (
    "ordered_raw_target",
    "parameter_prefix_extension",
    "method_refusal",
    "hooks_errors",
    "basic_authentication",
    "digest_authentication",
    "urlencoded_limits",
    "multipart_files",
    "response_definitions",
    "file_borrowed_missing",
    "early_response",
    "body_admission_continue",
    "one_shot_sources",
)


async def corpus(request, fixtures):
    rows = []

    def status(reply, code):
        assert (b":status", str(code).encode()) in reply["sections"][0], reply

    def fields(reply, name):
        return [v for k, v in reply["sections"][0] if k == name]

    reply = await request(
        "POST",
        "/items/../semantic?x=1",
        b"ordered",
        [(b"x-repeat", b"first"), (b"x-repeat", b"second")],
        [(b"x-upload-end", b"first"), (b"x-upload-end", b"second")],
    )
    status(reply, 200)
    assert reply["body"] == b"ordered"
    assert fields(reply, b"x-target") == [b"/items/../semantic?x=1"]
    assert fields(reply, b"x-route") == [b"/semantic"]
    assert fields(reply, b"x-repeat") == [b"first", b"second"]
    assert reply['sections'][-1] == [(b'x-upload-end',b'first'),(b'x-upload-end',b'second')]
    rows.append(
        dict(
            case=CASES[0],
            status="passed",
            ordered_fields=["first", "second"],
            raw_target="/items/../semantic?x=1",
            ordered_trailers=['first','second'],
        )
    )
    for method, path, expected in [
        ("GET", "/items/abc", b"parameter"),
        ("GET", "/prefix/deeper", b"prefix"),
        ("CUSTOM", "/extension", b"extension"),
    ]:
        reply = await request(method, path)
        status(reply, 200)
        assert reply["body"] == expected
    rows.append(dict(case=CASES[1], status="passed"))
    reply = await request("POST", "/items/abc", b"discard")
    status(reply, 405)
    assert b"GET" in b",".join(fields(reply, b"allow"))
    rows.append(dict(case=CASES[2], status="passed"))
    for path, expected in [("/throw", 500), ("/missing-terminal", 500), ("/short", 418)]:
        status(await request("GET", path), expected)
    committed = await request('GET','/throw-committed')
    assert committed.get('aborted') and not any((b':status',b'500') in section for section in committed['sections'])
    status(await request('GET','/items/recovered'),200)
    rows.append(dict(case=CASES[3], status="passed",committed_aborted=True))
    for token, code in [(None, 401), (b"Basic !!!", 401), (b"Basic dXNlcjpwYXNzd29yZA==", 200)]:
        reply = await request(
            "GET", "/auth/basic", None, [] if token is None else [(b"authorization", token)]
        )
        status(reply, code)
        if code == 401:
            assert fields(reply, b"www-authenticate") == [b'Basic realm="public"']
        else:
            assert reply["body"] == b"authenticated"
    rows.append(dict(case=CASES[4], status="passed"))
    challenge = await request("GET", "/auth/digest")
    status(challenge, 401)
    parameters = dict(
        re.findall(r'(\w+)="([^"]*)"', fields(challenge, b"www-authenticate")[0].decode())
    )
    md5 = lambda value: hashlib.md5(value.encode()).hexdigest()
    nonce, realm = parameters["nonce"], parameters["realm"]
    response = md5(
        ":".join(
            [
                md5("user:" + realm + ":password"),
                nonce,
                "00000001",
                "public-client",
                "auth",
                md5("GET:/auth/digest"),
            ]
        )
    )
    authorization = f'Digest username="user", realm="{realm}", nonce="{nonce}", uri="/auth/digest", response="{response}", algorithm=MD5, qop=auth, nc=00000001, cnonce="public-client", opaque="{parameters["opaque"]}"'.encode()
    reply = await request("GET", "/auth/digest", None, [(b"authorization", authorization)])
    status(reply, 200)
    assert reply["body"] == b"authenticated"
    status(await request("GET", "/auth/digest", None, [(b"authorization", authorization)]), 401)
    rows.append(dict(case=CASES[5], status="passed", replay_status=401))
    content = [(b"content-type", b"application/x-www-form-urlencoded")]
    reply = await request("POST", "/form", b"a=one&a=two&space=a+b", content)
    status(reply, 200)
    assert reply["body"] == b"a=one\na=two\nspace=a b\n"
    status(await request("POST", "/form", b"a=1&b=2&c=3&d=4&e=5", content), 413)
    limited=await request("POST", "/form", b"a="+b'x'*255, content)
    assert (b':status',b'413') in limited['sections'][0] or limited.get('reset')==1,limited
    status(await request("POST", "/form", b"a=%ZZ", content), 400)
    rows.append(dict(case=CASES[6], status="passed",byte_limit_refused=True))
    body = b'--boundary\r\nContent-Disposition: form-data; name="a"\r\n\r\none\r\n--boundary\r\nContent-Disposition: form-data; name="a"\r\n\r\ntwo\r\n--boundary\r\nContent-Disposition: form-data; name="upload"; filename="upload.bin"\r\nContent-Type: application/octet-stream\r\n\r\nbytes\r\n--boundary--\r\n'
    reply = await request(
        "POST", "/multipart", body, [(b"content-type", b"multipart/form-data; boundary=boundary")]
    )
    status(reply, 200)
    assert reply["body"] == b"a=one\na=two\nupload:upload.bin:5\n"
    for _ in range(100):
        if not list(fixtures.glob('lht-upload-*')):
            break
        await asyncio.sleep(.01)
    assert not list(fixtures.glob('lht-upload-*')), 'completed multipart retained temporary files'
    rows.append(dict(case=CASES[7], status="passed", file_bytes=5))
    for reply in await asyncio.gather(*(request("GET", "/definition") for _ in range(3))):
        status(reply, 200)
        assert reply["body"] == b"immutable definition"
        assert fields(reply, b"x-overlay") == [b"base", b"request"]
    rows.append(dict(case=CASES[8], status="passed", concurrent_cursors=3))
    reply = await request("GET", "/file")
    status(reply, 200)
    assert reply["body"] == (fixtures / "root.pem").read_bytes()
    reply = await request("GET", "/borrowed")
    status(reply, 200)
    assert reply["body"] == b"borrowed immutable bytes"
    status(await request("GET", "/missing-file"), 404)
    rows.append(dict(case=CASES[9], status="passed"))
    status(await request("POST", "/early", b"abandoned upload"), 403)
    rows.append(dict(case=CASES[10], status="passed"))
    continued=await request('POST','/body',b'continued admitted body',[(b'expect',b'100-continue')])
    assert continued['sections'][0]==[(b':status',b'100')],continued
    assert (b':status',b'200') in continued['sections'][1] and continued['body']==b'continued admitted body'
    rows.append(dict(case=CASES[11],status='passed',informational_status=100,body='continued admitted body'))
    for path, expected in [('/owned-file-once', b'one-shot file'), ('/owned-pipe-once', b'one-shot pipe')]:
        first = await request('GET', path)
        status(first, 200)
        assert first['body'] == expected
        second = await request('GET', path)
        status(second, 409)
        assert fields(second, b'x-source-closes') == [b'1']
    rows.append(dict(case=CASES[12], status='passed', sources=2, reuse_status=409, closes_per_source=1))
    return rows


async def h3_cases(port, fixtures, module):
    async with module.connect(
        "127.0.0.1",
        port,
        configuration=module.configuration(fixtures),
        create_protocol=module.Client,
    ) as peer:

        async def request(method, path, body=None, fields=(), trailers=()):
            if body is not None:
                fields = list(fields) + [(b"content-length", str(len(body)).encode())]
            expect_continue=(b'expect',b'100-continue') in fields
            state=peer.request(method,path,None if expect_continue else body,fields=fields,trailers=trailers,end=not expect_continue)
            if expect_continue:
                state['opening']=asyncio.get_running_loop().create_future()
                assert await asyncio.wait_for(state['opening'],2)==[(b':status',b'100')]
                peer.h3.send_data(state['stream'],body,end_stream=True);peer.transmit()
            try:return await asyncio.wait_for(state['done'],3)
            except RuntimeError:
                if path != '/throw-committed':raise
                assert isinstance(state.get('terminal'),module.StreamReset) and state['terminal'].error_code==0x102
                state['aborted']=True;return state

        return await corpus(request, fixtures)


async def tcp_cases(port, fixtures, protocol, h2_client):
    # Each operation owns a separate independently authenticated TCP connection;
    # concurrent response definitions must not share a client parser or cursor.
    def operation(method, path, body, fields, trailers):
        expect_continue=(b'expect',b'100-continue') in fields
        if protocol == "h2" and body is not None:
            fields = list(fields) + [(b"content-length", str(len(body)).encode())]
        if protocol == "h2":
            peer = h2_client(port, fixtures)
            try:
                stream = peer.request(
                    path,
                    method=method,
                    headers=[(k.decode(), v.decode()) for k, v in fields],
                    ended=body is None,
                )
                if expect_continue:
                    peer.until(lambda: bool(peer.responses.get(stream,{}).get('informational')))
                    assert peer.responses[stream]['informational']==[[(':status','100')]]
                if body is not None:
                    for offset in range(0,len(body),3):
                        peer.send(stream,body[offset:offset+3],ended=not trailers and offset+3>=len(body))
                if trailers:
                    peer.connection.send_headers(stream,[(k.decode(),v.decode()) for k,v in trailers],end_stream=True)
                    peer.flush()
                reply = peer.wait(stream)
                return dict(
                    sections=[[(k.encode(),v.encode()) for k,v in section] for section in reply.get('informational',[])]+[
                        [(k.encode(), v.encode()) for k, v in reply["headers"]],
                        [(k.encode(), v.encode()) for k, v in reply["trailers"]],
                    ],
                    body=reply["body"],
                    reset=reply.get('reset'),aborted=path=='/throw-committed' and reply.get('reset')==2,
                )
            finally:
                peer.close()
        context = ssl.create_default_context(cafile=str(fixtures / "root.pem"))
        context.set_alpn_protocols(["http/1.1"])
        peer = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
        peer.sock = context.wrap_socket(
            socket.create_connection(("127.0.0.1", port), timeout=4), server_hostname="a.example"
        )
        try:
            assert peer.sock.selected_alpn_protocol() == "http/1.1"
            peer.putrequest(method, path, skip_host=True)
            peer.putheader("Host", "a.example")
            for k, v in fields:
                peer.putheader(k.decode(), v.decode())
            if trailers:
                peer.putheader('Transfer-Encoding','chunked')
            elif body is not None:
                peer.putheader("Content-Length", str(len(body)))
            informational=[]
            if expect_continue:
                peer.endheaders()
                head=bytearray()
                while not head.endswith(b'\r\n\r\n'):
                    part=peer.sock.recv(1);assert part and len(head)<8192
                    head.extend(part)
                assert head.startswith(b'HTTP/1.1 100 '),head
                informational=[[(b':status',b'100')]]
                peer.send(body)
            elif trailers:
                peer.endheaders()
                for offset in range(0,len(body),3):
                    part=body[offset:offset+3]
                    peer.send(f'{len(part):x}\r\n'.encode()+part+b'\r\n')
                peer.send(b'0\r\n'+b''.join(k+b': '+v+b'\r\n' for k,v in trailers)+b'\r\n')
            else:peer.endheaders(body)
            class Response(http.client.HTTPResponse):
                def _read_and_discard_trailer(self):
                    self.trailers=[]
                    for _ in range(128):
                        line=self.fp.readline(8193)
                        assert len(line)<=8192 and line, 'bounded complete H1 trailer'
                        if line == b'\r\n':return
                        name,value=line.rstrip(b'\r\n').split(b':',1)
                        self.trailers.append((name.lower(),value.strip()))
                    raise AssertionError('H1 trailer count bound')
            peer.response_class=Response
            try:
                reply = peer.getresponse()
            except http.client.RemoteDisconnected:
                assert path=='/throw-committed'
                return dict(sections=[],body=b'',aborted=True)
            aborted=False
            try:payload=reply.read()
            except http.client.IncompleteRead as error:
                assert path=='/throw-committed'
                payload=error.partial;aborted=True
            return dict(
                sections=informational+[
                    [(b":status", str(reply.status).encode())]
                    + [(k.lower().encode(), v.encode()) for k, v in reply.getheaders()]
                ] + [getattr(reply,'trailers',[])],
                body=payload,
                aborted=aborted,
            )
        finally:
            peer.close()

    async def request(method, path, body=None, fields=(), trailers=()):
        return await asyncio.to_thread(operation, method, path, body, fields, trailers)

    return await corpus(request, fixtures)
