"""Task-local release execution, using existing package and protocol gates."""
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import shlex
import shutil
import sys
import uuid

import release_artifact as release_identity
import release_checks as checks

TLS_SUITES = '''tls_policy_concurrency tls_policy_hostile_profiles tls_acme_lifetime tls_acme_selection
 tls_acme_credentials tls_io tls_io_race tls_io_loopback tls_credentials tls_credentials_rotation
 tls_selection tls_mtls tls_psk_contract tls_psk tls_psk_runtime quic_tls_session quic_tls_handshake
 quic_recovery_tls http3_network_owner quic_interop_tls quic_interop_files tls_rotation'''.split()
OWNER_SUITES = '''websocket_session_race websocket_drain_race task_race task_executor task_resume_signal_lifetime
 connection_engine worker_pool external_readiness_adapter resource_limits quic_connection_lifecycle'''.split()
FUZZ_OFF = '''http1_head http1_body websocket_codec hpack http2_engine quic_parser quic_state
 quic_recovery http3_engine qpack'''.split()


def tree_artifacts(gate, directory):
    paths = [p for p in directory.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    gate.need(len(paths) < 20000, 'evidence tree exceeds count bound')
    return [gate.artifact(path) for path in sorted(paths)]


class ReleaseAttempt:
    def __init__(self, args, gate, manifest):
        self.args, self.gate, self.manifest = args, gate, manifest
        self.root, self.output = gate.ROOT, args.output
        self.source = None
        self.env = os.environ.copy()
        self.env.update(CXX=args.compiler, CC=args.cc, ASAN_OPTIONS=args.asan_options,
                        UBSAN_OPTIONS='halt_on_error=1')
        self.env.pop('V3_RELEASE_IDENTITY', None)
        self.env['LD_LIBRARY_PATH'] = str(args.provider_prefix / 'lib')
        self.env['V3_TLS_CFLAGS'] = '-I' + str(args.provider_prefix / 'include')
        self.env['V3_TLS_LIBS'] = '-L' + str(args.provider_prefix / 'lib') + ' -lssl -lcrypto'
        self.builds = {}
        self.linkage_records = {}

    def command(self, cmd, argv, cwd=None, env=None, validator=None, timeout=1800):
        return self.gate.command_receipt(cmd, argv, cwd or self.source or self.root,
                                         env or self.env, validator, timeout)

    def public_command(self, cmd, argv, timeout):
        if getattr(self.args, 'isolated_public_network', False):
            argv = ['unshare', '-Urn', 'sh', '-c', 'ip link set lo up; exec "$@"', 'public-release', *argv]
        return self.command(cmd, argv, timeout=timeout)

    def prepare(self, cmd):
        tools = {}
        self.manifest['tools'] = tools
        tool_inputs = {'compiler': self.args.compiler, 'cc': self.args.cc, 'make': self.args.make,
                       'python': sys.executable, 'client-python': self.args.python, 'node': self.args.node,
                       'h2spec': self.args.h2spec, 'go': self.args.go, 'docker': 'docker',
                       'tshark': self.args.tshark, 'editcap': 'editcap', 'autoreconf': 'autoreconf',
                       'autoconf': 'autoconf', 'automake': 'automake', 'libtoolize': 'libtoolize',
                       'pkg-config': 'pkg-config', 'timeout': self.args.test_timeout_tool, 'ar': 'ar', 'readelf': 'readelf', 'nm': 'nm', 'ldd': 'ldd'}
        for name, value in tool_inputs.items():
            tool = shutil.which(value) if value else None
            tools[name] = {'requested': value, 'status': 'unavailable'}
            if tool:
                try:
                    version_flag = 'version' if name == 'go' else '--version'
                    version = self.command(cmd, [tool, version_flag], self.root, timeout=30)
                    tools[name] = {'requested': value, 'status': 'available', 'binary': self.gate.artifact(tool),
                                   'command': cmd.records[-1].copy()}
                except Exception as error:
                    tools[name]['failure'] = str(error)
        self.gate.need(all(tools[k]['status'] == 'available' for k in ('compiler', 'cc', 'make')),
                       'required compiler/cc/make tool unavailable')
        if self.args.python:
            try:
                text = cmd([self.args.python, '-c', 'import json,sys,ssl; from importlib.metadata import version; '
                     'print(json.dumps({"python":sys.version,"ssl":ssl.OPENSSL_VERSION,**{n:version(n) for n in ("h2","aioquic","websockets")}}))'],
                     self.root, self.env, timeout=30)
                self.manifest['client_packages'] = json.loads(text)
            except Exception as error:
                self.manifest['client_packages'] = {'failure': str(error)}
        self.manifest['source'] = release_identity.source_identity(self.root)
        bootstrap_env = dict(self.env)
        bootstrap_env.pop('V3_TLS_CFLAGS', None); bootstrap_env.pop('V3_TLS_LIBS', None)
        self.command(cmd, ['bash', self.root / 'bootstrap'], self.root, bootstrap_env)
        dist = self.output / 'distribution-build'; dist.mkdir()
        self.command(cmd, ['bash', self.root / 'configure', '--enable-v3-tls=no', '--disable-examples'], dist, bootstrap_env)
        self.command(cmd, [self.args.make, 'dist'], dist, bootstrap_env)
        archives = list(dist.glob('libhttpserver-*.tar.gz'))
        self.gate.need(len(archives) == 1, 'missing/ambiguous fresh distribution archive')
        self.source = self.gate.extract_archive(archives[0], self.output / 'extracted')
        archive_files = release_identity.source_files(self.source)
        snapshot = self.manifest['source']['files_sha256']
        transformations = checks.verify_snapshot_packaging(self.root, snapshot, self.source)
        bundle = {}
        # Repository specifications are declared tooling inputs, never borrowed libraries.
        candidates = [self.root / '.gitignore'] + list((self.root / 'specs').rglob('*.md')) + list((self.root / 'docs').rglob('*.md'))
        for path in candidates:
            name = path.relative_to(self.root).as_posix()
            destination = self.source / name
            if not destination.exists():
                self.gate.need(snapshot.get(name) == release_identity.sha256(path), 'tooling input not from selected snapshot: ' + name)
                destination.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(path, destination)
                bundle[name] = release_identity.sha256(destination)
        files = release_identity.source_files(self.source)
        identity = dict(schema_version=1, source_root=str(self.source), source=self.manifest['source'],
                        archive=self.gate.artifact(archives[0]), files_sha256=files, tooling_bundle=bundle,
                        packaged_files_sha256=archive_files, configured_transformations=transformations)
        path = self.output / 'artifact-identity.json'; self.gate.save(path, identity)
        self.env['V3_RELEASE_IDENTITY'] = str(path)
        self.manifest['release_artifact'] = identity
        provider_files = [self.args.provider_prefix / 'include/openssl/opensslv.h']
        provider_files += [self.args.provider_prefix / 'lib' / name for name in
                           ('libssl.a', 'libcrypto.a', 'libssl.so.3', 'libcrypto.so.3')
                           if (self.args.provider_prefix / 'lib' / name).is_file()]
        self.manifest['artifacts'] = [self.gate.artifact(p) for p in [archives[0], path] + provider_files + self.args.tool_artifact]
        self.manifest['sanitizers'] = {'ASAN_OPTIONS': self.args.asan_options, 'UBSAN_OPTIONS': 'halt_on_error=1',
            'provider_instrumented': False, 'clients_instrumented': False,
            'leak_detection': 'unexecuted' if 'detect_leaks=0' in self.args.asan_options else 'requested'}
        return {'artifacts': self.manifest['artifacts']}

    def identity(self, mode='both', sanitizer='none'):
        return {'archive_sha256': self.manifest['release_artifact']['archive']['sha256'],
                'snapshot_sha256': self.manifest['source']['snapshot_sha256'],
                'extracted_sha256': hashlib.sha256(json.dumps(self.manifest['release_artifact']['files_sha256'], sort_keys=True).encode()).hexdigest(),
                'tls': mode, 'sanitizer': sanitizer}

    def stage(self, name, execute, mode='both', sanitizer='none'):
        def run(cmd):
            release_identity.verified(self.source, self.env['V3_RELEASE_IDENTITY'])
            value = execute(cmd)
            files = value.pop('artifacts', [])
            files += [self.gate.artifact(p) for p in (self.output / name).rglob('*') if p.is_file()]
            value['receipt'] = {'status': 'passed', 'gate': name, 'identity': self.identity(mode, sanitizer),
                                'counts': value.pop('counts'), 'artifacts': files, **value}
            value['identity'] = self.identity(mode, sanitizer)
            self.gate.verify_receipt(value['receipt'], value['identity'], public=name == 'public-capabilities')
            return value
        row = self.gate.execute_stage(self.manifest, self.output / name, name, run, retain=True)
        if 'package build prerequisite unavailable' in row.get('failure', ''):
            row['status'] = 'unexecuted'
            row['blocked_by'] = 'package build/install prerequisite'
        row['owner'] = {'public-capabilities': 'v3 public listener/credential integration owner',
                        'native-throughput-latency': 'v3 performance owner; triage native v3 benchmark dependency'}.get(name, row['owner'])
        return row

    def package(self, cmd):
        evidence = self.output / 'package/evidence'
        argv = [sys.executable, self.source / 'scripts/check-v3-installed-consumer.py', '--output', evidence,
                '--provider-prefix', self.args.provider_prefix, '--compiler', self.args.compiler,
                '--cc', self.args.cc, '--make', self.args.make, '--jobs', str(self.args.jobs),
                '--client-python', self.args.python, '--node', self.args.node, '--optimized']
        for path in self.args.pkg_config_dir: argv += ['--pkg-config-dir', path]
        for path in self.args.prerequisite_prefix: argv += ['--prerequisite-prefix', path]
        self.public_command(cmd, argv, timeout=3600)
        return {'counts': checks.validate_package(self.source, evidence, self.gate.read_json)}

    def normal_build(self, mode):
        lane = self.output / ('package/evidence/tls-' + mode)
        build, prefix = lane / 'build', lane / 'prefix'
        self.gate.need((build / 'config.h').is_file(), 'package build prerequisite unavailable: TLS=' + mode)
        self.gate.need(('#define NATIVE_V3_TLS' in (build / 'config.h').read_text()) == (mode == 'yes'), 'wrong TLS build')
        text = (build / 'src/Makefile').read_text()
        match = re.search(r'^abs_top_srcdir = (.+)$', text, re.M)
        self.gate.need(match and Path(match[1]).resolve() == self.source, 'build uses another source tree')
        return build, prefix

    def linkage(self, cmd, mode):
        build, prefix = self.normal_build(mode)
        if mode in self.linkage_records:
            self.gate.verify_files(self.linkage_records[mode])
            return self.linkage_records[mode]
        paths = self.gate.verify_library_files(build, prefix)
        records = [self.gate.artifact(p) for p in paths]
        installed = prefix / 'lib/libhttpserver.a'
        names = set(cmd(['ar', 't', installed], build, self.env).splitlines())
        for archive in (build / 'src/.libs').glob('libhttpserver_v3*.a'):
            for member in cmd(['ar', 't', archive], build, self.env).splitlines():
                self.gate.need(member in names, 'native convenience archive member absent from installed library')
                cmd(['ar', 'p', archive, member], build, self.env); first = cmd.records[-1]['sha256']
                cmd(['ar', 'p', installed, member], build, self.env)
                self.gate.need(first == cmd.records[-1]['sha256'], 'protocol library member differs from packaged archive')
            records.append(self.gate.artifact(archive))
        self.linkage_records[mode] = records
        return records

    def test_guard(self):
        tool = shutil.which(self.args.test_timeout_tool)
        self.gate.need(tool, 'required per-test deadline tool unavailable')
        return 'LOG_COMPILER=' + shlex.join([tool, '--kill-after=5s', '180s'])

    def native_check(self, cmd, build, env, timeout):
        try:
            self.command(cmd, [self.args.make, '-j' + str(self.args.jobs), 'check', self.test_guard()], build, env, timeout=timeout)
        finally:
            # Preserve per-case failure details even when make check exits early.
            evidence = cmd.directory / 'native-tests'; evidence.mkdir(exist_ok=True)
            for pattern in ('*.log', '*.trs'):
                for path in (build / 'test').rglob(pattern):
                    destination = evidence / path.relative_to(build / 'test')
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, destination)

    def cxx(self, cmd, mode):
        build, prefix = self.normal_build(mode)
        self.native_check(cmd, build, self.env, 3600)
        result = checks.automake_result(build, platform.system())
        installed_env = dict(self.env, BUILD_DIR=str(build), PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'), PKG_CONFIG_PATH='')
        self.command(cmd, ['bash', self.source / 'scripts/verify-installed-examples.sh'], build, installed_env)
        return {'counts': result, 'artifacts': self.linkage(cmd, mode), 'kind': 'packaged library and installed examples'}

    def suites(self, cmd, names, mode, build=None, tsan=False):
        normal = build is None
        build = build or self.normal_build(mode)[0]
        self.command(cmd, [self.args.make, '-j' + str(self.args.jobs), '-C', build / 'test', *names], timeout=3600)
        total_tests = total_checks = 0
        artifacts = []
        results = []
        for name in names:
            if name == 'tls_rotation':
                self.gate.need(normal and mode == 'yes', 'installed rotation requires the normal TLS-on package')
                evidence = self.output / 'package/evidence/tls-yes/public-tls'
                receipt_path = evidence / 'receipt.json'
                receipt = self.gate.read_json(receipt_path)
                result = checks.rotation_counts(receipt, self.source, build, self.normal_build(mode)[1])
                artifacts += tree_artifacts(self.gate, evidence)
                artifacts += tree_artifacts(self.gate, Path(receipt['consumer_directory']))
                total_tests += result['tests']; total_checks += result['checks']
                results.append({'suite': name, 'counts': result, 'installed_receipt': self.gate.artifact(receipt_path)})
                continue
            path = build / 'test' / name
            actual = path.parent / '.libs' / name
            artifacts.append(self.gate.artifact(actual if actual.is_file() else path))
            text = cmd([path], self.source / 'test', self.env, timeout=180, allow_failure=True)
            self.gate.need(cmd.records[-1]['exit'] == 0, 'suite failed: ' + name)
            result = checks.native_counts(self.source, name, text, tsan)
            total_tests += result['tests']; total_checks += result.get('checks', result['tests'])
            results.append({'suite': name, 'counts': result, 'command': cmd.records[-1].copy()})
        if normal:
            artifacts += self.linkage(cmd, mode)
        return {'counts': checks.counts(total_tests, total_checks), 'artifacts': artifacts, 'suites': results,
                'kind': 'supporting private/header tests with packaged native archive equivalence'}

    def sanitized(self, cmd, name, mode, sanitizer):
        if sanitizer == 'address,undefined':
            options = dict(item.split('=', 1) for item in self.args.asan_options.split(':') if '=' in item)
            self.gate.need(options.get('detect_leaks') == options.get('detect_stack_use_after_return') == '1',
                           'required leak and stack checking must be enabled')
        build = self.output / name / 'build'; build.mkdir()
        flags = '-std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=' + sanitizer
        if sanitizer == 'address,undefined': flags += ' -fno-sanitize-recover=undefined -fsanitize-address-use-after-scope'
        else: flags += ' -DLHS_SANITIZER_OWNS_OPERATOR_NEW'
        env = dict(self.env, CXXFLAGS=flags, LDFLAGS='-fsanitize=' + sanitizer)
        if sanitizer == 'thread':
            env['TSAN_OPTIONS'] = 'halt_on_error=1:suppressions=' + str(self.source / 'test/tsan.supp')
        self.command(cmd, ['bash', self.source / 'configure', '--enable-v3-tls=' + mode, '--disable-examples'], build, env)
        self.command(cmd, [self.args.make, '-j' + str(self.args.jobs), '-C', build / 'src'], build, env, timeout=3600)
        previous = self.env; self.env = env
        try:
            if sanitizer == 'thread':
                names = OWNER_SUITES + (['tls_policy_concurrency', 'tls_io_race', 'tls_credentials_rotation', 'quic_tls_handshake'] if mode == 'yes' else [])
                result = self.suites(cmd, names, mode, build, tsan=True)
                result['excluded_cases'] = [
                    'websocket_drain_race/close_allocation_failure_settles_once_and_wakes_parked_receive',
                    'task_resume_signal_lifetime/published_waiter_can_die_during_timeout_registration']
                return result
            self.native_check(cmd, build, env, 5400)
            return {'counts': checks.automake_result(build, platform.system()),
                    'artifacts': tree_artifacts(self.gate, build / 'src/.libs')}
        finally:
            self.builds[name] = build
            self.env = previous

    def h1(self, cmd):
        build = self.normal_build('no')[0]
        shell = (self.source / 'scripts/run-v3-http1-websocket-gates.sh').read_text()
        names = re.search(r"^tests='([^']+)'", shell, re.M)[1].split()
        self.command(cmd, [self.args.make, '-j' + str(self.args.jobs), '-C', build / 'test', *names, 'native_websocket_fixture'])
        directory = self.output / 'http1-websocket/evidence'
        self.gate.need(self.args.python, 'required pinned independent Python client missing')
        self.command(cmd, ['bash', self.source / 'scripts/run-v3-http1-websocket-gates.sh', '--build-dir', build,
            '--log-dir', directory, '--compiler', self.args.compiler, '--python', self.args.python, '--independent'])
        results = [checks.native_counts(self.source, name, (directory / (name + '.log')).read_text()) for name in names]
        self.gate.need((directory / 'independent.log').is_file(), 'independent WebSocket receipt missing')
        return {'counts': checks.counts(sum(r['tests'] for r in results)), 'artifacts': self.linkage(cmd, 'no')}

    def child(self, cmd, name):
        directory = self.output / name / 'evidence'
        build = self.normal_build('yes')[0]
        if name in ('http2', 'resource-slow-peers'):
            self.command(cmd, [self.args.make, '-j' + str(self.args.jobs), '-C', build / 'test', 'http2_tls_fixture', 'http3_udp_fixture'])
        if name == 'http2':
            self.gate.need(all((self.args.python, self.args.node, self.args.h2spec)), 'required HTTP/2 client/tool path missing')
            self.command(cmd, [sys.executable, self.source / 'scripts/run-v3-http2-gates.py', '--build-dir', build,
                '--log-dir', directory, '--python', self.args.python, '--node', self.args.node, '--h2spec', self.args.h2spec])
            result = checks.validate_h2(self.source, directory, self.gate.read_json)
        elif name == 'resource-slow-peers':
            self.gate.need(self.args.python, 'required pinned resource client Python missing')
            self.command(cmd, [self.args.python, self.source / 'scripts/lib/resource_limits_probe.py',
                '--h2-fixture', build / 'test/http2_tls_fixture', '--h3-fixture', build / 'test/http3_udp_fixture', '--output', directory], timeout=120)
            result = checks.validate_resources(directory, self.gate.read_json)
        elif name == 'parity':
            self.gate.need(self.args.python, 'required pinned parity client Python missing')
            self.command(cmd, [sys.executable, self.source / 'scripts/run-v3-behavior-parity.py', '--root', self.source,
                '--tls-on', build, '--tls-off', self.normal_build('no')[0], '--client-python', self.args.python,
                '--openssl', self.args.provider_prefix / 'bin/openssl', '--receipt-dir', directory], timeout=3600)
            runner = checks.load_runner('run-v3-behavior-parity.py', self.source)
            receipt = self.gate.read_json(directory / 'receipt.json')
            self.gate.need(not runner.check_receipt(self.source, self.gate.read_json(self.source / 'test/parity/task179-capabilities.json'), receipt), 'parity receipt incomplete')
            result = checks.counts(len(receipt['observations']))
        else:
            raise ValueError('unknown release child')
        return {'counts': result, 'artifacts': tree_artifacts(self.gate, directory) + self.linkage(cmd, 'yes')}

    def fuzz(self, cmd, mode):
        name = 'fuzz-' + ('off' if mode == 'no' else 'on')
        directory = self.output / name / 'evidence'
        targets = FUZZ_OFF if mode == 'no' else ['quic_tls_callbacks']
        self.command(cmd, ['bash', self.source / 'scripts/run-v3-protocol-fuzz.sh', '--compiler', self.args.compiler,
            '--build-dir', directory, '--native-config-dir', self.normal_build(mode)[0], '--targets', ' '.join(targets), '--runs', '2000', '--seconds', '30'], timeout=7200)
        return {'counts': checks.validate_fuzz(self.source, directory, targets)}

    def h3(self, cmd):
        directory = self.output / 'http3-full/evidence'
        argv = [sys.executable, self.source / 'scripts/run-v3-http3-full-gates.py', '--output', directory,
                '--tls-off-build', self.normal_build('no')[0], '--tls-on-build', self.normal_build('yes')[0],
                '--sanitized-off-build', self.builds.get('asan-ubsan-off', '/missing/sanitized-off'),
                '--sanitized-on-build', self.builds.get('asan-ubsan-on', '/missing/sanitized-on'),
                '--provider-prefix', self.args.provider_prefix, '--compiler', self.args.compiler]
        for key in ('python', 'go', 'tshark', 'runner_python', 'runner_dir', 'server_image', 'simulator_clock_receipt'):
            value = getattr(self.args, key)
            if value: argv += ['--' + key.replace('_', '-'), value]
        argv += ['--go-client', self.output / 'http3-full/go-client']
        if self.args.docker_direct_routing_compat: argv += ['--docker-direct-routing-compat']
        self.command(cmd, argv, timeout=14400)
        full = checks.load_runner('run-v3-http3-full-gates.py', self.source)
        receipt = self.gate.read_json(directory / 'manifest.json')
        full.accept_stages(receipt['stages'])
        self.gate.need(receipt['status'] == 'passed', 'full HTTP/3 gate unsuccessful')
        return {'counts': checks.counts(len(receipt['stages'])), 'artifacts': self.linkage(cmd, 'yes')}

    def packaged_family(self, cmd, family):
        self.gate.need(all((self.args.python, self.args.node, self.args.go)),
                       'required packaged capability/benchmark clients missing')
        directory = self.output / family
        go_client = directory / 'quic-go'
        self.command(cmd, ['env', 'GOPROXY=off', 'GOTOOLCHAIN=local', self.args.go, 'build',
            '-C', self.source / 'test/integ/http3-client-go', '-o', go_client, '.'], timeout=300)
        children = []
        artifacts = [self.gate.artifact(go_client)]
        for mode in ('no', 'yes'):
            build, prefix = self.normal_build(mode)
            evidence = directory / ('tls-' + mode)
            run_id = uuid.uuid4().hex
            argv = [self.args.python, self.source / 'scripts/run-v3-packaged-adapters.py',
                '--family', family, '--archive', self.manifest['release_artifact']['archive']['path'],
                '--archive-sha256', self.manifest['release_artifact']['archive']['sha256'],
                '--source', self.source, '--build', build, '--prefix', prefix,
                '--provider-prefix', self.args.provider_prefix, '--tls', mode,
                '--compiler', self.args.compiler, '--client-python', self.args.python,
                '--node', self.args.node, '--quic-go', go_client, '--run-id', run_id, '--output', evidence]
            if family == 'native-throughput-latency': argv += ['--require-full-workload']
            self.public_command(cmd, argv, timeout=7200)
            receipt_path = evidence / 'receipt.json'
            self.command(cmd, argv + ['--validate', receipt_path], timeout=300)
            receipt = self.gate.read_json(receipt_path)
            children.append({'tls': mode, 'run_id': run_id, 'receipt': self.gate.artifact(receipt_path)})
            artifacts += receipt['artifacts'] + tree_artifacts(self.gate, evidence) + self.linkage(cmd, mode)
        cells = sum(len(self.gate.read_json(record['path'])['cells'])
                    for child in children
                    for record in self.gate.read_json(child['receipt']['path'])['children'].values())
        result = {'counts': checks.counts(cells), 'artifacts': artifacts, 'children': children,
                  'kind': 'installed public capability' if family == 'public-capabilities' else 'native full-workload benchmark'}
        if family == 'public-capabilities': result['witnesses'] = ['tls', 'h2', 'h3', 'credentials']
        return result

    def execute(self):
        prepared = self.gate.execute_stage(self.manifest, self.output / 'preparation', 'preparation', self.prepare)
        self.manifest['stages'].remove(prepared); self.manifest['preparation'] = prepared
        if prepared['status'] != 'passed':
            self.manifest['stages'] = [{'id': name, 'status': 'unexecuted', 'failure': 'preparation prerequisite: ' + prepared['failure'],
                                       'owner': 'TASK-185 release gates', 'artifacts': []} for name in self.gate.LOCAL_GATES]
            return
        self.stage('package', self.package)
        for name, mode in (('cxx20-off', 'no'), ('cxx20-on', 'yes')):
            self.stage(name, lambda cmd, mode=mode: self.cxx(cmd, mode), mode)
        self.stage('http1-websocket', self.h1, 'no')
        self.stage('http2', lambda cmd: self.child(cmd, 'http2'), 'yes')
        self.stage('tls', lambda cmd: self.suites(cmd, TLS_SUITES, 'yes'), 'yes')
        self.stage('parity', lambda cmd: self.child(cmd, 'parity'))
        self.stage('resource-plateaus', lambda cmd: self.suites(cmd, ['native_protocol_memory', 'resource_limits'], 'no'), 'no')
        self.stage('resource-slow-peers', lambda cmd: self.child(cmd, 'resource-slow-peers'), 'yes')
        for name, mode, sanitizer in (('asan-ubsan-off', 'no', 'address,undefined'), ('asan-ubsan-on', 'yes', 'address,undefined'),
                                      ('tsan-off', 'no', 'thread'), ('tsan-on', 'yes', 'thread')):
            self.stage(name, lambda cmd, name=name, mode=mode, sanitizer=sanitizer: self.sanitized(cmd, name, mode, sanitizer), mode, sanitizer)
        self.stage('http3-full', self.h3)
        for name, mode in (('fuzz-off', 'no'), ('fuzz-on', 'yes')):
            self.stage(name, lambda cmd, mode=mode: self.fuzz(cmd, mode), mode, 'fuzzer,address,undefined')
        for family in ('public-capabilities', 'native-throughput-latency'):
            self.stage(family, lambda cmd, family=family: self.packaged_family(cmd, family))
