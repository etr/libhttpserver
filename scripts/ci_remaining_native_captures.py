#!/usr/bin/env python3
"""One approved five-category diagnostic batch. Never merge this branch into v3."""
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
import ci_remaining_native_captures_minidump as minidump

BASE = 'b2ee4a2bd2a779921c9200a6afa8bc222fcc64d2'
IDENTITY = 'FEATURE-ci-remaining-native-captures'
TARGETS = {
    'consumer': dict(attempts=64, seconds=300, cap=30, gate=30, count=None),
    'native_http1_e2e': dict(attempts=1, seconds=190, cap=190, gate=180, count=62),
    'tls_psk': dict(attempts=1, seconds=60, cap=60, gate=60, count=14),
    'external_http1_e2e': dict(attempts=1, seconds=60, cap=60, gate=60, count=4),
    'wire_cleanup': dict(attempts=16, seconds=300, cap=60, gate=60, count=None),
}
PLATFORMS = {'Windows': ('consumer', 'native_http1_e2e'),
             'Darwin': ('tls_psk', 'external_http1_e2e'), 'Linux': ('wire_cleanup',)}
# Targets/tools inherit only selected execution setup. Never inherit CI credentials.
ENV_KEYS = ('PATH', 'SYSTEMROOT', 'SystemRoot', 'WINDIR', 'COMSPEC', 'PATHEXT',
            'HOME', 'USERPROFILE', 'HOMEDRIVE', 'HOMEPATH', 'TMPDIR', 'TMP', 'TEMP', 'TEMPDIR',
            'LANG', 'LC_ALL', 'TZ', 'SDKROOT', 'DEVELOPER_DIR', 'MACOSX_DEPLOYMENT_TARGET',
            'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'V3_PACKAGE_DLL_DIRS',
            'V3_PACKAGE_TOOLCHAIN', 'V3_PACKAGE_PREREQUISITES', 'V3_PROVIDER_PREFIX',
            'MSYSTEM', 'MSYS', 'MSYS2_PATH_TYPE', 'MSYS2_ARG_CONV_EXCL')
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
    """Durably charge each attempt before spawn; time is monotonic actual time."""
    names = tuple(names)
    if (not names or len(set(names)) != len(names) or any(name not in TARGETS for name in names)
            or names != tuple(name for name in TARGETS if name in names)):
        raise ValueError('invalid, duplicate or reordered category selection')
    directory.mkdir(parents=True, exist_ok=True)
    save(directory / 'once.json', {'id': IDENTITY, 'pid': os.getpid()}, exclusive=True)
    ledger = dict(id=IDENTITY, state='running', runner_pid=os.getpid(), attempts=0,
                  actual_seconds=0, categories={})
    try:
        for name in names:
            category = directory / name
            category.mkdir()
            limits = TARGETS[name]
            row = dict(attempts=0, actual_seconds=0, state='running', outcomes=[], passed=True,
                       limits=limits)
            ledger['categories'][name] = row
            save(category / 'once.json', dict(id=IDENTITY, target=name), exclusive=True)
            while row['attempts'] < limits['attempts'] and row['actual_seconds'] < limits['seconds']:
                cap = min(limits['cap'], limits['seconds'] - row['actual_seconds'])
                # Do not begin a process when there is no useful retirement reserve.
                if cap <= .25:
                    row['remaining_unspent_seconds'] = cap
                    break
                case = category / ('%03d' % (row['attempts'] + 1))
                case.mkdir()
                row['attempts'] += 1
                ledger['attempts'] += 1
                ledger.update(active_target=name, active_case=str(case.resolve()), active_cap=cap)
                save(directory / 'budget.json', ledger)
                started = time.monotonic()
                try:
                    result = launch(name, case, cap)
                    if not isinstance(result, dict) or type(result.get('passed')) is not bool:
                        raise ValueError('missing or invalid target outcome')
                except Exception as error:
                    result = dict(passed=False, error=str(error))
                elapsed = time.monotonic() - started
                if elapsed > cap:
                    result.update(passed=False, budget_error='actual target allowance exceeded')
                result.update(target=name, actual_seconds=elapsed, cap_seconds=cap)
                save(case / 'outcome.json', result)
                row['actual_seconds'] += elapsed
                ledger['actual_seconds'] += elapsed
                row['outcomes'].append(result)
                row['passed'] = row['passed'] and result['passed']
                ledger.update(active_target=None, active_case=None, active_cap=None)
                save(directory / 'budget.json', ledger)
                if not result['passed']:
                    break
            row['state'] = 'closed'
            save(category / 'budget.json', row)
    finally:
        ledger.update(state='closed', active_target=None, active_case=None, active_cap=None,
                      passed=len(ledger['categories']) == len(names) and all(
                          row['state'] == 'closed' and row['passed'] and row['attempts'] > 0
                          for row in ledger['categories'].values()))
        save(directory / 'budget.json', ledger)
    return ledger


def consume_permit(directory, target, parent=None):
    ledger = json.loads((directory.parent.parent / 'budget.json').read_text())
    row = ledger.get('categories', {}).get(target, {})
    limits = TARGETS.get(target, {})
    if (ledger.get('id') != IDENTITY or ledger.get('state') != 'running'
            or ledger.get('runner_pid') != (os.getppid() if parent is None else parent)
            or ledger.get('active_target') != target
            or Path(ledger.get('active_case', '')).resolve() != directory.resolve()
            or not 1 <= row.get('attempts', 0) <= limits.get('attempts', 0)
            or not 0 < ledger.get('active_cap', 0) <= limits.get('cap', 0)
            or not row.get('actual_seconds', float('inf')) < limits.get('seconds', 0)):
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



def qualify_dump(result, receipt, path, identity):
    callback = receipt.get('callback', {})
    if (result.get('passed') is not True or receipt.get('passed') is not True
            or receipt.get('status') != 'CAPTURED' or receipt.get('api_result') is not True
            or receipt.get('dump_type') != 0x1000 or receipt.get('full_memory') is not False
            or receipt.get('owned_identity') != identity
            or callback.get('io_started') is not True or callback.get('io_finished') is not True
            or callback.get('rejected') is not False or callback.get('writes', 0) < 1):
        raise ValueError('dump API/callback/identity receipt unavailable')
    payload = minidump.validate_dump(path, identity['pid'], identity['created_unix'], identity['image'])
    if callback.get('max_end') != payload['size']:
        raise ValueError('dump callback/file size differs')
    return dict(status='captured', result=result, receipt=receipt, payload=payload, identity=identity,
                qualification='normal minidump with thread contexts/stacks and module identities; no full memory')


def bounded_dump(tree, directory, deadline):
    if getattr(tree, 'dump_attempted', False):
        return dict(status='unavailable', reason='already attempted; no retry')
    tree.dump_attempted = True
    if tree.exited(deadline) or deadline - time.monotonic() < 5:
        return dict(status='unavailable', reason='primary exited or insufficient diagnostic allowance')
    output = directory / 'dump'
    output.mkdir()
    import ctypes
    identity = minidump.process_identity(minidump.windows_api(), ctypes.wintypes.HANDLE(int(tree.child._handle)), tree.child.pid)
    save(output / 'expected.json', dict(identity=identity, parent_pid=os.getpid()), exclusive=True)
    command = [sys.executable, str(Path(__file__).resolve()), '--dump-worker',
               str(tree.child.pid), '--creation-filetime', str(identity['creation_filetime']),
               '--output', str(output)]
    result = run_program(command, output, os.environ, cap=4, gate=3, cleanup=.8)
    try:
        receipt = json.loads((output / 'minidump.json').read_text())
        return qualify_dump(result, receipt, output / 'primary.dmp', identity)
    except Exception as error:
        return dict(status='unavailable', result=result, error=str(error))


def dump_worker(directory, pid, creation):
    """Open only the parent's still-owned primary; verify its creation identity."""
    if platform.system() != 'Windows':
        raise RuntimeError('dump worker requires the owned native Windows primary')
    import ctypes
    from ctypes import wintypes
    from types import SimpleNamespace
    expected = json.loads((directory / 'expected.json').read_text())
    identity = expected['identity']
    if expected['parent_pid'] != os.getppid() or identity['pid'] != pid or identity['creation_filetime'] != creation:
        raise RuntimeError('dump parent / primary identity differs')
    save(directory / 'dump-once.json', dict(pid=pid, creation=creation), exclusive=True)
    kernel = minidump.windows_api()
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    handle = kernel.OpenProcess(0x0410, False, pid)  # QUERY_INFORMATION | VM_READ
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        child = SimpleNamespace(pid=pid, _handle=handle)
        receipt = minidump.capture_minidump(child, identity, directory / 'primary.dmp',
                                           time.monotonic() + 2.5, kernel)
        receipt['passed'] = receipt['status'] == 'CAPTURED'
        save(directory / 'minidump.json', receipt)
    finally:
        kernel.CloseHandle(handle)
    return 0 if receipt['passed'] else 1


def diagnostic_snapshot(tree, directory, state, deadline):
    row = dict(state, monotonic=time.monotonic())
    try:
        row['identity'] = process_identity(tree)
    except Exception as error:
        row['identity_error'] = str(error)
    if tree.job and Path(tree.child.args[0]).name == 'native_http1_e2e.exe':
        row['dump_capture'] = bounded_dump(tree, directory, deadline)
    else:
        row['dump_capture'] = dict(status='unavailable', reason='not the selected Windows primary')
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
    receipts = re.findall(r'^STOP_CAPTURE bits=(\d+) failed=([01]) running=([01])$', stderr, re.M)
    if receipts != [('127', '0', '0')]:
        raise ValueError('incomplete original stop witness: ' + repr(receipts))
    if 'WSA_WAIT_ERROR ' in stderr:
        raise ValueError('original Winsock failure preserved')


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


WIRE_CHECKS = (
    'Parked writer/application send wake/HTTP timer removal',
    'Raw refusals/all handshake splits/coalesced frames/queue-full suffix',
    'HTTP-looking frame rejection/handler return/handler throw',
    'Real socket saturation/parked writable/64 ordered MiB/retry',
    'Abort while writable blocked',
    'Local Close timeout preserves two WebSockets and HTTP',
)


def check_wire(output):
    if any(output.count(label + ' PASS') != 1 for label in WIRE_CHECKS):
        raise ValueError('missing or duplicate original six wire-check witnesses')
    if any(label in output for label in ('Drain DRAIN', 'Handler-safe drain', 'Independent client')):
        raise ValueError('unapproved later wire cases executed')
    if output.count('WIRE_FIXTURE_STDOUT_BEGIN') != 1 or output.count('WIRE_FIXTURE_STDOUT_END') != 1:
        raise ValueError('raw fixture stdout missing')
    rows = re.findall(r'^WIRE_DECISION exit=([01]) failed=([01]) accepted=(\d+|skipped) '
                      r'closed=(\d+|skipped) failure_reasons=(\d+)$', output, re.M)
    if len(rows) != 1:
        raise ValueError('missing exact terminal decision')
    status, failed, accepted, closed, reasons = rows[0]
    if (status != '0' or failed != '0' or accepted == 'skipped' or closed == 'skipped'
            or accepted != closed or reasons != '0' or not re.search(r'^STATS \d+ \d+$', output, re.M)):
        raise ValueError('original terminal fixture failure preserved: ' + repr(rows))
    return dict(accepted=int(accepted), closed=int(closed), failed=False, exit=0, checks=6)


def check_psk(stderr):
    if any(label not in stderr for label in ('PSK_PROVIDER ', 'PSK_WORKER ', 'PSK_LOOKUP ', 'PSK_LANE ')):
        raise ValueError('missing owning-worker PSK diagnostic receipt')
    if 'truncated=1' in stderr:
        raise ValueError('bounded PSK trace truncated')
    providers = set(re.findall(r'^PSK_PROVIDER session=(\S+) impl=\S+ thread=(\d+) rc=-?\d+ '
                              r'ssl_error=\d+ first=\d+ last=\d+ state=\d+ version=\d+ '
                              r'input_pending=\d+ output_pending=\d+ capacity=\d+ result=\d+ failure=\d+$', stderr, re.M))
    workers = set(re.findall(r'^PSK_WORKER session=(\S+) request=\S+ thread=(\d+) ', stderr, re.M))
    if not providers.intersection(workers):
        raise ValueError('no provider/worker ownership correlation')
    return dict(provider_worker_pairs=sorted(providers.intersection(workers)))


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def prepare(args, source, directory, env):
    """Build just the selected native lane/programs; execute no selected target."""
    setup = directory / 'setup'
    setup.mkdir(parents=True)
    command = Commands(setup)
    host = platform.system()
    mode = 'yes' if host == 'Darwin' else 'no'
    build, prefix = directory / 'build', directory / 'prefix'
    build.mkdir()
    prefix.mkdir()
    provider = args.provider_prefix or directory / 'no-provider'
    optimization = '-O3' if host == 'Linux' else '-O0'
    build_env = dict(env, CXX=args.compiler, CC=args.cc, MAKE=args.make,
                     CXXFLAGS=optimization + ' -g -DLIBHTTPSERVER_REMAINING_CAPTURE',
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
        selected = ['tls_psk'] if host == 'Darwin' else ['native_websocket_fixture']
        command([args.make, '-j2', '-C', 'test'] + selected, build, build_env)
        if host == 'Darwin':
            # A separate TLS-off static lane preserves the original external-H1 linkage.
            plain, plain_prefix = directory / 'plain-build', directory / 'plain-prefix'
            plain.mkdir()
            plain_prefix.mkdir()
            plain_env = dict(build_env, V3_TLS_CFLAGS='', V3_TLS_LIBS='', LDFLAGS='')
            command([bash, configure, '--disable-v3-tls', '--disable-shared', '--enable-static',
                     '--disable-examples', '--prefix=' + plain_prefix.as_posix()], plain, plain_env)
            command([args.make, '-j2', '-C', 'src'], plain, plain_env)
            command([args.make, '-C', 'src', 'install'], plain, plain_env)
            command([args.make, 'install-pkgconfigDATA'], plain, plain_env)
            plain_runtime = dict(runtime, PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join(
                [str(plain_prefix / 'lib/pkgconfig')] + args.pkg_config_dir))
            cflags = shlex.split(command(['pkg-config', '--cflags', 'libhttpserver'], plain, plain_runtime).strip())
            libraries = shlex.split(command(['pkg-config', '--libs', '--static', 'libhttpserver'], plain, plain_runtime).strip())
            # Select the installed archive explicitly, even if another shared package is on the host.
            libraries = [str(plain_prefix / 'lib/libhttpserver.a') if item == '-lhttpserver' else item
                         for item in libraries]
            consumer = directory / 'external-build'
            consumer.mkdir()
            executable = consumer / 'external_http1_e2e'
            flags = cflags + ['-DHTTPSERVER_COMPILATION', '-I' + str(source / 'src'), '-I' + str(source / 'test')]
            for macro, value in (('TLS_TEST_DIR', source / 'test'),
                                 ('PARITY_TRANSCRIPT_DIR', source / 'test/parity/transcripts'),
                                 ('PARITY_DATA_ROOT', source / 'test'), ('HTTP2_CORPUS_DIR', source / 'test/conformance/http2')):
                flags += ['-D' + macro + '="' + value.as_posix() + '"']
            units = [source / 'test' / unit for unit in
                     package_matrix.load()['suites']['external_http1_e2e']['sources']]
            compiles['external_http1_e2e'] = compile_probe(args.compiler, units, flags, libraries, executable,
                command, plain_runtime, source, plain, plain_prefix, True)
            programs = dict(tls_psk=build / 'test/tls_psk', external_http1_e2e=executable)
            for name in ('config.h', 'config.log', 'Makefile', 'test/Makefile'):
                shutil.copyfile(plain / name, setup / ('plain-' + name.replace('/', '-')))
        else:
            programs = dict(wire_cleanup=build / 'test/native_websocket_fixture')
    if host != 'Windows':
        programs = {name: path.parent / '.libs' / path.name if (path.parent / '.libs' / path.name).is_file() else path
                    for name, path in programs.items()}
        loader = 'DYLD_LIBRARY_PATH' if host == 'Darwin' else 'LD_LIBRARY_PATH'
        runtime[loader] = os.pathsep.join([str(build / 'src/.libs'), str(provider / 'lib'), runtime.get(loader, '')])
    context = dict(platform=platform.platform(), python=sys.version, python_path=sys.executable,
                   uid=os.getuid() if hasattr(os, 'getuid') else None,
                   environment=target_environment(runtime), build_environment={key: build_env.get(key) for key in
                       ('CXX', 'CC', 'MAKE', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'V3_TLS_CFLAGS', 'V3_TLS_LIBS',
                        'PKG_CONFIG_LIBDIR')},
                   sdk=compiler_sdk.capture(args.compiler, cwd=build), compiles=compiles,
                   programs={name: dict(path=str(path), sha256=digest(path)) for name, path in programs.items()})
    for name, argv in [('compiler', shlex.split(args.compiler) + ['--version']),
                       ('linker', ['xcrun', '--show-sdk-path'] if host == 'Darwin' else ['ld', '--version']),
                       ('system', ['uname', '-a'])]:
        context[name] = command(argv, build, runtime)
    artifacts = [build / 'config.h', build / 'config.log', build / 'Makefile', build / 'test/Makefile']
    artifacts += [path for path in (prefix.rglob('*') if host == 'Windows' else (build / 'src/.libs').glob('*'))
                  if path.is_file() and path.suffix.lower() in ('.dll', '.so', '.dylib', '.a')]
    if host == 'Darwin':
        artifacts += [path for path in plain_prefix.rglob('*') if path.is_file()]
        artifacts += [Path(str(programs['external_http1_e2e']) + '.map')]
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
            ['objdump', '-p', str(executable)] if host == 'Windows' else
            ['otool', '-L', str(executable)] if host == 'Darwin' else ['readelf', '-d', str(executable)],
            build, runtime)
    if mode == 'yes':
        context['provider_version'] = command([str(provider / 'bin/openssl'), 'version', '-a'], build, runtime)
        if not context['provider_version'].startswith('OpenSSL 3.5.9 '):
            raise RuntimeError('actual selected provider differs from OpenSSL 3.5.9')
        artifacts.append(provider / 'bin/openssl')
    artifacts.append(Path(sys.executable))
    artifacts += [source / path for path in ('configure', 'config.h.in', 'Makefile.in', 'src/Makefile.in', 'test/Makefile.in')]
    if host == 'Windows':
        modules = setup / 'modules'
        modules.mkdir()
        selected = [path for path in artifacts if path.suffix.lower() == '.dll']
        selected += list(programs.values())
        selected += [path.with_suffix(path.suffix + '.map') for path in programs.values()]
        if sum(path.stat().st_size for path in selected if path.is_file()) > 512 * 1024 * 1024:
            raise RuntimeError('selected module/symbol artifact cap exceeded')
        for path in selected:
            if path.is_file():
                shutil.copyfile(path, modules / path.name)
        context['retained_modules'] = {str(path): digest(path) for path in modules.iterdir()}
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
            'test/installed/package_consumer.cpp', 'src/detail/tls_session.cpp',
            'src/detail/tls_io_backend.cpp', 'src/detail/tls_psk_runtime.cpp',
            'test/integ/external_http1_e2e_test.cpp', 'test/integ/native_websocket_fixture.cpp',
            'test/integ/native_websocket_client.py'], cwd=source, env=env)
        (directory / 'instrumentation.diff').write_bytes(diff)
        programs, runtime = prepare(args, source, directory, env)
    except Exception as error:
        save(directory / 'setup-failure.json', dict(error=str(error), target_invocations=0, state='closed'))
        raise
    def launch(name, case, cap):
        target = TARGETS[name]
        if name == 'consumer':
            argv = [sys.executable, str(Path(__file__).resolve()), '--traffic-worker', str(programs[name]),
                    '--output', str(case)]
        else:
            consume_permit(case, name, parent=os.getpid())
            argv = [str(programs[name])]
            if name == 'wire_cleanup':
                argv = [sys.executable, str(source / 'test/integ/native_websocket_client.py'),
                        str(programs[name]), '--first-wire-cycle-only',
                        '--fixture-stdout-receipt', str(case / 'fixture-stdout.bin')]
        cwd = (source / 'test' if name == 'external_http1_e2e' or (host == 'Windows' and name != 'consumer') else
               directory / 'build/test' if host != 'Windows' else case)
        # Singles with a 60s original budget keep retirement inside that allowance.
        cleanup = min(6, cap / 5)
        result = run_program(argv, case, runtime, cap=cap, gate=min(target['gate'], cap),
                             cleanup=cleanup, cwd=cwd)
        try:
            if name == 'consumer':
                original = json.loads((case / 'traffic.json').read_text())
                result['original_traffic'] = original
                result['passed'] = result['passed'] and original.get('passed') is True
            elif name == 'wire_cleanup':
                result['wire'] = check_wire((case / 'stdout.bin').read_text(errors='replace'))
            else:
                result['suite'] = check_suite((case / 'stdout.bin').read_text(errors='replace'),
                                               result['original_exit'], target['count'])
                stderr = (case / 'stderr.bin').read_text(errors='replace')
                if name == 'tls_psk':
                    result['worker_trace'] = check_psk(stderr)
                if name == 'native_http1_e2e' and not result['passed']:
                    result['dump_status'] = [row.get('dump_capture') for row in json.loads(
                        (case / 'snapshots.json').read_text())]
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
    parser.add_argument('--dump-worker', type=int, help=argparse.SUPPRESS)
    parser.add_argument('--creation-filetime', type=int, help=argparse.SUPPRESS)
    parser.add_argument('--traffic-worker', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.dump_worker:
        return dump_worker(args.output, args.dump_worker, args.creation_filetime)
    if args.traffic_worker:
        return traffic_worker(args.output, args.traffic_worker)
    return execute(args)


if __name__ == '__main__':
    sys.exit(main())
