#!/usr/bin/env python3
"""Workflow dependencies and trust boundaries; execution contracts live beside runners."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[1]


def workflow(name):
    return yaml.safe_load((ROOT / '.github/workflows' / name).read_text())


class CIWorkflows(unittest.TestCase):
    def test_artifact_api_head_is_bound_to_trusted_event_head(self):
        lane = workflow('v3-release-acceptance.yml')
        self.assertEqual(lane.get('env', {}).get('WORKFLOW_HEAD_SHA'),
                         '${{ github.event.pull_request.head.sha || github.sha }}')

    def test_freebsd_stages_selected_archive_for_vm_transfer(self):
        steps = workflow('v3-release-acceptance.yml')['jobs']['freebsd']['steps']
        run = next(step['run'] for step in steps if 'ci-selected-input' in step.get('run', ''))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            selected = root / 'runner' / 'selected'
            selected.mkdir(parents=True)
            (selected / 'selected.json').write_text('selected archive identity')
            result = subprocess.run(['bash', '-e', '-c', run], cwd=root,
                env=dict(os.environ, RUNNER_TEMP=str(root / 'runner')), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / 'build/ci-selected-input/selected.json').read_text(), 'selected archive identity')

    def test_release_request_resolver_emits_structured_commit_and_version(self):
        steps = workflow('v3-release-acceptance.yml')['jobs']['prepare']['steps']
        run = next(step['run'] for step in steps if step.get('id') == 'request')
        source = run.split("<<'PYTHON'\n", 1)[1].rsplit('\nPYTHON', 1)[0]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'outputs'
            commit = 'a' * 40
            env = dict(os.environ, REQUEST_COMMIT=commit, REQUEST_VERSION='3.0.0', GITHUB_OUTPUT=str(output))
            result = subprocess.run([sys.executable, '-c', source], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_text(), 'commit=' + commit + '\nversion=3.0.0\n')

    def test_native_lane_checks_both_configurations_and_replays_independent_of_cache(self):
        path = ROOT / '.github/workflows/v3-native.yml'
        self.assertTrue(path.exists(), 'native CI lane missing')
        lane = workflow(path.name)
        self.assertEqual(lane['permissions'], {'contents': 'read'})
        native = lane['jobs']['native']
        self.assertEqual(native['strategy']['matrix']['tls'], ['no', 'yes'])
        runs = '\n'.join(step.get('run', '') for step in native['steps'])
        self.assertIn('make check', runs)
        self.assertIn('RUNNER_TEMP', runs)
        fuzz = lane['jobs']['fuzz']
        targets = fuzz['strategy']['matrix']['target']
        self.assertEqual(set(targets), {'http1_head', 'http1_body', 'websocket_codec', 'hpack',
            'http2_engine', 'quic_parser', 'quic_state', 'quic_recovery', 'http3_engine', 'qpack', 'quic_tls_callbacks'})
        save = next(step for step in fuzz['steps'] if '/save@' in step.get('uses', ''))
        self.assertIn("github.event_name != 'pull_request'", save['if'])
        self.assertIn('github.event.repository.default_branch', save['if'])
        self.assertIn('run_attempt', save['with']['key'])
        self.assertTrue(any(step.get('if') == 'always()' and '/upload-artifact@' in step.get('uses', '') for step in fuzz['steps']))

    def test_package_paths_do_not_contaminate_checkout_and_native_git_is_installed(self):
        lane = workflow('v3-package-validation.yml')
        for name in ('linux', 'macos', 'mingw64', 'freebsd'):
            text = str(lane['jobs'][name]['steps'])
            self.assertNotIn('$PWD/package-evidence', text)
            self.assertIn('client-python', text)
            self.assertIn('node', text)
        self.assertIn('git', str(lane['jobs']['mingw64']['steps']))
        self.assertIn('git', str(lane['jobs']['freebsd']['steps']))

    def test_freebsd_client_is_exact_pinned_node_source_when_ports_differ(self):
        for filename in ('v3-package-validation.yml','v3-release-acceptance.yml'):
            text=(ROOT/'.github/workflows'/filename).read_text()
            self.assertIn('729de494dd2872e5a3a6c32a1cd156a5413d4aca2772b2d873ee86bb5531bcd9',text)
            self.assertIn('node-v24.15.0.tar.gz',text)
            self.assertIn('node --version',text)

    def test_publisher_receives_only_accepted_manifest_and_authorization(self):
        lane=workflow('v3-release-acceptance.yml')
        step=next(step for step in lane['jobs']['full']['steps'] if step.get('id')=='authorization')
        path=step['with']['path']
        self.assertIn('authorization.json',path)
        self.assertIn('accepted-manifest.json',path)
        self.assertNotIn('imported-platforms',path)
        self.assertEqual(lane['jobs']['publish']['permissions']['contents'],'write')
        for name,job in lane['jobs'].items():
            if name!='publish': self.assertNotEqual(job.get('permissions',{}).get('contents'),'write')

    def test_macos_provisions_actual_acme_tcp443_binding_capability(self):
        for filename,job in (('v3-package-validation.yml','macos'),('v3-release-acceptance.yml','darwin')):
            steps=workflow(filename)['jobs'][job]['steps']
            text='\n'.join(step.get('run','') for step in steps)
            self.assertIn('GIT_CONFIG_KEY_0=safe.directory',text)
            self.assertIn('bind(("127.0.0.1",443))',text)

    def test_changelog_uses_actual_base_as_data(self):
        lane = workflow('changelog-check.yml')
        step = next(step for step in lane['jobs']['changelog']['steps'] if 'git diff' in step.get('run', ''))
        self.assertIn('BASE_REF', step['env'])
        self.assertNotIn('origin/master', step['run'])
        self.assertIn('git check-ref-format', step['run'])
        self.assertIn('--name-only --', step['run'])

    def test_all_modified_workflows_pin_actions(self):
        for name in ('v3-native.yml', 'v3-package-validation.yml', 'verify-build.yml', 'codeql-analysis.yml', 'changelog-check.yml', 'release.yml'):
            path = ROOT / '.github/workflows' / name
            if not path.exists(): continue
            for use in re.findall(r'uses:\s*(\S+)', path.read_text()):
                if not use.startswith('./.github/workflows/'):
                    self.assertRegex(use, r'@[0-9a-f]{40}$')


if __name__ == '__main__':
    unittest.main()
