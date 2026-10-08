#!/usr/bin/env python3
"""Build/install independent TLS-off/on packages and run clean consumer traffic.

Requires --provider-prefix pointing to genuine OpenSSL satisfying configure,
and explicit --prerequisite-prefix for transitional MHD/GnuTLS dependencies.
All logs/receipts stay in the supplied fresh --output directory.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import selectors
import shlex
import socket
import subprocess
import time

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('audit-v3-installed-package.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def traffic(executable, mode, cwd, env):
    process = subprocess.Popen([str(executable), mode], cwd=cwd, env=env, text=True,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            if not selector.select(15):
                raise audit.AuditError('consumer startup deadline exceeded')
            line = process.stdout.readline().strip()
        if not line.isdecimal():
            raise audit.AuditError('consumer startup failed: ' + process.stderr.read())
        with socket.create_connection(('127.0.0.1', int(line)), timeout=5) as connection:
            connection.sendall(b'GET /installed HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
            response = b''
            deadline = time.monotonic() + 10
            while True:
                connection.settimeout(max(0.01, deadline - time.monotonic()))
                chunk = connection.recv(4096)
                if not chunk:
                    break
                response += chunk
                if len(response) > 16384 or time.monotonic() >= deadline:
                    raise audit.AuditError('consumer response budget exceeded')
        head, body = response.split(b'\r\n\r\n', 1)
        if not head.startswith(b'HTTP/1.1 200 ') or body != b'installed-v3-ok\n':
            raise audit.AuditError('incorrect installed response: ' + repr(response))
        stdout, stderr = process.communicate('stop\n', timeout=15)
        if process.returncode:
            raise audit.AuditError(f'consumer stop failed {process.returncode}: {stdout} {stderr}')
        return {'command': [str(executable), mode], 'port': int(line), 'response': response.decode(), 'exit': 0}
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--provider-prefix', type=Path, required=True)
    parser.add_argument('--prerequisite-prefix', type=Path, action='append', default=[])
    parser.add_argument('--pkg-config-dir', type=Path, action='append', default=[], help='explicit extra prerequisite metadata directory (e.g. SDK zlib)')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    source = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    if output.exists():
        raise audit.AuditError('--output must be fresh; existing evidence is preserved')
    output.mkdir(parents=True)
    summaries, header_sets = {}, []
    for mode in ('no', 'yes'):
        lane = output / ('tls-' + mode)
        build, prefix, consumer_dir = lane / 'build', lane / 'prefix', lane / 'consumer'
        for path in (build, prefix, consumer_dir):
            path.mkdir(parents=True)
        env = audit.clean_environment()
        env.pop('V3_TLS_CFLAGS', None)
        env.pop('V3_TLS_LIBS', None)
        env['CXX'] = args.compiler
        pkgdirs = [p / 'lib/pkgconfig' for p in args.prerequisite_prefix] + args.pkg_config_dir
        env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join(map(str, pkgdirs)))
        env['CPPFLAGS'] = shlex.join(['-I' + str(p / 'include') for p in args.prerequisite_prefix])
        env['LDFLAGS'] = shlex.join(['-L' + str(p / 'lib') for p in args.prerequisite_prefix])
        env['CXXFLAGS'] = '-O0 -g'
        if mode == 'yes':
            env['V3_TLS_CFLAGS'] = '-I' + str(args.provider_prefix.resolve() / 'include')
            env['V3_TLS_LIBS'] = '-L' + str(args.provider_prefix.resolve() / 'lib') + ' -lssl -lcrypto'
        commands = []
        def command(argv, cwd, command_env):
            result = subprocess.run(argv, cwd=cwd, env=command_env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1200)
            record = {'argv': argv, 'cwd': str(cwd), 'exit': result.returncode,
                      'environment': {key: command_env.get(key) for key in ('CXX', 'CPPFLAGS', 'LDFLAGS', 'CXXFLAGS', 'V3_TLS_CFLAGS', 'V3_TLS_LIBS', 'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR')},
                      'output': result.stdout}
            commands.append(record)
            (lane / 'commands.json').write_text(json.dumps(commands, indent=2))
            if result.returncode:
                raise audit.AuditError('matrix command failed: ' + shlex.join(argv) + '\n' + result.stdout[-3000:])
            return result.stdout
        command([str(source / 'configure'), '--enable-v3-tls=' + mode, '--disable-examples', '--prefix=' + str(prefix)], build, env)
        command(['make', '-j' + str(args.jobs)], build, env)
        command(['make', 'install'], build, env)
        command(['make', 'check-v3-native-linkage', 'check-headers', 'check-hygiene', 'check-v3-installed-package-fixtures'], build, env)
        consumer_env = audit.clean_environment()
        consumer_env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join(map(str, [prefix / 'lib/pkgconfig'] + pkgdirs)))
        metadata = {}
        for key, flags in (('cflags', ['--cflags']), ('libs', ['--libs']), ('static', ['--libs', '--static']), ('version', ['--modversion']), ('pcfiledir', ['--variable=pcfiledir'])):
            metadata[key] = command(['pkg-config'] + flags + ['libhttpserver'], consumer_dir, consumer_env).strip()
        cflags = shlex.split(metadata['cflags'])
        invalid_flags = audit.consumer_flag_violations(cflags + shlex.split(metadata['libs']) + shlex.split(metadata['static']), (source, build))
        if invalid_flags:
            raise audit.AuditError('\n'.join(invalid_flags))
        metadata['file'] = (prefix / 'lib/pkgconfig/libhttpserver.pc').read_text()
        smokes = {}
        for linkage in ('shared', 'static'):
            executable = consumer_dir / ('v3_package_consumer' if linkage == 'shared' else 'static_consumer')
            libraries = shlex.split(metadata['libs' if linkage == 'shared' else 'static']) + ['-L' + str(p / 'lib') for p in args.prerequisite_prefix]
            if linkage == 'static':
                libraries = [str(prefix / 'lib/libhttpserver.a') if flag == '-lhttpserver' else flag for flag in libraries]
            command(shlex.split(args.compiler) + ['-std=c++20'] + cflags + [str(source / 'test/installed/v3_package_consumer.cpp')] + libraries + ['-o', str(executable)], consumer_dir, consumer_env)
            smokes[linkage] = traffic(executable, mode, consumer_dir, consumer_env)
        receipt = audit.audit(prefix, build, consumer_dir / 'v3_package_consumer', mode, 'pre-cutover', args.provider_prefix, args.compiler)
        receipt['consumer_metadata'] = metadata
        receipt['traffic'] = smokes
        (lane / 'audit.json').write_text(json.dumps(receipt, indent=2))
        if receipt['violations']:
            raise audit.AuditError('\n'.join(receipt['violations']))
        strict = audit.graph_violations(receipt['aggregate'], 'strict', mode, 'aggregate', audit.load_policy(), args.provider_prefix)
        if not strict:
            raise audit.AuditError('strict mode did not reject transitional package')
        (lane / 'strict-rejection.json').write_text(json.dumps(strict, indent=2))
        header_sets.append(receipt['headers'])
        summaries[mode] = {'prefix': str(prefix), 'headers': len(receipt['headers']['files']),
                           'nodes': len(receipt['aggregate']['nodes']), 'edges': len(receipt['aggregate']['edges']),
                           'strict_violations': len(strict), 'traffic': smokes}
        print('PASS installed consumer native TLS=' + mode, flush=True)
    if any(header_sets[0][key] != header_sets[1][key] for key in ('files', 'declaration_sha256')):
        raise audit.AuditError('TLS-on/off installed files or effective declarations differ')
    (output / 'summary.json').write_text(json.dumps(summaries, indent=2))
    print('PASS identical installed header sets and effective declarations', flush=True)


if __name__ == '__main__':
    main()
