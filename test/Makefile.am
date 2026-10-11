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

AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver/ -DHTTPSERVER_COMPILATION
METASOURCES = AUTO
noinst_PROGRAMS = native_websocket_fixture

check_PROGRAMS = io_completion_storage io_iocp_backend io_kqueue_backend io_epoll_backend external_http1_e2e external_readiness_adapter consumer_v3_readiness readiness_contract consumer_v3_executor websocket_drain websocket_drain_race task_resume_signal_lifetime http1_websocket_upgrade http1_websocket_handshake websocket_progress websocket_upgrade_decision websocket_codec websocket_utf8 websocket_session websocket_session_race consumer_v3_websocket native_http1_parity littletest_skip_semantics digest_client_self_test transcript_parser response_frame normalize http_semantic_outcome http_semantic_method http_semantic_fields http_semantic_request_head consumer_v3_semantic v3_header_hygiene task_executor task_core task_cancellation task_resume_signal task_race task_stop_inside_handler consumer_v3_concurrency io_operation io_connection_owner fake_io_backend io_backend_contract server_options_validate server_budgets server_routes consumer_v3_server http_status exchange_decisions exchange_routing consumer_v3_exchange body_reader consumer_v3_body_reader response_writer consumer_v3_response_writer http1_parser http1_body_mode http1_body_decoder http1_body_source http1_response_mode http1_response_framer http1_response_outbox io_poll_sys_listener worker_pool connection_engine http1_error_synth drain_scope native_server consumer_v3_native_server native_http1_e2e route_sync consumer_v3_route_sync response_definition consumer_v3_response_definition response_sources consumer_v3_response_sources v3_native_linkage base64_vector sha1_vector md5_vector sha256_vector secure_compare entropy_sys digest_nonce_ledger digest_params digest_response_vector digest_auth_policy basic_auth_policy consumer_v3_auth basic_auth_corpus digest_auth_corpus urlencoded_decoder forms_urlencoded forms_urlencoded_route forms_urlencoded_corpus consumer_v3_forms multipart_decoder forms_multipart forms_multipart_route forms_multipart_files forms_multipart_corpus consumer_v3_multipart consumer_v3_multipart_runtime server_routes_families server_hooks request_lifecycle consumer_v3_hooks routing_corpus hooks_corpus net_address consumer_v3_net_address peer_policy consumer_v3_peer_policy ip_controls_corpus

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

consumer_v3_multipart_runtime_SOURCES = headers/consumer_v3_multipart_runtime.cpp
consumer_v3_multipart_runtime_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_multipart_runtime_LDADD = $(top_builddir)/src/libhttpserver.la

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

check_PROGRAMS += behavior_replay
behavior_replay_SOURCES = unit/behavior_replay_test.cpp parity/case_wire.cpp parity/transcript.cpp
behavior_replay_CPPFLAGS = $(AM_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"'
behavior_replay_LDADD = $(top_builddir)/src/libhttpserver.la
noinst_HEADERS += parity/behavior_replay.hpp
noinst_HEADERS += parity/v3_behavior_profiles.hpp
noinst_HEADERS += parity/behavior_observation.hpp
EXTRA_DIST += parity/task179-capabilities.json integ/behavior_parity_runner_test.py
TESTS += integ/behavior_parity_runner_test.py

check_PROGRAMS += v3_behavior_parity
v3_behavior_parity_SOURCES = unit/v3_behavior_parity_test.cpp parity/transcript.cpp parity/case_wire.cpp parity/response_frame.cpp parity/normalize.cpp
v3_behavior_parity_CPPFLAGS = $(AM_CPPFLAGS) $(V3_TLS_CFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"' -DPARITY_DATA_ROOT='"$(top_srcdir)/test"'
v3_behavior_parity_LDADD = $(top_builddir)/src/libhttpserver_v3core.la $(V3_TLS_LIBS)
v3_behavior_parity_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

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
native_websocket_fixture_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/examples
EXTRA_DIST += integ/native_websocket_regression.py
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
check_PROGRAMS += native_http1_tls_parity
native_http1_tls_parity_SOURCES = integ/native_http1_tls_parity_test.cpp \
  parity/transcript.cpp parity/response_frame.cpp parity/normalize.cpp parity/case_wire.cpp
native_http1_tls_parity_CPPFLAGS = $(tls_io_loopback_CPPFLAGS) -DPARITY_TRANSCRIPT_DIR='"$(top_srcdir)/test/parity/transcripts"' -DPARITY_DATA_ROOT='"$(top_srcdir)/test"'
native_http1_tls_parity_LDADD = $(tls_io_LDADD)
native_http1_tls_parity_LDFLAGS = $(tls_io_LDFLAGS)
check_PROGRAMS += tls_policy_concurrency tls_policy_hostile_profiles
tls_policy_concurrency_SOURCES = unit/tls_policy_concurrency_test.cpp
tls_policy_concurrency_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_policy_concurrency_LDADD = $(tls_io_LDADD)
tls_policy_concurrency_LDFLAGS = $(tls_io_LDFLAGS)
tls_policy_hostile_profiles_SOURCES = unit/tls_policy_hostile_profiles_test.cpp
tls_policy_hostile_profiles_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
tls_policy_hostile_profiles_LDADD = $(tls_io_LDADD)
tls_policy_hostile_profiles_LDFLAGS = $(tls_io_LDFLAGS)
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
noinst_HEADERS += unit/tls_policy_tcp_fixture.hpp unit/tls_acme_adapter.hpp unit/tls_acme_peer.hpp unit/tls_acme_fixture.hpp unit/tls_io_fixture.hpp unit/tls_credentials_fixture.hpp unit/tls_mtls_fixture.hpp unit/tls_psk_fixture.hpp

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
hpack_corpus_CPPFLAGS = $(AM_CPPFLAGS) -DHPACK_SEED_DIR='"$(top_srcdir)/test/data/hpack/seeds"'
hpack_corpus_LDADD =
EXTRA_DIST += data/hpack/rfc7541.hpp fuzz/hpack_fuzz.hpp fuzz/README-hpack.md

check_PROGRAMS += hpack_dynamic_table hpack_field_section hpack_connection
hpack_dynamic_table_SOURCES = unit/hpack_dynamic_table_test.cpp
hpack_dynamic_table_LDADD =
hpack_field_section_SOURCES = unit/hpack_field_section_test.cpp
hpack_field_section_LDADD =
hpack_connection_SOURCES = unit/hpack_connection_test.cpp
hpack_connection_LDADD =

check_PROGRAMS += http2_frame http2_settings http2_connection
http2_frame_SOURCES = unit/http2_frame_test.cpp
http2_settings_SOURCES = unit/http2_settings_test.cpp
http2_connection_SOURCES = unit/http2_connection_test.cpp
http2_frame_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
http2_settings_LDADD = $(http2_frame_LDADD)
http2_connection_LDADD = $(http2_frame_LDADD)
noinst_HEADERS += unit/http2_fixture.hpp
if NATIVE_V3_TLS
check_PROGRAMS += http2_tls_boundary
http2_tls_boundary_SOURCES = unit/http2_tls_boundary_test.cpp
http2_tls_boundary_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
http2_tls_boundary_LDADD = $(tls_io_LDADD)
http2_tls_boundary_LDFLAGS = $(tls_io_LDFLAGS)
endif

check_PROGRAMS += http2_conformance
http2_conformance_SOURCES = unit/http2_conformance_test.cpp
http2_conformance_CPPFLAGS = $(AM_CPPFLAGS) -DHTTP2_CORPUS_DIR='"$(top_srcdir)/test/conformance/http2"'
http2_conformance_LDADD = $(http2_frame_LDADD)
EXTRA_DIST += conformance/http2/README.md conformance/http2/ack-payload.wire conformance/http2/bad-magic.wire conformance/http2/bad-ping-length.wire conformance/http2/bad-settings-length.wire conformance/http2/cases.tsv conformance/http2/continuation.wire conformance/http2/control-flood.wire conformance/http2/empty-settings.wire conformance/http2/interleaved.wire conformance/http2/invalid-then-valid.wire conformance/http2/local-ack.wire conformance/http2/orphan-continuation.wire conformance/http2/oversized.wire conformance/http2/ping-ack.wire conformance/http2/ping-stream.wire conformance/http2/ping.wire conformance/http2/priority-stream.wire conformance/http2/push.wire conformance/http2/settings-order.wire conformance/http2/settings-stream.wire conformance/http2/truncated-header.wire conformance/http2/truncated-magic.wire conformance/http2/truncated-payload.wire conformance/http2/unfinished-block.wire conformance/http2/unknown.wire conformance/http2/window-overflow.wire conformance/http2/wrong-first.wire conformance/http2/zero-connection-window.wire conformance/http2/zero-stream-window.wire

check_PROGRAMS += http2_headers http2_exchange
http2_headers_SOURCES = unit/http2_headers_test.cpp
http2_exchange_SOURCES = unit/http2_exchange_test.cpp
http2_headers_LDADD = $(http2_frame_LDADD)
http2_exchange_LDADD = $(http2_frame_LDADD)
noinst_HEADERS += unit/http2_request_fixture.hpp

check_PROGRAMS += http2_dispatch http2_native_owner
http2_dispatch_SOURCES = unit/http2_dispatch_test.cpp
http2_dispatch_LDADD = $(http2_frame_LDADD)
http2_native_owner_SOURCES = unit/http2_native_owner_test.cpp
http2_native_owner_LDADD = $(http2_frame_LDADD)
check_PROGRAMS += http2_streaming
http2_streaming_SOURCES = unit/http2_streaming_test.cpp
http2_streaming_LDADD = $(http2_frame_LDADD)
check_PROGRAMS += http2_flow_control
http2_flow_control_SOURCES = unit/http2_flow_control_test.cpp
http2_flow_control_LDADD = $(http2_frame_LDADD)

check_PROGRAMS += http2_fair_output http2_reset http2_rate_budget
http2_fair_output_SOURCES = unit/http2_fair_output_test.cpp
http2_reset_SOURCES = unit/http2_reset_test.cpp
http2_rate_budget_SOURCES = unit/http2_rate_budget_test.cpp
http2_fair_output_LDADD = $(http2_frame_LDADD)
http2_reset_LDADD = $(http2_frame_LDADD)
http2_rate_budget_LDADD = $(http2_frame_LDADD)

check_PROGRAMS += http2_drain
http2_drain_SOURCES = unit/http2_drain_test.cpp
http2_drain_LDADD = $(http2_frame_LDADD)

check_PROGRAMS += http2_websocket_handshake
http2_websocket_handshake_SOURCES = unit/http2_websocket_handshake_test.cpp
http2_websocket_handshake_LDADD = $(http2_frame_LDADD)
noinst_HEADERS += unit/http2_websocket_fixture.hpp
check_PROGRAMS += http2_websocket
http2_websocket_SOURCES = unit/http2_websocket_test.cpp
http2_websocket_LDADD = $(http2_frame_LDADD)
check_PROGRAMS += http2_websocket_flow
http2_websocket_flow_SOURCES = unit/http2_websocket_flow_test.cpp
http2_websocket_flow_LDADD = $(http2_frame_LDADD)
check_PROGRAMS += http2_websocket_lifecycle
http2_websocket_lifecycle_SOURCES = unit/http2_websocket_lifecycle_test.cpp
http2_websocket_lifecycle_LDADD = $(http2_frame_LDADD)

check_PROGRAMS += http2_engine_conformance
http2_engine_conformance_SOURCES = unit/http2_engine_conformance_test.cpp
http2_engine_conformance_CPPFLAGS = $(AM_CPPFLAGS) -DHTTP2_ENGINE_CORPUS_DIR='"$(top_srcdir)/test/conformance/http2-engine"'
http2_engine_conformance_LDADD = $(http2_frame_LDADD)
EXTRA_DIST += conformance/http2-engine/authority-host.wire
EXTRA_DIST += conformance/http2-engine/bad-value.wire
EXTRA_DIST += conformance/http2-engine/cases.tsv
EXTRA_DIST += conformance/http2-engine/closed-data.wire
EXTRA_DIST += conformance/http2-engine/closed-reset.wire
EXTRA_DIST += conformance/http2-engine/connect-duplicate-protocol.wire
EXTRA_DIST += conformance/http2-engine/connect-not-advertised.wire
EXTRA_DIST += conformance/http2-engine/connect-missing-authority.wire
EXTRA_DIST += conformance/http2-engine/connection-field.wire
EXTRA_DIST += conformance/http2-engine/connection-window-overflow.wire
EXTRA_DIST += conformance/http2-engine/content-length-data.wire
EXTRA_DIST += conformance/http2-engine/content-length-end.wire
EXTRA_DIST += conformance/http2-engine/discarded-dynamic-context.wire
EXTRA_DIST += conformance/http2-engine/duplicate-method.wire
EXTRA_DIST += conformance/http2-engine/eof-field-block.wire
EXTRA_DIST += conformance/http2-engine/even-head.wire
EXTRA_DIST += conformance/http2-engine/head-priority-self.wire
EXTRA_DIST += conformance/http2-engine/huffman-eos.wire
EXTRA_DIST += conformance/http2-engine/huffman-padding.wire
EXTRA_DIST += conformance/http2-engine/idle-data.wire
EXTRA_DIST += conformance/http2-engine/idle-reset.wire
EXTRA_DIST += conformance/http2-engine/index-oob.wire
EXTRA_DIST += conformance/http2-engine/index-zero.wire
EXTRA_DIST += conformance/http2-engine/interleaved-continuation.wire
EXTRA_DIST += conformance/http2-engine/invalid-field.wire
EXTRA_DIST += conformance/http2-engine/invalid-te.wire
EXTRA_DIST += conformance/http2-engine/late-table-update.wire
EXTRA_DIST += conformance/http2-engine/malformed-trailer.wire
EXTRA_DIST += conformance/http2-engine/missing-method.wire
EXTRA_DIST += conformance/http2-engine/missing-path.wire
EXTRA_DIST += conformance/http2-engine/missing-scheme.wire
EXTRA_DIST += conformance/http2-engine/padded-data.wire
EXTRA_DIST += conformance/http2-engine/priority-self.wire
EXTRA_DIST += conformance/http2-engine/priority-short.wire
EXTRA_DIST += conformance/http2-engine/priority-zero.wire
EXTRA_DIST += conformance/http2-engine/proxy-connection.wire
EXTRA_DIST += conformance/http2-engine/pseudo-after-regular.wire
EXTRA_DIST += conformance/http2-engine/stream-window-overflow.wire
EXTRA_DIST += conformance/http2-engine/table-update-overflow.wire
EXTRA_DIST += conformance/http2-engine/transfer-encoding.wire
EXTRA_DIST += conformance/http2-engine/truncated-integer.wire
EXTRA_DIST += conformance/http2-engine/truncated-literal.wire
EXTRA_DIST += conformance/http2-engine/upgrade-field.wire
EXTRA_DIST += conformance/http2-engine/uppercase-field.wire
EXTRA_DIST += conformance/http2-engine/valid-te.wire
EXTRA_DIST += conformance/http2-engine/zero-head.wire

if NATIVE_V3_TLS
noinst_PROGRAMS += http2_tls_fixture
http2_tls_fixture_SOURCES = integ/http2_tls_fixture.cpp
http2_tls_fixture_CPPFLAGS = $(V3_TLS_CFLAGS) $(AM_CPPFLAGS) -DTLS_TEST_DIR='"$(abs_top_srcdir)/test"' -DPARITY_DATA_ROOT='"$(abs_top_srcdir)/test"'
http2_tls_fixture_LDADD = $(tls_io_LDADD)
http2_tls_fixture_LDFLAGS = $(tls_io_LDFLAGS)
endif

check_PROGRAMS += http2_fuzz_replay
http2_fuzz_replay_SOURCES = unit/http2_fuzz_replay_test.cpp fuzz/http2_engine_fuzz.cpp
http2_fuzz_replay_CPPFLAGS = $(http2_engine_conformance_CPPFLAGS)
http2_fuzz_replay_LDADD = $(http2_frame_LDADD)
EXTRA_DIST += fuzz/http2_engine_fuzz.hpp integ/http2_client_h2.py integ/http2_client_node.mjs integ/http2_gate_runner_test.py integ/http2-client-requirements.txt conformance/http2/h2spec-exclusions.tsv
EXTRA_DIST += data/hpack/seeds/huffman-eos.seed
EXTRA_DIST += data/hpack/seeds/huffman-long-padding.seed
EXTRA_DIST += data/hpack/seeds/huffman-zero-padding.seed
EXTRA_DIST += data/hpack/seeds/index-oob.seed
EXTRA_DIST += data/hpack/seeds/index-zero.seed
EXTRA_DIST += data/hpack/seeds/integer-overflow.seed
EXTRA_DIST += data/hpack/seeds/integer-truncated.seed
EXTRA_DIST += data/hpack/seeds/late-table-update.seed
EXTRA_DIST += data/hpack/seeds/literal-truncated.seed
EXTRA_DIST += data/hpack/seeds/minimum-final-update.seed
EXTRA_DIST += data/hpack/seeds/missing-minimum-update.seed
EXTRA_DIST += data/hpack/seeds/oversized-table-update.seed
EXTRA_DIST += data/hpack/seeds/rfc-huffman.seed
EXTRA_DIST += data/hpack/seeds/rfc-request.seed
EXTRA_DIST += conformance/http2-engine/README.md data/hpack/seeds/README.md
EXTRA_DIST += conformance/http2-engine/peer-reset-data.wire conformance/http2-engine/peer-reset-head.wire conformance/http2-engine/closed-head.wire conformance/http2-engine/implicit-closed-head.wire conformance/http2-engine/half-closed-head.wire
EXTRA_DIST += conformance/http2-engine/peer-reset-invalid-hpack.wire conformance/http2-engine/closed-invalid-hpack.wire

# IOCP uses native-core linkage; staging lifetime is tested on every platform.
io_completion_storage_SOURCES = unit/io_completion_storage_test.cpp
io_completion_storage_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
io_completion_storage_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
io_iocp_backend_SOURCES = unit/io_iocp_backend_test.cpp
io_iocp_backend_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
io_iocp_backend_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

EXTRA_DIST += installed/v3_package_consumer.cpp installed/v3-package-matrix.json

check_PROGRAMS += io_datagram io_udp_backend quic_invariant_header quic_datagram_dispatch
io_datagram_SOURCES = unit/io_datagram_test.cpp
io_datagram_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
io_datagram_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
io_udp_backend_SOURCES = unit/io_udp_backend_test.cpp
io_udp_backend_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
io_udp_backend_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
quic_invariant_header_SOURCES = unit/quic_invariant_header_test.cpp
quic_invariant_header_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_invariant_header_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
quic_datagram_dispatch_SOURCES = unit/quic_datagram_dispatch_test.cpp
quic_datagram_dispatch_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_datagram_dispatch_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

noinst_HEADERS += unit/io_udp_backend_contract.hpp

check_PROGRAMS += quic_network_harness
quic_network_harness_SOURCES = unit/quic_network_harness_test.cpp support/quic_network_harness.cpp
quic_network_harness_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/test
quic_network_harness_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_network_harness_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
EXTRA_DIST += support/quic_network_harness.hpp
check_PROGRAMS += quic_fuzz_replay
quic_fuzz_replay_SOURCES = unit/quic_fuzz_replay_test.cpp fuzz/quic_parser_fuzz.cpp fuzz/quic_state_fuzz.cpp support/quic_network_harness.cpp
quic_fuzz_replay_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/test -DQUIC_SEED_DIR='"$(abs_top_srcdir)/test/data/quic"'
quic_fuzz_replay_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_fuzz_replay_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
EXTRA_DIST += fuzz/quic_parser_fuzz.hpp fuzz/quic_state_fuzz.hpp
EXTRA_DIST += data/quic/parser/README.md
EXTRA_DIST += data/quic/parser/empty.seed
EXTRA_DIST += data/quic/parser/long.seed
EXTRA_DIST += data/quic/parser/max-cids.seed
EXTRA_DIST += data/quic/parser/missing-fixed.seed
EXTRA_DIST += data/quic/parser/oversized.seed
EXTRA_DIST += data/quic/parser/short.seed
EXTRA_DIST += data/quic/parser/truncated.seed
EXTRA_DIST += data/quic/parser/version-zero.seed
EXTRA_DIST += data/quic/state/README.md
EXTRA_DIST += data/quic/state/empty.seed
EXTRA_DIST += data/quic/state/invalid.seed
EXTRA_DIST += data/quic/state/network.seed
EXTRA_DIST += data/quic/state/teardown.seed
EXTRA_DIST += data/quic/state/timers.seed
EXTRA_DIST += data/quic/state/truncated.seed
EXTRA_DIST += data/quic/state/loss-reorder.seed data/quic/parser/truncated-source.seed

check_PROGRAMS += quic_varint
quic_varint_SOURCES = unit/quic_varint_test.cpp
quic_varint_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_varint_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
EXTRA_DIST += unit/quic_codec_test_support.hpp

check_PROGRAMS += quic_packet
quic_packet_SOURCES = unit/quic_packet_test.cpp
quic_packet_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_packet_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += quic_frame
quic_frame_SOURCES = unit/quic_frame_test.cpp
quic_frame_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_frame_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += quic_transport_parameters
quic_transport_parameters_SOURCES = unit/quic_transport_parameters_test.cpp
quic_transport_parameters_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_transport_parameters_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += quic_codec_allocation
quic_codec_allocation_SOURCES = unit/quic_codec_allocation_test.cpp
quic_codec_allocation_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_codec_allocation_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

EXTRA_DIST += data/quic/parser/codec-initial.seed
EXTRA_DIST += data/quic/parser/codec-ack.seed
EXTRA_DIST += data/quic/parser/codec-stream.seed
EXTRA_DIST += data/quic/parser/codec-parameters.seed
EXTRA_DIST += data/quic/parser/codec-huge-length.seed
EXTRA_DIST += data/quic/parser/codec-ack-underflow.seed
EXTRA_DIST += data/quic/parser/codec-duplicate-parameters.seed
EXTRA_DIST += data/quic/parser/codec-nonminimal-frame.seed
EXTRA_DIST += data/quic/parser/codec-scalar-truncation.seed

if NATIVE_V3_TLS
check_PROGRAMS += quic_crypto
quic_crypto_SOURCES = unit/quic_crypto_test.cpp
quic_crypto_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
quic_crypto_LDADD = $(tls_io_LDADD)
quic_crypto_LDFLAGS = $(tls_io_LDFLAGS)
endif
EXTRA_DIST += unit/quic_crypto_test_support.hpp

if NATIVE_V3_TLS
check_PROGRAMS += quic_key_state
quic_key_state_SOURCES = unit/quic_key_state_test.cpp
quic_key_state_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
quic_key_state_LDADD = $(tls_io_LDADD)
quic_key_state_LDFLAGS = $(tls_io_LDFLAGS)
endif

check_PROGRAMS += quic_amplification
quic_amplification_SOURCES = unit/quic_amplification_test.cpp
quic_amplification_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_amplification_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

if NATIVE_V3_TLS
check_PROGRAMS += quic_address_token
quic_address_token_SOURCES = unit/quic_address_token_test.cpp
quic_address_token_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
quic_address_token_LDADD = $(tls_io_LDADD)
quic_address_token_LDFLAGS = $(tls_io_LDFLAGS)
check_PROGRAMS += quic_server_admission
quic_server_admission_SOURCES = unit/quic_server_admission_test.cpp
quic_server_admission_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
quic_server_admission_LDADD = $(tls_io_LDADD)
quic_server_admission_LDFLAGS = $(tls_io_LDFLAGS)
endif

check_PROGRAMS += quic_stream_state quic_reassembly
quic_stream_state_SOURCES = unit/quic_stream_state_test.cpp
quic_stream_state_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_stream_state_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
quic_reassembly_SOURCES = unit/quic_reassembly_test.cpp
quic_reassembly_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_reassembly_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

if NATIVE_V3_TLS
check_PROGRAMS += quic_tls_session
quic_tls_session_SOURCES = unit/quic_tls_session_test.cpp
quic_tls_session_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
quic_tls_session_LDADD = $(tls_io_LDADD)
quic_tls_session_LDFLAGS = $(tls_io_LDFLAGS)
check_PROGRAMS += quic_tls_handshake
quic_tls_handshake_SOURCES = unit/quic_tls_handshake_test.cpp
quic_tls_handshake_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
quic_tls_handshake_LDADD = $(tls_io_LDADD)
quic_tls_handshake_LDFLAGS = $(tls_io_LDFLAGS)
endif
EXTRA_DIST += unit/quic_tls_peer.hpp

check_PROGRAMS += quic_ack_state quic_recovery quic_repacketize
quic_ack_state_SOURCES = unit/quic_ack_state_test.cpp
quic_ack_state_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_ack_state_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
quic_recovery_SOURCES = unit/quic_recovery_test.cpp support/quic_network_harness.cpp
quic_recovery_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_recovery_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
quic_repacketize_SOURCES = unit/quic_repacketize_test.cpp
quic_repacketize_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_repacketize_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
if NATIVE_V3_TLS
check_PROGRAMS += quic_recovery_tls
quic_recovery_tls_SOURCES = unit/quic_recovery_tls_test.cpp
quic_recovery_tls_CPPFLAGS = $(tls_io_loopback_CPPFLAGS)
quic_recovery_tls_LDADD = $(tls_io_LDADD)
quic_recovery_tls_LDFLAGS = $(tls_io_LDFLAGS)
endif

check_PROGRAMS += quic_flow_control quic_flow_recovery quic_flow_storage
quic_flow_control_SOURCES = unit/quic_flow_control_test.cpp
quic_flow_control_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_flow_control_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
quic_flow_recovery_SOURCES = unit/quic_flow_recovery_test.cpp
quic_flow_recovery_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_flow_recovery_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
quic_flow_storage_SOURCES = unit/quic_flow_storage_test.cpp
quic_flow_storage_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_flow_storage_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += quic_congestion quic_pacing quic_send_schedule
quic_congestion_SOURCES = unit/quic_congestion_test.cpp
quic_congestion_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_congestion_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
quic_pacing_SOURCES = unit/quic_pacing_test.cpp
quic_pacing_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_pacing_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
quic_send_schedule_SOURCES = unit/quic_send_schedule_test.cpp
quic_send_schedule_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_send_schedule_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += qpack_static_table qpack_primitives qpack_field_section
qpack_static_table_SOURCES = unit/qpack_static_table_test.cpp
qpack_static_table_LDADD =
qpack_primitives_SOURCES = unit/qpack_primitives_test.cpp
qpack_primitives_LDADD =
qpack_field_section_SOURCES = unit/qpack_field_section_test.cpp
qpack_field_section_LDADD =
EXTRA_DIST += data/qpack/rfc9204.hpp

check_PROGRAMS += http3_frame http3_settings http3_connection
http3_frame_SOURCES = unit/http3_frame_test.cpp
http3_frame_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
http3_frame_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
http3_settings_SOURCES = unit/http3_settings_test.cpp
http3_settings_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
http3_settings_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
http3_connection_SOURCES = unit/http3_connection_test.cpp
http3_connection_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
http3_connection_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
EXTRA_DIST += unit/http3_fixture.hpp

check_PROGRAMS += http3_headers
http3_headers_SOURCES = unit/http3_headers_test.cpp
http3_headers_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
http3_headers_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
check_PROGRAMS += http3_exchange http3_streaming http3_cancellation
http3_exchange_SOURCES = unit/http3_exchange_test.cpp parity/response_frame.cpp
http3_exchange_LDADD = $(http3_headers_LDADD)
http3_exchange_LDFLAGS = $(http3_headers_LDFLAGS)
http3_streaming_SOURCES = unit/http3_streaming_test.cpp
http3_streaming_LDADD = $(http3_headers_LDADD)
http3_streaming_LDFLAGS = $(http3_headers_LDFLAGS)
http3_cancellation_SOURCES = unit/http3_cancellation_test.cpp parity/response_frame.cpp
http3_cancellation_LDADD = $(http3_headers_LDADD)
http3_cancellation_LDFLAGS = $(http3_headers_LDFLAGS)
EXTRA_DIST += unit/http3_request_fixture.hpp

EXTRA_DIST += unit/http3_route_parity_fixture.hpp

if NATIVE_V3_TLS
check_PROGRAMS += http3_network_owner quic_native_owner
quic_native_owner_SOURCES = unit/quic_native_owner_test.cpp
quic_native_owner_CPPFLAGS = $(tls_io_loopback_CPPFLAGS) -I$(top_srcdir)
quic_native_owner_LDADD = $(tls_io_LDADD)
quic_native_owner_LDFLAGS = $(tls_io_LDFLAGS)
http3_network_owner_SOURCES = unit/http3_network_owner_test.cpp support/http3_network_owner.cpp support/http3_network_lifecycle.cpp support/quic_interop_files.cpp
http3_network_owner_CPPFLAGS = $(tls_io_loopback_CPPFLAGS) -I$(top_srcdir)
http3_network_owner_LDADD = $(tls_io_LDADD)
http3_network_owner_LDFLAGS = $(tls_io_LDFLAGS)
endif
if NATIVE_V3_TLS
noinst_PROGRAMS += http3_udp_fixture
http3_udp_fixture_SOURCES = integ/http3_udp_fixture.cpp support/http3_network_owner.cpp support/http3_network_lifecycle.cpp support/quic_interop_files.cpp
http3_udp_fixture_CPPFLAGS = $(http3_network_owner_CPPFLAGS) -DPARITY_DATA_ROOT='"$(abs_top_srcdir)/test"'
http3_udp_fixture_LDADD = $(tls_io_LDADD)
http3_udp_fixture_LDFLAGS = $(tls_io_LDFLAGS)
endif
EXTRA_DIST += support/http3_network_owner.hpp integ/http3_client_aioquic.py integ/http3-client-requirements.txt integ/http3-client-go/go.mod integ/http3-client-go/go.sum

EXTRA_DIST += integ/http3-client-go/main.go integ/http3_gate_runner_test.py

if NATIVE_V3_TLS
check_PROGRAMS += quic_interop_tls
quic_interop_tls_SOURCES = unit/quic_interop_tls_test.cpp support/http3_network_owner.cpp support/http3_network_lifecycle.cpp support/quic_interop_files.cpp
quic_interop_tls_CPPFLAGS = $(http3_network_owner_CPPFLAGS)
quic_interop_tls_LDADD = libquic_interop_provider.a $(top_builddir)/src/.libs/libhttpserver_v3core.a $(V3_TLS_LIBS) -lpthread -ldl
quic_interop_tls_LDFLAGS = $(tls_io_LDFLAGS)
endif
check_PROGRAMS += quic_interop_files
quic_interop_files_SOURCES = unit/quic_interop_files_test.cpp support/quic_interop_files.cpp
quic_interop_files_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)
quic_interop_files_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_interop_files_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
EXTRA_DIST += support/quic_interop_files.hpp

if NATIVE_V3_TLS
noinst_LIBRARIES = libquic_interop_provider.a
libquic_interop_provider_a_SOURCES = $(top_srcdir)/src/detail/tls_session.cpp $(top_srcdir)/src/detail/quic_tls_session.cpp
libquic_interop_provider_a_CPPFLAGS = $(http3_network_owner_CPPFLAGS) -DLIBHTTPSERVER_QUIC_INTEROP_TEST_PROVIDER
endif

if NATIVE_V3_TLS
noinst_PROGRAMS += quic_interop_endpoint
quic_interop_endpoint_SOURCES = integ/quic_interop_endpoint.cpp support/http3_network_owner.cpp support/http3_network_lifecycle.cpp support/quic_interop_files.cpp
quic_interop_endpoint_CPPFLAGS = $(http3_network_owner_CPPFLAGS)
quic_interop_endpoint_LDADD = $(quic_interop_tls_LDADD)
quic_interop_endpoint_LDFLAGS = $(tls_io_LDFLAGS)
endif
EXTRA_DIST += interop/quic/run_endpoint.sh integ/quic_interop_runner_test.py
EXTRA_DIST += interop/quic/Dockerfile interop/quic/Dockerfile.dockerignore
EXTRA_DIST += interop/quic/Dockerfile.native interop/quic/Dockerfile.native.dockerignore

check_PROGRAMS += quic_connection_identity
quic_connection_identity_SOURCES = unit/quic_connection_identity_test.cpp
quic_connection_identity_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_connection_identity_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

EXTRA_DIST += unit/quic_owner_peer.hpp

check_PROGRAMS += quic_path_mtu
quic_path_mtu_SOURCES = unit/quic_path_mtu_test.cpp support/quic_network_harness.cpp
quic_path_mtu_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_path_mtu_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
check_PROGRAMS += quic_datagram_sizing
quic_datagram_sizing_SOURCES = unit/quic_datagram_sizing_test.cpp
quic_datagram_sizing_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_datagram_sizing_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += quic_connection_lifecycle
quic_connection_lifecycle_SOURCES = unit/quic_connection_lifecycle_test.cpp
quic_connection_lifecycle_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_connection_lifecycle_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += qpack_dynamic_table qpack_instructions qpack_dynamic_corpus
qpack_dynamic_table_SOURCES = unit/qpack_dynamic_table_test.cpp
qpack_dynamic_table_LDADD =
qpack_instructions_SOURCES = unit/qpack_instructions_test.cpp
qpack_instructions_LDADD =
qpack_dynamic_corpus_SOURCES = unit/qpack_dynamic_corpus_test.cpp
qpack_dynamic_corpus_LDADD =

check_PROGRAMS += http3_qpack
http3_qpack_SOURCES = unit/http3_qpack_test.cpp
http3_qpack_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
http3_qpack_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += qpack_accounting
qpack_accounting_SOURCES = unit/qpack_accounting_test.cpp
qpack_accounting_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
qpack_accounting_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += http3_qpack_scheduling
http3_qpack_scheduling_SOURCES = unit/http3_qpack_scheduling_test.cpp
http3_qpack_scheduling_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
http3_qpack_scheduling_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += http3_drain
http3_drain_SOURCES = unit/http3_drain_test.cpp
http3_drain_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
http3_drain_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += http3_websocket_handshake
http3_websocket_handshake_SOURCES = unit/http3_websocket_handshake_test.cpp
http3_websocket_handshake_LDADD = $(http3_headers_LDADD)
http3_websocket_handshake_LDFLAGS = $(http3_headers_LDFLAGS)
EXTRA_DIST += unit/http3_websocket_fixture.hpp

check_PROGRAMS += http3_websocket
http3_websocket_SOURCES = unit/http3_websocket_test.cpp
http3_websocket_LDADD = $(http3_headers_LDADD)
http3_websocket_LDFLAGS = $(http3_headers_LDFLAGS)

check_PROGRAMS += http3_websocket_flow
http3_websocket_flow_SOURCES = unit/http3_websocket_flow_test.cpp
http3_websocket_flow_LDADD = $(http3_headers_LDADD)
http3_websocket_flow_LDFLAGS = $(http3_headers_LDFLAGS)

check_PROGRAMS += http3_websocket_lifecycle
http3_websocket_lifecycle_SOURCES = unit/http3_websocket_lifecycle_test.cpp
http3_websocket_lifecycle_LDADD = $(http3_headers_LDADD)
http3_websocket_lifecycle_LDFLAGS = $(http3_headers_LDFLAGS)

EXTRA_DIST += interop/quic/task173-matrix.json

noinst_PROGRAMS += http3_semantic_parity_fixture
http3_semantic_parity_fixture_SOURCES = integ/http3_semantic_parity_fixture.cpp parity/response_frame.cpp
http3_semantic_parity_fixture_LDADD = $(http3_headers_LDADD)
http3_semantic_parity_fixture_LDFLAGS = $(http3_headers_LDFLAGS)
EXTRA_DIST += support/http3_semantic_routes.hpp integ/http3_semantic_aioquic.py integ/http3-client-go/semantic.go integ/http3-client-go/semantic_test.go integ/http3-client-go/public_test.go integ/http3-client-go/public_websocket.go

check_PROGRAMS += http3_semantic_h1
http3_semantic_h1_SOURCES = unit/http3_semantic_h1_test.cpp parity/response_frame.cpp
http3_semantic_h1_LDADD =
EXTRA_DIST += support/http3_semantic_h1.hpp

check_PROGRAMS += http3_websocket_harness
http3_websocket_harness_SOURCES = unit/http3_websocket_harness_test.cpp
http3_websocket_harness_LDADD = $(http3_headers_LDADD)
http3_websocket_harness_LDFLAGS = $(http3_headers_LDFLAGS)
EXTRA_DIST += support/http3_websocket_routes.hpp integ/http3_websocket_wire.py integ/http3_websocket_client_test.py integ/http3_websocket_aioquic.py integ/http3_websocket_gate_runner_test.py

# TASK-176: real recovery event sequences remain provider-free in TLS-off builds.
check_PROGRAMS += quic_recovery_fuzz_replay
quic_recovery_fuzz_replay_SOURCES = unit/quic_recovery_fuzz_replay_test.cpp fuzz/quic_recovery_fuzz.cpp
quic_recovery_fuzz_replay_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/test -DQUIC_RECOVERY_SEED_DIR='"$(abs_top_srcdir)/test/data/quic/recovery"'
quic_recovery_fuzz_replay_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
quic_recovery_fuzz_replay_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
EXTRA_DIST += fuzz/quic_event_fuzz.hpp fuzz/quic_recovery_fuzz.hpp unit/quic_event_replay.hpp

if NATIVE_V3_TLS
check_PROGRAMS += quic_tls_callbacks_fuzz_replay
quic_tls_callbacks_fuzz_replay_SOURCES = unit/quic_tls_callbacks_fuzz_replay_test.cpp fuzz/quic_tls_callbacks_fuzz.cpp support/http3_network_owner.cpp support/http3_network_lifecycle.cpp support/quic_interop_files.cpp
quic_tls_callbacks_fuzz_replay_CPPFLAGS = $(tls_io_loopback_CPPFLAGS) -I$(top_srcdir) -I$(top_srcdir)/test -DQUIC_CALLBACK_SEED_DIR='"$(abs_top_srcdir)/test/data/quic/callbacks"'
quic_tls_callbacks_fuzz_replay_LDADD = $(tls_io_LDADD)
quic_tls_callbacks_fuzz_replay_LDFLAGS = $(tls_io_LDFLAGS)
endif
EXTRA_DIST += fuzz/quic_tls_callbacks_fuzz.hpp data/quic/recovery/README.md data/quic/callbacks/README.md
EXTRA_DIST += data/quic/recovery/ack-loss-reorder.seed data/quic/recovery/path-pending-close.seed data/quic/recovery/pto-drain-reset.seed data/quic/recovery/reordered-crypto.seed data/quic/recovery/spaces-batch-flow.seed
EXTRA_DIST += data/quic/callbacks/all-levels-suites.seed data/quic/callbacks/backpressure.seed data/quic/callbacks/budget-dispatch-secret.seed data/quic/callbacks/chacha-transitions.seed data/quic/callbacks/close-held-lease.seed data/quic/callbacks/failure-pending-close.seed data/quic/callbacks/invalid-release.seed data/quic/callbacks/lease-transition.seed data/quic/callbacks/params-repetition.seed

check_PROGRAMS += qpack_fuzz_replay http3_fuzz_replay
qpack_fuzz_replay_SOURCES = unit/qpack_fuzz_replay_test.cpp fuzz/qpack_fuzz.cpp
qpack_fuzz_replay_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/test -DQPACK_SEED_DIR='"$(abs_top_srcdir)/test/data/qpack/seeds"'
qpack_fuzz_replay_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
qpack_fuzz_replay_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
http3_fuzz_replay_SOURCES = unit/http3_fuzz_replay_test.cpp fuzz/http3_engine_fuzz.cpp
http3_fuzz_replay_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/test -DHTTP3_SEED_DIR='"$(abs_top_srcdir)/test/data/http3"'
http3_fuzz_replay_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
http3_fuzz_replay_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
EXTRA_DIST += fuzz/qpack_fuzz.hpp fuzz/http3_engine_fuzz.hpp
EXTRA_DIST += data/http3/README.md data/qpack/seeds/README.md
EXTRA_DIST += data/http3/held-data.seed data/http3/held-data-fin.seed data/http3/held-data-reset.seed data/http3/near-input-cap-data.seed
EXTRA_DIST += data/qpack/seeds/blocked.seed
EXTRA_DIST += data/qpack/seeds/decoder-instructions.seed
EXTRA_DIST += data/qpack/seeds/dynamic.seed
EXTRA_DIST += data/qpack/seeds/exact-expanded.seed
EXTRA_DIST += data/qpack/seeds/instructions-duplicate.seed
EXTRA_DIST += data/qpack/seeds/integer-overflow.seed
EXTRA_DIST += data/qpack/seeds/integer-truncated.seed
EXTRA_DIST += data/qpack/seeds/invalid-huffman-eos.seed
EXTRA_DIST += data/qpack/seeds/invalid-huffman-padding.seed
EXTRA_DIST += data/qpack/seeds/invalid-post-base.seed
EXTRA_DIST += data/qpack/seeds/invalid-relative.seed
EXTRA_DIST += data/qpack/seeds/limit-compressed.seed
EXTRA_DIST += data/qpack/seeds/limit-expanded.seed
EXTRA_DIST += data/qpack/seeds/limit-fields.seed
EXTRA_DIST += data/qpack/seeds/literal-path.seed
EXTRA_DIST += data/qpack/seeds/never-indexed.seed
EXTRA_DIST += data/qpack/seeds/post-base.seed
EXTRA_DIST += data/qpack/seeds/rfc-authority.seed
EXTRA_DIST += data/qpack/seeds/static.seed
EXTRA_DIST += data/qpack/seeds/zero-table.seed
EXTRA_DIST += data/http3/bad-decoder.seed
EXTRA_DIST += data/http3/bad-encoder.seed
EXTRA_DIST += data/http3/bad-qpack.seed
EXTRA_DIST += data/http3/blocked-disabled.seed
EXTRA_DIST += data/http3/blocked-overflow.seed
EXTRA_DIST += data/http3/blocked-unblock.seed
EXTRA_DIST += data/http3/cancel-late.seed
EXTRA_DIST += data/http3/critical-eof.seed
EXTRA_DIST += data/http3/critical-reset.seed
EXTRA_DIST += data/http3/data-chunks.seed
EXTRA_DIST += data/http3/duplicate-critical.seed
EXTRA_DIST += data/http3/encoder-first.seed
EXTRA_DIST += data/http3/held-release.seed
EXTRA_DIST += data/http3/instructions-limit.seed
EXTRA_DIST += data/http3/invalid-offset.seed
EXTRA_DIST += data/http3/invalid-role.seed
EXTRA_DIST += data/http3/limit-1.seed
EXTRA_DIST += data/http3/limit-12.seed
EXTRA_DIST += data/http3/limit-13.seed
EXTRA_DIST += data/http3/limit-15.seed
EXTRA_DIST += data/http3/limit-2.seed
EXTRA_DIST += data/http3/limit-3.seed
EXTRA_DIST += data/http3/limit-8.seed
EXTRA_DIST += data/http3/missing-settings.seed
EXTRA_DIST += data/http3/outgoing-cancel.seed
EXTRA_DIST += data/http3/outgoing-publish.seed
EXTRA_DIST += data/http3/output-refusal.seed
EXTRA_DIST += data/http3/partial-cancel.seed
EXTRA_DIST += data/http3/partial-feedback.seed
EXTRA_DIST += data/http3/repeated-settings.seed
EXTRA_DIST += data/http3/retained-exact.seed
EXTRA_DIST += data/http3/retained-over.seed
EXTRA_DIST += data/http3/settings-duplicate.seed
EXTRA_DIST += data/http3/settings-invalid.seed
EXTRA_DIST += data/http3/settings-static.seed
EXTRA_DIST += data/http3/shutdown-blocked.seed
EXTRA_DIST += data/http3/siblings.seed
EXTRA_DIST += data/http3/table-wrap-evict.seed
EXTRA_DIST += data/http3/truncated.seed
EXTRA_DIST += data/http3/unexpected.seed
EXTRA_DIST += data/http3/unknown-frame.seed
EXTRA_DIST += data/http3/varint-2.seed
EXTRA_DIST += data/http3/varint-4.seed
EXTRA_DIST += data/http3/varint-8.seed

# The aggregate contract is dependency-free; independent network/fuzz lanes are opt-in.
TESTS += integ/http3_full_gate_runner_test.py
TEST_EXTENSIONS = .py
PY_LOG_COMPILER = python3
EXTRA_DIST += integ/http3_full_gate_runner_test.py conformance/http3/full-gates.json
EXTRA_DIST += integ/behavior_parity_h2.py
EXTRA_DIST += integ/behavior_parity_h3.py
EXTRA_DIST += integ/behavior_parity_client_test.py

check_PROGRAMS += resource_limits
resource_limits_SOURCES = unit/resource_limits_test.cpp
resource_limits_LDADD = $(http3_headers_LDADD)
resource_limits_LDFLAGS = $(http3_headers_LDFLAGS)

TESTS += ../scripts/lib/resource_limits_probe_test.py

check_PROGRAMS += server_diagnostics diagnostic_dispatch consumer_v3_diagnostics
server_diagnostics_SOURCES = unit/server_diagnostics_test.cpp
server_diagnostics_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
diagnostic_dispatch_SOURCES = unit/diagnostic_dispatch_test.cpp
diagnostic_dispatch_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
consumer_v3_diagnostics_SOURCES = headers/consumer_v3_diagnostics.cpp
consumer_v3_diagnostics_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_diagnostics_LDADD = $(top_builddir)/src/libhttpserver_v3core.la

EXTRA_DIST += unit/package_backend_witness.hpp

check_PROGRAMS += nonce_expiry_wait
nonce_expiry_wait_SOURCES = unit/nonce_expiry_wait_test.cpp integ/nonce_expiry_wait.hpp
nonce_expiry_wait_CPPFLAGS = -I$(top_srcdir)/test
nonce_expiry_wait_LDADD =

check_PROGRAMS += v3_examples
v3_examples_SOURCES = unit/v3_examples_test.cpp
v3_examples_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
v3_examples_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
EXTRA_DIST += integ/v3_examples_test.py
if BUILD_EXAMPLES
TESTS += integ/v3_examples_test.py
endif

if NATIVE_V3_TLS
check_PROGRAMS += v3_tls_rotation
v3_tls_rotation_SOURCES = examples/v3_tls_rotation.cpp
v3_tls_rotation_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/examples
v3_tls_rotation_LDADD = $(tls_io_LDADD)
v3_tls_rotation_LDFLAGS = $(tls_io_LDFLAGS)
endif

# Native fixture dependencies must accompany their translation units in distributions.
noinst_HEADERS += integ/raw_http_client.hpp parity/case_wire.hpp parity/v3_dispatch_replay.hpp unit/response_source_rig.hpp

check_PROGRAMS += consumer_v3_peer_policy_runtime
consumer_v3_peer_policy_runtime_SOURCES = integ/consumer_v3_peer_policy_runtime.cpp
consumer_v3_peer_policy_runtime_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/examples
consumer_v3_peer_policy_runtime_LDADD = $(top_builddir)/src/libhttpserver_v3core.la
consumer_v3_peer_policy_runtime_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += http_cookie consumer_v3_cookie
TESTS += http_cookie consumer_v3_cookie
http_cookie_SOURCES = unit/http_cookie_test.cpp
http_cookie_LDADD = $(top_builddir)/src/libhttpserver.la $(V3_TLS_LIBS)
http_cookie_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
consumer_v3_cookie_SOURCES = headers/consumer_v3_cookie.cpp
consumer_v3_cookie_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_cookie_LDADD = $(top_builddir)/src/libhttpserver.la $(V3_TLS_LIBS)
consumer_v3_cookie_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += consumer_v3_cookie_wire
TESTS += consumer_v3_cookie_wire
consumer_v3_cookie_wire_SOURCES = integ/consumer_v3_cookie_wire.cpp
consumer_v3_cookie_wire_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/test -I$(top_srcdir)/examples
consumer_v3_cookie_wire_LDADD = $(top_builddir)/src/libhttpserver.la $(V3_TLS_LIBS)
consumer_v3_cookie_wire_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

check_PROGRAMS += consumer_v3_tls_credentials
consumer_v3_tls_credentials_SOURCES = headers/consumer_v3_tls_credentials.cpp
consumer_v3_tls_credentials_CPPFLAGS = -I$(top_srcdir)/src
consumer_v3_tls_credentials_LDADD = $(top_builddir)/src/libhttpserver.la

if NATIVE_V3_TLS
check_PROGRAMS += public_tls_profiles
public_tls_profiles_SOURCES = unit/public_tls_profiles_test.cpp
TESTS += ../scripts/test-v3-public-http2.py
public_tls_profiles_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/test -DTLS_TEST_DIR='"$(abs_top_srcdir)/test"'
public_tls_profiles_LDADD = $(top_builddir)/src/libhttpserver.la
public_tls_profiles_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
endif

TESTS += ../scripts/test-v3-public-http3.py

check_PROGRAMS += http3_native_stream
http3_native_stream_SOURCES = unit/http3_native_stream_test.cpp
http3_native_stream_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/test -I$(top_srcdir) -DHTTPSERVER_COMPILATION
http3_native_stream_LDADD = $(top_builddir)/src/libhttpserver_v3core.la $(V3_TLS_LIBS)
http3_native_stream_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)

EXTRA_DIST += installed/v3_http3_semantics.hpp installed/v3_http3_semantics.py
EXTRA_DIST += installed/v3_http3_credentials.py
EXTRA_DIST += installed/v3_http3_lifecycle.py
EXTRA_DIST += installed/v3_http3_qpack.py installed/v3_http3_flow.py
