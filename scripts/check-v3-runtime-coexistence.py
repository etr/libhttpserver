#!/usr/bin/env python3
"""Linux runtime-package coexistence using a genuine retained ABI-2 build.

Development headers, metadata, archives and the unversioned link have shared
names; this gate proves only separate executables using the versioned runtimes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    receipt = dict(version=1, commands=[], boundary='versioned runtime packages; separate executables')

    def run(argv, env=None):
        result = subprocess.run(list(map(str, argv)), env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        receipt['commands'].append(dict(argv=list(map(str, argv)), exit=result.returncode, stdout=result.stdout))
        if result.returncode:
            raise RuntimeError('coexistence command failed: ' + str(argv) + '\n' + result.stdout[-3000:])
        return result.stdout

    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    try:
        baseline = args.baseline.resolve()
        library = baseline / 'libhttpserver.so.2.0.0'
        executable = baseline / 'consumer'
        if not library.is_file() or not executable.is_file():
            raise ValueError('genuine ABI-2 library and linked executable required')
        if not re.search(r'\(SONAME\).*\[libhttpserver.so.2\]', run(['readelf', '-d', library])):
            raise ValueError('baseline runtime must carry ABI-2 SONAME')
        if not re.search(r'\(NEEDED\).*\[libhttpserver.so.2\]', run(['readelf', '-d', executable])):
            raise ValueError('old executable must require ABI-2')
        # Resolve the configured prefix for a real DESTDIR install.
        status = (args.build / 'config.status').read_text()
        match = re.search(r'S\["prefix"\]="([^"]+)"', status)
        if not match or not match[1].startswith('/'):
            raise ValueError('absolute configured installation prefix required')
        prefix = output / 'stage' / match[1].lstrip('/')
        libraries = prefix / 'lib'
        libraries.mkdir(parents=True)
        for name in ('libhttpserver.so.2.0.0', 'libhttpserver.so.2'):
            source = baseline / name
            if source.is_symlink():
                (libraries / name).symlink_to(source.readlink())
            else:
                shutil.copyfile(source, libraries / name)
        old_hash = digest(libraries / library.name)
        old_link = (libraries / 'libhttpserver.so.2').readlink()
        run(['make', '-C', args.build, 'install', 'DESTDIR=' + str(output / 'stage')])
        if old_hash != digest(libraries / library.name) or old_link != (libraries / 'libhttpserver.so.2').readlink():
            raise ValueError('v3 installation changed retained ABI-2 runtime')
        new_library = libraries / 'libhttpserver.so.3.0.0'
        if not re.search(r'\(SONAME\).*\[libhttpserver.so.3\]', run(['readelf', '-d', new_library])):
            raise ValueError('new runtime must carry ABI-3 SONAME')
        source = output / 'consumer.cpp'
        source.write_text('#include <httpserver.hpp>\nint main() { return httpserver::query_features().http3; }\n')
        new_executable = output / 'consumer'
        run(shlex.split(args.compiler) + ['-std=c++20', '-I' + str(prefix / 'include'), str(source),
                                          '-L' + str(libraries), '-lhttpserver', '-o', str(new_executable)])
        if not re.search(r'\(NEEDED\).*\[libhttpserver.so.3\]', run(['readelf', '-d', new_executable])):
            raise ValueError('new executable must require ABI-3')
        env = dict(os.environ, LD_LIBRARY_PATH=str(libraries))
        run([executable], env)
        run([new_executable], env)
        receipt.update(status='passed', abi2_sha256=old_hash, abi3_sha256=digest(new_library),
                       abi2_consumer_sha256=digest(executable), abi3_consumer_sha256=digest(new_executable))
        print('PASS ABI-2 and ABI-3 versioned runtimes survive installation and run together')
    except Exception as error:
        receipt.update(status='failed', error=str(error))
        raise
    finally:
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
