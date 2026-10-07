#
#     This file is part of libhttpserver
#     Copyright (C) 2011-2019 Sebastiano Merlino
#
#     This library is free software; you can redistribute it and/or
#     modify it under the terms of the GNU Lesser General Public
#     License as published by the Free Software Foundation; either
#     version 2.1 of the License, or (at your option) any later version.
#
#     This library is distributed in the hope that it will be useful,
#     but WITHOUT ANY WARRANTY; without even the implied warranty of
#     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#     Lesser General Public License for more details.
#
#     You should have received a copy of the GNU Lesser General Public
#     License along with this library; if not, write to the Free Software
#     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

LDADD = $(top_builddir)/src/libhttpserver.la

if HAVE_GNUTLS
LDADD += -lgnutls
endif

LDADD += -lcurl

AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver/ -DHTTPSERVER_COMPILATION
METASOURCES = AUTO
noinst_PROGRAMS = native_websocket_fixture

check_PROGRAMS = io_kqueue_backend io_epoll_backend external_http1_e2e external_readiness_adapter consumer_v3_readiness readiness_contract consumer_v3_executor websocket_drain websocket_drain_race task_resume_signal_lifetime http1_websocket_upgrade http1_websocket_handshake websocket_progress websocket_upgrade_decision websocket_codec websocket_utf8 websocket_session websocket_session_race consumer_v3_websocket native_http1_parity basic file_upload http_utils threaded nodelay string_utilities http_endpoint ip_access_control ws_start_stop authentication digest_challenge_format deferred http_resource http_response create_webserver create_webserver_explicit new_response_types daemon_info uri_log post_iterator_null_key feature_unavailable header_hygiene_iovec header_hygiene iovec_entry http_method constants response_body http_response_sbo http_response_factories http_response_digest_factory http_response_move_sanitizer webserver_pimpl http_request_pimpl create_test_request http_request_arena http_request_unescape_arena http_request_const_getters http_request_tls_accessors http_request_operator_stream webserver_register_smartptr webserver_register_path_prefix webserver_on_methods webserver_route route_table lookup_pipeline route_table_concurrency hook_bus ws_registry ws_registry_concurrency daemon_lifecycle routing_regression route_lookup_canonicalize auth_skip_normalize http_resource_allow_cache v2_dispatch_contract threadsafety_stress webserver_features webserver_ws_unavailable webserver_ws_available webserver_register_ws_smartptr webserver_dauth_unavailable webserver_dauth_available consumer_fixture header_hygiene_hooks hook_api_shape hooks_no_firing hooks_accept_ctx_shape hooks_connection_lifecycle hooks_accept_decision_denied hooks_accept_decision_throwing hooks_body_chunk_ctx_shape hooks_request_received_short_circuit hooks_body_chunk_observes_progress hooks_body_chunk_short_circuit_no_leak hooks_before_handler_ctx_shape hooks_route_resolved_miss_and_hit hooks_before_handler_short_circuit hooks_alias_count hooks_alias_functional hooks_handler_exception_chain hooks_handler_exception_user_handler_throws_continues_chain hooks_handler_exception_slot hooks_response_sent_ctx_shape hooks_request_completed_ctx_shape hooks_after_handler_replaces_response hooks_after_handler_mutates_response_in_place hooks_response_sent_carries_status_bytes_timing hooks_request_completed_fires_on_early_failure hooks_log_access_alias_slot hooks_per_route_invalid_phase_throws hooks_per_route_order hooks_per_route_early_413_per_endpoint hooks_per_route_resource_destroyed_first hooks_per_route_concurrent_registration hooks_not_found_alias auth_handler_optional_signature no_v1_compat_shim cookie_header_sentinel cookie_render cookie_deprecation_sentinel http_response_cookie_wire http_request_cookies_parsed peer_address_to_string secure_zero_dce connection_state_sentinel connection_state_body_residue debug_dump_request_body_unset debug_dump_request_body_set debug_dump_request_body_zero littletest_skip_semantics digest_client_self_test unescaper_func transcript_parser response_frame normalize v2_profile_registry transcript_runner http_semantic_outcome http_semantic_method http_semantic_fields http_semantic_request_head consumer_v3_semantic v3_header_hygiene task_executor task_core task_cancellation task_resume_signal task_race task_stop_inside_handler consumer_v3_concurrency io_operation io_connection_owner fake_io_backend io_backend_contract server_options_validate server_budgets server_routes consumer_v3_server http_status exchange_decisions exchange_routing consumer_v3_exchange body_reader consumer_v3_body_reader response_writer consumer_v3_response_writer http1_parser http1_body_mode http1_body_decoder http1_body_source http1_response_mode http1_response_framer http1_response_outbox io_poll_sys_listener worker_pool connection_engine http1_error_synth drain_scope native_server consumer_v3_native_server native_http1_e2e route_sync consumer_v3_route_sync response_definition consumer_v3_response_definition response_sources consumer_v3_response_sources v3_native_linkage base64_vector sha1_vector md5_vector sha256_vector secure_compare entropy_sys digest_nonce_ledger digest_params digest_response_vector digest_auth_policy basic_auth_policy consumer_v3_auth basic_auth_corpus digest_auth_corpus urlencoded_decoder forms_urlencoded forms_urlencoded_route forms_urlencoded_corpus consumer_v3_forms multipart_decoder forms_multipart forms_multipart_route forms_multipart_files forms_multipart_corpus consumer_v3_multipart server_routes_families server_hooks request_lifecycle consumer_v3_hooks routing_corpus hooks_corpus net_address consumer_v3_net_address peer_policy consumer_v3_peer_policy ip_controls_corpus

# TASK-099 step 1: private operation I/O contract (six owned operation
# handles, exactly-once terminal claim). The test uses the default LDADD
# deliberately: linking libhttpserver.la exercises the new private .cpp
# registration in src/Makefile.am.
io_operation_SOURCES = unit/io_operation_test.cpp

# TASK-099 step 2: per-connection completion serializer (FIFO arrival
# order, no reentrancy, coalesced posted drain, destructor safety).
io_connection_owner_SOURCES = unit/io_connection_owner_test.cpp

# TASK-099 step 3: fake backend acceptance suite (one terminal completion
# per op under reordered fake completions, cancel/close/expire/wake
# semantics, coroutine integration, cross-owner linearity).
fake_io_backend_SOURCES = unit/fake_io_backend_test.cpp

# TASK-100 step 1: shared I/O backend contract. The scenario functions in
# unit/io_backend_contract.hpp run unchanged against the fake driver
# (fake_contract_suite, the harness-validation oracle) and against the
# poll/WSAPoll driver of this task (poll_contract_suite, steps 2+).
io_backend_contract_SOURCES = unit/io_backend_contract_test.cpp

# TASK-096 Cycle 5: segmented-input transcript runner. Executes the
# committed *.tseq corpus over raw segmented loopback sockets (plus
# curl-mediated TLS / Digest round-trip cases) against the v2 fixture.
transcript_runner_SOURCES = transcript_runner.cpp parity/transcript.cpp parity/response_frame.cpp parity/normalize.cpp parity/v2_fixture.cpp
transcript_runner_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'

PARITY_TRANSCRIPTS = parity/transcripts/routing.tseq \
                     parity/transcripts/hooks.tseq \
                     parity/transcripts/auth_basic.tseq \
                     parity/transcripts/auth_digest.tseq \
                     parity/transcripts/forms.tseq \
                     parity/transcripts/file_resp.tseq \
                     parity/transcripts/ip_controls.tseq \
                     parity/transcripts/shoutcast.tseq \
                     parity/transcripts/websocket.tseq \
                     parity/transcripts/tls.tseq

MOSTLYCLEANFILES = *.gcda *.gcno *.gcov

basic_SOURCES = integ/basic.cpp
file_upload_SOURCES = integ/file_upload.cpp
threaded_SOURCES = integ/threaded.cpp
ip_access_control_SOURCES = integ/ip_access_control.cpp
ws_start_stop_SOURCES = integ/ws_start_stop.cpp
authentication_SOURCES = integ/authentication.cpp
# digest_challenge_format: TASK-062 integration test that pins the
# RFC 7616 §3.3 WWW-Authenticate challenge format on the wire when the
# Digest-specific http_response::unauthorized(digest_challenge{...})
# factory is used. Spins up a webserver with digest_auth_random +
# nonce_nc_size, hits the protected route with plain curl (no
# --digest), and asserts the challenge header contains realm, nonce,
# opaque, algorithm, qop. This complements the converted digest_auth
# test in authentication.cpp which exercises the round-trip end-to-end.
digest_challenge_format_SOURCES = integ/digest_challenge_format_test.cpp
deferred_SOURCES = integ/deferred.cpp
http_utils_SOURCES = unit/http_utils_test.cpp
string_utilities_SOURCES = unit/string_utilities_test.cpp
http_endpoint_SOURCES = unit/http_endpoint_test.cpp
nodelay_SOURCES = integ/nodelay.cpp
http_resource_SOURCES = unit/http_resource_test.cpp
http_response_SOURCES = unit/http_response_test.cpp

# TASK-064 Cycle 1 sentinel: pins the <httpserver/cookie.hpp> public
# header surface (class shape, enum values, copy/move-constructibility).
# Pure compile + type-trait test -- empty LDADD.
cookie_header_sentinel_SOURCES = unit/cookie_header_sentinel_test.cpp
cookie_header_sentinel_LDADD =

# TASK-064 Cycles 4-6: cookie::to_set_cookie_header() rendering,
# parse_cookie_header() parsing, and the RFC-6265 round-trip pinning
# tests. Pure-host CPU test -- no MHD round-trip, so empty LDADD apart
# from the library link.
cookie_render_SOURCES = unit/cookie_render_test.cpp

# TASK-064 Cycle 11: [[deprecated]] attribute pin for the legacy
# string-blob cookie surface. Calls the deprecated function so it needs
# the library link (the [[deprecated]] attribute is purely a diagnostic
# annotation, not a linkage change).
cookie_deprecation_sentinel_SOURCES = unit/cookie_deprecation_sentinel_test.cpp

# TASK-064 Cycle 8: wire-level rendering through decorate_mhd_response.
# Needs -lmicrohttpd to inspect MHD_Response headers.
http_response_cookie_wire_SOURCES = unit/http_response_cookie_wire_test.cpp
http_response_cookie_wire_LDADD = $(LDADD) -lmicrohttpd

# TASK-064 Cycle 9: http_request::get_cookies_parsed() shape + lifetime.
# Needs -lmicrohttpd because http_request transitively touches MHD types.
http_request_cookies_parsed_SOURCES = unit/http_request_cookies_parsed_test.cpp
http_request_cookies_parsed_LDADD = $(LDADD) -lmicrohttpd
create_webserver_SOURCES = unit/create_webserver_test.cpp
# create_webserver_explicit: TASK-030 compile-time sentinel. Pins:
#   (1) PRD-NAM-REQ-004: webserver(const create_webserver&) is explicit
#       (negative is_convertible_v + positive is_constructible_v);
#   (2) PRD-NAM-REQ-003: the three error-page setters carry the _handler
#       suffix with std::function<http_response(const http_request&)>
#       (positive SFINAE detectors) and the legacy _resource-suffixed
#       setters are gone (negative SFINAE detectors).
# Pure compile test -- empty LDADD.
create_webserver_explicit_SOURCES = unit/create_webserver_explicit_test.cpp
create_webserver_explicit_LDADD =
new_response_types_SOURCES = integ/new_response_types.cpp
daemon_info_SOURCES = integ/daemon_info.cpp
uri_log_SOURCES = unit/uri_log_test.cpp
# uri_log_test directly references libmicrohttpd via ~connection_context(), so
# it needs an explicit -lmicrohttpd in its link line on top of the default
# LDADD (modern ld enforces --no-copy-dt-needed-entries).
uri_log_LDADD = $(LDADD) -lmicrohttpd
# unescaper_func: TASK-073. Pins the static webserver_impl::unescaper_func
# MHD_OPTION_UNESCAPE_CALLBACK directly -- it must leave its buffer
# unmutated and return the string length (or 0 for a null string). See
# the historical note above the definition in
# src/detail/webserver_callbacks.cpp. Does not touch any libmicrohttpd
# symbol directly, so no extra -lmicrohttpd link override is needed.
unescaper_func_SOURCES = unit/unescaper_func_test.cpp
# post_iterator_null_key: issue #375 regression. Drives the static
# webserver_impl::post_iterator callback with a null key (the continuation-
# chunk case MHD hits when a field is split across callbacks) and asserts it
# no longer throws std::logic_error / terminates. Constructs a
# connection_context whose ~connection_context() touches libmicrohttpd, so it needs
# the explicit -lmicrohttpd link the same way uri_log does.
post_iterator_null_key_SOURCES = unit/post_iterator_null_key_test.cpp
post_iterator_null_key_LDADD = $(LDADD) -lmicrohttpd
feature_unavailable_SOURCES = unit/feature_unavailable_test.cpp
header_hygiene_iovec_SOURCES = unit/header_hygiene_iovec_test.cpp
# header_hygiene: TASK-007 sentinel TU. Mimics a true consumer:
# - per-target CPPFLAGS overrides AM_CPPFLAGS so HTTPSERVER_COMPILATION
#   and the build-tree -I src/httpserver/ entries are NOT in scope (a
#   real consumer wouldn't have either). Only -I$(top_srcdir)/src is
#   passed so <httpserver.hpp> resolves.
# - LDADD is overridden to empty: this is a pure-compile assertion, the
#   `int main(){}` body has no library dependencies.
# Now an unconditional PASS: TASK-020 landed and the <httpserver.hpp>
# umbrella is free of backend-header leakage. See the historical
# XFAIL_TESTS note near the TESTS = $(check_PROGRAMS) assignment below.
header_hygiene_SOURCES = unit/header_hygiene_test.cpp
header_hygiene_CPPFLAGS = -I$(top_srcdir)/src $(CPPFLAGS)
header_hygiene_LDADD =
iovec_entry_SOURCES = unit/iovec_entry_test.cpp
http_method_SOURCES = unit/http_method_test.cpp
constants_SOURCES = unit/constants_test.cpp
# response_body: TASK-008 unit test for the internal detail::response_body
# hierarchy. It constructs/destroys MHD_Response objects directly via the
# libmicrohttpd inspection APIs (no daemon), so it needs an explicit
# -lmicrohttpd link the same way uri_log does.
response_body_SOURCES = unit/response_body_test.cpp
response_body_LDADD = $(LDADD) -lmicrohttpd

# http_response_sbo: TASK-009 unit test for the SBO value-type layout of
# http_response. Pokes the private body_/body_storage_/body_inline_/kind_
# members through a friend declaration, exercises the four-case move
# cross-product, and asserts the type-trait acceptance criteria. Needs
# -lmicrohttpd because it transitively touches detail::response_body subclasses
# whose materialize() returns MHD_Response*.
http_response_sbo_SOURCES = unit/http_response_sbo_test.cpp
http_response_sbo_LDADD = $(LDADD) -lmicrohttpd

# http_response_factories: TASK-010 unit test for the static factory
# functions on http_response (string/file/iovec/pipe/empty/deferred/
# unauthorized). Each factory placement-news the corresponding
# detail::response_body subclass into the SBO buffer, so this TU includes the
# private detail/response_body.hpp via the build-tree -DHTTPSERVER_COMPILATION
# path. Needs -lmicrohttpd for the same transitive reasons as
# http_response_sbo.
http_response_factories_SOURCES = unit/http_response_factories_test.cpp
http_response_factories_LDADD = $(LDADD) -lmicrohttpd

# http_response_digest_factory: TASK-062 unit test for the RFC-7616
# Digest-specific overload of http_response::unauthorized
# (digest_challenge). Pins status/kind/algorithm/opaque/domain
# round-trip through the factory, the SBO budget for the new
# detail::digest_challenge_response_body subclass, and the CR/LF/NUL injection
# guard on realm/opaque/domain. Needs -lmicrohttpd for the same
# transitive reasons as http_response_factories.
http_response_digest_factory_SOURCES = unit/http_response_digest_factory_test.cpp
http_response_digest_factory_LDADD = $(LDADD) -lmicrohttpd

# http_response_move_sanitizer: TASK-038 unit test that pins
# sanitizer-clean (ASan + UBSan) http_response move semantics. Covers the
# four move-assign cases and both move-ctor cases via factory-built
# responses, drives the heap-fallback branch with a synthetic >64 B body
# subclass (fat_body) reached through the same friend hook the SBO test
# uses, and pins the moved-from invariant contract (destructible,
# accessor-safe, re-assignable). Picked up by the asan / ubsan matrix
# entries in .github/workflows/verify-build.yml via $(check_PROGRAMS).
# Needs -lmicrohttpd because the TU subclasses detail::response_body whose
# materialize() returns MHD_Response*.
http_response_move_sanitizer_SOURCES = unit/http_response_move_sanitizer_test.cpp
http_response_move_sanitizer_LDADD = $(LDADD) -lmicrohttpd

# webserver_pimpl: TASK-014 sentinel. Compile-time assertions that the
# PIMPL split has happened: <microhttpd.h>/<pthread.h> no longer leak
# through the public webserver.hpp, and webserver is non-copyable and
# non-movable. Pure compile test -- empty LDADD.
webserver_pimpl_SOURCES = unit/webserver_pimpl_test.cpp
webserver_pimpl_LDADD =

# http_request_pimpl: TASK-015 sentinel. Compile-time assertions that the
# http_request PIMPL split has happened: <microhttpd.h>/<gnutls/gnutls.h>
# no longer leak through the public http_request.hpp, http_request is
# move-only, and sizeof(http_request) shrank to a small bag plus one
# impl pointer. Pure compile test -- empty LDADD.
http_request_pimpl_SOURCES = unit/http_request_pimpl_test.cpp
http_request_pimpl_LDADD =

# create_test_request: TASK-015 functional tests for the create_test_request
# builder and the http_request public API on test-constructed requests. Wires
# up headers/footers/cookies/args/querystring/requestor/auth via impl_ local
# maps rather than a live MHD_Connection*.
create_test_request_SOURCES = unit/create_test_request_test.cpp
create_test_request_LDADD = $(LDADD) -lmicrohttpd

# http_request_operator_stream: TASK-057 unit test for the redaction
# policy in http_request::operator<<. Default-built test requests must
# emit pass:"<redacted>", redacted Authorization / Proxy-Authorization
# values, and redacted cookie values; the opt-in
# create_test_request::expose_credentials_in_logs() restores the v1
# verbose form. Needs -lmicrohttpd because it constructs an http_request
# via create_test_request::build().
http_request_operator_stream_SOURCES = unit/http_request_operator_stream_test.cpp
http_request_operator_stream_LDADD = $(LDADD) -lmicrohttpd

# http_request_arena: TASK-016 unit test for the per-connection arena. Asserts
# that connection_state owns a std::pmr::monotonic_buffer_resource arena_,
# that arena_.release() rewinds the bump pointer, and that an
# http_request_impl constructed from the arena does not touch the upstream
# memory resource on the warm path. Exercises detail/http_request_impl.hpp
# directly via the build-tree HTTPSERVER_COMPILATION include path. Needs
# -lmicrohttpd because it transitively touches MHD types through the impl.
http_request_arena_SOURCES = unit/http_request_arena_test.cpp
http_request_arena_LDADD = $(LDADD) -lmicrohttpd

# http_request_unescape_arena: TASK-072 unit test. Pins the warm-path
# zero-upstream-alloc invariant for build_request_args with a "%2F"-
# containing GET argument, both for the default unescaper and for a
# user-registered unescaper. Also covers correctness (percent-encoded
# decode, invalid-hex passthrough, empty input, lifetime contract,
# user-callback grow). Needs -lmicrohttpd because it touches MHD types
# through the impl.
http_request_unescape_arena_SOURCES = unit/http_request_unescape_arena_test.cpp
http_request_unescape_arena_LDADD = $(LDADD) -lmicrohttpd

# http_request_const_getters: TASK-018 sentinel. Compile-time assertions
# that the per-key getters (get_header / get_cookie / get_footer /
# get_arg / get_arg_flat) and the always-present getters (get_path /
# get_method / get_version / get_content / get_querystring) are callable
# on `const http_request&` and (for the always-present five) noexcept.
# Locks the public-API contract via static_assert so accidental drift is
# caught at compile time. Pure compile test -- empty LDADD.
http_request_const_getters_SOURCES = unit/http_request_const_getters_test.cpp
http_request_const_getters_LDADD =

# http_request_tls_accessors: TASK-019 sentinel. Compile-time assertions
# that the high-level GnuTLS accessors on http_request exist
# unconditionally (no #ifdef HAVE_GNUTLS gate around the public
# declarations), that the raw `gnutls_session_t get_tls_session()` member
# has been removed from the public surface, that return types match the
# TASK-019 spec (string_view / int64_t / bool), and that the five
# documented-noexcept accessors carry the noexcept specifier. Pure
# compile test -- empty LDADD.
http_request_tls_accessors_SOURCES = unit/http_request_tls_accessors_test.cpp
http_request_tls_accessors_LDADD =

# webserver_register_smartptr: TASK-023. Runtime tests for the
# register_path smart-pointer ownership semantics (unique_ptr ownership
# transfer, shared_ptr caller-retains-ref, throw-on-null,
# throw-on-duplicate). The compile-time signature contract lives in
# webserver_register_path_prefix. Default LDADD (libhttpserver + curl) is
# sufficient; no microhttpd direct link needed.
webserver_register_smartptr_SOURCES = unit/webserver_register_smartptr_test.cpp

# webserver_register_path_prefix: TASK-024. Compile-time signature
# assertions that register_path / register_prefix exist with both
# unique_ptr and shared_ptr overloads, that unregister_path /
# unregister_prefix exist, and that register_resource has been removed
# entirely (negative SFINAE pin — clean break; no smart-pointer or
# bool-family overloads survive). Runtime tests: register_prefix matches a
# longer URL; register_path does not; parameterized exact paths bind their
# args; unregister_path / unregister_prefix / the umbrella
# unregister_resource alias all 404 the route after removal.
webserver_register_path_prefix_SOURCES = unit/webserver_register_path_prefix_test.cpp

# webserver_on_methods: TASK-025. Compile-time signature contract for
# the seven on_* lambda registration overloads (on_get / on_post / on_put
# / on_delete / on_patch / on_options / on_head) plus static_asserts on
# the §4.7 detail::route_entry shape (method_set + shared_ptr<http_resource>
# + bool is_prefix; TASK-071 collapsed the original variant arm). Runtime tests over real
# curl round-trips: PRD §3.4 hello-world; per-method 405 + Allow header;
# composition (on_get + on_post on the same path serve both); all seven
# methods dispatch correctly; duplicate (method, path) registration
# throws std::invalid_argument; merge-then-conflict still throws;
# parameterized path binds {id}; class-resource registration on a
# lambda-owned path throws. Default LDADD (libhttpserver + curl) is
# sufficient.
webserver_on_methods_SOURCES = unit/webserver_on_methods_test.cpp

# webserver_route: TASK-026. Compile-time signature contract for the two
# generic route() overloads (route(http_method, ...) and
# route(method_set, ...)) plus runtime curl tests covering: GET/POST
# dispatch, 405 + Allow on unregistered methods, the headline acceptance
# test (load (method, path) pairs from a vector at runtime and dispatch),
# method_set GET+HEAD serves both, method_set partial overlap with an
# existing handler throws atomically (POST stays unregistered), empty
# method_set throws, http_method::count_ sentinel throws, duplicate /
# cross-overload conflicts with on_* throw, and parameterized paths bind
# through the regex tier. Default LDADD (libhttpserver + curl) is
# sufficient.
webserver_route_SOURCES = unit/webserver_route_test.cpp

# route_table: TASK-027. Unit tests for the bespoke segment-trie
# (detail::segment_trie<route_entry>) and the LRU route cache
# (detail::route_cache), PLUS class-level tests that construct
# detail::route_table directly (register_v2_route / lookup_v2 /
# lock_for_write + the locked probe/upsert primitives) added when the
# route_table collaborator was extracted from webserver_impl (commit
# 21296ac) -- pinning the table-before-cache lock discipline and the
# duplicate/collision-rejection logic at the class boundary rather than
# only through webserver_impl's forwarders. Default LDADD
# (libhttpserver + curl) is sufficient.
route_table_SOURCES = unit/route_table_test.cpp

# lookup_pipeline: TASK-027 Cycle F. Drives the public webserver
# registration surface (register_path / register_prefix) and probes the
# impl-private lookup_v2() to pin the tier-order pipeline:
# cache -> exact -> radix -> regex. Uses the webserver_test_access
# friend gated on HTTPSERVER_COMPILATION (already supplied by AM_CPPFLAGS
# in this Makefile). Needs -lmicrohttpd because webserver's PIMPL impl
# carries microhttpd-typed members.
lookup_pipeline_SOURCES = unit/lookup_pipeline_test.cpp
lookup_pipeline_LDADD = $(LDADD) -lmicrohttpd

# route_table_concurrency: TASK-027 Cycle I. Multi-thread stress test.
# 4 writers + 16 readers for ~500ms, registering / unregistering /
# looking up against the v2 3-tier table. Gate for the lock-order
# discipline (table BEFORE cache). TASK-092: the tsan CI lane runs this
# binary RTC_ITERATIONS times via `make -C test
# check-route-table-concurrency` (time-boxed to <= 2 min); it is also
# run once implicitly by the tsan lane's plain `make check`.
# Needs -lmicrohttpd because webserver's PIMPL impl carries
# microhttpd-typed members.
route_table_concurrency_SOURCES = integ/route_table_concurrency.cpp
route_table_concurrency_LDADD = $(LDADD) -lmicrohttpd

# hook_bus: extracted-collaborator unit test (commit 1f9a82e). Constructs
# detail::hook_bus directly (no running webserver) and drives
# add()/remove()/has_hooks_for()/phase_hook_count(), the phase-mismatch
# and empty-callable registration throws, the any_hooks_ advisory-gate
# arm/clear invariant, the two alias slots (handler_exception_alias_ DOES
# arm the gate; log_access_alias_ does NOT), and the fire_* helpers'
# try/catch-into-log_fn containment. Previously only reachable indirectly
# through webserver's add_hook/hook_handle API. Default LDADD
# (libhttpserver + curl) is sufficient -- hook_bus never touches MHD.
hook_bus_SOURCES = unit/hook_bus_test.cpp

# ws_registry: extracted-collaborator unit test (commit b596fc8).
# Constructs detail::ws_registry directly and drives
# try_register/unregister/find/empty, the duplicate-key rejection (false
# return, original registration untouched), the no-op unregister of an
# unknown key, and the find()-returns-a-copy lifetime contract. Default
# LDADD (libhttpserver + curl) is sufficient -- ws_registry never
# dereferences websocket_handler, so it needs no MHD/websocket backend.
ws_registry_SOURCES = unit/ws_registry_test.cpp

# ws_registry_concurrency: concurrency stress companion to ws_registry,
# mirroring route_table_concurrency.cpp's pattern (4 writers + 16
# readers hammering try_register/unregister/find/empty). ws_registry's
# class comment documents its mutex as independent of every other
# cluster's; unlike route_table and hook_bus, it previously had zero
# concurrency verification anywhere in the suite (threadsafety_stress.cpp
# has no register_ws_resource/unregister_ws_resource op). Needs no MHD
# daemon -- ws_registry is self-contained, so default LDADD suffices.
ws_registry_concurrency_SOURCES = integ/ws_registry_concurrency.cpp

# daemon_lifecycle: extracted-collaborator unit test (commit e6af49f).
# Reaches the daemon_lifecycle every httpserver::webserver already owns
# (impl_->daemon_) via webserver_test_access and drives
# compose_transport_flags/compose_runtime_flags/compose_start_flags and
# build_mhd_option_array's per-flag composition directly -- no MHD daemon
# is ever started. Previously only reachable indirectly through a
# running daemon via the integ-level start/stop tests. Needs
# -lmicrohttpd because webserver's PIMPL impl carries microhttpd-typed
# members (same rationale as lookup_pipeline / route_table_concurrency).
daemon_lifecycle_SOURCES = unit/daemon_lifecycle_test.cpp
daemon_lifecycle_LDADD = $(LDADD) -lmicrohttpd

# threadsafety_stress: TASK-032. DR-008 multi-thread contract gate.
# Spawns 16 curl clients hitting an on_get handler that re-enters the
# public webserver surface (register_path / unregister_path / deny_ip /
# remove_denied_ip) for HTTPSERVER_STRESS_SECONDS (default 60). The TSan-clean
# rerun is the headline acceptance; the CI matrix `build-type: tsan`
# entry in verify-build.yml picks this binary up via `make check`
# without further wiring. A second sub-test reproduces the documented
# stop()-from-handler deadlock; TASK-092: the baseline Linux gcc CI lane
# now sets that gate via `make -C test check-stop-from-handler`
# (HTTPSERVER_RUN_STOP_FROM_HANDLER=1). Local runs remain opt-in — the
# sub-test skips unless the env var is set.
# Default LDADD (libhttpserver + curl) is sufficient — this test
# touches only public webserver APIs and does not need a direct
# microhttpd link.
threadsafety_stress_SOURCES = integ/threadsafety_stress.cpp

# routing_regression: TASK-028. v1 routing-test corpus regression gate.
# Drives the public webserver registration surface (register_path /
# register_prefix / on_get / on_post / unregister_path / unregister_prefix)
# and probes webserver_impl::lookup_v2 to assert that every routing
# pattern in the v1 corpus resolves to the same handler / tier / captures
# the v2 3-tier table is expected to produce after TASK-036 cuts dispatch
# over. Documented as the v2.0 routing regression gate in
# test/REGRESSION.md. Needs -lmicrohttpd because webserver's PIMPL impl
# carries microhttpd-typed members.
routing_regression_SOURCES = unit/routing_regression_test.cpp
routing_regression_LDADD = $(LDADD) -lmicrohttpd

# route_lookup_canonicalize (TASK-058 step 1): pins lookup_v2's
# canonicalisation contract behind a behaviour-only test (empty, leading-
# slash insertion, trailing-slash stripping, "/" identity, idempotency).
# Same -lmicrohttpd dependency as routing_regression: the webserver
# constructor pulls libhttpserver in, which transitively touches
# libmicrohttpd headers.
route_lookup_canonicalize_SOURCES = unit/route_lookup_canonicalize_test.cpp
route_lookup_canonicalize_LDADD = $(LDADD) -lmicrohttpd

# auth_skip_normalize (TASK-058 step 2): pins that auth_skip_paths
# entries are normalized at webserver construction time, so non-
# canonical inputs match canonical request paths.  Same -lmicrohttpd
# dependency as routing_regression: webserver pulls libhttpserver in,
# which transitively touches libmicrohttpd headers.
auth_skip_normalize_SOURCES = unit/auth_skip_normalize_test.cpp
auth_skip_normalize_LDADD = $(LDADD) -lmicrohttpd

# http_resource_allow_cache (TASK-058 step 3): pins the lazy Allow-
# header cache on http_resource -- correctness (cached value matches
# format_allow_header; mask mutation invalidates), identity (two
# consecutive calls return the same buffer pointer, no reallocation),
# and the invalidation surface (set_allowing / disallow_all / allow_all).
http_resource_allow_cache_SOURCES = unit/http_resource_allow_cache_test.cpp
http_resource_allow_cache_LDADD = $(LDADD) -lmicrohttpd

# v2_dispatch_contract: TASK-053. End-to-end safety net pinning the
# observable invariants the dispatch path must satisfy AFTER finalize_answer
# is cut over from resolve_resource_for_request (v1 maps) to
# resolve_resource_for_request_v2 (lookup_v2-backed). Drives real HTTP
# requests against the public webserver surface and asserts on response
# body / status / hook context (parameterized capture replay, prefix vs
# exact is_prefix flag, 405 method-mismatch still resolves the resource).
# Default LDADD plus -lcurl is sufficient (already in LDADD); no PIMPL
# friend access needed because the gate is the public observable contract.
v2_dispatch_contract_SOURCES = unit/v2_dispatch_contract_test.cpp

# webserver_features: TASK-034. Pins the public contract of
# webserver::features() (PRD-FLG-REQ-003): nested `struct features` with
# four bool fields in documented order; the static call is noexcept; the
# fields match the HAVE_* state the library was built with. The test TU
# is compiled with the same AM_CXXFLAGS as the library so its #ifdef
# HAVE_* probes are in lockstep with libhttpserver's. Default LDADD is
# sufficient.
webserver_features_SOURCES = unit/webserver_features_test.cpp

# webserver_ws_unavailable: TASK-034 / TASK-035. On HAVE_WEBSOCKET-off
# builds, pins that all three public WebSocket entry points
# (register_ws_resource(unique_ptr), register_ws_resource(shared_ptr),
# unregister_ws_resource) throw feature_unavailable whose what() names
# both "websocket" and "HAVE_WEBSOCKET". On HAVE_WEBSOCKET-on builds the
# suite is empty (the integ-level ws_start_stop test owns the success
# path; the smartptr ownership semantics live in
# webserver_register_ws_smartptr).
webserver_ws_unavailable_SOURCES = unit/webserver_ws_unavailable_test.cpp

# webserver_ws_available: TASK-081 cycle 1. Paired on-path companion to
# webserver_ws_unavailable. On HAVE_WEBSOCKET-on builds pins
# webserver::features().websocket == true and that null unique_ptr /
# shared_ptr arguments throw std::invalid_argument (NOT
# feature_unavailable) — the throw-type contrast that makes the
# unavailable TU's contract meaningful. On HAVE_WEBSOCKET-off builds
# the suite is empty (the off-path contracts live in
# webserver_ws_unavailable). Default LDADD is sufficient.
webserver_ws_available_SOURCES = unit/webserver_ws_available_test.cpp

# webserver_register_ws_smartptr: TASK-035. Compile-time signature
# assertions that the new unique_ptr/shared_ptr register_ws_resource
# overloads exist, the raw-pointer overload is gone, and
# unregister_ws_resource exists. On HAVE_WEBSOCKET-on builds the
# runtime suite mirrors webserver_register_smartptr (unique_ptr
# ownership transfer, shared_ptr caller-retains-ref, throw-on-null,
# throw-on-duplicate, unregister drops the slot). On HAVE_WEBSOCKET-off
# builds only the compile-time contract is exercised; the
# feature_unavailable throw-site contract lives in
# webserver_ws_unavailable. Default LDADD (libhttpserver + curl) is
# sufficient.
webserver_register_ws_smartptr_SOURCES = unit/webserver_register_ws_smartptr_test.cpp

# webserver_dauth_unavailable: security-reviewer-iter1-1 / TASK-034.
# On HAVE_DAUTH-off builds, pins that webserver(create_webserver(port).digest_auth(true))
# throws feature_unavailable naming "digest_auth" and "HAVE_DAUTH", and that a
# default-constructed webserver does NOT throw (digest_auth_default() returns false
# on HAVE_DAUTH-off). On HAVE_DAUTH-on builds the suite is empty.
webserver_dauth_unavailable_SOURCES = unit/webserver_dauth_unavailable_test.cpp

# webserver_dauth_available: TASK-081 cycle 2. Paired on-path companion
# to webserver_dauth_unavailable. On HAVE_DAUTH-on builds pins
# webserver::features().digest_auth == true and that constructing a
# webserver with digest_auth(true) does NOT throw feature_unavailable.
# On HAVE_DAUTH-off builds the suite is empty (the off-path contracts
# live in webserver_dauth_unavailable). Default LDADD is sufficient.
webserver_dauth_available_SOURCES = unit/webserver_dauth_available_test.cpp

# consumer_fixture: TASK-034 / AC #2 + TASK-037 CI matrix gate
# (PRD-FLG-REQ-001 / arch §9 testing item 2). Compile-and-link gate
# proving that a single consumer source file referencing every
# previously HAVE_*-gated public symbol compiles unchanged in every
# build configuration (HAVE_BAUTH/DAUTH/GNUTLS/WEBSOCKET on or off).
# main() returns 0; the test is "pass" purely by virtue of the binary
# existing. TASK-037 wires two GitHub Actions matrix entries
# (flag-invariance-on / flag-invariance-off in
# .github/workflows/verify-build.yml) that exercise this fixture in
# both the all-features-on and all-features-off lanes; an intentional
# regression that re-introduces #ifdef HAVE_* around a public method
# breaks the off lane (verified locally during TASK-037 cycle R).
consumer_fixture_SOURCES = consumer_fixture.cpp

# TASK-045 -- Hook bus skeleton (hook_phase, hook_action, hook_handle,
# hook_context, webserver::add_hook). Three tests:
#
# - header_hygiene_hooks: per-header preprocessor sentinel that the
#   four new hook public headers do not transitively pull <microhttpd.h>,
#   <gnutls/gnutls.h>, <sys/socket.h>, or <sys/uio.h>. Mirrors
#   header_hygiene_iovec_test.cpp.
# - hook_api_shape: compile-time + runtime contract for hook_phase /
#   hook_action / hook_handle / webserver::add_hook. Reaches into
#   webserver_impl via the HTTPSERVER_COMPILATION-gated friend bridge,
#   so it needs -lmicrohttpd transitively (default LDADD covers it).
# - hooks_no_firing: integration sentinel registering one hook on every
#   phase and asserting zero invocations across a real HTTP round-trip.
header_hygiene_hooks_SOURCES = unit/header_hygiene_hooks_test.cpp
hook_api_shape_SOURCES = unit/hook_api_shape_test.cpp
hooks_no_firing_SOURCES = integ/hooks_no_firing.cpp

# TASK-046 -- Lifecycle hook bus firing sites (connection_opened,
# accept_decision, connection_closed). Adds:
#
# - hooks_accept_ctx_shape: compile-time pin for accept_ctx's extended
#   {peer, accepted, reason} shape (TASK-045 landed peer only).
# - hooks_connection_lifecycle: integ test driving one curl round-trip
#   and asserting all three lifecycle hooks fired with a valid peer.
# - hooks_accept_decision_denied: denies 127.0.0.1, asserts the
#   accept_decision hook observes accepted=false reason="denied".
# - hooks_accept_decision_throwing: a throwing accept_decision hook
#   must not flip the accept/reject decision.
hooks_accept_ctx_shape_SOURCES = unit/hooks_accept_ctx_shape_test.cpp
hooks_connection_lifecycle_SOURCES = integ/hooks_connection_lifecycle.cpp
hooks_accept_decision_denied_SOURCES = integ/hooks_accept_decision_denied.cpp
hooks_accept_decision_throwing_SOURCES = integ/hooks_accept_decision_throwing.cpp

# TASK-047 -- Pre-handler short-circuit hooks (request_received, body_chunk).
# Adds:
# - hooks_body_chunk_ctx_shape: compile-time pin for the request_received_ctx /
#   body_chunk_ctx shapes (mutable request*, chunk span, offset, is_final).
# - hooks_request_received_short_circuit: hook returns 413 on Content-Length > 1 MB,
#   downstream body_chunk hook must NOT fire (closes #273).
# - hooks_body_chunk_observes_progress: hook accumulates bytes; total equals body size.
# - hooks_body_chunk_short_circuit_no_leak: ASan-verified that short-circuit drops
#   the in-flight MHD_PostProcessor without leaking its buffer.
hooks_body_chunk_ctx_shape_SOURCES = unit/hooks_body_chunk_ctx_shape_test.cpp
hooks_request_received_short_circuit_SOURCES = integ/hooks_request_received_short_circuit.cpp
hooks_body_chunk_observes_progress_SOURCES = integ/hooks_body_chunk_observes_progress.cpp
hooks_body_chunk_short_circuit_no_leak_SOURCES = integ/hooks_body_chunk_short_circuit_no_leak.cpp

# TASK-048 -- route_resolved + before_handler firing sites and the three
# v1-derived single-slot setters (auth_handler, not_found_handler,
# method_not_allowed_handler) re-described as hook-bus aliases. Adds:
# - hooks_before_handler_ctx_shape: compile-time pin for
#   before_handler_ctx (method + resource fields) and route_resolved_ctx.
# - hooks_route_resolved_miss_and_hit: two route_resolved hooks
#   observe both hit (matched.has_value()) and miss (matched==nullopt).
# - hooks_before_handler_short_circuit: before_handler hook returning
#   respond_with(403) skips both is_allowed check and resource handler.
# - hooks_alias_count: registering auth_handler(fn) installs +1 hook on
#   before_handler; not_found_handler installs +1 on route_resolved.
hooks_before_handler_ctx_shape_SOURCES = unit/hooks_before_handler_ctx_shape_test.cpp
hooks_route_resolved_miss_and_hit_SOURCES = integ/hooks_route_resolved_miss_and_hit.cpp
hooks_before_handler_short_circuit_SOURCES = integ/hooks_before_handler_short_circuit.cpp
hooks_alias_count_SOURCES = unit/hooks_alias_count_test.cpp
# TASK-054 -- auth_handler_ptr is
# std::function<std::optional<http_response>(...)>. The pinning TU
# auth_handler_optional_signature static_asserts the typedef and
# runtime-end-to-end-checks 200 on nullopt / 401 on engaged. The v1
# compat escape hatch (compat::auth_handler_v1_ptr + a [[deprecated]]
# setter overload) was removed in TASK-067; see no_v1_compat_shim below
# for the post-removal sentinel.
auth_handler_optional_signature_SOURCES = unit/auth_handler_optional_signature_test.cpp
# TASK-067 -- sentinel that the v1 compat auth-handler shim has been
# removed. Static_assert plus a positive control for the canonical shape.
no_v1_compat_shim_SOURCES = unit/no_v1_compat_shim_test.cpp
# hooks_alias_functional: TASK-048 finding fix. Pins that the
# method_not_allowed_handler alias is a functional before_handler hook
# that short-circuits the chain (not a no-op stub): a user hook registered
# after the alias does NOT fire for method-not-allowed requests.
hooks_alias_functional_SOURCES = integ/hooks_alias_functional_test.cpp
# TASK-071 sub-item A. End-to-end pin for the not_found_handler ->
# route_resolved alias seat: default-branch "Not Found" body, custom-branch
# user-handler body, and DR-012 §4.10 ordering (a user route_resolved hook
# co-fires alongside the alias seat — observation phase is not short-
# circuit-capable). Cross-pins the wire contract so a future structural
# refactor of install_not_found_alias_ cannot silently regress it.
hooks_not_found_alias_SOURCES = integ/hooks_not_found_alias_test.cpp

# TASK-049 -- handler_exception firing site and internal_error_handler
# alias slot. Adds:
# - hooks_handler_exception_chain: A=pass, B=respond_with(418); alias C is
#   never invoked because B wins.
# - hooks_handler_exception_user_handler_throws_continues_chain: A throws;
#   the chain continues to B; B's 418 is queued.
# - hooks_handler_exception_fallback_to_hardcoded_500: A=pass, B=throws,
#   alias C=throws -> dispatcher emits empty-body 500; alias C is called
#   exactly once (no re-entry).
# - hooks_handler_exception_slot: unit pin that internal_error_handler
#   wires the dedicated last-position slot (not a vector entry), distinct
#   from the TASK-048 aliases.
hooks_handler_exception_chain_SOURCES = integ/hooks_handler_exception_chain.cpp
hooks_handler_exception_user_handler_throws_continues_chain_SOURCES = integ/hooks_handler_exception_user_handler_throws_continues_chain.cpp
hooks_handler_exception_fallback_to_hardcoded_500_SOURCES = integ/hooks_handler_exception_fallback_to_hardcoded_500.cpp
hooks_handler_exception_slot_SOURCES = unit/hooks_handler_exception_slot_test.cpp

# TASK-050 -- after_handler / response_sent / request_completed firing
# sites + log_access alias. Adds:
# - hooks_response_sent_ctx_shape: compile-time pin for the extended
#   response_sent_ctx fields (status, bytes_queued, elapsed).
# - hooks_request_completed_ctx_shape: compile-time pin for the
#   extended request_completed_ctx (resp pointer + succeeded flag).
# - hooks_after_handler_replaces_response: after_handler returning
#   respond_with(r2) replaces conn->response before MHD_queue_response.
# - hooks_after_handler_mutates_response_in_place: after_handler
#   returning pass() after ctx.response->with_header(...) puts the
#   header on the wire.
# - hooks_response_sent_carries_status_bytes_timing: response_sent
#   hook observes status==200, bytes_queued==body.size(), elapsed>0.
# - hooks_request_completed_fires_on_early_failure: a request_received
#   hook short-circuits to a 413; request_completed still fires with
#   succeeded==true and a non-null resp pointing at the 413.
# - hooks_log_access_alias_slot: unit pin that log_access(fn) wires the
#   dedicated alias slot on webserver_impl (same pattern as the TASK-049
#   internal_error_handler alias slot), and that no entry is pushed
#   into hooks_response_sent_.
hooks_response_sent_ctx_shape_SOURCES = unit/hooks_response_sent_ctx_shape_test.cpp
hooks_request_completed_ctx_shape_SOURCES = unit/hooks_request_completed_ctx_shape_test.cpp
hooks_after_handler_replaces_response_SOURCES = integ/hooks_after_handler_replaces_response.cpp
hooks_after_handler_mutates_response_in_place_SOURCES = integ/hooks_after_handler_mutates_response_in_place.cpp
hooks_response_sent_carries_status_bytes_timing_SOURCES = integ/hooks_response_sent_carries_status_bytes_timing.cpp
hooks_request_completed_fires_on_early_failure_SOURCES = integ/hooks_request_completed_fires_on_early_failure.cpp
hooks_log_access_alias_slot_SOURCES = unit/hooks_log_access_alias_slot_test.cpp
hooks_log_access_alias_slot_LDADD = $(LDADD) -lmicrohttpd

# TASK-051 -- Per-route hooks on http_resource (add_hook overloads). Adds:
# - hooks_per_route_invalid_phase_throws: validation gate -- the five
#   pre-route-resolution phases must throw std::invalid_argument, the
#   five post-route-resolution phases must not throw.
# - hooks_per_route_order: server-wide response_sent hook A fires
#   before per-route hook B on the matched resource; on a different
#   resource only A fires.
# - hooks_per_route_early_413_per_endpoint: two endpoints with
#   different per-route before_handler size caps demonstrate per-route
#   policy isolation.
# - hooks_per_route_resource_destroyed_first: ASan-relevant. A
#   hook_handle whose resource was destroyed must remove() as a no-op.
# - hooks_per_route_concurrent_registration: TSan stress -- registers
#   on resource R from inside a handler on resource Q, exercising the
#   documented route_table_mutex_ -> resource hook_table_mutex_ lock
#   order.
hooks_per_route_invalid_phase_throws_SOURCES = integ/hooks_per_route_invalid_phase_throws.cpp
hooks_per_route_order_SOURCES = integ/hooks_per_route_order.cpp
hooks_per_route_early_413_per_endpoint_SOURCES = integ/hooks_per_route_early_413_per_endpoint.cpp
hooks_per_route_resource_destroyed_first_SOURCES = integ/hooks_per_route_resource_destroyed_first.cpp
hooks_per_route_concurrent_registration_SOURCES = integ/hooks_per_route_concurrent_registration.cpp

# TASK-065 -- RFC 5952 canonicalization of peer_address::to_string() for
# IPv6 (lowercase + leading-zero suppression + longest-run zero collapse
# + ::ffff:a.b.c.d dotted-quad). Pure-host CPU test -- no MHD round-trip.
peer_address_to_string_SOURCES = unit/peer_address_to_string_test.cpp

# TASK-068 -- CWE-226 arena-overflow zeroing + CWE-14 explicit_bzero.
# Three tests:
# - secure_zero_dce: pins that httpserver::detail::secure_zero() writes
#   actually land at -O2 -DNDEBUG (regression sentinel against future
#   compiler dead-store elimination).
# - connection_state_sentinel: unit pin that connection_state::reset_arena()
#   zeros the entire initial_buffer_ (8 KiB), not just the bump-pointer
#   prefix.
# - connection_state_body_residue: integration acceptance criterion --
#   a DEADBEEF body in request N is not observable to request N+1 on the
#   same MHD keep-alive connection. Reaches into connection_state via the
#   HTTPSERVER_COMPILATION-gated underlying_connection_for_testing()
#   accessor on http_request. Needs -lmicrohttpd for the
#   MHD_get_connection_info call.
secure_zero_dce_SOURCES = unit/secure_zero_dce_test.cpp
# Pin -O2 -DNDEBUG so the dead-store elimination assertion runs at the
# optimization level where it actually matters.
secure_zero_dce_CXXFLAGS = $(AM_CXXFLAGS) -O2 -DNDEBUG
secure_zero_dce_LDADD =
connection_state_sentinel_SOURCES = unit/connection_state_sentinel_test.cpp
connection_state_sentinel_LDADD =
connection_state_body_residue_SOURCES = integ/connection_state_body_residue_test.cpp
connection_state_body_residue_LDADD = $(LDADD) -lmicrohttpd

# TASK-074: integration tests gating the LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY
# opt-in env var. Each binary calls setenv()/unsetenv() at the top of main()
# BEFORE any libhttpserver code runs so the function-local-static cache in
# webserver_body_pipeline.cpp sees a deterministic value. Split into separate
# binaries because the magic-static cache means the first observation in
# the process locks in for the remainder. debug_dump_request_body_zero pins
# the explicit "0" value distinctly from the unset (absent-var) case.
debug_dump_request_body_unset_SOURCES = integ/debug_dump_request_body_unset_test.cpp
debug_dump_request_body_set_SOURCES = integ/debug_dump_request_body_set_test.cpp
debug_dump_request_body_zero_SOURCES = integ/debug_dump_request_body_zero_test.cpp

# TASK-076 Cycle 1: pin LT_SKIP / LT_SKIP_IF runtime semantics. Pure-host
# CPU test of the littletest framework itself — no library linkage
# needed beyond the default.
littletest_skip_semantics_SOURCES = unit/littletest_skip_semantics_test.cpp
littletest_skip_semantics_LDADD =

# TASK-079: pure-CPU self-test for the test-side digest-client helper
# (test/integ/digest_client.hpp). Pins the FIPS MD5/SHA-256 vectors, the
# RFC 7616 §3.3 parser shape, and the RFC 7616 §3.9.1 worked-example
# response value so the helper's behaviour is trusted before any integ
# test consumes it.
digest_client_self_test_SOURCES = unit/digest_client_self_test.cpp
digest_client_self_test_LDADD =

# TASK-096 -- v2 observable-behavior parity harness (test/parity/).
# Four unit binaries test the harness pieces themselves (parser, response
# framer, normalizer, v2 profile registry); transcript_runner executes the
# committed *.tseq corpus against the running v2 server through raw
# segmented sockets. The parser/framer/normalizer binaries are pure-host
# code with zero libhttpserver dependency (empty LDADD).
transcript_parser_SOURCES = unit/transcript_parser_test.cpp parity/transcript.cpp
transcript_parser_LDADD =

# TASK-096 Cycle 2: response framer (byte stream -> observed_response).
response_frame_SOURCES = unit/response_frame_test.cpp parity/response_frame.cpp
response_frame_LDADD =

# TASK-096 Cycle 3: normalizer (expectation-vs-observation comparison).
normalize_SOURCES = unit/normalize_test.cpp parity/normalize.cpp parity/response_frame.cpp parity/transcript.cpp
normalize_LDADD =
normalize_CPPFLAGS = $(AM_CPPFLAGS) -DNORMALIZE_FIXTURE_DIR='"$(top_srcdir)/test/data"'

# TASK-096 Cycle 4: v2 profile registry guard (every profile starts on
# port 0, serves /__smoke, stops cleanly).
v2_profile_registry_SOURCES = unit/v2_profile_registry_test.cpp parity/v2_fixture.cpp
v2_profile_registry_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TEST_DATA_DIR='"$(top_srcdir)/test"'

# TASK-097 -- v3 public semantic types (PRD-V3N-REQ-017/018/019/020/037,
# DR-V3-001). Header-only value types under httpserver::http; the unit
# tests need no library linkage.
http_semantic_outcome_SOURCES = unit/http_semantic_outcome_test.cpp
http_semantic_outcome_LDADD =

# TASK-097 step 2: v3 method type with extension round-trip (PRD-V3N-REQ-020).
http_semantic_method_SOURCES = unit/http_semantic_method_test.cpp
http_semantic_method_LDADD =

# TASK-097 step 3: insertion-ordered multivalue fields (REQ-017/018).
http_semantic_fields_SOURCES = unit/http_semantic_fields_test.cpp
http_semantic_fields_LDADD =

# TASK-097 step 4: request_head seed — raw target vs route path (REQ-019).
http_semantic_request_head_SOURCES = unit/http_semantic_request_head_test.cpp
http_semantic_request_head_LDADD =

# TASK-097 step 5: TLS-on/off header identity. consumer_v3_semantic is
# a compile-and-link gate: it includes <httpserver/http.hpp> and each
# sub-header directly WITHOUT HTTPSERVER_COMPILATION, so its per-target
# CPPFLAGS deliberately drops the -DHTTPSERVER_COMPILATION from
# AM_CPPFLAGS (mirroring the true-consumer compile in the top-level
# check-headers rule). Empty LDADD proves the v3 headers need no
# library linkage. v3_header_hygiene scans the six headers for config
# conditionals and backend/OS-socket tokens at runtime; it needs no
# library linkage either, only the header directory define.
consumer_v3_semantic_SOURCES = headers/consumer_v3_semantic.cpp
consumer_v3_semantic_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_semantic_LDADD =
v3_header_hygiene_SOURCES = unit/v3_header_hygiene_test.cpp
v3_header_hygiene_CPPFLAGS = $(AM_CPPFLAGS) -DV3_SEMANTIC_HEADER_DIR='"$(top_srcdir)/src/httpserver"'
v3_header_hygiene_LDADD =
# TASK-098 step 6: TLS-on/off header identity for the concurrency core.
# consumer_v3_concurrency includes <httpserver/concurrency/concurrency.hpp>
# and each sub-header WITHOUT HTTPSERVER_COMPILATION (per-target CPPFLAGS
# drop it, mirroring consumer_v3_semantic); empty LDADD proves the v3
# concurrency headers need no library linkage.
# Standalone executor surface must provide its own standard declarations.
consumer_v3_executor_SOURCES = headers/consumer_v3_executor.cpp
consumer_v3_executor_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_executor_LDADD =

consumer_v3_concurrency_SOURCES = headers/consumer_v3_concurrency.cpp
consumer_v3_concurrency_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_concurrency_LDADD =

# TASK-098 -- v3 concurrency core (PRD-V3N-REQ-024/025/031, DR-V3-003).
# Header-only public types under httpserver::concurrency; the unit tests
# need no library linkage.
task_executor_SOURCES = unit/task_executor_test.cpp
task_executor_LDADD =

# TASK-098 step 2: move-only single-consumer lazy task<T> with an owned
# coroutine frame. Header-only; no library linkage.
task_core_SOURCES = unit/task_core_test.cpp
task_core_LDADD =

# TASK-098 step 3: stop source/token fan-out with executor-posted
# cancellation. Header-only; no library linkage.
task_cancellation_SOURCES = unit/task_cancellation_test.cpp
task_cancellation_LDADD =

# TASK-098 step 4: idempotent one-shot resume signal with timeout.
# Header-only (background timeout thread included); no library linkage.
task_resume_signal_SOURCES = unit/task_resume_signal_test.cpp
task_resume_signal_LDADD =

# TASK-098 step 5: completion, timeout, disconnect and stop race suite.
task_race_SOURCES = unit/task_race_test.cpp
task_race_LDADD =

# TASK-098 step 5: request_stop-returns-inside-a-handler acceptance
# (PRD-V3N-REQ-031, DR-V3-008).
task_stop_inside_handler_SOURCES = unit/task_stop_inside_handler_test.cpp
task_stop_inside_handler_LDADD =

# TASK-101 -- v3 server configuration and resource budgets
# (PRD-V3N-REQ-009/014/016, DR-V3-001). Header-only public types under
# httpserver::server; the unit tests need no library linkage.
server_options_validate_SOURCES = unit/server_options_validate_test.cpp
server_options_validate_LDADD =

# TASK-101 step 3: hierarchical budgets. RAII reservations must release
# on every terminal path; header-only, no library linkage.
server_budgets_SOURCES = unit/server_budgets_test.cpp
server_budgets_LDADD =

# TASK-101 step 4: budget-bounded route registration with validated
# patterns (REQ-009). Header-only, no library linkage.
server_routes_SOURCES = unit/server_routes_test.cpp
server_routes_LDADD =

# TASK-101 step 5: TLS-on/off header identity for the server
# configuration area. consumer_v3_server includes
# <httpserver/server/configuration.hpp> and each sub-header WITHOUT
# HTTPSERVER_COMPILATION (per-target CPPFLAGS drop it, mirroring
# consumer_v3_semantic / consumer_v3_concurrency); empty LDADD proves
# the v3 server headers need no library linkage.
consumer_v3_server_SOURCES = headers/consumer_v3_server.cpp
consumer_v3_server_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_server_LDADD =

# TASK-102 -- header-time exchange decisions and route execution
# (PRD-V3N-REQ-009/023/024/025, DR-V3-003). The exchange area is
# header-only public surface plus a private seam under detail/; the
# unit tests include the seam under the AM_CPPFLAGS
# -DHTTPSERVER_COMPILATION define and need no library linkage.

# TASK-102 step 1: validated numeric status type (response seed).
http_status_SOURCES = unit/http_status_test.cpp
http_status_LDADD =

# TASK-102 step 2: exchange decision state machine (respond / admit /
# suspend / upgrade, double-terminal rejection, disconnect fan-out).
exchange_decisions_SOURCES = unit/exchange_decisions_test.cpp
exchange_decisions_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-102 step 3: registry match + route runner (exception
# containment, no-terminal-action 500 synthesis).
exchange_routing_SOURCES = unit/exchange_routing_test.cpp
exchange_routing_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-102 step 4: TLS-on/off header identity for the exchange area.
# consumer_v3_exchange includes <httpserver/exchange.hpp>,
# <httpserver/http/status.hpp> and <httpserver/server/routes.hpp>
# WITHOUT HTTPSERVER_COMPILATION (per-target CPPFLAGS drop it);
# Native linkage supplies the owned WebSocket session result.
consumer_v3_exchange_SOURCES = headers/consumer_v3_exchange.cpp
consumer_v3_exchange_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_exchange_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-103 -- bounded body reader and collect (PRD-V3N-REQ-021/022/025).
# Like the exchange area: header-only public surface (body_reader.hpp)
# plus an engine seam (detail::body_source) declared in the public
# header; the unit tests include the seam-free public headers under the
# AM_CPPFLAGS -DHTTPSERVER_COMPILATION define and need no library
# linkage.

# TASK-103 steps 1-4: one bounded incremental read (credit on
# consumption), EOF with trailers, collect(max) exactness, cancellation.
body_reader_SOURCES = unit/body_reader_test.cpp
body_reader_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-103 step 5: TLS-on/off header identity for the body area.
# consumer_v3_body_reader includes <httpserver/body_reader.hpp> and
# <httpserver/exchange.hpp> WITHOUT HTTPSERVER_COMPILATION (per-target
# CPPFLAGS drop it); empty LDADD proves the headers need no library
# linkage.
consumer_v3_body_reader_SOURCES = headers/consumer_v3_body_reader.cpp
consumer_v3_body_reader_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_body_reader_LDADD =

# TASK-104 -- response writer, body source and backpressure
# (PRD-V3N-REQ-025/026/027/037). Like the exchange and body areas:
# header-only public surface (response_writer.hpp) plus an engine seam
# (detail::body_sink) declared in the public header; the unit tests
# include the public headers under the AM_CPPFLAGS
# -DHTTPSERVER_COMPILATION define and need no library linkage.

# TASK-104 steps 1-4: one bounded write, park on a full queue, finish
# with trailers, typed sticky failures, disconnect cancellation,
# exactly-once close.
response_writer_SOURCES = unit/response_writer_test.cpp
response_writer_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-104 step 5: TLS-on/off header identity for the writer area.
# consumer_v3_response_writer includes <httpserver/response_writer.hpp>
# and <httpserver/exchange.hpp> WITHOUT HTTPSERVER_COMPILATION
# (per-target CPPFLAGS drop it); empty LDADD proves the headers need no
# library linkage.
consumer_v3_response_writer_SOURCES = headers/consumer_v3_response_writer.cpp
consumer_v3_response_writer_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_response_writer_LDADD =

# TASK-105 -- strict HTTP/1 start-line/header parser (PRD-V3N-REQ-004/
# 017/019, DR-V3-006). The parser lives in private HTTPSERVER_COMPILATION-
# gated headers under src/httpserver/detail/, reachable through the
# AM_CPPFLAGS -I src path; header-only, so no library linkage is needed.
http1_parser_SOURCES = unit/http1_parser_test.cpp
http1_parser_LDADD =

noinst_HEADERS = littletest.hpp parity/transcript.hpp parity/response_frame.hpp parity/normalize.hpp parity/server_fixture.hpp parity/v2_fixture.hpp parity/socket_io.hpp integ/test_utils.hpp integ/curl_helpers.hpp integ/server_ready.hpp integ/digest_client.hpp integ/stream_capture_helpers.hpp integ/log_capture.hpp unit/throw_probe.hpp unit/io_backend_contract.hpp unit/io_loopback.hpp unit/body_source_fake.hpp unit/body_sink_fake.hpp
AM_CXXFLAGS += -Wall -fPIC -Wno-overloaded-virtual

if COND_GCOV
AM_CFLAGS += -O0 --coverage --no-inline
AM_CXXFLAGS += -O0 --coverage --no-inline
AM_LDFLAGS += -O0 --coverage -lgcov --no-inline
endif

TESTS = $(check_PROGRAMS)

# TASK-020 has landed: <httpserver.hpp> no longer transitively pulls in any
# forbidden backend header, so header_hygiene is now an unconditional PASS.
# (Earlier milestones tracked the failure via XFAIL_TESTS; that line was
# removed when the umbrella went clean.)

@VALGRIND_CHECK_RULES@
# TASK-088: helgrind + drd race-detector suppression files join the shared
# VALGRIND_SUPPRESSIONS_FILES list. Suppression blocks are tool-tagged
# (Memcheck:/Helgrind:/drd:), so each tool ignores blocks meant for another;
# a single shared list carrying all three is functionally correct. Both new
# files ship comments-only (no entries) — real entries are added during
# CI-side finding-triage, never for libhttpserver's own locking.
VALGRIND_SUPPRESSIONS_FILES = libhttpserver.supp valgrind-helgrind.supp valgrind-drd.supp
EXTRA_DIST = libhttpserver.supp \
             valgrind-helgrind.supp \
             valgrind-drd.supp \
             tsan.supp \
             $(PARITY_TRANSCRIPTS) \
             PERFORMANCE.md \
             bench_harness.hpp \
             bench_baseline.hpp \
             v1_baseline/README.md \
             v1_baseline/v1_constants.hpp \
             v1_baseline/measure_v1_sizes.cpp \
             v1_baseline/measure_v1_get_headers.cpp

# TASK-039 -- Performance acceptance gates.
#
# Bench binaries verify the two PRD §3.6 numeric acceptance criteria:
#   * bench_get_headers              -> PRD-REQ-REQ-001 (>=10x speedup on get_headers)
#   * bench_sizeof_http_resource     -> PRD-REQ-REQ-003 (sizeof(http_resource) shrink)
#
# They live in EXTRA_PROGRAMS (not check_PROGRAMS), so:
#   - `make all`   does NOT build them.
#   - `make check` does NOT run them.
#   - `make bench` (the rule below) builds and runs both explicitly.
#
# Rationale (per TASK-039 plan §2 D4 / D6):
#   - Sanitizer runs of `make check` would distort the ratio; bench
#     belongs out of band on a quiet release-mode host.
#   - The bench is noise-sensitive and should not gate per-commit CI.
#     Release-readiness is gated on `make bench` succeeding once on
#     a controlled host (TASK-040+ runbook).
#
# How to add a new bench:
#   1. Append the program name to bench_targets below.
#   2. Add a `<name>_SOURCES = ...` line.
#   3. Add a `<name>_LDADD = ...` line if it needs more than the
#      default empty LDADD.
#   4. Run `make bench` from the build directory.
bench_targets = bench_sizeof_http_resource bench_get_headers bench_hook_overhead bench_route_lookup bench_warm_path
EXTRA_PROGRAMS = $(bench_targets)

# bench_sizeof_http_resource: pure compile-time static_assert guard.
# Note: this binary's only meaningful check is compilation; the runtime
# exit code carries no signal (main() returns 0 unconditionally). It is
# listed under bench_targets rather than check_PROGRAMS because the
# static_assert algebra references v1 baseline constants (from
# v1_constants.hpp) that assume a release-mode ABI, making it a release-
# validation step rather than a per-commit regression test.
# Empty LDADD: no symbols from libhttpserver are referenced at run time.
bench_sizeof_http_resource_SOURCES = bench_sizeof_http_resource.cpp
bench_sizeof_http_resource_LDADD =

# bench_get_headers: warms the get_headers() cache on an
# http_request built via create_test_request, then times 10x1M
# iterations. Compares the median ns/call to the v1 baseline.
# Needs -lmicrohttpd because create_test_request transitively
# constructs an http_request whose impl includes <microhttpd.h>.
bench_get_headers_SOURCES = bench_get_headers.cpp bench_harness.hpp
bench_get_headers_LDADD = $(LDADD) -lmicrohttpd

# bench_hook_overhead (TASK-052): asserts the dispatch hot path's
# per-phase any_hooks_ atomic-load gate stays within a 50ns ceiling
# when no hooks are registered. The gate cost is what every request
# pays per phase regardless of hook registration, and is the load-
# bearing measurement for PRD-HOOK-REQ-009 / §4.10's "zero-cost-when-
# unused" claim. Defines HTTPSERVER_COMPILATION so the bench can
# reach webserver_test_access; this is the same friend pattern used
# by test/unit/hook_api_shape_test.cpp.
bench_hook_overhead_SOURCES = bench_hook_overhead.cpp bench_harness.hpp
bench_hook_overhead_LDADD = $(LDADD) -lmicrohttpd

# bench_route_lookup (TASK-053): v2 dispatch performance acceptance.
# Drives webserver_impl::lookup_v2() directly (no MHD daemon, no
# sockets) and asserts two ceilings on the dispatch hot path:
#   (a) cache_warm_ns median <= 200 ns/lookup,
#   (b) radix_pure_ns median <= 5 us/lookup for an 8-segment
#       parameterized path (cache cold -- TASK-083 separates this from
#       the cache-warm measurement).
# Defines HTTPSERVER_COMPILATION so the bench can reach
# webserver_test_access, the same friend pattern used by
# bench_hook_overhead.cpp and the unit tests.
bench_route_lookup_SOURCES = bench_route_lookup.cpp bench_harness.hpp
bench_route_lookup_LDADD = $(LDADD) -lmicrohttpd

# bench_warm_path (TASK-058): per-request allocation pass.  Times
# canonicalize_lookup_path, should_skip_auth (non-empty + empty list),
# and serialize_allow_methods to verify the TASK-058 refactors land
# without regressing the warm GET path.  TASK-083 gates each median
# against per-platform baselines in bench_baseline.hpp (fail on >5%
# regression).  Defines HTTPSERVER_COMPILATION so the bench can reach
# webserver_test_access, the same friend pattern used by
# bench_route_lookup.
bench_warm_path_SOURCES = bench_warm_path.cpp bench_harness.hpp bench_baseline.hpp
bench_warm_path_LDADD = $(LDADD) -lmicrohttpd

bench: $(bench_targets)
	@for p in $(bench_targets); do \
	    echo "=== Running bench: $$p ==="; \
	    ./$$p || exit $$?; \
	done

# TASK-092: per-PR CI wiring for the two DR-008 stress gates. Both
# binaries already live in check_PROGRAMS (so `make check` runs each
# once), but the concurrency/deadlock contracts benefit from a
# deliberate, repeatable, env-gated invocation on the matrix. These
# convenience targets are what the tsan and baseline-gcc CI lanes call;
# see test/PERFORMANCE.md ("CI wiring — DR-008 stress gates").

# check-route-table-concurrency: loop the TSan stress binary
# RTC_ITERATIONS times (default 8) so the tsan lane hits the typical
# regression window instead of a single make-check pass. Time-boxed by
# the CI step's timeout-minutes (<= 2 min).
# RTC = route_table_concurrency
RTC_ITERATIONS ?= 8
# exit $$? (not exit 1) preserves route_table_concurrency's real exit code
# in the CI log and stops the loop on first failure.
check-route-table-concurrency: route_table_concurrency
	@for i in $$(seq 1 $(RTC_ITERATIONS)); do \
	    echo "=== route_table_concurrency iteration $$i/$(RTC_ITERATIONS) ==="; \
	    ./route_table_concurrency || exit $$?; \
	done

# check-stop-from-handler: run threadsafety_stress with the DR-008
# stop()-from-handler deadlock sub-test enabled. Sub-tests A/C are
# shrunk to STOP_HANDLER_STRESS_SECONDS (default 1s) so Sub-test B is
# the point of the run. Failure mode is a hang -> the CI step's
# timeout-minutes catches it.
STOP_HANDLER_STRESS_SECONDS ?= 1
check-stop-from-handler: threadsafety_stress
	@HTTPSERVER_RUN_STOP_FROM_HANDLER=1 \
	 HTTPSERVER_STRESS_SECONDS=$(STOP_HANDLER_STRESS_SECONDS) \
	 ./threadsafety_stress

.PHONY: bench check-route-table-concurrency check-stop-from-handler

# TASK-106 -- authoritative HTTP/1 body framing and trailers
# (PRD-V3N-REQ-004/017/021/023, DR-V3-006). Like the head parser: the
# body mode, decoder, and adapter live in private
# HTTPSERVER_COMPILATION-gated headers under src/httpserver/detail/,
# reachable through the AM_CPPFLAGS -I src path; header-only, so no
# library linkage is needed.
http1_body_mode_SOURCES = unit/http1_body_mode_test.cpp
http1_body_mode_LDADD =

http1_body_decoder_SOURCES = unit/http1_body_decoder_test.cpp
http1_body_decoder_LDADD =

http1_body_source_SOURCES = unit/http1_body_source_test.cpp
http1_body_source_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-107 -- HTTP/1 response framing and ordered persistence
# (PRD-V3N-REQ-004/026/027, DR-V3-006). Like the request-side framing:
# the response mode, framer, and outbox live in private
# HTTPSERVER_COMPILATION-gated headers under src/httpserver/detail/,
# reachable through the AM_CPPFLAGS -I src path; header-only, so no
# library linkage is needed. The framer and outbox tests compile the
# parity corpus parsers (response_frame/normalize/transcript) directly
# and read the live transcript corpus at runtime through
# PARITY_TRANSCRIPT_DIR.
http1_response_mode_SOURCES = unit/http1_response_mode_test.cpp
http1_response_mode_LDADD =

http1_response_framer_SOURCES = unit/http1_response_framer_test.cpp \
	parity/response_frame.cpp parity/normalize.cpp parity/transcript.cpp
http1_response_framer_LDADD =
http1_response_framer_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'

# write_front exercises the real io_operation + fake backend seam, so
# this program links libhttpserver.la (the TASK-099 convention) instead
# of relying on the header-only default.
http1_response_outbox_SOURCES = unit/http1_response_outbox_test.cpp \
	parity/response_frame.cpp
http1_response_outbox_LDADD = $(LDADD)

# TASK-108 step 1: loopback/any-address listener bind helper
# (pollsys::open_listener): v4/ANY/v6 literals, resolved port report,
# SO_REUSEADDR, nonblocking accept, name rejection. TASK-119 adds the
# fill_peer battery (accept-time peer capture) cross-checked against
# net::parse_address over the shared byte decode in v3core
# (detail/net_address.cpp), so the suite uses the default LDADD and
# links libhttpserver.la.
io_poll_sys_listener_SOURCES = unit/io_poll_sys_listener_test.cpp

# TASK-108 step 2: the native server's thread-pool executor. Links
# libhttpserver.la (the TASK-099 convention): the program exercises the
# new detail/worker_pool.cpp registration in src/Makefile.am.
worker_pool_SOURCES = unit/worker_pool_test.cpp

# TASK-108 step 4: the per-connection HTTP/1 engine over the real poll
# driver and a loopback pair (GET round trip, 404 synthesis, EOF close,
# budget refusal). Links libhttpserver.la for the engine .cpp files.
connection_engine_SOURCES = unit/connection_engine_test.cpp \
	parity/response_frame.cpp

# TASK-108 step 6: the bare error response serializer. Header-only
# surface; wire observations parsed through the parity frame parser.
http1_error_synth_SOURCES = unit/http1_error_synth_test.cpp \
	parity/response_frame.cpp
http1_error_synth_LDADD =

# TASK-108 step 8: the native_server lifecycle over the real listener
# engines (bind, stop join, REQ-016 validate gate, route-after-listen,
# destructor stop). Links libhttpserver.la for the engine .cpp files.
native_server_SOURCES = unit/native_server_test.cpp

# TASK-110 step 1: the counted drain scope in isolation (units, the
# deadline-bound wait, at-most-once cancellation, counted-thread
# rejection). Links libhttpserver.la for detail/drain_scope.cpp.
drain_scope_SOURCES = unit/drain_scope_test.cpp

# TASK-108 step 8: a consumer including <httpserver/server/server.hpp>
# without HTTPSERVER_COMPILATION must compile and link; the pimpl keeps
# the engine types out of the header, so the empty LDADD proves the
# surface needs no library linkage.
consumer_v3_native_server_SOURCES = headers/consumer_v3_native_server.cpp
consumer_v3_native_server_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_native_server_LDADD =

# TASK-108 step 9: the native HTTP/1 end-to-end suite over raw loopback
# clients (pollsys shims; no curl): keep-alive, version-conditional
# framing and close, pipelining, error synthesis, watchdog timeouts,
# client aborts, concurrency, handler-safe stop, the validate gate,
# and the connections budget.
native_http1_e2e_SOURCES = integ/native_http1_e2e_test.cpp \
	parity/response_frame.cpp

# TASK-108 step 10 (PRD-V3N-REQ-001/002): the link-surface audit.
# Links ONLY the v3core convenience archive -- no microhttpd, gnutls,
# or curl. Linking is the audit; scripts/audit-v3-native-linkage.sh
# (wired into check-local) inspects the binary's runtime dependencies.
v3_native_linkage_SOURCES = integ/v3_native_linkage_test.cpp
v3_native_linkage_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-111 step 1: the bounded synchronous value-returning route
# adapter (PRD-V3N-REQ-021/022, DR-V3-003). Header-only surface
# (server/route_sync.hpp), so the unit suite needs no library linkage;
# the rig reuses the scripted body fakes and the detail::run_route
# runner.
route_sync_SOURCES = unit/route_sync_test.cpp
route_sync_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-112 step 1: immutable reusable response definitions and
# overlays (PRD-V3N-REQ-026/028/029/030, DR-V3-005). Header-only surface
# (response_definition.hpp); the rig also constructs native sessions.
response_definition_SOURCES = unit/response_definition_test.cpp
response_definition_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-113: file, pipe and borrowed-buffer response ownership
# (PRD-V3N-REQ-028/029, DR-V3-005). Header-only surface
# (response_sources.hpp / response_definition.hpp); the rig constructs
# native sessions as part of the exchange sink.
response_sources_SOURCES = unit/response_sources_test.cpp
response_sources_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-112 step 5: a consumer including
# <httpserver/response_definition.hpp> WITHOUT HTTPSERVER_COMPILATION
# must compile and link; the surface is header-only, so the empty
# LDADD proves it needs no library linkage.
consumer_v3_response_definition_SOURCES = headers/consumer_v3_response_definition.cpp
consumer_v3_response_definition_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_response_definition_LDADD =

# TASK-113: a consumer including
# <httpserver/response_sources.hpp> WITHOUT HTTPSERVER_COMPILATION
# must compile and link; the vocabulary is header-only, so the empty
# LDADD proves it needs no library linkage.
consumer_v3_response_sources_SOURCES = headers/consumer_v3_response_sources.cpp
consumer_v3_response_sources_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_response_sources_LDADD =

# TASK-111 step 2: a consumer including
# <httpserver/server/route_sync.hpp> WITHOUT HTTPSERVER_COMPILATION
# must compile and link; the adapter is header-only, so the empty LDADD
# proves the surface needs no library linkage.
consumer_v3_route_sync_SOURCES = headers/consumer_v3_route_sync.cpp
consumer_v3_route_sync_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_route_sync_LDADD =

# TASK-114 step 1: the in-tree Base64 codec (RFC 4648). Header-only
# surface (detail/base64.hpp), so the unit suite needs no library
# linkage.
base64_vector_SOURCES = unit/base64_vector_test.cpp
base64_vector_LDADD =

# TASK-114 step 2: the in-tree SHA-1 (FIPS 180-4 / RFC 6234) used by
# the RFC 6455 upgrade-handshake accept computation. Header-only
# surface (detail/sha1.hpp), so the unit suite needs no library
# linkage.
sha1_vector_SOURCES = unit/sha1_vector_test.cpp
sha1_vector_LDADD =

# TASK-114 step 3: the in-tree MD5 (RFC 1321, required by RFC 7616
# Digest) and SHA-256 (the documented SHA-256 Digest variant). Both
# header-only surfaces (detail/md5.hpp, detail/sha256.hpp), so the
# unit suites need no library linkage.
md5_vector_SOURCES = unit/md5_vector_test.cpp
md5_vector_LDADD =

sha256_vector_SOURCES = unit/sha256_vector_test.cpp
sha256_vector_LDADD =

# TASK-114 step 4: the constant-time comparison (secure_compare.hpp)
# and the OS randomness seam (entropy_sys.hpp). Both header-only
# surfaces, so the unit suites need no library linkage.
secure_compare_SOURCES = unit/secure_compare_test.cpp
secure_compare_LDADD =

entropy_sys_SOURCES = unit/entropy_sys_test.cpp
entropy_sys_LDADD =

# TASK-115 step 1: the Digest nonce (keyed tag, mint, classification)
# and its bounded nc ledger. Header-only private surfaces
# (detail/digest_hex.hpp, digest_nonce.hpp, digest_ledger.hpp), so the
# unit suite needs no library linkage.
digest_nonce_ledger_SOURCES = unit/digest_nonce_ledger_test.cpp
digest_nonce_ledger_LDADD =

# TASK-115 step 2: the bounded Authorization: Digest field parser
# (detail/digest_params.hpp), header-only, no library linkage.
digest_params_SOURCES = unit/digest_params_test.cpp
digest_params_LDADD =

# TASK-115 step 3: the RFC 7616 response computation, pinned against
# the RFC's own worked examples plus the independent test-side MD5 for
# the legacy no-qop chain. Header-only, no library linkage.
digest_response_vector_SOURCES = unit/digest_response_vector_test.cpp
digest_response_vector_LDADD =

# TASK-114 step 5: the Basic authentication policy above semantic
# request heads. The members live in the library (detail/auth_basic.cpp
# in v3core), so the unit suite links libhttpserver.la (default LDADD).
basic_auth_policy_SOURCES = unit/basic_auth_policy_test.cpp

# TASK-115 step 4: the Digest authentication policy above semantic
# request heads. The members live in the library (detail/auth_digest.cpp
# in v3core), so the unit suite links libhttpserver.la (default LDADD).
# Black-box through the independent integ/digest_client.hpp client.
digest_auth_policy_SOURCES = unit/digest_auth_policy_test.cpp

# TASK-114: a consumer including <httpserver/auth/basic_auth.hpp>
# WITHOUT HTTPSERVER_COMPILATION must compile and link; the empty LDADD
# proves the vocabulary surface needs no library linkage (every check
# is a static_assert over unevaluated expressions).
consumer_v3_auth_SOURCES = headers/consumer_v3_auth.cpp
consumer_v3_auth_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_auth_LDADD =

# TASK-114 step 6: parity replay of all three auth_basic.tseq cases
# through the REAL policy and the REAL response framer -- no
# hand-synthesized challenge fields. The policy members live in the
# library, so the suite links libhttpserver.la (default LDADD) and
# compiles the parity corpus parsers directly, reading the live
# transcript at runtime (the TASK-107 framer convention).
basic_auth_corpus_SOURCES = unit/basic_auth_corpus_test.cpp \
	parity/response_frame.cpp parity/normalize.cpp parity/transcript.cpp
basic_auth_corpus_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'

# TASK-115 step 5: parity replay of both auth_digest.tseq cases
# through the REAL Digest policy and the REAL response framer. The
# roundtrip case answers the policy's challenge with the independent
# test-side RFC 7616 client before re-classifying, so the suite links
# libhttpserver.la (default LDADD) and compiles the parity corpus
# parsers directly (the TASK-114 basic corpus convention).
digest_auth_corpus_SOURCES = unit/digest_auth_corpus_test.cpp \
	parity/response_frame.cpp parity/normalize.cpp parity/transcript.cpp
digest_auth_corpus_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'


# TASK-116 step 1: the strict incremental urlencoded body decoder
# (detail/forms_urlencoded.hpp). Header-only, no library linkage;
# AM_CPPFLAGS carries -DHTTPSERVER_COMPILATION so the gated detail
# header is includable from the test tree.
urlencoded_decoder_SOURCES = unit/urlencoded_decoder_test.cpp
urlencoded_decoder_LDADD =

# TASK-117 step 1: the strict incremental multipart wire decoder
# (detail/forms_multipart.hpp). Header-only, no library linkage;
# AM_CPPFLAGS carries -DHTTPSERVER_COMPILATION so the gated detail
# header is includable from the test tree.
multipart_decoder_SOURCES = unit/multipart_decoder_test.cpp
multipart_decoder_LDADD =

# TASK-117 step 2: the multipart form vocabulary, one-shot decode,
# and multipart_read rejection verdicts (forms/multipart.hpp). The
# members live in the library (detail/forms_multipart.cpp in v3core),
# so the suite links libhttpserver.la (default LDADD).
forms_multipart_SOURCES = unit/forms_multipart_test.cpp

# TASK-117 step 3: the bounded multipart route adapter and the
# streaming read, driven through run_route with the scripted fakes
# (the route_sync convention). The members live in the library
# (detail/forms_multipart.cpp in v3core), so the suite links
# libhttpserver.la (default LDADD).
forms_multipart_route_SOURCES = unit/forms_multipart_route_test.cpp

# TASK-117 step 3: temp_file_part_sink (the v2 disk-upload port),
# driven through the real decode_multipart driver against a mkdtemp
# scratch directory. The members live in the library
# (detail/forms_multipart_files.cpp in v3core), so the suite links
# libhttpserver.la (default LDADD).
forms_multipart_files_SOURCES = unit/forms_multipart_files_test.cpp

# TASK-117: a consumer including <httpserver/forms/multipart.hpp>
# WITHOUT HTTPSERVER_COMPILATION must compile and link; the empty
# LDADD proves the vocabulary surface needs no library linkage (every
# check is a static_assert over unevaluated expressions).
consumer_v3_multipart_SOURCES = headers/consumer_v3_multipart.cpp
consumer_v3_multipart_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_multipart_LDADD =

# TASK-116 step 2: the urlencoded form vocabulary, one-shot decode,
# and form_read rejection verdicts (forms/urlencoded.hpp). The members
# live in the library (detail/forms_urlencoded.cpp in v3core), so the
# suite links libhttpserver.la (default LDADD, the basic_auth_policy
# convention).
forms_urlencoded_SOURCES = unit/forms_urlencoded_test.cpp

# TASK-116 step 3: the bounded urlencoded route adapter and the
# streaming read, driven through run_route with the scripted fakes
# (the route_sync convention). The members live in the library
# (detail/forms_urlencoded.cpp in v3core), so the suite links
# libhttpserver.la (default LDADD).
forms_urlencoded_route_SOURCES = unit/forms_urlencoded_route_test.cpp

# TASK-116: a consumer including <httpserver/forms/urlencoded.hpp>
# WITHOUT HTTPSERVER_COMPILATION must compile and link; the empty
# LDADD proves the vocabulary surface needs no library linkage (every
# check is a static_assert over unevaluated expressions).
consumer_v3_forms_SOURCES = headers/consumer_v3_forms.cpp
consumer_v3_forms_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_forms_LDADD =

# TASK-116 step 5: parity replay of the forms.tseq urlencoded_echo
# case through the REAL decode and the REAL response framer (the
# TASK-114 corpus convention). The decode members live in the library,
# so the suite links libhttpserver.la (default LDADD) and compiles the
# parity corpus parsers directly, reading the live transcript at
# runtime. multipart_field stays with the v2 transcript_runner lane
# until TASK-117.
forms_urlencoded_corpus_SOURCES = unit/forms_urlencoded_corpus_test.cpp \
	parity/response_frame.cpp parity/normalize.cpp parity/transcript.cpp parity/case_wire.cpp
forms_urlencoded_corpus_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'

# TASK-117 step 5: parity replay of the forms.tseq multipart_field
# case through the REAL multipart decode and the REAL response framer
# (the TASK-114 corpus convention; the request-side rebuild comes from
# the promoted parity/case_wire.cpp). The decode members live in the
# library, so the suite links libhttpserver.la (default LDADD) and
# compiles the parity corpus parsers directly, reading the live
# transcript at runtime.
forms_multipart_corpus_SOURCES = unit/forms_multipart_corpus_test.cpp \
	parity/response_frame.cpp parity/normalize.cpp parity/transcript.cpp parity/case_wire.cpp
forms_multipart_corpus_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'

# TASK-118 step 1: the ported route families -- method sets, the prefix
# family, and resolve() with the v2 precedence shape (plan D1). The
# registry stays header-only and self-contained, so the unit suite
# links libhttpserver.la (default LDADD, the server_routes convention).
server_routes_families_SOURCES = unit/server_routes_families_test.cpp

# TASK-118 step 2: the lifecycle hook vocabulary and bus (plan D4).
# The bus storage lives in v3core (detail/server_hooks.cpp), so the
# unit suite links libhttpserver.la (default LDADD).
server_hooks_SOURCES = unit/server_hooks_test.cpp

# TASK-118 step 3: the request dispatch pipeline over the real
# registry + bus + runner (the route_sync rig convention). The
# dispatcher lives in v3core, so the suite links libhttpserver.la
# (default LDADD).
request_lifecycle_SOURCES = unit/request_lifecycle_test.cpp

# TASK-118 step 5: a consumer including
# <httpserver/server/hooks.hpp> WITHOUT HTTPSERVER_COMPILATION must
# compile and link; the vocabulary surface is header-only (every check
# an unevaluated static_assert), so the empty LDADD proves it needs no
# library linkage (the bus storage itself lives in v3core).
consumer_v3_hooks_SOURCES = headers/consumer_v3_hooks.cpp
consumer_v3_hooks_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_hooks_LDADD =

# TASK-118 step 6: parity replay of the routing and hooks corpora
# through the REAL registry + hook bus + dispatcher + response framer
# (plan D6, the TASK-114 corpus convention; the shared driver lives in
# parity/v3_dispatch_replay.hpp). The dispatcher lives in v3core, so
# the suites link libhttpserver.la (default LDADD).
routing_corpus_SOURCES = unit/routing_corpus_test.cpp \
	parity/case_wire.cpp parity/response_frame.cpp parity/normalize.cpp \
	parity/transcript.cpp
routing_corpus_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'

hooks_corpus_SOURCES = unit/hooks_corpus_test.cpp \
	parity/case_wire.cpp parity/response_frame.cpp parity/normalize.cpp \
	parity/transcript.cpp
hooks_corpus_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'

# TASK-119 step 1: the net address vocabulary (net/address.hpp) --
# literal parsing across families, v4-mapped normalization, the
# pattern spellings, and the family isolation of matches. The parsing
# and text conversion live in v3core (detail/net_address.cpp), so the
# unit suite links libhttpserver.la (default LDADD).
net_address_SOURCES = unit/net_address_test.cpp

# TASK-119 step 1: a consumer including
# <httpserver/net/address.hpp> WITHOUT HTTPSERVER_COMPILATION must
# compile and link; every check is an unevaluated static_assert, so
# the empty LDADD proves the vocabulary surface needs no library
# linkage (parsing and text conversion live in v3core).
consumer_v3_net_address_SOURCES = headers/consumer_v3_net_address.cpp
consumer_v3_net_address_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_net_address_LDADD =

# TASK-119 step 2: the thread-safe peer policy store (the v2
# deny_ip/allow_ip semantics over the prefix-bits vocabulary). The
# storage lives in v3core (detail/peer_policy.cpp), so the unit suite
# links libhttpserver.la (default LDADD).
peer_policy_SOURCES = unit/peer_policy_test.cpp

# TASK-119 step 2: a consumer including
# <httpserver/server/peer_policy.hpp> WITHOUT HTTPSERVER_COMPILATION
# must compile and link; every check is an unevaluated static_assert,
# so the empty LDADD proves the vocabulary surface needs no library
# linkage (the store itself lives in v3core).
consumer_v3_peer_policy_SOURCES = headers/consumer_v3_peer_policy.cpp
consumer_v3_peer_policy_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_peer_policy_LDADD =

# TASK-119 step 6: parity replay of the ip_controls corpus (the pinned
# allow_listed_loopback case byte-for-byte through the REAL registry +
# dispatcher + framer with the peer policy in the loop) plus the
# refusal twin the v2 transcript deliberately does not pin (zero
# application bytes, one request_completed with end=peer_refused).
# The dispatcher lives in v3core, so the suite links libhttpserver.la
# (default LDADD).
ip_controls_corpus_SOURCES = unit/ip_controls_corpus_test.cpp \
	parity/case_wire.cpp parity/response_frame.cpp parity/normalize.cpp \
	parity/transcript.cpp
ip_controls_corpus_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'

# TASK-120: real native loopback replay of committed HTTP/1 pins.
native_http1_parity_SOURCES = integ/native_http1_parity_test.cpp \
	parity/transcript.cpp parity/response_frame.cpp parity/normalize.cpp
native_http1_parity_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"' -DPARITY_DATA_ROOT='"$(top_srcdir)/test"'

# TASK-121: transport-neutral protocol tests link the native archive only.
websocket_codec_SOURCES = unit/websocket_codec_test.cpp
websocket_codec_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
websocket_utf8_SOURCES = unit/websocket_utf8_test.cpp
websocket_utf8_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
websocket_session_SOURCES = unit/websocket_session_test.cpp
websocket_session_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
websocket_session_race_SOURCES = unit/websocket_session_race_test.cpp
websocket_session_race_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
consumer_v3_websocket_SOURCES = headers/consumer_v3_websocket.cpp
consumer_v3_websocket_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_websocket_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
EXTRA_DIST += unit/websocket_test_helpers.hpp

http1_websocket_handshake_SOURCES = unit/http1_websocket_handshake_test.cpp
http1_websocket_handshake_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
websocket_progress_SOURCES = unit/websocket_progress_test.cpp
websocket_progress_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
websocket_upgrade_decision_SOURCES = unit/websocket_upgrade_decision_test.cpp
websocket_upgrade_decision_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
native_websocket_fixture_SOURCES = integ/native_websocket_fixture.cpp
native_websocket_fixture_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
EXTRA_DIST += integ/native_websocket_client.py

http1_websocket_upgrade_SOURCES = unit/http1_websocket_upgrade_test.cpp
http1_websocket_upgrade_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
TESTS += integ/native_websocket_e2e.sh
EXTRA_DIST += integ/native_websocket_e2e.sh

task_resume_signal_lifetime_SOURCES = unit/task_resume_signal_lifetime_test.cpp
task_resume_signal_lifetime_LDADD =

websocket_drain_SOURCES = unit/websocket_drain_test.cpp
websocket_drain_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
websocket_drain_race_SOURCES = unit/websocket_drain_race_test.cpp
websocket_drain_race_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
EXTRA_DIST += unit/websocket_engine_helpers.hpp

# Portable external-loop contract: source-only public includes, no backend link.
consumer_v3_readiness_SOURCES = headers/consumer_v3_readiness.cpp
consumer_v3_readiness_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_readiness_LDADD =
readiness_contract_SOURCES = unit/readiness_contract_test.cpp
readiness_contract_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/test
readiness_contract_LDADD =

external_readiness_adapter_SOURCES = unit/external_readiness_adapter_test.cpp
external_readiness_adapter_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

external_http1_e2e_SOURCES = integ/external_http1_e2e_test.cpp parity/response_frame.cpp parity/normalize.cpp
external_http1_e2e_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

io_epoll_backend_SOURCES = unit/io_epoll_backend_test.cpp
io_epoll_backend_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
io_backend_contract_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

io_kqueue_backend_SOURCES = unit/io_kqueue_backend_test.cpp
io_kqueue_backend_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

# TASK-128: reusable RFC corpora, deterministic fuzz replay and native plateaus.
check_PROGRAMS += http1_conformance native_http1_conformance websocket_conformance protocol_fuzz_replay native_protocol_memory
protocol_corpus_flags = -DHTTP1_CORPUS_DIR='"$(top_srcdir)/test/conformance/http1"' -DWEBSOCKET_CORPUS_DIR='"$(top_srcdir)/test/conformance/websocket"'
http1_conformance_SOURCES = unit/http1_conformance_test.cpp
http1_conformance_CPPFLAGS = $(AM_CPPFLAGS) $(protocol_corpus_flags)
http1_conformance_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
native_http1_conformance_SOURCES = integ/native_http1_conformance_test.cpp parity/response_frame.cpp parity/normalize.cpp
native_http1_conformance_CPPFLAGS = $(AM_CPPFLAGS) $(protocol_corpus_flags)
native_http1_conformance_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
websocket_conformance_SOURCES = unit/websocket_conformance_test.cpp
websocket_conformance_CPPFLAGS = $(AM_CPPFLAGS) $(protocol_corpus_flags)
websocket_conformance_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
protocol_fuzz_replay_SOURCES = fuzz/protocol_fuzz_replay.cpp fuzz/http1_head_fuzz.cpp fuzz/http1_body_fuzz.cpp fuzz/websocket_codec_fuzz.cpp
protocol_fuzz_replay_CPPFLAGS = $(AM_CPPFLAGS) $(protocol_corpus_flags)
protocol_fuzz_replay_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
native_protocol_memory_SOURCES = integ/native_protocol_memory_test.cpp
native_protocol_memory_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
EXTRA_DIST += conformance/corpus.hpp fuzz/protocol_fuzz.hpp conformance/http1/README.md conformance/http1/cases.tsv conformance/websocket/README.md conformance/websocket/cases.tsv
EXTRA_DIST += conformance/http1/absolute-invalid.wire \
	conformance/http1/bad-chunk.wire \
	conformance/http1/bad-percent.wire \
	conformance/http1/bad-token.wire \
	conformance/http1/bad-trailer.wire \
	conformance/http1/bare-cr.wire \
	conformance/http1/chunk-extension.wire \
	conformance/http1/chunk-overflow.wire \
	conformance/http1/chunk-terminator.wire \
	conformance/http1/chunk-trailers.wire \
	conformance/http1/comma-cl.wire \
	conformance/http1/conflicting-cl.wire \
	conformance/http1/control-value.wire \
	conformance/http1/double-chunked.wire \
	conformance/http1/duplicate-cl.wire \
	conformance/http1/duplicate-host.wire \
	conformance/http1/empty-target.wire \
	conformance/http1/forbidden-trailer.wire \
	conformance/http1/http10-te.wire \
	conformance/http1/http10.wire \
	conformance/http1/http11.wire \
	conformance/http1/invalid-host.wire \
	conformance/http1/lone-lf.wire \
	conformance/http1/malformed-cl.wire \
	conformance/http1/missing-host.wire \
	conformance/http1/negative-cl.wire \
	conformance/http1/nul-value.wire \
	conformance/http1/obs-fold.wire \
	conformance/http1/overflow-cl.wire \
	conformance/http1/space-colon.wire \
	conformance/http1/te-cl.wire \
	conformance/http1/trailer-bytes-over.wire \
	conformance/http1/trailer-fields-over.wire \
	conformance/http1/truncated-chunk.wire \
	conformance/http1/truncated-length.wire \
	conformance/http1/unsupported-te.wire \
	conformance/http1/wrong-te-order.wire \
	conformance/http1/zero-body.wire \
	conformance/websocket/binary.wire \
	conformance/websocket/close-invalid-reason.wire \
	conformance/websocket/close-one-byte.wire \
	conformance/websocket/close-reserved.wire \
	conformance/websocket/empty-text.wire \
	conformance/websocket/fragment-limit.wire \
	conformance/websocket/fragmented-control.wire \
	conformance/websocket/fragmented.wire \
	conformance/websocket/high-bit64.wire \
	conformance/websocket/incomplete-utf8.wire \
	conformance/websocket/invalid-text.wire \
	conformance/websocket/large-control.wire \
	conformance/websocket/message-limit.wire \
	conformance/websocket/new-message-in-fragment.wire \
	conformance/websocket/nonminimal16.wire \
	conformance/websocket/nonminimal64.wire \
	conformance/websocket/orphan-continuation.wire \
	conformance/websocket/ping.wire \
	conformance/websocket/reserved-control.wire \
	conformance/websocket/reserved-opcode.wire \
	conformance/websocket/rsv1.wire \
	conformance/websocket/rsv2.wire \
	conformance/websocket/rsv3.wire \
	conformance/websocket/split-utf8.wire \
	conformance/websocket/text.wire \
	conformance/websocket/unmasked.wire

# Fuzz targets stay outside check_PROGRAMS; replay is deterministic.
.PHONY: check-v3-http1-websocket
V3_PROTOCOL_TESTS = http1_conformance native_http1_conformance websocket_conformance protocol_fuzz_replay native_protocol_memory http1_parser http1_body_mode http1_body_decoder http1_body_source http1_response_outbox native_http1_e2e native_http1_parity websocket_codec websocket_utf8 websocket_session websocket_session_race websocket_progress http1_websocket_handshake http1_websocket_upgrade websocket_upgrade_decision websocket_drain websocket_drain_race routing_corpus hooks_corpus basic_auth_corpus digest_auth_corpus forms_urlencoded_corpus forms_multipart_corpus ip_controls_corpus
check-v3-http1-websocket: $(V3_PROTOCOL_TESTS) native_websocket_fixture
	$(top_srcdir)/scripts/run-v3-http1-websocket-gates.sh --build-dir $(abs_top_builddir)
TESTS += integ/protocol_gate_runner_test.py integ/protocol_fuzz_runner_test.py
EXTRA_DIST += integ/protocol_gate_runner_test.py integ/protocol_fuzz_runner_test.py
EXTRA_DIST += conformance/http1/host-path.wire conformance/http1/host-userinfo.wire conformance/http1/host-port-invalid.wire conformance/http1/host-bracket-invalid.wire conformance/http1/host-percent-invalid.wire conformance/http1/host-ipv6.wire conformance/http1/host-regname.wire conformance/http1/host-empty.wire

# These protocol consumers need only the native archive.
native_http1_parity_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
native_http1_e2e_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
routing_corpus_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
hooks_corpus_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
basic_auth_corpus_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
digest_auth_corpus_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
forms_urlencoded_corpus_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
forms_multipart_corpus_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
ip_controls_corpus_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

EXTRA_DIST += conformance/websocket/close-below-range.wire conformance/websocket/close-reserved-1004.wire conformance/websocket/close-reserved-1006.wire conformance/websocket/close-reserved-1015.wire conformance/websocket/close-above-range.wire conformance/websocket/close-empty.wire

check_PROGRAMS += consumer_v3_features
consumer_v3_features_SOURCES = headers/consumer_v3_features.cpp
consumer_v3_features_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_features_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
AM_TESTS_ENVIRONMENT = V3_TLS_MODE="$(enable_v3_tls)"; export V3_TLS_MODE;

basic_auth_corpus_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

body_reader_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

consumer_v3_exchange_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

consumer_v3_features_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

consumer_v3_websocket_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

digest_auth_corpus_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

exchange_decisions_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

exchange_routing_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

external_http1_e2e_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

external_readiness_adapter_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

forms_multipart_corpus_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

forms_urlencoded_corpus_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

hooks_corpus_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

http1_body_source_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

http1_conformance_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

http1_websocket_handshake_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

http1_websocket_upgrade_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

io_backend_contract_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

io_epoll_backend_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

io_kqueue_backend_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

ip_controls_corpus_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

native_http1_conformance_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

native_http1_e2e_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

native_http1_parity_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

native_protocol_memory_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

native_websocket_fixture_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

protocol_fuzz_replay_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

response_definition_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

response_sources_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

response_writer_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

route_sync_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

routing_corpus_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

v3_native_linkage_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

websocket_codec_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

websocket_conformance_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

websocket_drain_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

websocket_drain_race_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

websocket_progress_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

websocket_session_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

websocket_session_race_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

websocket_upgrade_decision_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

websocket_utf8_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

if NATIVE_V3_TLS
check_PROGRAMS += tls_acme_lifetime tls_acme_selection tls_acme_credentials tls_io tls_io_race tls_io_loopback tls_credentials tls_credentials_rotation tls_selection tls_mtls tls_psk_contract tls_psk tls_psk_runtime
tls_acme_lifetime_SOURCES = unit/tls_acme_lifetime_test.cpp
tls_acme_lifetime_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_acme_lifetime_LDADD = $(tls_io_LDADD)
tls_acme_lifetime_LDFLAGS = $(tls_io_LDFLAGS)
tls_acme_selection_SOURCES = unit/tls_acme_selection_test.cpp
tls_acme_selection_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_acme_selection_LDADD = $(tls_io_LDADD)
tls_acme_selection_LDFLAGS = $(tls_io_LDFLAGS)
tls_acme_credentials_SOURCES = unit/tls_acme_credentials_test.cpp
tls_acme_credentials_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_acme_credentials_LDADD = $(tls_io_LDADD)
tls_acme_credentials_LDFLAGS = $(tls_io_LDFLAGS)
tls_io_SOURCES = unit/tls_io_test.cpp
tls_io_race_SOURCES = unit/tls_io_race_test.cpp
tls_io_loopback_SOURCES = unit/tls_io_loopback_test.cpp
tls_io_CPPFLAGS = $(AM_CPPFLAGS) -DTLS_TEST_DIR='"$(top_srcdir)/test"'
tls_io_race_CPPFLAGS = $(tls_io_CPPFLAGS)
tls_io_loopback_CPPFLAGS = $(V3_TLS_CFLAGS) $(tls_io_CPPFLAGS)
tls_io_LDADD = $(top_builddir)/src/libhttpserver_v3core.la $(V3_TLS_LIBS)
tls_io_race_LDADD = $(tls_io_LDADD)
tls_io_loopback_LDADD = $(tls_io_LDADD)
tls_io_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
tls_io_race_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
tls_io_loopback_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
tls_credentials_SOURCES = unit/tls_credentials_test.cpp
tls_credentials_CPPFLAGS = $(tls_io_CPPFLAGS)
tls_credentials_LDADD = $(tls_io_LDADD)
tls_credentials_LDFLAGS = $(tls_io_LDFLAGS)
tls_psk_SOURCES = unit/tls_psk_test.cpp
tls_psk_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_psk_LDADD = $(tls_io_LDADD)
tls_psk_LDFLAGS = $(tls_io_LDFLAGS)
tls_psk_runtime_SOURCES = unit/tls_psk_runtime_test.cpp
tls_psk_runtime_CPPFLAGS = $(tls_io_CPPFLAGS)
tls_psk_runtime_LDADD = $(tls_io_LDADD)
tls_psk_runtime_LDFLAGS = $(tls_io_LDFLAGS)
tls_psk_contract_SOURCES = unit/tls_psk_contract_test.cpp
tls_psk_contract_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_psk_contract_LDADD = $(tls_io_LDADD)
tls_psk_contract_LDFLAGS = $(tls_io_LDFLAGS)
tls_mtls_SOURCES = unit/tls_mtls_test.cpp
tls_mtls_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_mtls_LDADD = $(tls_io_LDADD)
tls_mtls_LDFLAGS = $(tls_io_LDFLAGS)
tls_selection_SOURCES = unit/tls_selection_test.cpp
tls_selection_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_selection_LDADD = $(tls_io_LDADD)
tls_selection_LDFLAGS = $(tls_io_LDFLAGS)

tls_credentials_rotation_SOURCES = unit/tls_credentials_rotation_test.cpp
tls_credentials_rotation_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_credentials_rotation_LDADD = $(tls_io_LDADD)
tls_credentials_rotation_LDFLAGS = $(tls_io_LDFLAGS)
endif
noinst_HEADERS += unit/tls_acme_adapter.hpp unit/tls_acme_peer.hpp unit/tls_acme_fixture.hpp unit/tls_io_fixture.hpp unit/tls_credentials_fixture.hpp unit/tls_mtls_fixture.hpp unit/tls_psk_fixture.hpp

check_PROGRAMS += io_tls_control
io_tls_control_SOURCES = unit/io_tls_control_test.cpp
io_tls_control_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
io_tls_control_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

EXTRA_DIST += cert.pem key.pem

EXTRA_DIST += data/tls_credentials/a.pem data/tls_credentials/a-key.pem data/tls_credentials/b.pem data/tls_credentials/b-key.pem data/tls_credentials/root.pem data/tls_credentials/intermediate.pem

EXTRA_DIST += data/tls_credentials/client-expired-key.pem data/tls_credentials/client-expired.pem data/tls_credentials/client-intermediate.pem data/tls_credentials/client-no-cn-key.pem data/tls_credentials/client-no-cn.pem data/tls_credentials/client-other-key.pem data/tls_credentials/client-other-root.pem data/tls_credentials/client-other.pem data/tls_credentials/client-root.pem data/tls_credentials/client-valid-key.pem data/tls_credentials/client-valid.pem data/tls_credentials/client-wrong-purpose-key.pem data/tls_credentials/client-wrong-purpose.pem

check_PROGRAMS += hpack_primitives hpack_static_table hpack_corpus
hpack_primitives_SOURCES = unit/hpack_primitives_test.cpp
hpack_primitives_LDADD =
hpack_static_table_SOURCES = unit/hpack_static_table_test.cpp
hpack_static_table_LDADD =
hpack_corpus_SOURCES = unit/hpack_corpus_test.cpp fuzz/hpack_fuzz.cpp
hpack_corpus_LDADD =
EXTRA_DIST += data/hpack/rfc7541.hpp fuzz/hpack_fuzz.hpp fuzz/README-hpack.md

check_PROGRAMS += hpack_dynamic_table hpack_field_section hpack_connection
hpack_dynamic_table_SOURCES = unit/hpack_dynamic_table_test.cpp
hpack_dynamic_table_LDADD =
hpack_field_section_SOURCES = unit/hpack_field_section_test.cpp
hpack_field_section_LDADD =
hpack_connection_SOURCES = unit/hpack_connection_test.cpp
hpack_connection_LDADD =
