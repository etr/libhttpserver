#!/usr/bin/env python3
"""Single approved four-target diagnostic batch. Never merge this branch into v3."""
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
import compiler_sdk
import package_matrix
import package_process
import package_traffic
from package_commands import Commands, _CommandTree, _WindowsJob, runtime_environment
from package_integration import compile_probe

BASE = '37095c23751960b6e1adcad95d7b3c4c34c64351'
IDENTITY = 'FEATURE-ci-native-missing-receipts'
TARGETS = {'consumer': (30, 30, None), 'native_http1_e2e': (190, 180, 62),
           'tls_psk': (60, 60, 14), 'quic_interop_tls': (60, 60, 4)}
PLATFORMS = {'Windows': ('consumer', 'native_http1_e2e'), 'Darwin': ('tls_psk', 'quic_interop_tls')}
# Targets/tools inherit only selected execution setup. Never inherit CI credentials.
ENV_KEYS = ('PATH', 'SYSTEMROOT', 'SystemRoot', 'WINDIR', 'COMSPEC', 'PATHEXT',
            'HOME', 'USERPROFILE', 'HOMEDRIVE', 'HOMEPATH', 'TMPDIR', 'TMP', 'TEMP', 'TEMPDIR',
            'LANG', 'LC_ALL', 'TZ', 'SDKROOT', 'DEVELOPER_DIR', 'MACOSX_DEPLOYMENT_TARGET',
            'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'V3_PACKAGE_DLL_DIRS',
            'V3_PACKAGE_TOOLCHAIN', 'V3_PACKAGE_PREREQUISITES', 'V3_PROVIDER_PREFIX',
            'MSYSTEM', 'MSYS', 'MSYS2_PATH_TYPE', 'MSYS2_ARG_CONV_EXCL',
            'GIT_CONFIG_COUNT', 'GIT_CONFIG_KEY_0', 'GIT_CONFIG_VALUE_0')
OUTPUT_CAP = 1024 * 1024


def target_environment(environment):
    return {key: environment[key] for key in ENV_KEYS if key in environment}


def save(path, value, exclusive=False):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if exclusive:
        with path.open('x', encoding='utf-8') as output:
            json.dump(value, output, indent=2)
            output.write('\n')
            output.flush()
            os.fsync(output.fileno())
    else:
        temporary = path.with_suffix('.tmp')
        with temporary.open('w', encoding='utf-8') as output:
            json.dump(value, output, indent=2)
            output.write('\n')
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)


def run_batch(directory, names, launch):
    names = tuple(names)
    if (not names or len(set(names)) != len(names) or any(name not in TARGETS for name in names)
            or names != tuple(name for name in TARGETS if name in names)):
        raise ValueError('invalid, duplicate or reordered target selection')
    directory.mkdir(parents=True, exist_ok=True)
    save(directory / 'once.json', {'id': IDENTITY, 'pid': os.getpid()}, exclusive=True)
    ledger = dict(id=IDENTITY, state='running', runner_pid=os.getpid(), attempts=0,
                  charged_seconds=0, outcomes={}, targets=names)
    try:
        for name in names:
            case = directory / name
            case.mkdir()
            ledger.update(active_target=name, active_case=str(case.resolve()))
            ledger['attempts'] += 1
            ledger['charged_seconds'] += TARGETS[name][0]
            if ledger['attempts'] > 4 or ledger['charged_seconds'] > 340:
                raise RuntimeError('approved budget exhausted')
            save(directory / 'budget.json', ledger)
            started = time.monotonic()
            try:
                result = launch(name, case)
                if not isinstance(result, dict) or type(result.get('passed')) is not bool:
                    raise ValueError('missing or invalid target outcome')
            except Exception as error:
                result = dict(passed=False, error=str(error))
            result.update(target=name, duration_seconds=time.monotonic() - started,
                          charged_seconds=TARGETS[name][0])
            save(case / 'outcome.json', result)
            ledger['outcomes'][name] = result
    finally:
        ledger.update(state='closed', active_target=None, active_case=None,
                      passed=len(ledger['outcomes']) == len(names) and
                      all(row['passed'] for row in ledger['outcomes'].values()))
        save(directory / 'budget.json', ledger)
    return ledger


def consume_permit(directory, target, parent=None):
    ledger = json.loads((directory.parent / 'budget.json').read_text())
    if (ledger.get('id') != IDENTITY or ledger.get('state') != 'running'
            or ledger.get('runner_pid') != (os.getppid() if parent is None else parent)
            or ledger.get('active_target') != target
            or Path(ledger.get('active_case', '')).resolve() != directory.resolve()
            or target not in TARGETS or not 1 <= ledger.get('attempts', 0) <= 4
            or not TARGETS[target][0] <= ledger.get('charged_seconds', 0) <= 340):
        raise RuntimeError('no active parent target launch permit')
    save(directory / 'consumed.json', dict(target=target, worker_pid=os.getpid()), exclusive=True)


class CaptureTree(_CommandTree):
    """Use the existing job/group ownership, keeping stdout and stderr separate."""
    def __init__(self, argv, cwd, env):
        self.job = _WindowsJob() if os.name == 'nt' else None
        self.child = None
        try:
            options = dict(creationflags=0x0004) if self.job else dict(start_new_session=True)
            self.child = subprocess.Popen(argv, cwd=cwd, env=target_environment(env),
                                          stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                          stderr=subprocess.PIPE, **options)
            if self.job:
                self.job.attach(self.child)
        except BaseException:
            try:
                self.terminate()
            finally:
                self.close()
            raise

    def terminate(self, deadline=None):
        if not self.job:
            return super().terminate(deadline)
        deadline = time.monotonic() + 5 if deadline is None else deadline
        self.job.terminate()
        if self.child is not None:
            # Covers the exceptional path before job assignment as well.
            if self.child.poll() is None:
                self.child.kill()
            self.child.wait(timeout=max(.001, deadline - time.monotonic()))
        accounting = self.job.Accounting()
        while time.monotonic() < deadline:
            self.job.check(self.job.api.QueryInformationJobObject(
                self.job.handle, 1, self.job.ctypes.byref(accounting),
                self.job.ctypes.sizeof(accounting), None))
            if accounting.active == 0:
                return
            time.sleep(min(.01, max(0, deadline - time.monotonic())))
        raise RuntimeError('owned job retirement deadline exceeded')


def windows_process_identity(api, handle, pid):
    import ctypes
    from ctypes import wintypes
    created, exited, kernel, user = (wintypes.FILETIME() for _ in range(4))
    api.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    api.GetProcessTimes.restype = wintypes.BOOL
    if not api.GetProcessTimes(handle, *(ctypes.byref(item) for item in (created, exited, kernel, user))):
        raise ctypes.WinError(ctypes.get_last_error())
    count = wintypes.DWORD()
    api.GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    api.GetProcessHandleCount.restype = wintypes.BOOL
    handles_ok = api.GetProcessHandleCount(handle, ctypes.byref(count))
    handle_error = ctypes.get_last_error() if not handles_ok else None
    return dict(pid=pid, creation_filetime=(created.dwHighDateTime << 32) | created.dwLowDateTime,
                handle_count=count.value if handles_ok else None, handle_error=handle_error)


def windows_identity(tree):
    """Inspect held primary / owned job handles; never act on an unverified PID."""
    import ctypes
    from ctypes import wintypes
    api = tree.job.api
    primary = windows_process_identity(api, wintypes.HANDLE(int(tree.child._handle)), tree.child.pid)
    class Members(ctypes.Structure):
        _fields_ = [('assigned', wintypes.DWORD), ('count', wintypes.DWORD),
                    ('pids', ctypes.c_size_t * 128)]
    members = Members()
    members_ok = api.QueryInformationJobObject(tree.job.handle, 3, ctypes.byref(members),
                                               ctypes.sizeof(members), None)
    primary.update(owned_job_pids=list(members.pids[:min(members.count, 128)]) if members_ok else None,
                   members_truncated=members.count > 128, members_query_ok=bool(members_ok), descendants=[])
    api.IsProcessInJob.argtypes = [wintypes.HANDLE, wintypes.HANDLE, ctypes.POINTER(wintypes.BOOL)]
    api.IsProcessInJob.restype = wintypes.BOOL
    for pid in primary['owned_job_pids'] or []:
        if pid == tree.child.pid:
            continue
        handle = api.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            primary['descendants'].append(dict(pid=pid, unavailable_error=ctypes.get_last_error()))
            continue
        try:
            owned = wintypes.BOOL()
            if not api.IsProcessInJob(handle, tree.job.handle, ctypes.byref(owned)) or not owned.value:
                primary['descendants'].append(dict(pid=pid, unavailable='job membership changed'))
                continue
            primary['descendants'].append(windows_process_identity(api, handle, pid))
        except OSError as error:
            primary['descendants'].append(dict(pid=pid, unavailable=str(error)))
        finally:
            api.CloseHandle(handle)
    return primary


def process_identity(tree):
    if tree.job:
        return windows_identity(tree)
    # Held unreaped group leader anchors ownership even after primary exit.
    if sys.platform.startswith('linux'):
        fields = (Path('/proc') / str(tree.child.pid) / 'stat').read_text().rsplit(')', 1)[1].split()
        return dict(pid=tree.child.pid, pgid=int(fields[2]), start_ticks=fields[19])
    result = subprocess.run(['ps', '-o', 'pid=,ppid=,pgid=,lstart=,stat=', '-p', str(tree.child.pid)],
                            capture_output=True, text=True, timeout=1, env=target_environment(os.environ))
    return dict(pid=tree.child.pid, ps=result.stdout[:4096], ps_exit=result.returncode,
                ownership='unreaped-session-leader')



def stack_command(tool, pid, directory):
    # Microsoft documents -pvr as noninvasive without suspending the target.
    return [tool, '-pvr', '-pd', '-sins', '-y', str(directory), '-p', str(pid), '-c', '~* k; qd']


def noninvasive_stack(tree, directory, deadline):
    if getattr(tree, 'stack_attempted', False):
        return dict(status='already attempted; no retry')
    tree.stack_attempted = True
    candidates = [shutil.which('cdb.exe'),
                  'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe']
    tool = next((str(Path(path).resolve()) for path in candidates if path and Path(path).is_file()), None)
    if not tool:
        return dict(status='unavailable', reason='no existing CDB; no debugger installed by this task',
                    searched=candidates)
    if tree.exited(deadline) or deadline - time.monotonic() < 4:
        return dict(status='unavailable', reason='primary exited or insufficient diagnostic allowance', tool=tool)
    output = directory / 'stack'
    output.mkdir()
    command = stack_command(tool, tree.child.pid, output)
    # Separate owned tool tree: its deadline/retirement remain inside the target allowance.
    result = run_program(command, output, os.environ, cap=2, gate=1.5, cleanup=.4)
    return dict(status='captured' if result['passed'] else 'unavailable-or-incomplete',
                tool=tool, tool_sha256=digest(tool), result=result,
                qualification='running threads are not suspended; stacks can change during capture',
                documentation='https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/cdb-command-line-options')


def diagnostic_snapshot(tree, directory, state, deadline):
    row = dict(state, monotonic=time.monotonic())
    try:
        row['identity'] = process_identity(tree)
    except Exception as error:
        row['identity_error'] = str(error)
    if tree.job:
        # Only the selected TLS-off primary can receive an optional stack probe.
        row['available_tools'] = {name: shutil.which(name) for name in ('procdump64.exe', 'procdump.exe', 'cdb.exe')}
        if Path(tree.child.args[0]).name == 'native_http1_e2e.exe':
            row['stack_capture'] = noninvasive_stack(tree, directory, min(deadline, state['gate_deadline']))
        else:
            row['stack_capture'] = 'not selected for this diagnostic tool process'
        row['dump_capture'] = 'unavailable: no bounded noninvasive process dumper configured'
    else:
        row['stack_capture'] = 'unavailable: task does not attach a debugger'
    row['remaining_seconds'] = max(0, deadline - time.monotonic())
    return row


def run_program(argv, directory, env, cap, gate, cleanup=6, cwd=None):
    """Bound execution + diagnostics + retirement inside this target's allowance."""
    if not 0 < cleanup < cap or not 0 < gate <= cap:
        raise ValueError('invalid target deadline')
    started = time.monotonic()
    deadline = started + cap
    tree = CaptureTree(argv, directory if cwd is None else cwd, env)
    result = dict(passed=False, pid=tree.child.pid, command=argv,
                  original_gate_seconds=gate, cap_seconds=cap, primary_exit_observed=False,
                  cwd=str(directory if cwd is None else cwd))
    eof = {'stdout': False, 'stderr': False}
    buffers = {'stdout': bytearray(), 'stderr': bytearray()}
    errors = []
    snapshots = []
    readers = []
    def drain(name):
        pipe = getattr(tree.child, name)
        try:
            with (directory / (name + '.bin')).open('wb') as output:
                while chunk := os.read(pipe.fileno(), 4096):
                    room = OUTPUT_CAP - len(buffers[name])
                    output.write(chunk[:room])
                    output.flush()
                    buffers[name].extend(chunk[:room])
                    if len(chunk) > room:
                        errors.append(name + ' output cap exceeded')
                        return
                eof[name] = True
        except OSError as error:
            errors.append(name + ': ' + str(error))
        finally:
            pipe.close()
    try:
        result['process_identity'] = process_identity(tree)
        for name in eof:
            thread = threading.Thread(target=drain, args=(name,), daemon=True)
            readers.append(thread)
            thread.start()
        summary_seen = None
        diagnostic_reasons = set()
        while True:
            now = time.monotonic()
            exited = tree.exited(deadline)
            if exited and not result['primary_exit_observed']:
                result.update(primary_exit_observed=True, primary_exit_seconds=now - started)
            complete = exited and all(eof.values())
            if complete or now >= started + gate:
                result['original_gate_failed'] = not complete or now - started > gate
            if b'tests executed' in buffers['stdout'] and summary_seen is None:
                summary_seen = now
            state = dict(primary_exit=exited, stdout_eof=eof['stdout'], stderr_eof=eof['stderr'],
                         elapsed_seconds=now - started, gate_deadline=started + gate)
            reasons = set()
            if summary_seen is not None and now >= summary_seen + .5:
                reasons.add('summary-delay')
            if exited and not all(eof.values()):
                reasons.add('primary-exit-before-eof')
            if now >= started + max(0, gate - 1):
                reasons.add('before-original-gate')
            if reasons - diagnostic_reasons and not complete:
                state['reasons'] = sorted(reasons - diagnostic_reasons)
                snapshots.append(diagnostic_snapshot(tree, directory, state, deadline))
                save(directory / 'snapshots.json', snapshots)
                diagnostic_reasons.update(reasons)
            if complete:
                result['passed'] = tree.child.returncode in (None, 0) and not result['original_gate_failed']
                break
            if errors or now >= deadline - cleanup:
                result['error'] = '; '.join(errors) if errors else 'hard retirement reserve reached'
                break
            time.sleep(.01)
    except Exception as error:
        result['error'] = str(error)
    finally:
        # The existing helper retains the unreaped POSIX leader / Windows job,
        # so retirement never signals a recycled PID or another process tree.
        try:
            tree.terminate(deadline=deadline - .1)
        except Exception as error:
            errors.append('retirement: ' + str(error))
        for reader in readers:
            reader.join(timeout=max(0, deadline - time.monotonic()))
        tree.close()
        result.update(original_exit=tree.child.returncode, stdout_eof=eof['stdout'], stderr_eof=eof['stderr'],
                      original_gate_failed=result.get('original_gate_failed', True),
                      duration_seconds=time.monotonic() - started)
        result['passed'] = result['passed'] and tree.child.returncode == 0 and not errors and all(eof.values())
        if errors:
            result['cleanup_errors'] = errors
        save(directory / 'snapshots.json', snapshots)
        save(directory / 'process.json', result)
    return result


def check_suite(output, status, expected):
    receipt = package_matrix.suite_result(output, status)
    if receipt['tests'] != expected:
        raise ValueError('original suite test count differs: ' + str(receipt['tests']))
    return receipt


class TeeBuffer(bytearray):
    def __init__(self, output):
        super().__init__()
        self.output = output

    def extend(self, data):
        self.output.write(data)
        self.output.flush()
        return super().extend(data)


class CaptureConsumer(package_process.Consumer):
    """Add raw receipts around the unchanged Consumer traffic/stop operations."""
    def __init__(self, *args, **kwargs):
        self.directory = Path(args[1])
        self.shutdown_error = None
        super().__init__(*args, **kwargs)
        save(self.directory / 'consumer-start.json', dict(pid=self.process.pid, command=self.command))

    def drain(self, name):
        with (self.directory / ('consumer-' + name + '.bin')).open('wb') as output:
            self.buffers[name] = TeeBuffer(output)
            super().drain(name)

    def __exit__(self, kind, error, traceback):
        if error is not None:
            if str(error).startswith('consumer shutdown'):
                self.shutdown_error = str(error)
            elif self.process.poll() is not None and self.process.returncode:
                self.shutdown_error = 'consumer shutdown failed: ' + str(self.process.returncode)
        super().__exit__(kind, error, traceback)
        receipt = json.loads(self.receipt.read_text())
        receipt.update(pid=self.process.pid, traffic_error=str(error) if error else None,
                       shutdown_error=self.shutdown_error)
        save(self.receipt, receipt)


def check_trace(stderr):
    headers = re.findall(r'^WAIT_CAPTURE clock=QPC frequency=(\d+) rows=(\d+) dropped=(\d+)$', stderr, re.M)
    rows = re.findall(r'^WAIT_CAPTURE seq=(\d+) ticks=(\d+) (.+)$', stderr, re.M)
    if (len(headers) != 1 or int(headers[0][0]) <= 0 or int(headers[0][1]) != len(rows)
            or int(headers[0][2]) != 0 or len(rows) > 80):
        raise ValueError('incomplete bounded native wait receipt')
    sequences = [int(row[0]) for row in rows]
    if not sequences or sequences != sorted(set(sequences)) or min(sequences) <= 0:
        raise ValueError('invalid native trace sequence')
    events = {name: [int(seq) for seq, _, label in rows if label == name] for name in
              ('main_stopping_before', 'main_stopping_after', 'request_stop_begin',
               'request_stop_end', 'host_stop_begin', 'host_stop_end', 'main_joined')}
    if any(len(values) != 1 for values in events.values()):
        raise ValueError('missing native stop witness')
    for first, second in (('main_stopping_before', 'main_stopping_after'),
                          ('main_stopping_after', 'request_stop_begin'),
                          ('request_stop_begin', 'request_stop_end'),
                          ('host_stop_begin', 'host_stop_end'), ('host_stop_end', 'main_joined'),
                          ('request_stop_end', 'main_joined')):
        if events[first][0] >= events[second][0]:
            raise ValueError('invalid native stop chronology')
    if any(not any(label == required for _, _, label in rows) for required in ('dispatch_begin', 'dispatch_end')):
        raise ValueError('native dispatch witness absent')
    if any(label in ('host_catch', 'wait_failure') for _, _, label in rows):
        raise ValueError('original native wait/host failure preserved')


def traffic_worker(directory, executable):
    consume_permit(directory, 'consumer')
    if platform.system() != 'Windows':
        raise RuntimeError('consumer target requires native Windows')
    package_traffic.Consumer = CaptureConsumer
    result = dict(passed=False)
    try:
        result['traffic'] = package_traffic.traffic(executable,
            dict(tls='no', linkage='shared', loop='external'), directory,
            target_environment(os.environ), package_matrix.load())
        process = json.loads((directory / 'shared-external-process.json').read_text())
        check_trace(process['stderr'].replace('\r\n', '\n'))
        for name in ('stdout', 'stderr'):
            if (directory / ('consumer-' + name + '.bin')).read_bytes().decode(errors='replace') != process[name]:
                raise ValueError('raw consumer pipe receipt mismatch: ' + name)
        result['passed'] = True
    except Exception as error:
        result['error'] = str(error)
    save(directory / 'traffic.json', result)
    return 0 if result['passed'] else 1


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def prepare(args, source, directory, env):
    """Build just the selected native lane/programs; execute no selected target."""
    setup = directory / 'setup'
    setup.mkdir(parents=True)
    command = Commands(setup)
    host = platform.system()
    mode = 'no' if host == 'Windows' else 'yes'
    build, prefix = directory / 'build', directory / 'prefix'
    build.mkdir()
    prefix.mkdir()
    provider = args.provider_prefix or directory / 'no-provider'
    build_env = dict(env, CXX=args.compiler, CC=args.cc, MAKE=args.make, CXXFLAGS='-O0 -g',
                     CPPFLAGS='', LDFLAGS='', PKG_CONFIG_PATH='',
                     PKG_CONFIG_LIBDIR=os.pathsep.join(args.pkg_config_dir))
    if mode == 'yes':
        build_env.update(V3_TLS_CFLAGS='-I' + (provider / 'include').as_posix(),
                         V3_TLS_LIBS='-L' + (provider / 'lib').as_posix() + ' -lssl -lcrypto',
                         LDFLAGS='-Wl,-rpath,' + (provider / 'lib').as_posix())
    bash = shutil.which('bash', path=env['PATH'])
    configure = (source / 'configure').as_posix()
    if host == 'Windows':
        configure = command([bash, '-c', 'cygpath -u "$1"', 'configure-path', configure], build, build_env).strip()
    command([bash, configure, '--enable-v3-tls=' + mode, '--disable-examples',
             '--prefix=' + prefix.as_posix()], build, build_env)
    command([args.make, '-j2', '-C', 'src'], build, build_env)
    runtime = runtime_environment(env, prefix, provider, [], args.toolchain_prefix)
    runtime['V3_PROVIDER_PREFIX'] = str(provider)
    programs, compiles = {}, {}
    if host == 'Windows':
        command([args.make, '-C', 'src', 'install'], build, build_env)
        # Top-level pc file installation does not enter unrelated test/doc targets.
        command([args.make, 'install-pkgconfigDATA'], build, build_env)
        runtime.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join(
            [str(prefix / 'lib/pkgconfig')] + args.pkg_config_dir))
        cflags = shlex.split(command(['pkg-config', '--cflags', 'libhttpserver'], build, runtime).strip())
        libraries = shlex.split(command(['pkg-config', '--libs', 'libhttpserver'], build, runtime).strip())
        libraries += ['-lws2_32', '-lmswsock', '-lbcrypt']
        consumer = directory / 'consumer-build'
        consumer.mkdir()
        for name, original in [('consumer.cpp', 'test/installed/package_consumer.cpp'),
                               ('host_poll.hpp', 'examples/host_poll.hpp')]:
            shutil.copyfile(source / original, consumer / name)
        definitions = {'consumer': [consumer / 'consumer.cpp'],
                       'native_http1_e2e': [source / 'test' / unit for unit in
                                          package_matrix.load()['suites']['native_http1_e2e']['sources']]}
        for name, units in definitions.items():
            executable = consumer / (name + '.exe')
            flags = cflags if name == 'consumer' else cflags + [
                '-DHTTPSERVER_COMPILATION', '-I' + str(source / 'src'), '-I' + str(source / 'test'),
                '-DPARITY_DATA_ROOT="' + (source / 'test').as_posix() + '"',
                '-DPARITY_TRANSCRIPT_DIR="' + (source / 'test/parity/transcripts').as_posix() + '"',
                '-DTLS_TEST_DIR="' + (source / 'test').as_posix() + '"']
            compiles[name] = compile_probe(args.compiler, units, flags, libraries, executable, command, runtime,
                                           source, build, prefix, name != 'consumer')
            programs[name] = executable
    else:
        command([args.make, '-j2', '-C', 'test', 'tls_psk', 'quic_interop_tls'], build, build_env)
        programs = {name: build / 'test' / name for name in PLATFORMS[host]}
    context = dict(platform=platform.platform(), python=sys.version, python_path=sys.executable,
                   uid=os.getuid() if hasattr(os, 'getuid') else None,
                   environment=target_environment(runtime), build_environment={key: build_env.get(key) for key in
                       ('CXX', 'CC', 'MAKE', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'V3_TLS_CFLAGS', 'V3_TLS_LIBS',
                        'PKG_CONFIG_LIBDIR')},
                   sdk=compiler_sdk.capture(args.compiler, cwd=build), compiles=compiles,
                   programs={name: dict(path=str(path), sha256=digest(path)) for name, path in programs.items()})
    for name, argv in [('compiler', shlex.split(args.compiler) + ['--version']),
                       ('linker', ['ld', '--version'] if host == 'Windows' else ['xcrun', '--show-sdk-path']),
                       ('system', ['uname', '-a'])]:
        context[name] = command(argv, build, runtime)
    artifacts = [build / 'config.h', build / 'config.log', build / 'Makefile', build / 'test/Makefile']
    artifacts += [path for path in (prefix.rglob('*') if host == 'Windows' else (build / 'src/.libs').glob('*'))
                  if path.is_file() and path.suffix.lower() in ('.dll', '.so', '.dylib', '.a')]
    if args.toolchain_prefix:
        artifacts += [path for path in (args.toolchain_prefix / ('bin' if host == 'Windows' else 'lib/c++')).glob('*')
                      if path.is_file() and (path.suffix.lower() in ('.dll', '.dylib') and
                          any(word in path.name for word in ('c++', 'gcc', 'winpthread', 'unwind')))]
    if mode == 'yes':
        artifacts += [path for path in (provider / 'lib').glob('*') if path.is_file()]
        artifacts += [provider / 'include/openssl/opensslv.h']
    compiler = shutil.which(shlex.split(args.compiler)[0], path=runtime['PATH'])
    if compiler:
        artifacts.append(Path(compiler))
    for path in (build / 'config.h', build / 'config.log', build / 'Makefile', build / 'test/Makefile'):
        shutil.copyfile(path, setup / (path.parent.name + '-' + path.name))
    if host == 'Darwin' and context['sdk'] is None:
        raise RuntimeError('selected SDK discovery unavailable; no native target launched')
    for name, executable in programs.items():
        context[name + '_dependencies'] = command(
            ['objdump', '-p', str(executable)] if host == 'Windows' else ['otool', '-L', str(executable)],
            build, runtime)
    if mode == 'yes':
        context['provider_version'] = command([str(provider / 'bin/openssl'), 'version', '-a'], build, runtime)
        if not context['provider_version'].startswith('OpenSSL 3.5.9 '):
            raise RuntimeError('actual selected provider differs from OpenSSL 3.5.9')
        artifacts.append(provider / 'bin/openssl')
    artifacts.append(Path(sys.executable))
    artifacts += [source / path for path in ('configure', 'config.h.in', 'Makefile.in', 'src/Makefile.in', 'test/Makefile.in')]
    context['artifact_sha256'] = {str(path): digest(path) for path in artifacts if path.is_file()}
    save(directory / 'context.json', context)
    return programs, runtime


def execute(args):
    host = platform.system()
    if host not in PLATFORMS or os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('approved native CI context required; preparation does not execute native cases')
    if os.environ.get('GITHUB_RUN_ATTEMPT') != '1':
        raise RuntimeError('workflow retry is outside the approved budget')
    if os.environ.get('GITHUB_REF') != 'refs/heads/task/' + IDENTITY:
        raise RuntimeError('diagnostic branch identity differs')
    source = Path(__file__).resolve().parents[1]
    actual_sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source,
                                         env=target_environment(os.environ)).decode().strip()
    if actual_sha != os.environ.get('CI_DIAGNOSTIC_APPROVED_SHA'):
        raise RuntimeError('caller-approved source commit differs from checkout')
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    env = target_environment(os.environ)
    # Shared SDK discovery uses os.environ for its compiler subprocesses too.
    os.environ.clear()
    os.environ.update(env)
    try:
        if subprocess.check_output(['git', 'merge-base', 'HEAD', BASE], cwd=source, env=env).decode().strip() != BASE:
            raise RuntimeError('approved source base differs')
        source_files = subprocess.check_output(['git', 'ls-files'], cwd=source, env=env).decode().splitlines()
        save(directory / 'source.json', dict(base=BASE, head=subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=source, env=env).decode().strip(),
            hashes={name: digest(source / name) for name in source_files if (source / name).is_file()}))
        diff = subprocess.check_output(['git', 'diff', BASE, '--', 'examples/host_poll.hpp',
            'test/installed/package_consumer.cpp', 'test/unit/tls_psk_test.cpp',
            'test/support/quic_interop_files.cpp'], cwd=source, env=env)
        (directory / 'instrumentation.diff').write_bytes(diff)
        programs, runtime = prepare(args, source, directory, env)
    except Exception as error:
        save(directory / 'setup-failure.json', dict(error=str(error), target_invocations=0, state='closed'))
        raise
    def launch(name, case):
        cap, gate, count = TARGETS[name]
        if name == 'consumer':
            argv = [sys.executable, str(Path(__file__).resolve()), '--traffic-worker', str(programs[name]),
                    '--output', str(case)]
        else:
            consume_permit(case, name, parent=os.getpid())
            argv = [str(programs[name])]
        result = run_program(argv, case, runtime, cap=cap, gate=gate,
                             cwd=source / 'test' if host == 'Windows' and name != 'consumer' else
                             directory / 'build/test' if host == 'Darwin' else case)
        try:
            if name == 'consumer':
                original = json.loads((case / 'traffic.json').read_text())
                result['original_traffic'] = original
                result['passed'] = result['passed'] and original.get('passed') is True
            else:
                result['suite'] = check_suite((case / 'stdout.bin').read_text(errors='replace'),
                                               result['original_exit'], count)
                stderr = (case / 'stderr.bin').read_text(errors='replace')
                if name == 'tls_psk' and any(label not in stderr for label in
                        ('label=cancel-held', 'label=timeout-held', 'label=mixed-psk', 'label=mixed-certificate')):
                    raise ValueError('missing PSK diagnostic receipt')
                if name == 'quic_interop_tls' and 'ROOT_CAPTURE root_bytes=' not in stderr:
                    raise ValueError('missing root-open diagnostic receipt')
        except Exception as error:
            result.update(passed=False, receipt_error=str(error))
        return result
    ledger = run_batch(directory / 'targets', PLATFORMS[host], launch)
    return 0 if ledger['passed'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--make', default='make')
    parser.add_argument('--provider-prefix', type=Path)
    parser.add_argument('--toolchain-prefix', type=Path)
    parser.add_argument('--pkg-config-dir', action='append', default=[])
    parser.add_argument('--traffic-worker', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.traffic_worker:
        return traffic_worker(args.output, args.traffic_worker)
    return execute(args)


if __name__ == '__main__':
    sys.exit(main())
