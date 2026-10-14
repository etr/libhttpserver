"""Recheck installed public compile, link, command and process artifacts."""
from pathlib import Path
import hashlib
import importlib.util
import re
import shlex
import os
from v3_native_benchmark import require, verify_artifact, parse_json
from v3_package_commands import dependencies
from v3_package_matrix import check_dependencies, check_linkage


def validate_process(record, require_port=False):
    require(record.get('exit') == 0 and 'error' not in record and record.get('command'), 'process exit/cleanup failure')
    if require_port:
        lines = record.get('stdout', '').splitlines()
        require(lines and all(word.isdecimal() and 0 < int(word) <= 65535 for word in lines[0].split()), 'missing bound readiness port')


def validate_compile(compiled, linkage, identity, artifacts, commands):
    require(compiled.get('private_test_headers') is False, 'private consumer')
    sources, deps = compiled.get('sources', {}), compiled.get('dependency_sha256', {})
    require(sources and deps and set(compiled['dependencies']) == set(deps), 'empty or inconsistent compile provenance')
    prefix, source, build = [Path(identity[key]) for key in ('prefix', 'source', 'build')]
    check_dependencies(deps, prefix, source, build, private=False)
    public_units = ('examples/', 'test/installed/', 'test/examples/')
    members = {digest for name, digest in identity['archive']['members'].items() if name.startswith(public_units)}

    def bound(path, digest=None):
        path = str(Path(path).resolve())
        require(path in artifacts, 'unbound compile artifact: ' + path)
        verify_artifact(artifacts[path])
        require(digest is None or artifacts[path]['sha256'] == digest, 'compile digest differs: ' + path)

    for path, digest in sources.items():
        require(digest in members, 'source outside supplied archive')
        bound(path, digest)
    require(set(sources) <= set(deps), 'sources missing from dependencies')
    require(any(path in identity['package'] for path in deps), 'no installed header dependency')
    for path, digest in deps.items():
        if path in identity['package']:
            require(digest == identity['package'][path] and hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest, 'installed header changed')
        else:
            require(digest in members, 'private or foreign dependency')
            bound(path, digest)
    linkmap = Path(compiled['link_map'])
    bound(linkmap)
    executable = str(linkmap)[:-4]
    bound(executable, compiled['artifact_sha256'])
    selected = [row for row in commands if row['argv'] and '-o' in row['argv'] and
                row['argv'][row['argv'].index('-o') + 1] == executable]
    require(len(selected) == 1, 'missing actual link command')
    link = selected[0]['argv']
    require('-Wl,-Map,' + str(linkmap) in link, 'wrong link map command')
    if linkage == 'static':
        archive = str(prefix / 'lib/libhttpserver.a')
        require(archive in link and archive in linkmap.read_text(), 'different installed static archive')
    compile_rows = [row for row in commands if '-c' in row['argv'] and '-o' in row['argv'] and
                    row['argv'][row['argv'].index('-o') + 1] in link and
                    row['argv'][row['argv'].index('-c') + 1] in sources]
    require(len(compile_rows) == len(sources), 'missing actual compile command')
    compiler = shlex.split(identity['compiler'])
    require(link[:len(compiler)] == compiler, 'different link compiler')
    for row in compile_rows:
        argv = row['argv']
        require(argv[:len(compiler)] == compiler, 'different compile compiler')
        require('-std=c++20' in argv and not any(re.match(r'-D(?:HTTPSERVER_COMPILATION|_HTTPSERVER_HPP_INSIDE_|HAVE_\w*|NATIVE_V3_TLS)(?:=|$)', flag) or flag in ('-include', '-imacros') for flag in argv), 'private/non-C++20 compile')
        for i, flag in enumerate(argv):
            include = argv[i + 1] if flag in ('-I', '-isystem') else flag[2:] if flag.startswith('-I') else None
            if include:
                require(not any(Path(include).resolve().is_relative_to(root) for root in (source, build)), 'source/build include')
        require('-MF' in argv and '-MMD' in argv, 'missing depfile command')
        depfile = argv[argv.index('-MF') + 1]
        bound(depfile)
        require(set(dependencies(Path(depfile))) <= set(deps), 'depfile/receipt disagreement')
        require(row['argv'][row['argv'].index('-c') + 1] in dependencies(Path(depfile)), 'empty depfile')
        bound(argv[argv.index('-o') + 1])


def catalog(roots, extras=(), excluded=()):
    paths = {str(p.resolve()) for root in roots for p in Path(root).rglob('*') if p.is_file()}
    paths.update(str(Path(p).resolve()) for p in extras)
    paths.difference_update(str(Path(p).resolve()) for p in excluded)
    from v3_native_benchmark import artifact
    return [artifact(p) for p in sorted(paths)]


def tls_rejection(record, stdout):
    argv = record.get('command', record.get('argv', []))
    if (record.get('exit') != 2 or 'error' in record or len(argv) != 9 or
            Path(argv[0]).name != 'independent-client' or not argv[1].isdecimal() or not 0 < int(argv[1]) <= 65535 or
            argv[2] not in ('12', '13') or argv[3] not in ('psk', 'mtls', 'certificate', 'acme') or argv[7] != 'once'):
        return False
    rows = [parse_json(line) for line in stdout.splitlines() if line.startswith('{')]
    return bool(rows) and all(row.get('connected') is False and row.get('early_sent') == 0 for row in rows)


def validate_catalog(receipt, path):
    records = receipt.get('artifacts', [])
    require(records, 'empty artifact catalog')
    for record in records: verify_artifact(record)
    require(records == catalog(receipt['artifact_roots'], receipt['artifact_extras'], [Path(receipt['artifact_roots'][0]) / 'receipt.json']), 'pruned or duplicate artifact catalog')
    artifacts = {row['path']: row for row in records}
    commands = []
    for record in records:
        location = Path(record['path'])
        if location.name == 'commands.json':
            rows = parse_json(location.read_text())
            require(rows, 'empty command log')
            for row in rows:
                require(row.get('argv') and 'error' not in row and (row.get('exit') == 0 or
                        tls_rejection(row, Path(row['log']).read_text())), 'failed/nonzero/timeout command')
                log = artifacts.get(str(Path(row['log']).resolve()))
                require(log and (log['sha256'], log['bytes']) == (row['sha256'], row['bytes']), 'unbound command output')
            commands.extend(rows)
        elif location.suffix == '.json':
            value = parse_json(location.read_text())
            if isinstance(value, dict) and 'command' in value and 'stdout' in value:
                require(str(Path(value['command'][0]).resolve()) in artifacts, 'unbound process executable')
                if not tls_rejection(value, value['stdout']):
                    argv = value['command']
                    controlled_close = (location.name == 'blocked-peer_close.json' and 'error' not in value and
                        value.get('exit') == (1 if os.name == 'nt' else -9) and len(argv) == 9 and
                        Path(argv[0]).name == 'independent-client' and argv[2:4] == ['13', 'psk'] and
                        argv[6:8] == ['block', 'once'] and value['stdout'] == '')
                    if not controlled_close:
                        executable = str(Path(argv[0]).resolve())
                        selected = receipt.get('identity', {}).get('tools', {}).get('quic_go', {}).get('path')
                        if executable == selected:
                            from v3_public_http3_inventory import validate_stop_observation
                            validate_process(value)
                            lines = value['stdout'].splitlines()
                            require(len(lines) == 2 and lines[0] == 'H3_ACTIVE', 'missing independent active/terminal witness')
                            terminal = parse_json(lines[1])
                            require(terminal.get('status') == 'passed' and terminal.get('client') == 'quic-go' and terminal.get('version') == 'v0.55.0', 'wrong independent terminal client')
                            validate_stop_observation(terminal)
                        elif Path(argv[0]).name == 'independent-client':
                            validate_process(value)
                            require(value['stdout'], 'empty independent TLS client response')
                        elif len(argv) == 6 and argv[1] == 'no' and argv[4:] == ['off', '0'] and Path(argv[0]).name.endswith('-profiles'):
                            validate_process(value)
                            require(value['stdout'].strip() == 'PROFILES_OFF 5', 'wrong typed profile rejection')
                        else:
                            validate_process(value, require_port=True)
    require(commands, 'missing command execution')
    return artifacts, commands


def validate_runtime(executable, linkage, identity):
    spec = importlib.util.spec_from_file_location('adapter_audit', Path(identity['source']) / 'scripts/audit-v3-installed-package.py')
    audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)
    graph = audit.dependency_graph([str(executable)])
    check_linkage(graph, linkage, Path(identity['prefix']))
    require(not audit.graph_violations(graph, 'strict', identity['tls'], 'aggregate', audit.load_policy(),
                                      Path(identity.get('provider_prefix', '/nonexistent'))), 'runtime dependency policy')
    return graph


def validate_fixture_client(client, identity, artifacts, commands):
    executable = client.get('executable', client.get('path'))
    require(executable in artifacts and artifacts[executable]['sha256'] == client['sha256'], 'unbound independent TLS client')
    links = [row for row in commands if '-o' in row['argv'] and row['argv'][row['argv'].index('-o') + 1] == executable]
    require(len(links) == 1 and '-std=c++20' in links[0]['argv'], 'missing independent client compile command')
    units = [arg for arg in links[0]['argv'] if Path(arg).name == 'v3_tls_client.cpp']
    require(len(units) == 1, 'missing independent client source')
    unit = units[0]
    permitted = {str(Path(executable).parent / 'v3_tls_client.cpp'), str(Path(identity['source']) / 'test/installed/v3_tls_client.cpp')}
    require(unit in permitted and unit in artifacts and artifacts[unit]['sha256'] == identity['archive']['members']['test/installed/v3_tls_client.cpp'], 'independent client source differs')
    compiler = shlex.split(identity['compiler'])
    require(links[0]['argv'][:len(compiler)] == compiler, 'different independent client compiler')


def validate_child_provenance(child, identity, artifacts, commands):
    require(child.get('status') == 'passed' and child.get('run_id'), 'failed child receipt')
    for name, digest in child.get('source_hashes', {}).items():
        require(identity['archive']['members'].get(name) == digest, 'child source outside archive')
    require(child.get('library_hashes') and all(identity['package'].get(path) == digest for path, digest in child['library_hashes'].items()), 'different installed child library')
    cells = child.get('cells', [])
    require(len(cells) == 4 and {(c['linkage'], c['loop']) for c in cells} ==
            {(a, b) for a in ('shared', 'static') for b in ('managed', 'external')}, 'missing/duplicate child cells')
    client = child.get('independent_client', child.get('credential_fixture_tool'))
    if client: validate_fixture_client(client, identity, artifacts, commands)
    seen = set()
    for cell in cells + child.get('examples', []):
        if 'loop' in cell: require(cell.get('status', 'passed') == 'passed', 'failed/skipped cell')
        for key, compiled in cell.items():
            if key != 'compile' and not key.endswith('_compile'): continue
            signature = (compiled['link_map'], cell['linkage'])
            if signature not in seen:
                validate_compile(compiled, cell['linkage'], identity, artifacts, commands)
                validate_runtime(compiled['link_map'][:-4], cell['linkage'], identity)
                seen.add(signature)
        if 'dependencies' in cell: check_linkage(cell['dependencies'], cell['linkage'], Path(identity['prefix']))
    for cell in cells:
        for key in ('profile_dependencies', 'example_dependencies'):
            if key in cell: check_linkage(cell[key], cell['linkage'], Path(identity['prefix']))
