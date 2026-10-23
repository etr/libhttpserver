# FEATURE-ci-windows-entropy: portable Windows CNG success status

Status: Complete (local implementation, Groundwork validation and finalization). Native
Windows execution remains CI / v3 PR owned. Base: `v3` at
`a68ba478e967988331c167af102189c905b6fe95`.

Windows CI run39948363870 job120396472238 checked out that same base and failed
compiling actual `src/detail/entropy_sys.cpp` because `STATUS_SUCCESS` was
undeclared. This native compiler failure is the accepted RED. A task-local
probe compiling the actual Windows source with only the documented CNG API
boundary doubled reproduced the same error before the change.

The Windows draw now compares against `NTSTATUS{0}`, documented by Microsoft's
[NTSTATUS values](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55)
as STATUS_SUCCESS. The [BCryptGenRandom contract](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom)
supplies NTSTATUS and identifies STATUS_SUCCESS as success. Exact-zero
acceptance remains deliberate: Microsoft's generic
[NT_SUCCESS predicate](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-ntstatus-values)
would also accept additional positive and informational statuses.

Action items:

- [x] Create isolated worktree from v3 using Groundwork's identity helper;
  preserve bauth and other unrelated work.
- [x] Retain source-bound native compiler RED and read official Microsoft docs.
- [x] Replace only the unavailable macro with a typed documented equivalent;
  preserve exact-zero success, provider, buffer/count, RNG flag and fill policy.
- [x] Apply doubt-driven checks for widened status acceptance, parameter or
  fallback changes, and empty/error/all-zero handling before commit.
- [x] Run existing focused real OS entropy tests and a bounded actual-source
  API boundary probe; retain raw evidence without adding a public test seam.
- [x] Run changed-file cpplint and diff checks.
- [x] Retain commands, logs, docs and doubts at root
  `.debug/FEATURE-ci-windows-entropy/`.
- [x] Caller: Groundwork validation and local finalization into v3; four reviewers approved with zero findings.
- [ ] CI / v3 PR: genuine native Windows SDK compilation/execution.

Local verification: the unchanged Linux entropy executable passes 5 tests and
21 checks against production `entropy_sys.cpp` (the conditional single-byte
zero path accounts for baseline's 20 checks). Compilation uses Clang18,
C++20 and `-Wall -Wextra -Werror` without warnings. The boundary probe passes
zero plus eight nonzero status cases spanning positive, informational, warning
and error ranges; it checks exact null algorithm, output pointer/count, flag2,
one call per nonempty fill, bounds, no call for empty fill and unchanged error
messages including all-zero rejection. This is API boundary evidence only,
not genuine Windows SDK/ABI, cryptographic RNG or runtime proof. No Windows
compiler/SDK is installed locally; that check stays pending in CI under
AGENTS.md, without blocking local completion.

No new production headers, dependencies, test-only API, RNG primitive, fallback
or generic testing framework were added. Scratch probe files are uncommitted.
No broad baseline, unrelated Linux investigation, audit-policy repair,
validation loop, merge or publication was performed.

Project notes: Autotools/C++20 normally uses `./bootstrap`, out-of-tree configure
and make; this bounded task directly compiled the existing custom Littletest
`test/unit/entropy_sys_test.cpp` with actual `src/detail/entropy_sys.cpp`,
`-DHTTPSERVER_COMPILATION -Isrc -Itest`. The outcome header is self-contained;
no extra production dependencies are needed for this executable. Changed-file
cpplint uses root CPPLINT.cfg. Groundwork resolved branch/path verbatim; Git
metadata mutation required the existing sandbox escalation.
