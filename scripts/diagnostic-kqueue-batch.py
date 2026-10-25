#!/usr/bin/env python3
"""One nonresumable native capture; never merge this diagnostic into v3."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import time
import uuid

MAX_COMPARISONS = 64
PROCESS_SECONDS = 20
BATCH_SECONDS = 300
CASE = 'accept_read_write_cancel_release_close_match_poll_trace'


def save(path, receipt):
    """Persist the launch debit before starting a child, including directory metadata."""
    temporary = path.with_suffix('.tmp')
    with temporary.open('w') as output:
        json.dump(receipt, output, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)
    directory = os.open(path.parent, os.O_RDONLY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def trace_receipt(output, token):
    lines = [line.removeprefix('KQUEUE_DIAGNOSTIC ') for line in output.splitlines()
             if line.startswith('KQUEUE_DIAGNOSTIC ')]
    if len(lines) != 1:
        raise ValueError('expected exactly one comparison witness')
    trace = json.loads(lines[0])
    if trace.get('token') != token:
        raise ValueError('comparison token mismatch')
    for name in ('poll', 'kqueue'):
        vector = trace.get(name)
        if not isinstance(vector, list) or len(vector) != 17 or not all(isinstance(s, str) for s in vector):
            raise ValueError('incomplete original 17-entry ' + name + ' vector')
    differences = [i for i, (left, right) in enumerate(zip(trace['poll'], trace['kqueue'])) if left != right]
    if trace.get('differences') != differences:
        raise ValueError('difference indices do not match complete vectors')
    return trace


def valid_summary(output, mode):
    expected = 48 if mode == 'full' else 1
    checks = re.findall(r'^(\d+) checks$', output, re.M)
    successes = re.findall(r'^-> (\d+) successes$', output, re.M)
    running = re.findall(r'^Running test \(\d+\): (.+)$', output, re.M)
    return (re.findall(r'^(\d+) tests executed$', output, re.M) == [str(expected)]
            and re.findall(r'^-> (\d+) failures$', output, re.M) == ['0']
            and re.findall(r'^-> (\d+) skipped$', output, re.M) == ['0']
            and len(running) == expected and running.count(CASE) == 1
            and len(checks) == 1 and checks == successes
            and int(checks[0]) >= 57 and (mode == 'full' or checks == ['57']))


def run_batch(argv, evidence, cwd, env, *, process_seconds=PROCESS_SECONDS, batch_seconds=BATCH_SECONDS):
    # Smaller bounds exist solely for offline fake-child timeout contracts.
    if not 0 < process_seconds <= PROCESS_SECONDS or not 0 <= batch_seconds <= BATCH_SECONDS:
        raise ValueError('time budgets may only be reduced')
    evidence = Path(evidence).resolve()
    try:
        evidence.mkdir()
    except FileExistsError:
        print('REFUSED: existing batch directory; never resume, reset, or retry', flush=True)
        return 125
    started = time.monotonic()
    receipt = dict(status='incomplete', cause='UNKNOWN', release_acceptance=False,
                   attempts=0, comparisons=0, maximum_comparisons=MAX_COMPARISONS,
                   process_seconds=process_seconds, batch_seconds=batch_seconds,
                   platform=platform.platform(), euid=os.geteuid(), cwd=str(cwd),
                   argv=[str(arg) for arg in argv], processes=[])
    path = evidence / 'receipt.json'

    def finish(status, code):
        receipt.update(status=status, driver_exit=code, elapsed_seconds=time.monotonic() - started)
        save(path, receipt)
        print(json.dumps(dict(status=status, attempts=receipt['attempts'], comparisons=receipt['comparisons'],
                              cause='UNKNOWN', release_acceptance=False)), flush=True)
        return code

    save(path, receipt)
    for index in range(MAX_COMPARISONS):
        remaining = batch_seconds - (time.monotonic() - started)
        if remaining <= 0:
            return finish('batch-timeout', 124)
        mode = 'full' if index == 0 else 'focused'
        token = uuid.uuid4().hex
        child_env = dict(env, LHS_KQUEUE_DIAGNOSTIC_MODE=mode, LHS_KQUEUE_DIAGNOSTIC_TOKEN=token)
        row = dict(attempt=index + 1, mode=mode, token=token, exit=None, timed_out=False,
                   timeout_seconds=min(process_seconds, remaining),
                   environment={key: child_env.get(key) for key in
                                ('PATH', 'SDKROOT', 'CXX', 'CC', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS',
                                 'V3_PACKAGE_TOOLCHAIN', 'V3_TLS_CFLAGS', 'V3_TLS_LIBS',
                                 'DYLD_LIBRARY_PATH', 'LD_LIBRARY_PATH', 'V3_PACKAGE_DLL_DIRS',
                                 'LHS_KQUEUE_DIAGNOSTIC_MODE', 'LHS_KQUEUE_DIAGNOSTIC_TOKEN')})
        receipt['attempts'] += 1  # Conservative launch debit, including crashes before comparison.
        receipt['processes'].append(row)
        save(path, receipt)
        process_started = time.monotonic()
        stdout_path = evidence / ('attempt-%02d.stdout' % (index + 1))
        stderr_path = evidence / ('attempt-%02d.stderr' % (index + 1))
        row.update(stdout=str(stdout_path), stderr=str(stderr_path))
        try:
            with stdout_path.open('wb') as stdout, stderr_path.open('wb') as stderr:
                process = subprocess.Popen(argv, cwd=cwd, env=child_env, stdout=stdout,
                                           stderr=stderr, start_new_session=True)
                row['pid'] = process.pid
                try:
                    row['exit'] = process.wait(timeout=row['timeout_seconds'])
                except subprocess.TimeoutExpired:
                    row['timed_out'] = True
                    # No graceful extra execution window beyond the approved per-process bound.
                    os.killpg(process.pid, signal.SIGKILL)
                    row['exit'] = process.wait()
        except OSError as error:
            row['launch_error'] = str(error)
            return finish('launch-failed', 125)
        row['elapsed_seconds'] = time.monotonic() - process_started
        for name, log in (('stdout', stdout_path), ('stderr', stderr_path)):
            row[name + '_sha256'] = hashlib.sha256(log.read_bytes()).hexdigest()
        output = stdout_path.read_text(errors='replace')
        try:
            row['trace'] = trace_receipt(output, token)
            receipt['comparisons'] += 1
        except (ValueError, TypeError, AttributeError) as error:
            row['receipt_error'] = str(error)
        save(path, receipt)
        if row['timed_out']:
            return finish('process-timeout', 124)
        if row['exit']:
            code = row['exit'] if row['exit'] > 0 else 128 - row['exit']
            return finish('child-failed', code)
        if 'receipt_error' in row:
            return finish('invalid-receipt', 125)
        if row['trace']['differences']:
            return finish('mismatch', 1)
        if not valid_summary(output, mode):
            row['receipt_error'] = 'original runner summary is incomplete or failed'
            return finish('invalid-receipt', 125)
    return finish('nonreproduction', 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--cwd', type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != 'Darwin' or os.geteuid() != 0:
        parser.error('requires native Darwin and original stage EUID 0; no local C++ probes')
    return run_batch([str(args.program.resolve())], args.evidence, args.cwd.resolve(), os.environ)


if __name__ == '__main__':
    raise SystemExit(main())
