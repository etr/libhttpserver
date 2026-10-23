"""Task-local command logs, runtime loader selection and compiler dependencies."""
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import threading
import time


OUTPUT_LIMIT = 64 * 1024 * 1024


class _WindowsJob:
    """Assign a suspended command before it can spawn any descendants."""
    def __init__(self):
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)

        class ThreadEntry(ctypes.Structure):
            _fields_ = [(name, wintypes.DWORD) for name in
                        ('size', 'usage', 'thread', 'process')] + [
                            ('priority', wintypes.LONG), ('delta', wintypes.LONG),
                            ('flags', wintypes.DWORD)]

        class Accounting(ctypes.Structure):
            _fields_ = [(name, ctypes.c_longlong) for name in
                        ('user', 'kernel', 'period_user', 'period_kernel')] + [
                            (name, wintypes.DWORD) for name in
                            ('faults', 'total', 'active', 'terminated')]

        self.ThreadEntry, self.Accounting = ThreadEntry, Accounting
        signatures = {
            'CreateJobObjectW': ([ctypes.c_void_p, wintypes.LPCWSTR], wintypes.HANDLE),
            'AssignProcessToJobObject': ([wintypes.HANDLE, wintypes.HANDLE], wintypes.BOOL),
            'OpenProcess': ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            'CreateToolhelp32Snapshot': ([wintypes.DWORD, wintypes.DWORD], wintypes.HANDLE),
            'Thread32First': ([wintypes.HANDLE, ctypes.POINTER(ThreadEntry)], wintypes.BOOL),
            'Thread32Next': ([wintypes.HANDLE, ctypes.POINTER(ThreadEntry)], wintypes.BOOL),
            'OpenThread': ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            'ResumeThread': ([wintypes.HANDLE], wintypes.DWORD),
            'TerminateJobObject': ([wintypes.HANDLE, wintypes.UINT], wintypes.BOOL),
            'QueryInformationJobObject': ([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                           wintypes.DWORD, ctypes.c_void_p], wintypes.BOOL),
            'CloseHandle': ([wintypes.HANDLE], wintypes.BOOL),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = args, result
        self.handle = self.check(self.api.CreateJobObjectW(None, None))

    def check(self, result):
        if not result:
            raise self.ctypes.WinError(self.ctypes.get_last_error())
        return result

    def attach(self, child):
        # PROCESS_SET_QUOTA | PROCESS_TERMINATE are required for job assignment.
        handle = self.check(self.api.OpenProcess(0x0101, False, child.pid))
        try:
            self.check(self.api.AssignProcessToJobObject(self.handle, handle))
        finally:
            self.api.CloseHandle(handle)
        # Popen closes the primary thread handle; find it while still suspended.
        snapshot = self.api.CreateToolhelp32Snapshot(0x0004, 0)  # TH32CS_SNAPTHREAD
        if snapshot == self.ctypes.c_void_p(-1).value:
            raise self.ctypes.WinError(self.ctypes.get_last_error())
        try:
            entry = self.ThreadEntry()
            entry.size = self.ctypes.sizeof(entry)
            found = self.api.Thread32First(snapshot, self.ctypes.byref(entry))
            while found:
                if entry.process == child.pid:
                    thread = self.check(self.api.OpenThread(0x0002, False, entry.thread))
                    try:
                        if self.api.ResumeThread(thread) == 0xffffffff:
                            raise self.ctypes.WinError(self.ctypes.get_last_error())
                    finally:
                        self.api.CloseHandle(thread)
                    return
                found = self.api.Thread32Next(snapshot, self.ctypes.byref(entry))
            raise RuntimeError('command primary thread not found')
        finally:
            self.api.CloseHandle(snapshot)

    def terminate(self):
        self.check(self.api.TerminateJobObject(self.handle, 1))

    def wait_empty(self):
        deadline = time.monotonic() + 5
        accounting = self.Accounting()
        while True:
            self.check(self.api.QueryInformationJobObject(
                self.handle, 1, self.ctypes.byref(accounting), self.ctypes.sizeof(accounting), None))
            if not accounting.active:
                return
            if time.monotonic() > deadline:
                raise RuntimeError('command job cleanup deadline exceeded')
            time.sleep(.01)

    def close(self):
        self.api.CloseHandle(self.handle)


class _CommandTree:
    def __init__(self, argv, cwd, env):
        self.job = _WindowsJob() if os.name == 'nt' else None
        self.child = None
        try:
            options = dict(creationflags=0x0004) if self.job else dict(start_new_session=True)
            self.child = subprocess.Popen(argv, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                          stderr=subprocess.STDOUT, **options)
            if self.job:
                self.job.attach(self.child)
        except BaseException:
            try:
                self.terminate()
            finally:
                self.close()
            raise

    def exited(self, deadline=None):
        if self.job:
            return self.child.poll() is not None
        return self._anchor(deadline)

    def _anchor(self, deadline=None):
        # Never reap the session leader before retirement: its kernel lifetime
        # reserves the numeric PID/PGID for every group signal and observation.
        if self.child.returncode is not None:
            raise RuntimeError('command process-group ownership lost: leader reaped')
        if callable(getattr(os, 'waitid', None)) and hasattr(os, 'WNOWAIT'):
            try:
                return os.waitid(os.P_PID, self.child.pid,
                                 os.WEXITED | os.WNOHANG | os.WNOWAIT) is not None
            except ChildProcessError as error:
                raise RuntimeError('command process-group ownership lost') from error
        members = _process_snapshot(time.monotonic() + 5 if deadline is None else deadline)
        leader = members.get(self.child.pid)
        self._check_leader(leader)
        return leader[2] == 'Z'

    def _check_leader(self, leader):
        if leader is None or leader[:2] != (os.getpid(), self.child.pid):
            raise RuntimeError('command process-group ownership lost: leader absent or changed')

    def _kill_group(self, deadline):
        _remaining(deadline)
        self._anchor(deadline)
        _remaining(deadline)
        try:
            os.killpg(self.child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # The snapshot must still prove that no live member remains.
        except PermissionError:
            # Darwin excludes zombies from group signals and can return EPERM.
            self._anchor(deadline)
            members = _process_snapshot(deadline)
            self._check_leader(members.get(self.child.pid))
            self._anchor(deadline)
            _remaining(deadline)
            if any(group == self.child.pid and state != 'Z'
                   for _, group, state in members.values()):
                raise

    def terminate(self, deadline=None):
        if self.job:
            self.job.terminate()
            if self.child is not None and self.child.poll() is None:
                self.child.kill()  # Also covers a failure before job assignment.
            if self.child is not None:
                self.child.wait(timeout=5)
            self.job.wait_empty()
            return
        if self.child is None:
            return
        deadline = deadline if deadline is not None else time.monotonic() + 5
        retired = False
        try:
            self._kill_group(deadline)
            while True:
                _remaining(deadline)
                self._anchor(deadline)
                members = _process_snapshot(deadline)
                self._check_leader(members.get(self.child.pid))
                live = any(group == self.child.pid and state != 'Z'
                           for _, group, state in members.values())
                if not live and retired:
                    break
                retired = not live
                if live:
                    self._kill_group(deadline)
                time.sleep(min(.01, _remaining(deadline)))
        finally:
            # Failure remains a failure; still reap our direct child if possible.
            self.child.wait(timeout=_remaining(deadline))

    def close(self):
        if self.job:
            self.job.close()


def _remaining(deadline):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise RuntimeError('command cleanup deadline exceeded')
    return remaining


def _process_snapshot(deadline):
    members = {}
    if sys.platform.startswith('linux'):
        for entry in Path('/proc').iterdir():
            _remaining(deadline)
            if not entry.name.isascii() or not entry.name.isdigit():
                continue
            try:
                data = (entry / 'stat').read_text()
            except (FileNotFoundError, ProcessLookupError):
                continue
            fields = data.rsplit(')', 1)[-1].split()
            if (len(fields) < 4 or len(fields[0]) != 1 or fields[0] not in 'RSDZTWtXxKIP'
                    or not fields[1].isascii() or not fields[1].isdigit()
                    or not fields[2].isascii() or not fields[2].isdigit()
                    or data.split(' ', 1)[0] != entry.name or ')' not in data):
                raise RuntimeError('malformed command process snapshot')
            members[int(entry.name)] = (int(fields[1]), int(fields[2]), fields[0])
        return members
    # macOS Python <3.13 has no waitid. Numeric state/parent/group fields keep
    # observation non-reaping without relying on coarse ps start-time strings.
    executable = next((path for path in ('/bin/ps', '/usr/bin/ps') if Path(path).is_file()), None)
    if executable is None:
        raise RuntimeError('command process snapshot unavailable: system ps missing')
    output, done, errors = io.BytesIO(), threading.Event(), []
    probe = subprocess.Popen([executable, '-A', '-o', 'pid=', '-o', 'ppid=', '-o', 'pgid=', '-o', 'state='],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    reader = threading.Thread(target=_capture, args=(probe.stdout, output, done, errors, 4 * 1024 * 1024), daemon=True)
    reader.start()
    try:
        if not done.wait(_remaining(deadline)):
            raise RuntimeError('command process snapshot deadline exceeded')
        if errors:
            raise RuntimeError('command process snapshot failed: ' + str(errors[0]))
        if probe.wait(timeout=_remaining(deadline)):
            raise RuntimeError('command process snapshot ps failed')
    finally:
        if probe.poll() is None:
            probe.kill()
        try:
            probe.wait(timeout=max(0, deadline - time.monotonic()))
        finally:
            reader.join(timeout=max(0, deadline - time.monotonic()))
    if reader.is_alive():
        raise RuntimeError('command process snapshot reader deadline exceeded')
    for line in output.getvalue().decode('ascii').splitlines():
        _remaining(deadline)
        fields = line.split()
        # Darwin can report unknown Mach state '?'; retain it as non-retired.
        if (len(fields) != 4 or any(not item.isdigit() for item in fields[:3])
                or not fields[3] or fields[3][0] not in 'RSDZTWtXxKIPLU?'
                or any(flag not in '+<>ACEJLNSsVWX' for flag in fields[3][1:])):
            raise RuntimeError('malformed command process snapshot')
        pid, parent, group = map(int, fields[:3])
        # FreeBSD exposes kernel PID 0 in parent/group 0; it is observation only.
        if pid in members or (pid == 0 and (parent != 0 or group != 0)):
            raise RuntimeError('invalid command process snapshot PID')
        members[pid] = (parent, group, fields[3][0])
    if not members:
        raise RuntimeError('empty command process snapshot')
    return members


def _capture(pipe, output, done, errors, limit=OUTPUT_LIMIT):
    remaining = limit
    try:
        while chunk := os.read(pipe.fileno(), 65536):
            output.write(chunk[:remaining])
            if len(chunk) > remaining:
                raise RuntimeError('command deadline/output budget exceeded')
            remaining -= len(chunk)
    except Exception as error:
        errors.append(error)
    finally:
        pipe.close()
        done.set()


class Commands:
    def __init__(self, directory):
        self.directory = directory
        self.records = []

    def __call__(self, argv, cwd, env, timeout=1200, allow_failure=False):
        started = time.monotonic()
        argv = [str(arg) for arg in argv]
        log = self.directory / ('command-%04d.log' % len(self.records))
        record = dict(argv=argv, cwd=str(cwd), log=str(log),
                      environment={key: env.get(key) for key in ('CXX', 'CPPFLAGS', 'LDFLAGS', 'CXXFLAGS',
                        'V3_TLS_CFLAGS', 'V3_TLS_LIBS', 'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_PATH',
                        'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'V3_PACKAGE_DLL_DIRS', 'V3_PACKAGE_TOOLCHAIN', 'CC', 'PATH',
                        'V3_RELEASE_IDENTITY', 'ASAN_OPTIONS', 'UBSAN_OPTIONS', 'TSAN_OPTIONS')})
        self.records.append(record)
        try:
            with log.open('wb') as output:
                tree = _CommandTree(argv, cwd, env)
                child = tree.child
                done, errors = threading.Event(), []
                reader = threading.Thread(target=_capture, args=(child.stdout, output, done, errors), daemon=True)
                try:
                    reader.start()
                    deadline = time.monotonic() + timeout
                    while not tree.exited(deadline) or not done.wait(.05):
                        if errors:
                            raise errors[0]
                        if time.monotonic() > deadline:
                            raise RuntimeError('command deadline/output budget exceeded')
                        time.sleep(.05)
                    if errors:
                        raise errors[0]
                except BaseException as error:
                    original_error = error
                    raise
                else:
                    original_error = None
                finally:
                    cleanup_deadline = time.monotonic() + 5
                    try:
                        if tree.job and original_error is None:
                            child.wait(timeout=5)
                        else:
                            tree.terminate(cleanup_deadline)
                    except BaseException as cleanup_error:
                        context = f'; original command failure: {original_error}' if original_error else ''
                        raise RuntimeError(f'command cleanup failed: {cleanup_error}{context}') from cleanup_error
                    finally:
                        record['exit'] = child.returncode
                        if reader.ident is not None:
                            budget = 5 if tree.job else max(0, cleanup_deadline - time.monotonic())
                            reader.join(timeout=budget)
                        tree.close()
                    if reader.is_alive():
                        context = f'; original command failure: {original_error}' if original_error else ''
                        raise RuntimeError('command pipe cleanup deadline exceeded' + context)
            if log.stat().st_size > OUTPUT_LIMIT:
                raise RuntimeError('command deadline/output budget exceeded')
            with log.open('rb') as output:
                data = output.read(OUTPUT_LIMIT + 1)
            if len(data) > OUTPUT_LIMIT:
                raise RuntimeError('command deadline/output budget exceeded')
            record.update(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))
            if child.returncode and not allow_failure:
                raise RuntimeError('matrix command failed: ' + shlex.join(argv) + '\n' + data[-3000:].decode(errors='replace'))
            return data.decode(errors='replace')
        except Exception as error:
            record['error'] = str(error)
            raise
        finally:
            record['duration_seconds'] = round(time.monotonic() - started, 6)
            (self.directory / 'commands.json').write_text(json.dumps(self.records, indent=2) + '\n')


def runtime_environment(clean, prefix, provider, prerequisites, toolchain=None):
    env = dict(clean)
    directories = [root / directory for root in [prefix, provider] + prerequisites for directory in ('lib', 'bin')]
    search = os.pathsep.join(str(path) for path in directories)
    env['LD_LIBRARY_PATH'] = search
    env['DYLD_LIBRARY_PATH'] = search
    env['V3_PACKAGE_DLL_DIRS'] = search
    env['V3_PACKAGE_PREREQUISITES'] = os.pathsep.join(map(str, prerequisites))
    if toolchain:
        env['V3_PACKAGE_TOOLCHAIN'] = str(toolchain)
    if os.name == 'nt':
        env['PATH'] = search + os.pathsep + env.get('PATH', '')
    return env


def dependencies(depfile):
    content = depfile.read_text().replace('\\\n', ' ')
    return shlex.split(content.split(': ', 1)[1])
