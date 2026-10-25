# FEATURE-ci-quic-physical-root: physical temporary root for partial QUIC Initial fixture

Status: Complete locally; scoped compiler checks and Groundwork validation pass. Focused Linux execution unavailable; native post-fix CI proof incomplete.
Base: exact local `v3` at `37095c23751960b6e1adcad95d7b3c4c34c64351`.
Plan: `.groundwork-plans/FEATURE-ci-quic-physical-root-plan.md` in the primary checkout.

## Goal and scope

Resolve the temporary directory to its physical path only in
`partial_initial_keeps_file_pump_parked_before_negotiated_flow` before constructing
its file-serving owner. Darwin's `/tmp` is a symlink; the original fixture
passes that symlink to a root opener that correctly requires a directory and
rejects symlinks. Wrap the existing root initializer in
`std::filesystem::canonical`; preserve the owner call's `root.string()` and
all other test-source bytes. Production behavior and the support root opener's
`O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC` contract remain unchanged.

## Action items

- [x] Read the accepted plan, literal journal and executor memory; create the
  resolved isolated Groundwork worktree from the exact local v3 base.
- [x] Inspect retained original native RED and selected actual fixture before
  editing; retain source and original evidence hashes.
- [x] Wrap only the selected root initializer in `canonical`; preserve partial
  Initial bytes, owner operations, assertions and all four registered tests.
- [x] Compile the actual owning TU to a GNU object and syntax-check it with
  existing real LLVM/libc++ and private OpenSSL 3.5.9 headers/configuration.
- [x] Run changed-file cpplint, diff and byte-preservation checks; retain exact
  commands, outputs and source/tool/configuration/provider hashes.
- [ ] Focused actual Linux case under symlinked TMPDIR: unavailable with the
  inspected caches; no compatible cached interop-provider/core pair established.
  No provider/core rebuild or broader executable run is authorized or performed.
- [x] Caller Groundwork validation and local finalization.
- [ ] Native Darwin/FreeBSD post-fix execution (CI/v3 PR-owned; incomplete,
  not a local completion or merge blocker under project policy).

## Verification and limits

Primary journal: `.debug/ci-quic-physical-root-_mha28yk/journal.md`.
Evidence: `.debug/ci-quic-physical-root-_mha28yk/implementation/`.
Original RED: retained Darwin run `40241540719`, job `121314199531`, original
`quic_interop_tls` (four tests, 146 checks, one failure). Its selected fixture
passes `/tmp`; the original `O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC`
open returns -1/ENOTDIR for that symlink before owner datagram/tick/flow
assertions. A real child directory opens successfully. Selected member identity,
CRC and SHA were verified; whole archive SHA remains unverified. The earlier
assessment JSON contains historical open-batch wording: the parent journal's
terminal audit closes the four-target diagnostic batch at 4/340 charged.

GNU 13.3.0 object compilation and LLVM 19.1.1 with real libc++ 20.1 (header `_LIBCPP_VERSION=200100`) syntax
checking pass on the changed actual owning TU with empty diagnostics. The
existing generated Linux build config and actual private OpenSSL 3.5.9 include
configuration are hashed; no bootstrap, provider/core/SDK build was run.
Changed-file cpplint and diff checks pass. A byte contract proves the single
initializer substitution is the entire test-source delta; root-open support
is identical to base. No mirror test or test helper/framework was added.

Focused Linux runtime execution is unavailable; original native RED supplies
the causal evidence, while local compilation and exact source preservation
verify this reversible fixture change. Native post-fix runtime proof remains
incomplete and CI/v3 PR-owned. No new native executions, probes, suites, CI
API actions, dispatches, publication or unrelated repairs are authorized here.
All earlier closed budgets and unrelated UNKNOWN assessments remain unchanged.

Groundwork validation session `e79cc6c106c5a7eaf50ef45a49dc44b3` sealed PASS:
nine approvals, one iteration, zero fixes, zero unworked findings. The source
remains byte-identical to validated commit `0de42780565dff252b54378de305c80a36810622`.
Caller performs normal local merge, publication and exact worktree cleanup;
all unavailable/unexecuted runtime checks above remain incomplete.
