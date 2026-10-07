# TASK-145 implementation recovery evidence

## Paused for cloud continuation on 2026-10-07

The user paused the serial batch and requested a WIP commit and remote push of
`task/TASK-145` and `v3`, followed by removal of the local task worktree.
TASK-145 remains **In Progress**. Groundwork implementation recovery was
interrupted; no final validation or completion receipt exists for this snapshot.

The independent hyper-h2 and Node HTTP/2 clients and 145 h2spec cases passed
locally during recovery. The final recovery reported TLS-on 24/24 and TLS-off
23/23 focused suites passing after correcting HPACK-versus-closed-stream error
precedence. Saved logs also show three ASan/UBSan targets and both bounded fuzz
targets (`hpack`, `http2_engine`) passing. These observations are partial local
evidence, not sealed acceptance of the paused commit. The last lint/install
attempt could not install `lizard` through the local proxy; finish that check in
the cloud environment.

Resume TASK-145 before starting TASK-146. Re-resolve primary/task paths and
recreate compatible Python (>=3.10), pinned client dependencies, Node, h2spec,
and OpenSSL build inputs in the cloud; the local build directories and provider
paths are disposable. The saved plan is `.groundwork-plans/TASK-145-plan.md`.
Finish implementation, run local tests and Groundwork validation, then merge to
`v3` and clean up. Nonlocal platform evidence remains assigned to CI/the v3 PR.

## Dependency failure resolved

Selected worktree: `/Users/etr/progs/libhttpserver/.worktrees/TASK-145`.
Branch: `task/TASK-145`; implementation-entry revision:
`d0067349999b4337767d4d44b05031514c1f0862`.
Task card and index remain **In Progress**. This recovery prepares the failed
implement phase for retry; it does not establish task acceptance.

The original report of hyper-h2 4.4.1 release unavailability was incorrect.
On 2026-10-07, the original command was reproduced:

```sh
/private/tmp/task145-client-env/bin/python -m pip install h2==4.4.1
```

It exits 1 with `No matching distribution found for h2==4.4.1` under
**Python 3.9.6**. hyper-h2 4.4.1 requires **Python >=3.10**, confirmed by
installed distribution metadata and the upstream
[release notes](https://python-hyper.org/projects/hyper-h2/en/stable/release-notes.html).
The [PyPI release](https://pypi.org/project/h2/4.4.1/) exists.
The pin is preserved; no substitute client version is used.

## Repaired local setup

Created an isolated, ignored environment inside the selected worktree using
the installed **Python 3.12.13** interpreter:

```sh
python3.12 -m venv build/task145-client-env
build/task145-client-env/bin/python -m pip install --no-cache-dir h2==4.4.1
build/task145-client-env/bin/python -m pip install --no-cache-dir -r test/integ/http2-client-requirements.txt
build/task145-client-env/bin/python -m pip check
build/task145-client-env/bin/python build/task145-client-dependency-smoke.py
```

Installation and both verification commands exit 0. Installed versions:
`h2==4.4.1`, `hpack==4.2.0`, `hyperframe==6.1.0`; pip 25.0.1 reports
`No broken requirements found`. The retained requirements file pins all three
packages; it introduces no production dependency.

The retained build-local smoke exchanges frames between two in-memory hyper-h2
connections and checks SETTINGS negotiation, two multiplexed GET streams,
Extended CONNECT headers, DATA delivery, an 8-byte stream window stall and
WINDOW_UPDATE resumption, PING acknowledgment and CANCEL reset. It passes.
This checks installed client APIs only; it is not independent-client evidence
against libhttpserver or a TLS/WebSocket integration receipt.

The local `.groundwork-plans/TASK-145-plan.md` now requires a compatible Python
interpreter, points to this environment and requirements file, and retains
hyper-h2 **4.4.1**. For the implement retry, pass:

```text
--python /Users/etr/progs/libhttpserver/.worktrees/TASK-145/build/task145-client-env/bin/python
```

Do not reuse `/private/tmp/task145-client-env`, which uses Python 3.9.6.
Node remains **24.15.0**. The prior h2spec download observation was not rerun;
h2spec installation/execution remains implementation work.

## Remaining implement scope

Native implementation, builds, RFC 9113 conformance, real client matrices,
h2spec, fuzzing, sanitizers and lint gates remain pending the normal implement
retry. BSD, Windows and other nonlocal checks remain CI/v3-PR owned. No task
completion, validation/finalization phase, staging, commit, merge, push,
deployment, worktree removal or memory update was performed in recovery.
