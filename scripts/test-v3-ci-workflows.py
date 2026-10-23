#!/usr/bin/env python3
"""Workflow dependencies and trust boundaries; execution contracts live beside runners."""
from pathlib import Path
import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[1]


def workflow(name):
    return yaml.safe_load((ROOT / '.github/workflows' / name).read_text())


def workflow_shell(lane, job, step):
    shell = step.get('shell', job.get('defaults', {}).get('run', {}).get('shell',
                lane.get('defaults', {}).get('run', {}).get('shell')))
    if shell is None:
        return ['bash', '-e', '-c']
    if shell == 'bash':
        return ['bash', '--noprofile', '--norc', '-e', '-o', 'pipefail', '-c']
    raise AssertionError('unsupported workflow fixture shell: ' + shell)


@unittest.skipIf(os.name == 'nt', 'FreeBSD VM commands use a POSIX shell')
class FreeBSDNodePrerequisites(unittest.TestCase):
    workflows = ('v3-package-validation.yml', 'v3-release-acceptance.yml')

    def checkout(self, root):
        root.mkdir(exist_ok=True)
        env = {key: value for key, value in os.environ.items() if not key.startswith('GIT_')}
        env.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull)
        for args in (['init', '-q'], ['-c', 'user.name=fixture', '-c', 'user.email=fixture@example.invalid',
                                     'commit', '-q', '--allow-empty', '-m', 'fixture']):
            subprocess.run(['git'] + args, cwd=root, env=env, check=True, capture_output=True)
        return env

    def guest_run(self, name, runner):
        steps = workflow(name)['jobs']['freebsd']['steps']
        vm = next(step for step in steps if 'freebsd-vm@' in step.get('uses', ''))
        run = '\n'.join('export RUNNER_TEMP=' + shlex.quote(str(runner))
                        if line.startswith('export RUNNER_TEMP=') else line
                        for line in vm['with']['run'].splitlines())
        return run, steps

    def recover(self, root, steps, runner, env):
        recovery = next(step for step in steps if step.get('name') == 'Recover FreeBSD evidence')
        self.assertEqual(recovery['if'], 'always()')
        tools = root / 'transport'
        tools.mkdir()
        # Replace only SSH transport: real OpenSSH scp runs both protocol peers,
        # including the receiving client's strict filename validation.
        ssh = tools / 'ssh'
        ssh.write_text('''#!/usr/bin/env python3
import os, pathlib, shlex, sys
argv = shlex.split(sys.argv[-1])
assert argv[:3] == ['scp', '-r', '-f'], argv
remote = pathlib.PurePosixPath(argv[-1])
assert str(remote).startswith(('/tmp/v3-package-123-1/', '/tmp/v3-release-123-1/')), remote
relative = str(remote).split('/', 3)[3]
source = os.environ['FIXTURE_RUNNER'] + '/' + relative
if sys.argv[-1].endswith('/.'):
    source += '/.'
os.execv(os.environ['REAL_SCP'], ['scp', '-r', '-f', source])
''')
        ssh.chmod(0o755)
        scp = tools / 'scp'
        scp.write_text('#!/bin/sh\nfor arg do [ "$arg" != -T ] || exit 91; done\nexec "$REAL_SCP" -S "$FIXTURE_SSH" "$@"\n')
        scp.chmod(0o755)
        host = root / 'host'
        host.mkdir(exist_ok=True)
        run = recovery['run'].replace('${{ github.run_id }}', '123').replace('${{ github.run_attempt }}', '1')
        transport_env = dict(env, PATH=str(tools) + os.pathsep + env['PATH'],
                             REAL_SCP=shutil.which('scp'), FIXTURE_SSH=str(ssh), FIXTURE_RUNNER=str(runner))
        result = subprocess.run(['bash', '-eu', '-c', run], cwd=host, env=transport_env,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        upload = next(step for step in steps if step.get('if') == 'always()' and '/upload-artifact@' in step.get('uses', ''))
        return host / upload['with']['path']

    def execute_prerequisite(self, root, name, fail_stage):
        env = self.checkout(root)
        runner = root / 'runner'
        tools = root / 'tools'
        tools.mkdir()
        llvm = root / 'selected-llvm19'
        llvm.mkdir()
        scripts = {
            'node': '#!/bin/sh\necho v24.21.0\n',
            'llvm-config19': '#!/bin/sh\necho "$SELECTED_LLVM"\n',
            'curl': '#!/bin/sh\nexit 0\n',
            'sha256': '#!/bin/sh\necho 729de494dd2872e5a3a6c32a1cd156a5413d4aca2772b2d873ee86bb5531bcd9\n',
            # Download/extraction is the external boundary; execute the workflow's
            # actual configure invocation and capture what reaches its subprocess.
            'tar': '''#!/usr/bin/env python3
import os, pathlib
source = pathlib.Path(os.environ['FIXTURE_RUNNER']) / 'node-source/node-v24.15.0'
source.mkdir(parents=True)
configure = source / 'configure'
configure.write_text(''' + repr('''#!/usr/bin/env python3
import json, os, pathlib, sys
pathlib.Path(os.environ['OBSERVED']).write_text(json.dumps({key: os.environ.get(key) for key in ('CC', 'CXX')}))
print('node-configure-diagnostic', flush=True)
sys.exit(43 if os.environ['FAIL_STAGE'] == 'configure' else 0)
''') + ''')
configure.chmod(0o755)
''',
            'gmake': '#!/bin/sh\necho node-build-diagnostic\nexit 47\n',
        }
        for name_, source in scripts.items():
            path = tools / name_
            path.write_text(source)
            path.chmod(0o755)
        for compiler in ('clang', 'clang++'):
            path = llvm / compiler
            path.write_text('#!/bin/sh\necho "selected clang version 19.1.7"\n')
            path.chmod(0o755)
        run, steps = self.guest_run(name, runner)
        env = dict(env, PATH=str(tools) + os.pathsep + os.environ['PATH'],
                   FIXTURE_RUNNER=str(runner), SELECTED_LLVM=str(llvm),
                   OBSERVED=str(root / 'observed.json'), FAIL_STAGE=fail_stage)
        env.pop('CC', None)
        env.pop('CXX', None)
        result = subprocess.run(['sh', '-eu', '-c', run], cwd=root, env=env,
                                capture_output=True, text=True, timeout=10)
        return result, steps, runner, llvm, env

    def test_pinned_node_configure_receives_selected_llvm_compilers(self):
        for name in self.workflows:
            with self.subTest(workflow=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                result, _, _, llvm, _ = self.execute_prerequisite(root, name, 'build')
                self.assertEqual(result.returncode, 47, result.stdout + result.stderr)
                observed = json.loads((root / 'observed.json').read_text())
                self.assertEqual(observed, dict(CC=str(llvm / 'clang'), CXX=str(llvm / 'clang++')))

    def test_node_failure_diagnostics_survive_failed_vm_copyback(self):
        for name in self.workflows:
            for stage, status in (('configure', 43), ('build', 47)):
                with self.subTest(workflow=name, stage=stage), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    result, steps, runner, llvm, env = self.execute_prerequisite(root, name, stage)
                    self.assertEqual(result.returncode, status, result.stdout + result.stderr)
                    logs = self.recover(root, steps, runner, env) / 'node-source'
                    provenance = (logs / 'compiler.log').read_text()
                    self.assertIn(str(llvm / 'clang++'), provenance)
                    self.assertIn('clang version 19', provenance)
                    self.assertIn('node-configure-diagnostic', (logs / 'configure.log').read_text())
                    if stage == 'build':
                        self.assertIn('node-build-diagnostic', (logs / 'build.log').read_text())
                    self.assertFalse((logs / 'node-v24.15.0').exists(), 'Do not upload the Node source/build tree')

    def test_guest_git_trust_is_exact_checkout_and_inherited_by_provenance_calls(self):
        for name in self.workflows:
            with self.subTest(workflow=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                checkout = root / 'pinned checkout'
                env = self.checkout(checkout)
                other = root / 'other checkout'
                self.checkout(other)
                env['GIT_TEST_ASSUME_DIFFERENT_OWNER'] = '1'
                refused = subprocess.run(['git', 'rev-parse', '--verify', 'HEAD^{commit}'],
                                         cwd=checkout, env=env, capture_output=True, text=True)
                self.assertNotEqual(refused.returncode, 0)
                self.assertIn('dubious ownership', refused.stderr)
                runner = root / 'runner'
                run, _ = self.guest_run(name, runner)
                early = run.split('if [ "$(node --version)"', 1)[0]
                # The child process uses the same Git contract as the real Python
                # provenance validators; another checkout must remain untrusted.
                probe = 'import subprocess; subprocess.run(["git", "rev-parse", "--verify", "HEAD^{commit}"], check=True)'
                result = subprocess.run(['sh', '-eu', '-c', early + '\n' + shlex.join([sys.executable, '-c', probe]) +
                    '\ngit -C ' + shlex.quote(str(other)) + ' rev-parse --verify HEAD'],
                    cwd=checkout, env=env, capture_output=True, text=True, timeout=10)
                self.assertIn('dubious ownership', result.stderr)
                self.assertIn(str(other), result.stderr)
                self.assertNotIn(str(checkout), result.stderr)
                self.assertEqual(result.returncode, 128)
                folder = 'package-evidence' if name == self.workflows[0] else 'platform-evidence'
                provenance = (runner / folder / 'git-provenance.log').read_text()
                head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=checkout, text=True).strip()
                self.assertIn(str(checkout), provenance)
                self.assertIn('git version', provenance)
                self.assertIn(head, provenance)
                self.assertIn(head, result.stdout)

    def test_failed_early_git_probe_is_retained_and_recovered_before_node(self):
        for name in self.workflows:
            with self.subTest(workflow=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                env = self.checkout(root)
                (root / '.git/HEAD').write_text('ref: refs/heads/missing\n')
                runner = root / 'runner'
                run, steps = self.guest_run(name, runner)
                early = run.split('if [ "$(node --version)"', 1)[0]
                result = subprocess.run(['sh', '-eu', '-c', early], cwd=root, env=env,
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Needed a single revision', result.stderr)
                evidence = self.recover(root, steps, runner, env)
                self.assertIn('Needed a single revision', (evidence / 'git-provenance.log').read_text())
                self.assertFalse((evidence / 'node-source').exists())

    def test_successful_evidence_recovery_merges_named_directory_in_place(self):
        for name in self.workflows:
            with self.subTest(workflow=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                runner = root / 'runner'
                _, steps = self.guest_run(name, runner)
                folder = 'package-evidence' if name == self.workflows[0] else 'platform-evidence'
                source = runner / folder
                (source / 'platform').mkdir(parents=True)
                (source / 'platform/FreeBSD.tar.gz').write_bytes(b'platform proof')
                (source / '.diagnostic').write_text('hidden diagnostic')
                existing = root / 'host' / folder
                existing.mkdir(parents=True)
                (existing / 'kept.log').write_text('action copyback diagnostic')
                evidence = self.recover(root, steps, runner, dict(os.environ))
                self.assertEqual((evidence / 'kept.log').read_text(), 'action copyback diagnostic')
                self.assertEqual((evidence / '.diagnostic').read_text(), 'hidden diagnostic')
                self.assertEqual((evidence / 'platform/FreeBSD.tar.gz').read_bytes(), b'platform proof')
                self.assertFalse((evidence / folder).exists())


class WindowsGitPreflight(unittest.TestCase):
    def test_native_git_checkout_diagnostic_runs_before_provider_build(self):
        from contextlib import redirect_stdout
        import io
        from unittest.mock import patch
        for name, job in (('v3-package-validation.yml', 'mingw64'), ('v3-release-acceptance.yml', 'windows')):
            with self.subTest(workflow=name):
                steps = workflow(name)['jobs'][job]['steps']
                preflight = next((step for step in steps if "<<'GIT_PROBE'" in step.get('run', '')), None)
                self.assertIsNotNone(preflight, 'native Git checkout preflight missing')
                provider = next(step for step in steps if 'prepare-v3-package-ci.py' in step.get('run', ''))
                self.assertLess(steps.index(preflight), steps.index(provider))
                script = preflight['run'].split("<<'GIT_PROBE'\n", 1)[1].split('\nGIT_PROBE', 1)[0]
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    git = root / ('git.exe' if os.name == 'nt' else 'git')
                    git.write_text('selected native Git fixture')
                    git.chmod(0o755)
                    popen = subprocess.Popen
                    code = '''
import sys
if sys.argv[1:] == ['--version']:
    print('git version selected-native-fixture')
else:
    assert sys.argv[1:] == ['rev-parse', '--verify', 'HEAD^{commit}'], sys.argv
    print('fatal: native checkout diagnostic fixture', file=sys.stderr)
    sys.exit(128)
'''
                    def create_process(argv, **options):
                        self.assertEqual(argv[0], 'git')
                        return popen([sys.executable, '-c', code] + argv[1:], **options)
                    output = io.StringIO()
                    with patch.dict(os.environ, PATH=str(root)), redirect_stdout(output), \
                            patch.object(subprocess, 'Popen', side_effect=create_process):
                        with self.assertRaises(subprocess.CalledProcessError):
                            exec(compile(script, 'native-git-preflight', 'exec'), {})
                    self.assertIn(str(git), output.getvalue())
                    self.assertIn('git version selected-native-fixture', output.getvalue())
                    self.assertIn('fatal: native checkout diagnostic fixture', output.getvalue())


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

    def callback_configure(self, root, fail_stage=''):
        lane = workflow('v3-native.yml')
        job = lane['jobs']['fuzz']
        steps = job['steps']
        step = next(step for step in steps if step.get('name') == 'Configure genuine native sources')
        source = root / 'source'; source.mkdir()
        runner = root / 'runner'; runner.mkdir()
        bootstrap = source / 'bootstrap'
        bootstrap.write_text('#!/bin/sh\necho bootstrap-diagnostic\n[ "$FAIL_STAGE" != bootstrap ]\n')
        configure = source / 'configure'
        configure.write_text("""#!/usr/bin/env python3
import json, os, pathlib, sys
pathlib.Path('config.log').write_text('provider-probe-diagnostic')
pathlib.Path('observed.json').write_text(json.dumps(dict(os.environ)))
print('configure-diagnostic', flush=True)
expected = os.environ['RUNNER_TEMP'] + '/prerequisites/provider/lib'
sys.exit(23 if os.environ.get('LD_LIBRARY_PATH') != expected or os.environ['FAIL_STAGE'] == 'configure' else 0)
""")
        for path in (bootstrap, configure): path.chmod(0o755)
        env = dict(os.environ, RUNNER_TEMP=str(runner), GITHUB_WORKSPACE=str(source),
                   TARGET='quic_tls_callbacks', FAIL_STAGE=fail_stage)
        env.pop('LD_LIBRARY_PATH', None)
        result = subprocess.run(workflow_shell(lane, job, step) + [step['run']],
                                cwd=source, env=env, capture_output=True, text=True, timeout=10)
        upload = next(step for step in steps if '/upload-artifact@' in step.get('uses', ''))
        self.assertEqual(upload['if'], 'always()')
        retained = set()
        for pattern in upload['with']['path'].splitlines():
            for name in glob.glob(pattern.replace('${{ runner.temp }}', str(runner)), recursive=True):
                path = Path(name)
                if path.is_dir():
                    retained.update(p for p in path.rglob('*') if p.is_file())
                else:
                    retained.add(path)
        return result, runner / 'native', retained

    def test_callback_configure_receives_selected_provider_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            result, native, _ = self.callback_configure(Path(directory))
            observed = json.loads((native / 'observed.json').read_text())
            self.assertEqual(observed.get('LD_LIBRARY_PATH'), str(native.parent / 'prerequisites/provider/lib'))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_callback_configure_failure_retains_probe_and_command_diagnostics(self):
        with tempfile.TemporaryDirectory() as directory:
            result, native, retained = self.callback_configure(Path(directory), 'configure')
            self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
            self.assertIn(native / 'config.log', retained)
            self.assertEqual((native / 'config.log').read_text(), 'provider-probe-diagnostic')
            self.assertIn(native / 'configure-output.log', retained)
            self.assertIn('configure-diagnostic', (native / 'configure-output.log').read_text())
            self.assertIn(native / 'bootstrap.log', retained)

    def test_callback_bootstrap_failure_retains_command_diagnostics(self):
        with tempfile.TemporaryDirectory() as directory:
            result, native, retained = self.callback_configure(Path(directory), 'bootstrap')
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertFalse((native / 'observed.json').exists(), 'Configure ran after failed bootstrap')
            self.assertFalse((native / 'configure-output.log').exists())
            self.assertIn(native / 'bootstrap.log', retained)
            self.assertIn('bootstrap-diagnostic', (native / 'bootstrap.log').read_text())

    def test_native_tls_check_uses_selected_provider_cli(self):
        lane = workflow('v3-native.yml')
        job = lane['jobs']['native']
        step = next(step for step in job['steps'] if 'make check' in step.get('run', ''))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'; source.mkdir()
            runner = root / 'runner'; runner.mkdir()
            tools = root / 'bin'; tools.mkdir()
            provider = runner / 'prerequisites/provider'
            (provider / 'bin').mkdir(parents=True)
            cli = provider / 'bin/openssl'
            cli.write_text('#!/bin/sh\nprintf "selected-cli %s\\n" "$*"\n')
            (source / 'bootstrap').write_text('#!/bin/sh\nexit 0\n')
            (source / 'configure').write_text('#!/bin/sh\nexit 0\n')
            (tools / 'make').write_text("""#!/usr/bin/env python3
import json, os, pathlib, sys
if 'check' in sys.argv:
    pathlib.Path('check-environment.json').write_text(json.dumps(dict(os.environ)))
""")
            # The shell fixture has no ELF dependencies; actual shared linkage is checked by the native gate.
            (tools / 'ldd').write_text('#!/bin/sh\necho "selected-cli-linkage $1"\n')
            for path in (cli, source / 'bootstrap', source / 'configure', tools / 'make', tools / 'ldd'):
                path.chmod(0o755)
            env = dict(os.environ, RUNNER_TEMP=str(runner), GITHUB_WORKSPACE=str(source),
                       TLS_MODE='yes', PATH=str(tools) + os.pathsep + os.environ['PATH'])
            for name in ('V3_OPENSSL', 'LD_LIBRARY_PATH'):
                env.pop(name, None)
            result = subprocess.run(workflow_shell(lane, job, step) + [step['run']],
                                    cwd=source, env=env, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            observed = json.loads((runner / 'native/check-environment.json').read_text())
            self.assertEqual(observed.get('V3_OPENSSL'), str(cli))
            self.assertEqual(observed.get('LD_LIBRARY_PATH'), str(provider / 'lib'))
            provenance = (runner / 'native/openssl-provenance.log').read_text()
            self.assertIn('selected-cli version -a', provenance)
            self.assertIn('selected-cli-linkage ' + str(cli), provenance)

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
