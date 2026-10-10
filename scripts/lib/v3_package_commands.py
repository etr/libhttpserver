"""Task-local command logs, runtime loader selection and compiler dependencies."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
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

    def terminate(self):
        if self.job:
            self.job.terminate()
            if self.child is not None and self.child.poll() is None:
                self.child.kill()  # Also covers a failure before job assignment.
        elif self.child is not None:
            try:
                os.killpg(self.child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        if self.child is not None:
            self.child.wait(timeout=5)
        if self.job:
            self.job.wait_empty()

    def close(self):
        if self.job:
            self.job.close()


def _capture(pipe, output, done, errors):
    remaining = OUTPUT_LIMIT
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
        argv = [str(arg) for arg in argv]
        log = self.directory / ('command-%04d.log' % len(self.records))
        record = dict(argv=argv, cwd=str(cwd), log=str(log),
                      environment={key: env.get(key) for key in ('CXX', 'CPPFLAGS', 'LDFLAGS', 'CXXFLAGS',
                        'V3_TLS_CFLAGS', 'V3_TLS_LIBS', 'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_PATH',
                        'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'V3_PACKAGE_DLL_DIRS', 'V3_PACKAGE_TOOLCHAIN')})
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
                    while child.poll() is None or not done.wait(.05):
                        if errors:
                            raise errors[0]
                        if time.monotonic() > deadline:
                            raise RuntimeError('command deadline/output budget exceeded')
                        time.sleep(.05)
                    if errors:
                        raise errors[0]
                    child.wait(timeout=5)
                    record['exit'] = child.returncode
                except BaseException:
                    tree.terminate()
                    raise
                finally:
                    if reader.ident is not None:
                        reader.join(timeout=5)
                    tree.close()
                    if reader.is_alive():
                        raise RuntimeError('command pipe cleanup deadline exceeded')
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
