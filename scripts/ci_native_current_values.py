#!/usr/bin/env python3
"""Two original native programs, once each, on one never-merge diagnostic CI run."""
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
import time

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import compiler_sdk
import package_matrix
from package_commands import Commands, runtime_environment
from package_integration import compile_probe

BASE = 'c1b4beb0215f273f1eefbbea2709b117e7f2a7fa'
BRANCH = 'refs/heads/task/FEATURE-ci-native-current-values'
TARGETS = {'Windows': ('external_http1_e2e', 4, 52), 'Darwin': ('tls_psk', 14, 313)}
TRACE_HASHES = {'test/integ/external_http1_e2e_test.cpp': '002a911e38b193ac32c6918af346c896115b2dc10b686de7a1e5bd0a7b3c59f8', 'test/unit/tls_psk_test.cpp': '934eb7f517863721e2836988e853c3226c13ddff5017435a0f4c16fc3c4ea23b'}
ALLOWED = {'.github/workflows/v3-package-validation.yml', 'scripts/ci_native_current_values.py', *TRACE_HASHES}
ENV_KEYS = ('PATH', 'SYSTEMROOT', 'SystemRoot', 'WINDIR', 'COMSPEC', 'PATHEXT',
            'HOME', 'USERPROFILE', 'HOMEDRIVE', 'HOMEPATH', 'TMPDIR', 'TMP', 'TEMP',
            'LANG', 'LC_ALL', 'TZ', 'SDKROOT', 'DEVELOPER_DIR', 'MACOSX_DEPLOYMENT_TARGET',
            'MSYSTEM', 'MSYS', 'MSYS2_PATH_TYPE', 'MSYS2_ARG_CONV_EXCL',
            'V3_PACKAGE_TOOLCHAIN', 'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'ImageOS')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value, exclusive=False):
    with path.open('x' if exclusive else 'w', encoding='utf-8') as output:
        json.dump(value, output, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())


def guard(source):
    host = platform.system()
    approved = os.environ.get('CI_DIAGNOSTIC_APPROVED_SHA', '')
    if (host not in TARGETS or os.environ.get('GITHUB_ACTIONS') != 'true'
            or os.environ.get('GITHUB_EVENT_NAME') != 'workflow_dispatch'
            or os.environ.get('GITHUB_REPOSITORY') != 'etr/libhttpserver'
            or os.environ.get('GITHUB_REF') != BRANCH
            or os.environ.get('GITHUB_RUN_ATTEMPT') != '1'
            or not re.fullmatch('[0-9a-f]{40}', approved)
            or approved != os.environ.get('GITHUB_SHA')
            or not os.environ.get('GITHUB_WORKFLOW_REF', '').endswith(
                '/.github/workflows/v3-package-validation.yml@' + BRANCH)):
        raise RuntimeError('only the exact approved diagnostic dispatch and first attempt are allowed')
    git = lambda *args: subprocess.check_output(['git', *args], cwd=source, text=True).strip()
    if git('rev-parse', 'HEAD') != approved or git('merge-base', 'HEAD', BASE) != BASE:
        raise RuntimeError('checkout or diagnostic base differs')
    if git('status', '--porcelain', '--untracked-files=no'):
        raise RuntimeError('tracked checkout differs from approved commit')
    changed = set(git('diff', '--name-only', BASE, 'HEAD').splitlines())
    if changed != ALLOWED or any(digest(source / name) != sha for name, sha in TRACE_HASHES.items()):
        raise RuntimeError('only the reviewed workflow/helper and exact test traces may differ')
    return dict(base=BASE, head=approved, branch=BRANCH, run_id=os.environ.get('GITHUB_RUN_ID'),
                run_attempt=1, host=host, target=TARGETS[host][0], maximum_invocations=1,
                maximum_seconds_including_retirement=60, execution_seconds=54, retirement_reserve_seconds=6)


def prepare(args, source, directory, env):
    """Use established package setup; build only the chosen library and original program."""
    setup = directory / 'setup'
    setup.mkdir()
    command = Commands(setup)
    host = platform.system()
    mode = 'yes' if host == 'Darwin' else 'no'
    build, prefix = directory / 'build', directory / 'prefix'
    build.mkdir()
    prefix.mkdir()
    provider = args.provider_prefix or directory / 'no-provider'
    configured = dict(env, CC=args.cc, CXX=args.compiler, MAKE=args.make, CXXFLAGS='-O0 -g',
                      CPPFLAGS='', LDFLAGS='', PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join(args.pkg_config_dir))
    if host == 'Darwin':
        if not args.provider_prefix:
            raise RuntimeError('selected private provider required before any original launch')
        configured.update(V3_TLS_CFLAGS='-I' + (provider / 'include').as_posix(),
                          V3_TLS_LIBS='-L' + (provider / 'lib').as_posix() + ' -lssl -lcrypto',
                          LDFLAGS='-Wl,-rpath,' + (provider / 'lib').as_posix())
    bash = shutil.which('bash', path=env['PATH'])
    configure = (source / 'configure').as_posix()
    if host == 'Windows':
        configure = command([bash, '-c', 'cygpath -u "$1"', 'configure-path', configure], build, configured).strip()
    command([bash, configure, '--enable-v3-tls=' + mode, '--disable-examples', '--prefix=' + prefix.as_posix()], build, configured)
    command([args.make, '-j2', '-C', 'src'], build, configured)
    runtime = runtime_environment(env, prefix, provider, [], args.toolchain_prefix)
    compilation = None
    if host == 'Windows':
        command([args.make, '-C', 'src', 'install'], build, configured)
        command([args.make, 'install-pkgconfigDATA'], build, configured)
        runtime.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join([str(prefix / 'lib/pkgconfig')] + args.pkg_config_dir))
        flags = shlex.split(command(['pkg-config', '--cflags', 'libhttpserver'], build, runtime).strip())
        flags += ['-DHTTPSERVER_COMPILATION', '-I' + str(source / 'src'), '-I' + str(source / 'test')]
        libraries = shlex.split(command(['pkg-config', '--libs', 'libhttpserver'], build, runtime).strip()) + ['-lws2_32', '-lmswsock', '-lbcrypt']
        consumer = directory / 'selected-program'
        consumer.mkdir()
        executable = consumer / 'external_http1_e2e.exe'
        units = [source / 'test' / name for name in ('integ/external_http1_e2e_test.cpp', 'parity/response_frame.cpp', 'parity/normalize.cpp')]
        compilation = compile_probe(args.compiler, units, flags, libraries, executable, command, runtime, source, build, prefix, True)
    else:
        command([args.make, '-j2', '-C', 'test', 'tls_psk', 'CXXFLAGS=-O0 -g -DHTTPSERVER_PSK_HELD_VALUES_TRACE'], build, configured)
        executable = build / 'test/tls_psk'
        if (executable.parent / '.libs' / executable.name).is_file():
            executable = executable.parent / '.libs' / executable.name
        runtime['DYLD_LIBRARY_PATH'] = os.pathsep.join([str(build / 'src/.libs'), str(provider / 'lib'), runtime.get('DYLD_LIBRARY_PATH', '')])
        version = command([str(provider / 'bin/openssl'), 'version', '-a'], build, runtime)
        if not version.startswith('OpenSSL 3.5.9 '):
            raise RuntimeError('selected provider version differs; no original launch')
    for name in ('config.h', 'config.log', 'Makefile', 'test/Makefile'):
        shutil.copyfile(build / name, setup / name.replace('/', '-'))
    context = dict(platform=platform.platform(), python=sys.version, uid=os.getuid() if hasattr(os, 'getuid') else None,
                   sdk=compiler_sdk.capture(args.compiler, cwd=build), compiler=command(shlex.split(args.compiler) + ['--version'], build, runtime),
                   executable=str(executable), executable_sha256=digest(executable), compilation=compilation,
                   trace_sha256={name: digest(source / name) for name in TRACE_HASHES},
                   image_os=os.environ.get('ImageOS'), environment={key: runtime.get(key) for key in ('PATH', 'DYLD_LIBRARY_PATH', 'V3_PACKAGE_TOOLCHAIN')})
    context['dependencies'] = command(['objdump', '-p', str(executable)] if host == 'Windows' else ['otool', '-L', str(executable)], build, runtime)
    if host == 'Darwin' and context['sdk'] is None:
        raise RuntimeError('selected Darwin SDK unavailable; no original launch')
    save(directory / 'context.json', context)
    return executable, runtime, source / 'test' if host == 'Windows' else build / 'test'


def capture_once(executable, cwd, env, directory, identity):
    """These two original test programs create threads, not child processes."""
    started = time.monotonic()
    save(directory / 'once.json', dict(identity, state='charged-before-launch', executable_sha256=digest(executable)), exclusive=True)
    child = None
    receipt = dict(identity, state='closed', original_started=False, timed_out=False, retirement_confirmed=False, passed=False)
    try:
        with (directory / 'stdout.bin').open('xb') as output, (directory / 'stderr.bin').open('xb') as errors:
            child = subprocess.Popen([str(executable)], cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=output, stderr=errors)
            receipt.update(original_started=True, pid=child.pid)
            try:
                receipt['exit'] = child.wait(timeout=max(0, started + 54 - time.monotonic()))
            except subprocess.TimeoutExpired:
                receipt['timed_out'] = True
            finally:
                # Act only on the retained direct-child handle/PID, never a name.
                if child.poll() is None:
                    child.kill()
                receipt['exit'] = child.wait(timeout=max(0, started + 60 - time.monotonic()))
                receipt['retirement_confirmed'] = True
        for name in ('stdout.bin', 'stderr.bin'):
            path = directory / name
            receipt[name] = dict(bytes=path.stat().st_size, sha256=digest(path))
            if path.stat().st_size > 1024 * 1024:
                raise RuntimeError('bounded selected log allowance exceeded')
        stdout = (directory / 'stdout.bin').read_text(errors='replace').replace('\r\n', '\n')
        receipt['raw_summary'] = {name: [int(value) for value in re.findall(pattern, stdout, re.M)] for name, pattern in {
            'tests': r'^(\d+) tests executed$', 'checks': r'^(\d+) checks$', 'successes': r'^-> (\d+) successes$',
            'failures': r'^-> (\d+) failures$', 'skipped': r'^-> (\d+) skipped$'}.items()}
        receipt['suite'] = package_matrix.suite_result(stdout, receipt['exit'])
        receipt['passed'] = (not receipt['timed_out'] and receipt['suite']['tests'] == TARGETS[identity['host']][1]
                             and receipt['suite']['checks'] == TARGETS[identity['host']][2])
    except BaseException as error:
        receipt['error'] = str(error)
    finally:
        receipt['seconds'] = time.monotonic() - started
        receipt['within_60_seconds'] = receipt['seconds'] <= 60
        receipt['passed'] = receipt['passed'] and receipt['retirement_confirmed'] and receipt['within_60_seconds']
        save(directory / 'capture.json', receipt)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--make', default='make')
    parser.add_argument('--provider-prefix', type=Path)
    parser.add_argument('--toolchain-prefix', type=Path, required=True)
    parser.add_argument('--pkg-config-dir', action='append', default=[])
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    identity = guard(source)  # Deny before setup or budget allocation outside the approved dispatch.
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    save(directory / 'source.json', dict(identity, tracked_sha256={name: digest(source / name) for name in subprocess.check_output(
        ['git', 'ls-files'], cwd=source, text=True).splitlines() if (source / name).is_file()}))
    (directory / 'instrumentation.diff').write_bytes(subprocess.check_output(['git', 'diff', BASE, 'HEAD', '--', *TRACE_HASHES], cwd=source))
    env = {key: os.environ[key] for key in ENV_KEYS if key in os.environ}
    os.environ.clear()
    os.environ.update(env)
    try:
        executable, runtime, cwd = prepare(args, source, directory, env)
    except Exception as error:
        save(directory / 'setup-failure.json', dict(identity, error=str(error), original_started=False, state='closed'))
        return 1
    return 0 if capture_once(executable, cwd, runtime, directory, identity)['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
