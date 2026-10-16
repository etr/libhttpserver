#!/usr/bin/env python3
"""Selected archive and publication decisions exercise the real readiness gate."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/lib'))
import release_gate_runner_test as release_contracts


class PublicationContracts(unittest.TestCase):
    def setUp(self):
        import hashlib
        release_contracts.ReleaseContracts.setUp(self)
        self.artifact_identity['source']['working_diff_sha256']=hashlib.sha256(b'').hexdigest()
        self.identity_path.write_text(json.dumps(self.artifact_identity))
    @classmethod
    def setUpClass(cls):
        release_contracts.ReleaseContracts.setUpClass()
        cls.gate = release_contracts.ReleaseContracts.gate
        cls.ci_path = ROOT / 'scripts/lib/release_ci.py'

    def ci(self):
        self.assertTrue(self.ci_path.exists(), 'selected-archive CI acceptance helper missing')
        spec = importlib.util.spec_from_file_location('release_ci', self.ci_path)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        return module

    def publisher(self):
        import ast
        import textwrap
        text=(ROOT/'.github/workflows/v3-release-acceptance.yml').read_text()
        self.assertIn('  publish:\n',text,'isolated publisher job missing')
        job=text.split('  publish:\n',1)[1]
        self.assertNotIn('checkout@',job)
        script=textwrap.dedent(job.split("python3 - <<'PYTHON'\n",1)[1]).rsplit('\nPYTHON',1)[0]
        tree=ast.parse(script)
        tree.body=[node for node in tree.body if isinstance(node,(ast.Import,ast.ImportFrom,ast.FunctionDef))]
        namespace={}; exec(compile(tree,'publisher','exec'),namespace)
        return namespace['publish_checked']

    def publish(self, authorization, binding, archive, publication):
        self.publisher()(authorization,binding,archive,self.complete(),'d'*40,publication)

    def complete(self):
        manifest = {'schema_version': 1, 'inventory': self.gate.inventory_document(),
            'release_artifact': self.artifact_identity, 'artifact_identity_path': str(self.identity_path),
            'artifacts': self.receipt['artifacts'], 'stages': [], 'platforms': []}
        for name in self.gate.LOCAL_GATES + self.gate.PLATFORMS:
            identity = self.gate.expected_identity(manifest, name)
            row = {'id': name, 'status': 'passed', 'artifacts': self.receipt['artifacts'],
                   'identity': identity, 'receipt': dict(self.receipt, identity=identity, gate=name)}
            manifest['stages' if name in self.gate.LOCAL_GATES else 'platforms'].append(row)
        return manifest

    def binding(self):
        return dict(repository='etr/libhttpserver', run_id='12', run_attempt='1',
                    source_commit='d' * 40, tag='3.0.0', archive_sha256=self.gate.digest(self.binary),
                    event='push', artifact_id='23', workflow_source='d'*40, producer_ids=['FreeBSD', 'Darwin', 'Windows'],
                    producer_artifacts={name:dict(id='24',digest='a'*64) for name in ('freebsd','darwin','windows')})

    def test_valid_publication_requires_actual_readiness_then_calls_side_effect_once(self):
        ci = self.ci(); manifest = self.complete(); binding = self.binding()
        publish = Mock()
        authorization = ci.authorize(self.gate, manifest, binding)
        self.publish(authorization, binding, self.binary, publish)
        publish.assert_called_once()

    def test_every_missing_or_unsuccessful_gate_and_platform_blocks_publication(self):
        ci = self.ci()
        for axis in ('stages', 'platforms'):
            for index in range(len(self.complete()[axis])):
                for damage in ('missing', 'failed', 'duplicate'):
                    manifest = self.complete(); publish = Mock()
                    if damage == 'missing': manifest[axis].pop(index)
                    if damage == 'failed': manifest[axis][index]['status'] = 'failed'
                    if damage == 'duplicate': manifest[axis].append(copy.deepcopy(manifest[axis][index]))
                    with self.subTest(axis=axis, index=index, damage=damage), self.assertRaises(ValueError):
                        self.publish(ci.authorize(self.gate, manifest, self.binding()), self.binding(), self.binary, publish)
                    publish.assert_not_called()

    def test_wrong_source_archive_run_tag_and_pr_cannot_publish(self):
        ci = self.ci(); manifest = self.complete(); binding = self.binding()
        authorization = ci.authorize(self.gate, manifest, binding)
        for key in ('repository', 'run_id', 'run_attempt', 'source_commit', 'tag', 'artifact_id'):
            altered = dict(binding, **{key: 'wrong'}); publish = Mock()
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.publish(authorization, altered, self.binary, publish)
            publish.assert_not_called()
        for alteration in ({'event': 'pull_request'}, {'source_commit': 'a' * 40},
                           {'archive_sha256': 'a' * 64}, {'tag': '3.0.0; touch marker'},
                           {'producer_ids': ['Darwin']}):
            with self.subTest(alteration=alteration), self.assertRaises(ValueError):
                ci.authorize(self.gate, manifest, dict(binding, **alteration))
        self.binary.write_text('changed after acceptance')
        publish = Mock()
        with self.assertRaises(ValueError): self.publish(authorization, binding, self.binary, publish)
        publish.assert_not_called()

    def test_ci_entrypoint_rejects_unresolved_request_without_side_effects(self):
        import subprocess
        script = ROOT / 'scripts/run-v3-release-ci.py'
        self.assertTrue(script.exists(), 'selected archive CI entrypoint missing')
        output=self.directory/'ci'
        result=subprocess.run([sys.executable,str(script),'prepare','--output',str(output),
            '--commit','$(touch marker)','--version','3.0.0'],cwd=self.directory,capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.directory/'marker').exists())
        self.assertFalse(output.exists())

    def test_authorization_requires_content_bound_platform_producer_artifacts(self):
        ci=self.ci()
        for damage in ('missing','wrong-id','wrong-digest'):
            binding=self.binding()
            if damage=='missing': binding['producer_artifacts'].pop('darwin')
            if damage=='wrong-id': binding['producer_artifacts']['darwin']['id']='$(touch marker)'
            if damage=='wrong-digest': binding['producer_artifacts']['darwin']['digest']='old'
            with self.subTest(damage=damage),self.assertRaises(ValueError): ci.authorize(self.gate,self.complete(),binding)

    def test_producer_binding_cannot_change_after_authorization(self):
        ci=self.ci(); binding=self.binding(); authorization=ci.authorize(self.gate,self.complete(),binding)
        binding['producer_artifacts']['darwin']['id']='25'
        with self.assertRaises(ValueError): self.publish(authorization,binding,self.binary,Mock())

    def test_actual_inline_publisher_calls_once_only_after_real_readiness(self):
        publisher=self.publisher()
        ci=self.ci(); binding=self.binding(); manifest=self.complete()
        authorization=ci.authorize(self.gate,manifest,binding)
        release=Mock()
        publisher(authorization,binding,self.binary,manifest,'d'*40,release)
        release.assert_called_once()
        for alteration in ('head','manifest','archive','binding'):
            release=Mock(); changed=copy.deepcopy(manifest); expected=copy.deepcopy(binding); tag_commit='d'*40
            if alteration=='head': tag_commit='e'*40
            if alteration=='manifest': changed['platforms'].pop()
            if alteration=='archive': self.binary.write_text('different archive')
            if alteration=='binding': expected['run_attempt']='2'
            with self.subTest(alteration=alteration),self.assertRaises((ValueError,AssertionError)):
                publisher(authorization,expected,self.binary,changed,tag_commit,release)
            release.assert_not_called()

    def test_immutable_download_checks_source_run_attempt_digest_and_zip_members(self):
        import hashlib
        import zipfile
        ci=self.ci()
        archive=self.directory/'artifact.zip'
        with zipfile.ZipFile(archive,'w') as stream: stream.writestr('selected.json','selected bytes')
        expected=dict(repository='etr/libhttpserver',run_id='12',run_attempt='1',source_commit='d'*40,artifact_id='23')
        metadata=dict(id=23,expired=False,created_at='2026-10-15T03:01:00Z',workflow_run={'id':12,'head_sha':'d'*40},digest='sha256:'+hashlib.sha256(archive.read_bytes()).hexdigest())
        run=dict(id=12,run_attempt=1,run_started_at='2026-10-15T03:00:00Z',repository={'full_name':'etr/libhttpserver'},head_sha='d'*40)
        ci.verify_download(archive,metadata,run,expected,metadata['digest'],self.directory/'download')
        for field,value in [('id',24),('expired',True),('digest','sha256:'+'a'*64),('created_at','2026-10-14T03:01:00Z')]:
            altered=dict(metadata,**{field:value})
            with self.subTest(field=field),self.assertRaises(ValueError):
                ci.verify_download(archive,altered,run,expected,metadata['digest'],self.directory/field)
        for changed in (dict(run,run_attempt=2),dict(run,head_sha='a'*40),dict(run,repository={'full_name':'other/repo'})):
            with self.assertRaises(ValueError): ci.verify_download(archive,metadata,changed,expected,metadata['digest'],self.directory/'wrongrun')
        for index,name in enumerate(('../outside','/outside','C:/outside','a\\..\\outside')):
            bad=self.directory/('escape-%d.zip'%index)
            with zipfile.ZipFile(bad,'w') as stream: stream.writestr(name,'escape')
            digest='sha256:'+hashlib.sha256(bad.read_bytes()).hexdigest()
            with self.subTest(name=name),self.assertRaises(ValueError):
                ci.verify_download(bad,dict(metadata,digest=digest),run,expected,digest,self.directory/('badzip-%d'%index))

    def test_exact_commit_request_rejects_modified_and_untracked_source_inputs(self):
        import subprocess
        ci=self.ci(); root=self.directory/'checkout'; (root/'scripts').mkdir(parents=True)
        for name in ('configure.ac','ChangeLog','scripts/validate-version.sh'):
            (root/name).write_bytes((ROOT/name).read_bytes())
        def git(*args):
            return subprocess.check_output(['git','-C',str(root),'-c','core.hooksPath=/dev/null',*args],stderr=subprocess.PIPE,text=True)
        git('init','-q'); git('add','.')
        git('-c','user.name=CI fixture','-c','user.email=ci@example.invalid','commit','-qm','fixture')
        commit=git('rev-parse','HEAD').strip()
        ci.validate_request(root,commit,'3.0.0')
        original=(root/'configure.ac').read_bytes(); (root/'configure.ac').write_bytes(original+b'\n')
        with self.assertRaises(ValueError): ci.validate_request(root,commit,'3.0.0')
        (root/'configure.ac').write_bytes(original); (root/'injected.cpp').write_text('untracked source')
        with self.assertRaises(ValueError): ci.validate_request(root,commit,'3.0.0')

    def test_publication_rejects_self_consistent_dirty_source_snapshot(self):
        ci=self.ci(); manifest=self.complete()
        manifest['release_artifact']['source']['working_diff_sha256']='a'*64
        self.identity_path.write_text(json.dumps(manifest['release_artifact']))
        self.gate.check_tag_ready(manifest)
        with self.assertRaises(ValueError): ci.authorize(self.gate,manifest,self.binding())

    def test_requested_commit_and_version_are_data_and_must_match_checkout(self):
        ci = self.ci()
        for commit, version in [('d'*40, '3.0.0; touch marker'), ('$(touch marker)', '3.0.0'), ('e'*40, '3.0.0')]:
            with self.subTest(commit=commit,version=version), self.assertRaises(ValueError):
                ci.validate_request(ROOT, commit, version)

    def test_request_requires_a_git_checkout_even_when_versions_are_valid(self):
        ci=self.ci()
        with self.assertRaises(ValueError): ci.validate_request(self.directory,'e'*40,'3.0.0')

    def test_selected_archive_relocation_preserves_source_and_rejects_tampering(self):
        import io
        import tarfile
        ci = self.ci()
        checkout = self.directory / 'checkout'; checkout.mkdir()
        (checkout / 'configure.ac').write_text('source')
        (checkout / 'specs').mkdir(); (checkout / 'specs/product.md').write_text('tooling')
        archive = self.directory / 'candidate.tar.gz'
        with tarfile.open(archive, 'w:gz') as stream:
            for name in ('configure.ac',):
                stream.add(checkout / name, arcname='libhttpserver-3.0.0/' + name)
        source = dict(head='d' * 40, files_sha256={name: self.gate.digest(checkout / name)
            for name in ('configure.ac', 'specs/product.md')}, snapshot_sha256='b' * 64)
        identity = ci.selected_archive(checkout, source, archive, self.gate.digest(archive), self.directory / 'relocated')
        self.assertEqual(identity['files_sha256'], source['files_sha256'])
        self.assertEqual((Path(identity['source_root']) / 'specs/product.md').read_text(), 'tooling')
        (checkout / 'configure.ac').write_text('another commit')
        with self.assertRaises(ValueError):
            ci.selected_archive(checkout, source, archive, self.gate.digest(archive), self.directory / 'wrong-source')
        with self.assertRaises(ValueError):
            ci.selected_archive(checkout, source, archive, 'a' * 64, self.directory / 'wrong-archive')
        for kind in ('symlink', 'hardlink', 'absolute', 'traversal', 'duplicate'):
            bad = self.directory / (kind + '.tar.gz')
            with tarfile.open(bad, 'w:gz') as stream:
                item = tarfile.TarInfo('root/configure.ac'); item.size = 1
                stream.addfile(item, io.BytesIO(b'x'))
                item = tarfile.TarInfo({'absolute':'/outside', 'traversal':'../outside'}.get(kind, 'root/configure.ac'))
                if kind == 'symlink': item.type=tarfile.SYMTYPE; item.linkname='/outside'
                if kind == 'hardlink': item.type=tarfile.LNKTYPE; item.linkname='root/configure.ac'
                stream.addfile(item)
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                ci.selected_archive(checkout, source, bad, self.gate.digest(bad), self.directory / kind)


if __name__ == '__main__':
    unittest.main()
