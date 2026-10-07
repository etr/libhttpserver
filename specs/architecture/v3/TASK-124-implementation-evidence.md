# TASK-124 implementation evidence

Implementation started from merged `v3` at
`99060cf750acfbfcd3198474edc3fd700915b207`, in the existing registered
`task/TASK-124` worktree. No branch/worktree creation, staging, commits, merge,
remote workflow, or platform provisioning was performed by the executor.

The public port is an abstract, library-owned `readiness_driver` with owned
snapshots and standard-only registration/event values. The normative
[external-loop contract](../../../docs/external-loop-contract.md) defines
registration generations, handle borrowing, wake publication races, clock and
deadline rules, failed-dispatch posture, and threading/lifetime constraints.
TASK-125 still owns the concrete adapter, access path, configuration enforcement,
and actual runtime behavior. A test fixture's successful dispatch is not runtime
proof for the library.

## TDD and local verification

Before adding the header, both the genuine consumer and value-test translation
units failed because `httpserver/server/readiness.hpp` did not exist. After the
header was added, direct C++20 builds with `-Wall -Wextra -Werror -pedantic`
passed; the consumer ran successfully and value tests passed 4 tests / 30 checks,
with zero failures or skips. The inherited standalone executor consumer also
compiled and ran before implementation.

Local receipts are retained in `_build/task-124-receipts/`; the single fresh
configured VPATH build is `_build/task-124/`. Configuration used:

```sh
../../configure --disable-examples --enable-doxygen-doc CXX=clang++ CXXFLAGS='-O0 -g0' CPPFLAGS='-I/opt/homebrew/include' LDFLAGS='-L/opt/homebrew/lib'
```

Compilation and tests were serial. Optimization/debug data were disabled to
bound disk/memory use. The initial sandbox build denied `nice`'s `setpriority`
and ran at its inherited priority. The first full test run was interrupted by
the sandbox's localhost-network restriction; no test process remained. The
authorized full suite was then rerun outside that sandbox with `nice -n15`.
The interrupted log is retained as `check-sandbox-interrupted.log`; the final
successful run is `check.log`. No sanitizer run was added for this
declaration/value-only change.

Final local gates:

- Fresh configured `make -j1` build: PASS.
- `nice -n15 make -C _build/task-124 -j1 check`: exit 0; **221/221** registered
  tests passed, zero skips, expected failures, unexpected passes, failures,
  or errors. This includes the genuine consumer, the four public value tests
  (30 checks), and `io_operation`, `io_connection_owner`, `fake_io_backend`,
  and `io_backend_contract` with their real inherited library linkage.
- `check-local` ran as part of that command: direct source-header compilation,
  native linkage audit (A1 and A2), documentation and existing lint gates,
  fresh Doxygen with zero substantive warnings, shared staged
  `check-install-layout` including the installed readiness consumer, and
  `check-hygiene` all passed.
- A retained header-only install made with `install-nobase_includeHEADERS`
  is under `installed-headers/` in the receipts directory. The same fixture
  compiled with only its staged public include path, exit 0, no diagnostics.
  Both transitive project headers are byte-identical to final source;
  `installed-consumer-receipt.json` retains the command, status and hashes.

The changed C++ files pass cpplint, `scripts/check-file-size.sh` passes, and
`git diff --check` passes. Narrow include-order annotations keep the consumer's
public header first, verifying independent inclusion.

## Native family compile ledger

Canonical fixture SHA-256:
`c173735d654ea9ef8c95c4c186f0be9575f5c81d327d6035cdd8b3885ccc24ab`.
Transitive project header SHA-256 values:

| Input | SHA-256 |
|---|---|
| `src/httpserver/server/readiness.hpp` | `a17a603c8d9f9f4d8afab021c1f7a504b904d7b662697861a26cce32fea86016` |
| `src/httpserver/http/outcome.hpp` | `33f43a7d65a893e58e84d3344aed2fd0cdaeb3aeace798f906300688e09baa2e` |

| OS family | Native fixture result | Receipt / outstanding dependency |
|---|---|---|
| Linux | PASS, exit 0, no compiler diagnostics | `linux-consumer-receipt.json`, `linux-consumer.log` |
| macOS | PASS, exit 0, no compiler diagnostics | `macos-consumer-receipt.json`, `macos-consumer.log` |
| BSD | Unverified; not executed locally | CI/v3 PR verification; not a local merge gate |
| Windows | Unverified; not executed locally | CI/v3 PR verification; not a local merge gate |

The actual macOS host is macOS 26.3.1 (a), build `25D771280a`, Darwin 25.3.0,
arm64. Toolchain: Apple clang 21.0.0 (`clang-2100.1.1.101`), target
`arm64-apple-darwin25.3.0`. From the worktree root, the executed command was:

```sh
c++ -std=c++20 -I src -c test/headers/consumer_v3_readiness.cpp -o _build/task-124-receipts/consumer_v3_readiness.o
```

The structured receipt retains the exact command, compiler/platform identity,
exit status, empty compiler output, and all three hashes. This proves native
macOS public-consumer compilation only.

The user clarified on 2026-10-06 that passing local builds and tests gates
local task completion and merge; other platforms belong to CI and the v3 PR.
Missing BSD or Windows receipts do not block TASK-124. The ledger reports only
executed checks; no portable runtime readiness claim is made. Groundwork
validation and finalization remain pending.

## Implementation recovery (2026-10-06)

The failure reproduced as the missing native-family receipts; the source and
focused tests did not fail. The registered worktree remains `task/TASK-124` at
the same base commit, with all implementation changes uncommitted.

The existing local Colima Linux VM and existing `minimail-lingua-cross:latest`
image supplied a native Linux compiler without installation or provisioning.
The disposable container ran with networking disabled, read-only source, one
CPU and 512 MiB RAM, and was automatically removed. Debian 13.7, GCC 14.2.0,
target `aarch64-linux-gnu`, compiled the unchanged fixture using:

```sh
c++ -std=c++20 -I src -c test/headers/consumer_v3_readiness.cpp -o /tmp/consumer_v3_readiness.o
```

The command exited 0 with no diagnostics. The receipt in
`_build/task-124-receipts/linux-consumer-receipt.json` retains the native kernel,
distribution, toolchain, command, image identity and all three canonical input
hashes; each hash matches the ledger above. Compiler stderr is retained in
`linux-consumer.log`.

The configured macOS build's consumer and value-test targets rebuilt
successfully. The consumer exited 0; the value tests passed 4 tests / 30 checks,
zero failures or skips. Recovery logs are `recovery-focused-build.log` and
`recovery-focused-check.log`. The prior full-suite receipt remains the full-suite
evidence; recovery did not rerun that suite or change source. `git diff --check`
also passed.

Remaining environment/access boundary: local Docker inventory contains Linux
containers and a retained TASK-127 FreeBSD VM checkpoint, stopped by its owner;
no running native BSD guest or Windows environment was found. The checkpoint's
disk, seed and stopped state were preserved. The machine-access client reports
`Stopped` with no peers, and SSH configuration contains only `github.com`.
No native Windows VM application or Windows compiler was found in the inspected
local inventory. Host storage has approximately 3.9 GiB available. External
native-host access or provisioned BSD/Windows environments must be supplied by
the coordinator; this local repair has no authority to publish a CI branch,
provision external machines, or resume the unrelated stopped checkpoint.
Neither missing receipt is claimed passed. Task/card status stays In Progress;
no validation/finalization step, staging, commit, push, merge or deployment ran.

## Independent implementation resume (2026-10-06)

The registered worktree, branch and starting commit were independently checked;
existing implementation and all retained receipts were preserved. The native
Linux, native macOS and installed-consumer receipts still match the final
fixture and transitive public-header hashes. The retained `check.log` records
the prior 221/221 full suite and `check-local` gates; those full gates were not
repeated because no production source changed.

The six focused consumer/value and inherited I/O regression targets were rerun
serially. The sandbox run passed five targets but denied loopback operations
in `io_backend_contract`; its failed receipt is
`resume-focused-check.log`. After that process completed, the authorized
`nice -n15 make -C _build/task-124/test -j1 check` invocation with
`TESTS='consumer_v3_readiness readiness_contract io_operation io_connection_owner fake_io_backend io_backend_contract'`
passed 6/6 targets with zero failures, errors or skips. The real backend
contract passed 41 tests / 734 checks. The successful receipt is
`resume-focused-authorized-check.log`. Changed-file cpplint, file-size checks
and `git diff --check` also passed.

All three implementation action items are checked, with task/card status still
In Progress. At the time of this resume, the executor treated native BSD and
Windows receipts as blocking; the subsequent user clarification above
supersedes that interpretation. No production source, Git staging,
commit, merge, worktree management or memory write was performed in this resume.
