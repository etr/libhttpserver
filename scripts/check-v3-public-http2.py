#!/usr/bin/env python3
"""Compile and exercise installed public-only native H2, shared/static and both loops."""
import argparse
import hashlib
import importlib.metadata
import importlib.util
import json
import os
import re
from pathlib import Path
import shlex
import shutil
import ssl
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts/lib'))
from v3_native_benchmark import require
from v3_package_commands import Commands, runtime_environment
from v3_package_integration import compile_probe
from v3_package_matrix import check_linkage, check_scenarios
from v3_package_process import Consumer
from v3_public_http2_inventory import PYTHON, NODE, validate_client, validate_public, POLICY, ONESHOTS



def scenarios(executable, loop, tls, directory, env, fixtures, commands, node, source):
    from v3_public_http2_h2 import Client, basic, timeouts, drain, flow_credit, running_reset, budget_limits, queued_reset, disconnect, idle_deadline, require
    from v3_public_http2_policy import policy, websocket
    rows = []
    if tls == 'no':
        out = commands([executable, fixtures, loop, 'both'], directory, env)
        require(out.strip() == 'TLS_OFF_H2_REJECTED', 'TLS-off H2 rejection inventory')
        return [dict(case='tls_off_h2_rejected', status='passed')]
    require(importlib.metadata.version('h2') == '4.4.1', 'pinned Python-h2 unavailable')
    for family in sorted(PYTHON.keys() | POLICY.keys()):
        mode = 'timeouts' if family in ('timeouts', 'header', 'websocket') else family if family in POLICY or family.endswith('-limit') else 'both'
        process_receipt = directory / ('python-' + family + '-process.json')
        with Consumer([str(executable), str(fixtures), loop, mode], directory, env, process_receipt) as child:
            def command(text):
                child.process.stdin.write((text + '\n').encode()); child.process.stdin.flush()
                return child.line()
            port = child.port()
            if family == 'basic': observations = basic(port, fixtures, command)
            elif family == 'timeouts': observations = timeouts(port, fixtures)
            elif family.startswith('drain-'): observations = drain(port, fixtures, family == 'drain-finish', command)
            elif family == 'flow': observations = flow_credit(port, fixtures)
            elif family == 'running-reset': observations = running_reset(port, fixtures)
            elif family.endswith('-limit'): observations = budget_limits(port, fixtures, family, command)
            elif family == 'queued': observations = queued_reset(port, fixtures, command)
            elif family == 'disconnect': observations = disconnect(port, fixtures, command)
            elif family in ('header', 'settings'): observations = idle_deadline(port, fixtures, family)
            elif family == 'websocket': observations = websocket(port, fixtures, command)
            else: observations = policy(port, fixtures, family, command)
            result = dict(client='hyper-h2', version='4.4.1', alpn='h2', scenarios=observations)
            child.stop()
        child_result = json.loads(process_receipt.read_text())
        if loop == 'external': require('INACTIVE_DRIVER_REJECTED' in child_result['stderr'], 'inactive external driver rejection absent')
        if family.startswith('drain-'):
            expected = r'RETAINED_TICKET 0 0' if family == 'drain-finish' else r'RETAINED_TICKET 1 [1-9]\d*'
            require(re.search(expected, child_result['stderr']) is not None, 'retained native drain ticket result absent')
            observations.append(dict(case='ticket_after_server_destruction', status='passed'))
        validate_client(result, 'python', family)
        rows.append(dict(family=family, **result, process_receipt=str(process_receipt)))
    for mode, (expected, case) in ONESHOTS.items():
        out = commands([executable, fixtures, loop, mode], directory, env)
        require(out.strip() == expected, 'H2 configuration predicate: ' + mode)
        rows.append(dict(family=mode, scenarios=[dict(case=case, status='passed')]))
    for family in NODE:
        output = commands([node, source / 'test/installed/v3_http2_node.mjs', executable, fixtures, family, loop], directory, env, timeout=25)
        result = json.loads(output.strip().splitlines()[-1])
        validate_client(result, 'node', family)
        require(result['server']['executable'] == str(executable) and result['server']['exit'] == 0, 'Node public server identity')
        if loop == 'external': require('INACTIVE_DRIVER_REJECTED' in result['server']['stderr'], 'Node inactive external driver rejection absent')
        rows.append(dict(family=family, **result))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prefix', 'provider-prefix', 'build', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--source', type=Path, default=ROOT)
    parser.add_argument('--tls', choices=('yes', 'no'), required=True)
    parser.add_argument('--compiler', default='/usr/bin/clang++-18')
    parser.add_argument('--node', default='node')
    parser.add_argument('--sanitize', default='')
    parser.add_argument('--run-id')
    return run(parser.parse_args())


def run(args):
    from v3_public_http2_h2 import Client
    args.output = args.output.resolve(); args.output.mkdir(parents=True, exist_ok=False)
    prefix, source, build, provider = [p.resolve() for p in (args.prefix, args.source, args.build, args.provider_prefix)]
    command = Commands(args.output)
    env = runtime_environment(os.environ, prefix, provider, [])
    env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'))
    receipt = dict(version=1, run_id=args.run_id, predicate='installed-public-http2', tls=args.tls, prefix=str(prefix),
                   source=str(source), build=str(build), command=sys.argv, cells=[],
                   required_python=PYTHON, required_node=NODE, required_policy=POLICY,
                   environment={key: env.get(key) for key in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'TSAN_OPTIONS', 'LD_LIBRARY_PATH')})
    try:
        spec = importlib.util.spec_from_file_location('audit', source / 'scripts/audit-v3-installed-package.py')
        audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)
        receipt['revision'] = receipt['branch'] = None
        receipt['compiler'] = command(shlex.split(args.compiler) + ['--version'], source, env)
        receipt['client'] = dict(executable=sys.executable, version=sys.version, ssl=ssl.OPENSSL_VERSION,
                                sha256=hashlib.sha256(Path(sys.executable).read_bytes()).hexdigest())
        receipt['provider'] = dict(prefix=str(provider), libraries={str(p): hashlib.sha256(p.read_bytes()).hexdigest()
            for pattern in ('libssl.so*', 'libcrypto.so*', 'libssl.a', 'libcrypto.a') for p in (provider / 'lib').glob(pattern) if p.is_file()}) if args.tls == 'yes' else None
        if args.tls == 'yes':
            receipt['python_modules'] = {}
            for package in ('h2', 'hpack', 'hyperframe'):
                module = importlib.import_module(package)
                directory = Path(module.__file__).parent
                receipt['python_modules'][package] = dict(version=importlib.metadata.version(package),
                    sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(directory.rglob('*.py'))})
        if args.tls == 'yes':
            node = shutil.which(args.node); require(node is not None, 'missing pinned Node')
            receipt['node'] = dict(executable=node, sha256=hashlib.sha256(Path(node).read_bytes()).hexdigest(),
                                   version=command([node, '--version'], source, env).strip())
            require(receipt['node']['version'] == 'v24.15.0', 'Node pin differs')
        else: node = args.node
        inputs = sorted(p for p in (source / 'src').rglob('*') if p.suffix in ('.cpp', '.hpp'))
        inputs += [source / p for p in ('src/Makefile.am', 'configure.ac', 'test/installed/v3_http2_consumer.cpp',
                   'test/installed/v3_http2_node.mjs', 'test/installed/v3_http2_raw.mjs', 'examples/v3_http2.cpp', 'examples/v3_example_ws.hpp', 'examples/v3_tls_unavailable.hpp', 'scripts/check-v3-public-http2.py')]
        inputs += sorted((source / 'scripts/lib').glob('v3_public_http2*.py'))
        receipt['source_hashes'] = {str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        receipt['source_identity'] = hashlib.sha256(json.dumps(receipt['source_hashes'], sort_keys=True).encode()).hexdigest()
        receipt['library_hashes'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (prefix / 'lib').glob('libhttpserver.*') if p.is_file()}
        pcdir = command(['pkg-config', '--variable=pcfiledir', 'libhttpserver'], args.output, env).strip()
        require(Path(pcdir).resolve() == prefix / 'lib/pkgconfig', 'wrong installed pkg-config prefix')
        cflags = shlex.split(command(['pkg-config', '--cflags', 'libhttpserver'], args.output, env))
        require(not audit.consumer_flag_violations(cflags, (source, build)), 'source/provider contaminated compile flags')
        consumer = Path(tempfile.mkdtemp(prefix='task195-installed-'))
        receipt['consumer_directory'] = str(consumer)
        for p in ('test/installed/v3_http2_consumer.cpp', 'examples/v3_host_poll.hpp', 'examples/v3_http2.cpp', 'examples/v3_example_ws.hpp', 'examples/v3_tls_unavailable.hpp'):
            shutil.copyfile(source / p, consumer / Path(p).name)
        fixtures = consumer / 'fixtures'; shutil.copytree(source / 'test/data/tls_credentials', fixtures)
        (fixtures / 'client-valid-chain.pem').write_bytes((fixtures / 'client-valid.pem').read_bytes() + (fixtures / 'client-intermediate.pem').read_bytes())
        (fixtures / 'a-chain.pem').write_bytes((fixtures / 'a.pem').read_bytes() + (fixtures / 'intermediate.pem').read_bytes())
        receipt['fixture_hashes'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in fixtures.iterdir() if p.is_file()}
        for linkage in ('shared', 'static'):
            flags = ['--libs'] + (['--static'] if linkage == 'static' else [])
            libraries = shlex.split(command(['pkg-config'] + flags + ['libhttpserver'], consumer, env))
            if linkage == 'static': libraries = [str(prefix / 'lib/libhttpserver.a') if f == '-lhttpserver' else f for f in libraries]
            libraries += ['-pthread', '-Wl,-rpath,' + str(prefix / 'lib'), '-Wl,-rpath,' + str(provider / 'lib')] + shlex.split(args.sanitize)
            executable = consumer / linkage
            provenance = compile_probe(args.compiler, [consumer / 'v3_http2_consumer.cpp'], cflags + shlex.split(args.sanitize),
                                       libraries, executable, command, env, source, build, prefix, False)
            validate_public(True, provenance['dependencies'], prefix, source, build)
            graph = audit.dependency_graph([str(executable)]); check_linkage(graph, linkage, prefix)
            violations = audit.graph_violations(graph, 'strict', args.tls, 'aggregate', audit.load_policy(), provider)
            require(not violations, 'dependency audit: ' + repr(violations))
            if args.tls == 'yes':
                example = consumer / (linkage + '-example')
                example_compile = compile_probe(args.compiler, [consumer / 'v3_http2.cpp'], cflags + shlex.split(args.sanitize),
                                                libraries, example, command, env, source, build, prefix, False)
                example_graph = audit.dependency_graph([str(example)]); check_linkage(example_graph, linkage, prefix)
                with Consumer([str(example), str(fixtures / 'a-chain.pem'), str(fixtures / 'a-key.pem')], consumer, env,
                              args.output / (linkage + '-example-process.json')) as child:
                    client = Client(child.port(), fixtures)
                    require(client.get('/hello')['body'] == b'hello h2\n', 'installed public H2 example')
                    client.close(); child.stop()
                receipt.setdefault('examples', []).append(dict(linkage=linkage, compile=example_compile, dependencies=example_graph))
            for loop in ('managed', 'external'):
                directory = args.output / (linkage + '-' + loop); directory.mkdir()
                cell = dict(linkage=linkage, loop=loop, compile=provenance, dependencies=graph)
                receipt['cells'].append(cell)
                cell['families'] = scenarios(executable, loop, args.tls, directory, env, fixtures, command, node, source)
                if loop == 'external' and args.tls == 'yes':
                    cell['readiness'] = []
                    for row in cell['families']:
                        if row.get('family') != 'disconnect': continue
                        stderr = row['server']['stderr'] if row['client'] == 'node:http2' else json.loads(Path(row['process_receipt']).read_text())['stderr']
                        stale = re.search(r'STALE_EVENTS_IGNORED (\d+)', stderr)
                        require(stale is not None and int(stale[1]) >= 2, 'missing stale connection readiness events')
                        cell['readiness'].append(dict(client=row['client'], case='external_stale_generation_after_close', stale_events=int(stale[1])))
                cell['status'] = 'passed'
                print('PASS installed H2', args.tls, linkage, loop, len(cell['families']), flush=True)
        require(len(receipt['cells']) == 4, 'incomplete installed H2 matrix')
        receipt['status'] = 'passed'
    except BaseException as error:
        receipt.update(status='failed', error=str(error)); raise
    finally:
        (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


if __name__ == '__main__':
    main()
