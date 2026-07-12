# v2.0 Routing Regression Gate

Per architecture §9 (testing) item 5 and TASK-028 (AR-003 release-blocker
risk): the v1 routing-test corpus must pass against the v2.0
implementation without regression. Any divergence from v1 routing
semantics is either documented here with rationale **or fixed**.

The gate is run automatically as part of `make check`. The bespoke v2
parity TU lives at [`test/unit/routing_regression_test.cpp`](unit/routing_regression_test.cpp)
and is wired in via [`test/Makefile.am`](Makefile.am) as
`check_PROGRAMS += routing_regression`. Manual selective run:

```sh
cd build/test && ./routing_regression
```

## Why two surfaces

Dispatch in `webserver_impl::finalize_answer` is driven by the v2 3-tier
table (TASK-027) — `resolve_resource_for_request` calls `lookup_v2()`
exclusively after the **TASK-053** cutover (and supersedes the original
TASK-036 plan). **TASK-067** deleted the v1 registration-side maps and
mutex; the v2 table is now the only routing surface end-to-end —
lambda/class conflict detection probes the v2 tiers directly via
`find_v2_entry_by_path_`.

So this gate protects two distinct surfaces:

1. **End-to-end dispatch (now via the v2 3-tier table).** The full
   routing corpus continues to round-trip through curl in
   `test/integ/basic.cpp` plus the TASK-024/025/026 unit suites.
2. **v2 3-tier table semantics.** `routing_regression_test.cpp` and
   `v2_dispatch_contract_test.cpp` together pin v2-lookup correctness.
   A failure in either is a release blocker.

## Pattern taxonomy

Every routing pattern in the v1 corpus, mapped to the v2 file that hosts
it (or its v2-API port). When you add a new routing pattern, add a row
here AND a test in `routing_regression_test.cpp`.

| Pattern | v1 test that pins it | v2 hosting file | Status |
|---|---|---|---|
| Exact path | `basic_suite::two_endpoints`, `duplicate_endpoints`, `request_with_*` | `test/integ/basic.cpp` | API-ported |
| Exact root `/` | `http_endpoint_suite::http_endpoint_root_only` | `test/unit/http_endpoint_test.cpp` | unchanged |
| Single-segment param `/{arg}` | `basic_suite::regex_matching_arg`, `regex_matching_arg_with_url_pars`, `http_endpoint_*` unit tests | `test/integ/basic.cpp`, `test/unit/http_endpoint_test.cpp` | API-ported |
| Multi-segment param `/{a}/{b}` | `http_endpoint_suite::http_endpoint_multiple_params` | `test/unit/http_endpoint_test.cpp` | unchanged |
| Custom-regex param `/{arg|([0-9]+)}` | `basic_suite::regex_matching_arg_custom`, `http_endpoint_registration_arg_custom_regex` | `test/integ/basic.cpp`, `test/unit/http_endpoint_test.cpp` | API-ported; v2 radix tier enforces the per-segment regex in-line via `radix_node::wildcard_constraint_`, matching v1 semantics |
| Prefix (family) | `basic_suite::family_endpoints`, `non_family_url_with_regex_like_pieces`, `single_resource_mode` | `test/integ/basic.cpp` | API-ported (`register_resource(..., true)` → `register_prefix`) |
| Regex (anchored, no `{}`) | `basic_suite::regex_matching`, `regex_url_exact_match`, `url_with_regex_like_pieces` | `test/integ/basic.cpp` | API-ported |
| Regex-checking disabled | `basic_suite::*` with `no_regex_checking()` | `test/integ/basic.cpp` | unchanged |
| Method-mismatched (405 + Allow) | `basic_suite::method_not_allowed_header`, `head_request`, `options_request`, `trace_request`, `only_render_*`, `custom_method_not_allowed_handler` | `test/integ/basic.cpp` | API-ported |
| Lambda-only registration (`on_get` / `on_post` / ...) | (new in v2) | `test/unit/webserver_on_methods_test.cpp` (TASK-025) | new |
| Generic `route(http_method, ...)` / `route(method_set, ...)` | (new in v2) | `test/unit/webserver_route_test.cpp` (TASK-026) | new |
| Register / unregister cycles | `basic_suite::register_unregister`, `unregister_then_404`, `unregister_path` (was `unregister_resource`) | `test/integ/basic.cpp` | API-ported (one rename) |
| Overlapping (regex vs regex; exact vs prefix; exact vs regex) | `basic_suite::overlapping_endpoints` | `test/integ/basic.cpp` | API-ported; v2 changes the precedence story — see "Documented divergences" |

The v2 parity TU itself (`routing_regression_test.cpp`) carries one
`LT_BEGIN_AUTO_TEST` per pattern row. Each test drives the public
registration surface and probes `webserver_impl::lookup_v2()` (via the
`webserver_test_access` friend hook gated on `HTTPSERVER_COMPILATION`)
to pin tier classification, method_set composition, captures, and
prefix flagging.

## Documented divergences

These are v2 semantics that deliberately differ from v1, with the
rationale. The corresponding assertions in `routing_regression_test.cpp`
are pinned to v2 behavior; if v2 ever changes, the test edit makes the
new contract explicit rather than silent.

### 1. `*_nonexistent_method` tests removed (TASK-021)

v1 had two tests in `http_resource_test.cpp` —
`set_allowing_nonexistent_method` and `is_allowed_nonexistent_method` —
that exercised allowing/disallowing methods by string name including
strings outside the `http_method` enum. v2 replaced the boolean
per-method map with the `method_set` bitmask (DR-021). The bitmask
makes "nonexistent method by name" structurally unreachable, so the
tests were dropped. Two more were renamed:

- `resource_init_sets_all_methods` → `default_state_all_methods_set`
- `get_allowed_methods_only_returns_true` → `get_allowed_methods_only_returns_set`

One new test was added: `set_allowing_count_sentinel_has_no_effect`.

No port required.

### 2. `unregister_resource` → `unregister_path` rename (TASK-024)

v2 splits resource registration into the public `register_path` /
`register_prefix` pair (TASK-024). The symmetric removal API renames
to match: `unregister_resource(path)` → `unregister_path(path)`, with
a new `unregister_prefix(path)` for the prefix half. The old name is
preserved as a `[[deprecated]]` forwarder for source compatibility.
The v1 test was renamed in place; behavior unchanged.

### 3. Custom-regex parameter constraints — RESOLVED (v1 parity restored)

In v1, `/items/{id|([0-9]+)}` was enforced by the `registered_resources_regex`
map (deleted in TASK-067): the per-segment `[0-9]+` constraint
participates in `std::regex_match`, so `/items/abc` does not match the
route.

Between the TASK-053 cutover and the current commit, the v2 radix tier
briefly diverged: `{id|([0-9]+)}` was stored as a single wildcard
segment with the full source token as its name, with no constraint
enforcement. `/items/abc` and `/items/42` both resolved to the same
radix entry.

**Resolution**: the radix tier now parses `{name|regex}` at registration
time, binds captures under the bare `name`, and stores the compiled
regex on the wildcard node (`radix_node::wildcard_constraint_`, using
the same `extended | icase | nosubs` flags as `http_endpoint`). During
`radix_tree::find()` the constraint is matched against the request
segment; a miss breaks the descent so the lookup falls back to a prefix
candidate or 404. `regex_matching_arg_custom`, `regex_url_exact_match`,
and `parameterized_with_custom_regex_enforced_in_radix_tier` now pin
the v1 behaviour directly.

### 4. Overlapping-routes precedence: v1 iteration-order accident → v2 deterministic structural precedence

v1's `basic_suite::overlapping_endpoints` documents:

```
LT_CHECK_EQ(s, "2");   // Not sure why regex wins, but it does...
```

— i.e., when two parameterized routes both match `/foo/bar/`, v1 picks
the second-registered one due to `std::map` iteration order. This is
explicitly called out in the v1 test comment as an unintended
accident, not a designed behavior.

v2 gives a deterministic structural precedence: the tier order is
exact → radix → regex, and within the radix tier the trie walk visits
exact children before wildcard children. Two wildcard-rooted routes
that both match are resolved by first-registration insertion order
into the wildcard chain. The pinned test
(`overlapping_two_regex_routes_deterministic_first_wins`) asserts that
the resolved handler is the first-registered `shared_ptr` (`*hp ==
first`), pinning the deterministic first-wins contract. The earlier
"could be either one" wording was a v1-era hedge that no longer
matches v2 behavior.

The companion test (`later_exact_registration_shadows_earlier_regex`)
pins the cleaner half of `basic_suite::overlapping_endpoints`: when an
exact route is later registered on the same path, it shadows the
earlier parameterized route. The v2 lookup pipeline gives this for
free because exact tier is walked first.

### 5. Trailing-slash and leading-slash canonicalization in `lookup_v2`

v1 canonicalizes both the registered path and the incoming request
path by constructing `http_endpoint(path, family, registration=false)`
at dispatch time — strip trailing `/`, prepend `/` if absent, then
match. So `/ok`, `/ok/`, `ok`, and `ok/` are all equivalent at
dispatch.

The v2 `lookup_v2` initially took the raw path string and probed
`exact_routes_` / radix / regex directly, which made `/ok` (stored
canonical) miss against an `/ok/` request. TASK-028 fixed this in
`webserver.cpp` by adding `canonicalize_lookup_path()` and applying
it at the head of `lookup_v2`, including cache keying. This brings v2
in line with v1 semantics; the test
`exact_path_normalization_aliases` pins it.

### 6. CONNECT-method client-roundtrip integ test (TASK-078)

`test/integ/basic.cpp` previously contained two `/* ... */`-commented
CONNECT bodies inside `basic_suite::complete` and `basic_suite::only_render`
that issued `curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CONNECT")`
through the high-level libcurl easy API. They were commented out in
2017 (commit bd39cb4) because libcurl treats CONNECT as a tunnel-establishing
verb and waits for the socket to be upgraded; `curl_easy_perform` then
hangs against a plain HTTP server. The bodies have been deleted; there is
no way to drive a CONNECT round-trip through `curl_easy_perform` and the
high-level easy API has no other shape that produces a useful integ
assertion against an HTTP server that is not also a proxy.

The method-to-render-fn table in `src/detail/webserver_request.cpp` (the
`methods[]` array around line 608) maps the `CONNECT` wire token to
`http_resource::render_connect`. The `render_connect` return-type
contract is pinned by a file-scope `static_assert` in
`test/unit/http_resource_test.cpp` (around line 138) — there is no named
test/symbol `render_connect_returns_by_value`. No test exercises the
`methods[]` dispatch-table mapping itself. The deleted integ blocks
added no coverage beyond the static_assert.

No port required; not a v1-only feature, but a v1-era libcurl misuse.

## How to extend

When adding a new routing pattern to the public API:

1. Add an `LT_BEGIN_AUTO_TEST` to `test/unit/routing_regression_test.cpp`
   driving the public registration surface and probing `lookup_v2()`.
2. Add a row to the **Pattern taxonomy** table above.
3. If the new pattern intentionally diverges from v1 semantics, add a
   subsection under **Documented divergences** with the rationale.

## Cross-references

- [`specs/architecture/09-testing.md`](../specs/architecture/09-testing.md) item 5 — testing strategy.
- [`specs/architecture/12-open-questions.md`](../specs/architecture/12-open-questions.md) AR-003 — release-blocker risk.
- [`specs/tasks/M5-routing-lifecycle/TASK-028.md`](../specs/tasks/M5-routing-lifecycle/TASK-028.md) — this gate's source task.
- [`specs/tasks/M5-routing-lifecycle/TASK-027.md`](../specs/tasks/M5-routing-lifecycle/TASK-027.md) — the 3-tier table this gate validates.
- [`specs/tasks/M5-routing-lifecycle/TASK-036.md`](../specs/tasks/M5-routing-lifecycle/TASK-036.md) — downstream consumer (dispatch cutover).
