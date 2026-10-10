"""Task-local command logs, runtime loader selection and compiler dependencies."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import time


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
                child = subprocess.Popen(argv, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + timeout
                    while child.poll() is None:
                        if time.monotonic() > deadline or log.stat().st_size > 64 * 1024 * 1024:
                            raise RuntimeError('command deadline/output budget exceeded')
                        time.sleep(.05)
                finally:
                    if child.poll() is None:
                        child.kill()
                    child.wait(timeout=5)
                record['exit'] = child.returncode
            data = log.read_bytes()
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
