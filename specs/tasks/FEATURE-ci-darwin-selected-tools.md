# FEATURE-ci-darwin-selected-tools: use selected tools throughout native archive checks

Status: In Progress — implementation and focused local checks complete; caller validation and finalization pending.
Base: `v3` at `f37e7c79c042568b3f4b2bc9244c328c1fda1088`.
Plan: `.groundwork-plans/FEATURE-ci-darwin-selected-tools-plan.md` in the primary checkout.
Journal: `.debug/ci-darwin-selected-tools-bwzz0bjg/journal.md` in the primary checkout.

Native Darwin archive run `40030063567`, job `120676108141`, shows Apple/Xcode
make rejecting the mandatory `--eval` test enumeration. The quoted compiler
fixture also replaces the selected compiler with host `c++`, whose SDK lacks
the required cancellation declarations. These are selected-tool setup failures;
the other closed native assertion and fixture assessments remain unchanged.

Action items:

- [x] Read the saved plan, literal journal, primary CLAUDE.md, AGENTS and v3
  architecture; isolate from the exact local v3 base and preserve unrelated work.
- [x] Verify Homebrew's official `make` formula installs `gmake`; install and
  explicitly select it for both macOS package/archive proof invocations.
- [x] Demonstrate the production archive runner's later real make/Python
  checks inherit stale `CXX`, `CC` and `MAKE`; use the selected CLI values in
  its local environment without changing its parent or prerequisite steps.
- [x] Preserve the quoted fixture's caller CXX command, executable selection,
  spaced paths and argument order; honor the caller's MAKE in native fixtures.
- [x] Demonstrate and correct compound compiler/CC retention in the same
  production runner; retain interpreter, explicit script arguments and original
  and resolved tool bytes while keeping make a single executable path.
- [x] Verify focused platform/workflow/retention regressions, actual public
  header compilation through the existing private SDK, Python/shell syntax,
  actionlint and whitespace; inspect the complete owned diff.
- [ ] Caller: perform full Groundwork validation, finalization and merge.
- [ ] Native Darwin/FreeBSD/Windows post-repair execution: CI/v3-PR owned,
  not a local acceptance gate.

Implementation and evidence (2026-10-23):

Both macOS jobs add only Homebrew `make` and `--make gmake` to their main
proof invocation. The [official formula](https://formulae.brew.sh/formula/make)
documents both names. Other jobs, triggers, pinned actions, compiler/SDK,
Node and provider prerequisite steps retain their existing choices. Generic
library build requirements do not acquire a new make-version policy.

The package producer already selected CXX/CC/MAKE in its own child environment;
that cannot update the archive controller's subsequent make checks. Real local
RED failed with `unselected-make` and `unselected-cc`, and the quoted fixture
bypassed the caller's real compiler wrapper. The controller now supplies its
CLI tool choices locally. The fixture resolves the caller's compiler executable
before adding its deliberately competing `c++` to PATH, then forwards the
complete parsed command and all arguments.

The final archive tool-retention loop also rejected an entire compound CXX
command as an executable. The focused RED reached this exact production error
after successful real make and header compilation. Retention now parses only
compiler/CC command forms when they are not already executable paths, retains
their file arguments, and uses the existing catalog's original/resolved byte
retention. A real catalog regression checks a spaced interpreter alias,
compiler script alias, compound CC and a spaced make executable. Its fixture
quotes recursive MAKE inside its own Makefile; generic project Makefiles and
their existing shell command conventions are unchanged.

Focused local results:

- 14 selected platform tests pass, including both TLS-mode make checks,
  explicit choice over stale ambient tools, parent-environment preservation,
  inherited-SDK fallback, wrong/missing runtime rejection, real test enumeration,
  quoted compiler forwarding, privileged workflow argv and final exact bytes.
- Three affected workflow checks pass; two existing original/resolved catalog
  and tampering checks pass. No full baseline or 239-program suite was run.
- Two compiler-boundary regressions also pass through the existing private
  Linux libc++20 SDK. A durable production-runner proof retains real commands,
  compiler argument vectors, public-header syntax compilation and final catalog
  bytes. Archive identity, the expensive package producer, native inspectors,
  result counts and native origin validation are fixtures: this is Linux tool
  selection/retention evidence, not native Darwin/FreeBSD ABI evidence.
- Changed Python sources parse; every macOS job shell block passes `bash -n`;
  actionlint 1.7.7 passes both changed workflows with its optional shellcheck
  integration disabled; `git diff --check` passes. CLAUDE.md configures cpplint
  for C++ only and has no applicable Python linter.

Durable evidence, source snapshots and task-executor notes live in the primary
checkout's `.debug/ci-darwin-selected-tools-bwzz0bjg/implementation/`. The first
compiler-wrapper forwarding attempt exposed its own deliberate PATH shadow;
resolving the preexisting compiler before shadowing corrected that task fixture.
The first retention assertion mistakenly required an intermediate make symlink
label; existing retention promises the selected original and final resolved
paths. That assertion was corrected without expanding production retention.
These task-local setup failures are preserved; no incidental subsystem failure
was investigated or repaired. Native post-repair gates remain unexecuted and
overall CI is not green.
