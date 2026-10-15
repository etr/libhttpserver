"""Copy, compile and smoke all examples outside the source/build trees."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from example_inventory import HEADERS, SOURCES
from package_integration import compile_probe

source, build, prefix, consumer = map(lambda p: Path(p).resolve(), sys.argv[1:])
env = dict(os.environ, PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'),
           LD_LIBRARY_PATH=str(prefix / 'lib'), DYLD_LIBRARY_PATH=str(prefix / 'lib'))
compiler = os.environ.get('CXX', 'c++')

def command(argv, cwd, environment, **kwargs):
    result = subprocess.run(argv, cwd=cwd, env=environment, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(shlex.join(argv) + '\n' + result.stdout + result.stderr)
    return result.stdout + result.stderr

def pkg(*arguments):
    return shlex.split(command(['pkg-config', '--define-variable=prefix=' + str(prefix), *arguments, 'libhttpserver'], consumer, env))

consumer.mkdir(exist_ok=True)
for name, unit in SOURCES.items():
    shutil.copy2(source / unit, consumer / (name + '.cpp'))
for name in HEADERS:
    shutil.copy2(source / 'examples' / name, consumer / name)
flags = pkg('--cflags') + ['-pthread']
for linkage in ('shared', 'static'):
    directory = consumer / linkage; directory.mkdir()
    libraries = pkg('--libs', *(['--static'] if linkage == 'static' else []))
    if linkage == 'static':
        libraries = [str(prefix / 'lib/libhttpserver.a') if flag == '-lhttpserver' else flag for flag in libraries]
    libraries += ['-pthread', '-Wl,-rpath,' + str(prefix / 'lib')]
    for name in SOURCES:
        compile_probe(compiler, [consumer / (name + '.cpp')], flags, libraries, directory / name,
                      command, env, source, build, prefix, False, ['-O1', '-g'])
        if sys.platform.startswith('linux'):
            loaded = command(['ldd', str(directory / name)], consumer, env)
            library_lines = [line for line in loaded.splitlines() if 'libhttpserver' in line]
            if linkage == 'static' and library_lines:
                raise RuntimeError('static example loaded a shared libhttpserver: ' + name)
            if linkage == 'shared' and (not library_lines or any(str(prefix / 'lib') not in line for line in library_lines)):
                raise RuntimeError('shared example did not load the staged library: ' + name)
        print('PASS installed native example: ' + name + ' (' + linkage + ')', flush=True)
    lane = os.environ.get('V3_TLS_MODE')
    if lane is None:
        lane = 'yes' if 'S["enable_v3_tls"]="yes"' in (build / 'config.status').read_text() else 'no'
    smoke_env = dict(env, V3_EXAMPLES_DIR=str(directory), V3_ROTATION_EXAMPLE=str(directory / 'tls_rotation'),
                     V3_TLS_FIXTURES=str(source / 'test/data/tls_credentials'), V3_TLS_MODE=lane)
    python = shlex.split(os.environ.get('PYTHON', sys.executable))
    output = command(python + [str(source / 'test/integ/examples_test.py')], consumer, smoke_env)
    print(output, end='')
    print('PASS installed example runtime (' + linkage + ')', flush=True)
