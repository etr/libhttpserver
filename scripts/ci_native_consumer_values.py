#!/usr/bin/env python3
"""One approved Windows installed consumer case, once, with failure-only values."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
from package_commands import Commands, runtime_environment
from package_integration import compile_probe
from package_process import Consumer
import package_matrix
import package_traffic

BASE = 'b71192c918dffca48fa7a971795160b63d3d199f'
BRANCH = 'refs/heads/task/FEATURE-ci-windows-consumer-values'
TRACE_HASHES = {'examples/host_poll.hpp': '077b7e36bbd3f52de8886e1af2a0b2a0998036c7fccfe2f52fd8cddc91c0c991', 'test/installed/package_consumer.cpp': '8f15dd2a04d5614330e4890f051f88979da461ecce68b381f4f4895abb5ba110'}
ALLOWED = {'.github/workflows/v3-package-validation.yml', 'scripts/ci_native_consumer_values.py', *TRACE_HASHES}
ENV_KEYS = ('PATH', 'SYSTEMROOT', 'SystemRoot', 'WINDIR', 'COMSPEC', 'PATHEXT', 'HOME',
            'USERPROFILE', 'HOMEDRIVE', 'HOMEPATH', 'TMPDIR', 'TMP', 'TEMP', 'LANG', 'LC_ALL',
            'TZ', 'MSYSTEM', 'MSYS', 'MSYS2_PATH_TYPE', 'MSYS2_ARG_CONV_EXCL')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value, exclusive=False):
    with path.open('x' if exclusive else 'w', encoding='utf-8') as output:
        json.dump(value, output, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())


def guard(source):
    approved = os.environ.get('CI_DIAGNOSTIC_APPROVED_SHA', '')
    if (platform.system() != 'Windows' or os.environ.get('GITHUB_ACTIONS') != 'true'
            or os.environ.get('GITHUB_EVENT_NAME') != 'workflow_dispatch'
            or os.environ.get('GITHUB_REPOSITORY') != 'etr/libhttpserver'
            or os.environ.get('GITHUB_REF') != BRANCH
            or os.environ.get('GITHUB_RUN_ATTEMPT') != '1'
            or not re.fullmatch('[0-9a-f]{40}', approved)
            or approved != os.environ.get('GITHUB_SHA')
            or not os.environ.get('GITHUB_WORKFLOW_REF', '').endswith(
                '/.github/workflows/v3-package-validation.yml@' + BRANCH)):
        raise RuntimeError('only the exact approved Windows dispatch and first attempt are allowed')
    git = lambda *args: subprocess.check_output(['git', *args], cwd=source, text=True).strip()
    if git('rev-parse', 'HEAD') != approved or git('merge-base', 'HEAD', BASE) != BASE:
        raise RuntimeError('checkout or diagnostic base differs')
    if git('status', '--porcelain', '--untracked-files=no'):
        raise RuntimeError('tracked checkout differs from approved commit')
    if set(git('diff', '--name-only', BASE, 'HEAD').splitlines()) != ALLOWED:
        raise RuntimeError('unreviewed diagnostic source delta')
    if any(digest(source / name) != sha for name, sha in TRACE_HASHES.items()):
        raise RuntimeError('reviewed failure traces differ')
    return dict(base=BASE, head=approved, branch=BRANCH, run_id=os.environ.get('GITHUB_RUN_ID'),
                run_attempt=1, target='installed shared TLS-no external consumer',
                maximum_original_invocations=1, maximum_seconds_including_retirement=60,
                execution_seconds=54, retirement_reserve_seconds=6)


def prepare(args, source, directory, env):
    setup = directory / 'setup'
    setup.mkdir()
    command = Commands(setup)
    build, prefix = directory / 'build', directory / 'prefix'
    build.mkdir()
    prefix.mkdir()
    configured = dict(env, CC=args.cc, CXX=args.compiler, MAKE='make', CXXFLAGS='-O0 -g',
                      CPPFLAGS='', LDFLAGS='', PKG_CONFIG_PATH='',
                      PKG_CONFIG_LIBDIR=os.pathsep.join(args.pkg_config_dir))
    bash = shutil.which('bash', path=env['PATH'])
    configure = command([bash, '-c', 'cygpath -u "$1"', 'configure-path',
                         (source / 'configure').as_posix()], build, configured).strip()
    command([bash, configure, '--enable-v3-tls=no', '--disable-examples',
             '--prefix=' + prefix.as_posix()], build, configured)
    command(['make', '-j2', '-C', 'src'], build, configured)
    command(['make', '-C', 'src', 'install'], build, configured)
    command(['make', 'install-pkgconfigDATA'], build, configured)
    runtime = runtime_environment(env, prefix, directory / 'no-provider', [], args.toolchain_prefix)
    runtime.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join(
        [str(prefix / 'lib/pkgconfig')] + args.pkg_config_dir))
    flags = shlex.split(command(['pkg-config', '--cflags', 'libhttpserver'], build, runtime).strip())
    libraries = shlex.split(command(['pkg-config', '--libs', 'libhttpserver'], build, runtime).strip())
    libraries += ['-lws2_32', '-lmswsock', '-lbcrypt']
    consumer = directory / 'selected-program'
    consumer.mkdir()
    unit = consumer / 'consumer.cpp'
    shutil.copyfile(source / 'test/installed/package_consumer.cpp', unit)
    shutil.copyfile(source / 'examples/host_poll.hpp', consumer / 'host_poll.hpp')
    executable = consumer / 'package_consumer.exe'
    compilation = compile_probe(args.compiler, [unit], flags, libraries, executable,
                                command, runtime, source, build, prefix, False)
    save(directory / 'context.json', dict(platform=platform.platform(), python=sys.version,
        compiler=command(shlex.split(args.compiler) + ['--version'], build, runtime),
        executable=str(executable), executable_sha256=digest(executable), compilation=compilation,
        trace_sha256={name: digest(source / name) for name in TRACE_HASHES},
        dependencies=command(['objdump', '-p', str(executable)], build, runtime)))
    return executable, consumer, runtime


def capture_once(executable, cwd, env, directory, identity):
    created = []
    receipt = dict(identity, state='closed', original_started=False, timed_out=False,
                   retirement_confirmed=False, passed=False)
    started = None
    timer = None

    def retire(child):
        if child.poll() is None:
            try:
                child.kill()
            except OSError:
                if child.poll() is None:
                    raise
        return child.wait(timeout=max(0, started + 60 - time.monotonic()))

    class BoundedConsumer(Consumer):
        def __init__(self, *args, **kwargs):
            nonlocal started, timer
            if created or args[0] != [str(executable), 'no', 'external']:
                raise RuntimeError('only one exact original consumer invocation is allowed')
            started = time.monotonic()
            save(directory / 'once.json', dict(identity, state='charged-before-launch',
                 executable_sha256=digest(executable)), exclusive=True)
            created.append(self)
            super().__init__(*args, **kwargs)
            receipt.update(original_started=True, pid=self.process.pid)

            def deadline():
                receipt['timed_out'] = True
                try:
                    receipt['watchdog_exit'] = retire(self.process)
                except BaseException as error:
                    receipt['retirement_error'] = str(error)
            timer = threading.Timer(max(0, started + 54 - time.monotonic()), deadline)
            timer.daemon = True
            timer.start()

    original = package_traffic.Consumer
    try:
        package_traffic.Consumer = BoundedConsumer
        cell = dict(tls='no', linkage='shared', loop='external', cases=package_matrix.load()['cases'])
        result = package_traffic.traffic(executable, cell, cwd, env, package_matrix.load())
        save(directory / 'traffic.json', result)
        expected = {'http10', 'http11', 'post', 'persistent', 'pipeline', 'shutdown'}
        receipt['passed'] = {row['case'] for row in result['traffic']} == expected
    except BaseException as error:
        receipt['error'] = str(error)
    finally:
        package_traffic.Consumer = original
        if timer:
            timer.cancel()
        if created and hasattr(created[0], 'process'):
            child = created[0]
            try:
                receipt['exit'] = retire(child.process)
                receipt['retirement_confirmed'] = True
            except BaseException as error:
                receipt['retirement_error'] = str(error)
            for reader in getattr(child, 'readers', []):
                reader.join(timeout=max(0, started + 60 - time.monotonic()))
            for name, buffer in getattr(child, 'buffers', {}).items():
                path = directory / (name + '.bin')
                path.write_bytes(bytes(buffer))
                receipt[name] = dict(bytes=path.stat().st_size, sha256=digest(path))
        if timer:
            timer.join(timeout=max(0, started + 60 - time.monotonic()))
        receipt['seconds'] = time.monotonic() - started if started is not None else 0
        receipt['within_60_seconds'] = receipt['seconds'] <= 60
        receipt['passed'] = (receipt['passed'] and receipt.get('exit') == 0
            and not receipt['timed_out'] and receipt['retirement_confirmed']
            and receipt['within_60_seconds'] and not receipt.get('retirement_error'))
        save(directory / 'capture.json', receipt)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--toolchain-prefix', type=Path, required=True)
    parser.add_argument('--pkg-config-dir', action='append', default=[])
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    identity = guard(source)
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    save(directory / 'source.json', dict(identity, trace_sha256=TRACE_HASHES))
    (directory / 'instrumentation.diff').write_bytes(subprocess.check_output(
        ['git', 'diff', BASE, 'HEAD', '--', *TRACE_HASHES], cwd=source))
    env = {key: os.environ[key] for key in ENV_KEYS if key in os.environ}
    os.environ.clear()
    os.environ.update(env)
    try:
        executable, cwd, runtime = prepare(args, source, directory, env)
    except Exception as error:
        save(directory / 'setup-failure.json', dict(identity, error=str(error),
             original_started=False, state='closed'))
        return 1
    return 0 if capture_once(executable, cwd, runtime, directory, identity)['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
