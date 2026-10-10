#!/usr/bin/env python3
"""Build checksummed shared provider and transitional MHD for package CI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import urllib.request

SOURCES = {
    'openssl-3.5.9': ('https://www.openssl.org/source/openssl-3.5.9.tar.gz',
                      '603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a'),
    'libmicrohttpd-1.0.3': ('https://s3.amazonaws.com/libhttpserver/libmicrohttpd_releases/libmicrohttpd-1.0.3.tar.gz',
                          '7816b57aae199cf5c3645e8770e1be5f0a4dfafbcb24b3772173dc4ee634126a'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--make', default='make')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    receipt = dict(platform=platform.platform(), sources={}, commands=[])
    def command(argv, cwd, env):
        log = root / ('prepare-%02d.log' % len(receipt['commands']))
        row = dict(argv=argv, cwd=str(cwd), log=str(log))
        receipt['commands'].append(row)
        with log.open('wb') as output:
            result = subprocess.run(argv, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT, timeout=1800)
        row['exit'] = result.returncode
        if result.returncode:
            raise RuntimeError('prerequisite command failed: ' + str(argv))
    try:
        for name, (url, expected) in SOURCES.items():
            archive = root / (name + '.tar.gz')
            urllib.request.urlretrieve(url, archive)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            receipt['sources'][name] = dict(url=url, expected_sha256=expected, actual_sha256=digest)
            if digest != expected:
                raise RuntimeError('source checksum mismatch: ' + name)
            with tarfile.open(archive) as tar:
                # These pinned archives have one named root; reject path/symlink escapes.
                for member in tar.getmembers():
                    path = Path(member.name)
                    if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0] != name:
                        raise RuntimeError('unsafe source archive path')
                    if member.issym() or member.islnk():
                        if Path(member.linkname).is_absolute() or '..' in Path(member.linkname).parts:
                            raise RuntimeError('unsafe source archive link')
                tar.extractall(root)
        env = dict(os.environ)
        provider, legacy = root / 'provider', root / 'legacy'
        configure = ['perl', 'Configure', 'shared', 'no-tests', '--prefix=' + provider.as_posix(), '--libdir=lib']
        if platform.system() == 'Windows':
            configure += ['mingw64']
        command(configure, root / 'openssl-3.5.9', env)
        command([args.make, '-j' + str(args.jobs)], root / 'openssl-3.5.9', env)
        command([args.make, 'install_sw'], root / 'openssl-3.5.9', env)
        command(['bash', './configure', '--prefix=' + legacy.as_posix(), '--enable-shared', '--enable-static',
                 '--disable-examples'], root / 'libmicrohttpd-1.0.3', env)
        command([args.make, '-j' + str(args.jobs)], root / 'libmicrohttpd-1.0.3', env)
        command([args.make, 'install'], root / 'libmicrohttpd-1.0.3', env)
        receipt.update(status='passed', provider_prefix=str(provider), prerequisite_prefix=str(legacy))
    except Exception as error:
        receipt.update(status='failed', error=str(error))
        raise
    finally:
        (root / 'preparation.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
