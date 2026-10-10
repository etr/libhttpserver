#!/usr/bin/env python3
"""Fail-closed audit of a real installed transitional v3 package.

Fixture tests exercise policy only. Real receipts retain inspector commands,
resolved graph edges, metadata, public declarations and all archive members.
"""
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess


class AuditError(RuntimeError):
    pass


COMMANDS = []


def run(command, retain_output=True, **kwargs):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=120, **kwargs)
    record = {'argv': command, 'returncode': result.returncode, 'stderr': result.stderr}
    if retain_output:
        record['stdout'] = result.stdout
    else:
        record.update(stdout_sha256=hashlib.sha256(result.stdout.encode()).hexdigest(), stdout_bytes=len(result.stdout.encode()))
    if 'input' in kwargs:
        record['stdin'] = kwargs['input']
    COMMANDS.append(record)
    if result.returncode:
        raise AuditError(f'command failed: {shlex.join(command)}: {result.stderr[-3000:]}')
    return result.stdout


def load_policy():
    return json.loads(Path(__file__).with_name('v3-installed-dependency-policy.json').read_text())


def family(path):
    name = Path(path).name.lower()
    frameworks = {'security': 'Security', 'corefoundation': 'CoreFoundation', 'coreservices': 'CoreServices'}
    if name in frameworks:
        return frameworks[name]
    if name.endswith('.exe'):
        name = name[:-4]
    match = re.match(r'lib(.+?)(?:\.[0-9]|\.dylib|\.so|\.a|-[0-9])', name)
    return match[1] if match else 'consumer' if name in ('consumer', 'v3_package_consumer', 'v3_native_linkage') else name


RUNTIME = ('libSystem.*', 'libc++.*', 'libc++abi.*', 'libstdc++.*', 'libgcc_s.*',
           'libc.so*', 'libm.so*', 'libpthread.*', 'libthr.*', 'libdl.*', 'librt.*',
           'libatomic.*', 'libresolv.*', 'libexecinfo.*', 'ld-linux*', 'ld-musl*', 'ld-elf*.so*')
BACKEND_TYPE = re.compile(r'\b(?:MHD_\w+|gnutls_\w+|SSL_CTX|SSL|X509|BIO|OSSL_\w+)\b')
LEGACY_SYMBOL = re.compile(r'^(MHD_|gnutls_|nettle_|hogweed_|wslay_|curl_|wolfSSL_|mbedtls_)')
TLS_SYMBOL = re.compile(r'^(SSL_|TLS_|DTLS_|OPENSSL_|OpenSSL_|CRYPTO_|EVP_|BIO_|ERR_|OSSL_|X509_|RAND_|BN_|PEM_|ASN1_)')


def runtime(path):
    if platform.system() == 'Windows':
        location = Path(path).resolve()
        system = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32'
        if location.is_relative_to(system.resolve()) and windows_system(Path(path).name):
            return True
        toolchain = os.environ.get('V3_PACKAGE_TOOLCHAIN')
        return bool(toolchain and location.is_relative_to(Path(toolchain).resolve()) and
                    any(fnmatch.fnmatch(location.name.lower(), pattern) for pattern in
                        ('libstdc++-*.dll', 'libgcc_s_*.dll', 'libwinpthread-*.dll')))
    trusted_roots = ('/libexec', '/usr/lib', '/lib', '/lib64', '/usr/lib64', '/usr/local/lib/gcc', '/opt/homebrew/Cellar/gcc', '/opt/homebrew/Cellar/llvm')
    return (any(fnmatch.fnmatch(Path(path).name, pattern) for pattern in RUNTIME)
            and any(Path(path).is_relative_to(Path(root)) for root in trusted_roots))


def parse_otool(output, artifact):
    lines = output.splitlines()
    if not lines or not lines[0].endswith(':'):
        raise AuditError('empty/malformed otool inspection')
    dependencies = []
    for line in lines[1:]:
        match = re.fullmatch(r'\s+(\S+) \(compatibility version [0-9.]+, current version [0-9.]+\)', line)
        if not match:
            raise AuditError('malformed otool dependency: ' + line)
        name = match[1]
        # otool -L includes a dylib's own LC_ID_DYLIB as the first entry.
        if (Path(name).name == Path(artifact).name or Path(name).resolve() == Path(artifact).resolve()) and not dependencies:
            continue
        dependencies.append(name)
    if not dependencies:
        raise AuditError('empty dependency inspection: ' + str(artifact))
    return dependencies


# macOS ships these libraries only in the sealed dyld shared cache. They are
# recorded as explicit platform leaves rather than treating /usr/lib as safe.
DYLD_LEAVES = {'/usr/lib/libSystem.B.dylib', '/usr/lib/libc++.1.dylib', '/usr/lib/libc++abi.dylib',
               '/usr/lib/libz.1.dylib', '/usr/lib/libiconv.2.dylib', '/usr/lib/libffi.dylib',
               '/System/Library/Frameworks/Security.framework/Versions/A/Security',
               '/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation',
               '/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices'}


WINDOWS_SYSTEM = {'kernel32.dll', 'kernelbase.dll', 'ntdll.dll', 'msvcrt.dll', 'ucrtbase.dll',
                  'ws2_32.dll', 'advapi32.dll', 'bcrypt.dll', 'bcryptprimitives.dll', 'crypt32.dll',
                  'secur32.dll', 'user32.dll', 'iphlpapi.dll', 'winmm.dll', 'ole32.dll', 'shell32.dll',
                  'userenv.dll', 'ncrypt.dll', 'gdi32.dll', 'shlwapi.dll', 'comdlg32.dll'}


def windows_system(name):
    name = name.lower()
    return name in WINDOWS_SYSTEM or bool(re.fullmatch(r'(?:api|ext)-ms-win-[a-z0-9-]+\.dll', name))


def parse_pe(output):
    if not re.search(r'file format pei?-(?:x86-64|i386)', output):
        raise AuditError('missing/malformed PE inspection')
    names = re.findall(r'^\s*DLL Name:\s*(\S+\.dll)\s*$', output, re.M | re.I)
    if not names or output.lower().count('dll name:') != len(names):
        raise AuditError('empty/malformed PE imports')
    return [name.lower() for name in names]


def resolve_pe(name):
    name = name.lower()
    if windows_system(name):
        return str(Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32' / name)
    roots = os.environ.get('V3_PACKAGE_DLL_DIRS', '').split(os.pathsep)
    for root in filter(None, roots):
        directory = Path(root)
        if directory.is_dir():
            matches = [path for path in directory.iterdir() if path.name.lower() == name and path.is_file()]
            if len(matches) == 1:
                return str(matches[0].resolve())
    raise AuditError('unresolved PE import: ' + name)


def package_artifacts(prefix):
    artifacts = [path for directory in (prefix / 'lib', prefix / 'bin') if directory.is_dir()
                 for path in directory.iterdir() if path.is_file() and path.name.lower().startswith('libhttpserver')]
    archives = [path for path in artifacts if path.name == 'libhttpserver.a']
    shared = [path for path in artifacts if path.name.lower().endswith(('.dll', '.dylib')) or '.so' in path.name]
    if not archives or not shared:
        raise AuditError('installed shared/static aggregate missing (import archives are not static libraries)')
    return sorted(shared), archives


def platform_inspector(path):
    if platform.system() == 'Windows':
        if runtime(path) and windows_system(Path(path).name):
            return []
        return [(name, resolve_pe(name)) for name in parse_pe(run(['objdump', '-p', path]))]
    if platform.system() == 'Darwin':
        names = parse_otool(run(['otool', '-L', path]), path)
        resolved = []
        rpaths = re.findall(r'path (\S+) \(offset', run(['otool', '-l', path]))
        for name in names:
            location = name.replace('@loader_path', str(Path(path).parent))
            if location.startswith('@rpath/'):
                candidates = [Path(r.replace('@loader_path', str(Path(path).parent))) / location[7:] for r in rpaths]
                location = next((str(p) for p in candidates if p.is_file()), location)
            resolved.append((name, location))
        return resolved
    if platform.system() in ('Linux', 'FreeBSD'):
        dynamic = run(['readelf', '-d', path])
        if 'Dynamic section' not in dynamic:
            raise AuditError('missing ELF dynamic section: ' + path)
        names = re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic)
        if not names:
            if runtime(path):
                return []
            raise AuditError('empty ELF dependency inspection')
        resolved = run(['ldd', path])
        if 'not found' in resolved:
            raise AuditError('unresolved ELF dependency: ' + resolved)
        mapping = dict(re.findall(r'\s*(\S+) => (/\S+) \(', resolved))
        mapping.update((Path(p).name, p) for p in re.findall(r'^\s*(/\S+) \(', resolved, re.M))
        for name in names:
            if name not in mapping:
                raise AuditError('missing resolution for ' + name)
        return [(name, mapping[name]) for name in names]
    raise AuditError('no installed-package inspector for this host; assign nonlocal checks to CI')


def dependency_graph(roots, inspector=platform_inspector):
    nodes, edges, visited = [], [], set()
    def visit(path):
        identity = str(Path(path).resolve())
        if identity in visited:
            return identity
        platform_leaf = (platform.system() == 'Darwin' and identity in DYLD_LEAVES) or (platform.system() == 'Windows' and runtime(identity) and windows_system(Path(identity).name))
        if not Path(identity).is_file() and not platform_leaf:
            raise AuditError('unresolved dependency: ' + path)
        visited.add(identity)
        cached = platform_leaf and not Path(identity).exists()
        nodes.append({'id': identity, 'provenance': 'OS/API-set platform leaf' if cached else 'platform inspector',
                      'sha256': None if cached else hashlib.sha256(Path(identity).read_bytes()).hexdigest()})
        dependencies = [] if cached else inspector(identity)
        if not isinstance(dependencies, list):
            raise AuditError('incomplete inspector result: ' + path)
        for name, location in dependencies:
            child = visit(location)
            edges.append({'parent': identity, 'name': name, 'child': child})
        return identity
    for root in roots:
        visit(root)
    return {'nodes': nodes, 'edges': edges, 'roots': [str(Path(p).resolve()) for p in roots]}


def graph_violations(graph, mode, tls, surface, policy, provider_prefix):
    violations = []
    identities = {node['id'] for node in graph['nodes']}
    if graph.get('roots'):
        reachable = set(graph['roots'])
        while True:
            children = {edge['child'] for edge in graph['edges'] if edge['parent'] in reachable}
            if children.issubset(reachable):
                break
            reachable.update(children)
        violations += ['missing dependency edge: ' + node for node in sorted(identities - reachable)]
    for edge in graph['edges']:
        parent, child = edge['parent'], edge['child']
        if child not in identities or parent not in identities:
            violations.append('missing dependency node: ' + str(edge))
            continue
        dep = family(child)
        owner = graph.get('consumer_roles', {}).get(parent, family(parent))
        if runtime(child):
            continue
        if dep == 'httpserver' and surface == 'aggregate' and child in identities:
            continue
        if dep in ('ssl', 'crypto'):
            if tls != 'yes' or not Path(child).is_relative_to(Path(provider_prefix).resolve()):
                violations.append('unselected native provider: ' + child)
            continue
        if surface == 'aggregate' and mode == 'pre-cutover' and dep in policy.get('legacy_edges', {}).get(owner, []):
            roots = list(filter(None, os.environ.get('V3_PACKAGE_PREREQUISITES', '').split(os.pathsep)))
            if roots and not any(Path(child).is_relative_to(Path(root).resolve()) for root in roots) and child not in DYLD_LEAVES:
                violations.append('unselected transitional dependency: ' + child)
            continue
        violations.append(f'undeclared dependency ({surface}/{mode}): {parent} -> {child}')
    return violations


def symbol_violations(output, surface, tls, mode):
    violations = []
    for line in output.splitlines():
        symbol = line.split()[-1] if line.split() else ''
        symbol = re.sub(r'^_*imp_+', '', symbol).lstrip('_')
        symbol = re.sub(r'@[0-9]+$', '', symbol)
        if LEGACY_SYMBOL.match(symbol) and (surface == 'native' or mode == 'strict' or symbol.startswith(('curl_', 'wolfSSL_', 'mbedtls_', 'wslay_'))):
            violations.append('legacy archive symbol: ' + symbol)
        if TLS_SYMBOL.match(symbol) and tls != 'yes':
            violations.append('TLS-off archive symbol: ' + symbol)
    return violations


def layout_violations(headers):
    return ['private installed header: ' + h for h in headers if '/detail/' in '/' + h or h.endswith('_impl.hpp')]


def header_violations(effective, includes, forbidden_roots=()):
    violations = ['backend declaration: ' + match.group() for match in BACKEND_TYPE.finditer(effective)]
    for include in includes:
        include = include.replace('\\', '/')
        if re.search(r'/(?:openssl/|gnutls/|microhttpd(?:_ws)?\.h|sys/(?:socket|uio)\.h|httpserver/detail/)', include) or include.endswith('_impl.hpp'):
            violations.append('backend/private include: ' + include)
        if any(Path(include).is_relative_to(Path(root).resolve()) for root in forbidden_roots):
            violations.append('source/build include contamination: ' + include)
    return violations


def consumer_flag_violations(flags, forbidden_roots):
    violations = []
    for flag in flags:
        if re.match(r'-D(?:HTTPSERVER_COMPILATION|_HTTPSERVER_HPP_INSIDE_|HAVE_\w*|NATIVE_V3_TLS)(?:=|$)', flag):
            violations.append('internal consumer macro: ' + flag)
        if any(str(Path(root).resolve()) in flag for root in forbidden_roots):
            violations.append('source/build consumer flag: ' + flag)
        if flag in ('-include', '-imacros'):
            violations.append('injected consumer header: ' + flag)
    return violations


def clean_environment():
    return {key: value for key, value in os.environ.items() if key not in (
        'CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'LIBRARY_PATH', 'CPPFLAGS', 'CXXFLAGS', 'LDFLAGS', 'LIBS',
        'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_SYSROOT_DIR', 'DYLD_LIBRARY_PATH', 'LD_LIBRARY_PATH')}


def audit_headers(prefix, source, build, compiler):
    include = prefix / 'include'
    files = sorted(str(p.relative_to(include)) for p in include.rglob('*') if p.is_file())
    if not files or not {'httpserver.hpp', 'httpserverpp'}.issubset(files):
        raise AuditError('incomplete installed header tree')
    violations = layout_violations(files)
    declarations, traces = {}, {}
    for name in files:
        if not (include / name).resolve().is_relative_to(include.resolve()):
            raise AuditError('installed header resolves outside prefix: ' + name)
        text = (include / name).read_text()
        gated = '_HTTPSERVER_HPP_INSIDE_' in text and name not in ('httpserver.hpp', 'httpserverpp')
        unit = '#include <' + ('httpserver.hpp' if gated else name) + '>\n'
        args = shlex.split(compiler) + ['-std=c++20', '-I' + str(include), '-x', 'c++', '-']
        effective = run(args + ['-E'], input=unit, env=clean_environment(), retain_output=False)
        run(args + ['-fsyntax-only'], input=unit, env=clean_environment())
        includes = re.findall(r'^# \d+ "([^"]+)"', effective, re.M)
        actual = [p for p in includes if not p.startswith('<')]
        if gated and (include / name).resolve() not in [Path(path).resolve() for path in actual]:
            raise AuditError('installed gated header not reachable through umbrella: ' + name)
        traces[name] = sorted(set(actual))
        violations += header_violations(effective, actual, (source, build))
        # Effective declarations from installed headers only, without varying prefix/line markers.
        current, public = False, []
        for line in effective.splitlines():
            marker = re.match(r'^# \d+ "([^"]+)"', line)
            if marker:
                current = not marker[1].startswith('<') and Path(marker[1]).resolve().is_relative_to(include.resolve())
            elif current and line.strip():
                public.append(line.strip())
        declarations[name] = hashlib.sha256('\n'.join(public).encode()).hexdigest()
    return {'files': files, 'declaration_sha256': declarations, 'include_traces': traces}, violations


def audit(prefix, build, consumer, tls, mode, provider_prefix, compiler='c++'):
    source = Path(__file__).resolve().parent.parent
    COMMANDS.clear()
    headers, violations = audit_headers(prefix, source, build, compiler)
    policy = load_policy()
    # Declarations are fixed in source policy; retain current build/package evidence.
    provenance = {}
    for name in ('configure.ac', 'src/Makefile.am', 'libhttpserver.pc.in'):
        provenance[name] = (source / name).read_text()
    for name in ('libhttpserver.pc', 'config.log', 'src/Makefile'):
        provenance['build/' + name] = (build / name).read_text()
    provenance['installed/libhttpserver.pc'] = (prefix / 'lib/pkgconfig/libhttpserver.pc').read_text()
    if mode == 'pre-cutover' and ('-lmicrohttpd' not in provenance['src/Makefile.am'] or
                                  'libmicrohttpd' not in provenance['installed/libhttpserver.pc']):
        raise AuditError('transitional declaration provenance missing')
    shared, archives = package_artifacts(prefix)
    consumers = [consumer]
    static_consumer = consumer.parent / ('static_consumer.exe' if platform.system() == 'Windows' else 'static_consumer')
    if static_consumer.is_file():
        consumers.append(static_consumer)
    aggregate = dependency_graph([str(p) for p in consumers + shared])
    violations += graph_violations(aggregate, mode, tls, 'aggregate', policy, provider_prefix)
    suffix = '.exe' if platform.system() == 'Windows' else ''
    native_bin = build / ('test/.libs/v3_native_linkage' + suffix)
    if not native_bin.exists():
        native_bin = build / ('test/v3_native_linkage' + suffix)
    native_archive = build / 'src/.libs/libhttpserver_v3core.a'
    native = dependency_graph([str(native_bin)])
    violations += graph_violations(native, mode, tls, 'native', policy, provider_prefix)
    symbols = {}
    for artifact, surface in [(p, 'aggregate') for p in archives] + [(native_archive, 'native')]:
        output = run(['nm', '-u', str(artifact)])
        if not output.strip():
            raise AuditError('empty archive inspection: ' + str(artifact))
        symbols[str(artifact)] = output
        violations += symbol_violations(output, surface, tls, mode)
    return {'version': 1, 'host': platform.platform(), 'tls_mode': tls, 'policy_mode': mode,
            'headers': headers, 'aggregate': aggregate, 'native': native, 'archive_symbols': symbols,
            'declarations': policy, 'provenance': provenance, 'commands': COMMANDS.copy(), 'violations': violations}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prefix', 'build', 'consumer', 'provider-prefix', 'receipt'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--tls', choices=('yes', 'no'), required=True)
    parser.add_argument('--mode', choices=('pre-cutover', 'strict'), default='pre-cutover')
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    try:
        receipt = audit(args.prefix.resolve(), args.build.resolve(), args.consumer.resolve(), args.tls,
                        args.mode, args.provider_prefix, args.compiler)
    except (AuditError, OSError, subprocess.TimeoutExpired) as error:
        receipt = {'violations': [str(error)], 'commands': COMMANDS}
    args.receipt.write_text(json.dumps(receipt, indent=2) + '\n')
    for violation in receipt['violations']:
        print(violation)
    return bool(receipt['violations'])


if __name__ == '__main__':
    raise SystemExit(main())
