# v2.0 Deferred Backlog — Implementation Plan

**Status:** Draft 1
**Last updated:** 2026-05-28
**Owner:** Sebastiano Merlino
**Inputs:** [unworked review issues tracker](../unworked_review_issues/), [product_specs.md](../product_specs.md), [architecture/](../architecture/)

---

## Why this exists

Passes 1–4 over `specs/unworked_review_issues/` dispositioned the 1974 review
findings down to 322 actionable deferrals + 74 explicit wontfix. Most of the
322 are small (test-coverage gaps, perf nits, doc drift) and can be picked
up opportunistically. This plan covers the seven **substantive deferrals**
that are too big to inline into a sweep and that touch v2.0's release
posture — security, correctness, performance, or public-API shape.

Each entry below has the same shape as `specs/tasks/M*/TASK-*.md` so they
can be split out into individual task files when work starts.

## Summary

| Task ID (proposed) | Name | Severity | Milestone | Estimate | GA-blocker? |
|---|---|---|---|---|---|
| TASK-053 | Wire `lookup_v2()` into dispatch hot path | Major | M5 | L | **Yes** — TASK-027 work is dead code today |
| TASK-054 | Migrate `auth_handler_ptr` to `optional<http_response>` | Major | M4/M5 | M | **Yes** — last `shared_ptr<http_response>` on public API |
| TASK-055 | DR-009 revision: default error body must not surface `e.what()` | Major | M6 | M | **Yes** — CWE-209 information disclosure |
| TASK-056 | Hash-DoS hardening + prefix-route disambiguation in radix tree | Major | M5 | M | **Yes** — security hardening |
| TASK-057 | Redact credentials in `http_request::operator<<` | Minor (sec) | M3 | S | **Yes** — A09:2021 logging failure |
| TASK-058 | Hot-path allocation pass: canonicalize/normalize/serialize_allow_methods | Minor | post-v2.0 | L | No — perf polish |
| TASK-059 | Supply-chain: sha256-pin PMD analyzer download in CI | Minor (sec) | M6 | S | Yes — quick win |

GA-blockers (six of seven) should land before v2.0 cuts a release tag.
TASK-058 is a post-v2.0 polish but the prep work (string_view return type
on `canonicalize_lookup_path`) can ride along with TASK-053.

---

## TASK-053 — Wire `lookup_v2()` into the dispatch hot path

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** `webserver_impl` dispatch
**Estimate:** L

**Goal:**
Make TASK-027's 3-tier route table (`exact_routes_`, radix tree, regex
vector, `route_cache_v2`) the actual lookup the dispatch path consults.
Today the v2 table is built and maintained on every register/unregister
but never queried — `webserver_impl::find_route_for_request` still walks
the v1 vector. The architectural goal of TASK-027 (O(1) exact lookup +
LRU cache + radix scan) is not realised end-to-end.

**Action Items:**
- [ ] Locate the v1 lookup site (`webserver_impl::find_route_for_request`
  or equivalent in `src/detail/webserver_dispatch.cpp`) and add a feature
  flag (`use_lookup_v2_`, default `true`) that selects `lookup_v2` over
  the legacy walk.
- [ ] Wire the `lookup_result` shape (tier_hit, captured_params) into the
  existing dispatch contract so the call site doesn't need a parallel
  `route_entry*` path.
- [ ] Remove the v1 fallback walk once `make check` and
  `routing_regression_test` pass under the v2 path.
- [ ] Delete the legacy linear `route_table_v1_` field when no caller
  remains. (May require a follow-up grep sweep of `webserver_register.cpp`
  / `webserver_routes.cpp`.)
- [ ] Run `test/bench_hook_overhead.cpp` and a new
  `test/bench_route_lookup.cpp` to confirm the cache-hit path is in the
  expected 100ns ballpark and the radix tier is in the µs range. When
  creating `bench_route_lookup.cpp` also verify the TASK-056 acceptance
  criterion: the `std::map`-based radix node container introduced by
  TASK-056 must show no regression worse than 2× on the cache-miss radix
  path compared to the former `std::unordered_map` baseline (the 2× budget
  was formally deferred from TASK-056 to this task because `lookup_v2` was
  not yet wired into dispatch when TASK-056 landed).
- [ ] Drop the "TODO(Cycle K): rename route_cache_v2 → route_lru_cache"
  comment in `webserver_impl.hpp:202` and do the rename now that v1 is
  gone.

**Dependencies:**
- Blocked by: TASK-027 (Done), TASK-028 (Done)
- Blocks: TASK-058 (the canonicalize_lookup_path optimization needs
  lookup_v2 to be the only caller)

**Acceptance Criteria:**
- `routing_regression_test` passes with v1 fallback removed.
- `lookup_v2` is the only function `webserver_impl::dispatch` calls
  during route resolution (verified by grep).
- `bench_route_lookup` shows cache-hit latency ≤ 200 ns on Apple M-series
  / Intel; radix tier ≤ 5 µs for paths up to 8 segments.
- The `route_table_v1_` field no longer exists.
- All hook bus tests continue to fire `route_resolved` with the correct
  `route_entry*` once the new path is the only one.

**Related Findings:** task-036 #37, task-027 #11/#12, task-028 #19,
manual-validation #7/#9
**Related Decisions:** §4.5 (routing)

---

## TASK-054 — Migrate `auth_handler_ptr` from `shared_ptr<http_response>` to `optional<http_response>`

**Milestone:** M4 - Handler & Resource Model (touch-back from M5)
**Component:** `create_webserver` / `webserver_impl::auth`
**Estimate:** M

**Goal:**
The auth handler signature is the last place a `std::shared_ptr<http_response>`
appears in the public API. Per PRD-RSP-REQ-007 (`http_response` is a value
type, no shared ownership on the response path) and the by-value cutover
done in TASK-046, this should become `std::optional<http_response>`. A
TODO comment marking this migration already lives at
`src/httpserver/create_webserver.hpp:92-99`.

**Action Items:**
- [x] Define `auth_handler_ptr` as
  `std::function<std::optional<http_response>(const http_request&)>` in
  `create_webserver.hpp`. Keep the old typedef alias for one transitional
  build with a `[[deprecated]]` attribute pointing at the new shape.
- [x] Update the auth short-circuit path in `webserver_dispatch.cpp` to
  consume `optional<http_response>` instead of dereferencing a
  `shared_ptr`.
- [x] Update the documented v1 alias surface (`auth_handler` setter) to
  internally adapt v1 callers that still produce a `shared_ptr` (free
  function `compat::adapt_legacy_auth(...)`), with a `[[deprecated]]`
  warning.
- [x] Update `examples/centralized_authentication.cpp` to return
  `std::optional<http_response>` directly (drop `std::make_shared`).
- [x] Update Doxygen, README "Centralized authentication" section, and
  RELEASE_NOTES.md "Migration notes" with the new signature.
- [x] Remove the TODO comment at `create_webserver.hpp:92-99`.

**Dependencies:**
- Blocked by: TASK-046 (by-value response cutover, Done)
- Blocks: None

**Acceptance Criteria:**
- `grep -rE 'shared_ptr<.*http_response' src/httpserver/` returns no
  results in public headers.
- `examples/centralized_authentication.cpp` and the auth integration
  test compile against the new signature.
- The v1 shim adapter compiles with `-Werror=deprecated-declarations`
  off, and emits a deprecation warning when on.
- One heap allocation removed per authenticated request (verifiable via
  `bench_auth.cpp` or by inspection).

**Related Findings:** task-036 #38, task-030 #18
**Related Decisions:** DR-009, PRD-RSP-REQ-007

**Status:** Done

---

## TASK-055 — DR-009 revision: default error body must not surface `e.what()`

**Milestone:** M6 - Release Readiness
**Component:** `webserver_impl::internal_error_page` / `log_dispatch_error`
**Estimate:** M

**Goal:**
DR-009 made the explicit choice that the default internal-error response
body would include `e.what()` for debugging ergonomics. Two security
reviewers (task-031 #3, task-031 #4) and one Pass-1 sweeper (task-036 #44)
have flagged this as CWE-209 information disclosure — exception messages
routinely contain file paths, SQL fragments, internal identifiers, and
stack-derived strings that should not cross a process boundary to an
untrusted client. The decision needs a coordinated revision: the default
behaviour should be sanitized; the verbose form should be opt-in.

**Action Items:**
- [x] Draft DR-009-rev1 in `specs/architecture/11-decisions/DR-009.md`:
  default body is a fixed string ("Internal Server Error"); the
  "with the request id (if any) appended" clause was deferred (no
  request-id concept exists in the codebase today; documented in
  DR-009 Revision 1 "Consequences"). Verbose body is opt-in via
  `create_webserver::expose_exception_messages(bool)` flag intended
  for development environments only.
- [x] Update `webserver_impl::internal_error_page` to consult the flag.
- [x] Update `log_dispatch_error` to log `e.what()` verbatim to the
  server log (where it belongs) regardless of the flag; add a Doxygen
  note that handlers throwing exceptions with sensitive data should
  catch+sanitize before throwing.
- [x] Update the two regression tests that pin the current behaviour
  (`dr009_runtime_error_message_surfaces_in_default_body` and sibling)
  to split into:
  - `dr009_default_body_is_fixed_string` (new contract)
  - `dr009_verbose_body_surfaces_message_when_opted_in`
- [x] Update README "Error handling" section + RELEASE_NOTES.md
  "Behaviour change" note.

**Dependencies:**
- Blocked by: TASK-031 (Done — original DR-009 implementation)
- Blocks: v2.0 release tag

**Acceptance Criteria:**
- Default `webserver` configuration does not surface `e.what()` in the
  HTTP response body — verifiable via the renamed regression test.
- `expose_exception_messages(true)` restores the old behaviour and is
  documented as for-development-only.
- `log_dispatch_error` continues to log `e.what()` to the server log
  unchanged.
- Both regression tests pass.

**Related Findings:** task-031 #3, task-031 #4, task-036 #44, task-034 #24
**Related Decisions:** DR-009 (revised), CWE-209

**Status:** Done

---

## TASK-056 — Hash-DoS hardening + prefix-route disambiguation in radix tree

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** `detail::radix_tree`, `webserver_impl::upsert_v2_param_route`
**Estimate:** M

**Goal:**
Two security findings on the v2 routing table were deferred during
TASK-027 cleanup:
1. The radix tree's per-segment children use `std::unordered_map`, which
   is vulnerable to algorithmic-complexity DoS (CWE-407) when an
   attacker controls path segments. Either switch to `std::map`
   (O(log n) but DoS-immune) or hash-randomize (libc++ does not, by
   default).
2. `upsert_v2_param_route` uses a `find()`-then-`insert()` pattern
   without an `is_prefix_match` guard, so a route registered as
   `/admin/*` and a later route registered as `/admin` (exact) silently
   collide on the cache key.

**Action Items:**
- [x] Replace `std::unordered_map` with `std::map` (or a small
  flat_map) in the radix node child container. Benchmark the impact:
  acceptable if cache-miss path stays under 5 µs on the worst-case
  realistic tree depth.
- [x] Add the `is_prefix_match` guard at the call site of
  `upsert_v2_param_route` so prefix-vs-exact terminus collisions are
  detected at registration time, not at lookup time.
- [x] Add a regression test
  `routing_regression_test.cpp::register_exact_after_prefix_does_not_collide`
  pinning the registration-time error.
- [x] Add a stress-test variant in `threadsafety_stress.cpp` that
  hammers the registration path with adversarial path segments to
  confirm the new container is DoS-resistant.
- [x] Update `specs/architecture/04-components/route-table.md` with the
  new container choice and rationale.

**Dependencies:**
- Blocked by: TASK-027 (Done)
- Blocks: v2.0 release tag (security-hardening item)

**Acceptance Criteria:**
- `bench_route_lookup` shows no regression worse than 2× on the
  cache-miss radix path.
  **DEFERRED to TASK-053** — `test/bench_route_lookup.cpp` does not
  exist yet; TASK-053 already owns creating that benchmark as part of
  wiring `lookup_v2` into the dispatch hot path. The 2× budget
  established here applies to the `std::map`-based container swap and
  must be verified by TASK-053's implementer against the TASK-056
  baseline. Creating the benchmark in TASK-056 would pre-empt TASK-053's
  scope and would benchmark a lookup path (`lookup_v2`) that is not yet
  wired into dispatch.
- New regression test passes.
- A 60-second adversarial-segment stress run completes without
  registration latency spikes > 10× baseline.
- Routing architecture doc reflects the new container.

**Related Findings:** task-027 #14, task-027 #18
**Related Decisions:** §4.5 routing

**Status:** Done

---

## TASK-057 — Redact credentials in `http_request::operator<<`

**Milestone:** M3 - Webserver internal & Request Refactor
**Component:** `http_request` diagnostic streaming
**Estimate:** S

**Goal:**
`http_request::operator<<` at `src/http_request.cpp:370` emits
`pass:"<plaintext>"` for diagnostic logging. This is an OWASP A09:2021
logging failure — anyone running with verbose request logging on a
production deploy leaks every Basic-auth password into their log
aggregation pipeline.

**Action Items:**
- [x] Replace the literal password emission with a fixed redaction token
  (`pass:"<redacted>"`). Same treatment for `digested_pass` and any
  other authentication secret on the stream.
- [x] Add an opt-in `webserver_builder.expose_credentials_in_logs(true)`
  flag for the rare developer who needs the verbose form locally.
- [x] Update Doxygen on the operator to call out the redaction policy.
- [x] Add a unit test
  `http_request_test::operator_stream_redacts_credentials`.

**Dependencies:**
- Blocked by: None
- Blocks: v2.0 release tag

**Acceptance Criteria:**
- `operator<<` output does not contain the literal password value by
  default.
- Opt-in flag restores the v1 verbose form and is documented as
  for-development-only.
- New unit test passes.

**Related Findings:** task-019 #22
**Related Decisions:** none new; A09:2021

**Status:** Done

---

## TASK-058 — Hot-path allocation pass

**Milestone:** post-v2.0 (polish; not a release blocker)
**Component:** `webserver_dispatch` / `webserver_request`
**Estimate:** L (broken into ~4 sub-items)

**Goal:**
Three perf items deferred during the manual-validation sweep all share
the same shape — a `std::string` is rebuilt on every request when a
`string_view` or a registration-time cache would do. None of them are
release-blocking but together they're worth 5–15% on the warm path.

**Action Items:**
- [ ] **canonicalize_lookup_path → string_view return.** Refactor
  `src/detail/webserver_dispatch.cpp::canonicalize_lookup_path` to
  return a `string_view` into a caller-owned scratch buffer (or the
  request arena) instead of allocating a `std::string` on every call.
- [ ] **normalize_path at registration time.** Move the
  `normalize_path` call from `webserver_request.cpp::should_skip_auth`
  (where it runs per request) to the registration site, storing the
  normalized form in `route_entry`. Update the lookup to use the
  pre-normalized form.
- [ ] **serialize_allow_methods caching.** `webserver_dispatch.cpp:363`
  rebuilds the Allow header value on every 405 response. Cache the
  serialized string on `route_entry` and invalidate on
  add/remove-method.
- [ ] Add `test/bench_warm_path.cpp` measuring before/after per-request
  cost so the next reviewer can verify the gain.

**Dependencies:**
- Blocked by: TASK-053 (must be the only caller of `canonicalize_lookup_path`)
- Blocks: None

**Acceptance Criteria:**
- `bench_warm_path` shows ≥ 5% improvement on the warm-cache GET path,
  measured median over 11 outer rounds × 1M inner iterations.
- No new allocations attributable to the dispatch path under a heap
  profiler.
- `routing_regression_test` and `body_test` both still pass.

**Related Findings:** manual-validation #7, #8, #9; task-027 #11/#12/#13;
task-028 #19
**Related Decisions:** §4.5 routing

---

## TASK-059 — Supply-chain: sha256-pin PMD analyzer download in CI

**Milestone:** M6 - Release Readiness
**Component:** `.github/workflows/verify-build.yml`
**Estimate:** S

**Goal:**
`.github/workflows/verify-build.yml:454` downloads `pmd.zip` over HTTPS
without verifying the file integrity. A TODO comment was added in a
prior commit but the actual sha256sum check is still missing. This is
the only outstanding supply-chain hardening item from the
manual-validation review.

**Action Items:**
- [ ] Pin the current PMD release version (e.g. `7.2.0`) in the
  workflow.
- [ ] After `curl/wget` of the zip, run
  `echo "<known-good-sha256>  pmd.zip" | sha256sum -c -` and fail the
  step on mismatch.
- [ ] Document the rotation procedure in the workflow comments: when
  PMD releases a new version, update both the URL and the hash in the
  same PR.
- [ ] Remove the TODO comment now that the check is real.

**Dependencies:**
- Blocked by: None
- Blocks: v2.0 release tag (security item, but trivially small)

**Acceptance Criteria:**
- A modified `pmd.zip` (e.g. one byte flipped) causes the CI step to
  fail.
- The current good build still succeeds.
- The TODO comment is gone.

**Related Findings:** manual-validation #10
**Related Decisions:** none

---

## Execution order

```
TASK-057 (creds redaction, S, GA)     ──┐
TASK-059 (sha256 pin, S, GA)          ──┤  ← quick wins, can land in any order
TASK-055 (DR-009 revision, M, GA)     ──┤
TASK-056 (hash-DoS + prefix, M, GA)   ──┤
TASK-054 (auth_handler optional, M, GA) ─┘

TASK-053 (lookup_v2 wiring, L, GA)    ──→ TASK-058 (alloc pass, L, post-v2.0)
```

Recommended sequencing for one engineer:
1. Week 1 — TASK-057 + TASK-059 (Friday-afternoon-sized).
2. Week 1–2 — TASK-055 (DR revision needs a written design doc).
3. Week 2 — TASK-054 (mechanical migration with a deprecation cycle).
4. Week 2–3 — TASK-056 (touches routing perf; benchmark-gated).
5. Week 3–4 — TASK-053 (the largest single-task scope here).
6. Post-v2.0 — TASK-058.

Six of the seven items are GA blockers. Cutting the v2.0 tag with
TASK-053 unaddressed would mean shipping with TASK-027's cache as
dead code and TASK-054 leaving a `shared_ptr<http_response>` on the
public API — both materially undercut the v2.0 architectural narrative.
