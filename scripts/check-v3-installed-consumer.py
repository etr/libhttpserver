#!/usr/bin/env python3
"""Fresh four-platform installed TLS-off/on, shared/static, managed/external matrix."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import release_artifact as release_identity
import package_matrix as matrix
from package_commands import Commands, runtime_environment
from package_integration import compile_probe, run_suites
from package_traffic import traffic

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('audit-v3-installed-package.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def package_lane(args, source, output, mode, inventory):
    audit.COMMANDS.clear()
    try:
        return _package_lane(args, source, output, mode, inventory)
    except (audit.AuditError, OSError, subprocess.TimeoutExpired) as error:
        receipt = dict(violations=[str(error)], commands=audit.COMMANDS.copy())
        if hasattr(error, 'failed_artifact'):
            receipt['failed_artifact'] = error.failed_artifact
        try:
            (output / ('tls-' + mode) / 'audit-failure.json').write_text(json.dumps(receipt, indent=2) + '\n')
        except OSError as persistence_error:
            error.add_note('could not persist audit failure evidence: ' + str(persistence_error))
        raise


def _package_lane(args, source, output, mode, inventory):
    lane = output / ('tls-' + mode)
    build, prefix, consumer_dir = lane / 'build', lane / 'prefix', lane / 'consumer'
    for directory in (build, prefix, consumer_dir):
        directory.mkdir(parents=True)
    command = Commands(lane)
    env = audit.clean_environment()
    env.pop('V3_TLS_CFLAGS', None)
    env.pop('V3_TLS_LIBS', None)
    env['CXX'] = args.compiler
    env['CC'] = args.cc
    env['MAKE'] = args.make
    pkgdirs = [root / 'lib/pkgconfig' for root in args.prerequisite_prefix] + args.pkg_config_dir
    env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join(path.as_posix() for path in pkgdirs))
    env['CPPFLAGS'] = shlex.join(['-I' + (root / 'include').as_posix() for root in args.prerequisite_prefix])
    env['LDFLAGS'] = shlex.join(['-L' + (root / 'lib').as_posix() for root in args.prerequisite_prefix])
    if os.name != 'nt':
        env['LDFLAGS'] += ' ' + shlex.join(['-Wl,-rpath,' + (root / 'lib').as_posix() for root in args.prerequisite_prefix + [args.provider_prefix]])
    env['CXXFLAGS'] = '-O3 -g' if args.optimized else '-O0 -g'
    if mode == 'yes':
        env['V3_TLS_CFLAGS'] = '-I' + (args.provider_prefix / 'include').as_posix()
        env['V3_TLS_LIBS'] = '-L' + (args.provider_prefix / 'lib').as_posix() + ' -lssl -lcrypto'
    bash = 'bash'
    configure = (source / 'configure').as_posix()
    if platform.system() == 'Windows':
        # CreateProcess searches System32 before PATH for an unqualified name.
        bash = shutil.which('bash', path=env.get('PATH', ''))
        if not bash:
            raise ValueError('bash is unavailable on the selected PATH')
        bash = str(Path(bash).resolve())
        # Autoconf splits auxiliary-file search directories on ':', so its
        # source path must use MSYS notation rather than a Windows drive name.
        configure = command([bash, '-c', 'cygpath -u "$1"', 'configure-path', configure], build, env).strip()
    command([bash, configure, '--enable-v3-tls=' + mode, '--disable-examples',
             '--prefix=' + prefix.as_posix()], build, env)
    command([args.make, '-j' + str(args.jobs)], build, env)
    command([args.make, 'install'], build, env)
    runtime_env = runtime_environment(audit.clean_environment(), prefix, args.provider_prefix, args.prerequisite_prefix, args.toolchain_prefix)
    runtime_env['MAKE'] = args.make
    runtime_env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=os.pathsep.join(path.as_posix() for path in [prefix / 'lib/pkgconfig'] + pkgdirs))
    runtime_env.update({key: env[key] for key in ('V3_TLS_CFLAGS', 'V3_TLS_LIBS') if key in env})
    runtime_env['V3_PROVIDER_PREFIX'] = str(args.provider_prefix)
    # Inspectors and configure probes must resolve the same selected provider.
    os.environ.update({key: value for key, value in runtime_env.items() if key in
                       ('LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'V3_PACKAGE_DLL_DIRS', 'V3_PACKAGE_TOOLCHAIN',
                        'V3_PACKAGE_PREREQUISITES', 'PATH')})
    command([args.make, 'check-v3-native-linkage', 'check-headers', 'check-hygiene',
             'check-v3-installed-package-fixtures'], build, dict(env, **runtime_env))
    metadata = {}
    for key, flags in (('cflags', ['--cflags']), ('libs', ['--libs']), ('static', ['--libs', '--static']),
                       ('version', ['--modversion']), ('pcfiledir', ['--variable=pcfiledir'])):
        metadata[key] = command(['pkg-config'] + flags + ['libhttpserver'], consumer_dir, runtime_env).strip()
    if Path(metadata['pcfiledir']).resolve() != (prefix / 'lib/pkgconfig').resolve():
        raise ValueError('pkg-config selected a different package')
    cflags = shlex.split(metadata['cflags'])
    violations = audit.consumer_flag_violations(cflags + shlex.split(metadata['libs']) + shlex.split(metadata['static']), (source, build))
    if violations:
        raise ValueError('\n'.join(violations))
    metadata['file'] = (prefix / 'lib/pkgconfig/libhttpserver.pc').read_text()
    (lane / 'metadata.json').write_text(json.dumps(metadata, indent=2))
    shutil.copyfile(source / 'test/installed/package_consumer.cpp', consumer_dir / 'consumer.cpp')
    shutil.copyfile(source / 'examples/host_poll.hpp', consumer_dir / 'host_poll.hpp')
    public, integrations = {}, {}
    suffix = '.exe' if os.name == 'nt' else ''
    for linkage in inventory['linkages']:
        executable = consumer_dir / (('package_consumer' if linkage == 'shared' else 'static_consumer') + suffix)
        libraries = shlex.split(metadata['libs' if linkage == 'shared' else 'static'])
        libraries += ['-L' + (root / 'lib').as_posix() for root in args.prerequisite_prefix]
        if linkage == 'static':
            if '-lhttpserver' not in libraries or not (prefix / 'lib/libhttpserver.a').is_file():
                raise ValueError('installed static archive is missing from metadata/link command')
            libraries = [(prefix / 'lib/libhttpserver.a').as_posix() if flag == '-lhttpserver' else flag for flag in libraries]
        if os.name != 'nt':
            libraries += ['-pthread'] + ['-Wl,-rpath,' + (root / 'lib').as_posix() for root in [prefix, args.provider_prefix] + args.prerequisite_prefix]
        else:
            libraries += ['-lws2_32', '-lmswsock', '-lbcrypt']
        provenance = compile_probe(args.compiler, [consumer_dir / 'consumer.cpp'], cflags, libraries,
                                   executable, command, runtime_env, source, build, prefix, False)
        graph = audit.dependency_graph([str(executable)])
        matrix.check_linkage(graph, linkage, prefix)
        public[linkage] = dict(compile=provenance, dependencies=graph, cells=[])
        for cell in matrix.cells(inventory, platform.system()):
            if cell['tls'] == mode and cell['linkage'] == linkage:
                public[linkage]['cells'].append(traffic(executable, cell, consumer_dir, runtime_env, inventory))
                (lane / 'public.json').write_text(json.dumps(public, indent=2))
        compiled_sources = []
        for name in ('compiled_operations.cpp', 'compiled_operations_worker.cpp'):
            copied = consumer_dir / name
            if not copied.exists():
                shutil.copyfile(source / 'test/installed' / name, copied)
            compiled_sources.append(copied)
        compiled_executable = consumer_dir / ('compiled_operations_' + linkage + suffix)
        compiled_provenance = compile_probe(args.compiler, compiled_sources, cflags, libraries,
                                           compiled_executable, command, runtime_env, source, build, prefix, False)
        compiled_graph = audit.dependency_graph([str(compiled_executable)])
        matrix.check_linkage(compiled_graph, linkage, prefix)
        command([str(compiled_executable)], consumer_dir, runtime_env)
        public[linkage]['compiled_operations'] = dict(compile=compiled_provenance, dependencies=compiled_graph)
        configured_nm = re.search(r'^NM = (.*)$', (build / 'Makefile').read_text(), re.M)
        if configured_nm is None:
            raise ValueError('generated Makefile is missing configured NM')
        command([sys.executable, str(source / 'scripts/check-v3-compiled-operations.py'),
                 '--nm=' + configured_nm[1], str(prefix / 'lib/libhttpserver.a')], consumer_dir, runtime_env)
        private_dir = lane / ('integration-' + linkage)
        private_dir.mkdir()
        private_libraries = libraries + (shlex.split(env['V3_TLS_LIBS']) if mode == 'yes' else [])
        integrations[linkage] = run_suites(inventory, platform.system(), mode, linkage, private_dir, prefix,
                                          source, build, args.compiler, cflags, private_libraries, command, runtime_env, audit)
    command([sys.executable, str(source / 'scripts/check-v3-public-tls.py'), '--prefix', str(prefix),
             '--provider-prefix', str(args.provider_prefix), '--build', str(build), '--tls', mode,
             '--compiler', args.compiler, '--output', str(lane / 'public-tls')], source, runtime_env)
    command([args.client_python, str(source / 'scripts/check-v3-public-http2.py'), '--prefix', str(prefix),
             '--provider-prefix', str(args.provider_prefix), '--build', str(build), '--tls', mode,
             '--compiler', args.compiler, '--node', args.node, '--output', str(lane / 'public-http2')], source, runtime_env)
    receipt = audit.audit(prefix, build, consumer_dir / ('package_consumer' + suffix), mode,
                          'strict', args.provider_prefix, args.compiler)
    receipt.update(consumer_metadata=metadata, public=public, integrations=integrations)
    (lane / 'audit.json').write_text(json.dumps(receipt, indent=2))
    if receipt['violations']:
        raise ValueError('\n'.join(receipt['violations']))
    print('PASS installed consumer native TLS=' + mode, flush=True)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--provider-prefix', type=Path, required=True)
    parser.add_argument('--prerequisite-prefix', type=Path, action='append', default=[])
    parser.add_argument('--pkg-config-dir', type=Path, action='append', default=[])
    parser.add_argument('--toolchain-prefix', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--client-python', default=sys.executable)
    parser.add_argument('--node', default='node')
    parser.add_argument('--make', default='make')
    parser.add_argument('--optimized', action='store_true', help='Build optimized production libraries for release performance gates')
    args = parser.parse_args()
    source = Path(__file__).resolve().parent.parent
    for key in ('output', 'provider_prefix', 'toolchain_prefix'):
        if getattr(args, key):
            setattr(args, key, getattr(args, key).resolve())
    args.prerequisite_prefix = [path.resolve() for path in args.prerequisite_prefix]
    args.pkg_config_dir = [path.resolve() for path in args.pkg_config_dir]
    output = args.output
    if output.exists():
        raise ValueError('--output must be fresh; existing evidence is preserved')
    output.mkdir(parents=True)
    summary = dict(version=2, platform=platform.platform(), host=platform.system(), source=str(source),
                   inventory_sha256=hashlib.sha256((source / 'test/installed/v3-package-matrix.json').read_bytes()).hexdigest(),
                   compiler=args.compiler, nonlocal_checks='deferred to CI / v3 PR', lanes={})
    try:
        inventory = matrix.load()
        matrix.cells(inventory, platform.system())
        identity = Commands(output)
        summary['revision'] = release_identity.revision(source)
        summary['branch'] = ('source-archive' if os.environ.get('V3_RELEASE_IDENTITY') else
                             identity(['git', 'branch', '--show-current'], source, audit.clean_environment()).strip())
        summary['release_artifact'] = release_identity.source_identity(source)
        summary['compiler_version'] = identity(shlex.split(args.compiler) + ['--version'], source, audit.clean_environment())
        summary['python_version'] = sys.version
        summary['tool_versions'] = {tool: identity([tool, '--version'], source, audit.clean_environment())
                                    for tool in ('pkg-config', args.make)}
        provider_header = args.provider_prefix / 'include/openssl/opensslv.h'
        summary['provider_header_sha256'] = hashlib.sha256(provider_header.read_bytes()).hexdigest()
        summary['provider_header'] = provider_header.read_text()
        summary['source_hashes'] = {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in
                                   ('configure.ac', 'src/Makefile.am', 'libhttpserver.pc.in', 'examples/host_poll.hpp')}
        receipts = [package_lane(args, source, output, mode, inventory) for mode in inventory['tls']]
        headers = [receipt['headers'] for receipt in receipts]
        if any(headers[0][key] != headers[1][key] for key in ('files', 'declaration_sha256')):
            raise ValueError('TLS-on/off installed files or effective declarations differ')
        summary['lanes'] = {receipt['tls_mode']: dict(status='passed', public_cells=4,
                           suite_counts={linkage: len(suites) for linkage, suites in receipt['integrations'].items()}) for receipt in receipts}
        summary['status'] = 'passed'
        print('PASS identical installed header sets and effective declarations', flush=True)
    except Exception as error:
        summary.update(status='failed', error=str(error))
        raise
    finally:
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
