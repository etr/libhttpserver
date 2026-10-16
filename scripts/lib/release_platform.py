"""Fixed platform receipt bundles: immutable logical paths, never foreign execution."""
import hashlib
import json
import posixpath
from pathlib import Path, PurePosixPath, PureWindowsPath
import shutil
import tarfile

from release_ci import require
import release_artifact as identity

PLATFORMS = ('FreeBSD', 'Darwin', 'Windows')
MAX_FILES = 20000
MAX_BYTES = 8 * 1024 * 1024 * 1024


def read_document(path):
    def unique(pairs):
        value = {}
        for key, child in pairs:
            require(key not in value, 'duplicate platform receipt/catalog key')
            value[key] = child
        return value
    require(path.is_file() and path.stat().st_size <= 64 * 1024 * 1024, 'missing/oversized platform document')
    return json.loads(path.read_text(), object_pairs_hook=unique)


def record(path):
    return dict(sha256=identity.sha256(path), bytes=Path(path).stat().st_size)


def logical_key(value):
    windows = PureWindowsPath(str(value))
    return posixpath.normpath(windows.as_posix() if windows.drive else str(value))


def provenance_path(value):
    windows = PureWindowsPath(str(value))
    if windows.drive:
        # Comparison-only namespace; these paths are never opened or executed.
        return Path('/__v3_foreign_windows__') / windows.drive.rstrip(':') / Path(*windows.parts[1:])
    return Path(value)


def retain(destination, platform, receipt, paths):
    require(platform in PLATFORMS, 'unknown platform namespace')
    destination.mkdir(parents=True, exist_ok=False)
    (destination / 'bytes').mkdir()
    catalog = {}
    total = 0
    paths = set(map(Path, paths))
    paths |= {path.resolve() for path in paths}
    for path in sorted(paths):
        logical = str(path)
        value = record(path); total += value['bytes']
        require(len(catalog) < MAX_FILES and total <= MAX_BYTES, 'platform evidence exceeds bounds')
        name = 'bytes/' + value['sha256']
        target = destination / name
        if not target.exists(): shutil.copyfile(path, target)
        require(record(target) == value, 'platform artifact changed during retention')
        catalog[logical] = dict(value, retained=name)
    (destination / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    (destination / 'catalog.json').write_text(json.dumps(dict(schema_version=1, platform=platform, receipt=record(destination / 'receipt.json'), paths=catalog), indent=2) + '\n')
    return Catalog(destination)


class Catalog:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        require(not any(p.is_symlink() for p in self.directory.rglob('*')), 'platform bundle symlink')
        require((self.directory / 'catalog.json').is_file() and (self.directory / 'catalog.json').stat().st_size <= 64 * 1024 * 1024, 'oversized platform catalog')
        value = read_document(self.directory / 'catalog.json')
        require(value.get('schema_version') == 1 and value.get('platform') in PLATFORMS, 'unknown platform namespace')
        self.platform, self.paths = value['platform'], value['paths']
        self.receipt_record = value['receipt']
        self.catalog_record = record(self.directory / 'catalog.json')
        self.verify_metadata()
        require(0 < len(self.paths) < MAX_FILES, 'empty/oversized platform catalog')
        require(sum(r['bytes'] for r in self.paths.values()) <= MAX_BYTES, 'oversized platform bytes')
        self.logical_paths = {}
        for logical in self.paths:
            key = logical_key(logical)
            previous = self.logical_paths.get(key)
            require(previous is None or self.paths[previous] == self.paths[logical], 'conflicting logical path aliases')
            self.logical_paths[key] = logical
        permitted = {'catalog.json', 'receipt.json'}
        for logical, value in self.paths.items():
            name = value['retained']
            require(name == 'bytes/' + value['sha256'] and len(value['sha256']) == 64
                    and all(c in '0123456789abcdef' for c in value['sha256']), 'unsafe platform catalog member')
            permitted.add(name)
            self.path(logical)
        observed = {p.relative_to(self.directory).as_posix() for p in self.directory.rglob('*') if p.is_file()}
        require(observed == permitted, 'missing/extraneous platform catalog bytes')

    def verify_metadata(self):
        require(record(self.directory / 'catalog.json') == self.catalog_record, 'platform catalog changed')
        require(record(self.directory / 'receipt.json') == self.receipt_record, 'original platform receipt bytes changed')

    def path(self, logical):
        original = self.logical_paths.get(logical_key(logical))
        value = self.paths.get(original)
        require(value is not None, 'missing catalog entry: ' + str(logical))
        path = self.directory / value['retained']
        require(path.is_file() and not path.is_symlink() and record(path) == {k: value[k] for k in ('bytes', 'sha256')},
                'changed platform catalog bytes: ' + str(logical))
        return path

    def read_json(self, logical):
        path = self.path(logical)
        require(path.stat().st_size <= 64 * 1024 * 1024, 'oversized platform receipt')
        return read_document(path)

    def verify_files(self, records):
        require(isinstance(records, list) and records, 'empty platform artifact evidence')
        for value in records:
            require(record(self.path(value['path'])) == {k: value[k] for k in ('bytes', 'sha256')}, 'platform artifact identity differs')


def unpack(archive, output):
    require(not output.exists(), 'platform namespace collision')
    with tarfile.open(archive) as stream:
        members = stream.getmembers()
        require(0 < len(members) < MAX_FILES + 4 and sum(m.size for m in members) <= MAX_BYTES, 'platform bundle exceeds bounds')
        names = set(); roots = set()
        for member in members:
            path = PurePosixPath(member.name)
            require(not path.is_absolute() and '..' not in path.parts and '\\' not in member.name
                    and (member.isfile() or member.isdir()) and member.name not in names, 'unsafe/duplicate platform member')
            names.add(member.name); roots.add(path.parts[0])
        require(len(roots) == 1 and next(iter(roots)) in PLATFORMS, 'unknown platform namespace')
        output.mkdir(parents=True)
        stream.extractall(output, filter='data')
    return Catalog(output / roots.pop())


def package_origin(root, origin, read_json, read_text, expected, platform, read_bytes=None):
    import package_matrix as matrix
    from release_checks import validate_package
    directory = Path(origin['package'])
    summary = read_json(directory / 'summary.json')
    require(summary['revision'] == origin['source_commit'] == summary['release_artifact']['source']['head'], 'platform source commit differs')
    require(summary['host'] == platform and summary['release_artifact']['archive']['sha256'] == expected['archive_sha256']
            and summary['release_artifact']['source']['snapshot_sha256'] == expected['snapshot_sha256'], 'stale platform package source/archive')
    require(hashlib.sha256((read_bytes or (lambda p: Path(p).read_bytes()))(origin['provider_header'])).hexdigest() == summary['provider_header_sha256'],
            'platform provider artifact substituted')
    result = validate_package(root, directory, read_json, source_root=Path(origin['source_root']), provenance_path=provenance_path)
    inventory = matrix.load()
    for mode in ('no', 'yes'):
        for linkage in ('shared', 'static'):
            suites = read_json(directory / ('tls-' + mode) / ('integration-' + linkage) / 'suites.json')
            for name, definition in matrix.suites(inventory, platform, mode).items():
                receipt = suites[name]
                observed = matrix.suite_result(read_text(receipt['log']), 0)
                require(all(receipt.get(k) == v for k, v in observed.items()), 'stale platform suite summary')
                require(set(definition.get('required_cases', [])) <= set(observed['case_names']), 'pruned platform suite')
    return result


def validate_origin(root, row, catalog):
    from release_checks import automake_counts, counts
    catalog.verify_metadata()
    expected = row['identity']; origin = row['origin']
    prefix = logical_key(origin['package']).rstrip('/') + '/'
    for logical in catalog.paths:
        if logical_key(logical).startswith(prefix) and logical.endswith('.json'):
            referenced_files(catalog.read_json(logical), catalog.path)
    result = package_origin(root, origin, catalog.read_json, lambda p: catalog.path(p).read_text(), expected, row['id'], lambda p: catalog.path(p).read_bytes())
    require(set(origin['native']) == {'no', 'yes'}, 'missing native TLS configuration')
    for mode, native in origin['native'].items():
        names = native['tests']
        require(len(names) == len(set(names)) and names, 'duplicate/empty native suite inventory')
        mandatory = {'protocol_fuzz_replay', 'hpack_corpus', 'http2_fuzz_replay', 'quic_fuzz_replay',
                     'quic_recovery_fuzz_replay', 'http3_fuzz_replay', 'qpack_fuzz_replay',
                     'io_' + {'Windows':'iocp', 'Darwin':'kqueue', 'FreeBSD':'kqueue'}[row['id']] + '_backend'}
        if mode == 'yes': mandatory.add('quic_tls_callbacks_fuzz_replay')
        require(mandatory <= set(names), 'pruned mandatory native suite')
        logs = []
        for name in names:
            value = catalog.path(Path(native['build']) / 'test' / (name + '.trs')).read_text()
            if ':test-result: SKIP' in value:
                require(name in {'io_iocp_backend','io_epoll_backend','io_kqueue_backend'} and name not in mandatory, 'unexpected native skip')
            else:
                require(':test-result: PASS' in value, 'failed native test result')
                logs.append(name)
        text = catalog.path(Path(native['build']) / 'test/test-suite.log').read_text()
        import re
        skipped = len(names) - len(logs)
        require(re.findall(r'^# SKIP:[ \t]+(\d+)$', text, re.M) == [str(skipped)], 'native skip summary differs')
        require(re.findall(r'^# TOTAL:[ \t]+(\d+)$', text, re.M) == [str(len(names))], 'native registered suite summary differs')
        text = re.sub(r'^# TOTAL:[ \t]+\d+$', '# TOTAL: ' + str(len(logs)), text, flags=re.M)
        text = re.sub(r'^# SKIP:[ \t]+\d+$', '# SKIP: 0', text, flags=re.M)
        native_counts = automake_counts(text)
        result = counts(result['tests'] + native_counts['tests'], result['checks'] + native_counts['checks'])
    require(row['receipt']['counts'] == result, 'platform envelope counts differ from origin')
    catalog.verify_files(row['artifacts']); catalog.verify_files(row['receipt']['artifacts'])
    return result


def import_bundle(root, manifest, catalog, expected):
    row = read_document(catalog.directory / 'receipt.json')
    require(row.get('id') == catalog.platform and row.get('identity') == expected and row.get('status') == 'passed'
            and row.get('archive_sha256') == expected['archive_sha256'],
            'wrong/failed platform bundle identity')
    current = [r for r in manifest['platforms'] if r['id'] == row['id']]
    require(len(current) == 1 and current[0]['status'] == 'pending', 'duplicate/unknown platform producer')
    require(row['origin']['source_commit'] == manifest['release_artifact']['source']['head'], 'platform from another source commit')
    validate_origin(root, row, catalog)
    row['portable_bundle'] = str(catalog.directory)
    row['portable_catalog'] = dict(path=str(catalog.directory / 'catalog.json'), **catalog.catalog_record)
    manifest['platforms'] = [row if r['id'] == row['id'] else r for r in manifest['platforms']]


def referenced_files(document, resolve=Path):
    """Fixed package provenance records, including external clients/provider inputs."""
    result = set()
    def visit(value):
        if isinstance(value, list):
            for child in value: visit(child)
        elif isinstance(value, dict):
            if 'path' in value and 'sha256' in value:
                path = Path(value['path']); retained = resolve(str(path))
                require(retained.is_file() and identity.sha256(retained) == value['sha256'], 'referenced package artifact changed')
                result.add(path)
            for key in ('dependency_sha256', 'sources'):
                mapping = value.get(key)
                if isinstance(mapping, dict):
                    for name, digest in mapping.items():
                        if isinstance(digest, str) and len(digest) == 64:
                            path = Path(name); retained = resolve(str(path))
                            require(retained.is_file() and identity.sha256(retained) == digest, 'referenced package dependency changed')
                            result.add(path)
            # Dependency graph node IDs name the actual provider/runtime libraries.
            for node in value.get('nodes', []):
                if isinstance(node, dict) and isinstance(node.get('id'), str):
                    path = Path(node['id'])
                    if node.get('provenance') == 'OS/API-set platform leaf':
                        from release_checks import load_runner
                        audit = load_runner('audit-v3-installed-package.py')
                        windows = PureWindowsPath(str(path))
                        approved = str(path) in audit.DYLD_LEAVES or (
                            windows.parent.name.lower() == 'system32' and windows.parent.parent.name.lower() == 'windows'
                            and audit.windows_system(windows.name))
                        require(approved and node.get('sha256') is None, 'undeclared virtual platform leaf')
                        continue
                    if path.is_absolute() or PureWindowsPath(str(path)).is_absolute():
                        retained = resolve(str(path))
                        require(retained.is_file() and identity.sha256(retained) == node.get('sha256'), 'referenced runtime library missing/changed')
                        result.add(path)
            for child in value.values(): visit(child)
    visit(document)
    return result
