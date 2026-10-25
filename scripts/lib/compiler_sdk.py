"""One identity-owned Clang Darwin SDKSettings input; no resource allowlist."""
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess

from native_benchmark import artifact, require, verify_artifact

# PATH also selects wrappers/drivers. Other loader variables are intentionally
# outside this capability: the selected compiler/wrapper remains a trusted tool.
SELECTION_ENV = ('SDKROOT', 'DEVELOPER_DIR', 'TOOLCHAINS', 'PATH',
                 'MACOSX_DEPLOYMENT_TARGET', 'IPHONEOS_DEPLOYMENT_TARGET',
                 'TVOS_DEPLOYMENT_TARGET', 'WATCHOS_DEPLOYMENT_TARGET',
                 'DRIVERKIT_DEPLOYMENT_TARGET', 'XROS_DEPLOYMENT_TARGET',
                 'CCC_OVERRIDE_OPTIONS', 'CLANG_CONFIG_FILE_SYSTEM_DIR',
                 'CLANG_CONFIG_FILE_USER_DIR', 'CPATH', 'CPLUS_INCLUDE_PATH',
                 'C_INCLUDE_PATH', 'OBJC_INCLUDE_PATH', 'COMPILER_PATH',
                 'GCC_EXEC_PREFIX', 'CLANG_NO_DEFAULT_CONFIG')


def selection_environment(env):
    return {key: env.get(key) for key in SELECTION_ENV}


def _selected(query):
    require(query['exit'] == 0, 'failed SDK driver query')
    jobs = []
    for line in (query['stdout'] + '\n' + query['stderr']).splitlines():
        try:
            argv = shlex.split(line)
        except ValueError:
            continue
        if '-cc1' in argv:
            jobs.append(argv)
    require(len(jobs) == 1, 'ambiguous SDK driver query')
    job = jobs[0]
    require(job.count('-triple') == 1 and job.count('-isysroot') == 1, 'unsupported SDK driver query')
    triple = job[job.index('-triple') + 1]
    require(re.fullmatch(r'[^-]+-apple-macosx[0-9.]+', triple), 'non-macOS SDK driver query')
    sdk = Path(job[job.index('-isysroot') + 1])
    require(sdk.is_absolute(), 'relative SDK root')
    sdk = sdk.resolve()
    metadata = sdk / 'SDKSettings.json'
    require(metadata.is_file() and metadata.resolve() == metadata, 'missing or escaped SDK metadata')
    driver = Path(job[0])
    require(driver.is_absolute() and driver.is_file(), 'missing selected SDK driver')
    return sdk, metadata, driver.resolve(), triple


def _query_digest(query):
    return hashlib.sha256(json.dumps(query, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def capture(compiler, *, cwd=None):
    """Capture before compilation. Unsupported discovery gives no exception."""
    if platform.system() != 'Darwin':
        return None
    argv = shlex.split(compiler)
    # Indirect argument/config/VFS files cannot be pinned by this narrow model.
    indirect = ('@', '--config', '-config', '-ivfsoverlay', '-vfsoverlay',
                '-Xclang', '-Xpreprocessor', '-Wp,')
    if not argv or any(arg.startswith(indirect) for arg in argv):
        return None
    cwd = str(Path(cwd or Path.cwd()).resolve())
    query_argv = argv + ['-###', '-x', 'c++', '-c', os.devnull, '-o', os.devnull]
    environment = selection_environment(os.environ)
    try:
        process = subprocess.run(query_argv, cwd=cwd, env=dict(os.environ), capture_output=True,
                                 text=True, timeout=15)
        query = dict(argv=query_argv, cwd=cwd, exit=process.returncode,
                     stdout=process.stdout, stderr=process.stderr)
        require(len(process.stdout) + len(process.stderr) <= 1024 * 1024, 'SDK query output too large')
        sdk, metadata, driver, target = _selected(query)
        search = os.pathsep.join(str((Path(cwd) / entry).resolve()) for entry in os.get_exec_path())
        tool = (str((Path(cwd) / argv[0]).resolve()) if os.sep in argv[0]
                else shutil.which(argv[0], path=search))
        require(tool is not None, 'missing configured SDK compiler')
        inputs = {str(Path(tool).resolve())}
        inputs.update(str((Path(cwd) / arg).resolve()) for arg in argv[1:]
                      if (Path(cwd) / arg).is_file())
        data = metadata.read_bytes()
        require(len(data) <= 1024 * 1024, 'SDK metadata too large')
        return dict(version=1, compiler_argv=argv, environment=environment, query=query, query_sha256=_query_digest(query),
                    driver=artifact(driver), compiler_inputs={path: artifact(path) for path in sorted(inputs)},
                    sdk=str(sdk), target=target, metadata=artifact(metadata), metadata_hex=data.hex())
    except (OSError, ValueError, IndexError, subprocess.SubprocessError):
        return None


def context_key(context):
    return {key: value for key, value in context.items() if key not in ('query', 'query_sha256')}


def verify(context, compiler):
    """Requery independently; receipt paths/hashes alone never select an SDK."""
    require(context.get('version') == 1 and context['compiler_argv'] == shlex.split(compiler),
            'different SDK compiler context')
    require(context['environment'] == selection_environment(os.environ), 'changed SDK selection environment')
    query = context['query']
    require(context.get('query_sha256') == _query_digest(query), 'changed retained SDK query')
    require(query['argv'] == context['compiler_argv'] + ['-###', '-x', 'c++', '-c', os.devnull, '-o', os.devnull],
            'different SDK query command')
    sdk, metadata, driver, target = _selected(query)
    require((str(sdk), str(metadata), str(driver), target) ==
            (context['sdk'], context['metadata']['path'], context['driver']['path'], context['target']), 'changed SDK query selection')
    for record in [context['metadata'], context['driver'], *context['compiler_inputs'].values()]:
        verify_artifact(record)
    data = bytes.fromhex(context['metadata_hex'])
    require(data == metadata.read_bytes() and hashlib.sha256(data).hexdigest() == context['metadata']['sha256'],
            'changed retained SDK metadata')
    current = capture(compiler, cwd=query['cwd'])
    require(current is not None and context_key(current) == context_key(context), 'changed selected SDK context')
    return str(metadata)


def artifact_paths(context):
    return sorted({context['metadata']['path'], context['driver']['path'], *context['compiler_inputs']})


def validate_command(context, row):
    argv, prefix = row['argv'], context['compiler_argv']
    require(argv[:len(prefix)] == prefix, 'different SDK compile compiler')
    require(all(key in row.get('environment', {}) and row['environment'][key] == value
                for key, value in context['environment'].items()), 'different SDK compile environment')
    require(row.get('cwd') and Path(row['cwd']).is_absolute(), 'missing SDK compile working directory')
    current = capture(shlex.join(prefix), cwd=row['cwd'])
    require(current is not None and context_key(current) == context_key(context),
            'different SDK compile working directory context')
    # Admission is deliberately bounded to public compile_probe flags. Unknown
    # driver arguments, response/config/VFS/target options never inherit proof.
    tail, i = argv[len(prefix):], 0
    while i < len(tail):
        flag = tail[i]
        if flag in ('-MF', '-c', '-o', '-I', '-isystem'):
            require(i + 1 < len(tail) and not tail[i + 1].startswith('@'), 'unsupported SDK compile operand')
            i += 2
            continue
        allowed = (flag in ('-std=c++20', '-MMD', '-pthread', '-g', '-fno-omit-frame-pointer',
                            '-fno-sanitize-recover=all') or
                   re.fullmatch(r'-O[0123sgz]', flag) or
                   re.fullmatch(r'-W(?:all|extra|error(?:=return-type)?)', flag) or
                   re.fullmatch(r'-fsanitize=(?:address|undefined|thread)(?:,(?:address|undefined|thread))*', flag) or
                   re.fullmatch(r'-D[A-Za-z_][A-Za-z_0-9]*(?:=.*)?', flag) or
                   flag.startswith('-I') and len(flag) > 2)
        require(allowed, 'unsupported SDK compile context option: ' + flag)
        i += 1
