#!/usr/bin/env python3
"""Prepare one archive, produce native platform proof, or authorize publication."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts/lib'))
from package_commands import Commands
import release_artifact as identity
import release_checks as checks
from release_ci import authorize, require, selected_archive, validate_request
from release_platform import PLATFORMS, referenced_files, retain, validate_origin


def gate_runner():
    spec = importlib.util.spec_from_file_location('release_gate_ci', ROOT / 'scripts/run-v3-release-gates.py')
    gate = importlib.util.module_from_spec(spec); sys.modules[spec.name] = gate; spec.loader.exec_module(gate)
    return gate


def prepare(args):
    validate_request(ROOT, args.commit, args.version)
    args.output.mkdir(parents=True, exist_ok=False)
    cmd = Commands(args.output)
    env = dict(os.environ); env.pop('V3_RELEASE_IDENTITY', None)
    cmd(['bash', str(ROOT / 'bootstrap')], ROOT, env)
    build = args.output / 'build'; build.mkdir()
    cmd(['bash', str(ROOT / 'configure'), '--disable-v3-tls', '--disable-examples'], build, env)
    cmd([args.make, 'dist'], build, env)
    archives = list(build.glob('libhttpserver-*.tar.gz'))
    require(len(archives) == 1, 'ambiguous/missing selected archive')
    archive = args.output / archives[0].name; shutil.copyfile(archives[0], archive)
    validate_request(ROOT, args.commit, args.version)
    source = identity.source_identity(ROOT)
    selected = selected_archive(ROOT, source, archive, identity.sha256(archive), args.output / 'verified')
    (args.output / 'selected.json').write_text(json.dumps(dict(commit=args.commit, version=args.version,
        archive=archive.name, sha256=identity.sha256(archive), source=source,
        files_sha256=selected['files_sha256']), indent=2) + '\n')
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
            output.write('sha256=' + identity.sha256(archive) + '\n')


def platform_proof(args):
    require(platform.system() in PLATFORMS, 'native nonlocal platform required')
    selected = json.loads(args.selection.read_text())
    validate_request(ROOT, selected['commit'], selected['version'])
    args.output.mkdir(parents=True, exist_ok=False)
    release = selected_archive(ROOT, identity.source_identity(ROOT), args.archive, selected['sha256'], args.output / 'source')
    require(release['source'] == selected['source'] and release['files_sha256'] == selected['files_sha256'], 'selected snapshot/tooling differs')
    source = Path(release['source_root'])
    identity_path = args.output / 'artifact-identity.json'; identity_path.write_text(json.dumps(release))
    env = dict(os.environ, V3_RELEASE_IDENTITY=str(identity_path),
        V3_TLS_CFLAGS='-I' + (args.provider_prefix / 'include').as_posix(),
        V3_TLS_LIBS='-L' + (args.provider_prefix / 'lib').as_posix() + ' -lssl -lcrypto')
    if args.toolchain_prefix:
        env['V3_PACKAGE_TOOLCHAIN'] = str(args.toolchain_prefix)
    env['LD_LIBRARY_PATH'] = str(args.provider_prefix / 'lib')
    env['DYLD_LIBRARY_PATH'] = str(args.provider_prefix / 'lib')
    env['PATH'] = str(args.provider_prefix / 'bin') + os.pathsep + env['PATH']
    env['V3_OPENSSL'] = str(args.provider_prefix / 'bin' / ('openssl.exe' if os.name == 'nt' else 'openssl'))
    cmd = Commands(args.output)
    package = args.output / 'matrix'
    argv = [sys.executable, source / 'scripts/check-v3-installed-consumer.py', '--output', package,
        '--provider-prefix', args.provider_prefix, '--compiler', args.compiler, '--cc', args.cc,
        '--make', args.make, '--jobs', str(args.jobs), '--client-python', args.client_python, '--node', args.node]
    for directory in args.pkg_config_dir: argv += ['--pkg-config-dir', directory]
    if args.toolchain_prefix: argv += ['--toolchain-prefix', args.toolchain_prefix]
    cmd(argv, source, env, timeout=10800)
    gate = gate_runner()
    expected = gate.expected_identity({'release_artifact':release}, platform.system())
    origin = dict(package=str(package), source_root=str(source), source_commit=selected['commit'],
        provider_header=str(args.provider_prefix / 'include/openssl/opensslv.h'), native={})
    result = checks.validate_package(ROOT, package, gate.read_json)
    for mode in ('no', 'yes'):
        build = package / ('tls-' + mode) / 'build'
        cmd([args.make, '-j' + str(args.jobs)], build, env, timeout=3600)
        cmd([args.make, 'check', '-j' + str(args.jobs)], build, env, timeout=7200)
        text = cmd([args.make, '--no-print-directory', '-s', '-C', str(build / 'test'),
            '--eval', 'ci-list-tests:;@printf "%s\\n" $(TEST_LOGS)', 'ci-list-tests'], build, env)
        tests = [name.removesuffix('.log') for name in text.splitlines() if name]
        origin['native'][mode] = dict(build=str(build), tests=tests)
        native = checks.automake_result(build, platform.system())
        result = checks.counts(result['tests'] + native['tests'], result['checks'] + native['checks'])
    files = [p for p in args.output.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    # Preserve explicit tools/provider inputs referenced outside the platform tree.
    files += [args.provider_prefix / 'include/openssl/opensslv.h', args.archive]
    for receipt_path in package.rglob('*.json'):
        files += list(referenced_files(json.loads(receipt_path.read_text())))
    for tool in (args.compiler, args.cc, args.make, args.client_python, args.node, 'git'):
        selected_tool = shutil.which(str(tool)); require(selected_tool, 'selected tool unavailable: ' + str(tool))
        files.append(Path(selected_tool))
    files = sorted(set(files))
    artifacts = [gate.artifact(path) for path in files]
    row = dict(id=platform.system(), status='passed', archive_sha256=selected['sha256'], identity=expected,
        origin=origin, artifacts=artifacts, receipt=dict(status='passed', gate=platform.system(), identity=expected, counts=result, artifacts=artifacts))
    directory = args.output / 'bundle' / platform.system()
    catalog = retain(directory, platform.system(), row, files)
    validate_origin(ROOT, row, catalog)
    with tarfile.open(args.output / (platform.system() + '.tar.gz'), 'w:gz') as stream:
        stream.add(directory, arcname=platform.system(), recursive=True)


def accept(args):
    gate = gate_runner(); manifest = gate.read_json(args.manifest)
    gate.import_platform_bundles(manifest, args.platform_bundle, args.output)
    gate.check_tag_ready(manifest)
    selected = json.loads(args.selection.read_text())
    require(manifest['release_artifact']['source'] == selected['source'] and
            manifest['release_artifact']['archive']['sha256'] == selected['sha256'], 'selected archive/source acceptance differs')
    args.output.mkdir(parents=True, exist_ok=True)
    gate.save(args.output / 'accepted-manifest.json', manifest)
    if args.publish:
        binding = dict(repository=os.environ['GITHUB_REPOSITORY'], run_id=os.environ['GITHUB_RUN_ID'],
            run_attempt=os.environ['GITHUB_RUN_ATTEMPT'], source_commit=selected['commit'], tag=selected['version'],
            archive_sha256=selected['sha256'], event=os.environ['GITHUB_EVENT_NAME'], artifact_id=args.artifact_id,
            producer_ids=list(PLATFORMS), workflow_source=os.environ['GITHUB_SHA'],
            producer_artifacts={name:dict(id=os.environ['PRODUCER_'+name.upper()+'_ID'],digest=os.environ['PRODUCER_'+name.upper()+'_DIGEST']) for name in ('freebsd','darwin','windows')})
        gate.save(args.output / 'authorization.json', authorize(gate, manifest, binding))
    else:
        gate.save(args.output / 'validation.json', dict(status='passed', publication=False, source_commit=selected['commit'], archive_sha256=selected['sha256']))


def download(args):
    import subprocess
    from release_ci import verify_download
    expected=dict(repository=os.environ['GITHUB_REPOSITORY'],run_id=os.environ['GITHUB_RUN_ID'],
        run_attempt=os.environ['GITHUB_RUN_ATTEMPT'],workflow_head=os.environ['WORKFLOW_HEAD_SHA'],artifact_id=args.artifact_id)
    require(args.artifact_id.isdigit(), 'invalid artifact ID')
    endpoint='repos/'+expected['repository']+'/actions/artifacts/'+args.artifact_id
    metadata=json.loads(subprocess.check_output(['gh','api',endpoint],text=True))
    run=json.loads(subprocess.check_output(['gh','api','repos/'+expected['repository']+'/actions/runs/'+expected['run_id']+'/attempts/'+expected['run_attempt']],text=True))
    archive=args.output.parent/('artifact-'+args.artifact_id+'.zip')
    with archive.open('xb') as output:
        subprocess.run(['gh','api',endpoint+'/zip'],stdout=output,check=True)
    digest=args.digest if args.digest.startswith('sha256:') else 'sha256:'+args.digest
    verify_download(archive,metadata,run,expected,digest,args.output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='operation', required=True)
    prep = sub.add_parser('prepare'); prep.add_argument('--commit', required=True); prep.add_argument('--version', required=True)
    platform_parser = sub.add_parser('platform')
    platform_parser.add_argument('--archive', type=Path, required=True)
    platform_parser.add_argument('--provider-prefix', type=Path, required=True)
    platform_parser.add_argument('--compiler', default='clang++'); platform_parser.add_argument('--cc', default='clang')
    platform_parser.add_argument('--client-python', required=True); platform_parser.add_argument('--node', required=True)
    platform_parser.add_argument('--jobs', type=int, default=2)
    platform_parser.add_argument('--pkg-config-dir', type=Path, action='append', default=[])
    platform_parser.add_argument('--toolchain-prefix', type=Path)
    fetch = sub.add_parser('download'); fetch.add_argument('--artifact-id', required=True); fetch.add_argument('--digest', required=True)
    acceptance = sub.add_parser('accept'); acceptance.add_argument('--manifest', type=Path, required=True)
    acceptance.add_argument('--platform-bundle', type=Path, action='append', default=[])
    acceptance.add_argument('--publish', action='store_true'); acceptance.add_argument('--artifact-id')
    for command in (prep, platform_parser, acceptance, fetch): command.add_argument('--output', type=Path, required=True)
    for command in (platform_parser, acceptance): command.add_argument('--selection', type=Path, required=True)
    for command in (prep, platform_parser): command.add_argument('--make', default='make')
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.operation == 'prepare': prepare(args)
    elif args.operation == 'platform':
        args.archive = args.archive.resolve(); args.provider_prefix = args.provider_prefix.resolve(); platform_proof(args)
    elif args.operation == 'download': download(args)
    else: accept(args)


if __name__ == '__main__':
    main()
