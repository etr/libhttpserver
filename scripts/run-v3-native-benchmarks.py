#!/usr/bin/env python3
"""Opt-in installed public native throughput/latency matrix (normal allocator)."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import secrets
import shlex
import shutil
import sys
import tempfile
import time
sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import native_benchmark as bench
import package_matrix as matrix
from package_commands import Commands, runtime_environment
from package_process import Consumer
from package_integration import compile_probe
spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('audit-v3-installed-package.py'))
audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)


def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def normal_build(makefile, env):
    flags = re.findall(r'^CXXFLAGS\s*=\s*(.*)$', makefile, re.M)
    optimization = [[option for option in shlex.split(flag) if re.fullmatch(r'-O(?:[0-9]+|g|s|z|fast)?', option)] for flag in flags]
    bench.require(optimization and all(options and options[-1] in ('-O2', '-O3') for options in optimization), 'optimized library build required')
    forbidden = r'sanitize|fprofile|coverage|jemalloc|tcmalloc|mimalloc|asan|ubsan|tsan'
    bench.require(not re.search(forbidden, makefile, re.I) or not re.search(forbidden, ' '.join(flags), re.I), 'instrumented/substituted allocator build')
    bench.require(not any(env.get(key) for key in ('LD_PRELOAD', 'DYLD_INSERT_LIBRARIES')), 'allocator/interposer injection')


def identity(args):
    source, prefix, build, provider = args.source, args.prefix, args.build, args.provider_prefix
    files = sorted(p for p in (source / 'src').rglob('*') if p.suffix in ('.cpp', '.hpp'))
    files += [source / name for name in ('configure.ac', 'src/Makefile.am', 'libhttpserver.pc.in', 'examples/host_poll.hpp',
        'test/installed/benchmark_consumer.cpp', 'test/installed/benchmark_client.py',
        'scripts/run-v3-native-benchmarks.py', 'scripts/lib/native_benchmark.py', 'scripts/lib/package_integration.py')]
    package = sorted(p for p in prefix.rglob('*') if p.is_file() and (p.suffix in ('.hpp', '.h', '.pc', '.a') or '.so' in p.name or '.dylib' in p.name or '.dll' in p.name))
    bench.require(package and (prefix / 'lib/libhttpserver.a').is_file(), 'missing installed package')
    normal_build((build / 'src/Makefile').read_text(), os.environ)
    return dict(source={str(p.relative_to(source)): digest(p) for p in files},
        package={str(p): digest(p) for p in package},
        provider={str(p): digest(p) for p in provider.rglob('*') if p.is_file() and (p.suffix in ('.a', '.h', '.pc') or '.so' in p.name)},
        build={str(build / name): digest(build / name) for name in ('config.status', 'config.log', 'src/Makefile')},
        fixtures={p.name: digest(p) for p in (source / 'test/data/tls_credentials').iterdir() if p.is_file()},
        workload={key: getattr(args, key) for key in bench.DEFAULTS}, tls=args.tls,
        compiler=args.compiler, client_python=str(Path(args.client_python).resolve()))


def validate(path, expected_identity, expected_run_id=None):
    """Offline integrity/statistics verification; fresh acceptance supplies run ID."""
    return bench.validate_receipt(bench.parse_json(Path(path).read_text()), expected_identity, expected_run_id)


def run(args):
    bench.require(not args.output.exists(), '--output must be new; failed evidence is preserved')
    args.output.mkdir(parents=True)
    receipt = dict(schema_version=1, status='failed', run_id=getattr(args, 'run_id', None) or secrets.token_hex(16), tls=args.tls, cells=[],
                   source=str(args.source), build=str(args.build), prefix=str(args.prefix),
                   full_workload=all(getattr(args, key) == value for key, value in bench.DEFAULTS.items()),
                   host=dict(platform=platform.platform(), machine=platform.machine(), cpu=platform.processor(),
                             cpu_count=os.cpu_count(), cpu_info=Path('/proc/cpuinfo').read_text() if Path('/proc/cpuinfo').exists() else None),
                   nonlocal_checks='CI / v3 PR owned; unexecuted locally')
    command = Commands(args.output)
    try:
        receipt['identity'] = identity(args)
        clean = audit.clean_environment()
        env = runtime_environment(clean, args.prefix, args.provider_prefix, [])
        env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join(str(p / 'lib/pkgconfig') for p in (args.prefix, args.provider_prefix)))
        os.environ.update({key: env[key] for key in ('LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'V3_PACKAGE_DLL_DIRS')})
        receipt['tools'] = dict(compiler=command(shlex.split(args.compiler) + ['--version'], args.output, env),
                               pkg_config=command(['pkg-config', '--version'], args.output, env),
                               clients=command([args.client_python, '-c', "import sys,ssl,importlib.metadata as m; print(sys.version); print(ssl.OPENSSL_VERSION); print(m.version('h2')); print(m.version('aioquic')); assert m.version('h2')=='4.4.1'; assert m.version('aioquic')=='1.3.0'"], args.output, env))
        client_files = command([args.client_python, '-c', "import h2,aioquic,json,pathlib; print(json.dumps([str(p) for m in (h2,aioquic) for p in pathlib.Path(m.__file__).parent.rglob('*') if p.is_file() and p.suffix in ('.py','.so')]))"], args.output, env)
        receipt['client_inputs'] = [bench.artifact(p) for p in json.loads(client_files)]
        metadata = {key: command(['pkg-config'] + flags + ['libhttpserver'], args.output, env).strip() for key, flags in
                    (('cflags', ['--cflags']), ('shared', ['--libs']), ('static', ['--libs', '--static']),
                     ('version', ['--modversion']), ('pcfiledir', ['--variable=pcfiledir']))}
        bench.require(Path(metadata['pcfiledir']).resolve() == args.prefix / 'lib/pkgconfig', 'wrong pkg-config prefix')
        bench.require(metadata['version'].startswith('3.'), 'wrong package version')
        flags = shlex.split(metadata['cflags'])
        bench.require(not audit.consumer_flag_violations(flags + shlex.split(metadata['shared']) + shlex.split(metadata['static']), (args.source, args.build)), 'private/source compile flags')
        receipt['metadata'] = metadata
        # Source/header dependency audits reject compilation anywhere under source.
        directory = Path(tempfile.mkdtemp(prefix='task198-installed-'))
        receipt['consumer_directory'] = str(directory)
        shutil.copyfile(args.source / 'test/installed/benchmark_consumer.cpp', directory / 'consumer.cpp')
        shutil.copyfile(args.source / 'examples/host_poll.hpp', directory / 'host_poll.hpp')
        shutil.copyfile(args.source / 'test/installed/benchmark_client.py', directory / 'client.py')
        fixtures = directory / 'fixtures'; shutil.copytree(args.source / 'test/data/tls_credentials', fixtures)
        receipt['consumers'] = {}
        for linkage in ('shared', 'static'):
            executable = directory / linkage
            libraries = shlex.split(metadata[linkage])
            if linkage == 'static': libraries = [str(args.prefix / 'lib/libhttpserver.a') if flag == '-lhttpserver' else flag for flag in libraries]
            libraries += ['-pthread', '-Wl,-rpath,' + str(args.prefix / 'lib'), '-Wl,-rpath,' + str(args.provider_prefix / 'lib')]
            provenance = compile_probe(args.compiler, [directory / 'consumer.cpp'], flags + ['-Wall', '-Wextra', '-Werror'], libraries,
                executable, command, env, args.source, args.build, args.prefix, False, compile_options=['-O2', '-DNDEBUG'])
            graph = audit.dependency_graph([str(executable)])
            matrix.check_linkage(graph, linkage, args.prefix)
            bench.require(not audit.graph_violations(graph, 'strict', args.tls, 'aggregate', audit.load_policy(), args.provider_prefix), 'dependency policy')
            bench.require(not re.search('asan|ubsan|tsan|jemalloc|tcmalloc|mimalloc', json.dumps(graph), re.I), 'instrumented executable')
            symbols = command(['nm', '-a', str(executable)], directory, env)
            bench.require(not re.search('__asan_|__ubsan_|__tsan_|malloc_conf|mi_malloc|tc_malloc', symbols), 'instrumented static dependency')
            receipt['consumers'][linkage] = dict(compile=provenance, dependencies=graph)
            if args.tls == 'no':
                for protocol in ('h1-tls', 'h2', 'h3'):
                    rejected = command([executable, fixtures, 'managed', protocol, receipt['run_id'], args.workers, args.payload], directory, env, timeout=15)
                    bench.require(rejected.strip() == 'UNAVAILABLE ' + protocol, 'TLS-off rejection')
            for cell in bench.required_cells(args.tls):
                if cell[0] != linkage: continue
                _, loop, protocol = cell
                out = args.output / ('-'.join(cell)); out.mkdir()
                observation_path, server_path = out / 'samples.json', out / 'server.json'
                wall_start = time.monotonic_ns()
                with Consumer([str(executable), str(fixtures), loop, protocol, receipt['run_id'], str(args.workers), str(args.payload)],
                              directory, env, server_path, timeout=15) as server:
                    port = server.port()
                    server_pid = server.process.pid
                    command([args.client_python, directory / 'client.py', '--port', port, '--fixtures', fixtures,
                        '--protocol', protocol, '--run-id', receipt['run_id'], '--warmup', args.warmup, '--requests', args.requests,
                        '--concurrency', bench.effective_concurrency(protocol, receipt['identity']['workload']), '--payload', args.payload, '--deadline', args.deadline,
                        '--output', observation_path], directory, env, timeout=args.deadline)
                    server.stop()
                observed = bench.parse_json(observation_path.read_text())
                bench.validate_observation(observed, receipt['run_id'], protocol)
                bench.validate_server(bench.parse_json(server_path.read_text()), observed)
                receipt['cells'].append(dict(linkage=linkage, loop=loop, protocol=protocol, client_exit=0, fresh_execution=True,
                    concurrency=bench.effective_concurrency(protocol, receipt['identity']['workload']), observation=bench.artifact(observation_path), server=bench.artifact(server_path),
                    server_pid=server_pid, process_wall_s=(time.monotonic_ns() - wall_start) / 1e9, statistics=observed['statistics']))
                print('PASS benchmark', linkage, loop, protocol, observed['statistics'], flush=True)
        bench.require(receipt['identity'] == identity(args), 'inputs changed during measurement')
        for record in receipt['client_inputs']: bench.verify_artifact(record)
        for linkage, consumer in receipt['consumers'].items():
            bench.require(digest(directory / linkage) == consumer['compile']['artifact_sha256'], 'consumer changed during measurement')
            for path, expected in consumer['compile']['dependency_sha256'].items():
                bench.require(digest(path) == expected, 'consumer header changed during measurement')
        receipt['status'] = 'passed'
        receipt['artifacts'] = [bench.artifact(p) for root in (directory, args.output) for p in sorted(root.rglob('*')) if p.is_file()]
        receipt['artifacts'] += receipt['client_inputs']
        bench.validate_receipt(receipt, receipt['identity'], receipt['run_id'])
    except BaseException as error:
        receipt.update(status='failed', error=str(error))
        raise
    finally:
        (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2, allow_nan=False) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'build', 'prefix', 'provider-prefix', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--tls', choices=('yes', 'no'), required=True)
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--client-python', default=sys.executable)
    parser.add_argument('--validate', type=Path)
    parser.add_argument('--expected-run-id')
    parser.add_argument('--run-id')
    for key, value in bench.DEFAULTS.items(): parser.add_argument('--' + key.replace('_', '-'), type=int, default=value)
    args = parser.parse_args()
    for name in ('source', 'build', 'prefix', 'provider_prefix', 'output'): setattr(args, name, getattr(args, name).resolve())
    bench.require(0 < args.requests <= 65536 and 0 < args.warmup <= 65536 and 0 < args.concurrency <= 8 and
                  0 < args.h3_concurrency <= 4 and 0 < args.workers <= 32 and 0 < args.payload <= 16384 and 0 < args.deadline <= 600, 'workload bounds')
    if args.validate: validate(args.validate, identity(args), args.expected_run_id)
    else: run(args)


if __name__ == '__main__': main()
