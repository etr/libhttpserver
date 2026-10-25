# FEATURE-ci-native-receipt-line-endings

Status: In Progress (implementation; caller validation/finalization pending).
Base: local v3 `47c6f89a6402e1d4cf71623772c44faa23be2a58`.
Plan: primary `.groundwork-plans/FEATURE-ci-native-receipt-line-endings-plan.md`.
Journal: primary `.debug/ci-47c-windows-configuration-5x995i1u/journal.md`.

Accept LF and native CRLF records in the three line-oriented package matrix
readers: configuration_results, supported_configuration_results and suite_result.
Preserve raw receipt/log bytes and all existing lane, outcome, inventory, count,
case witness, skip and exit requirements. Do not change parity JSON or global
process, socket or HTTP handling.

- [x] Read accepted plan, owning readers, native writers/callers and actual receipt; isolate from exact v3 base.
- [x] Record meaningful RED with actual captured native stderr through the real parser.
- [x] Implement component-local LF/CRLF handling and strict positive/negative regressions.
- [x] Run affected Matrix/receipt checks, Python compilation and diff checks.
- [ ] Caller Groundwork validation/finalization.
- [ ] Native Windows post-fix package/archive checks: CI/v3 PR owned, incomplete; not a local acceptance gate.

Actual Windows package run 40170157075 / MinGW job 121099184812 produced
artifact 12590541497 (SHA256
`87ebf648957f8f3e473c1e04bec15e1a07a0a6cd0b66cfe1a94895aafffeb3fc`).
Its native TLS-off managed process exits zero and emits all ten expected
PACKAGE_CASE rows with CRLF. Exact captured stderr fails the old parser;
changing only CRLF to LF preserves all rows/outcomes and passes. Archive job
121099305201 reports the same configuration failure; no second artifact download.

Only focused local data/parser and receipt checks are authorized. The two
configure timestamp checks remain failed/incomplete/closed and unretried;
epoll's exhausted 64-comparison batch and closed adapter/header/outbox/kqueue
uncertainties remain unchanged. Newly approved native kqueue capture is queued
after this repair/finalization, with 0/64 comparisons used; this task does not
implement or execute it. Other 47c CI remains useful and untouched.

Local implementation evidence (2026-10-25):

The captured 528-byte stderr literal has SHA256
`f6407bb289875ad13aa92722b35f883bf36cf87815533c5b666d0cdbe7d5e17e`.
First RED: LF-only conversion parses all ten original outcomes, while exact
native input raises `configuration case inventory incomplete`. Expanded RED
also catches CRLF/mixed endings in supported configurations and native suite
records, including actual Consumer capture from a local Python byte writer.
These are local parser/fixture results, not new native Windows execution.

Each reader now replaces only CRLF with LF in its local immutable string.
Original regexes and subsequent validation remain unchanged. Positives cover
LF/CRLF/mixed endings and both TLS/loop lanes. Negatives retain missing,
duplicate/unexpected case, invalid outcome/binding/lane, zero/inconsistent suite
counts, missing witnesses, failed/skipped/nonzero-exit, bare CR, trailing
whitespace and junk rejection. The actual Consumer regression verifies raw
stderr/stdout and the retained process JSON remain unchanged by parsing.

Focused verification: MatrixTest + ProcessTest, 28 tests passed; existing
Commands success/allowed-failure and nonzero-exit receipt tests, 2 passed.
Changed Python files compile and `git diff --check` passes. No documented
Python linter exists. No full suite, SDK/native build, Windows probe, CI poll,
rerun or CI-control action was performed. Primary implementation/ contains
RED/GREEN logs, source hashes and `actual-receipt-green.json`, which compares
the repaired parser against original LF-only outcomes while preserving the
740-byte native JSON receipt (SHA256
`fd27b6db1cc254d9b97ac60416f4f278a74671fd66ed3f5a086a183ed833eba8`).
Native Windows post-fix CI remains incomplete and CI/v3 PR owned.
