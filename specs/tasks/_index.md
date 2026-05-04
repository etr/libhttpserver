# libhttpserver v2.0 — Task Plan

**Status:** Draft 1
**Last updated:** 2026-04-30
**Owner:** Sebastiano Merlino
**Inputs:** [specs/product_specs.md](../product_specs.md), [specs/architecture/](../architecture/)

---

## Overview

44 tasks across 6 milestones implementing the v2.0 clean-cutover release. The v2.0 cutover is single-shot (no Alpha→Beta→GA phasing per PRD §1), so milestones are technical layers that each leave the public API in a compilable state and exercise an outcome a downstream consumer would care about. There is no parallel maintenance branch — v1.x is end-of-life on the day v2.0 ships (DR-011, OQ-007).

## Milestones

| ID | Name | Outcome | Tasks |
|---|---|---|---|
| M1 | Foundation | C++20 floor, header layout & guards, primitive types (`http_method`, `method_set`), `feature_unavailable`, `iovec_entry`, `httpserver::constants`, header-hygiene CI gate. After M1 the library still functions as v1 — additive only. | TASK-001 .. TASK-007 |
| M2 | Response Refactor | `http_response` is a value type with SBO body, factories, fluent `with_*` chains, const-correct getters. Public `*_response` subclasses gone. After M2 a downstream consumer can build & chain a response. | TASK-008 .. TASK-013 |
| M3 | Webserver internal & Request Refactor | `webserver_impl` and `http_request_impl` PIMPL split; per-connection arena allocator; `const&` / `string_view` getters; high-level GnuTLS accessors. Public headers are free of `<microhttpd.h>`, `<pthread.h>`, `<gnutls/gnutls.h>`, `<sys/socket.h>`. | TASK-014 .. TASK-020 |
| M4 | Handler & Resource Model | `http_resource` allow-mask via `method_set`, snake_case `render_*`, smart-pointer registration, `register_path`/`register_prefix`, lambda `on_*`, generic `route()`. After M4 a consumer can register handlers in either form. | TASK-021 .. TASK-026 |
| M5 | Routing, Lifecycle, Builder & Features | 3-tier route table (hash + radix + regex) with LRU cache, v1-corpus regression gate, name canonicalization (`stop_and_wait`, `block_ip`/`unblock_ip`, `_handler` suffix), error-propagation contract, thread-safety stress test, builder cleanup, `features()`, websocket smart-pointer overloads, handler return-by-value dispatch cutover. After M5 the library is feature-complete. | TASK-027 .. TASK-036 |
| M6 | Release Readiness | Build-flag-invariance CI test, sanitizer move tests, performance acceptance (`get_headers` ≥10×, `sizeof(http_resource)` shrink), examples (≤10 LOC hello world), README rewrite, RELEASE_NOTES.md, Doxygen refresh, SOVERSION bump 1→2, packaging. | TASK-037 .. TASK-044 |

## Dependency graph

```
M1: Foundation
└── 001 [C++20] ──→ 002 [headers/guards] ──┬──→ 003 [feature_unavailable]
                                            ├──→ 004 [iovec_entry]
                                            ├──→ 005 [http_method/method_set]
                                            ├──→ 006 [constants]
                                            └──→ 007 [hygiene CI test]

M2: Response Refactor (can begin once 002 lands)
└── 008 [detail::body] ──→ 009 [http_response value+SBO] ──┬──→ 010 [factories]
                                                            ├──→ 011 [const accessors]
                                                            ├──→ 012 [fluent setters]
                                                            └──→ 013 [remove subclasses]

M3: Webserver internal & Request Refactor (can begin once 002 lands)
└── 014 [webserver_impl skeleton] ──→ 015 [http_request_impl skeleton] ──→ 016 [arena]
                                                                            ├──→ 017 [const& getters]
                                                                            ├──→ 018 [string_view getters]
                                                                            └──→ 019 [GnuTLS accessors]
                                                                                    └──→ 020 [final hygiene sweep]

M4: Handler & Resource Model (depends on M1 005 + M2 009 + M3 014)
└── 021 [method_set on http_resource] ──→ 022 [snake_case render_*] ─┐
    023 [smart-ptr register_resource] ──→ 024 [register_path/prefix] ┤
                                                       025 [on_*] ───┼──→ 026 [route()]
                                                                      │
M5: Routing, Lifecycle, Builder & Features
└── 027 [3-tier route table] ──→ 028 [v1 routing-corpus regression]
    029 [stop_and_wait + block_ip] (depends on 014)
    030 [_handler suffix + explicit] (depends on 014)
    031 [error propagation] (depends on 027, 030)
    032 [thread-safety stress test] (depends on 027, 031)
    033 [create_webserver cleanup] (depends on 006, 014)
    034 [features() + flag-independence] (depends on 003, 019, 033)
    035 [websocket smart-ptr] (depends on 014, 034)
    036 [handler return-by-value dispatch] (depends on 022, 025, 027, 031)

M6: Release Readiness
└── 037 [build-flag invariance CI] (depends on 034)
    038 [sanitizer move tests] (depends on 009, 036)
    039 [performance acceptance] (depends on 017, 018, 021)
    040 [examples] (depends on 025, 036) ──→ 041 [README] ──→ 042 [RELEASE_NOTES] ──→ 043 [Doxygen] ──→ 044 [SOVERSION bump]
```

## Critical path

The longest dependency chain (each link representing a true blocker, not just a milestone boundary):

```
001 → 002 → 014 → 015 → 016 → 027 → 028 → 036 → 040 → 041 → 042 → 043 → 044
(C++20 → headers → webserver_impl → request_impl → arena → route table → routing regression → return-by-value → examples → README → RELEASE_NOTES → Doxygen → SOVERSION)
```

Nominally: **13 sequential tasks**, each S–XL. Most other tasks parallelize off this spine — M2 (response) is fully independent of M3 (request) once TASK-002 lands, M4 fans out from M1 + M2 + early M3, and M6's documentation and tests can start mid-M5 once their respective inputs are available.

## Task Status

| # | Task | Milestone | Status | Blocked by |
|---|------|-----------|--------|------------|
| TASK-001 | Bump C++ standard floor to C++20 | M1 | In Progress | None |
| TASK-002 | Public/private header layout and inclusion guards | M1 | Done | TASK-001 |
| TASK-003 | Add `httpserver::feature_unavailable` exception type | M1 | Done | TASK-002 |
| TASK-004 | Library-defined `iovec_entry` POD with layout-pinning asserts | M1 | Done | TASK-002 |
| TASK-005 | Add `http_method` enum and `method_set` bitmask | M1 | Done | TASK-002 |
| TASK-006 | Replace `#define` constants with `httpserver::constants` | M1 | Done | TASK-002 |
| TASK-007 | CI test for public-header hygiene | M1 | Done | TASK-002 |
| TASK-008 | Internal `detail::body` hierarchy | M2 | Done | TASK-002 |
| TASK-009 | `http_response` value type with SBO buffer | M2 | Done | TASK-008 |
| TASK-010 | `http_response` factory functions | M2 | Done | TASK-008, TASK-009, TASK-004 |
| TASK-011 | `http_response` const-correct accessors | M2 | Done | TASK-009 |
| TASK-012 | `http_response` fluent `with_*` setters | M2 | Done | TASK-009 |
| TASK-013 | Remove `*_response` subclasses and dispatch virtuals | M2 | Done | TASK-009, TASK-010, TASK-011, TASK-012 |
| TASK-014 | `webserver_impl` skeleton (PIMPL prep) | M3 | In Progress | TASK-002 |
| TASK-015 | `http_request_impl` skeleton (PIMPL split) | M3 | Not Started | TASK-002, TASK-014 |
| TASK-016 | Per-connection arena for `http_request_impl` | M3 | Not Started | TASK-014, TASK-015 |
| TASK-017 | `http_request` container getters return `const&` | M3 | Not Started | TASK-015 |
| TASK-018 | `http_request` single-key getters return `string_view`, all const | M3 | Not Started | TASK-015, TASK-016 |
| TASK-019 | High-level GnuTLS accessors replacing `gnutls_session_t` | M3 | Not Started | TASK-015 |
| TASK-020 | Final public-header backend-include sweep | M3 | Not Started | TASK-014, TASK-015, TASK-019 |
| TASK-021 | `http_resource` allow-mask via `method_set` | M4 | Not Started | TASK-005 |
| TASK-022 | Snake_case `render_*` overrides on `http_resource` | M4 | Not Started | TASK-021 |
| TASK-023 | Smart-pointer `register_resource` overloads | M4 | Not Started | TASK-014 |
| TASK-024 | `register_path` and `register_prefix` (replace `bool family`) | M4 | Not Started | TASK-023 |
| TASK-025 | Lambda handler entry points `on_*` | M4 | Not Started | TASK-005, TASK-009, TASK-014 |
| TASK-026 | Generic `webserver::route(method, path, handler)` | M4 | Not Started | TASK-005, TASK-025 |
| TASK-027 | 3-tier route table with LRU cache | M5 | Not Started | TASK-005, TASK-014, TASK-021, TASK-024, TASK-025, TASK-026 |
| TASK-028 | Routing-semantics regression gate | M5 | Not Started | TASK-027 |
| TASK-029 | Naming consistency — `stop_and_wait`, `block_ip`/`unblock_ip` | M5 | Not Started | TASK-014 |
| TASK-030 | `_handler` suffix renames + `explicit` constructor | M5 | Not Started | TASK-014 |
| TASK-031 | Handler error-propagation contract (DR-009) | M5 | Not Started | TASK-027, TASK-030 |
| TASK-032 | Thread-safety contract stress test (DR-008) | M5 | Not Started | TASK-027, TASK-031 |
| TASK-033 | `create_webserver` builder cleanup | M5 | Not Started | TASK-006, TASK-014 |
| TASK-034 | Build-flag-independent public API + `webserver::features()` | M5 | Not Started | TASK-003, TASK-019, TASK-033 |
| TASK-035 | Smart-pointer `register_ws_resource` overloads | M5 | Not Started | TASK-014, TASK-034 |
| TASK-036 | Handler return-by-value dispatch cutover | M5 | Not Started | TASK-022, TASK-025, TASK-027, TASK-031 |
| TASK-037 | CI test for build-flag invariance | M6 | Not Started | TASK-034 |
| TASK-038 | Sanitizer-clean tests for `http_response` move semantics | M6 | Not Started | TASK-009, TASK-036 |
| TASK-039 | Performance acceptance (`get_headers`, `sizeof(http_resource)`) | M6 | Not Started | TASK-017, TASK-018, TASK-021 |
| TASK-040 | Rewrite `examples/` | M6 | Not Started | TASK-025, TASK-036 |
| TASK-041 | Rewrite `README.md` | M6 | Not Started | TASK-031, TASK-032, TASK-040 |
| TASK-042 | Write `RELEASE_NOTES.md` for v2.0 | M6 | Not Started | TASK-041 |
| TASK-043 | Doxygen / inline doc refresh | M6 | Not Started | TASK-031, TASK-034, TASK-041 |
| TASK-044 | SOVERSION bump and packaging | M6 | Not Started | TASK-042, TASK-043 |

## PRD requirement coverage

Each PRD EARS requirement maps to one or more tasks below.

| PRD ID | Tasks |
|---|---|
| PRD-HDR-REQ-001 (no `<microhttpd.h>`) | TASK-002, TASK-014, TASK-015, TASK-020, TASK-007 |
| PRD-HDR-REQ-002 (no `<pthread.h>`/`<sys/socket.h>`) | TASK-002, TASK-014, TASK-020, TASK-007 |
| PRD-HDR-REQ-003 (no `<gnutls/gnutls.h>`) | TASK-019, TASK-020, TASK-007 |
| PRD-HDR-REQ-004 (PIMPL — exempts `http_response`) | TASK-014, TASK-015 (positive rule); TASK-009 (exemption clause: `http_response` stays non-PIMPL) |
| PRD-HDR-REQ-005 (remove dispatch virtuals) | TASK-013 |
| PRD-FLG-REQ-001 (no `#ifdef HAVE_*`) | TASK-034, TASK-037 |
| PRD-FLG-REQ-002 (sentinel/throw) | TASK-019, TASK-031, TASK-034, TASK-035 |
| PRD-FLG-REQ-003 (`features()`) | TASK-034 |
| PRD-FLG-REQ-004 (error names feature + flag) | TASK-003, TASK-034 |
| PRD-FLG-REQ-005 (`feature_unavailable` from `runtime_error`) | TASK-003 |
| PRD-CFG-REQ-001 (`bool` setter form) | TASK-033 |
| PRD-CFG-REQ-002 (`constexpr` constants) | TASK-006, TASK-033 (verifies `create_webserver.hpp` carries no `#define`) |
| PRD-CFG-REQ-003 (validate + throw) | TASK-033 |
| PRD-CFG-REQ-004 (no `no_*` setters) | TASK-033 |
| PRD-HDL-REQ-001 (handler signature) | TASK-025, TASK-036 |
| PRD-HDL-REQ-002 (`on_*` entry points) | TASK-025, TASK-027 |
| PRD-HDL-REQ-003 (smart-ptr registration) | TASK-023, TASK-035 |
| PRD-HDL-REQ-004 (`register_prefix` not `bool family`) | TASK-024 |
| PRD-HDL-REQ-005 (no raw-pointer registration) | TASK-023, TASK-035 |
| PRD-HDL-REQ-006 (`route(method, path, handler)`) | TASK-005, TASK-026 |
| PRD-RSP-REQ-001 (factory by value) | TASK-009, TASK-010 |
| PRD-RSP-REQ-002 (no mutating accessors) | TASK-011 |
| PRD-RSP-REQ-003 (no insert-on-miss) | TASK-011 |
| PRD-RSP-REQ-004 (fluent return) | TASK-012 |
| PRD-RSP-REQ-005 (`unauthorized` factory) | TASK-010 |
| PRD-RSP-REQ-006 (no `*_response` classes) | TASK-013 |
| PRD-RSP-REQ-007 (handler returns by value) | TASK-009, TASK-036 |
| PRD-REQ-REQ-001 (`const&` getters) | TASK-017, TASK-018; TASK-039 (numeric §3.6 acceptance: ≥10× `get_headers()` speedup) |
| PRD-REQ-REQ-002 (`is_allowed` const) | TASK-021 |
| PRD-REQ-REQ-003 (bitmask method state) | TASK-005, TASK-021; TASK-039 (numeric §3.6 acceptance: `sizeof(http_resource)` shrink) |
| PRD-NAM-REQ-001 (snake_case) | TASK-022, TASK-029 |
| PRD-NAM-REQ-002 (one canonical verb) | TASK-029 |
| PRD-NAM-REQ-003 (`_handler` suffix) | TASK-030 |
| PRD-NAM-REQ-004 (`explicit` ctor) | TASK-030 |
| PRD-NAM-REQ-005 (`block_ip`/`unblock_ip` only) | TASK-029 |

## Decision-record coverage

| DR | Tasks |
|---|---|
| DR-001 (C++20 floor) | TASK-001 |
| DR-002 (header layout) | TASK-002, TASK-014, TASK-015 |
| DR-003a (no PIMPL `http_response`) | TASK-009 |
| DR-003b (PIMPL `webserver`/`http_request`) | TASK-014, TASK-015, TASK-016 |
| DR-004 (handler return by value) | TASK-025, TASK-036 |
| DR-005 (SBO body) | TASK-008, TASK-009, TASK-038 |
| DR-006 (`http_method`/`method_set`) | TASK-005, TASK-021 |
| DR-007 (3-tier route table) | TASK-027, TASK-028 |
| DR-008 (thread-safety contract) | Implements: TASK-027 (shared_mutex), TASK-032 (stress test). Documents: TASK-041, TASK-043 |
| DR-009 (error-propagation contract) | Implements: TASK-031. Documents: TASK-041, TASK-043 |
| DR-010 (deferred / WS lifecycle) | TASK-035, TASK-036 |
| DR-011 (SOVERSION-only versioning) | TASK-044 |
