#!/usr/bin/env python3
import importlib.util
import io
import json
import re
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/lib'))


class PlatformBundles(unittest.TestCase):
    def module(self):
        path = ROOT / 'scripts/lib/release_platform.py'
        self.assertTrue(path.exists(), 'portable platform receipt implementation missing')
        spec = importlib.util.spec_from_file_location('release_platform', path)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        return module

    def test_catalog_relocates_original_bytes_and_rejects_tampering_missing_and_symlinks(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); original = root / 'original.log'
            original.write_text('retained immutable native results')
            bundle = root / 'bundle'
            module.retain(bundle, 'Darwin', {'id': 'Darwin'}, [original])
            moved = root / 'moved'; bundle.rename(moved); original.unlink()
            resolver = module.Catalog(moved)
            self.assertEqual(resolver.path(str(original)).read_text(), 'retained immutable native results')
            with self.assertRaises(ValueError): resolver.path('/missing/record')
            retained = resolver.path(str(original)); retained.write_text('tampered')
            with self.assertRaises(ValueError): module.Catalog(moved)
            retained.unlink(); retained.symlink_to(ROOT / 'configure.ac')
            with self.assertRaises(ValueError): module.Catalog(moved)

    def test_original_and_resolved_tool_paths_both_retain_exact_bytes(self):
        module=self.module()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); binary=root/'binary'; binary.write_text('native selected compiler')
            alias=root/'compiler'; alias.symlink_to(binary)
            catalog=module.retain(root/'bundle','Windows',{'id':'Windows'},[alias])
            self.assertEqual(catalog.path(str(alias)).read_bytes(),catalog.path(str(binary)).read_bytes())

    def test_equivalent_relative_dependency_paths_preserve_both_original_labels(self):
        module=self.module()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); (root/'other').mkdir(); file=root/'file'; file.write_text('dependency bytes')
            alias=root/'other/../file'
            catalog=module.retain(root/'bundle','FreeBSD',{'id':'FreeBSD'},[alias])
            self.assertEqual(catalog.path(str(alias)).read_bytes(),catalog.path(str(file)).read_bytes())

    def test_windows_logical_paths_resolve_without_rewriting_original_receipts(self):
        module=self.module()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); original=root/'log'; original.write_text('native Windows bytes')
            retained=root/'bundle'; module.retain(retained,'Windows',{'id':'Windows'},[original])
            catalog=json.loads((retained/'catalog.json').read_text())
            record=catalog['paths'].pop(str(original)); catalog['paths'][r'C:\native\matrix\log']=record
            (retained/'catalog.json').write_text(json.dumps(catalog))
            resolver=module.Catalog(retained)
            self.assertEqual(resolver.path('C:\\native/matrix/../matrix/log').read_text(),'native Windows bytes')

    def test_provider_client_and_dependency_records_require_retained_original_bytes(self):
        module=self.module()
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); client=root/'client.py'; client.write_text('selected client module')
            document={'nested':{'path':str(client),'sha256':hashlib.sha256(client.read_bytes()).hexdigest()},
                      'compile':{'dependency_sha256':{str(client):hashlib.sha256(client.read_bytes()).hexdigest()}}}
            self.assertEqual(module.referenced_files(document),{client})
            client.write_text('substituted client module')
            with self.assertRaises(ValueError): module.referenced_files(document)
            client.unlink()
            with self.assertRaises(ValueError): module.referenced_files(document)

    def test_duplicate_catalog_keys_are_rejected(self):
        module=self.module()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); file=root/'file'; file.write_text('bytes')
            module.retain(root/'bundle','FreeBSD',{'id':'FreeBSD'},[file])
            catalog=root/'bundle/catalog.json'
            catalog.write_text(catalog.read_text().replace('"schema_version": 1','"schema_version": 1, "schema_version": 1'))
            with self.assertRaises(ValueError): module.Catalog(root/'bundle')

    def test_receipt_bytes_are_immutable_even_for_json_equivalent_edits(self):
        module=self.module()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); file=root/'file'; file.write_text('retained bytes')
            module.retain(root/'bundle','Darwin',{'id':'Darwin'},[file])
            receipt=root/'bundle/receipt.json'
            receipt.write_bytes(receipt.read_bytes()+b'\n')
            with self.assertRaises(ValueError): module.Catalog(root/'bundle')

    def test_existing_sealed_os_leaves_do_not_require_fabricated_binary_files(self):
        module=self.module()
        for name in ('/usr/lib/libSystem.B.dylib',r'C:\Windows\System32\api-ms-win-core-synch-l1-2-0.dll'):
            graph={'nodes':[dict(id=name,sha256=None,provenance='OS/API-set platform leaf')]}
            self.assertEqual(module.referenced_files(graph),set())
        with self.assertRaises(ValueError):
            module.referenced_files({'nodes':[dict(id='/private/provider/libssl.dylib',sha256=None,provenance='OS/API-set platform leaf')]})

    def test_bundle_extraction_rejects_escape_duplicates_links_and_extra_files(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, kind in (('../outside', None), ('/outside', None), ('Darwin/link', tarfile.SYMTYPE),
                               ('Darwin/link', tarfile.LNKTYPE), ('Darwin/extra', None)):
                with self.subTest(name=name, kind=kind):
                    archive = root / 'bundle.tar.gz'
                    with tarfile.open(archive, 'w:gz') as stream:
                        item = tarfile.TarInfo(name); item.size=1
                        if kind: item.type=kind; item.linkname='/outside'; item.size=0
                        stream.addfile(item, io.BytesIO(b'x') if item.size else None)
                    with self.assertRaises(ValueError): module.unpack(archive, root / ('extract-' + str(kind) + str(len(name))))

    def test_valid_archive_import_rejects_duplicate_members_and_namespace_collisions(self):
        module=self.module()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); file=root/'file'; file.write_text('retained bytes')
            module.retain(root/'Darwin','Darwin',{'id':'Darwin'},[file])
            archive=root/'valid.tar.gz'
            with tarfile.open(archive,'w:gz') as stream: stream.add(root/'Darwin',arcname='Darwin')
            module.unpack(archive,root/'imported')
            with self.assertRaises(ValueError): module.unpack(archive,root/'imported')
            duplicate=root/'duplicate.tar.gz'
            with tarfile.open(duplicate,'w:gz') as stream:
                stream.add(root/'Darwin',arcname='Darwin')
                stream.add(root/'Darwin/catalog.json',arcname='Darwin/catalog.json')
            with self.assertRaises(ValueError): module.unpack(duplicate,root/'duplicate')

    def platform_origin_fixture(self):
        module = self.module()
        import package_matrix as matrix
        import release_artifact as identity
        import release_checks as checks
        import hashlib
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name); package = root / 'matrix'; package.mkdir()
        inventory = matrix.load()
        def write(path, value):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(value))
        header = root / 'provider.h'; header.write_bytes(b'selected OpenSSL\r\n')
        selected = dict(archive_sha256='a'*64, snapshot_sha256='b'*64, extracted_sha256=hashlib.sha256(json.dumps({},sort_keys=True).encode()).hexdigest(), tls='both', sanitizer='none')
        write(package/'summary.json', dict(status='passed', host='Darwin', lanes={'no':{},'yes':{}},
            revision='d'*40, provider_header_sha256=identity.sha256(header),
            release_artifact={'archive':{'sha256':'a'*64}, 'source':{'snapshot_sha256':'b'*64,'head':'d'*40}}))
        for mode in ('no','yes'):
            lane=package/('tls-'+mode); prefix=lane/'prefix'
            (prefix/'lib').mkdir(parents=True)
            (prefix/'lib/libhttpserver.dylib').write_text('selected native library')
            public={}
            for linkage in ('shared','static'):
                graph={'nodes':[{'id':str(prefix/'lib/libhttpserver.dylib'),'sha256':identity.sha256(prefix/'lib/libhttpserver.dylib')}] if linkage=='shared' else []}
                public[linkage]=dict(dependencies=graph, compile={'dependencies':[]},
                    cells=[{'configurations':inventory['cases']},{'configurations':inventory['cases']}])
                suites={}
                for name, definition in matrix.suites(inventory, 'Darwin', mode).items():
                    cases=definition.get('required_cases',[]) or ['fixture_case']
                    log=lane/('integration-'+linkage)/(name+'.log')
                    log.parent.mkdir(parents=True, exist_ok=True)
                    log.write_text(''.join('Running test (%d): %s\n'%(i,case) for i,case in enumerate(cases))+
                        '%d tests executed\n%d checks\n-> 0 failures\n-> 0 skipped\n'%(len(cases),len(cases)))
                    suites[name]=dict(matrix.suite_result(log.read_text(),0),status='passed', log=str(log))
                write(lane/('integration-'+linkage)/'suites.json',suites)
            write(lane/'audit.json',dict(violations=[],tls_mode=mode,public=public))
        origin=dict(package=str(package), source_root=str(root/'source'), source_commit='d'*40, provider_header=str(header), native={})
        # This test checks installed matrix origin independently; native origin is mandatory in complete bundles.
        counts=module.package_origin(ROOT, origin, lambda p: json.loads(Path(p).read_text()), lambda p: Path(p).read_text(), selected, 'Darwin')
        self.assertGreater(counts['tests'],0)
        wrong_origin=dict(origin,source_commit='e'*40)
        with self.assertRaises(ValueError):
            module.package_origin(ROOT,wrong_origin,lambda p:json.loads(Path(p).read_text()),lambda p:Path(p).read_text(),selected,'Darwin')
        for mode in ('no','yes'):
            build=root/('native-'+mode); (build/'test').mkdir(parents=True)
            names=['protocol_fuzz_replay','hpack_corpus','http2_fuzz_replay','quic_fuzz_replay',
                   'quic_recovery_fuzz_replay','http3_fuzz_replay','qpack_fuzz_replay','io_kqueue_backend']
            if mode=='yes': names.append('quic_tls_callbacks_fuzz_replay')
            for name in names: (build/'test'/(name+'.trs')).write_text(':test-result: PASS\n')
            (build/'test/test-suite.log').write_text('# TOTAL: %d\n# PASS: %d\n# SKIP: 0\n# XFAIL: 0\n# FAIL: 0\n# XPASS: 0\n# ERROR: 0\n'%(len(names),len(names)))
            origin['native'][mode]=dict(build=str(build), tests=names)
            counts=checks.counts(counts['tests']+len(names),counts['checks']+len(names))
        artifacts=[dict(path=str(header),sha256=identity.sha256(header),bytes=header.stat().st_size)]
        row=dict(id='Darwin',identity=selected,archive_sha256='a'*64,origin=origin,status='passed',artifacts=artifacts,
                 receipt=dict(status='passed',gate='Darwin',identity=selected,counts=counts,artifacts=artifacts))
        retained=root/'retained'
        catalog=module.retain(retained,'Darwin',row,[p for p in root.rglob('*') if p.is_file()])
        module.validate_origin(ROOT,row,catalog)
        return module, root, package, header, selected, origin, row, catalog

    def test_platform_origin_rejects_pruned_mandatory_native_suite(self):
        import release_checks as checks
        module, root, _, _, _, _, row, catalog = self.platform_origin_fixture()
        module.validate_origin(ROOT, row, catalog)
        incomplete = json.loads(json.dumps(row))
        native = incomplete['origin']['native']['yes']
        native['tests'].remove('quic_tls_callbacks_fuzz_replay')
        test_root = Path(native['build']) / 'test'
        (test_root / 'quic_tls_callbacks_fuzz_replay.trs').unlink()
        (test_root / 'test-suite.log').write_text(
            '# TOTAL: %d\n# PASS: %d\n# SKIP: 0\n# XFAIL: 0\n# FAIL: 0\n# XPASS: 0\n# ERROR: 0\n' %
            (len(native['tests']), len(native['tests'])))
        old_counts = incomplete['receipt']['counts']
        incomplete['receipt']['counts'] = checks.counts(old_counts['tests'] - 1, old_counts['checks'] - 1)
        # Reseal coherent counts and logs so mandatory-suite coverage is the sole rejection.
        paths = [p for p in root.rglob('*') if p.is_file() and catalog.directory not in p.parents]
        pruned = module.retain(root / 'pruned', 'Darwin', incomplete, paths)
        with self.assertRaisesRegex(ValueError, '^pruned mandatory native suite$'):
            module.validate_origin(ROOT, incomplete, pruned)

    def test_platform_origin_rejects_retained_provider_substitution(self):
        module, _, _, header, _, _, row, catalog = self.platform_origin_fixture()
        module.validate_origin(ROOT, row, catalog)
        catalog.path(str(header)).write_text('corrupt retained provider')
        with self.assertRaisesRegex(ValueError, '^changed platform catalog bytes: ' + re.escape(str(header)) + '$'):
            module.validate_origin(ROOT, row, catalog)

    def test_platform_origin_rejects_stale_summary(self):
        import release_checks as checks
        module, root, package, header, selected, origin, row, catalog = self.platform_origin_fixture()
        def write(path, value):
            path.write_text(json.dumps(value))
        manifest=dict(release_artifact={'archive':{'sha256':'a'*64},'source':{'snapshot_sha256':'b'*64,'head':'d'*40},'files_sha256':{}},
                      platforms=[dict(id=p,status='pending') for p in module.PLATFORMS])
        module.import_bundle(ROOT, manifest, catalog, expected=selected)
        self.assertEqual(manifest['platforms'][1]['status'],'passed')
        gate=checks.load_runner('run-v3-release-gates.py')
        gate.verify_gate_rows(manifest,[manifest['platforms'][1]])
        raw_receipt=(catalog.directory/'receipt.json').read_bytes()
        raw_catalog=(catalog.directory/'catalog.json').read_bytes()
        (catalog.directory/'catalog.json').write_bytes(raw_catalog+b'\n')
        with self.assertRaisesRegex(ValueError,'changed artifact'):
            gate.verify_gate_rows(manifest,[manifest['platforms'][1]])
        (catalog.directory/'catalog.json').write_bytes(raw_catalog)
        with self.assertRaises(ValueError): module.import_bundle(ROOT, manifest, catalog, expected=selected)
        wrong=json.loads(json.dumps(row)); wrong['archive_sha256']='e'*64
        (catalog.directory/'receipt.json').write_text(json.dumps(wrong))
        fresh=dict(release_artifact={'source':{'head':'d'*40}},platforms=[dict(id=p,status='pending') for p in module.PLATFORMS])
        with self.assertRaises(ValueError): module.import_bundle(ROOT, fresh, catalog, expected=selected)
        (catalog.directory/'receipt.json').write_bytes(raw_receipt)
        module.validate_origin(ROOT,row,catalog)
        def windows(value):
            if isinstance(value,dict): return {k:windows(v) for k,v in value.items()}
            if isinstance(value,list): return [windows(v) for v in value]
            if isinstance(value,str) and value.startswith(str(root)):
                return value.replace(str(root),'C:/native').replace('/','\\')
            return value
        def physical(value):
            return Path(str(value).replace('\\','/').replace('C:/native',str(root)))
        module.package_origin(ROOT,windows(origin),lambda p:windows(json.loads(physical(p).read_text())),
            lambda p:physical(p).read_text(),selected,'Darwin',lambda p:physical(p).read_bytes())
        suites_path=package/'tls-no/integration-shared/suites.json'
        suites=json.loads(suites_path.read_text()); first=next(iter(suites))
        altered=json.loads(suites_path.read_text()); altered[first]['tests']+=1; write(suites_path,altered)
        with self.assertRaises(ValueError): module.package_origin(ROOT,origin,lambda p:json.loads(Path(p).read_text()),lambda p:Path(p).read_text(),selected,'Darwin')
        write(suites_path,suites); removed=suites.pop(first); write(suites_path,suites)
        with self.assertRaises(ValueError): module.package_origin(ROOT,origin,lambda p:json.loads(Path(p).read_text()),lambda p:Path(p).read_text(),selected,'Darwin')
        suites[first]=removed; write(suites_path,suites); header.write_text('different provider')
        with self.assertRaises(ValueError): module.package_origin(ROOT,origin,lambda p:json.loads(Path(p).read_text()),lambda p:Path(p).read_text(),selected,'Darwin')


if __name__ == '__main__': unittest.main()
