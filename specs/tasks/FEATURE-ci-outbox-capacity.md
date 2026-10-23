# FEATURE-ci-outbox-capacity: portable HTTP/1 outbox capacity admission

Status: implementation and focused local verification complete. Validation/finalization belong to the caller. Base: `v3` at `56da7ec9e7ca848de9a19e58fbeb59e2baaa801c`.

A valid response head previously failed under libc++19 because `reserve(276)` retained capacity279 and the outbox required an exact capacity match. The unchanged real outbox reproduced the failure while its framer accepted the same response. Native macOS CI attribution remains corroborated; the original failing wire/subcase was not observed.

The outbox now admits the existing geometric request and then any observed allocator-rounded excess before copying or publishing. It records actual retained capacity, keeps the old buffer charged until destruction, and rolls back provisional storage on refusal or exception. Final-head exceptions restore the previous interim buffer and its charge. Growth arithmetic checks string/aggregate bounds. This preserves the existing transient private candidate allocation behavior; it does not promise advance knowledge of allocator rounding.

Action items:

- [x] Isolate from v3; preserve unrelated worktrees.
- [x] Reproduce original real libc++ outbox refusal with a durable behavior regression (RED).
- [x] Admit actual rounded capacity without changing budget limits or serialized ownership.
- [x] Verify exact root/child charge, child/parent refusal, old-plus-replacement admission, byte preservation, rollback and release.
- [x] Verify exception/overflow negative contracts against actual outbox paths.
- [x] Apply doubt-driven development before commit; evidence: `.debug/FEATURE-ci-outbox-capacity/doubts.md`.
- [x] Finish focused GNU/libc++ outbox and affected existing resource-allocation checks, plus cpplint.

Final verification: GNU and libc++19 each pass the full affected outbox executable (29 tests/244 checks), four unchanged selected existing H1 resource-allocation tests (4 tests/414 checks), and actual allocation-failure/SIZE_MAX probes. cpplint passes all three changed C++ files; `git diff --check` passes. The focused resource selection is not a claim that the complete cross-protocol resource suite ran. Source hashes and original failing/passing commands and outputs are retained with the journal.

Evidence is retained in `.debug/FEATURE-ci-outbox-capacity/`: commands, build/run logs, original RED, bounded fault probe and journal. Scratch probe programs remain uncommitted. No broad baseline suite was run. Native macOS/BSD/Windows execution remains CI/v3-PR owned under AGENTS.md and does not block local completion.

Separate Linux incidental failure remains **UNCERTAIN**, as settled by the prior single bounded check. Its evidence is `.debug/FEATURE-ci-native-runtime/journal.md`: native `icy_http10_and_head_keep_framing` assertions354/356 failed; a subsequent unchanged-core parity replay passed6 tests/35 assertions but its process failed LSan because ptrace is unsupported in the sandbox. This is not a passing process gate or proof of independence. This task does not reopen, retry or repair that failure.

Project notes: C++20; normal setup is `./bootstrap`, out-of-tree `configure --disable-v3-tls`, `make`. Tests use `test/littletest.hpp`; this task directly compiled the affected existing test with its real production translation units using GNU libstdc++ and the retained task-local libc++19, without installing dependencies. `cpplint` reads root `CPPLINT.cfg` (200-column limit). Worktree creation required the existing Git sandbox escalation; branch/path came verbatim from Groundwork's identity helper. No configured agent memory was available.
