# v2.0 Branch Gap Audit — Smells, Deferred Work, Suppressions

**Status:** Draft 1
**Date:** 2026-06-04
**Owner:** Sebastiano Merlino
**Scope:** 226 modified files on `feature/v2.0` vs `master` —
33 `src/*.cpp`, 48 `src/**/*.hpp`, 99 `test/**/*.cpp`, 6 `test/**/*.hpp`,
38 `examples/*.cpp`, 2 `.github/workflows/*.yml`, 16 CI helper scripts,
`test/tsan.supp`, `.codacy.yaml`.

## Why this exists

End-to-end walk over every modified C++ TU, header, example, GitHub Actions
workflow, and CI helper script on `feature/v2.0` looking for **laziness, shortcuts,
ignored issues, suppressions, deferred work, and unexecuted tasks**. Findings
below are graded **HIGH** (real residual work / unexplained suppression),
**MED** (acknowledged but stale or weakening), **LOW** (documented design
choice, flagged for completeness).

Companion to [`v2-deferred-backlog-plan.md`](v2-deferred-backlog-plan.md):
this is the *forensic* survey, that one is the *plan*.

---

## 1. Source code (`src/`)

### HIGH — Unscoped warning suppressions

| File | Finding |
|---|---|
| `src/http_utils.cpp:62` | `#pragma GCC diagnostic ignored "-Warray-bounds"` — **file-scoped, no `push`/`pop`, no comment, no follow-up marker.** Worst-shaped suppression in the audit. |
| `src/detail/ip_representation.cpp:55` | Same pattern: `#pragma GCC diagnostic ignored "-Warray-bounds"` file-wide, never restored. |

### HIGH — Unfinished edits / leftover prose

| File | Finding |
|---|---|
| `src/detail/webserver_register.cpp:344-349` | TASK-029 block comment **ends mid-sentence** (`"…keeps working at the daemon level, but"` then closing brace). Forgotten edit. |
| `src/webserver.cpp:503-504` | Two orphan comment fragments floating outside any function, leftover from removed logic. |

### HIGH — Public-API gaps disguised as features

| File | Finding |
|---|---|
| `src/httpserver/http_response.hpp:184-196` | `http_response::unauthorized(...)` Digest scheme documented as a **non-RFC-7616-compliant stub** (no nonce/opaque/algorithm/qop). Strict parsers will reject it. No follow-up landed. |
| `src/httpserver/http_response.hpp:159-161` + `src/http_response.cpp:409` + `test/unit/http_response_factories_test.cpp:253-266` | `pipe(int fd, std::size_t size_hint = 0)` — `size_hint` is publicly exposed but ignored; a test pins **"accepted-but-ignored"** as the contract. |
| `src/httpserver/http_response.hpp:304-313` | Structured cookie type "intentionally deferred to a follow-up task". |
| `src/peer_address.cpp:49-50` | RFC 5952 IPv6 zero-compression skipped, "TASK-046 can refine when telemetry firms up". |

### MED — "Future task" forward-debt clusters

| File | Finding |
|---|---|
| `src/hook_handle.cpp:142, 449, 497` + `src/httpserver/detail/webserver_impl.hpp:336, 360` | Five separate "if a future task adds a runtime setter for the alias slots, this MUST take the lock shared" comments. All point at the same missing capability: **runtime re-registration of internal alias slots**. |
| `src/httpserver/detail/webserver_impl.hpp:152-164` | v1 `registered_resources*` maps + mutex deliberately survived TASK-053 cutover; removal is "a separate, larger task". |
| `src/httpserver/detail/connection_state.hpp:122, 130` | Two acknowledged residual gaps (CWE-226 arena-overflow zeroing, CWE-14 `explicit_bzero` vs `memset`). |
| `src/httpserver/detail/http_request_impl.hpp:95-99` | Transitional two-arg ctor preserved "for source compatibility", no removal date. |
| `src/httpserver/create_webserver.hpp:108-157, 549, 558-571` | Entire `namespace compat` shim + `[[deprecated]]` overload + paired `-Wdeprecated-declarations` suppression; "will be removed in the next release". |
| `src/http_resource.cpp:81-115` | `TODO(C++20 cleanup): migrate hook_table_ to std::atomic<std::shared_ptr<T>>` paired with another `-Wdeprecated-declarations` push/pop. |
| `src/detail/webserver_aliases.cpp:298-309` | `install_not_found_alias_` registers an **empty stub callback**, labelled "structurally deferred (TASK-048)". |
| `src/detail/webserver_dispatch.cpp:260, 313-318` | Dead `lambda_handler` variant arm kept "for a future task" that may store lambdas directly. |
| `src/detail/http_request_impl.cpp:202-206` | Acknowledged-deferral (TASK-018) for arena-allocated unescape; warm-path "0-alloc" criterion explicitly does not apply when an unescaper is registered. |
| `src/detail/body.cpp:57, 66` | `static_assert` messages reference TASK-004 memcpy fallback. |

### LOW — Known hazards under build flag

| File | Finding |
|---|---|
| `src/detail/webserver_body_pipeline.cpp:199-208` | `#ifdef DEBUG` block prints raw request body (including passwords) to stdout. Comment explicitly warns it must never be widened to release builds. Real CWE risk, contained. |
| `src/detail/webserver_callbacks.cpp:339-342` | Workaround for upstream libmicrohttpd v0.99 unescape bug, never revisited. |

---

## 2. Tests

### HIGH — The 50 ms sleep cluster (single largest piece of consistent deferred work)

TASK-049 finding #3 introduced `wait_for_server_ready` in
`test/integ/hooks_handler_exception_chain.cpp:53-71` but **the fix was never
propagated** to the following ~20 sibling integ tests, which still use bare
`std::this_thread::sleep_for(std::chrono::milliseconds(50));` as a server-ready
wait:

```
test/integ/hooks_after_handler_mutates_response_in_place.cpp:77
test/integ/hooks_after_handler_replaces_response.cpp:72
test/integ/hooks_accept_decision_throwing.cpp:93, 127
test/integ/hooks_accept_decision_banned.cpp:90
test/integ/hooks_body_chunk_short_circuit_no_leak.cpp:90
test/integ/hooks_body_chunk_observes_progress.cpp:88
test/integ/hooks_before_handler_short_circuit.cpp:82, 128
test/integ/hooks_alias_functional_test.cpp:105
test/integ/hooks_handler_exception_user_handler_throws_continues_chain.cpp:92
test/integ/hooks_handler_exception_fallback_to_hardcoded_500.cpp:108
test/integ/hooks_connection_lifecycle.cpp:126
test/integ/hooks_per_route_early_413_per_endpoint.cpp:132
test/integ/hooks_per_route_concurrent_registration.cpp:120
test/integ/hooks_request_completed_fires_on_early_failure.cpp:95
test/integ/hooks_no_firing.cpp:151
test/integ/hooks_request_received_short_circuit.cpp:105, 163
test/integ/hooks_response_sent_carries_status_bytes_timing.cpp:79
test/integ/hooks_route_resolved_miss_and_hit.cpp:132
test/integ/hooks_per_route_order.cpp:96
```

### HIGH — Tautological-pass pattern in TLS lanes

`test/integ/ws_start_stop.cpp` carries **~20 `try { ws.start() } catch { LT_CHECK_EQ(1,1); return; }` blocks** across SSL, mTLS, PSK, and SNI sections at lines:
759, 789, 914, 949, 1016, 1051, 1091, 1129, 1171, 1212, 1266, 1367, 1486, 1512, 1537, 1552, 1599, 1642, 1685, 1728.

Plus 5 more `if (!has_gnutls_cli()) { LT_CHECK_EQ(1,1); return; }` env-gates at 1550, 1597, 1640, 1683, 1726.

Effect: on hosts lacking GnuTLS or `gnutls-cli`, **the entire TLS suite reduces to `LT_CHECK_EQ(1,1)` passes** — a build that silently lost TLS support would still report green.

### HIGH — Whole-suite Windows / DARWIN skips

| File | Finding |
|---|---|
| `test/integ/threaded.cpp:61-95` | Entire suite body `#ifndef _WINDOWS`. Under Windows `set_up`/`tear_down`/`base` are empty no-ops. Zero coverage. |
| `test/integ/ws_start_stop.cpp:113-1389` | Wide `#ifndef _WINDOWS` over most of the file. No compensating Win-only suite. |
| `test/integ/ws_start_stop.cpp:176-180` + `authentication.cpp:176-180` | `// do not run the digest auth tests on windows… Will fix this separately.` Explicitly postponed. |
| `test/integ/ws_start_stop.cpp:337` | `#ifndef DARWIN` over `custom_socket`, no comment justifying. |

### HIGH — Commented-out test bodies

| File | Finding |
|---|---|
| `test/integ/basic.cpp:821-830, 885-896` | CONNECT-method blocks `/* … */` commented out, no tracking issue. |
| `test/integ/basic.cpp:2895` | `validator_builder` test only asserts the server boots; validator hook is "stored but not currently called in webserver". |

### MED — v2 digest auth: 6 placeholder tests

`test/integ/authentication.cpp:42-60, 245-613` — the `digest_auth*` family
(`digest_auth_wrong_pass`, `digest_auth_with_ha1_md5[_wrong_pass]`,
`digest_auth_with_ha1_sha256[_wrong_pass]`, `digest_user_cache_with_auth`) is
**observationally indistinguishable** under v2 because the nonce/opaque state
machine isn't driven. Retained as static-challenge pins.

### MED — Latency-gate weakening

`test/integ/threadsafety_stress.cpp:756-768` — latency gate loosened from
**10× to 100×** warmup median because of CI-runner noise. Documented but real
loss of regression bite.

### MED — Empty suites on the "wrong" build configuration

| File | Finding |
|---|---|
| `test/unit/webserver_ws_unavailable_test.cpp:30-33` | Entire suite **empty on HAVE_WEBSOCKET-on builds**. |
| `test/unit/webserver_dauth_unavailable_test.cpp:33-37` | Entire suite **empty on HAVE_DAUTH-on builds**. |
| `test/unit/header_hygiene_test.cpp:67-118` | pthread leak detector **disabled on both libc++ and libstdc++** (every CI lane). Effective coverage near-zero. |
| `test/unit/webserver_register_ws_smartptr_test.cpp` | Runtime half is `#ifdef HAVE_WEBSOCKET`. |
| `test/unit/http_request_operator_stream_test.cpp:57-140` | Credential-redaction tests live entirely behind `#ifdef HAVE_BAUTH`. |
| `test/unit/body_test.cpp:198-291`, `http_response_factories_test.cpp:235-267`, `iovec_entry_test.cpp:85-100` | `#ifndef _WIN32` gates skip pipe/iovec body tests on Windows. |
| `test/unit/create_test_request_test.cpp:506-538`, `create_webserver_test.cpp:140-146, 636-712`, `create_test_request_test.cpp:159-211, 332-348` | Multiple HAVE_BAUTH / HAVE_DAUTH / HAVE_GNUTLS gates create matrix-only coverage. |

### MED — v2 dispatch cutover safety net

`test/unit/v2_dispatch_contract_test.cpp:36-43` — explicitly states v1
dispatch is still authoritative; suite is a safety net for a not-yet-completed
cutover (TASK-036).

### MED — Bench infrastructure with manual-only gates

| File | Finding |
|---|---|
| `test/bench_hook_overhead.cpp:121-128` | Relative `2×-of-HOOK_BASELINE_NS` gate the task spec asked for was **punted to "future work"**; absolute 50 ns ceiling used instead. |
| `test/bench_warm_path.cpp:45-46, 278-280` | **No pass/fail ceilings at all**, manual before/after comparison. |
| `test/bench_route_lookup.cpp:222-227` | Measures a "cache-warm + radix" mix rather than pure radix-tier latency. |
| `test/bench_harness.hpp:58-72` | MSVC sink known to be elidable; robust `_ReadWriteBarrier()` alternative punted. |

### MED — v1 baseline never re-measured

`test/v1_baseline/v1_constants.hpp:65-70, 94-98` + `test/PERFORMANCE.md:34` —
the libstdc++/Linux v1 `get_headers()` ns/call constant was **never
re-measured**; the libc++ value (760 ns) is reused on both stdlibs. Sizeof
constants are per-stdlib; only the ns/call constant is mono-platform.

### MED — Other test smells

| File | Finding |
|---|---|
| `test/Makefile.am:67-74` | Orphaned comment claims `header_hygiene` is in `XFAIL_TESTS`; lines 532-535 admit it was removed when TASK-020 landed. **Stale, update.** |
| `test/Makefile.am:252-253` | route_table_concurrency TSan run is manual-only, never wired into CI. |
| `test/Makefile.am:264-267` | `stop()`-from-handler deadlock test gated behind `HTTPSERVER_RUN_STOP_FROM_HANDLER=1`, skipped by default in CI. |
| `test/unit/auth_handler_optional_signature_test.cpp:176-192` | `throwing_handler_is_swallowed_and_request_passes` ratifies a questionable swallow-and-pass behaviour as the pin. |
| `test/unit/hooks_log_access_alias_slot_test.cpp:166-205` | "Second registration replaces first" test only simulates re-registration via two webservers (no real runtime setter exists). |
| `test/unit/webserver_register_smartptr_test.cpp:60-64, 148-156` | Known parallel-runner fragility documented but not fixed. |
| `test/unit/http_resource_test.cpp:51-60` | sizeof gate loose at 256 bytes; authoritative check delegated to a bench. |
| `test/unit/webserver_pimpl_test.cpp:42-52` | pimpl size bound at 1152 bytes, "TASK-019/020 will tighten". |

### LOW — tsan.supp is clean

Only 2 entries, both narrow libstdc++ symbols (`std::ctype<char>::narrow`,
`_M_narrow_init`) for upstream gcc bug #58938. No wildcards, no libhttpserver
symbols. **Contrast with valgrind: the 6 wildcard valgrind suppressions from
commit `87185ea` were correctly removed by `c26c373` after the underlying UAF
was root-caused.**

### LOW — Misc

- `test/littletest.hpp:21` — decade-old `//TODO: personalized messages` (vendored library).
- `test/integ/route_table_concurrency.cpp:93-97, 182-185` — handler-thread writer ops wrapped in `catch(...) {}`, documented as "race-on-duplicate permitted under stress".

---

## 3. CI workflows & scripts

### HIGH — Residual TEMP-BUMP

`scripts/check-file-size.sh:56-60` — `FILE_LOC_MAX="${FILE_LOC_MAX:-750}"`
(long-term target **500**). Documented split roadmap in comment block lines 26-46:

| File | LOC today | Planned split |
|---|---|---|
| `create_webserver.hpp` | 712 | ~400 + ~300 |
| `hook_handle.cpp` | 572 | ~430 |
| `webserver_routes.cpp` | 547 | ~480 |
| `http_request.hpp` | 583 | ~400 + ~250 |

**Not executed.** Single residual TEMP-BUMP from commit `87185ea`.

### HIGH — Coverage holes in CI matrix

| Location | Finding |
|---|---|
| `.github/workflows/verify-build.yml:75-85` | msan matrix entry **commented out** (Ubuntu 18.04 / clang-6.0 retired); no replacement. asan/lsan/tsan/ubsan all wired up, **msan silently gone**. |
| `verify-build.yml:766` | Valgrind lane configures `--disable-valgrind-helgrind --disable-valgrind-drd --disable-valgrind-sgcheck`. **Only memcheck runs**; helgrind/drd race detectors and sgcheck stack-overrun checks never execute. |
| `verify-build.yml:382-388` | Windows lane intentionally excludes doxygen+graphviz; doxygen invariant runs Linux-only. |
| `verify-build.yml:982` | `fail_ci_if_error: false` on Codecov upload — coverage failures are non-blocking. |
| `.github/workflows/codeql-analysis.yml` | Uses deprecated/unpinned `@v1`/`@v2` actions; commented-out Autobuild scaffolding never replaced. CodeQL job not hardened to match `verify-build.yml`'s SHA-pinning. |

### HIGH — Acceptance gate not wired into per-PR CI

`scripts/check-parallel-install.sh:36-38` explicitly opt-in only via
`make check-parallel-install`. And the gate **degrades to `SKIP` (exit 0)
on five distinct failure paths**:

| Line | Failure mode |
|---|---|
| 154 | master ref missing |
| 162 | git worktree add failed |
| 170 | v1 bootstrap failed |
| 199 | v1 configure failed |
| 207 | v1 make failed |

Any environment quirk silently no-ops the gate.

### MED — Soft degradations

| Location | Finding |
|---|---|
| `scripts/check-soversion.sh:193, 210, 249` | SONAME assertions degrade to filename-only when `readelf`/`otool` is absent; pkg-config check degrades to literal Version-line grep. |
| `scripts/check-readme.sh:321-331` + `check-release-notes.sh:348-353` | markdownlint findings are **advisory-only**; only readme has a STRICT knob (defaults to "no"). |
| `scripts/check-readme.sh:273` | `RELEASE_NOTES.md) continue ;;  # created by TASK-042, not yet present` — **stale residue**: TASK-042 has shipped. |
| `scripts/check-readme.sh:294-297` | Known fence-balance gap not fixed (two consecutive opening fences + two closing fences pass the even-count check). |
| `scripts/check-release-notes.sh:107-109, 248-254` | Intentionally partial coverage of the `no_*` setter family (spot-checked, not enumerated). |
| `scripts/check-examples.sh:120` | `client_cert_auth` allow-listed out of the noinst_PROGRAMS coverage check. |
| `scripts/check-parallel-install.sh:55` + `scripts/check-soversion.sh:48` | `set -uo pipefail` (no `-e`); opt-out of fail-on-error. |

### LOW — Clean

- `release.yml` & `verify-build.yml` — **zero `continue-on-error: true`**, zero `-Wno-*`, zero `paths-ignore`, zero CodeQL/SARIF suppressions, zero ASAN/LSAN weakening. The `if: failure()` blocks are diagnostic-only.
- `.codacy.yaml` — excludes `specs/**` and `test/*.md` paths only; no rules disabled.
- `scripts/check-complexity.sh` — threshold = 10, comment explicitly forbids raising.
- `scripts/check-duplication.sh` — threshold = 100 (PMD default).

---

## 4. Examples — clean

Zero raw TODO/FIXME/HACK, zero `#if 0`, zero `detail/` includes, zero
hardcoded `/tmp` paths. All use v2 idioms uniformly.

The few "in production…" caveats are acknowledged simplifications with
security/portability guidance:

| File | Caveat |
|---|---|
| `examples/per_route_auth.cpp:45-49` | Non-constant-time password compare (notes the production fix). |
| `examples/pipe_response_example.cpp:54-57` | No write-loop hardening (notes production must). |
| `examples/client_cert_auth.cpp:65` | In-memory allowlist (notes DB/config in production). |
| `examples/centralized_authentication.cpp:51` | Env-var read on every request (notes load-once in production). |
| `examples/minimal_https_psk.cpp:35` | Illustrative PSK values, production guidance below. |
| `examples/clf_access_log.cpp:102` | Hard-coded `HTTP/1.1` (ctx does not expose protocol). |

These could be tightened further but are defensible for examples.

---

## Cross-cutting patterns

1. **TODO/FIXME hygiene is excellent.** The codebase tracks deferral via
   TASK-NNN references and explicit "acknowledged deferral" prose rather than
   raw markers. Only one stray `TODO` (in vendored `littletest.hpp`).
2. **Biggest single piece of work:** propagate `wait_for_server_ready` from
   `hooks_handler_exception_chain.cpp` to the 20 sibling integ tests.
3. **Weakest coverage area:** TLS. `ws_start_stop.cpp` reduces to
   `LT_CHECK_EQ(1,1)` passes when GnuTLS or `gnutls-cli` is absent, and
   Windows has zero coverage for these features.
4. **v2 Digest auth is feature-deleted.** 6 integ tests retained as static
   pins, plus `http_response::unauthorized` documented as a non-RFC-compliant
   stub.
5. **One unfinished edit** at `src/detail/webserver_register.cpp:344-349`.
6. **Two undocumented file-scoped `-Warray-bounds` suppressions**
   (`http_utils.cpp:62`, `detail/ip_representation.cpp:55`) — no scope,
   no rationale, no pop.
7. **One residual TEMP-BUMP** in CI: file-size ceiling 500 → 750. Companion
   valgrind wildcard suppressions were properly removed.
8. **Three silent CI gaps:** msan lane gone, helgrind/drd/sgcheck disabled,
   parallel-install gate not in per-PR CI.

## Proposed disposition (next steps)

A separate prioritization sweep should slot the **HIGH** items into either
the existing v2.0 release scope or an explicit v2.1 backlog. The **MED**
items are mostly already tracked by TASK-NNN references in their source
comments; cross-reference and confirm. The **LOW** items can stay where
they are.

Two items are strictly mechanical and could be cleared in a single PR:

- Fix the unfinished sentence at `src/detail/webserver_register.cpp:344-349`.
- Drop the orphan comment at `src/webserver.cpp:503-504`.
- Update the stale `XFAIL_TESTS` comment at `test/Makefile.am:67-74`.
- Drop the stale `RELEASE_NOTES.md) continue` at `scripts/check-readme.sh:273`.

The two unscoped `-Warray-bounds` suppressions should either be scoped to
the minimum offending line range with a `push`/`pop` and a comment, or
investigated and removed.
