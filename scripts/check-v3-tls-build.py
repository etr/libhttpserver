#!/usr/bin/env python3
"""Exercise native provider discovery with real headers and bounded bad fixtures.

V3_TLS_TEST_PREFIX must identify a genuine, installed stable 3.5 provider.
CPPFLAGS/LDFLAGS are for the transitional legacy prerequisites only.
"""
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

source = Path(__file__).resolve().parent.parent
prefix = Path(os.environ['V3_TLS_TEST_PREFIX']).resolve()
flags = '-I' + str(prefix / 'include')
libs = '-L' + str(prefix / 'lib') + ' -lssl -lcrypto'

with tempfile.TemporaryDirectory(prefix='v3-tls-matrix-') as temporary:
    root = Path(temporary)

    def configure(name, mode, cflags, ldlibs, expected=None, extra=None):
        build = root / name
        build.mkdir()
        env = dict(os.environ, V3_TLS_CFLAGS=cflags, V3_TLS_LIBS=ldlibs)
        if cflags is None:
            env.pop('V3_TLS_CFLAGS', None)
        if ldlibs is None:
            env.pop('V3_TLS_LIBS', None)
        env.update(extra or {})
        result = subprocess.run([str(source / 'configure'), mode,
                                 '--disable-examples'], cwd=build, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True)
        (build / 'output.log').write_text(result.stdout)
        if expected is None:
            assert result.returncode == 0, name + ': ' + result.stdout[-3000:]
            assert 'unrecognized options' not in result.stdout, name + ': TLS mode ignored'
        else:
            assert result.returncode != 0, name + ': unsuitable provider accepted'
            assert expected in result.stdout, name + ': wrong failure: ' + result.stdout[-3000:]
        print('PASS:', name, flush=True)
        return build

    # These paths are intentionally unusable; off must never inspect them.
    configure('off-no-provider', '--disable-v3-tls', '-I/no/provider',
              '-L/no/provider -lmissing', extra={'PKG_CONFIG': '/usr/bin/false'})
    configure('on-no-provider', '--enable-v3-tls', None, None, 'OpenSSL 3.5',
              {'PKG_CONFIG': '/usr/bin/false'})
    configure('invalid-mode', '--enable-v3-tls=maybe', flags, libs,
              '--enable-v3-tls accepts only yes or no')

    def headers(name, replacements):
        include = root / (name + '-include')
        shutil.copytree(prefix / 'include', include)
        version = include / 'openssl/opensslv.h'
        text = version.read_text()
        for macro, value in replacements.items():
            text = re.sub(r'(#\s*define\s+' + macro + r'\s+)[^\n]+',
                          lambda match: match[1] + value, text)
        version.write_text(text)
        return '-I' + str(include)

    for name, macros in [
            ('libressl', {'LIBRESSL_VERSION_NUMBER': '0x3030600fL'}),
            ('old-line', {'OPENSSL_VERSION_MINOR': '4'}),
            ('old-patch', {'OPENSSL_VERSION_PATCH': '8'}),
            ('prerelease', {'OPENSSL_VERSION_PRE_RELEASE': '"-beta1"'}),
            ('new-line', {'OPENSSL_VERSION_MINOR': '6'}),
            ('new-major', {'OPENSSL_VERSION_MAJOR': '4'})]:
        selected = headers(name, macros)
        if name == 'libressl':
            path = Path(selected[2:]) / 'openssl/opensslv.h'
            path.write_text(path.read_text() + '\n#define LIBRESSL_VERSION_NUMBER 0x3030600fL\n')
        configure(name, '--enable-v3-tls', selected, libs, 'stable OpenSSL 3.5.9')
        if name == 'new-line':
            configure('off-unsuitable-provider', '--disable-v3-tls', selected, libs)

    configure('missing-library', '--enable-v3-tls', flags,
              '-L/no/provider -lmissing', 'QUIC TLS callback API')
    # Genuine headers paired with an archive lacking callback symbols.
    stub = root / 'stub.c'
    stub.write_text('int task129_no_callbacks(void) { return 0; }\n')
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-c', str(stub), '-o', str(root / 'stub.o')], check=True)
    subprocess.run(['ar', 'rcs', str(root / 'stub.a'), str(root / 'stub.o')], check=True)
    configure('missing-callbacks', '--enable-v3-tls', flags,
              str(root / 'stub.a'), 'QUIC TLS callback API')
    selected = headers('missing-callback-id', {})
    dispatch = Path(selected[2:]) / 'openssl/core_dispatch.h'
    dispatch.write_text(dispatch.read_text().replace(
        '#define OSSL_FUNC_SSL_QUIC_TLS_ALERT 2006',
        '#define TASK129_MISSING_ALERT_ID 2006'))
    configure('missing-callback-id', '--enable-v3-tls', selected, libs,
              'QUIC TLS callback API')
    configure('mismatched-runtime', '--enable-v3-tls',
              headers('runtime', {'OPENSSL_VERSION_PATCH': '10'}), libs,
              'runtime does not match selected headers')
    stub.write_text('const char *OPENSSL_version_pre_release(void) { return "-beta1"; }\n')
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-c', str(stub), '-o', str(root / 'prerelease.o')], check=True)
    subprocess.run(['ar', 'rcs', str(root / 'prerelease.a'), str(root / 'prerelease.o')], check=True)
    configure('prerelease-runtime', '--enable-v3-tls', flags,
              str(root / 'prerelease.a') + ' ' + libs,
              'runtime does not match selected headers')
    configure('on-genuine-provider', '--enable-v3-tls', flags, libs)
    configure('on-isolated-pkg-config', '--enable-v3-tls', None, None,
              extra={'PKG_CONFIG_LIBDIR': str(prefix / 'lib/pkgconfig'),
                     'PKG_CONFIG_PATH': ''})
print('Native TLS configure matrix PASS')
