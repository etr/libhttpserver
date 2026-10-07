#!/usr/bin/env python3
"""Rebuild the task's native sources and consumers with matching instrumentation."""
import argparse
import os
import pathlib
import shlex
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
TESTS = ('http1_conformance native_http1_conformance websocket_conformance '
         'protocol_fuzz_replay native_protocol_memory http1_parser http1_body_mode '
         'http1_body_decoder http1_body_source http1_response_outbox native_http1_e2e '
         'native_http1_parity websocket_codec websocket_utf8 websocket_session '
         'websocket_session_race websocket_progress http1_websocket_handshake '
         'http1_websocket_upgrade websocket_upgrade_decision websocket_drain '
         'websocket_drain_race routing_corpus hooks_corpus basic_auth_corpus '
         'digest_auth_corpus forms_urlencoded_corpus forms_multipart_corpus ip_controls_corpus')


def sources(makefile, target):
    text = makefile.read_text().replace('\\\n', ' ')
    prefix = target + '_SOURCES = '
    for line in text.splitlines():
        if line.startswith(prefix):
            result = shlex.split(line[len(prefix):])
            if not result or any(not item.endswith('.cpp') for item in result):
                raise RuntimeError('unsupported source declaration: ' + target)
            return result
    raise RuntimeError('missing sources: ' + target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True, type=pathlib.Path)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--sanitizer', choices=['address,undefined', 'thread'], required=True)
    parser.add_argument('--python', default='python3')
    parser.add_argument('--independent', action='store_true')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    (build / 'test').mkdir(parents=True, exist_ok=True)
    (build / 'objects').mkdir(exist_ok=True)
    flags = ['-std=c++20', '-pthread', '-O1', '-g', '-fno-omit-frame-pointer',
             '-fsanitize=' + args.sanitizer, '-DHTTPSERVER_COMPILATION',
             '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'test')]
    if args.sanitizer == 'thread':
        flags.append('-DLHS_SANITIZER_OWNS_OPERATOR_NEW')
    macros = [f'-DHTTP1_CORPUS_DIR="{ROOT}/test/conformance/http1"',
              f'-DWEBSOCKET_CORPUS_DIR="{ROOT}/test/conformance/websocket"',
              f'-DPARITY_TRANSCRIPT_DIR="{ROOT}/test/parity/transcripts"',
              f'-DPARITY_DATA_ROOT="{ROOT}/test"']
    with (build / 'build.log').open('w') as log:
        def run(command):
            log.write(shlex.join(command) + '\n'); log.flush()
            subprocess.run(command, check=True, stdout=log, stderr=log, timeout=180)
        run([args.compiler, '--version'])
        run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'])
        objects = []
        for source in sources(ROOT / 'src/Makefile.am', 'libhttpserver_v3core_la'):
            obj = build / 'objects' / (pathlib.Path(source).stem + '.o')
            run([args.compiler, *flags, '-c', str(ROOT / 'src' / source), '-o', str(obj)])
            objects.append(str(obj))
        archive = build / 'native.a'
        if archive.exists():
            archive.unlink()
        # No uninstrumented archive, object or previously built binary enters this lane.
        run(['ar', 'rcs', str(archive), *objects])
        for target in [*TESTS.split(), 'native_websocket_fixture']:
            test_sources = [str(ROOT / 'test' / f) for f in sources(ROOT / 'test/Makefile.am', target)]
            run([args.compiler, *flags, *macros, *test_sources, str(archive), '-o', str(build / 'test' / target)])
    command = [str(ROOT / 'scripts/run-v3-http1-websocket-gates.sh'), '--build-dir', str(build),
               '--tests', TESTS, '--compiler', args.compiler, '--python', args.python]
    if args.independent:
        command.append('--independent')
    environment = os.environ.copy()
    environment["CXXFLAGS"] = shlex.join(flags + macros)
    subprocess.run(command, check=True, timeout=300, env=environment)


if __name__ == '__main__':
    main()
