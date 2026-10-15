#!/usr/bin/env python3
"""Installed public-only certificate TLS/H1 witnesses with independent clients."""
import argparse
from functools import partial
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import socket
import ssl
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts/lib'))
from package_commands import Commands, runtime_environment
from package_integration import compile_probe
from package_matrix import check_linkage, check_scenarios, load
from package_process import Consumer
from package_traffic import response
from public_tls_profiles import scenarios as profile_scenarios, example as rotation_example


def check(value, message):
    if not value:
        raise RuntimeError(message)


def exchange(connection, version='1.1', close=True, observations=None):
    connection.sendall(('GET /installed HTTP/' + version + '\r\nHost: a.example\r\nConnection: ' +
                        ('close' if close else 'keep-alive') + '\r\n\r\n').encode())
    body, pending, head = response(connection)
    check(head.startswith('HTTP/' + version + ' 200 '), 'response protocol differs')
    check(body == b'installed-v3-tls-ok\n' and not pending, 'complete TLS body differs')
    observed = dict(protocol=version, head=head, body=body.decode())
    if observations is not None: observations.append(observed)
    return observed


def command(child, text, expected=None):
    child.process.stdin.write((text + '\n').encode())
    child.process.stdin.flush()
    actual = child.line()
    check(expected is None or actual == expected, 'consumer command outcome: ' + actual)
    return actual


def accepted(child, minimum):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        counts = [int(n) for n in command(child, 'stats').split()]
        if counts[0] >= minimum:
            return counts
        time.sleep(.005)
    raise RuntimeError('accept witness deadline')


def connect(port, context, host='a.example'):
    raw = socket.create_connection(('127.0.0.1', port), timeout=3)
    try:
        return context.wrap_socket(raw, server_hostname=host)
    except BaseException:
        raw.close()
        raise


def recover(port, context):
    # EOF is a transport result, not a raw-child/provider retirement barrier.
    # Capacity remains charged until that retirement; require bounded recovery.
    deadline, attempts = time.monotonic() + 3, 0
    while time.monotonic() < deadline:
        attempts += 1
        try:
            peer = connect(port, context)
        except (ssl.SSLError, ConnectionError):
            time.sleep(.005)
            continue
        with peer:
            exchange(peer)
        return attempts
    raise RuntimeError('handshake admission did not recover after retirement')


def scenarios(executable, loop, mode, directory, env, fixtures, wire=None):
    exchange = partial(globals()['exchange'], observations=wire)
    command_directory = directory / 'credential-commands'
    command_directory.mkdir()
    configuration_commands = Commands(command_directory)
    rows = []
    def launch(name):
        return Consumer([str(executable), mode, loop, str(fixtures), name], directory, env,
                        directory / (loop + '-' + name + '-process.json'))
    if mode == 'no':
        output = configuration_commands([str(executable), mode, loop, str(fixtures), 'off'], directory, env)
        check('PUBLIC_TLS_CONFIGURATION 14' in output, 'TLS-off outcome inventory absent')
        return [dict(case='tls_off_same_source', configuration_checks=14, command_receipt=configuration_commands.records[-1])]
    context = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
    context.set_alpn_protocols(['http/1.1'])
    fallback = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
    # Unknown/absent SNI still verifies the selected certificate's chain, and
    # the independently verified named certificate is compared byte-for-byte.
    fallback.check_hostname = False
    fallback.set_alpn_protocols(['http/1.1'])
    with launch('serve') as child:
        ports = [int(n) for n in child.line().split()]
        check(len(ports) == 2 and all(ports), 'two TLS listeners absent')
        certificates = {}
        for name, index, hostname, expected_name in (
                ('sni_a', 0, 'a.example', 'a'), ('sni_b', 0, 'b.example', 'b'),
                ('second_b', 1, 'b.example', 'b'), ('unknown_sni', 0, 'unknown.example', 'a'),
                ('absent_sni', 0, None, 'a'), ('isolated_store', 1, 'a.example', 'b')):
            verified = hostname == expected_name + '.example'
            with connect(ports[index], context if verified else fallback, hostname) as peer:
                der = peer.getpeercert(binary_form=True)
                if verified:
                    certificates[expected_name] = der
                else:
                    check(der == certificates[expected_name], 'default/listener certificate differs')
                check(peer.selected_alpn_protocol() == 'http/1.1', 'ALPN differs')
                rows.append(dict(case=name, certificate_sha256=hashlib.sha256(der).hexdigest(),
                                 verified_chain=True, verified_hostname=verified, **exchange(peer)))
        for version in ('1.0', '1.1'):
            with connect(ports[0], context) as peer:
                rows.append(dict(case='http' + version, **exchange(peer, version)))
        # One generation is pinned at accept, before any ClientHello bytes.
        count = int(command(child, 'stats').split()[0])
        held_raw = socket.create_connection(('127.0.0.1', ports[0]), timeout=3)
        accepted(child, count + 1)
        with connect(ports[0], context) as established:
            exchange(established, close=False)
            command(child, 'invalid', 'PRESERVED')
            command(child, 'replace', 'REPLACED')
            with context.wrap_socket(held_raw, server_hostname='a.example') as held:
                check(held.getpeercert(binary_form=True) == certificates['a'], 'held handshake generation changed')
                exchange(held)
            exchange(established)
        command(child, 'churn', 'CHURNING')
        for _ in range(8):
            with connect(ports[0], fallback, 'a.example') as peer:
                check(peer.getpeercert(binary_form=True) in certificates.values(), 'torn concurrent publication')
                exchange(peer)
        check(child.line() == 'CHURNED', 'publication worker did not retire')
        command(child, 'replace', 'REPLACED')
        command(child, 'destroy', 'DESTROYED')
        with connect(ports[0], context, 'b.example') as peer:
            check(peer.getpeercert(binary_form=True) == certificates['b'], 'new generation not selected')
            exchange(peer)
        rows.append(dict(case='atomic_replacement_and_caller_lifetime'))
        # Failed TLS must never reach application routing.
        before = int(command(child, 'stats').split()[1])
        with socket.create_connection(('127.0.0.1', ports[0]), timeout=3) as malformed:
            malformed.sendall(b'GET /installed HTTP/1.1\r\n\r\n')
            try:
                malformed.recv(4096)
            except ConnectionResetError:
                pass
        check(int(command(child, 'stats').split()[1]) == before, 'failed handshake reached handler')
        rows.append(dict(case='handshake_failure_no_handler'))
        command(child, 'deny', 'DENIED')
        try:
            with connect(ports[0], context, 'b.example'):
                raise RuntimeError('denied peer negotiated TLS')
        except (ssl.SSLError, ConnectionError):
            pass
        check(int(command(child, 'stats').split()[1]) == before, 'denied peer reached handler')
        rows.append(dict(case='peer_policy_before_tls'))
        child.stop()
    # Each fresh process supplies deterministic capacity/timeout/stop/drain proof.
    with launch('saturation') as child:
        port = int(child.line().split()[0])
        stalled = [socket.create_connection(('127.0.0.1', port), timeout=3) for _ in range(4)]
        accepted(child, 4)
        refusal_started = time.monotonic()
        with socket.create_connection(('127.0.0.1', port), timeout=3) as overflow:
            # Admission refusal must precede the 500 ms handshake timeout.
            # Otherwise an admitted fifth stalled handshake looks like refusal.
            overflow.settimeout(.2)
            check(overflow.recv(1) == b'', 'handshake connection budget bypassed')
        refusal_seconds = time.monotonic() - refusal_started
        for peer in stalled:
            peer.settimeout(3)
            check(peer.recv(1) == b'', 'stalled handshake deadline missed')
            peer.close()
        attempts = recover(port, context)
        rows.append(dict(case='bounded_handshake_timeout_and_recovery', stalled=4,
                         refusal_seconds=refusal_seconds, refusal_deadline_seconds=.2,
                         handshake_deadline_seconds=.5, recovery_attempts=attempts))
        child.stop()
    for scenario in ('timer_budget', 'storage_budget'):
        with launch(scenario) as child:
            port = int(child.line().split()[0])
            with socket.create_connection(('127.0.0.1', port), timeout=3) as held:
                accepted(child, 1)
                with socket.create_connection(('127.0.0.1', port), timeout=3) as excess:
                    excess.settimeout(.2)
                    check(excess.recv(1) == b'', scenario + ' bypassed')
                held.settimeout(3)
                check(held.recv(1) == b'', scenario + ' timeout missed')
            attempts = recover(port, context)
            child.stop()
            rows.append(dict(case=scenario + '_retirement', recovery_attempts=attempts))
    for scenario in ('stop_pending', 'drain_pending', 'handler_stop', 'handler_drain'):
        with launch(scenario) as child:
            port = int(child.line().split()[0])
            if scenario.endswith('pending'):
                peer = socket.create_connection(('127.0.0.1', port), timeout=3)
                accepted(child, 1)
                if scenario == 'drain_pending':
                    command(child, 'drain', 'DRAINED')
                child.stop()
                peer.close()
            else:
                with connect(port, context) as peer:
                    # Keep the peer open until the handler's own shutdown call
                    # returns. Coordinator stop cannot supply this witness.
                    peer.sendall(b'GET /installed HTTP/1.1\r\nHost: a.example\r\n\r\n')
                    deadline = time.monotonic() + 3
                    acknowledgement = 'PENDING'
                    while time.monotonic() < deadline and acknowledgement == 'PENDING':
                        acknowledgement = command(child, 'handler_shutdown')
                        time.sleep(.005)
                    expected = 'STOPPED' if scenario == 'handler_stop' else 'DRAINED'
                    check(acknowledgement == expected, 'handler shutdown did not complete: ' + acknowledgement)
                    check(int(command(child, 'stats').split()[1]) == 1, 'handler count differs')
                child.stop()
            rows.append(dict(case=scenario, **({} if scenario.endswith('pending') else
                             dict(handler_shutdown=acknowledgement))))
    with launch('accept_stop') as child:
        port = int(child.line().split()[0])
        with socket.create_connection(('127.0.0.1', port), timeout=3) as peer:
            check(peer.recv(1) == b'', 'reentrant accept hook did not stop')
        child.stop()
        rows.append(dict(case='accept_hook_safe_stop'))
    with launch('mixed') as child:
        ports = [int(n) for n in child.line().split()]
        check(len(ports) == 3, 'mixed listeners absent')
        with connect(ports[0], context) as peer:
            exchange(peer)
        with socket.create_connection(('127.0.0.1', ports[2]), timeout=3) as peer:
            exchange(peer)
        child.stop()
        rows.append(dict(case='mixed_valid_listeners'))
    for disabled, forbidden, allowed in (('disabled10', '1.0', '1.1'), ('disabled11', '1.1', '1.0')):
        with launch(disabled) as child:
            port = int(child.line().split()[0])
            no_alpn = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
            with connect(port, no_alpn) as peer:
                exchange(peer, allowed)
            before = int(command(child, 'stats').split()[1])
            with connect(port, no_alpn) as peer:
                peer.sendall(('GET /installed HTTP/' + forbidden + '\r\nHost: a.example\r\n\r\n').encode())
                data = peer.recv(4096)
                check(b' 505 ' in data, 'disabled H1 version was not rejected')
            check(int(command(child, 'stats').split()[1]) == before, 'disabled protocol reached handler')
            child.stop()
            rows.append(dict(case=disabled + '_before_handler'))
    with socket.socket() as occupied, socket.socket() as free:
        occupied.bind(('127.0.0.1', 0))
        occupied.listen()
        free.bind(('127.0.0.1', 0))
        earlier, later = free.getsockname()[1], occupied.getsockname()[1]
        free.close()
        output = configuration_commands([str(executable), mode, loop, str(fixtures), 'rollback', str(earlier), str(later)], directory, env)
        check('ROLLBACK_OK' in output, 'partial bind rollback absent')
        with socket.socket() as reusable:
            reusable.bind(('127.0.0.1', earlier))
        rows.append(dict(case='partial_bind_rollback', command_receipt=configuration_commands.records[-1]))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--provider-prefix', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--tls', choices=('yes', 'no'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', default='/usr/bin/clang++-18')
    parser.add_argument('--sanitize', default='')
    parser.add_argument('--source', type=Path, default=ROOT)
    parser.add_argument('--run-id')
    return run(parser.parse_args())


def run(args):
    source = args.source.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    prefix = args.prefix.resolve()
    command_log = Commands(args.output)
    env = runtime_environment(dict(os.environ), prefix, args.provider_prefix, [])
    env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'))
    receipt = dict(version=1, run_id=args.run_id, source=str(source), tls=args.tls, prefix=str(prefix), build=str(args.build.resolve()), cells=[])
    try:
        spec = importlib.util.spec_from_file_location('audit', source / 'scripts/audit-v3-installed-package.py')
        audit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(audit)
        receipt['revision'] = receipt['branch'] = None
        inputs = sorted(path for path in (source / 'src').rglob('*')
                        if path.is_file() and path.suffix in ('.cpp', '.hpp'))
        inputs += [source / name for name in ('src/Makefile.am', 'configure.ac',
                   'scripts/check-v3-public-tls.py', 'scripts/lib/public_tls_profiles.py', 'scripts/lib/package_matrix.py',
                   'test/installed/v3-package-matrix.json', 'test/installed/tls_consumer.cpp',
                   'test/installed/tls_profiles.cpp', 'test/installed/tls_client.cpp', 'test/examples/tls_rotation.cpp')]
        receipt['source_hashes'] = {str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
                                    for path in inputs}
        receipt['source_identity'] = hashlib.sha256(json.dumps(receipt['source_hashes'], sort_keys=True).encode()).hexdigest()
        receipt['compiler_version'] = command_log([args.compiler, '--version'], source, env)
        receipt['client'] = dict(executable=sys.executable, sha256=hashlib.sha256(Path(sys.executable).read_bytes()).hexdigest(),
                                 version=sys.version, ssl=ssl.OPENSSL_VERSION)
        receipt['client']['dependencies'] = audit.dependency_graph([ssl._ssl.__file__])
        receipt['pcfiledir'] = command_log(['pkg-config', '--variable=pcfiledir', 'libhttpserver'], args.output, env).strip()
        check(Path(receipt['pcfiledir']).resolve() == prefix / 'lib/pkgconfig', 'wrong pkg-config package')
        receipt['pkg_config'] = (prefix / 'lib/pkgconfig/libhttpserver.pc').read_text()
        receipt['library_hashes'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in (prefix / 'lib').glob('libhttpserver.*') if path.is_file()}
        cflags = shlex.split(command_log(['pkg-config', '--cflags', 'libhttpserver'], args.output, env))
        check(not audit.consumer_flag_violations(cflags, (source, args.build)), 'contaminated public compile flags')
        consumer = Path(tempfile.mkdtemp(prefix='task193-public-tls-client-'))
        receipt['consumer_directory'] = str(consumer)
        for unit in ('test/installed/tls_consumer.cpp', 'test/installed/tls_profiles.cpp', 'test/installed/tls_client.cpp', 'test/examples/tls_rotation.cpp', 'examples/host_poll.hpp'):
            shutil.copyfile(source / unit, consumer / Path(unit).name)
        fixtures = consumer / 'fixtures'
        shutil.copytree(source / 'test/data/tls_credentials', fixtures)
        (fixtures / 'client-valid-fingerprint.txt').write_text(hashlib.sha256(ssl.PEM_cert_to_DER_cert((fixtures / 'client-valid.pem').read_text())).hexdigest())
        for identity in ('client-valid', 'client-other'):
            chain = (fixtures / (identity + '.pem')).read_bytes()
            if identity == 'client-valid':
                chain += (fixtures / 'client-intermediate.pem').read_bytes()
            (fixtures / (identity + '-chain.pem')).write_bytes(chain)
        independent = consumer / 'independent-client'
        command_log([args.compiler, '-std=c++20', '-I' + str(args.provider_prefix / 'include'),
                     consumer / 'tls_client.cpp', '-L' + str(args.provider_prefix / 'lib'),
                     '-Wl,-rpath,' + str(args.provider_prefix / 'lib'), '-lssl', '-lcrypto', '-o', independent], consumer, env)
        receipt['independent_client'] = dict(executable=str(independent),
            sha256=hashlib.sha256(independent.read_bytes()).hexdigest(), dependencies=audit.dependency_graph([str(independent)]))
        check('libhttpserver' not in json.dumps(receipt['independent_client']['dependencies']), 'client links server library')
        command_log([independent, 'fixtures', fixtures], consumer, env, timeout=10)
        (fixtures / 'malformed.pem').write_text('malformed certificate')
        (fixtures / 'malformed-key.pem').write_text('malformed private key')
        command_log([args.provider_prefix / 'bin/openssl', 'pkey', '-in', fixtures / 'acme-key.pem', '-aes-256-cbc', '-passout', 'pass:task194-test-only', '-out', fixtures / 'encrypted-key.pem'], consumer, env)
        (fixtures / 'extra.pem').write_bytes((fixtures / 'acme.pem').read_bytes() * 2)
        for name in ('mismatch-key', 'past-expiry', 'late-expiry'):
            shutil.copyfile(fixtures / 'acme.pem', fixtures / (name + '.pem'))
        receipt['fixture_hashes'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in fixtures.iterdir() if path.is_file()}
        for linkage in ('shared', 'static'):
            flags = ['--libs'] + (['--static'] if linkage == 'static' else [])
            libraries = shlex.split(command_log(['pkg-config'] + flags + ['libhttpserver'], consumer, env))
            if linkage == 'static':
                libraries = [str(prefix / 'lib/libhttpserver.a') if flag == '-lhttpserver' else flag for flag in libraries]
            if os.name == 'nt':
                libraries += ['-lws2_32', '-lmswsock', '-lbcrypt']
            else:
                libraries += ['-pthread', '-Wl,-rpath,' + str(prefix / 'lib'), '-Wl,-rpath,' + str(args.provider_prefix / 'lib')]
            libraries += shlex.split(args.sanitize)
            executable = consumer / (linkage + ('.exe' if os.name == 'nt' else ''))
            provenance = compile_probe(args.compiler, [consumer / 'tls_consumer.cpp'], cflags + shlex.split(args.sanitize),
                                       libraries, executable, command_log, env, source, args.build.resolve(), prefix, False)
            profiles = consumer / (linkage + '-profiles' + ('.exe' if os.name == 'nt' else ''))
            profile_compile = compile_probe(args.compiler, [consumer / 'tls_profiles.cpp'], cflags + shlex.split(args.sanitize),
                                           libraries, profiles, command_log, env, source, args.build.resolve(), prefix, False)
            example = consumer / (linkage + '-rotation-example' + ('.exe' if os.name == 'nt' else ''))
            example_compile = compile_probe(args.compiler, [consumer / 'tls_rotation.cpp'], cflags + shlex.split(args.sanitize),
                                           libraries, example, command_log, env, source, args.build.resolve(), prefix, False)
            example_graph = audit.dependency_graph([str(example)])
            check_linkage(example_graph, linkage, prefix)
            profile_graph = audit.dependency_graph([str(profiles)])
            check_linkage(profile_graph, linkage, prefix)
            graph = audit.dependency_graph([str(executable)])
            check_linkage(graph, linkage, prefix)
            for loop in ('managed', 'external'):
                cell = args.output / (linkage + '-' + loop)
                cell.mkdir()
                result = dict(linkage=linkage, loop=loop, compile=provenance, dependencies=graph)
                result['wire'] = []
                result['scenarios'] = scenarios(executable, loop, args.tls, cell, env, fixtures, result['wire'])
                profile_rows = profile_scenarios(profiles, independent, loop, args.tls, cell, env, fixtures)
                result['profile_compile'], result['profile_dependencies'] = profile_compile, profile_graph
                if args.tls == 'yes':
                    result['scenarios'] += profile_rows
                    result['scenarios'].append(rotation_example(example, loop, cell, env, fixtures))
                    result['example_compile'], result['example_dependencies'] = example_compile, example_graph
                else:
                    result['profile_scenarios'] = profile_rows
                receipt['cells'].append(result)
                print('PASS public TLS', args.tls, linkage, loop, len(result['scenarios']), flush=True)
        check(len(receipt['cells']) == 4, 'incomplete public matrix')
        expected = load()['public_tls_scenarios'] if args.tls == 'yes' else ['tls_off_same_source']
        for cell in receipt['cells']:
            check_scenarios(cell['scenarios'], expected)
        receipt['status'] = 'passed'
    except BaseException as error:
        receipt.update(status='failed', error=str(error))
        raise
    finally:
        (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


if __name__ == '__main__':
    main()
