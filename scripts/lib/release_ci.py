"""Content-bound authorization after the fixed release readiness decision."""
from datetime import datetime
import hashlib
import json
import os
import platform
import re
from pathlib import Path, PurePosixPath, PureWindowsPath
import shutil
import stat
import subprocess
import zipfile


VERSION = r'[0-9]+\.[0-9]+\.[0-9]+(?:-(?:rc|alpha|beta)[0-9]+)?'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical_digest(document):
    return hashlib.sha256(json.dumps(document, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def validate_binding(binding):
    require(re.fullmatch(VERSION, binding['tag']), 'invalid version tag')
    require(re.fullmatch(r'[0-9a-f]{40}', binding['source_commit']), 'invalid source commit')
    require(binding['event'] in ('push', 'workflow_dispatch'), 'untrusted publication event')
    require(binding['producer_ids'] == ['FreeBSD', 'Darwin', 'Windows'], 'required producer identities missing')
    require(re.fullmatch(r'[0-9a-f]{40}', binding['workflow_source']), 'invalid workflow source')
    require(set(binding['producer_artifacts']) == {'freebsd','darwin','windows'}, 'missing producer artifact identity')
    for artifact in binding['producer_artifacts'].values():
        require(re.fullmatch(r'[1-9][0-9]*', artifact['id']) and re.fullmatch(r'[0-9a-f]{64}', artifact['digest']), 'invalid producer artifact identity')
    for key in ('run_id', 'run_attempt', 'artifact_id'):
        require(re.fullmatch(r'[1-9][0-9]*', binding[key]), 'invalid run/artifact identity')
    require(re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', binding['repository']), 'invalid repository')


def authorize(gate, manifest, binding):
    validate_binding(binding)
    require(manifest['release_artifact']['source'].get('working_diff_sha256') == hashlib.sha256(b'').hexdigest(), 'publication source differs from committed checkout')
    gate.check_tag_ready(manifest)
    require(manifest['release_artifact']['source']['head'] == binding['source_commit'], 'wrong selected source commit')
    require(manifest['release_artifact']['archive']['sha256'] == binding['archive_sha256'], 'wrong selected archive')
    return dict(schema_version=1, binding=json.loads(json.dumps(binding)), manifest_sha256=canonical_digest(manifest))


def selected_archive(checkout, source, archive, expected_sha256, extraction):
    import release_artifact as identity
    from release_checks import extract_archive, verify_snapshot_packaging
    require(identity.sha256(archive) == expected_sha256, 'selected archive digest mismatch')
    for name, digest in source['files_sha256'].items():
        require(identity.sha256(checkout / name) == digest, 'selected source snapshot changed')
    extracted = extract_archive(archive, extraction)
    packaged = identity.source_files(extracted)
    transformations = verify_snapshot_packaging(checkout, source['files_sha256'], extracted)
    tooling = {}
    candidates = [checkout / '.gitignore'] + list((checkout / 'specs').rglob('*.md')) + list((checkout / 'docs').rglob('*.md'))
    for path in candidates:
        if not path.is_file(): continue
        name = path.relative_to(checkout).as_posix()
        destination = extracted / name
        if not destination.exists():
            require(source['files_sha256'].get(name) == identity.sha256(path), 'tooling not from selected source')
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(path.read_bytes())
            tooling[name] = identity.sha256(destination)
    return dict(schema_version=1, source_root=str(extracted), source=source,
                archive=dict(path=str(archive.resolve()), sha256=expected_sha256, bytes=archive.stat().st_size),
                files_sha256=identity.source_files(extracted), tooling_bundle=tooling,
                packaged_files_sha256=packaged, configured_transformations=transformations)


def validate_request(root, commit, version):
    require(re.fullmatch(VERSION, version), 'invalid source version')
    require(re.fullmatch(r'[0-9a-f]{40}', commit), 'invalid selected commit')
    try:
        actual = subprocess.check_output(['git', 'rev-parse', '--verify', 'HEAD^{commit}'], cwd=root, text=True, stderr=subprocess.PIPE).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        diagnostic = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        detail = ': ' + diagnostic[-2048:].strip() if diagnostic else ''
        raise ValueError('selected request requires executable Git and a checkout' + detail) from error
    require(actual == commit, 'selected commit differs from checkout')
    status = subprocess.check_output(['git', 'status', '--porcelain=v1', '--untracked-files=all'], cwd=root, text=True)
    require(not status, 'selected checkout has modified/untracked source inputs')
    bash = 'bash'
    if platform.system() == 'Windows':
        # CreateProcess searches System32 before PATH for an unqualified name.
        bash = shutil.which('bash', path=os.environ.get('PATH', ''))
        require(bash, 'bash is unavailable on the selected PATH')
        bash = str(Path(bash).resolve())
    subprocess.run([bash, str(root / 'scripts/validate-version.sh'), version], cwd=root, check=True)


def verify_download(archive, metadata, run, expected, expected_digest, destination):
    require(str(metadata['id']) == expected['artifact_id'] and metadata['expired'] is False, 'wrong/expired artifact ID')
    require(str(metadata['workflow_run']['id']) == expected['run_id'] and metadata['workflow_run']['head_sha'] == expected['workflow_head'], 'artifact from another workflow source/run')
    require(str(run['id']) == expected['run_id'] and str(run['run_attempt']) == expected['run_attempt']
            and run['repository']['full_name'] == expected['repository'] and run['head_sha'] == expected['workflow_head'], 'wrong repository/source/run attempt')
    require(datetime.fromisoformat(metadata['created_at']) >= datetime.fromisoformat(run['run_started_at']), 'artifact belongs to an earlier run attempt')
    from release_artifact import sha256
    digest = 'sha256:' + sha256(archive)
    require(digest == expected_digest == metadata['digest'], 'downloaded artifact digest mismatch')
    require(not destination.exists(), 'artifact destination already exists')
    with zipfile.ZipFile(archive) as stream:
        members = stream.infolist()
        require(len(members) < 20004 and sum(m.file_size for m in members) < 9 * 1024**3, 'artifact extraction bounds exceeded')
        names = set()
        for member in members:
            name = PurePosixPath(member.filename)
            mode = member.external_attr >> 16
            require(not name.is_absolute() and not PureWindowsPath(member.filename).drive and '..' not in name.parts and '\\' not in member.filename
                    and member.filename not in names and not stat.S_ISLNK(mode), 'unsafe/duplicate artifact member')
            names.add(member.filename)
        destination.mkdir(parents=True)
        stream.extractall(destination)
