# TASK-192 implementation evidence

Manual Groundwork implementation in `/home/etr/progs/libhttpserver/.worktrees/TASK-192`, branch `task/TASK-192`, from `v3` at `94195e7262fe7778bddc860b3996591d90822c7e`. Caller owns validation, finalization, merge and cleanup. Task remains In Progress. No configured executor memory store was found; reusable setup facts are recorded below.

## Verified cause and interface decision

The fresh task-local configured `make -C build/task192/normal-off/test cookie_render` exits 2: no target. The source/install manifests contain no v3 cookie header or implementation. The legacy suite depends on removed v2 classes and fluent setters. The inventory and TASK-179 guide claimed a standalone helper while migration documentation delegated parsing/rendering to applications. The authorized repair supplies the bounded helper and keeps all three named capability-054 witnesses.

[Interface comparison and doubts](TASK-192-design-evidence.md) compares a validated fluent class, an owned aggregate with explicit operations, and a fields adapter on depth, locality and seam placement. Chosen: owned `http::cookie`, `parse_cookie_header(string_view) -> vector<cookie>`, and `render_set_cookie(cookie, string&) -> outcome`. The current public tree contains no reusable generic `result<T>` for this synchronous helper, so no generic result framework was introduced. Rendering validates every raw construction path before atomically replacing output; applications append successful values as ordinary repeated fields. No v2 request/response hierarchy, shims, cookie jar, automatic policy or parse cache is restored.

The historical v2 implementation at `f5d7e9e8^:src/cookie.cpp` confirms SP/HT-only token/name trimming, first-equals splitting, one outer quote pair, untrimmed leading value whitespace, malformed/empty-name omission, preserved case/empty values/percent bytes and lenient interior controls. The helper preserves those observable rules. Rendering preserves exact Expires/Max-Age/Domain/Path/Secure/HttpOnly/SameSite order, signed Max-Age, and None implying Secure. It rejects CR/LF/NUL/semicolon, invalid names, comma-bearing Domain/Path and invalid enum values. Typed outcomes replace fluent setter/exception shapes. Expires now explicitly rejects times outside civil years 0000..9999 rather than relying on platform-dependent `time_t` conversion; migration documentation records this real change. C++20 civil date arithmetic and fixed English names require no locale, timezone or OS date APIs.

## Behavioral RED/GREEN and clearing doubts

Evidence root: `build/task192`; unique journal path is recorded in `build/task192/journal-path`.

- Public declarations and a no-behavior scaffold permit behavioral RED: `/usr/bin/clang++-18 -std=c++20 -Isrc -Itest test/unit/http_cookie_test.cpp src/cookie.cpp -o build/task192/cookie-red`, then execute. `cookie-red.log`: 9 cases / 69 checks / 35 expected failures. The empty parser, unchanged rendering and missing typed rejection fail actual assertions; missing-header compilation alone is not counted.
- `cookie-green.log`: 9 cases / 129 checks, zero failures/skips. Ownership is tested after overwriting/destroying input and clearing the original vector. First-equals/quotes/malformed tokens and raw controls are pinned. The CR witness first asserts the exact parsed name/value retains CR, then requires `invalid_argument` and unchanged output. All four construction fields exercise CR/LF/NUL/semicolon rejection; parsed LF/NUL, invalid names, comma attributes, enum, signed-age extrema, SameSite, epoch/negative/leap/boundary dates and int64 overflow are covered.
- `runner-red.log`: 31 Python tests / 10 failures demonstrating accepted unrelated commands/suites/missing executable/hash/log/count proof. `runner-green.log`: 31 pass. `runner-count-red.log`: 32 tests / six failures on mismatched success totals, ordinals and duplicate summary/failure/skip records. `runner-count-green.log`: 32 pass.
- Runner mutation fixtures intentionally use synthetic bytes/logs; the positive contract executes the actual current cookie suite, retaining command/suite/case identities, executable and log hashes, completion markers and aggregate test/check counts. Full parity uses real freshly built binaries, including libtool wrapper resolution and the explicit peer_policy taskset invocation. The behavior-wire special witness remains intact.
- Linear token scanning and one owned pair per nonempty token bound parser storage by admitted input, without cache/global state. Existing HTTP field budgets govern input. Rendering builds a local string after validation. All named public-interface doubts are cleared by the final native, provenance and installed wire receipts below.

## Local verification

Four fresh C++20 Clang 18 configurations use `--disable-examples`, `-std=c++20 -pthread -O1 -g -fno-omit-frame-pointer`. TLS-on selects genuine OpenSSL 3.5.9 at `/home/etr/progs/libhttpserver/build/tooling/provider`, with include and library/rpath flags plus `-lssl -lcrypto` in `V3_TLS_LIBS`. ASan/nonrecovering UBSan adds `-fsanitize=address,undefined -fno-sanitize-recover=undefined -fsanitize-address-use-after-scope`, with `ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, outside ptrace. No suppression or disabled diagnostic. Lanes also run with `TZ=Pacific/Honolulu`, `LC_ALL=C`; rendered dates remain GMT.

`run_lanes.py` and each lane's `lane-receipt.json` retain exact configure/build/execution commands, environment, exits and durations. Library builds precede explicitly selected targets. The selection covers cookie native/consumer/public wire, ordered semantic fields/request heads, H1 response framing/outbox, response definitions, header hygiene, native linkage and Littletest result/skip checks; pinned Python runner/client contracts and check-local/install-layout/distribution gates follow.

An initial task-local provider selection put -L/rpath in `V3_TLS_LDFLAGS`, which configure regenerates from `V3_TLS_LIBS`; those configure failures are retained under `initial-lane-attempts` and corrected without production changes. A concurrent normal-off contract invocation captured the intentionally failing count RED before GREEN landed; its log is retained and the lane rerun after the fix. Historical TASK-185 evidence is untouched.

All four lane receipts pass. Each executes 11 native programs: nine ordinary Littletest suites total 143 cases / 886 checks / zero failures/skips; a separate framework skip-semantics contract intentionally reports 4 cases / 2 checks / 2 skips and exits successfully, and the standalone native linkage probe exits zero. The three cookie programs alone total 12 cases / 141 checks with zero skips. Final runner contracts execute 32 Python tests in each lane using that lane's actual cookie binary; client contracts execute three Python tests. Final wire fixture cleanup removes an unused admin-address branch and directly asserts stopped state after all throwing wire/lifecycle predicates.

`lane-final-summary.json`, `native-final-counts.json`, each lane's `runner-final.log` / `wire-final.log` and `distribution-contract.json` retain current results. Changed C++ cpplint, Python syntax and whitespace checks pass. The inherited unused `parse_one` warning in unchanged `http1_response_outbox_test.cpp` remains outside this helper repair. Initial check-local executions followed the repository's explicit missing-Doxygen skip. The retained genuine Doxygen 1.9.8 tool at `/tmp/task183-tools/doxygen` was then found and both normal TLS configurations executed the real zero-substantive-warning gate. Missing optional Graphviz initially caused map-file errors; the final documented command passes `HAVE_DOT=NO` through `DX_ENV`, with no source warning suppression. Initial logs are retained under `initial-lane-attempts`.

The first complete normal run executed all 92 commands / 168 golden observations / 90 supporting witnesses, but correctly failed stale input hashes after the verified malformed-proof repair. A subsequent normal receipt passed before the final cookie mapping/fixture cleanup. The first complete sanitized run likewise completed all observations and correctly rejected stale inputs after those final bounded edits. These are retained as diagnostics; only the final fresh-input receipts below serve as completion proof.

Additional checker RED/GREEN: `runner-malformed-red.log` reports three malformed proof-type exceptions (None/list/string); `runner-malformed-green.log` passes all 32 tests after treating non-record proof as invalid evidence. The final capability-054 mapping explicitly cites the actual typed helper/API and bounded-date migration instead of a generic request/source rationale.

## Complete final parity receipts

Both complete parity executions and independent final receipt checks pass against current inputs. `final-summary.json` retains the complete source/input hash map and exact cookie supporting/cell records; each final directory contains the authoritative receipt and every command log.

| Final receipt directory | Commands | Native executions | Native cases/checks | Golden observations | Supporting witnesses | Capabilities/cells |
|---|---:|---:|---:|---:|---:|---:|
| `parity-normal-final2` | 92 | 78 | 957 / 16024 | 168 | 90 | 60 / 360 |
| `parity-asan-final` | 92 | 78 | 957 / 16024 | 168 | 90 | 60 / 360 |

Each receipt has 344 applicable cells (152 preserved, 192 explicitly documented migrations) and 16 justified not-applicable cells. Both final receipts execute the full H1/H2/H3 corpus, all inventory-selected suites, TLS regressions, check-local and runner/client contracts; none are removed to obtain green. Independent results are `parity-normal-final2-independent.json` and `parity-asan-final-independent.json`.

`parity-normal-final2/receipt.json` SHA-256: `eb2e5fe2ed5b97586face83d59c47d8a0a0ebda9acde27e5f4e6d1c9db37afce`.

`parity-asan-final/receipt.json` SHA-256: `705744123db2b9af2090666034b82780690d72cb256c384878cc923883bf7fef`.

Capability-054 retains these exact three semantic witnesses in every applicable cell:

- `http_cookie:parse_multi_semicolon_separated:semantic`
- `http_cookie:render_full_attribute_ordering:semantic`
- `http_cookie:render_guard_rejects_cr_in_parsed_value:semantic`

Their `on-http_cookie` command binds the actual executable, suite, complete log and 9 executed cases / 129 checks, with zero failures/skips; the TLS-off cookie command also executes and retains matching provenance. Semantic evidence is not relabeled as per-protocol transport traffic. Separate installed raw-wire consumers supply actual managed/external TCP observations.

## Installed and wire proof

The public `consumer_v3_cookie_wire` uses only installed public library headers plus copied test-side Littletest/host-poll support and its own independent raw TCP client. Both managed and external readiness scenarios observe exact repeated Set-Cookie values and ordering for multiple request Cookie fields/pairs, empty values, case, first equals, percent bytes, GMT date and SameSite=None. The injection scenario retains CR-bearing parsed data, requires typed rejection before append, and observes one 422 response, no Set-Cookie field or second message, actual EOF, and orderly server/readiness cleanup. Scenario predicates throw on failure; Littletest records two stopped-state assertions after completed wire/lifecycle checks, not a fabricated count of internal predicates.

`installed_cookie.py` stages fresh normal TLS-off/on packages under an invocation-unique `/tmp/task192-cookie-package-*` prefix and copies consumer sources/support to an invocation-unique `/tmp/task192-installed-cookie-*` directory. The final probe explicitly rejects every compiler include path beneath the source/worktree root, so neither package headers nor copied test support come from source/build include paths. Compiler commands include only the installed include directory, no source/build include paths, compilation macro, private/provider header or v2 library. Shared consumers link the identified staged `.so`, static consumers the explicit `.a`. `installed-cookie-receipt.json` retains compile/run lines, actual library/executable hashes, dependency logs and counts. Final installed execution covers both consumers against shared and static libraries in both TLS configurations (eight successful runs). An initial unscoped TLS-on install spent its bounded staging interval building unrelated noinst fixtures; its timeout log is retained. Final staging scopes TESTS/check_PROGRAMS/noinst_PROGRAMS together, as planned, and all real consumer predicates still execute. Semantic consumer: 1 case / 10 checks. Wire consumer: 2 cases / 2 scenario checks; both exercise actual installed native runtime.

## Scope, remaining ownership and archival

BSD/macOS/Windows and nonlocal backends are unexecuted and CI/v3-PR owned under AGENTS.md. TASK-200 retains timer/manual-executor teardown race ownership. This task performs no validation loop, merge, worktree cleanup, tag, push or publication and does not complete TASK-185.

Archive `build/task192`, `.debug/cookie-parity-hm1hbv4g`, `/tmp/task192-installed-cookie-eq00vu0g` and `/tmp/task192-cookie-package-xs3scfon` before caller cleanup. The final installed receipt identifies both isolated directories and 26 successful compile/dependency/run/staging commands. Earlier in-tree installation iterations are diagnostic only; strict final proof uses these fresh outside-tree prefixes. Setup memory: bootstrap Autotools; configure VPATH builds; build src first; targeted Makefile programs/Littletest assertions live in test/unit, test/headers and test/integ; public consumer CPPFLAGS omit HTTPSERVER_COMPILATION; provider -L/rpath belongs in V3_TLS_LIBS. Git metadata requires the authorized escalation; ASan/network gates execute outside ptrace.
