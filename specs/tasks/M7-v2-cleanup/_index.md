# M7 — v2.0 Branch Cleanup

**Status:** Draft 1
**Last updated:** 2026-06-07
**Owner:** Sebastiano Merlino
**Inputs:** [../v2-branch-gap-audit.md](../v2-branch-gap-audit.md)

---

## Overview

Forensic gap audit (`v2-branch-gap-audit.md`) walked every modified file on
`feature/v2.0` vs `master` and surfaced **HIGH** / **MED** / **LOW** findings
across source, tests, CI, and examples. This milestone converts every
substantive finding into a workable task that groundwork's plan / implement /
validate pipeline can complete.

This is a companion to [`../v2-deferred-backlog-plan.md`](../v2-deferred-backlog-plan.md):
that plan covered the seven substantive deferrals from the review-issue sweep
(TASK-053 .. TASK-059). M7 covers the residual audit findings (TASK-060 ..
TASK-093).

## Task table

| ID | Name | Audit grade | Estimate | Status |
|---|---|---|---|---|
| TASK-060 | Scope or remove file-scoped `-Warray-bounds` suppressions | HIGH | S | Done |
| TASK-061 | Mechanical cleanup sweep — unfinished prose, orphan comments, stale doc refs | HIGH | S | Done |
| TASK-062 | RFC-7616-compliant Digest auth response factory | HIGH | L | Done |
| TASK-063 | Honor or remove `http_response::pipe` `size_hint` parameter | HIGH | S | Done |
| TASK-064 | Structured cookie type | HIGH | M | Done |
| TASK-065 | RFC 5952 IPv6 zero-compression in `peer_address` | HIGH | S | Done |
| TASK-066 | Runtime setter for hook alias slots | MED | M | Done |
| TASK-067 | Remove v1 `registered_resources*` maps and `namespace compat` shim | MED | L | Done |
| TASK-068 | `connection_state` hardening — CWE-226 / CWE-14 | MED | S | Done |
| TASK-069 | Remove transitional two-arg `http_request_impl` constructor | MED | S | Done |
| TASK-070 | Migrate `hook_table_` to `std::atomic<std::shared_ptr<T>>` | MED | M | Backlog |
| TASK-071 | Wire `install_not_found_alias_` stub and remove dead `lambda_handler` arm | MED | S | Done |
| TASK-072 | Arena-allocated unescape on the warm path | MED | M | Done |
| TASK-073 | Revisit libmicrohttpd v0.99 unescape workaround | LOW | S | Done |
| TASK-074 | Gate DEBUG raw-body printing behind explicit opt-in | LOW (sec) | S | Done |
| TASK-075 | Propagate `wait_for_server_ready` to sibling hooks integ tests | HIGH | M | Done |
| TASK-076 | Replace tautological-pass blocks in TLS test lanes | HIGH | M | Done |
| TASK-077 | Restore Windows / Darwin coverage in skipped test suites | HIGH | L | Done |
| TASK-078 | Resolve commented-out CONNECT-method test bodies | HIGH | S | Done |
| TASK-079 | Drive nonce/opaque state machine in v2 digest-auth integ tests | MED | M | Done |
| TASK-080 | Tighten threadsafety_stress latency gate back from 100× to 10× | MED | M | Done |
| TASK-081 | Fill empty-on-correct-build unit suites and re-enable pthread leak detector | MED | M | Done |
| TASK-082 | Tighten static-size bounds in `http_resource_test` and `webserver_pimpl_test` | MED | S | Done |
| TASK-083 | Wire real CI gates into benchmarks | MED | M | Done |
| TASK-084 | Re-measure libstdc++/Linux v1 baseline for `get_headers` ns/call | MED | S | Done |
| TASK-085 | Residual test-smell sweep | MED | S | Done |
| TASK-086 | Execute file-size split roadmap (FILE_LOC_MAX 750 → 500) | HIGH | L | Done |
| TASK-087 | Restore msan CI lane | HIGH | M | Done |
| TASK-088 | Re-enable helgrind / drd / sgcheck in valgrind CI lane | HIGH | M | Done |
| TASK-089 | Wire `check-parallel-install` into per-PR CI + remove SKIP-degrades-to-pass | HIGH | M | Done |
| TASK-090 | Harden CodeQL workflow and Codecov upload | HIGH | S | Done |
| TASK-091 | Tighten CI script soft-degradations | MED | M | Done |
| TASK-092 | Wire `route_table_concurrency` TSan + `stop()`-from-handler into per-PR CI | MED | S | Done |
| TASK-093 | Tighten example caveats (auth, pipe, access-log) | LOW | S | Done |
| TASK-094 | Extend `threadsafety_stress` with per-resource `add_hook` CAS race | MED | S | Done |
| TASK-095 | Zero arena *overflow* blocks on connection reuse (CWE-226 residue from TASK-068) | MED (sec) | M | Backlog |

## Dependency notes

- TASK-062 (Digest RFC-7616) blocks TASK-079 (digest integ tests state-machine).
- TASK-066 (runtime alias setter) blocks TASK-085 (residual alias-slot test smell).
- TASK-072 (arena unescape) depends on TASK-058 (warm-path bench infra; Done).
- TASK-061 and TASK-085 overlap on the `test/Makefile.am:67-74` stale comment — either lands it.
- TASK-093 partially depends on TASK-052's docs sweep noting any hook-context protocol-version gap.

Everything else is parallelizable. Recommended sequencing: knock out the
mechanical Ss (TASK-060, TASK-061, TASK-069, TASK-073, TASK-074, TASK-085,
TASK-090, TASK-093) in week one, then bunch the Ms across one engineer per
domain (src / test / CI), then close out the Ls (TASK-062, TASK-067,
TASK-077, TASK-086).

## Notes for groundwork

- Each task file mirrors the format of `specs/tasks/M*/TASK-*.md`: goal, action items, dependencies, acceptance criteria, related requirements + decisions, status.
- All current statuses are `Backlog`; flip to `In progress` / `Done` as work lands.
- The audit graded each finding HIGH / MED / LOW. The "Audit grade" column above preserves the grading; treat HIGHs as gating for the next minor release, MEDs as opportunistic, LOWs as documentation-only unless they slot into a related larger task.
