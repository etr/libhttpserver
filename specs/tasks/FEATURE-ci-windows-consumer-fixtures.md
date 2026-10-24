# FEATURE-ci-windows-consumer-fixtures

Status: Complete (local validation/finalization; native CI pending).
Base: local v3 `6b2f596e88796f1a033a770d26b07aaa7049a8ff`.
Plan: primary `.groundwork-plans/FEATURE-ci-windows-consumer-fixtures-plan.md`.
Journal: primary `.debug/ci-6b2-windows-consumer-fixtures-9mpis316/journal.md`.

Correct the three accepted native Windows fixture assumptions in
`scripts/test-v3-installed-consumer.py`: selected absolute Bash and cygpath at
the inspection producer subprocess boundary, semantic configure command receipt
selection, and consistent canonical native path identities in matrix fixtures.
Preserve real native compilation, strict inspection failure persistence, and
all original linkage/header contamination negative controls.

- [x] Read accepted plan, owning sources, native failure log and project instructions; isolate from exact local v3.
- [x] Record RED for absolute shell/conversion and configure receipt ordering through the real runner/Commands boundary.
- [x] Correct the three fixture assumptions without changing production code or adding skips.
- [x] Verify the affected module, direct receipt recovery, Python compilation and diff checks.
- [x] Caller Groundwork validation/finalization.
- [ ] Native Windows post-fix package/archive checks: CI/v3 PR owned, incomplete; not a local acceptance gate.

Native RED: package run 40072250923, Windows job 120808025501,
artifact 12551364760 SHA256
`9fcc5f1f8f02b91bb9d01268b880cc12ff4a0036957aebe5e15bcc4ddb27caec`.
The retained full log ran 65 tests with two failures, seven errors and 29
existing Windows POSIX skips. No post-fix native receipt is claimed. Closed
epoll uncertainty and all other closed assessments remain outside this task.

Task-local setup notes: Python unittest fixtures run directly with
`python3 scripts/test-v3-installed-consumer.py`; real compiler/Bash/Automake
tools are already installed locally, with no package installation needed.
No documented Python lint command exists. Existing primary checkout instructions
are ignored files and must be read there. Git metadata requires the approved
Git command escalation; `.worktrees/` is already ignored. No prior executor
memory file was available in either configured agent-memory location.

Implementation evidence (2026-10-24):

The inspection producer now intercepts the actual selected absolute Bash and
its cygpath conversion at subprocess creation. The original four inspection
tests remain unchanged and additionally run through a selected-shell subclass
that replaces only the runner's platform binding. The inspector retains its
separate FreeBSD binding and the compiler sees the actual native host. Real
consumer/object/link-map production, full inspector stdout/stderr, exact exits,
artifact SHA256, stale-command clearing and receipt write-error assertions all
remain in force. The controlled shell/converter outputs supply no native
Windows tool or loader evidence.

Both default and optimized configure cases select the actual configure receipt
by its TLS/examples/prefix arguments, require exactly exit 23 and the expected
CXXFLAGS, and check conversion order, arguments, output and exit 0 when present.
A new alternate-runner test executes the real host Bash configure script after
controlled conversion, reproducing the prior first-receipt mistake locally.
The three path fixtures derive graph IDs, dependency payloads and all roots from
the same resolved temporary directory. Wrong-prefix, static/shared, source
public-header and unapproved host-helper failures remain strict.

RED: the new selected-shell replays escaped the old producer boundary (four
errors); both configure flag subcases read conversion exit 0 instead of 23.
Native path-only RED remains the retained original Windows log, not a simulated
local receipt. GREEN: all 70 affected module tests pass locally in 22.223s (65
existing plus five new), and the exact direct package/archive receipt-recovery
test passes in 1.967s. Python compilation and diff whitespace checks pass.
AST comparison confirms all four original inspection test bodies and every
existing skip decorator are unchanged. The native baseline's 29 Windows skips
remain documented; no new skips were introduced. Production runner, matrix
validator, compiler integration and command recorder bytes match the base.

Logs and hashes: primary journal directory contains `local-red.log`,
`local-green.log`, `local-module.log`, `local-recovery.log` and
`local-source-evidence.json`. No unrelated failure, retry, broad baseline,
native probe, CI control or publication occurred. Parent reported the completed
archive Windows job 120808170892 has the same 65-test symptom pattern; no
duplicate artifact was downloaded or investigated. Native post-fix Windows
checks remain incomplete and owned by CI/v3 PR; caller owns validation,
finalization and publication decisions.

Reusable executor notes: fixture regressions load the real runner by
`importlib.util`, substitute subprocess producer outputs with real Python child
processes, and delegate compiler subprocesses unchanged. Replace a runner or
auditor module's `platform` binding for branch coverage rather than mutating
the shared platform module. Configure receipts can include successful path
conversion before the expected failure; select them by command semantics.

Groundwork validation `03e31718ba1957895fa2a136563888e2` sealed PASS for
`cb23206ed9e08fa9136336bc060fdcbcfe34951c` / tree
`fc9edb4f1d1978b7ac0b20c308fe57ba0715024f`: one iteration, nine fresh
independent approvals, zero fixes and zero unworked findings. Input-matched
passing checks reused; no report created. Native Windows post-fix checks remain
incomplete and CI/v3 PR owned. Existing 6b2 CI jobs are preserved; caller owns
publication timing. Closed epoll uncertainty remains outside this task.
