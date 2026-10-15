#!/usr/bin/env python3
"""Verified archive provenance for existing release runners; Git remains default."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha256(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def source_files(root):
    return {p.relative_to(root).as_posix(): sha256(p) for p in sorted(root.rglob('*'))
            if p.is_file() and '__pycache__' not in p.parts}


def verified(root, identity_path=None):
    path = Path(identity_path or os.environ['V3_RELEASE_IDENTITY'])
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('release identity exceeds bound')
    identity = json.loads(path.read_text())
    if identity.get('schema_version') != 1 or Path(identity['source_root']).resolve() != root.resolve():
        raise ValueError('release identity source root mismatch')
    archive = identity['archive']
    if sha256(archive['path']) != archive['sha256']:
        raise ValueError('release archive identity changed')
    files = identity['files_sha256']
    if not files or source_files(root) != files:
        raise ValueError('extracted release source changed or contains unrecorded files')
    for name, digest in identity.get('tooling_bundle', {}).items():
        if files.get(name) != digest:
            raise ValueError('release tooling bundle changed: ' + name)
    return identity


def source_identity(root):
    root = Path(root).resolve()
    if os.environ.get('V3_RELEASE_IDENTITY'):
        identity = verified(root)
        return dict(identity, **identity['source'])
    names = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=root).decode().split('\0')
    hashes = {name: sha256(root / name) for name in sorted(set(names)) if name and (root / name).is_file()}
    return {'head': revision(root), 'files_sha256': hashes,
            'snapshot_sha256': hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest(),
            'working_diff_sha256': hashlib.sha256(working_diff(root)).hexdigest()}


def revision(root):
    if os.environ.get('V3_RELEASE_IDENTITY'):
        return verified(Path(root))['source']['head']
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()


def working_diff(root):
    if os.environ.get('V3_RELEASE_IDENTITY'):
        return json.dumps(verified(Path(root)), sort_keys=True).encode()
    return subprocess.check_output(['git', 'diff', '--binary', 'HEAD'], cwd=root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(source_identity(args.root), sort_keys=True))
