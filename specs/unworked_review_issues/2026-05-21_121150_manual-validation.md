# Unworked Review Issues

**Run:** 2026-05-21 12:11:50
**Task:** manual-validation
**Total:** 56 (0 critical, 12 major, 44 minor)

## Major

1. [x] **code-quality-reviewer** | `src/webserver.cpp:151` | code-elegance
   The file opens and closes `namespace detail` ten times (lines 151, 420, 519, 801, 1251, 1448, 1528, 1717, 1769, 1993). Three of these open/close pairs are new in this PR. Each refactored helper that belongs to `webserver_impl` is wrapped in its own `namespace detail { … }  // namespace detail` island rather than being folded into the existing block that already surrounds that section of the file. This violates the 'separate concepts vertically' and 'related code should appear vertically dense' principles — a reader must scroll past repeated namespace open/close lines to understand which functions belong together.
   *Recommendation:* Fold the new `webserver_impl` helper definitions into the nearest existing `namespace detail` block instead of opening a fresh block. If a helper must be placed between two non-detail sections, prefer moving the non-detail code or reordering the helper to keep the namespace contiguous.
   *Status:* already addressed by webserver.cpp decomposition into src/detail/ — webserver.cpp now contains only 1 open/close of namespace detail (lines 150-172); remaining helpers live in their own TU files each with a single namespace block.

2. [x] **code-quality-reviewer** | `src/websocket_handler.cpp:113` | error-handling
   `send_control_frame` only sets `valid = false` when `send_all` fails. If `encode(...)` itself returns a non-OK status (e.g. the stream is in an error state), the function returns silently with no indication of failure — the caller's `valid` flag remains `true` and the session appears healthy. The original `send_ping`/`send_pong` code had the same pattern, but the factoring-out opportunity was not taken to harden it.
   *Recommendation:* Add an `else` branch (or set `valid = false` unconditionally when `encode` does not return `MHD_WEBSOCKET_STATUS_OK`) so encode failures surface as a closed session. Alternatively, document in a comment that a failed encode is intentionally treated as a no-op if the existing behavior is by design.
   *Status:* fixed in this batch — else branch added in src/websocket_handler.cpp setting valid=false on encode failure (commit 1275e2a).

3. [x] **code-simplifier** | `src/webserver.cpp:2462` | code-structure
   `resolve_method_callback` is a long if-else-if chain (9 branches) matching strcmp against each method string to set both the pointer-to-member callback and the method_set enum. The two assignments per branch mean any future new method requires touching three things: the enum, the chain, and the method string table — violating the single source-of-truth principle.
   *Recommendation:* A static lookup table mapping method wire strings to (callback, http_method) pairs would replace the chain with a single linear scan (or a hash probe). This also reduces the function's CCN from ~10 to ~3. The data-driven approach is more readable and extensible.
   *Status:* fixed in this batch — resolve_method_callback rewritten as a data-driven static table in src/detail/webserver_request.cpp (commit 1275e2a).

4. [x] **code-simplifier** | `src/webserver.cpp:339` | naming
   `route_tier_kind::regex_` uses a trailing underscore to avoid the C++ reserved token `regex`. The enum tag itself is `route_tier_kind`, so the member could just be spelled `regex_kind` or `pattern` to communicate the same thing without the awkward suffix that implies a name collision.
   *Recommendation:* Rename `route_tier_kind::regex_` to `route_tier_kind::pattern` (or `route_tier_kind::regex_pattern`) to avoid the underscore escape and make the label self-describing.
   *Status:* fixed in this batch — route_tier_kind::regex_ renamed to route_tier_kind::pattern across route_tier.hpp, webserver_register.cpp, webserver_routes.cpp (commit 1275e2a).

5. [x] **code-simplifier** | `src/webserver.cpp:576` | naming
   `upsert_v2_param_route` is named after the storage tier ('param') but the same function also handles prefix routes (because both share param_and_prefix_routes_). The name understates what it actually owns.
   *Recommendation:* Rename to `upsert_v2_radix_route` or `upsert_v2_trie_route` to match the storage tier name used throughout the surrounding code (radix, trie, param_and_prefix_routes_).
   *Status:* fixed in this batch — upsert_v2_param_route renamed to upsert_v2_radix_route in webserver_routes.cpp and webserver_impl_dispatch.hpp (commit 1275e2a).

6. [x] **code-simplifier** | `src/webserver.cpp:592` | code-structure
   insert_fresh_v2_entry and update_existing_v2_entry both build a route_entry struct inline — the exact same three-field assignment (methods, handler, is_prefix=false) — before switching on tier. The construction idiom is repeated in both functions and in register_v2_route at line 443.
   *Recommendation:* Extract a one-liner `make_route_entry(methods, handler)` that returns a pre-filled route_entry with is_prefix=false, then call it in all three sites. This eliminates the repeated three-field pattern and makes the intent of each caller immediately clear.
   *Fixed:* Added `make_non_prefix_entry(methods, shim)` static helper in `src/detail/webserver_routes.cpp` and call it from both branches of `insert_fresh_v2_entry`. A docstring records why the other two call sites (`register_v2_route`, `update_existing_v2_entry`) stay open-coded: one uses `method_set::set_all()` plus a caller-controlled `is_prefix`, the other merges into an existing entry rather than constructing a new one — so the helper's contract doesn't fit them.

7. [x] **performance-reviewer** | `src/webserver.cpp:1874` | memory-allocation
   canonicalize_lookup_path allocates a std::string on every lookup_v2 call. This function is on the hot path (called once per request before the cache probe) and returns a new string even for paths that need no transformation.
   *Recommendation:* Accept the path as std::string_view and return early with a string_view (or a small inline buffer) when no transformation is needed.
   *Acknowledged-deferred:* This is a suspected per-request allocation on the lookup hot path. TASK-058 was the dedicated task slot for this optimisation but landed with a narrower scope. The bench_route_lookup harness added in TASK-053 step 5 is now the right gate — once a target run shows canonicalize_lookup_path in the warm-path profile, the string_view + small-inline-buffer rewrite is the correct shape. Closing the finding with a deferred-until-profile note; the bench harness is in place to retire the deferral when the data supports it.

8. [x] **performance-reviewer** | `src/webserver.cpp:1976` | memory-allocation
   normalize_path in should_skip_auth allocates a std::vector<std::string> and a std::string per element via apply_normalized_segment, then builds the result with string concatenation. This runs on every request that has an auth_handler configured, before any skip-list comparison.
   *Recommendation:* Pre-normalize the auth_skip_paths entries once at registration time (when the webserver is built via create_webserver) so normalize_path is never called per request.
   *Acknowledged-deferred:* Same posture as item #7 and as task-027 items 11–13. Pre-normalising `auth_skip_paths` at registration time is the right shape and is grouped with the wider auth-skip lookup refactor (referenced in earlier review files); the bench harness should drive the timing.

9. [x] **performance-reviewer** | `src/webserver.cpp:2347` | missing-caching
   serialize_allow_methods builds a std::string from scratch on every 405 response, iterating over all 9 method bits each time. For servers with high 405 rates (strict REST APIs), this string is constructed repeatedly with identical content for a given route entry.
   *Recommendation:* Cache the serialized Allow string inside route_entry at registration time (computed once when the method_set is finalized). The method_set is immutable after registration, so a cached string is safe. At dispatch time, serialize_allow_methods becomes a trivial string copy or a string_view return.
   *Acknowledged-deferred:* This is a per-405-response allocation, not per-request, so the "hot path" claim only bites for endpoints that genuinely serve high 405 volume. Caching inside `route_entry` is a straightforward design once a profile justifies it; the existing shared helper (TASK-048) already keeps the construction in one place, so a future swap to a cached field is a localised change.

10. [x] **security-reviewer** | `.github/workflows/verify-build.yml:454` | supply-chain
    PMD distribution zip is fetched from github.com release URL over HTTPS but with no SHA-256 checksum verification before execution. The workflow runs `sudo unzip -q -o /tmp/pmd.zip -d /opt` with no integrity check. This contrasts with every libmicrohttpd tarball fetch in the same workflow, all of which include an explicit `sha256sum -c` step (e.g. line 556). A compromised GitHub release asset or a MITM between the runner and the CDN edge could inject a malicious `pmd` binary that runs as root in /opt.
    *Recommendation:* Add a sha256sum check immediately after the curl download, mirroring the pattern used for libmicrohttpd: `echo "<expected-sha256>  /tmp/pmd.zip" | sha256sum -c`. Pin the expected digest in the workflow file (or in a companion checksums file) and update it with each PMD version bump.
    *Fixed:* TASK-059 ("sha256-pin PMD analyzer download in CI", commit 7abbe4e + validation iter1 48a70a5) implemented exactly this — `PMD_SHA256=110934b…` + `shasum -c` gate, plus a rotation comment. Finding pre-dates the fix.

11. [x] **security-reviewer** | `/Users/etr/progs/libhttpserver/src/httpserver/detail/webserver_impl.hpp:100` | sensitive-data-handling
    The per-connection arena (`monotonic_buffer_resource` over an embedded initial buffer inside `connection_state`) is zeroed on each request-completion via `reset_arena()` (documented at webserver.cpp line 1270-1271). However, the initial allocation of `connection_state` via `new detail::connection_state()` in `connection_notify` (webserver.cpp line 1302) does NOT zero the embedded buffer before any request uses it. If the arena's initial buffer is backed by OS memory that was previously used by another process (or by a prior libhttpserver session in the same process after `stop()`/`start()`) the first request on a fresh connection could have its pmr allocations land in memory that still contains stale credential or payload data from a prior use. The `reset_arena()` path correctly clears after each keep-alive request but the very first request on a new connection is unprotected.
    *Recommendation:* Zero-initialize the embedded buffer in `connection_state`'s constructor. If the buffer is a `std::array<std::byte, N>`, change the member initializer from default initialization to value-initialization (i.e., `buffer_{}` not `buffer_`). Alternatively, add an explicit `std::fill(buffer_.begin(), buffer_.end(), std::byte{0})` in the constructor body. This matches the `reset_arena()` contract that already zeroes on teardown.
    *Status:* already addressed by prior TASK-016 work — connection_state.hpp declares `initial_buffer_{}` (value-initialized to zero), satisfying the recommendation.

12. [x] **security-reviewer** | `/Users/etr/progs/libhttpserver/src/webserver.cpp:1554` | input-validation
    In `setup_new_upload_file_info`, when `generate_random_filename_on_upload` is false, the disk path is assembled by simple string concatenation: `parent->file_upload_dir + "/" + safe_name`. `sanitize_upload_filename` (http_utils.cpp line 283) correctly strips directory separators and rejects '.' and '..' but it does NOT strip or reject embedded null bytes. A multipart filename field containing a null byte (e.g. `foo.txt\x00.php`) would be truncated by C-string filesystem calls at the null but `safe_name` is a `std::string` so the full byte sequence (including the null) is passed to `std::ofstream::open()`. On Linux `open()` is a syscall that takes a `const char*`; the `c_str()` call on the concatenated path will silently truncate at the embedded null, potentially creating the file at the truncated location (CWE-626 / null byte injection). The random-filename branch is not affected.
    *Recommendation:* Extend `sanitize_upload_filename` to reject any string that contains a null byte: `if (basename.find('\0') != std::string::npos) return "";`. Also consider adding a maximum length guard (e.g. 255 bytes) to prevent excessively long filenames from exceeding POSIX NAME_MAX on the target filesystem.
    *Status:* fixed in this batch — null-byte rejection added to sanitize_upload_filename in src/http_utils.cpp; two new tests added in test/unit/http_utils_test.cpp (commit 1275e2a).

## Minor

13. [x] **architecture-alignment-checker** | `scripts/check-complexity.sh:1` | pattern-violation
    The per-function cyclomatic-complexity gate (check-complexity.sh via lizard) and copy/paste detection gate (check-duplication.sh via PMD CPD) are fully implemented, wired into the CI lint lane in .github/workflows/verify-build.yml (lines 676-682), and listed in Makefile.am EXTRA_DIST. However, neither gate is mentioned in the architecture specification. Section 09-testing.md enumerates six named test surfaces (header hygiene, build-flag invariance, move semantics, SBO size, routing semantics, thread-safety stress) but omits complexity and duplication gates. Section 08-build-and-packaging.md describes the build toolchain without mentioning code-quality enforcement scripts. The ChangeLog entry correctly records these as architectural quality controls, but the architecture document is the normative source of truth for CI gate inventory.
    *Recommendation:* Add a paragraph to 09-testing.md (or 08-build-and-packaging.md) describing these two gates as architectural quality controls: CCN ceiling of 10 (enforced by lizard via check-complexity.sh, wired to the 'lint' CI lane), and zero-duplication above 100 tokens (enforced by PMD CPD via check-duplication.sh, same lane). Reference the ratchet-down strategy documented in the ChangeLog. This is documentation debt, not a behavioral violation.
    *Status:* deferred — 09-testing.md not yet updated to mention complexity/duplication gates.

14. [x] **architecture-alignment-checker** | `src/httpserver/detail/radix_tree.hpp:117` | adr-violation
    radix_tree gained match_root_terminus as a new public method (lines 117-130). The architecture spec (04-components/route-table.md) states: 'v2.0 commits only to the outer shape (three-tier with cache), not the radix-tree implementation choice.' The new method is an implementation-internal helper extracted from find() for CCN reduction; it is not part of any public-facing API and is only callable from within the same internal header. This is fully consistent with the spec's intent. The finding is noted only because match_root_terminus is technically public on the class (not private), which means any code within the HTTPSERVER_COMPILATION boundary could call it directly rather than going through find(). In practice no caller does so; the method exists solely to keep find()'s CCN below 10.
    *Recommendation:* Mark match_root_terminus as private (or at minimum document it as an internal helper not part of the radix_tree contract). Since radix_tree has no external callers beyond webserver_impl and the spec explicitly defers implementation choice, making it private reinforces that the method is an implementation detail of find() and cannot be relied upon by future refactors.
    *Status:* deferred — match_root_terminus remains public in radix_tree.hpp; private accessor or documentation comment not yet added.

15. [x] **architecture-alignment-checker** | `src/httpserver/detail/webserver_impl.hpp:370` | adr-violation
    webserver_impl has grown approximately 30 new method declarations in this refactor sweep (build_mhd_option_array, add_base_mhd_options, add_tls_mhd_options, add_gnutls_mhd_options, add_extended_mhd_options, add_https_extra_options, compose_start_flags, compose_transport_flags, compose_runtime_flags, try_handle_websocket_upgrade, validate_websocket_handshake, complete_websocket_upgrade, resolve_resource_for_request, lookup_route_cache, scan_regex_routes, store_route_cache, apply_extracted_params, apply_auth_short_circuit, dispatch_resource_handler, serialize_allow_methods, materialize_and_queue_response, prepare_or_create_lambda_shim, commit_handlers_to_shim, insert_fresh_v1_entries, upsert_v2_table_entry, upsert_v2_param_route, insert_fresh_v2_entry, update_existing_v2_entry, handle_post_form_arg, setup_new_upload_file_info, manage_upload_stream, process_file_upload). The architecture spec (04-components/webserver.md) describes webserver_impl as holding 'the MHD_Daemon*, the route-table data structures, per-connection arena state, and synchronization primitives' and 'dispatch helpers'. The new helpers all fit within the 'dispatch helpers' charter and stay entirely behind the PIMPL barrier — <microhttpd.h> does not escape to any public header. However, the architecture spec does not explicitly anticipate webserver_impl acting as the host for fine-grained sub-helpers of start(), finalize_answer(), post_iterator(), and on_methods_(). The growth is directionally correct but the spec's description of webserver_impl's scope is now narrower than the implementation.
    *Recommendation:* Update 04-components/webserver.md to expand the webserver_impl description: 'webserver_impl holds ... and the decomposed dispatch helpers for start(), finalize_answer(), post_iterator(), and on_methods_() carved out to enforce the CCN=10 ceiling.' This makes the spec accurately reflect the agreed scope of webserver_impl and prevents future reviewers from flagging the helper density as a violation.
    *Status:* deferred — 04-components/webserver.md not yet updated to reflect expanded webserver_impl helper scope.

16. [x] **architecture-alignment-checker** | `src/httpserver/http_utils.hpp:440` | pattern-violation
    ip_representation (in the public header http_utils.hpp) now carries five private member helpers (parse_ipv4, parse_ipv6, compute_ipv6_omitted_segments, apply_ipv6_part, parse_nested_ipv4). Similarly, http_endpoint (detail/http_endpoint.hpp, lines 195-202) now carries five private member helpers (normalize_url_complete, process_url_part, append_non_registration_part, append_literal_url_part, append_parameter_url_part, compile_regex_url). This decomposition keeps both structs/classes below CCN=10 per function and leaves public surfaces unchanged. However, the architecture specification does not explicitly address the pattern of adding private member helpers to public-namespaced or detail-layer structs as a CCN-reduction technique. The architecture distinguishes between the PIMPL barrier (where implementation grows freely behind the barrier) and public/detail-layer types (where the spec focuses on public surface stability). ip_representation is in the public header; adding private helpers to it is architecturally safe but sets a precedent that is not ratified in any DR.
    *Recommendation:* Consider adding a short note to 05-cross-cutting.md or a new DR entry ratifying the pattern: 'Private member helpers may be added to any class or struct to decompose functions exceeding CCN=10, provided the public surface is unchanged. This applies at all layers including public-header types.' Without this ratification, future reviewers may question whether private helpers on public-header types constitute inadvertent ABI surface (they do not in standard C++ — private members are not part of the public ABI — but the architecture should make this explicit).
    *Status:* deferred — 05-cross-cutting.md not yet updated to ratify private-helper CCN decomposition pattern.

17. [x] **code-quality-reviewer** | `src/http_utils.cpp:412` | code-readability
    `ip_representation::parse_nested_ipv4` takes `const std::vector<std::string>& parts` and `unsigned int i` to reference `parts[i]` internally, and also takes `int y` as the byte offset. The combination of three positional numeric parameters (`parts`, `i`, `y`) with no named-parameter wrapper makes call-site reasoning fragile — a transposed argument would compile silently.
    *Recommendation:* Consider documenting the parameter roles in the function signature comment or converting `y` to a named type alias. This is a minor readability concern only.
    *Status:* deferred — parse_nested_ipv4 parameter naming unchanged; no comment added.

18. [x] **code-quality-reviewer** | `src/http_utils.cpp:430` | test-coverage
    The `compute_ipv6_omitted_segments` refactor introduces a specific new code path: an IPv6 address with a leading `::` AND a trailing nested IPv4 dotted-quad (e.g. `::ffff:1.2.3.4`). The pre-existing test suite exercises `ip_representation` but the PR description notes no new tests were added. This specific combination (empty_count == 2 with a trailing dot segment triggering `omitted -= 1`) is the most structurally complex branch in the new helper.
    *Recommendation:* Add a unit test for `ip_representation("::ffff:192.168.1.1")` and `ip_representation("::192.168.1.1")` to the existing ip_representation test suite to pin the omitted-segment accounting for IPv4-mapped addresses.
    *Status:* deferred — no new tests added for compute_ipv6_omitted_segments edge cases.

19. [x] **code-quality-reviewer** | `src/httpserver/detail/http_endpoint.hpp:192` | code-readability
    The five new private helper methods (`normalize_url_complete`, `process_url_part`, `append_non_registration_part`, `append_literal_url_part`, `append_parameter_url_part`, `compile_regex_url`) are declared in the private section of `http_endpoint` with a block comment explaining their purpose. However, `bool& first` is passed by non-const reference through multiple levels of these helpers as a mutable flag — a pattern that the Clean Code 'no flag arguments' rule flags. While the decomposition is a net improvement, the shared mutable `first` bool makes call-site reasoning slightly harder.
    *Recommendation:* Consider encapsulating the 'have we seen the first piece yet' state into a small local struct or by rewriting the loop to handle the first iteration differently (e.g. an index check `i == 0`). This is a minor style improvement and not blocking.
    *Status:* deferred — bool& first flag argument pattern unchanged in http_endpoint helpers.

20. [x] **code-quality-reviewer** | `src/httpserver/http_method.hpp:127` | code-elegance
    The `to_string` switch-to-array refactor is a clean improvement, but the `constexpr std::array` is declared inside the `constexpr` function body. While this is valid C++17/20, the array is conceptually a file-scope constant (it never changes). Declaring it inside the function body means it is technically re-initialized on every call in non-consteval contexts, and any future addition of a new `http_method` value requires updating both the enum and the array with no compile-time enforcement beyond the array size assertion.
    *Recommendation:* Add a `static_assert(names.size() == static_cast<std::size_t>(http_method::count_), "names array out of sync with http_method enum");` immediately after the array declaration — this enforces the synchronization contract at compile time. The existing guard only checks that `count_` is the right sentinel value, not that every enum value has a name string.
    *Status:* deferred — static_assert for names.size() == http_method::count_ not yet added to to_string() in http_method.hpp.

21. [x] **code-quality-reviewer** | `src/webserver.cpp:806` | code-readability
    `make_option` is declared `static` inside `namespace detail` (line 806). The `static` storage class specifier on a free function inside a named namespace is redundant — internal linkage is better achieved with an anonymous namespace, or simply by omitting `static` (the function is not visible outside the TU anyway since it is not declared in any header). Mixing both namespace and static is a naming-rule inconsistency relative to the rest of the file.
    *Recommendation:* Either move `make_option` into the anonymous namespace block immediately above, or drop the `static` keyword. The anonymous namespace option is more idiomatic modern C++.
    *Status:* deferred — make_option in webserver_setup.cpp still declared `static` inside `namespace detail`; the redundant static keyword not yet removed.

22. [x] **code-quality-reviewer** | `test/unit/http_utils_test.cpp:1003` | readability
    sanitize_upload_filename_trailing_slash is the only new test whose name does not fully capture the expected outcome: the name says 'trailing slash' but the contract pinned is 'trailing separator yields empty basename -> returns empty string'. The comment on line 1002 rescues the intent but the name alone is ambiguous.
    *Recommendation:* Rename to sanitize_upload_filename_trailing_slash_returns_empty to make the expected outcome self-documenting without relying on the adjacent comment.
    *Status:* wontfix — cosmetic/style preference, no functional impact; test-name rename; pure readability

23. [x] **code-quality-reviewer** | `test/unit/webserver_on_methods_test.cpp:562` | test-coverage
    allow_header_get_head_set_is_ordered tests {GET, HEAD} but HEAD is both the second-lowest enum value (index 1) and alphabetically second, so this case cannot distinguish enum-order from alphabetical order. The test intent is sound but it is a weaker discriminator than the three-method test below it.
    *Recommendation:* Add a comment noting that this specific pair does not distinguish the two orderings, or replace it with {HEAD, POST} (enum: HEAD=1, POST=2; alphabetical: HEAD < POST too) vs {DELETE, GET} (enum: GET=0, DELETE=4; alphabetical: DELETE < GET) which would unambiguously discriminate. Alternatively, the existing three-method test (allow_header_get_post_put_set_is_enum_ordered) already covers the discriminating case, so the two-method test can remain as-is with an explanatory comment.
    *Status:* deferred — allow_header_get_head_set_is_ordered test not yet updated with clarifying comment or stronger discriminator.

24. [x] **code-quality-reviewer** | `test/unit/webserver_register_path_prefix_test.cpp:399` | test-coverage
    Port 8190 (PORT+10) used by auth_skip_single_dot_segment_is_elided collides with the base PORT=8190 defined in webserver_on_methods_test.cpp. Ports 8191 (PORT+11) and 8192 (PORT+12) from this file also overlap with on_methods PORT+1 and PORT+2. Both files compile to separate test binaries; under sequential `make check` the tests pass because each binary starts and stops its server before the next binary runs. Under `make -j check` (or any parallel test runner) all three pairs could bind the same port simultaneously and cause intermittent failures.
    *Recommendation:* Shift the normalize_path / should_skip_auth group (PORT+8..+12) to PORT+13..+17 (8193..8197) to clear the overlap, or set PORT in webserver_on_methods_test.cpp to 8220 so its range 8220..8243 does not overlap with any existing file.
    *Status:* deferred — port collision between webserver_register_path_prefix_test.cpp and webserver_on_methods_test.cpp not yet resolved.

25. [x] **code-simplifier** | `/Users/etr/progs/libhttpserver/test/unit/http_utils_test.cpp:807` | naming
    `ip_representation_middle_bytes_comparison` substantially overlaps with `ip_representation_ffff_comparison` (lines 805-818): both construct `a("::ffff:192.168.1.1")` and `b("::192.168.1.1")` and assert `a < b == false`. The second test adds a `bool result = a < b` temporary variable and a `c < d` check that is already present in `ip_representation_ffff_comparison` (line 816-817). The test body comment refers to internal line numbers of the production source (`lines 489-494`), which will become stale when source is edited.
    *Recommendation:* Consolidate the two tests into one, removing the source-line-number comment. Name the single test after the observable contract (e.g. `ip_representation_ffff_prefix_compares_greater_than_zero_prefix`) and assert both the symmetric false case and the ordering case in that one test.
    *Status:* wontfix — cosmetic/style preference, no functional impact; test consolidation is naming polish

26. [x] **code-simplifier** | `/Users/etr/progs/libhttpserver/test/unit/http_utils_test.cpp:911` | code-structure
    `dump_header_map_empty_prefix` (lines 911-921) uses `output.find(...) != std::string::npos` wrapped in `LT_CHECK_EQ(..., true)` instead of a direct substring check. The file already has `dump_header_map_no_prefix` (lines 739-748) that tests empty-prefix formatting directly with `LT_CHECK_EQ(ss.str(), ...)`. The new test therefore adds weaker assertions on functionality already covered more precisely by the older test.
    *Recommendation:* Either remove `dump_header_map_empty_prefix` as redundant with `dump_header_map_no_prefix`, or give it a distinct purpose (e.g. testing `header_view_map` vs the existing `map<string_view,string_view,...>` type). If kept, replace the `find != npos` pattern with a direct equality assertion on `ss.str()` to match the style used throughout the file.
    *Status:* deferred — dump_header_map_empty_prefix not yet strengthened or removed.

27. [x] **code-simplifier** | `/Users/etr/progs/libhttpserver/test/unit/http_utils_test.cpp:924` | naming
    Test name `ip_representation_comparison_equal` is anchored to a misleading comment header ('Test get_ip_str with nullptr (edge case)') that describes a completely different intent. The comment was copy-pasted from the previous nullptr test and was never updated.
    *Recommendation:* Remove or correct the misleading comment on line 923 so readers understand this test checks equal-address less-than symmetry, not a nullptr edge case.
    *Status:* wontfix — cosmetic/style preference, no functional impact; misleading comment fix; naming polish

28. [x] **code-simplifier** | `src/detail/http_endpoint.cpp:45` | naming
    `normalize_url_complete` mutates `url_complete` in place (strips trailing '/', prepends leading '/') but its name suggests a read or return operation. Callers do not get a normalized copy — the method modifies `this`.
    *Recommendation:* Rename to `fix_url_complete_boundaries` or `trim_url_complete` to make the side-effect explicit. Alternatively, prefix with `ensure_` (e.g. `ensure_url_complete_boundaries`) to match the pattern used by `ensure_headerlike_cache` elsewhere in the codebase.
    *Status:* deferred — normalize_url_complete not yet renamed to reflect its in-place mutation semantics.

29. [x] **code-simplifier** | `src/http_request.cpp:506` | code-structure
    `extract_x509_common_name` duplicates the two-pass call/fill/strip-null pattern from `extract_x509_string` (the generic parameterised helper immediately above it), but uses a different GnuTLS API (`gnutls_x509_crt_get_dn_by_oid`) that has the same two-pass shape. The comment at line 493 explains the pattern for the DN helpers, yet `extract_x509_common_name` does not reuse it.
    *Recommendation:* The two-pass pattern could be factored into `extract_x509_string` if it accepted an additional `oid` parameter, or a second overload. If the OID parameter makes the generic helper too wide, consider at least documenting why `extract_x509_common_name` cannot reuse it, so future maintainers understand the deliberate choice.
    *Status:* deferred — extract_x509_common_name duplication not yet refactored.

30. [x] **code-simplifier** | `src/http_utils.cpp:394` | naming
    `ipv4_mapped_prefix_invalid` uses negative naming (returns `true` when the prefix IS invalid). The anonymous-namespace sibling `is_v4_mapped_prefix_octet_pair` (line 558) uses positive naming for the same conceptual check from the other side. Having two helpers that test complementary conditions under different naming conventions makes callers harder to audit.
    *Recommendation:* Rename `ipv4_mapped_prefix_invalid` to `is_valid_ipv4_mapped_prefix` (flipping the return sense) to make both helpers use positive naming. This aligns with `is_v4_mapped_prefix_octet_pair` and avoids double-negation at call sites (`if (ipv4_mapped_prefix_invalid(...))` reads as `if (not valid)`).
    *Status:* wontfix — cosmetic/style preference, no functional impact; test rename to reflect intent; naming polish

31. [x] **code-simplifier** | `src/webserver.cpp:2273` | code-structure
    `store_route_cache` acquires the cache lock, prepends to route_cache_list, and then evicts from the back if the map exceeds ROUTE_CACHE_MAX_SIZE. The eviction logic (erase map entry, pop_back list) is correct but could easily be broken if the list/map invariant is misunderstood. A local comment explaining the invariant (each list element corresponds to exactly one map entry) is missing.
    *Recommendation:* Add a one-line inline comment above the eviction block: `// Map and list stay in sync: each eviction removes the same key from both.` This costs nothing and prevents future maintainers from accidentally breaking the LRU invariant.
    *Status:* deferred — LRU invariant comment not yet added in store_route_cache.

32. [x] **code-simplifier** | `src/webserver.cpp:315` | code-structure
    `stop_and_wait()` at line 315 is a one-line forwarder to `stop()` with no wait logic. The name promises blocking behaviour that the body does not deliver.
    *Recommendation:* Either implement the wait semantics (pthread_cond_wait loop) or remove the function and document that callers should use `stop()` directly. The misleading name is more harmful than the single forwarding line.
    *Status:* deferred — stop_and_wait() still forwards to stop() with no wait semantics; webserver.cpp:319.

33. [x] **code-simplifier** | `src/webserver.cpp:951` | code-structure
    `compose_transport_flags` and `compose_runtime_flags` both accumulate into a local `int flags = 0` through a series of `if (condition) flags |= FLAG;` statements and then return it. The functions are correctly small, but `compose_start_flags` is essentially a trivial OR of two calls plus the start_method — it is barely above the threshold where a dedicated function adds readability value.
    *Recommendation:* Consider inlining `compose_start_flags` into `start()` (it is its only caller) and retaining the two sub-helpers. The three-line body of compose_start_flags does not merit a function name of its own.
    *Status:* deferred — compose_start_flags not yet inlined; still a separate function.

34. [x] **housekeeper** | `/Users/etr/progs/libhttpserver/CONTRIBUTING.md:130` | documentation-stale
    CONTRIBUTING.md describes the cpplint exemption rules and the ChangeLog requirement but says nothing about the new cyclomatic-complexity gate (CCN ≤ 10 via lizard) or the copy/paste-duplication gate (PMD CPD, min-tokens 100). A contributor introducing a new complex function will see a CI failure with no in-tree explanation of what the gate is or how to run it locally.
    *Recommendation:* Add a 'Code quality gates' sub-section under Styleguides (or under Pull Requests) explaining: (1) every function in src/ must stay at or below CCN 10; (2) no copy/paste block ≥ 100 tokens; (3) how to run `make lint-complexity` and `make lint-duplication` locally before pushing.
    *Status:* deferred — CONTRIBUTING.md not yet updated with complexity/duplication gate documentation.

35. [x] **housekeeper** | `/Users/etr/progs/libhttpserver/README.md:88` | documentation-stale
    README.md documents `make check` and `make examples` but does not mention the two new local lint targets `make lint-complexity` and `make lint-duplication` that were added in this sweep. Contributors who want to run the same complexity/duplication checks that CI enforces have no in-tree reference for how to do so.
    *Recommendation:* Add a short note to the 'Build / install' section (near the existing `make check` line) listing `make lint-complexity` (requires lizard: `pip3 install lizard`) and `make lint-duplication` (requires PMD 7: `brew install pmd` / download from pmd.github.io).
    *Status:* deferred — README.md not yet updated with lint target documentation.

36. [x] **housekeeper** | `/Users/etr/progs/libhttpserver/specs/architecture/09-testing.md:null` | architecture-not-updated
    The architecture testing strategy (§9) lists six specific CI test surfaces (header hygiene, build-flag invariance, move semantics, SBO size, routing regression, thread-safety stress) but does not mention the new lint lane items: per-function cyclomatic complexity (CCN ≤ 10, lizard) and copy/paste duplication (PMD CPD, min-tokens 100). These are enforced as CI gates on the same `feature/v2.0` branch and belong alongside the other named CI surfaces. Similarly §8 (build-and-packaging) makes no mention of the new `make lint-complexity` / `make lint-duplication` Makefile targets.
    *Recommendation:* Add an item 7 to the §9 testing list: 'Per-function cyclomatic complexity and copy/paste duplication (CCN ≤ 10 via lizard; zero CPD hits at ≥ 100 tokens via PMD CPD): enforced in the CI lint lane via `make lint-complexity` / `make lint-duplication`.' Optionally note the two new targets in §8 under the Autoconf/Makefile paragraph. Run /groundwork:source-architecture-from-code to capture these changes.
    *Status:* deferred — specs/architecture/09-testing.md not yet updated.

37. [x] **performance-reviewer** | `src/http_utils.cpp:564` | missing-caching
    ip_representation::operator< computes two running score sums by calling accumulate_octet_score 14 times (16 octets minus indices 10 and 11 skipped in the first loop) on every comparison. ip_representation is used as a std::set key (bans and allowances sets) so this comparator runs O(log N) times per policy_callback invocation. accumulate_octet_score itself is defined in an anonymous namespace and not marked inline.
    *Recommendation:* Mark accumulate_octet_score inline (or move it into the header as a constexpr helper). The function body is three lines; most compilers will inline it at -O2 without the hint, but the explicit keyword removes any ABI-linkage ambiguity and makes the intent clear. More impactful: consider caching a pre-computed numeric score inside ip_representation at construction time (a single int64_t) so operator< reduces to a scalar comparison, eliminating the 14-iteration scoring loop entirely.
    *Status:* deferred — accumulate_octet_score not yet marked inline in src/detail/ip_representation.cpp; per-construction score caching not implemented.

38. [x] **performance-reviewer** | `src/httpserver/detail/radix_tree.hpp:104` | memory-allocation
    radix_tree::insert and radix_tree::find both call tokenize(), which calls http_utils::tokenize_url, which calls string_split, allocating a std::vector<std::string> of heap-owned segment strings on every insert and every cache-miss find. On a cache miss the find path pays this allocation in addition to the hash probe and trie walk.
    *Recommendation:* For the find path, tokenize into a small-buffer vector of string_views over the input path to avoid heap allocation entirely. A std::array<std::string_view, N> with a fallback to vector would cover the common case of paths with fewer than N segments (typically ≤ 8) with zero heap usage. The insert path (registration time) can keep the owning string allocation since it is called once.
    *Status:* deferred — radix_tree::find still allocates vector<string> via tokenize_url on every cache-miss call.

39. [x] **performance-reviewer** | `src/webserver.cpp:1619` | memory-allocation
    post_iterator line 1619: when file_upload_target is not FILE_UPLOAD_DISK_ONLY, each upload chunk appends to the in-memory argument via `std::string(mr->dhr->get_arg(key)) + std::string(data, size)`. This constructs two temporary std::string objects and performs a concatenation on every chunk, giving O(n^2) total allocation growth for large uploads.
    *Recommendation:* Use the existing grow_last_arg path (which appends in place) for the memory path too, similar to how handle_post_form_arg uses it for regular form fields. Replace the set_arg_flat(key, std::string(get_arg)+std::string(data)) pattern with grow_last_arg(key, std::string(data, size)) so the buffer grows linearly.
    *Status:* deferred — O(n^2) in-memory upload append pattern not yet replaced with grow_last_arg.

40. [x] **performance-reviewer** | `src/webserver.cpp:1946` | memory-allocation
    lookup_v2 copies result.entry (a route_entry by value) out of both the exact_routes_ map and the radix tree on every cache miss, then copies it again into cache_value v for storage. route_entry contains a shared_ptr<http_resource> and a method_set; the shared_ptr copy bumps an atomic reference count on every miss.
    *Recommendation:* The copy is correct and necessary to drop the table lock before touching the cache. The shared_ptr refcount bump on cache miss is acceptable. Document this explicitly (it is already implicitly documented by the lock-order comment) so future readers do not attempt to return a pointer into the locked table.
    *Status:* deferred — explicit comment documenting the intentional shared_ptr copy not yet added in lookup_v2.

41. [x] **performance-reviewer** | `src/webserver.cpp:2092` | memory-allocation
    decorate_mhd_response builds a temporary std::string for each cookie via `k + '=' + v` before passing it to MHD_add_response_header. For responses with many cookies this allocates one string per cookie.
    *Recommendation:* Use a small stack buffer or a pre-sized string built via reserve: `std::string cookie_hdr; cookie_hdr.reserve(k.size() + 1 + v.size()); cookie_hdr = k; cookie_hdr += '='; cookie_hdr += v;` which avoids two intermediate string objects.
    *Status:* deferred — cookie string allocation not yet optimized in decorate_mhd_response.

42. [x] **performance-reviewer** | `src/webserver.cpp:2523` | memory-allocation
    answer_to_connection constructs a std::string from the raw `url` C-string pointer on every first-step call (line 2523: `std::string t_url = url;`). This is unavoidable for the unescape mutator, but the allocation happens before any cache probe. On very high request rates (TURBO mode, no body), this is the first heap touch per request.
    *Recommendation:* This is borderline for this code's architecture; the per-connection arena introduced in TASK-016 is the right long-term home for this string. File as a TASK comment rather than changing it now, since the arena is not yet plumbed into answer_to_connection's frame.
    *Status:* deferred — arena integration for t_url not yet implemented; recommendation to file as TASK noted.

43. [x] **security-reviewer** | `/Users/etr/progs/libhttpserver/src/http_request.cpp:1161` | sensitive-data-logging
    The `operator<<` debug dump for `http_request` (line 1161) prints `pass:"<password>"` to the output stream with no redaction. Any caller that routes `operator<<` output to a log sink (e.g. an access log or error log callback) will record plaintext Basic Auth credentials. This is a pre-existing pattern, but the refactor preserved it unconditionally on HAVE_BAUTH-off builds as well (it now prints two empty quoted strings, which is harmless, but the HAVE_BAUTH-on path still prints the live password). CWE-312 / CWE-532.
    *Recommendation:* Redact the password field in the stream operator: replace `r.get_pass()` with a fixed string like `"[REDACTED]"`, or gate the password print behind a compile-time `HTTPSERVER_DUMP_CREDENTIALS` define that defaults to off. The username can remain visible for debugging.
    *Status:* deferred — http_request operator<< still prints plaintext password at src/http_request.cpp:391.

44. [x] **security-reviewer** | `/Users/etr/progs/libhttpserver/src/http_request.cpp:537` | cryptographic
    The `verify_peer_certificate` helper calls `gnutls_certificate_verify_peers2` which does NOT check the hostname of the server certificate against the expected server name. For a *server* verifying a *client* certificate this is the correct API (SNI hostname checking is not applicable to client certs), but the function is named `verify_peer_certificate` without this nuance documented, which could mislead future readers into assuming full chain + hostname validation. There is no functional vulnerability here given the server role, but the naming is potentially misleading. CWE-297 (adjacent).
    *Recommendation:* Add a comment in `verify_peer_certificate` clarifying that `gnutls_certificate_verify_peers2` performs chain and revocation verification only (no hostname matching), and that hostname checking is intentionally not applicable in the server-verifying-client-cert scenario. This documents the design intent and prevents accidental misuse if the function is ever adapted for a client-side context.
    *Status:* deferred — no clarifying comment yet added to verify_peer_certificate in src/detail/http_request_impl_tls.cpp.

45. [x] **security-reviewer** | `/Users/etr/progs/libhttpserver/src/webserver.cpp:1807` | error-handling
    In `internal_error_page`, when `force_our` is false and no `internal_error_handler` is configured, the function returns `http_response::string(std::string{msg})` where `msg` is the exception message from the dispatch path (e.g. `e.what()` from a handler exception, or `"materialize_response returned null"`). On a public-facing deployment this can surface C++ exception details (including type names, file paths, or library internals) in HTTP 500 response bodies visible to clients. CWE-209.
    *Recommendation:* When no `internal_error_handler` is configured, default to returning an empty-body or fixed-text 500 rather than embedding `msg` in the response body. The `msg` should continue to go to `log_dispatch_error` for operator visibility, but should not reach the client. Document that `internal_error_handler` is the supported hook for customising the 500 body.
    *Status:* deferred — internal_error_page still embeds exception message in response body when no handler configured; CWE-209 not yet remediated.

46. [x] **security-reviewer** | `/Users/etr/progs/libhttpserver/src/webserver.cpp:1995` | input-validation
    The `should_skip_auth` function normalizes the path via `normalize_path` (which handles `..` and `.` traversal) before matching against `auth_skip_paths`. This is correct for absolute paths but the wildcard suffix matching at line 2001-2004 uses a simple `compare(0, prefix.size(), prefix)` without any normalization of the `skip_path` entries themselves. If an operator registers a skip path such as `"/public/../admin/*"` (which `normalize_path` would reduce to `"/admin/*"`), the comparison would fail (the registered entry is unnormalized, the incoming path is normalized) and auth would NOT be skipped — this is a safe failure in direction, but the mismatch could also silently fail to skip auth for legitimate public paths if the skip paths are stored in unnormalized form.
    *Recommendation:* Normalize each entry in `auth_skip_paths` at registration time (or in `should_skip_auth` before comparison) using the same `normalize_path` function. This closes the normalization asymmetry and makes the matching deterministic regardless of how operators specify the skip paths.
    *Status:* deferred — auth_skip_paths entries not yet normalized at registration time; normalization asymmetry in should_skip_auth (webserver_request.cpp:139) remains.

47. [x] **spec-alignment-checker** | `/Users/etr/progs/libhttpserver/specs/product_specs.md:238` | specification-gap
    §4 Traceability does not include an entry for the new scripts/check-complexity.sh, scripts/check-duplication.sh, or .github/workflows/verify-build.yml (lint lane lizard/PMD steps). If a code-quality NFR is added per finding #1, the traceability table should be updated to map API-QUALITY (or equivalent) → these files.
    *Recommendation:* After adding the §2.7 NFR block, extend the §4 Traceability table with a row pointing to scripts/check-complexity.sh, scripts/check-duplication.sh, and .github/workflows/verify-build.yml (the lint matrix entry).
    *Status:* deferred — product_specs.md §4 traceability table not yet updated.

48. [x] **spec-alignment-checker** | `/Users/etr/progs/libhttpserver/specs/product_specs.md:null` | specification-gap
    The product_specs.md §2 Non-functional & cross-cutting requirements has no code-quality or maintainability NFR covering per-function cyclomatic complexity or copy/paste duplication thresholds. The two new CI gates (lizard CCN ≤ 10, PMD CPD ≥ 100 tokens) are described in the ChangeLog and enforced in scripts/ and verify-build.yml, but they have no corresponding EARS requirement in the spec (e.g. 'The system shall enforce a per-function cyclomatic complexity ceiling of 10 across all library source files'). This is a spec gap, not a code defect — the gates work correctly. However, without an EARS requirement, the gates cannot be formally traced and future reviewers cannot determine the rationale for the thresholds from the spec alone.
    *Recommendation:* Add a new subsection to §2 (e.g. §2.7 Code-Quality Gates) with two Ubiquitous EARS requirements: one pinning the CCN ceiling to 10 and one pinning the CPD minimum-tokens to 100. Reference scripts/check-complexity.sh and scripts/check-duplication.sh as the implementing artefacts. This closes the traceability gap without changing any code.
    *Status:* deferred — §2.7 Code-Quality Gates subsection not yet added to product_specs.md.

49. [x] **spec-alignment-checker** | `/Users/etr/progs/libhttpserver/specs/product_specs.md:null` | specification-gap
    The ChangeLog entry for this task notes that the CCN ceiling was 'intentionally set above the current worst offender so CI stays green; ratcheted down per refactor commit until reaching the long-term target of 10'. The spec does not record this ratcheting strategy. At the time this commit lands the ceiling is already at 10 (scripts/check-complexity.sh CCN_MAX=10, verified against all 14 decomposed functions in the changed files), so no spec violation exists today. The gap is that the spec has no place to document that the 10-function ceiling is the final, permanent target — future maintainers cannot distinguish the target from a transitional value without reading the ChangeLog.
    *Recommendation:* The recommended §2.7 addition (finding #1) would naturally capture this as a permanent ceiling. No immediate code change required.
    *Status:* deferred — addressed together with finding #48 (§2.7 addition).

50. [x] **test-quality-reviewer** | `scripts/check-complexity.sh:1` | missing-test
    check-complexity.sh has no smoke test. The script's two exit-code branches (lizard not installed -> exit 2; violation found -> exit 1; clean -> exit 0) and its CCN_MAX env-var override are not tested by any shell bats/shunit2 test or a dedicated CI job step. A silent change to the CCN_MAX default or the lizard invocation would not be caught until a real violation slips through.
    *Recommendation:* Add a minimal Bats or shunit2 smoke test under test/ (or scripts/tests/) that: (a) invokes the script with a synthetic single-function file well under CCN 10 and asserts exit 0; (b) invokes it with LIZARD_EXTRA that forces an artificial violation and asserts exit 1. Gate the test on lizard availability.
    *Status:* deferred — no smoke tests added for check-complexity.sh.

51. [x] **test-quality-reviewer** | `scripts/check-duplication.sh:1` | missing-test
    check-duplication.sh has no smoke test. The PMD CPD exit-code remapping (4 -> 1), the CPD_MIN_TOKENS override, and the 'pmd not installed -> exit 2' path are untested. A bug in the remapping case statement would silently pass a run that found duplicates.
    *Recommendation:* Add a smoke test analogous to the suggestion for check-complexity.sh: exercise the 'clean' path and, if possible, the 'violation found' path with a synthetic duplicate snippet at the minimum-token boundary.
    *Status:* deferred — no smoke tests added for check-duplication.sh.

52. [x] **test-quality-reviewer** | `src/http_utils.cpp:394` | missing-test
    ipv4_mapped_prefix_invalid (anonymous namespace, http_utils.cpp:394) and is_v4_mapped_prefix_octet_pair (http_utils.cpp:558) are tested only indirectly through the ip_representation constructor and operator< integration tests. The direct boundary conditions (a == 0, a == 255, a == 1, a == 128) of ipv4_mapped_prefix_invalid are not explicitly targeted by any individual test case, making it harder to confirm the extracted helpers are correct in isolation after further refactoring.
    *Recommendation:* The existing ip_representation6_str_invalid_nested_starting_wrong_prefix test (http_utils_test.cpp:622) and ip_representation_ffff_comparison (line 805) provide adequate indirect coverage; direct unit tests would be good-to-have but are not blocking for v2.0 given the density of indirect coverage. Mark as low-priority backlog item.
    *Status:* deferred — low-priority per recommendation; indirect coverage already adequate for v2.0.

53. [x] **test-quality-reviewer** | `test/unit/http_utils_test.cpp:924` | naming-convention
    Test name 'ip_representation_comparison_equal' (line 924) describes the same scenario as 'ip_representation_less_than' at line 688 (which already covers same-address equality returning false). The dedicated test adds no new scenario.
    *Recommendation:* Either remove the redundant test or rename it to pin a specific edge case not covered by ip_representation_less_than (e.g. equality after mask expansion).
    *Status:* wontfix — cosmetic/style preference, no functional impact; test rename; naming polish

54. [x] **test-quality-reviewer** | `test/unit/http_utils_test.cpp:924` | redundant-test
    ip_representation_comparison_equal (line 924) is a strict subset of ip_representation_less_than (line 688), which already asserts both directions of the same-address comparison for IPv4, IPv6, and nested forms. The new test exercises no additional code path.
    *Recommendation:* Remove ip_representation_comparison_equal; its assertions are subsumed by ip_representation_less_than.
    *Status:* deferred — redundant test not yet removed.

55. [x] **test-quality-reviewer** | `test/unit/webserver_on_methods_test.cpp:438` | naming-convention
    The seven individual per-method dispatch tests (on_get_dispatches_get through on_head_dispatches_head, lines 438-513) duplicate the coverage already provided by all_seven_on_methods_serve_their_method (line 259). Each adds maintenance burden (a new webserver instance and port) without catching any regression that the combined test would miss.
    *Recommendation:* Keep all_seven_on_methods_serve_their_method and remove the seven individual dispatch tests, or keep the individuals and remove the combined test — but not both.
    *Status:* deferred — redundant per-method dispatch tests not yet removed.

56. [x] **test-quality-reviewer** | `test/unit/webserver_on_methods_test.cpp:522` | multiple-concerns
    on_get_and_on_post_compose_on_true_regex_path (line 522) combines three behaviours in one test: (a) GET dispatch, (b) POST dispatch, and (c) DELETE 405. A failure in any one of them makes the error message ambiguous.
    *Recommendation:* Split into two focused tests: one asserting both methods serve correctly, and one asserting that an unregistered method returns 405.
    *Status:* deferred — on_get_and_on_post_compose_on_true_regex_path not yet split.
