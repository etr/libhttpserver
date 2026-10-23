# FEATURE-ci-freebsd-proof: FreeBSD checkout provenance and evidence recovery

Status: Complete (local implementation, Groundwork validation and finalization). Native FreeBSD execution remains CI / v3 PR owned under AGENTS.md. Base: `v3` at `663c0eb390a5d0a9380b8c9002c67b0cf2af770e`.

Archive FreeBSD job `120331982719` in run `39928823544` completed its pinned Node source build and client installation, then failed native Git provenance with `dubious ownership`. Its always-run recovery also failed: legacy SCP rejected the source basename `.`. The affected workflow sections were unchanged from `919fa6aa` to this task's base, as established by the caller's bounded comparison. Full original output is retained in the main repository's `.debug/FEATURE-ci-freebsd-proof/original-freebsd-job.log`.

Both package and archive FreeBSD commands now export a process-scoped `safe.directory` for exactly the current pinned checkout. Before Node setup, native Git reports its executable, version and verified HEAD commit into `git-provenance.log` and the job output. Probe failure prints its retained diagnostic and stops the guest command. Python provenance subprocesses inherit the same exact checkout trust; no global Git settings or source/artifact binding checks change.

Both host recovery commands copy the fixed, quoted, named evidence directory into its parent. OpenSSH's legacy SCP filename checks remain enabled. Recovery merges into `package-evidence/` or `platform-evidence/`, preserves hidden files and existing action-copyback evidence, and retains the archive proof at `platform-evidence/platform/FreeBSD.tar.gz`.

Action items:

- [x] Create the isolated task branch/worktree using Groundwork's identity helper; preserve unrelated bauth work.
- [x] Retain the original Git/SCP failure and confirm new focused regressions fail before implementation.
- [x] Establish exact checkout Git trust before expensive prerequisites and retain early native provenance on success/failure.
- [x] Correct both SCP commands without `-T`, skipped validation, masked recovery failures, or changed upload conditions.
- [x] Exercise real Git ownership refusal, inherited checkout trust and refusal of another checkout.
- [x] Exercise actual OpenSSH SCP sender/receiver protocol with only SSH transport replaced locally; verify strict-basename RED, recovered diagnostics, hidden files and exact package/archive layout.
- [x] Run all three affected Python workflow test scripts, actionlint, shell/Python syntax and diff checks.
- [x] Preserve raw outputs, command journal and verification limits in the main repository's `.debug/FEATURE-ci-freebsd-proof/`.
- [x] Caller: Groundwork validation and local finalization into `v3`; five reviewers approved with zero findings.
- [ ] CI / v3 PR: fresh native FreeBSD execution and hosted artifact recovery/upload.

Local verification: all 20 workflow tests, 4 package-workflow tests and 6 platform-CI tests pass. Actionlint passes for both changed workflows. The pre-fix regression run failed in 10 subcases across both workflows, including real native Git checkout refusal and real SCP `unexpected filename: .`; the repaired commands pass. Local Git uses `GIT_TEST_ASSUME_DIFFERENT_OWNER=1` to exercise its actual ownership policy, with an independently initialized checkout still refused. The SCP fixture runs the installed OpenSSH client and server protocol; it replaces SSH transport only and uses no network credentials or daemon. These checks provide local command/protocol evidence, not native FreeBSD or hosted action proof.

Pinned Node source setup took roughly 2h28 and completed in the failed native job. Tool pins, checksums, dependency setup, caches, timeouts, source bindings, artifact identities, permissions and publication are unchanged. No native baseline, broad suite, unrelated subsystem investigation, merge, push or publication was performed. Prior closed findings remain closed. The separate older Linux parity failure remains **UNCERTAIN** after its settled bounded diagnostic batch; this task does not reopen it or treat a passing replay as independence.

Project notes / task-executor memory: v3 is Autotools/C++20 (`./bootstrap`, out-of-tree `configure`, `make check`); workflow-only changes need no native build setup. Focused workflow contracts use Python `unittest` and PyYAML in `scripts/test-v3-ci-workflows.py`, `test-v3-package-workflow.py`, and `test-v3-platform-ci.py`. Extract actual YAML commands for execution; mock only unavailable transport/tool boundaries, retaining real Git/SCP policy checks. Task evidence lives in ignored main `.debug/<identifier>/`, task records in `specs/tasks/`. Worktree identity comes from Groundwork's shared helper; Git metadata creation/commits may need the existing sandbox escalation.
