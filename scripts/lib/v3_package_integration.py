"""Compile named test tools against installed libraries, retaining provenance."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import time
from v3_package_commands import dependencies
import v3_package_matrix as matrix


def compile_probe(compiler, sources, flags, libraries, executable, command, env, source, build, prefix, private):
    objects, included = [], set()
    for index, unit in enumerate(sources):
        obj = executable.parent / (executable.stem + '-' + str(index) + '.o')
        depfile = obj.with_suffix('.d')
        command(shlex.split(compiler) + ['-std=c++20', '-O0', '-g'] + flags +
                ['-MMD', '-MF', str(depfile), '-c', str(unit), '-o', str(obj)], executable.parent, env)
        included.update(dependencies(depfile))
        objects.append(str(obj))
    matrix.check_dependencies(included, prefix, source, build, private)
    command(shlex.split(compiler) + objects + libraries + ['-Wl,-Map,' + str(executable) + '.map', '-o', str(executable)],
            executable.parent, env)
    return {'dependencies': sorted(included), 'sources': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
            'artifact_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(), 'private_test_headers': private,
            'dependency_sha256': {path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in sorted(included)},
            'link_map': str(executable) + '.map'}


def run_suites(inventory, host, mode, linkage, directory, prefix, source, build, compiler, cflags, libraries, command, env, audit):
    receipts = {}
    for name, definition in matrix.suites(inventory, host, mode).items():
        suite_dir = directory / name
        suite_dir.mkdir()
        executable = suite_dir / (name + ('.exe' if os.name == 'nt' else ''))
        receipt = dict(name=name, tls=mode, linkage=linkage, execution=definition.get('execution', 'installed-library'))
        try:
            flags = cflags + ['-DHTTPSERVER_COMPILATION', '-I' + str(source / 'src'), '-I' + str(source / 'test')]
            for macro, value in (('TLS_TEST_DIR', source / 'test'), ('PARITY_TRANSCRIPT_DIR', source / 'test/parity/transcripts'),
                                 ('PARITY_DATA_ROOT', source / 'test'), ('HTTP2_CORPUS_DIR', source / 'test/conformance/http2')):
                flags += ['-D' + macro + '="' + value.as_posix() + '"']
            if mode == 'yes':
                flags += shlex.split(env['V3_TLS_CFLAGS'])
            units = [source / 'test' / unit for unit in definition['sources']]
            if any(not unit.is_relative_to(source / 'test') for unit in units):
                raise ValueError('integration may compile only test sources')
            receipt['compile'] = compile_probe(compiler, units, flags, libraries, executable, command, env, source, build, prefix, True)
            first_command = len(audit.COMMANDS)
            graph = audit.dependency_graph([str(executable)])
            graph['consumer_roles'] = {str(executable.resolve()): 'static_consumer' if linkage == 'static' else 'consumer'}
            receipt.update(dependencies=graph, inspection_commands=audit.COMMANDS[first_command:])
            if receipt['execution'] != 'header-only':
                matrix.check_linkage(graph, linkage, prefix)
            elif linkage == 'static':
                matrix.check_linkage(graph, linkage, prefix)
            violations = audit.graph_violations(graph, 'pre-cutover', mode, 'aggregate', audit.load_policy(), Path(env['V3_PROVIDER_PREFIX']))
            if violations:
                raise ValueError('\n'.join(violations))
            started = time.monotonic()
            output = command([str(executable)], source / 'test', env, timeout=180, allow_failure=True)
            receipt.update(matrix.suite_result(output, command.records[-1]['exit']))
            missing = set(definition.get('required_cases', [])) - set(receipt['case_names'])
            if missing:
                raise ValueError('required suite cases absent: ' + ', '.join(sorted(missing)))
            receipt.update(duration=time.monotonic() - started, log=command.records[-1]['log'])
            if name in ('tls_io_loopback', 'native_http1_tls_parity'):
                matrix.check_tls_witnesses(output, inventory['platforms'][host])
            if name == 'native_http1_tls_parity':
                receipt['encrypted_replays'] = matrix.parity_observation_counts(output)
            if host == 'Windows' and name == 'io_iocp_backend':
                for scenario in ('managed_windows_selects_iocp', 'read_delivers_bytes_exactly_once',
                                 'pending_accept_cancel_and_concurrent_close_drain_every_native_request',
                                 'queued_success_cancel_not_found_retains_storage_until_packet'):
                    if scenario not in output:
                        raise ValueError('missing IOCP scenario: ' + scenario)
            receipt['status'] = 'passed'
        except Exception as error:
            receipt.update(status='failed', error=str(error))
            raise
        finally:
            receipts[name] = receipt
            (directory / 'suites.json').write_text(json.dumps(receipts, indent=2) + '\n')
    return receipts
