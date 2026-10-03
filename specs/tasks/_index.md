# libhttpserver — Task Plans

**Status:** v2 historical plan; v3.0 task plan approved for implementation
**Last updated:** 2026-09-29
**Owner:** Sebastiano Merlino
**Inputs:** [product specs](../product_specs.md), [architecture](../architecture/)

---

## v3.0 plan

90 tasks, TASK-096–TASK-185, across M8–M13. M1–M7 and TASK-001–TASK-095 remain the v2 plan. All v3 tasks start as `Not Started`. Estimates are S ≈ 1 day, M ≈ 2 days, L ≈ 3 days of focused implementation and tests; interoperability work may take longer.

### v3 integration branch

The base and integration branch for every TASK-096–TASK-185 is **`v3`**. Before executing a task, confirm that `v3` exists, contains the approved v3 specs and task files, and is the checked-out base branch. Create the task's branch and worktree from `v3`; validate and merge the completed task back into `v3`. If a Groundwork workflow reports another `base_branch`, stop before implementation or merge and correct the branch context. Integration of `v3` into `main` is a separate release decision.

### v3 milestones

| ID | Outcome | Tasks |
|---|---|---:|
| [M8: Native HTTP/1](M8-native-http1/) | A TLS-off application serves HTTP/1.0 and HTTP/1.1 with native sockets, bounded streaming, routing, and handler-safe shutdown. | 15 |
| [M9: HTTP/1 parity and WebSockets](M9-http1-parity-websocket/) | Owned HTTP/1 matches applicable v2 behavior, serves WebSockets, and can run in an application-supplied event loop. | 18 |
| [M10: TLS and HTTP/2](M10-tls-http2/) | OpenSSL-backed TLS, live credentials and ACME serve multiplexed HTTP/2 and WebSockets on the same route model. | 20 |
| [M11: First HTTP/3 slice](M11-http3-first-slice/) | Owned QUIC v1 and static-QPACK HTTP/3 serve bounded GET/POST through the shared exchange. | 15 |
| [M12: Full HTTP/3](M12-http3-full/) | QUIC recovery and migration, dynamic QPACK, H3 drain, and WebSockets over H3 pass independent-client gates. | 15 |
| [M13: v3.0 release](M13-v3-release/) | Parity, cross-platform packages, resource limits, documentation, and all protocol/security release gates pass. | 7 |

### v3 dependency graph

```text
M8  096 → 097 → 098 → 099 → 101 → 102 → 103 → 106 → 108 [native HTTP/1]
                    099 → 100 ────────────────────────────────┘
                    102 → 104 → 107 ──────────────────────────┘
M9  108 → 111–120 [parity]; 121 → 122 → 123 [WebSocket]; 124–127 [I/O]
M10 108 → 129 → 130 → 131 → 132 [TLS]; 137 → 138 → 139–145 [HTTP/2]
                    131 → 133–136 [mTLS/PSK/ACME]; 146–148 [IOCP/audit]
M11 149–158 [owned QUIC]; 159 [static QPACK] → 160–163 [first H3 slice]
M12 164–178 [QUIC/H3 recovery, migration, dynamic QPACK, WebSocket]
M13 179–184 [migration, package, platform gates] → 185 [release gate]
```

The graph shows the main tracks; each task file and the status table below give exact blockers. The longest path by task count has 27 tasks:

`096 → 097 → 098 → 099 → 101 → 102 → 103 → 106 → 108 → 129 → 130 → 131 → 132 → 155 → 156 → 157 → 158 → 164 → 166 → 170 → 172 → 175 → 178 → 179 → 183 → 184 → 185`.

### v3 PRD coverage

| Requirement | Tasks |
|---|---|
| PRD-V3N-REQ-001 | TASK-108, TASK-148, TASK-182, TASK-184, TASK-185 |
| PRD-V3N-REQ-002 | TASK-108, TASK-114, TASK-115, TASK-129, TASK-148, TASK-182, TASK-185 |
| PRD-V3N-REQ-003 | TASK-129, TASK-148, TASK-152, TASK-182, TASK-184, TASK-185 |
| PRD-V3N-REQ-004 | TASK-100, TASK-105, TASK-106, TASK-107, TASK-108, TASK-120, TASK-128, TASK-185 |
| PRD-V3N-REQ-005 | TASK-129, TASK-130, TASK-132, TASK-137, TASK-138, TASK-139, TASK-140, TASK-145, TASK-185 |
| PRD-V3N-REQ-006 | TASK-141, TASK-142, TASK-145, TASK-185 |
| PRD-V3N-REQ-007 | TASK-129, TASK-149, TASK-150, TASK-151, TASK-152, TASK-153, TASK-154, TASK-155, TASK-156, TASK-159, TASK-160, TASK-161, TASK-162, TASK-163, TASK-164, TASK-165, TASK-166, TASK-167, TASK-168, TASK-173, TASK-174, TASK-176, TASK-177, TASK-178, TASK-185 |
| PRD-V3N-REQ-008 | TASK-149, TASK-150, TASK-151, TASK-153, TASK-154, TASK-156, TASK-157, TASK-158, TASK-160, TASK-161, TASK-163, TASK-164, TASK-165, TASK-166, TASK-168, TASK-169, TASK-173, TASK-174, TASK-176, TASK-177, TASK-178, TASK-185 |
| PRD-V3N-REQ-009 | TASK-101, TASK-102, TASK-108, TASK-111, TASK-118, TASK-140, TASK-161, TASK-162, TASK-174, TASK-183 |
| PRD-V3N-REQ-010 | TASK-122, TASK-128 |
| PRD-V3N-REQ-011 | TASK-144, TASK-145, TASK-171, TASK-172, TASK-175, TASK-178 |
| PRD-V3N-REQ-012 | TASK-121, TASK-122, TASK-128, TASK-144, TASK-171, TASK-172, TASK-175, TASK-178 |
| PRD-V3N-REQ-013 | TASK-121, TASK-122, TASK-123, TASK-128, TASK-144, TASK-172, TASK-175, TASK-178 |
| PRD-V3N-REQ-014 | TASK-099, TASK-100, TASK-101, TASK-119, TASK-126, TASK-127, TASK-139, TASK-146, TASK-180, TASK-181 |
| PRD-V3N-REQ-015 | TASK-124, TASK-125, TASK-182 |
| PRD-V3N-REQ-016 | TASK-099, TASK-101, TASK-124, TASK-139, TASK-180 |
| PRD-V3N-REQ-017 | TASK-097, TASK-105, TASK-106, TASK-137, TASK-138, TASK-140, TASK-159, TASK-167, TASK-169, TASK-177 |
| PRD-V3N-REQ-018 | TASK-097, TASK-138 |
| PRD-V3N-REQ-019 | TASK-097, TASK-105 |
| PRD-V3N-REQ-020 | TASK-097, TASK-140 |
| PRD-V3N-REQ-021 | TASK-103, TASK-106, TASK-111, TASK-116, TASK-117, TASK-141, TASK-154, TASK-157, TASK-161, TASK-180 |
| PRD-V3N-REQ-022 | TASK-103, TASK-111, TASK-116 |
| PRD-V3N-REQ-023 | TASK-102, TASK-106, TASK-109, TASK-118 |
| PRD-V3N-REQ-024 | TASK-098, TASK-102, TASK-109 |
| PRD-V3N-REQ-025 | TASK-098, TASK-102, TASK-103, TASK-109, TASK-117, TASK-141, TASK-142, TASK-157, TASK-176 |
| PRD-V3N-REQ-026 | TASK-104, TASK-107, TASK-112, TASK-141, TASK-161 |
| PRD-V3N-REQ-027 | TASK-104, TASK-107, TASK-141, TASK-142, TASK-157, TASK-158, TASK-168, TASK-169, TASK-177, TASK-180 |
| PRD-V3N-REQ-028 | TASK-112, TASK-113 |
| PRD-V3N-REQ-029 | TASK-112, TASK-113 |
| PRD-V3N-REQ-030 | TASK-112 |
| PRD-V3N-REQ-031 | TASK-098, TASK-110, TASK-123, TASK-183 |
| PRD-V3N-REQ-032 | TASK-110, TASK-123, TASK-143, TASK-166, TASK-170 |
| PRD-V3N-REQ-033 | TASK-123, TASK-144, TASK-172 |
| PRD-V3N-REQ-034 | TASK-129, TASK-130, TASK-131, TASK-132, TASK-133, TASK-134, TASK-135, TASK-147, TASK-155 |
| PRD-V3N-REQ-035 | TASK-131, TASK-132, TASK-147 |
| PRD-V3N-REQ-036 | TASK-136, TASK-147 |
| PRD-V3N-REQ-037 | TASK-097, TASK-104, TASK-129, TASK-148, TASK-181, TASK-183, TASK-184 |
| PRD-V3N-REQ-038 | TASK-096, TASK-114, TASK-115, TASK-116, TASK-117, TASK-118, TASK-119, TASK-120, TASK-128, TASK-164, TASK-179, TASK-183, TASK-185 |

### v3 decision coverage

| Decision | Tasks |
|---|---|
| DR-V3-001 | TASK-096, TASK-097, TASK-101, TASK-105, TASK-106, TASK-107, TASK-108, TASK-114, TASK-115, TASK-116, TASK-117, TASK-118, TASK-119, TASK-120, TASK-121, TASK-122, TASK-128, TASK-137, TASK-138, TASK-139, TASK-140, TASK-141, TASK-142, TASK-144, TASK-145, TASK-150, TASK-151, TASK-152, TASK-153, TASK-154, TASK-156, TASK-157, TASK-158, TASK-159, TASK-160, TASK-161, TASK-162, TASK-163, TASK-164, TASK-165, TASK-167, TASK-168, TASK-169, TASK-171, TASK-173, TASK-174, TASK-175, TASK-176, TASK-177, TASK-178, TASK-179, TASK-180, TASK-181, TASK-182, TASK-183, TASK-184, TASK-185 |
| DR-V3-002 | TASK-129, TASK-130, TASK-131, TASK-132, TASK-133, TASK-134, TASK-135, TASK-136, TASK-147, TASK-148, TASK-155 |
| DR-V3-003 | TASK-098, TASK-102, TASK-103, TASK-104, TASK-109, TASK-111, TASK-121, TASK-122, TASK-144, TASK-171 |
| DR-V3-004 | TASK-099, TASK-100, TASK-124, TASK-125, TASK-126, TASK-127, TASK-146, TASK-149 |
| DR-V3-005 | TASK-112, TASK-113 |
| DR-V3-006 | TASK-105, TASK-106, TASK-107, TASK-108, TASK-137, TASK-138, TASK-139, TASK-140, TASK-141, TASK-142, TASK-150, TASK-151, TASK-152, TASK-153, TASK-154, TASK-156, TASK-157, TASK-158, TASK-159, TASK-160, TASK-161, TASK-164, TASK-165, TASK-167, TASK-168, TASK-169 |
| DR-V3-007 | TASK-129, TASK-130, TASK-131, TASK-132, TASK-133, TASK-134, TASK-135, TASK-136, TASK-147, TASK-148, TASK-155 |
| DR-V3-008 | TASK-110, TASK-123, TASK-143, TASK-166, TASK-170, TASK-172 |


---

The sections below retain the v2.0 historical plan. M7 has its own [task index](M7-v2-cleanup/_index.md).

## Overview

44 tasks across 6 milestones implementing the v2.0 clean-cutover release. The v2.0 cutover is single-shot (no Alpha→Beta→GA phasing per PRD §1), so milestones are technical layers that each leave the public API in a compilable state and exercise an outcome a downstream consumer would care about. There is no parallel maintenance branch — v1.x is end-of-life on the day v2.0 ships (DR-011, OQ-007).

## Milestones

| ID | Name | Outcome | Tasks |
|---|---|---|---|
| M1 | Foundation | C++20 floor, header layout & guards, primitive types (`http_method`, `method_set`), `feature_unavailable`, `iovec_entry`, `httpserver::constants`, header-hygiene CI gate. After M1 the library still functions as v1 — additive only. | TASK-001 .. TASK-007 |
| M2 | Response Refactor | `http_response` is a value type with SBO body, factories, fluent `with_*` chains, const-correct getters. Public `*_response` subclasses gone. After M2 a downstream consumer can build & chain a response. | TASK-008 .. TASK-013 |
| M3 | Webserver internal & Request Refactor | `webserver_impl` and `http_request_impl` PIMPL split; per-connection arena allocator; `const&` / `string_view` getters; high-level GnuTLS accessors. Public headers are free of `<microhttpd.h>`, `<pthread.h>`, `<gnutls/gnutls.h>`, `<sys/socket.h>`. | TASK-014 .. TASK-020 |
| M4 | Handler & Resource Model | `http_resource` allow-mask via `method_set`, snake_case `render_*`, smart-pointer registration, `register_path`/`register_prefix`, lambda `on_*`, generic `route()`. After M4 a consumer can register handlers in either form. | TASK-021 .. TASK-026 |
| M5 | Routing, Lifecycle, Builder & Features | 3-tier route table (hash + radix + regex) with LRU cache, v1-corpus regression gate, name canonicalization (`stop_and_wait`, `block_ip`/`unblock_ip`, `_handler` suffix), error-propagation contract, thread-safety stress test, builder cleanup, `features()`, websocket smart-pointer overloads, handler return-by-value dispatch cutover, lifecycle hook bus (server-wide + per-route, 11 phases, with v1 setters retained as documented aliases). After M5 the library is feature-complete. | TASK-027 .. TASK-036, TASK-045 .. TASK-052 |
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
    045 [hook bus skeleton] (depends on 009, 014) ──┐
                                                     ├──→ 046 [conn + accept hooks; closes #332]
                                                     ├──→ 047 [request_received + body_chunk; closes #273]
                                                     ├──→ 048 [route_resolved + before_handler; 404/405/auth aliases] (also depends on 027, 031)
                                                     ├──→ 049 [handler_exception; internal_error_handler alias] (also depends on 031)
                                                     ├──→ 050 [after_handler + response_sent + request_completed; log_access alias; closes #281, #69]
                                                     └──→ 051 [per-route hooks on http_resource] (also depends on 048, 049, 050)
                                                              └──→ 052 [docs + examples + bench + stress-test ext; touches back into TASK-040/041/042/043]

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
| TASK-001 | Bump C++ standard floor to C++20 | M1 | Done | None |
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
| TASK-014 | `webserver_impl` skeleton (PIMPL prep) | M3 | Done | TASK-002 |
| TASK-015 | `http_request_impl` skeleton (PIMPL split) | M3 | Done | TASK-002, TASK-014 |
| TASK-016 | Per-connection arena for `http_request_impl` | M3 | Done | TASK-014, TASK-015 |
| TASK-017 | `http_request` container getters return `const&` | M3 | Done | TASK-015 |
| TASK-018 | `http_request` single-key getters return `string_view`, all const | M3 | Done | TASK-015, TASK-016 |
| TASK-019 | High-level GnuTLS accessors replacing `gnutls_session_t` | M3 | Done | TASK-015 |
| TASK-020 | Final public-header backend-include sweep | M3 | Done | TASK-014, TASK-015, TASK-019 |
| TASK-021 | `http_resource` allow-mask via `method_set` | M4 | Done | TASK-005 |
| TASK-022 | Snake_case `render_*` overrides on `http_resource` | M4 | Done | TASK-021 |
| TASK-023 | Smart-pointer `register_resource` overloads | M4 | Done | TASK-014 |
| TASK-024 | `register_path` and `register_prefix` (replace `bool family`) | M4 | Done | TASK-023 |
| TASK-025 | Lambda handler entry points `on_*` | M4 | Done | TASK-005, TASK-009, TASK-014 |
| TASK-026 | Generic `webserver::route(method, path, handler)` | M4 | Done | TASK-005, TASK-025 |
| TASK-027 | 3-tier route table with LRU cache | M5 | Done | TASK-005, TASK-014, TASK-021, TASK-024, TASK-025, TASK-026 |
| TASK-028 | Routing-semantics regression gate | M5 | Done | TASK-027 |
| TASK-029 | Naming consistency — `stop_and_wait`, `block_ip`/`unblock_ip` | M5 | Done | TASK-014 |
| TASK-030 | `_handler` suffix renames + `explicit` constructor | M5 | Done | TASK-014 |
| TASK-031 | Handler error-propagation contract (DR-009) | M5 | Done | TASK-027, TASK-030 |
| TASK-032 | Thread-safety contract stress test (DR-008) | M5 | Done | TASK-027, TASK-031 |
| TASK-033 | `create_webserver` builder cleanup | M5 | Done | TASK-006, TASK-014 |
| TASK-034 | Build-flag-independent public API + `webserver::features()` | M5 | Done | TASK-003, TASK-019, TASK-033 |
| TASK-035 | Smart-pointer `register_ws_resource` overloads | M5 | Done | TASK-014, TASK-034 |
| TASK-036 | Handler return-by-value dispatch cutover | M5 | Done | TASK-022, TASK-025, TASK-027, TASK-031 |
| TASK-045 | Hook bus skeleton (`hook_phase`, `hook_action`, `hook_handle`, `webserver::add_hook`) | M5 | Done | TASK-009, TASK-014 |
| TASK-046 | Fire `connection_opened` / `connection_closed` / `accept_decision` | M5 | Done | TASK-045 |
| TASK-047 | Fire `request_received` and `body_chunk` (pre-handler short-circuit) | M5 | Done | TASK-045 |
| TASK-048 | Fire `route_resolved` and `before_handler`; wire 404/405/auth aliases | M5 | Done | TASK-045, TASK-027, TASK-031 |
| TASK-049 | Fire `handler_exception`; wire `internal_error_handler` alias | M5 | Done | TASK-045, TASK-031 |
| TASK-050 | Fire `after_handler` (post-handler short-circuit), `response_sent`, `request_completed`; wire `log_access` alias | M5 | Done | TASK-045 |
| TASK-051 | Per-route hooks (`http_resource::add_hook`) | M5 | Done | TASK-045, TASK-048, TASK-049, TASK-050 |
| TASK-052 | Hook bus documentation, examples, benchmark, stress-test extension (touches back into TASK-040/041/042/043) | M5 | Done | TASK-045, TASK-046, TASK-047, TASK-048, TASK-049, TASK-050, TASK-051 |
| TASK-037 | CI test for build-flag invariance | M6 | Done | TASK-034 |
| TASK-038 | Sanitizer-clean tests for `http_response` move semantics | M6 | Done | TASK-009, TASK-036 |
| TASK-039 | Performance acceptance (`get_headers`, `sizeof(http_resource)`) | M6 | Done | TASK-017, TASK-018, TASK-021 |
| TASK-040 | Rewrite `examples/` | M6 | Done | TASK-025, TASK-036 |
| TASK-041 | Rewrite `README.md` | M6 | Done | TASK-031, TASK-032, TASK-040 |
| TASK-042 | Write `RELEASE_NOTES.md` for v2.0 | M6 | Done | TASK-041 |
| TASK-043 | Doxygen / inline doc refresh | M6 | Done | TASK-031, TASK-034, TASK-041 |
| TASK-044 | SOVERSION bump and packaging | M6 | Done | TASK-042, TASK-043 |
| [TASK-096](M8-native-http1/TASK-096.md) | Capture v2 observable-behavior baseline and native transcript harness | M8 | Not Started | None |
| [TASK-097](M8-native-http1/TASK-097.md) | Define public semantic types and ordered fields | M8 | Not Started | TASK-096 |
| [TASK-098](M8-native-http1/TASK-098.md) | Define C++20 task executor, cancellation and resume signals | M8 | Not Started | TASK-097 |
| [TASK-099](M8-native-http1/TASK-099.md) | Implement private operation I/O contract and fake backend | M8 | Not Started | TASK-098 |
| [TASK-100](M8-native-http1/TASK-100.md) | Implement poll and WSAPoll socket backends | M8 | Not Started | TASK-099 |
| [TASK-101](M8-native-http1/TASK-101.md) | Implement validated server options, budgets and route registration | M8 | Not Started | TASK-097, TASK-099 |
| [TASK-102](M8-native-http1/TASK-102.md) | Implement header-time exchange decisions and route execution | M8 | Not Started | TASK-098, TASK-101 |
| [TASK-103](M8-native-http1/TASK-103.md) | Implement bounded body reader and collect | M8 | Not Started | TASK-102 |
| [TASK-104](M8-native-http1/TASK-104.md) | Implement response writer, body source and backpressure | M8 | Not Started | TASK-102 |
| [TASK-105](M8-native-http1/TASK-105.md) | Implement strict HTTP/1 start-line and header parser | M8 | Not Started | TASK-097, TASK-101 |
| [TASK-106](M8-native-http1/TASK-106.md) | Implement authoritative HTTP/1 body framing and trailers | M8 | Not Started | TASK-103, TASK-105 |
| [TASK-107](M8-native-http1/TASK-107.md) | Implement HTTP/1 response framing and ordered persistence | M8 | Not Started | TASK-104, TASK-105 |
| [TASK-108](M8-native-http1/TASK-108.md) | Wire native TCP listener to an end-to-end routed HTTP/1 service | M8 | Not Started | TASK-100, TASK-102, TASK-106, TASK-107 |
| [TASK-109](M8-native-http1/TASK-109.md) | Implement Expect admission, early rejection and suspension deadlines | M8 | Not Started | TASK-102, TASK-106, TASK-108 |
| [TASK-110](M8-native-http1/TASK-110.md) | Implement handler-safe stop and deadline drain | M8 | Not Started | TASK-098, TASK-107, TASK-108 |
| [TASK-111](M9-http1-parity-websocket/TASK-111.md) | Add bounded synchronous value-returning route adapter | M9 | Not Started | TASK-102, TASK-103, TASK-104, TASK-108 |
| [TASK-112](M9-http1-parity-websocket/TASK-112.md) | Implement immutable reusable response definitions and overlays | M9 | Not Started | TASK-104, TASK-107 |
| [TASK-113](M9-http1-parity-websocket/TASK-113.md) | Implement file, pipe and borrowed-buffer response ownership | M9 | Not Started | TASK-104, TASK-112 |
| [TASK-114](M9-http1-parity-websocket/TASK-114.md) | Port Basic authentication and in-tree hash/entropy primitives | M9 | Not Started | TASK-102, TASK-108 |
| [TASK-115](M9-http1-parity-websocket/TASK-115.md) | Port RFC 7616 Digest authentication and replay checks | M9 | Not Started | TASK-114 |
| [TASK-116](M9-http1-parity-websocket/TASK-116.md) | Port URL-encoded form handling with bounded admission | M9 | Not Started | TASK-103, TASK-106 |
| [TASK-117](M9-http1-parity-websocket/TASK-117.md) | Port streaming multipart uploads and cleanup | M9 | Not Started | TASK-103, TASK-106, TASK-116 |
| [TASK-118](M9-http1-parity-websocket/TASK-118.md) | Port route matching and lifecycle hook behavior | M9 | Not Started | TASK-102, TASK-108 |
| [TASK-119](M9-http1-parity-websocket/TASK-119.md) | Port IP controls with peer-address policy | M9 | Not Started | TASK-108, TASK-118 |
| [TASK-120](M9-http1-parity-websocket/TASK-120.md) | Port SHOUTcast and remaining HTTP/1 parity cases | M9 | Complete | TASK-108, TASK-118 |
| [TASK-121](M9-http1-parity-websocket/TASK-121.md) | Implement transport-neutral WebSocket frame codec and session | M9 | Complete | TASK-098, TASK-104 |
| [TASK-122](M9-http1-parity-websocket/TASK-122.md) | Implement WebSocket over HTTP/1.1 upgrade | M9 | Complete | TASK-105, TASK-108, TASK-121 |
| [TASK-123](M9-http1-parity-websocket/TASK-123.md) | Integrate WebSocket close with cancellation and server drain | M9 | Not Started | TASK-110, TASK-121, TASK-122 |
| [TASK-124](M9-http1-parity-websocket/TASK-124.md) | Freeze portable external-loop readiness contract | M9 | Not Started | TASK-099 |
| [TASK-125](M9-http1-parity-websocket/TASK-125.md) | Implement external-loop readiness adapter | M9 | Not Started | TASK-100, TASK-124 |
| [TASK-126](M9-http1-parity-websocket/TASK-126.md) | Implement Linux epoll managed I/O backend | M9 | Not Started | TASK-099, TASK-100 |
| [TASK-127](M9-http1-parity-websocket/TASK-127.md) | Implement BSD/macOS kqueue managed I/O backend | M9 | Not Started | TASK-099, TASK-100 |
| [TASK-128](M9-http1-parity-websocket/TASK-128.md) | Gate HTTP/1 and WebSocket conformance, fuzzing and parity | M9 | Not Started | TASK-113, TASK-115, TASK-117, TASK-118, TASK-120, TASK-122, TASK-123 |
| [TASK-129](M10-tls-http2/TASK-129.md) | Establish OpenSSL 3.5 LTS build boundary and feature gates | M10 | Not Started | TASK-108 |
| [TASK-130](M10-tls-http2/TASK-130.md) | Drive nonblocking TCP TLS through private I/O operations | M10 | Not Started | TASK-099, TASK-129 |
| [TASK-131](M10-tls-http2/TASK-131.md) | Build and atomically publish immutable TLS credential snapshots | M10 | Not Started | TASK-129, TASK-130 |
| [TASK-132](M10-tls-http2/TASK-132.md) | Implement early SNI, default-host and ALPN selection | M10 | Not Started | TASK-130, TASK-131 |
| [TASK-133](M10-tls-http2/TASK-133.md) | Implement initial-handshake mTLS profiles and peer metadata | M10 | Not Started | TASK-131, TASK-132 |
| [TASK-134](M10-tls-http2/TASK-134.md) | Resolve external-PSK lookup and timeout execution contract | M10 | Not Started | TASK-129, TASK-131 |
| [TASK-135](M10-tls-http2/TASK-135.md) | Implement TLS 1.2 and 1.3 external-PSK profiles | M10 | Not Started | TASK-131, TASK-132, TASK-134 |
| [TASK-136](M10-tls-http2/TASK-136.md) | Implement ACME TLS-ALPN-01 publication and removal | M10 | Not Started | TASK-131, TASK-132 |
| [TASK-137](M10-tls-http2/TASK-137.md) | Implement bounded HPACK primitives and static tables | M10 | Not Started | TASK-097 |
| [TASK-138](M10-tls-http2/TASK-138.md) | Implement connection-owned HPACK dynamic tables | M10 | Not Started | TASK-137 |
| [TASK-139](M10-tls-http2/TASK-139.md) | Implement HTTP/2 preface, frame and SETTINGS machine | M10 | Not Started | TASK-130, TASK-132, TASK-137 |
| [TASK-140](M10-tls-http2/TASK-140.md) | Route HTTP/2 headers-only streams through the exchange | M10 | Not Started | TASK-102, TASK-138, TASK-139 |
| [TASK-141](M10-tls-http2/TASK-141.md) | Implement HTTP/2 streaming bodies and two-level flow control | M10 | Not Started | TASK-103, TASK-104, TASK-140 |
| [TASK-142](M10-tls-http2/TASK-142.md) | Implement HTTP/2 fair output, resets and rate budgets | M10 | Not Started | TASK-141 |
| [TASK-143](M10-tls-http2/TASK-143.md) | Implement HTTP/2 staged GOAWAY and deadline drain | M10 | Not Started | TASK-110, TASK-142 |
| [TASK-144](M10-tls-http2/TASK-144.md) | Implement WebSocket over HTTP/2 Extended CONNECT | M10 | Not Started | TASK-121, TASK-140, TASK-141, TASK-143 |
| [TASK-145](M10-tls-http2/TASK-145.md) | Gate HTTP/2 conformance, fuzzing and independent clients | M10 | Not Started | TASK-138, TASK-139, TASK-140, TASK-141, TASK-142, TASK-143, TASK-144 |
| [TASK-146](M10-tls-http2/TASK-146.md) | Implement Windows IOCP managed I/O backend | M10 | Not Started | TASK-099, TASK-100 |
| [TASK-147](M10-tls-http2/TASK-147.md) | Verify certificate rotation, SNI, mTLS, PSK and ACME under concurrency | M10 | Not Started | TASK-133, TASK-135, TASK-136, TASK-146 |
| [TASK-148](M10-tls-http2/TASK-148.md) | Run TLS-on/off installed-consumer dependency audit | M10 | Not Started | TASK-129, TASK-145, TASK-146, TASK-147 |
| [TASK-149](M11-http3-first-slice/TASK-149.md) | Add owned UDP send/receive operations and CID dispatch seam | M11 | Not Started | TASK-099, TASK-100 |
| [TASK-150](M11-http3-first-slice/TASK-150.md) | Add deterministic QUIC network, clock and fuzz harness | M11 | Not Started | TASK-149 |
| [TASK-151](M11-http3-first-slice/TASK-151.md) | Implement strict QUIC v1 packet, frame and parameter codecs | M11 | Not Started | TASK-150 |
| [TASK-152](M11-http3-first-slice/TASK-152.md) | Implement QUIC packet protection and key lifecycle with OpenSSL EVP | M11 | Not Started | TASK-129, TASK-151 |
| [TASK-153](M11-http3-first-slice/TASK-153.md) | Implement server CID admission, Retry and amplification limits | M11 | Not Started | TASK-149, TASK-151, TASK-152 |
| [TASK-154](M11-http3-first-slice/TASK-154.md) | Implement QUIC stream state and bounded reassembly | M11 | Not Started | TASK-151 |
| [TASK-155](M11-http3-first-slice/TASK-155.md) | Bridge OpenSSL QUIC TLS callbacks to owned CRYPTO streams | M11 | Not Started | TASK-131, TASK-132, TASK-152, TASK-153 |
| [TASK-156](M11-http3-first-slice/TASK-156.md) | Implement ACK generation, RFC 9002 loss detection and PTO | M11 | Not Started | TASK-151, TASK-152, TASK-155 |
| [TASK-157](M11-http3-first-slice/TASK-157.md) | Implement QUIC stream/connection flow control | M11 | Not Started | TASK-154, TASK-156 |
| [TASK-158](M11-http3-first-slice/TASK-158.md) | Implement QUIC congestion control, pacing and fair send scheduling | M11 | Not Started | TASK-156, TASK-157 |
| [TASK-159](M11-http3-first-slice/TASK-159.md) | Implement bounded static-only QPACK codec | M11 | Not Started | TASK-097 |
| [TASK-160](M11-http3-first-slice/TASK-160.md) | Implement HTTP/3 control streams, SETTINGS and frame roles | M11 | Not Started | TASK-155, TASK-157, TASK-159 |
| [TASK-161](M11-http3-first-slice/TASK-161.md) | Bridge HTTP/3 request streams to semantic exchanges | M11 | Not Started | TASK-102, TASK-103, TASK-104, TASK-158, TASK-160 |
| [TASK-162](M11-http3-first-slice/TASK-162.md) | Run independent HTTP/3 client smoke tests | M11 | Not Started | TASK-161 |
| [TASK-163](M11-http3-first-slice/TASK-163.md) | Package a QUIC interop-runner endpoint and diagnostics | M11 | Not Started | TASK-155, TASK-157, TASK-161 |
| [TASK-164](M12-http3-full/TASK-164.md) | Implement QUIC CID lifecycle, path validation and rebinding | M12 | Not Started | TASK-153, TASK-156, TASK-158 |
| [TASK-165](M12-http3-full/TASK-165.md) | Implement QUIC datagram sizing and black-hole recovery | M12 | Not Started | TASK-158, TASK-164 |
| [TASK-166](M12-http3-full/TASK-166.md) | Implement QUIC idle, close, drain and key disposal | M12 | Not Started | TASK-110, TASK-156, TASK-157, TASK-164 |
| [TASK-167](M12-http3-full/TASK-167.md) | Implement QPACK dynamic table and instruction codecs | M12 | Not Started | TASK-159 |
| [TASK-168](M12-http3-full/TASK-168.md) | Implement QPACK blocked-section and critical-stream accounting | M12 | Not Started | TASK-160, TASK-167 |
| [TASK-169](M12-http3-full/TASK-169.md) | Integrate dynamic QPACK with HTTP/3 request scheduling | M12 | Not Started | TASK-161, TASK-168 |
| [TASK-170](M12-http3-full/TASK-170.md) | Implement staged HTTP/3 GOAWAY and graceful drain | M12 | Not Started | TASK-143, TASK-166, TASK-169 |
| [TASK-171](M12-http3-full/TASK-171.md) | Implement HTTP/3 WebSocket Extended CONNECT negotiation | M12 | Not Started | TASK-121, TASK-161, TASK-169 |
| [TASK-172](M12-http3-full/TASK-172.md) | Implement WebSocket-over-HTTP/3 DATA and lifecycle adapter | M12 | Not Started | TASK-166, TASK-170, TASK-171 |
| [TASK-173](M12-http3-full/TASK-173.md) | Run required QUIC interop-runner matrix | M12 | Not Started | TASK-163, TASK-164, TASK-165, TASK-166 |
| [TASK-174](M12-http3-full/TASK-174.md) | Run independent HTTP/3 semantic client matrix | M12 | Not Started | TASK-169, TASK-170 |
| [TASK-175](M12-http3-full/TASK-175.md) | Build independent WebSocket-over-HTTP/3 client harness | M12 | Not Started | TASK-172 |
| [TASK-176](M12-http3-full/TASK-176.md) | Fuzz QUIC recovery and TLS callback event sequences | M12 | Not Started | TASK-166 |
| [TASK-177](M12-http3-full/TASK-177.md) | Fuzz HTTP/3 framing, QPACK and blocked streams | M12 | Not Started | TASK-169, TASK-170 |
| [TASK-178](M12-http3-full/TASK-178.md) | Gate full HTTP/3 and WebSocket-over-H3 conformance | M12 | Not Started | TASK-173, TASK-174, TASK-175, TASK-176, TASK-177 |
| [TASK-179](M13-v3-release/TASK-179.md) | Audit v2-to-v3 behavior parity across all protocols | M13 | Not Started | TASK-128, TASK-145, TASK-178 |
| [TASK-180](M13-v3-release/TASK-180.md) | Verify hierarchical resource limits and slow-peer plateaus across engines | M13 | Not Started | TASK-128, TASK-145, TASK-178 |
| [TASK-181](M13-v3-release/TASK-181.md) | Expose bounded diagnostic callbacks and counters | M13 | Not Started | TASK-108, TASK-145, TASK-178 |
| [TASK-182](M13-v3-release/TASK-182.md) | Complete four-platform managed/external-loop package validation | M13 | Not Started | TASK-125, TASK-126, TASK-127, TASK-146, TASK-178 |
| [TASK-183](M13-v3-release/TASK-183.md) | Publish v2-to-v3 migration guide and API examples | M13 | Not Started | TASK-179 |
| [TASK-184](M13-v3-release/TASK-184.md) | Remove MHD build/link paths and bump v3 SOVERSION | M13 | Not Started | TASK-179, TASK-182, TASK-183 |
| [TASK-185](M13-v3-release/TASK-185.md) | Run final dependency, conformance, sanitizer and performance release gates | M13 | Not Started | TASK-180, TASK-181, TASK-182, TASK-183, TASK-184 |

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
| PRD-HOOK-REQ-001 (`add_hook` returns RAII `hook_handle`) | TASK-045 |
| PRD-HOOK-REQ-002 (registered hook invoked on every phase firing in order) | TASK-045, TASK-046, TASK-047, TASK-048, TASK-049, TASK-050, TASK-051 |
| PRD-HOOK-REQ-003 (pre-handler short-circuit skips remaining hooks + resource) | TASK-047, TASK-048, TASK-049 |
| PRD-HOOK-REQ-004 (post-handler `after_handler` short-circuit replaces response) | TASK-050 |
| PRD-HOOK-REQ-005 (throwing hook routes through DR-9 §5.2) | TASK-046, TASK-047, TASK-048, TASK-049, TASK-050, TASK-051 |
| PRD-HOOK-REQ-006 (`http_resource::add_hook` is per-route, runs after server-wide) | TASK-051 |
| PRD-HOOK-REQ-007 (concurrent `add_hook` doesn't disturb in-flight chains) | TASK-045, TASK-052 (stress-test extension) |
| PRD-HOOK-REQ-008 (zero-cost when unused — atomic `any_hooks_` flag) | TASK-045, TASK-052 (`bench_hook_overhead`) |
| PRD-HOOK-REQ-009 (v1 setters documented as aliases) | TASK-048, TASK-049, TASK-050, TASK-052 (final docs sweep) |

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
| DR-012 (lifecycle hook bus) | Implements: TASK-045 (skeleton), TASK-046 .. TASK-051 (phase firing + per-route). Documents: TASK-052 (README / RELEASE_NOTES / Doxygen / examples, plus stress-test and benchmark extensions). |
