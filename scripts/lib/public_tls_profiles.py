"""Bounded installed authentication, retirement and ACME wire witnesses."""
import hashlib
import json
import socket
import ssl
import time

from package_commands import Commands
from package_process import Consumer
from package_traffic import response


def check(value, text):
    if not value:
        raise RuntimeError(text)


def command(child, text, expected=None):
    child.process.stdin.write((text + '\n').encode())
    child.process.stdin.flush()
    actual = child.line()
    check(expected is None or actual == expected, 'profile command: ' + actual)
    return actual


def counts(child):
    return list(map(int, command(child, 'stats').split()))


def wait_count(child, index, minimum):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        observed = counts(child)
        if observed[index] >= minimum:
            return observed
        time.sleep(.005)
    raise RuntimeError('profile counter deadline')


def context(fixtures, version, identity=None):
    result = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
    result.minimum_version = result.maximum_version = (
        ssl.TLSVersion.TLSv1_2 if version == '12' else ssl.TLSVersion.TLSv1_3)
    result.set_alpn_protocols(['http/1.1'])
    if identity:
        result.load_cert_chain(str(fixtures / (identity + '-chain.pem')), str(fixtures / (identity + '-key.pem')))
    return result


def connect(port, ctx, host='a.example'):
    raw = socket.create_connection(('127.0.0.1', port), timeout=3)
    try:
        return ctx.wrap_socket(raw, server_hostname=host)
    except BaseException:
        raw.close()
        raise


def exchange(peer, fingerprint=''):
    peer.sendall(b'GET /installed HTTP/1.1\r\nHost: a.example\r\nConnection: close\r\n\r\n')
    body, pending, head = response(peer)
    check(head.startswith('HTTP/1.1 200 ') and not pending, 'profile response framing')
    check(body == ('profile-ok\n' + fingerprint + '\n').encode(), 'verified metadata/body differs')
    check(peer.selected_alpn_protocol() == 'http/1.1', 'profile ALPN differs')
    return dict(head=head, body=body.decode(), certificate_sha256=hashlib.sha256(peer.getpeercert(binary_form=True)).hexdigest())


def scenarios(executable, client, loop, mode, directory, env, fixtures):
    rows, serial = [], 0
    commands = Commands(directory)
    def launch(scenario, port=0):
        nonlocal serial
        serial += 1
        return Consumer([str(executable), mode, loop, str(fixtures), scenario, str(port)],
                        directory, env, directory / ('profiles-%d.json' % serial))
    def probe(port, version='13', kind='psk', host='a.example', alpn='http/1.1', identity='identity', action='once', success=True):
        output = commands([client, str(port), version, kind, host, alpn, identity, action, fixtures], directory,
                          env, timeout=8, allow_failure=not success)
        lines = [json.loads(line) for line in output.splitlines() if line.startswith('{')]
        check(lines and all(row['connected'] == success for row in lines), 'independent handshake outcome differs')
        if success:
            for row in lines:
                check(row['version'] == ('TLSv1.2' if version == '12' else 'TLSv1.3'), 'independent version differs')
                check(row['alpn'] == alpn, 'independent ALPN differs')
                if kind == 'psk':
                    check(not row['ticket'] and not row['certificate_sha256'], 'PSK acquired certificate/ticket')
                    check(row['cipher'] == ('PSK-AES128-GCM-SHA256' if version == '12' else 'TLS_AES_128_GCM_SHA256'), 'PSK cipher differs')
        return lines
    if mode == 'no':
        with launch('off') as child:
            check(child.line() == 'PROFILES_OFF 5', 'same-source unavailable profile inventory')
            child.process.wait(timeout=3)
            check(child.process.returncode == 0, 'TLS-off profile process failed')
        return [dict(case='profiles_off_same_source', checks=5)]
    # Inspect client validity independently rather than assuming fixture dates.
    trusted = ssl.PEM_cert_to_DER_cert((fixtures / 'client-valid.pem').read_text())
    trusted_fingerprint = hashlib.sha256(trusted).hexdigest()
    for auth in ('request', 'require'):
        with launch('mtls-' + auth) as child:
            port = child.port()
            for version in ('12', '13'):
                for identity in ('client-valid', None, 'client-other'):
                    accepted = identity == 'client-valid' or (identity is None and auth == 'request')
                    before = counts(child)[1]
                    evidence = {}
                    try:
                        with connect(port, context(fixtures, version, identity)) as peer:
                            evidence = exchange(peer, trusted_fingerprint if identity == 'client-valid' else '')
                    except (ssl.SSLError, ConnectionError, EOFError, RuntimeError) as error:
                        if isinstance(error, RuntimeError) and str(error) != 'missing/oversized response head':
                            raise
                        check(not accepted, 'valid mTLS client rejected')
                    else:
                        check(accepted, 'untrusted/absent client reached route')
                    after = counts(child)
                    check(after[1] == before + int(accepted) and after[3] == 0, 'mTLS route/metadata witness')
                    rows.append(dict(case='mtls_' + auth + '_' + version + '_' + (identity or 'absent'),
                                     accepted=accepted, handler_delta=after[1] - before, **evidence))
            command(child, 'incompatible', 'PRESERVED')
            with connect(port, context(fixtures, '13', 'client-valid')) as peer:
                exchange(peer, trusted_fingerprint)
            child.stop()
        rows.append(dict(case='mtls_' + auth + '_bound_policy'))
    with launch('psk') as child:
        port = child.port()
        for version in ('12', '13'):
            for identity in ('identity', 'rejected', 'wrong-key', 'throw'):
                success = identity == 'identity'
                before = counts(child)[1]
                evidence = probe(port, version, identity=identity, success=success)
                after = counts(child)
                check(after[1] == before + int(success) and after[7] == 0, 'PSK callback/route witness')
                rows.append(dict(case='psk_' + version + '_' + identity, client=evidence, handler_delta=after[1] - before))
        for version in ('12', '13'):
            probe(port, version, identity='version' + version)
        check(counts(child)[7] == 0, 'public callback TLS version translation differs')
        rows.append(dict(case='psk_binary13', client=probe(port, identity='binary')))
        for identity in ('empty', 'oversized'):
            before = counts(child)
            probe(port, identity=identity, success=False)
            after = counts(child)
            check(after[1] == before[1] and after[4] == before[4] + 1, 'invalid key routed or skipped lookup')
        before = counts(child)
        probe(port, identity='x' * 65, success=False)
        after = counts(child)
        check(after[1] == before[1] and after[4] == before[4], 'oversized identity entered callback')
        rows.append(dict(case='psk_identity_key_bounds', maximum_identity=64, maximum_key=32, negative_handshakes=3))
        before = counts(child)[1]
        early = probe(port, action='early')[0]
        check(early['early_sent'] > 0 and early['early_status'] == 1, 'wire early data was not rejected')
        check(counts(child)[1] == before + 1, 'early request reached route')
        rows.append(dict(case='psk_wire_early_data_rejected', client=early, handler_delta=1))
        rows.append(dict(case='psk_no_ticket_resumption', client=probe(port, action='resume')))
        probe(port, identity='self')
        check(counts(child)[8] == 9, 'callback self-retirement did not return would_deadlock')
        rows.append(dict(case='psk_self_retirement_refused'))
        child.stop()
    # Running and queued callbacks retain one fixed runtime across publication.
    for terminal in ('timeout', 'peer_close', 'request_stop', 'drain'):
        with launch('psk-block') as child:
            port = child.port()
            args = [str(client), str(port), '13', 'psk', 'a.example', 'http/1.1', 'block', 'once', str(fixtures)]
            with Consumer(args, directory, env, directory / ('blocked-' + terminal + '.json')) as blocked:
                wait_count(child, 4, 1)
                started = time.monotonic()
                if terminal == 'peer_close':
                    blocked.process.kill()
                    blocked.process.wait(timeout=3)
                elif terminal in ('request_stop', 'drain'):
                    command(child, terminal, 'STOPPED' if terminal == 'request_stop' else 'DRAINED')
                if terminal != 'peer_close':
                    rejected = json.loads(blocked.line())
                    check(not rejected['connected'], 'blocked lookup authenticated')
                    blocked.process.wait(timeout=3)
                elapsed = time.monotonic() - started
                check(elapsed < 2 and counts(child)[1] == 0 and counts(child)[5] == 0, 'logical terminal joined/routed callback')
                if terminal == 'timeout':
                    for _ in range(4):
                        command(child, 'rotate', 'ROTATED')
                    # One queued lookup expires without callback entry; excess
                    # admissions reject within the same finite absolute bound.
                    probe(port, identity='identity', success=False)
                    check(counts(child)[4] == 1, 'queued expiry invoked callback')
                if terminal == 'peer_close':
                    command(child, 'drop', 'DROPPED')
                command(child, 'retire', '8')
                command(child, 'release', 'RELEASED')
                retired = wait_count(child, 5, 1)
                check(retired[6] == 1 and retired[1] == 0, 'late accepted key resurrected handshake')
                command(child, 'retire', '0')
            child.stop()
        rows.append(dict(case='psk_blocked_' + terminal, terminal_seconds=elapsed, callbacks_entered=retired[4],
                         callbacks_retired=retired[5], callbacks_cancelled=retired[6], handler_count=retired[1],
                         caller_handles_dropped=terminal == 'peer_close'))
    with launch('psk-block') as child:
        port = child.port()
        arguments = [str(client), str(port), '13', 'psk', 'a.example', 'http/1.1', 'block', 'once', str(fixtures)]
        with Consumer(arguments, directory, env, directory / 'saturated-active.json') as active:
            wait_count(child, 4, 1)
            with Consumer(arguments, directory, env, directory / 'saturated-queued.json') as queued, Consumer(arguments, directory, env, directory / 'saturated-excess.json') as excess:
                started = time.monotonic()
                for _ in range(4):
                    command(child, 'rotate', 'ROTATED')
                for pending in (active, queued, excess):
                    check(not json.loads(pending.line())['connected'], 'saturated runtime authenticated pending work')
                    pending.process.wait(timeout=3)
                check(time.monotonic() - started < 2 and counts(child)[4:6] == [1, 0],
                      'rotation replenished held callback capacity')
                command(child, 'release', 'RELEASED')
                wait_count(child, 5, 1)
                probe(port)
                recovered = counts(child)
                check(recovered[4:6] == [2, 2] and recovered[1] == 1, 'runtime capacity did not recover after retirement')
        child.stop()
        rows.append(dict(case='psk_saturation_rotation_recovery', callbacks_entered=2, callbacks_retired=2, handler_count=1))
    with launch('psk-shared') as child:
        ports = list(map(int, child.line().split()))
        check(len(ports) == 2 and ports[0] != ports[1], 'shared runtime listeners absent')
        probe(ports[0])
        probe(ports[1])
        command(child, 'request_stop', 'STOPPED')
        probe(ports[1])
        check(counts(child)[4] == 3, 'stopping server terminated shared lookup admission')
        child.stop()
        rows.append(dict(case='psk_shared_runtime_server_isolation', callback_count=3))
    for terminal in ('stop', 'drain'):
        with launch('psk-handler-' + terminal) as child:
            port = child.port()
            held_args = [str(client), str(port), '13', 'psk', 'a.example', 'http/1.1', 'identity', 'hold-stop' if terminal == 'stop' else 'hold', str(fixtures)]
            with Consumer(held_args, directory, env, directory / ('handler-held-' + terminal + '.json')) as healthy:
                check(json.loads(healthy.line())['held'], 'healthy established socket absent')
                blocked_args = [str(client), str(port), '13', 'psk', 'a.example', 'http/1.1', 'block', 'once', str(fixtures)]
                with Consumer(blocked_args, directory, env, directory / ('handler-blocked-' + terminal + '.json')) as blocked:
                    wait_count(child, 4, 2)
                    healthy.process.stdin.write(b'request\n'); healthy.process.stdin.flush()
                    deadline = time.monotonic() + 2
                    acknowledgement = 'PENDING'
                    while acknowledgement == 'PENDING' and time.monotonic() < deadline:
                        acknowledgement = command(child, 'handler_returned')
                    check(acknowledgement == 'RETURNED' and counts(child)[5] == 1, 'handler joined held application lookup')
                    completed = json.loads(healthy.line())
                    check(completed.get('terminal') == 'stopped' if terminal == 'stop' else completed['connected'],
                          'established handler connection did not reach its permitted terminal')
                    check(not json.loads(blocked.line())['connected'], 'cancelled lookup authenticated')
                    command(child, 'retire', '8')
                    command(child, 'release', 'RELEASED')
                    wait_count(child, 5, 2)
                    command(child, 'retire', '0')
                    healthy.process.wait(timeout=3); blocked.process.wait(timeout=3)
            child.stop()
        rows.append(dict(case='psk_handler_' + terminal + '_while_lookup_held', handler_returned=True))
    for version in ('12', '13'):
        with launch('certificate') as child:
            port = child.port()
            same = probe(port, version, kind='certificate', action='resume')
            check(same[0]['reused'] == 0 and same[1]['reused'] == 1 and same[1]['certificate_messages'] == 0,
                  'ordinary same-generation resumption absent')
            rows.append(dict(case='certificate_resumption_' + version, client=same))
            cross_args = [str(client), str(port), version, 'certificate', 'a.example', 'http/1.1', 'identity', 'interactive', str(fixtures)]
            with Consumer(cross_args, directory, env, directory / ('cross-host-' + version + '.json')) as session:
                old_host = json.loads(session.line())
                session.process.stdin.write(b'next b.example\n'); session.process.stdin.flush()
                other_host = json.loads(session.line())
                check(other_host['connected'] and other_host['reused'] == 0 and other_host['certificate_messages'] == 1
                      and other_host['certificate_sha256'] != old_host['certificate_sha256'], 'ticket crossed host namespace')
                session.process.stdin.write(b'quit\n'); session.process.stdin.flush()
                session.process.wait(timeout=3)
            rows.append(dict(case='certificate_cross_host_' + version, client=other_host))
            args = [str(client), str(port), version, 'certificate', 'a.example', 'http/1.1', 'identity', 'interactive', str(fixtures)]
            with Consumer(args, directory, env, directory / ('rotation-session-' + version + '.json')) as session:
                old = json.loads(session.line())
                session.process.stdin.write(b'next a.example\n'); session.process.stdin.flush()
                resumed = json.loads(session.line())
                check(resumed['connected'] and resumed['reused'] == 1 and resumed['certificate_messages'] == 0
                      and resumed['certificate_sha256'] == old['certificate_sha256'], 'same-host session did not resume before rotation')
                with connect(port, context(fixtures, version)) as established:
                    for turn in range(2):
                        established.sendall(b'GET /installed HTTP/1.1\r\nHost: a.example\r\nConnection: keep-alive\r\n\r\n')
                        body, pending, head = response(established)
                        check(head.startswith('HTTP/1.1 200 ') and body == b'profile-ok\n\n' and not pending,
                              'replacement disrupted established connection')
                        if turn == 0:
                            command(child, 'rotate-same-host', 'ROTATED')
                    session.process.stdin.write(b'next a.example\n'); session.process.stdin.flush()
                    fresh = json.loads(session.line())
                    check(fresh['connected'] and fresh['reused'] == 0 and fresh['certificate_messages'] == 1
                          and fresh['certificate_sha256'] != old['certificate_sha256'], 'old same-host session bypassed replacement')
                    expected = hashlib.sha256(ssl.PEM_cert_to_DER_cert((fixtures / 'a-replacement.pem').read_text())).hexdigest()
                    check(fresh['certificate_sha256'] == expected and fresh['alpn'] == old['alpn'] == 'http/1.1',
                          'same-host replacement material or ALPN differs')
                session.process.stdin.write(b'quit\n'); session.process.stdin.flush()
                session.process.wait(timeout=3)
            child.stop()
            rows.append(dict(case='certificate_rotation_rejects_old_session_' + version, old=old, resumed=resumed, fresh=fresh,
                             endpoint=['127.0.0.1', port], sni='a.example', established_requests=2))
    with launch('certificate') as child:
        port = child.port()
        args = [str(client), str(port), '13', 'certificate', 'a.example', 'http/1.1', 'identity', 'hold-acme', str(fixtures)]
        with Consumer(args, directory, env, directory / 'selected-normal-rotation.json') as selected:
            check(json.loads(selected.line())['certificate_selected'], 'normal handshake selection absent')
            command(child, 'rotate', 'ROTATED')
            selected.process.stdin.write(b'continue\n'); selected.process.stdin.flush()
            old = json.loads(selected.line())
            fresh = probe(port, kind='certificate', host='b.example')[0]
            check(old['connected'] and old['certificate_sha256'] != fresh['certificate_sha256'],
                  'selected normal handshake did not retain its generation')
            selected.process.wait(timeout=3)
        child.stop()
        rows.append(dict(case='certificate_selected_handshake_rotation', selected=old, fresh=fresh))
    with launch('certificate') as first, launch('certificate') as second:
        first_port, second_port = first.port(), second.port()
        args = [str(client), str(first_port), '13', 'certificate', 'a.example', 'http/1.1', 'identity', 'interactive', str(fixtures)]
        with Consumer(args, directory, env, directory / 'cross-store.json') as session:
            json.loads(session.line())
            session.process.stdin.write(('next a.example ' + str(second_port) + '\n').encode()); session.process.stdin.flush()
            other = json.loads(session.line())
            check(other['connected'] and other['reused'] == 0 and other['certificate_messages'] == 1, 'ticket crossed store namespace')
            session.process.stdin.write(b'quit\n'); session.process.stdin.flush()
            session.process.wait(timeout=3)
        first.stop(); second.stop()
        rows.append(dict(case='certificate_cross_store', client=other))
    # These cells use actual port443; the caller supplies an isolated namespace
    # when host privileges do not permit binding. Never override trusted metadata.
    with launch('acme', 443) as child:
        port = child.port()
        check(port == 443, 'ACME did not bind actual TCP443')
        command(child, 'publish', 'PUBLISHED')
        positive = probe(port, kind='acme', alpn='acme-tls/1')[0]
        supplied = hashlib.sha256(ssl.PEM_cert_to_DER_cert((fixtures / 'acme.pem').read_text())).hexdigest()
        check(positive['certificate_messages'] == 1 and not positive['ticket'] and positive['certificate_sha256'] == supplied, 'ACME full supplied-certificate exchange')
        rows.append(dict(case='acme_actual443_exact', local_port=port, client=positive))
        canonical = probe(port, kind='acme', host='A.EXAMPLE.', alpn='acme-tls/1')[0]
        check(canonical['certificate_sha256'] == positive['certificate_sha256'], 'ACME canonical SNI differs')
        rows.append(dict(case='acme_canonical_sni', client=canonical))
        for host in ('-', 'unknown.example', 'example', 'a.example.suffix', '*.example'):
            probe(port, kind='acme', host=host, alpn='acme-tls/1', success=False)
        rows.append(dict(case='acme_sni_rejections', checks=5))
        # Normal protocol offers must keep their normal chain/hostname and body.
        for alpn in ('http/1.1', '-', 'acme-tls/1,http/1.1'):
            ctx = context(fixtures, '13')
            ctx.set_alpn_protocols([] if alpn == '-' else alpn.split(','))
            with connect(port, ctx) as peer:
                normal = peer.getpeercert(binary_form=True)
                peer.sendall(b'GET /installed HTTP/1.1\r\nHost: a.example\r\nConnection: close\r\n\r\n')
                body, _, head = response(peer)
                check(body == b'profile-ok\n\n' and head.startswith('HTTP/1.1 200 '), 'ACME leaked into normal HTTP')
                check(hashlib.sha256(normal).hexdigest() != positive['certificate_sha256'], 'normal selected challenge certificate')
        rows.append(dict(case='acme_alpn_normal_isolation', checks=3))
        variants = ('missing-san', 'multiple-san', 'wrong-san', 'duplicate-san', 'missing-digest', 'duplicate-digest',
                        'noncritical', 'wrong-length', 'wrong-digest', 'expired', 'future', 'malformed', 'extra',
                        'mismatch-key', 'malformed-key', 'encrypted-key', 'past-expiry', 'late-expiry')
        for variant in variants:
            command(child, 'variant ' + variant, 'REJECTED')
            check(probe(port, kind='acme', alpn='acme-tls/1')[0]['certificate_sha256'] == positive['certificate_sha256'],
                  'invalid ACME publication changed selected challenge')
        rows.append(dict(case='acme_invalid_material_preserves', checks=len(variants), variants=list(variants)))
        resumed = probe(port, kind='acme', alpn='acme-tls/1', action='resume')
        check(all(row['certificate_messages'] == 1 and row['reused'] == 0 and not row['ticket'] for row in resumed),
              'ACME resumed without fresh certificate')
        rows.append(dict(case='acme_no_resumption', client=resumed))
        before = counts(child)[1]
        early = probe(port, kind='acme-early', alpn='acme-tls/1', action='early')[0]
        check(early['early_sent'] > 0 and early['early_status'] == 1 and counts(child)[1] == before,
              'ACME early request reached route')
        rows.append(dict(case='acme_wire_early_data_rejected', client=early, handler_delta=0))
        args = [str(client), str(port), '13', 'certificate', 'a.example', 'http/1.1', 'identity', 'interactive', str(fixtures)]
        with Consumer(args, directory, env, directory / 'normal-ticket-challenge.json') as session:
            normal = json.loads(session.line())
            session.process.stdin.write(b'challenge\n'); session.process.stdin.flush()
            challenge = json.loads(session.line())
            check(challenge['connected'] and challenge['reused'] == 0 and challenge['certificate_messages'] == 1
                  and challenge['certificate_sha256'] == positive['certificate_sha256'], 'normal ticket bypassed challenge certificate')
            session.process.stdin.write(b'quit\n'); session.process.stdin.flush()
            session.process.wait(timeout=3)
        rows.append(dict(case='acme_normal_session_isolation', normal=normal, challenge=challenge))
        args = [str(client), str(port), '13', 'acme', 'a.example', 'acme-tls/1', 'identity', 'hold-acme', str(fixtures)]
        with Consumer(args, directory, env, directory / 'held-challenge.json') as held:
            check(json.loads(held.line())['certificate_selected'], 'challenge was not selected before removal')
            command(child, 'remove', 'REMOVED')
            held.process.stdin.write(b'continue\n'); held.process.stdin.flush()
            pinned = json.loads(held.line())
            check(pinned['connected'] and pinned['certificate_sha256'] == positive['certificate_sha256'], 'removal changed pinned challenge')
            held.process.wait(timeout=3)
        rows.append(dict(case='acme_selected_handshake_pinned', client=pinned))
        command(child, 'remove', 'REMOVED')
        command(child, 'remove', 'REMOVED')
        probe(port, kind='acme', alpn='acme-tls/1', success=False)
        with connect(port, context(fixtures, '13')) as peer:
            exchange(peer)
        rows.append(dict(case='acme_remove_normal_preserved'))
        command(child, 'race', 'RACED')
        probe(port, kind='acme', alpn='acme-tls/1', success=False)
        with connect(port, context(fixtures, '13')) as peer:
            exchange(peer)
        rows.append(dict(case='acme_concurrent_writers', writers=3, operations=9))
        published = command(child, 'expire').split()
        check(len(published) == 2 and published[0] == 'PUBLISHED', 'expiry receipt absent')
        expires = int(published[1]) / 1000
        probe(port, kind='acme', alpn='acme-tls/1')
        started = time.monotonic()
        while time.time() < expires + .1 and time.monotonic() - started < 12:
            time.sleep(.02)
        observed = time.time()
        check(observed >= expires + .1, 'system clock did not reach actual challenge expiry')
        probe(port, kind='acme', alpn='acme-tls/1', success=False)
        rows.append(dict(case='acme_real_expiry', expires_unix=expires, observed_unix=observed, waited_seconds=time.monotonic() - started))
        child.stop()
    with launch('acme') as child:
        port = child.port()
        check(port != 443, 'wrong-port witness is443')
        command(child, 'publish', 'PUBLISHED')
        probe(port, kind='acme', alpn='acme-tls/1', success=False)
        with connect(port, context(fixtures, '13')) as peer:
            exchange(peer)
        child.stop()
        rows.append(dict(case='acme_wrong_port_rejected', local_port=port))
    for profile in ('mtls-require', 'psk'):
        with launch(profile, 443) as child:
            port = child.port()
            command(child, 'publish', 'PUBLISHED')
            before = counts(child)
            selected = probe(port, kind='acme', alpn='acme-tls/1')[0]
            after = counts(child)
            check(after[1] == before[1] and after[4] == before[4] and selected['certificate_messages'] == 1,
                  'normal authentication leaked into challenge selection')
            child.stop()
        rows.append(dict(case='acme_' + profile + '_auth_isolation', client=selected))
    return rows


def example(executable, loop, directory, env, fixtures):
    with Consumer([str(executable), str(fixtures), loop, '0'], directory, env,
                  directory / 'rotation-example.json') as child:
        port = child.port()
        with connect(port, context(fixtures, '13')) as established:
            old = hashlib.sha256(established.getpeercert(binary_form=True)).hexdigest()
            def request(peer, close=False):
                peer.sendall(b'GET /installed HTTP/1.1\r\nHost: a.example\r\nConnection: ' +
                             (b'close' if close else b'keep-alive') + b'\r\n\r\n')
                body, pending, head = response(peer)
                check(body == b'rotation-example\n' and not pending and head.startswith('HTTP/1.1 200 '),
                      'installed rotation example response differs')
            request(established)
            command(child, 'replace', 'REPLACED')
            request(established)
            with connect(port, context(fixtures, '13'), 'b.example') as fresh:
                new = hashlib.sha256(fresh.getpeercert(binary_form=True)).hexdigest()
                check(new != old, 'installed example did not rotate certificate')
                request(fresh, True)
            command(child, 'invalid', 'PRESERVED')
            with connect(port, context(fixtures, '13'), 'b.example') as preserved:
                check(hashlib.sha256(preserved.getpeercert(binary_form=True)).hexdigest() == new,
                      'installed example rejected candidate changed material')
                request(preserved, True)
            request(established, True)
        child.stop()
        return dict(case='installed_rotation_example', old_certificate_sha256=old, new_certificate_sha256=new,
                    held_requests=3, fresh_verified_requests=2)
