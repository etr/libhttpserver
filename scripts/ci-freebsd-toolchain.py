#!/usr/bin/env python3
"""Provision stable libc++20 privately, retaining FreeBSD's system libcxxrt ABI."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request


VERSION = '20.1.8'
SOURCE_SHA256 = '6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d'
SOURCE_URL = ('https://github.com/llvm/llvm-project/releases/download/llvmorg-' + VERSION
              + '/llvm-project-' + VERSION + '.src.tar.xz')


def configure_command(source, build, prefix, cc, cxx):
    return ['cmake', '-G', 'Ninja', '-S', str(source / 'runtimes'), '-B', str(build),
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
            '-DCMAKE_C_COMPILER=' + cc, '-DCMAKE_CXX_COMPILER=' + cxx,
            '-DLLVM_ENABLE_RUNTIMES=libcxx', '-DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF',
            '-DLLVM_INCLUDE_TESTS=OFF', '-DLIBCXX_INCLUDE_TESTS=OFF',
            '-DLIBCXX_INCLUDE_BENCHMARKS=OFF',
            '-DLIBCXX_ABI_VERSION=1', '-DLIBCXX_ABI_UNSTABLE=OFF',
            '-DLIBCXX_HARDENING_MODE=extensive', '-DLIBCXX_CXX_ABI=libcxxrt',
            '-DLIBCXX_CXX_ABI_INCLUDE_PATHS=/usr/include/c++/v1',
            '-DLIBCXXABI_USE_LLVM_UNWINDER=OFF',
            '-DLIBCXX_ENABLE_NEW_DELETE_DEFINITIONS=ON']


def download_source(work):
    archive = work / 'source.tar.xz'
    with urllib.request.urlopen(SOURCE_URL, timeout=60) as response, archive.open('wb') as stream:
        shutil.copyfileobj(response, stream)
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != SOURCE_SHA256:
            raise RuntimeError('LLVM source SHA256 mismatch')
    root = 'llvm-project-' + VERSION + '.src'
    with tarfile.open(archive) as source:
        members = [member for member in source.getmembers() if any(
            member.name.startswith(root + '/' + part + '/')
            for part in ('cmake', 'llvm/cmake', 'runtimes', 'libcxx', 'libc'))]
        source.extractall(work, members=members, filter='data')
    return work / root


def write_compiler(prefix, cxx):
    wrapper = prefix / 'bin/clang++'
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    # Wrappers retain complete SDK selection when package audits clean the environment.
    wrapper.write_text('''#!/usr/bin/env bash
set -eu
link_flags=(''' + shlex.join(['-nostdlib++', '-L' + str(prefix / 'lib'), '-lc++']) + ''')
rpath=(''' + shlex.quote('-Wl,-rpath,' + str(prefix / 'lib')) + ''')
for argument in "$@"; do
  case "$argument" in
    -c|-E|-S|-fsyntax-only|-r|-Wl,-r|--version) link_flags=(); rpath=(); break ;;
    -static) rpath=() ;;
  esac
done
# Bash 3.2 nounset needs guarded empty arrays; absent flags must add zero arguments.
exec ''' + shlex.join([cxx, '-nostdinc++', '-isystem', str(prefix / 'include/c++/v1')])
                       + ' "$@" ${link_flags[@]+"${link_flags[@]}"} ${rpath[@]+"${rpath[@]}"}\n')
    wrapper.chmod(0o755)
    return wrapper


def check_cancellation(prefix, compiler):
    source = prefix / 'cancellation.cpp'
    source.write_text('''#include <atomic>
#include <stop_token>
#include <thread>
static_assert(_LIBCPP_VERSION / 10000 == 20);
int main() {
  std::stop_source source;
  int callbacks = 0;
  std::stop_callback callback(source.get_token(), [&] { ++callbacks; });
  if (!source.request_stop() || source.request_stop() || callbacks != 1) return 1;
  std::atomic<bool> observed = false;
  std::jthread worker([&](std::stop_token token) {
    while (!token.stop_requested()) std::this_thread::yield();
    observed = true;
  });
  worker.request_stop();
  worker.join();
  return observed ? 0 : 2;
}
''')
    executable = prefix / 'cancellation'
    subprocess.run([str(compiler), '-std=c++20', '-pthread', str(source), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
    return executable


def prepare(prefix):
    if platform.system() != 'FreeBSD':
        raise RuntimeError('this selector requires the native FreeBSD libcxxrt SDK')
    prefix.mkdir(parents=True)
    llvm_bin = Path(subprocess.check_output(['llvm-config19', '--bindir'], text=True).strip())
    cc, cxx = str(llvm_bin / 'clang'), str(llvm_bin / 'clang++')
    abi_header = Path('/usr/include/c++/v1/cxxabi.h')
    abi_runtime = Path('/lib/libcxxrt.so.1')
    provenance = {'version': VERSION, 'url': SOURCE_URL, 'sha256': SOURCE_SHA256,
                  'cc': cc, 'cxx': cxx, 'abi': 'libcxxrt', 'abi_version': 1,
                  'abi_files': {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in (abi_header, abi_runtime)}}
    (prefix / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    with (prefix / 'compiler.log').open('w') as log:
        subprocess.run([cxx, '--version'], stdout=log, stderr=subprocess.STDOUT, check=True)
    with tempfile.TemporaryDirectory(prefix='v3-libcxx-') as directory:
        work = Path(directory)
        source = download_source(work)
        commands = [configure_command(source, work / 'build', prefix, cc, cxx),
                    ['cmake', '--build', str(work / 'build'), '--target', 'install-cxx', '--parallel', '2']]
        provenance['commands'] = commands
        (prefix / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        for name, command in zip(('configure', 'build'), commands):
            with (prefix / (name + '.log')).open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    compiler = write_compiler(prefix, cxx)
    executable = check_cancellation(prefix, compiler)
    dependencies = subprocess.check_output(['ldd', str(executable)], text=True)
    (prefix / 'dependencies.log').write_text(dependencies)
    resolved = {line.split()[0]: Path(line.split()[2]).resolve()
                for line in dependencies.splitlines() if '=>' in line}
    if (resolved.get('libc++.so.1') != (prefix / 'lib/libc++.so.1').resolve()
            or resolved.get('libcxxrt.so.1') != abi_runtime.resolve()):
        raise RuntimeError('cancellation probe loaded an unselected C++ runtime')
    (prefix / 'toolchain.env').write_text('\n'.join(
        name + '=' + shlex.quote(str(value)) for name, value in
        (('V3_CXX', compiler), ('V3_CC', cc), ('V3_PACKAGE_TOOLCHAIN', prefix))) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    prepare(parser.parse_args().output.resolve())
