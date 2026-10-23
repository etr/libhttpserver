# FEATURE-ci-macos-make-fixture: preserve make frontend identity in fixture aliases

Status: In Progress — implementation ready; caller validation/finalization pending.
Base: local `v3` at `9b93ebe8eb355b6da6099f8ed7764cda8c23bb90`.

Native macOS package job `120582231719` and archive Darwin job `120582524154`
both fail the new named `gmake` regression during real configure. Their
61-test fixture suites report one failure and three skips. Canonical GNU make
version detection and absolute/default selections succeed; renaming Apple's
developer-tool frontend through a `gmake` symlink makes its name-sensitive
dispatch fail to locate `gmake`.

The fixture now creates executable POSIX launchers for its `make` and `gmake`
PATH entries. Each launcher execs the shell-quoted original tool path with
`"$@"`, preserving the tool's invocation identity and argument boundaries.
The broken default still executes the deliberately failing unselected tool.
Selected MAKE values, ambient conflicts, real configure/dependency generation,
compile/install/check behavior and assertions remain intact. The production
driver, including both selected-MAKE environment assignments, is byte-identical
to the base. No dependency, platform skip, workflow or production change.

Action items:

- [x] Read the plan, literal journal, both actual native failure logs and complete fixture before editing.
- [x] Create the isolated Groundwork worktree from the exact local v3 base; preserve unrelated work.
- [x] Record the actual native regression as RED before the minimal launcher repair.
- [x] Replace renamed frontend symlinks with canonical-tool exec launchers.
- [x] With the corrected fixture, run all three real Autoconf cases against the original pre-MAKE-fix driver as counterfactual RED.
- [x] Run the same three cases against the current unchanged driver as GREEN; retain actual dependency and exact MAKE receipts.
- [x] Verify relevant preservation tests, launcher syntax/argument/status behavior, Python syntax and diff checks.
- [x] Retain raw evidence, executed sources, source bindings, versions and verification limits.
- [ ] Caller: complete required Groundwork validation and local finalization into `v3`.
- [ ] CI / v3 PR: confirm repaired fixture on native macOS package/archive jobs.

Verification: 22 existing installed-shell, ps and matrix contracts pass before
and after the repair. The corrected fixture with the exact original driver
from `c2d5c626f456f218676a6d840c8ed1442aa28438` fails all three cases in real
`config.status` dependency-fragment bootstrapping (expected RED). With the
current driver all three pass in 7.083s: actual configure, compile and install
succeed, then the deliberate check failure reaches the required marker.
`.deps/probe.Po` includes `probe.h`; build, install and check receipts retain
exactly `gmake`, `/usr/bin/gmake` or `make` for their respective selections.
The named case still has an unusable PATH default; the absolute/default cases
still override a conflicting inherited MAKE.

Both real valid launchers also execute a GNU make target containing spaces
through `-f -`, preserve its output and propagate GNU make's exit2 from the
deliberate recipe exit17. All captured shell launchers pass `/bin/sh -n`;
`python3 -m py_compile scripts/test-v3-installed-consumer.py` and
`git diff --check` pass. No Python linter is configured in repository guidance.
No full baseline, broad retry, unrelated repair or old runtime diagnosis was
performed. Native repaired macOS proof remains incomplete and CI/v3-PR owned;
local Linux proof does not claim Apple frontend execution.

The main repository's ignored `.debug/ci-macos-make-fixture-fdno1_wr/` retains
the literal journal, native logs, before/after preservation logs, source-bound
`baseline-actual-configure/` and `fixed-actual-configure/` projects/commands,
launcher contract output and versions. The evidence harness reuses the prior
task's actual execution setup, imports the registered test class and executes
the original driver blob in its normal module with the original file binding.
It does not model configure or the macOS frontend.

SHA256 bindings: final fixture
`88ca80f1759e5784534dd458da374814dc76b47652efc851e742877fced393a3`;
original driver
`61ef4487227c4196fbb7fb486e90b3bba255452e678ba47e8754ed8fa0383bc8`;
unchanged current driver
`6c565c7a7c3af16d46edc126638384fe15d81b8d7c36c098bb2661ec593dafb4`.

Project notes / task-executor memory: v3 uses Autotools/C++20; this test-only
repair needs no provider/full library setup. Focused checks use
`python3 scripts/test-v3-installed-consumer.py InstalledMakeTest InstalledShellTest InstalledShellPSTest MatrixTest`
and Python compilation. The real fixture uses `autoreconf --install --force`,
Autoconf2.71, Automake1.16.5, GNU Make4.3, GCC13.3.0 and Python3.12.3 locally.
Its existing script registration/distribution remains sufficient. Groundwork's
identity helper resolves the worktree/branch; git metadata requires sandbox
escalation. Preserve canonical frontend invocation names with exec launchers
rather than renamed symlinks. No merge, push or CI cancellation by executor.
