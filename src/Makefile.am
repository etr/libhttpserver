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

AM_CPPFLAGS = -I../ -I$(srcdir)/httpserver/ -DHTTPSERVER_COMPILATION
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver.la
# TASK-034 / PRD-FLG-REQ-001: websocket_handler.cpp is built and
# websocket_handler.hpp is installed unconditionally so the public
# surface is identical across HAVE_WEBSOCKET-on and HAVE_WEBSOCKET-off
# builds. The WS-off branch in websocket_handler.cpp provides stub
# definitions (every member throws feature_unavailable except is_valid()
# which returns false).
# TASK-108 step 10 (PRD-V3N-REQ-001/002): the v3 native engine as a
# convenience library. Its objects fold into libhttpserver.la below (the
# shipped .la is unchanged) while staying linkable alone: the
# v3_native_linkage check program links ONLY this archive and the audit
# script proves the native TLS-off surface needs no microhttpd, gnutls,
# openssl, or wslay.
noinst_LTLIBRARIES = libhttpserver_v3core.la
libhttpserver_v3core_la_SOURCES = detail/features.cpp detail/io_operation.cpp detail/io_udp_backend.cpp detail/quic_invariant_header.cpp detail/quic_datagram_dispatch.cpp detail/io_connection_owner.cpp detail/fake_io_backend.cpp detail/io_poll_backend.cpp detail/io_epoll_backend.cpp detail/io_kqueue_backend.cpp detail/io_iocp_backend.cpp detail/io_managed_backend.cpp detail/io_managed_socket_backend.cpp detail/io_external_adapter.cpp detail/io_wake_source.cpp detail/worker_pool.cpp detail/connection_engine.cpp detail/connection_engine_request.cpp detail/listener_engine.cpp detail/drain_scope.cpp detail/server.cpp detail/auth_basic.cpp detail/auth_digest.cpp detail/forms_urlencoded.cpp detail/forms_multipart.cpp detail/forms_multipart_files.cpp detail/server_hooks.cpp detail/request_lifecycle.cpp detail/net_address.cpp detail/peer_policy.cpp detail/websocket_codec.cpp detail/websocket_driver.cpp detail/websocket_session_state.cpp detail/websocket_session.cpp detail/http1_websocket_handshake.cpp detail/connection_engine_websocket.cpp

libhttpserver_la_SOURCES = string_utilities.cpp webserver.cpp http_utils.cpp file_info.cpp http_request.cpp http_request_auth.cpp http_response.cpp http_response_factories.cpp http_resource.cpp create_webserver.cpp create_test_request.cpp websocket_handler.cpp hook_handle.cpp peer_address.cpp resource_hook_table.cpp cookie.cpp detail/http_endpoint.cpp detail/response_body.cpp detail/ip_representation.cpp detail/ip_access_control.cpp detail/ws_registry.cpp detail/hook_bus.cpp detail/route_table.cpp detail/daemon_lifecycle.cpp detail/dispatch_util.cpp detail/error_pages.cpp detail/hook_dispatcher.cpp detail/http_request_impl.cpp detail/http_request_impl_args.cpp detail/http_request_impl_tls.cpp detail/request_dispatcher.cpp detail/request_pipeline.cpp detail/response_materializer.cpp detail/upload_pipeline.cpp detail/websocket_upgrader.cpp detail/webserver_impl.cpp detail/webserver_callbacks.cpp detail/webserver_body_pipeline.cpp
# noinst_HEADERS: shipped in the tarball but NEVER installed under $prefix/include.
# Detail headers (httpserver/detail/*.hpp) live here so they cannot leak to
# downstream consumers — the public surface comes in through <httpserver.hpp>.
noinst_HEADERS = httpserver/string_utilities.hpp httpserver/detail/connection_context.hpp httpserver/detail/http_endpoint.hpp httpserver/detail/response_body.hpp httpserver/detail/webserver_impl.hpp httpserver/detail/webserver_impl_dispatch.hpp httpserver/detail/connection_state.hpp httpserver/detail/ip_access_control.hpp httpserver/detail/ws_registry.hpp httpserver/detail/hook_bus.hpp httpserver/detail/route_table.hpp httpserver/detail/daemon_lifecycle.hpp httpserver/detail/dispatch_util.hpp httpserver/detail/error_pages.hpp httpserver/detail/hook_dispatcher.hpp httpserver/detail/request_dispatcher.hpp httpserver/detail/request_pipeline.hpp httpserver/detail/response_materializer.hpp httpserver/detail/upload_pipeline.hpp httpserver/detail/websocket_upgrader.hpp httpserver/detail/secure_zero.hpp httpserver/detail/http_request_impl.hpp httpserver/detail/resource_hook_table.hpp httpserver/detail/route_entry.hpp httpserver/detail/lambda_resource.hpp httpserver/detail/segment_trie.hpp httpserver/detail/route_cache.hpp httpserver/detail/route_tier.hpp httpserver/detail/unescape_helpers.hpp httpserver/detail/http_field_validation.hpp httpserver/detail/exchange_runner.hpp httpserver/detail/method_utils.hpp httpserver/detail/path_normalize.hpp httpserver/detail/io_operation.hpp httpserver/detail/io_connection_owner.hpp httpserver/detail/fake_io_backend.hpp httpserver/detail/io_poll_backend.hpp httpserver/detail/io_epoll_backend.hpp httpserver/detail/io_kqueue_backend.hpp httpserver/detail/io_iocp_backend.hpp httpserver/detail/io_completion_storage.hpp httpserver/detail/io_socket_backend.hpp httpserver/detail/io_managed_backend.hpp httpserver/detail/io_managed_socket_backend.hpp httpserver/detail/io_poll_sys.hpp httpserver/detail/io_timer_sweep.hpp httpserver/detail/worker_pool.hpp httpserver/detail/connection_engine.hpp httpserver/detail/listener_engine.hpp httpserver/detail/drain_scope.hpp httpserver/detail/http1_error_synth.hpp httpserver/detail/digest_hex.hpp httpserver/detail/digest_nonce.hpp httpserver/detail/digest_ledger.hpp httpserver/detail/digest_params.hpp httpserver/detail/digest_response.hpp httpserver/detail/forms_urlencoded.hpp httpserver/detail/forms_multipart.hpp httpserver/detail/forms_verdict.hpp httpserver/detail/auth_text.hpp httpserver/detail/lifecycle_sink.hpp httpserver/detail/request_lifecycle.hpp httpserver/detail/http1_websocket_handshake.hpp httpserver/detail/websocket_driver.hpp httpserver/detail/websocket_utf8.hpp httpserver/detail/websocket_codec.hpp httpserver/detail/websocket_session_state.hpp gettext.h
nobase_include_HEADERS = httpserver.hpp httpserver/body_kind.hpp httpserver/cookie.hpp httpserver/constants.hpp httpserver/create_webserver.hpp httpserver/create_webserver_setters.hpp httpserver/create_test_request.hpp httpserver/webserver.hpp httpserver/webserver_routes.hpp httpserver/webserver_runtime.hpp httpserver/webserver_websocket.hpp httpserver/webserver_hooks.hpp httpserver/websocket_handler.hpp httpserver/http_utils.hpp httpserver/http_utils_helpers.hpp httpserver/ip_representation.hpp httpserver/file_info.hpp httpserver/http_request.hpp httpserver/http_response.hpp httpserver/http_resource.hpp httpserver/feature_unavailable.hpp httpserver/iovec_entry.hpp httpserver/http_arg_value.hpp httpserver/http_method.hpp httpserver/hook_phase.hpp httpserver/hook_action.hpp httpserver/hook_handle.hpp httpserver/hook_context.hpp httpserver/http.hpp httpserver/http/outcome.hpp httpserver/http/protocol.hpp httpserver/http/method.hpp httpserver/http/fields.hpp httpserver/http/request_head.hpp httpserver/concurrency/executor.hpp httpserver/concurrency/task.hpp httpserver/concurrency/cancellation.hpp httpserver/concurrency/resume_signal.hpp httpserver/concurrency/concurrency.hpp httpserver/server/budgets.hpp httpserver/server/options.hpp httpserver/server/tls_peer_metadata.hpp httpserver/server/routes.hpp httpserver/server/configuration.hpp httpserver/server/server.hpp httpserver/server/readiness.hpp httpserver/http/status.hpp httpserver/exchange.hpp httpserver/body_reader.hpp httpserver/response_writer.hpp httpserver/response_sources.hpp httpserver/response_definition.hpp httpserver/server/route_sync.hpp httpserver/auth/basic_auth.hpp httpserver/auth/digest_auth.hpp httpserver/forms/urlencoded.hpp httpserver/forms/multipart.hpp httpserver/server/hooks.hpp httpserver/net/address.hpp httpserver/server/peer_policy.hpp httpserver/websocket/message.hpp httpserver/websocket/options.hpp httpserver/websocket/session.hpp

AM_CXXFLAGS += -fPIC -Wall

if COND_GCOV
AM_CFLAGS += -O0 --coverage --no-inline
AM_CXXFLAGS += -O0 --coverage --no-inline
AM_LDFLAGS += -O0 --coverage -lgcov --no-inline
endif

if !COND_CROSS_COMPILE
libhttpserver_la_LIBADD = libhttpserver_v3core.la -lmicrohttpd
if HAVE_WEBSOCKET
libhttpserver_la_LIBADD += -lmicrohttpd_ws
endif
# When TLS is enabled the library calls gnutls_* directly (http_request_impl_tls.cpp),
# so it must link gnutls. Linux/macOS resolve it transitively through libmicrohttpd,
# but mingw does not export those symbols from the microhttpd import lib -- link it
# explicitly so the shared library builds on every platform.
if HAVE_GNUTLS
libhttpserver_la_LIBADD += -lgnutls
endif
endif

libhttpserver_la_CFLAGS = $(AM_CFLAGS)
libhttpserver_la_CXXFLAGS = $(AM_CXXFLAGS)
# -version-number is scoped to the shared library link step only (not global
# LDFLAGS, which would affect executables and test runners unnecessarily).
libhttpserver_la_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS) -no-undefined -version-number @LHT_LDF_VERSION@

install-data-hook:
	@# Install an `<httpserverpp>` alias next to `<httpserver.hpp>`. On POSIX
	@# this is a symlink. On MSYS2/mingw `ln -s` typically falls back to a
	@# file copy (it produces a regular file, not a link); when even that
	@# fails (e.g. tightened sandboxes, or `ln` missing the `-s` flag in
	@# pristine mingw shells) we fall back to an explicit `cp`. The
	@# downstream `check-install-layout` rule accepts either form.
	mkdir -p $(DESTDIR)$(includedir)
	cd $(DESTDIR)$(includedir) && \
	    rm -f httpserverpp && \
	    { $(LN_S) httpserver.hpp httpserverpp || cp httpserver.hpp httpserverpp; }

uninstall-hook:
	(cd $(DESTDIR)$(includedir) && rm -f httpserverpp)

noinst_HEADERS += httpserver/detail/http1_host.hpp

# Backend discovery flags belong only to this private provider translation unit.
libhttpserver_v3core_la_LIBADD =
if NATIVE_V3_TLS
noinst_LTLIBRARIES += libhttpserver_v3tls.la
libhttpserver_v3tls_la_SOURCES = detail/tls_provider.cpp detail/tls_session.cpp detail/tls_io_backend.cpp detail/tls_credentials.cpp detail/tls_psk.cpp detail/tls_psk_runtime.cpp detail/tls_psk_attempt.cpp detail/quic_key_derivation.cpp detail/quic_packet_crypto.cpp detail/quic_retry_integrity.cpp detail/quic_key_state.cpp
libhttpserver_v3tls_la_CPPFLAGS = $(V3_TLS_CFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS)
libhttpserver_v3tls_la_LDFLAGS = $(V3_TLS_LDFLAGS)
libhttpserver_v3tls_la_LIBADD = $(V3_TLS_LIBS)
libhttpserver_v3core_la_LIBADD += libhttpserver_v3tls.la
endif
noinst_HEADERS += httpserver/detail/tls_build_probe.hpp
nobase_include_HEADERS += httpserver/features.hpp

noinst_HEADERS += httpserver/detail/tls_io_backend.hpp httpserver/detail/tls_session.hpp httpserver/detail/tls_credentials.hpp httpserver/detail/tls_psk.hpp httpserver/detail/tls_psk_runtime.hpp httpserver/detail/tls_psk_attempt.hpp

# TASK-137: HPACK primitives are private, header-only native helpers.
noinst_HEADERS += httpserver/detail/hpack_primitives.hpp httpserver/detail/hpack_static_table.hpp httpserver/detail/hpack_huffman_table.hpp

noinst_HEADERS += httpserver/detail/hpack_dynamic_table.hpp httpserver/detail/hpack_field_section.hpp \
                  httpserver/detail/hpack_decoder.hpp httpserver/detail/hpack_encoder.hpp httpserver/detail/hpack_connection.hpp

noinst_HEADERS += httpserver/detail/http2_frame.hpp httpserver/detail/http2_connection.hpp
libhttpserver_v3core_la_SOURCES += detail/http2_frame.cpp detail/http2_connection.cpp

noinst_HEADERS += httpserver/detail/http2_request_engine.hpp
libhttpserver_v3core_la_SOURCES += detail/http2_request_engine.cpp
noinst_HEADERS += httpserver/detail/http2_request_head.hpp
libhttpserver_v3core_la_SOURCES += detail/http2_request_head.cpp

noinst_HEADERS += httpserver/detail/http2_flow_control.hpp httpserver/detail/http2_body_stream.hpp
libhttpserver_v3core_la_SOURCES += detail/http2_body_stream.cpp
noinst_HEADERS += httpserver/detail/http2_request_state.hpp
libhttpserver_v3core_la_SOURCES += detail/http2_response_stream.cpp detail/http2_stream_flow.cpp

noinst_HEADERS += httpserver/detail/websocket_handshake.hpp httpserver/detail/http2_websocket_handshake.hpp
libhttpserver_v3core_la_SOURCES += detail/websocket_handshake.cpp detail/http2_websocket_handshake.cpp detail/http2_websocket_stream.cpp

noinst_HEADERS += httpserver/detail/io_datagram.hpp httpserver/detail/io_udp_backend.hpp httpserver/detail/quic_invariant_header.hpp httpserver/detail/quic_datagram_dispatch.hpp

libhttpserver_v3core_la_SOURCES += detail/quic_varint.cpp
noinst_HEADERS += httpserver/detail/quic_codec.hpp httpserver/detail/quic_varint.hpp

libhttpserver_v3core_la_SOURCES += detail/quic_packet.cpp detail/quic_frame.cpp detail/quic_frame_write.cpp detail/quic_transport_parameters.cpp
noinst_HEADERS += httpserver/detail/quic_packet.hpp httpserver/detail/quic_frame.hpp httpserver/detail/quic_transport_parameters.hpp

noinst_HEADERS += httpserver/detail/quic_crypto.hpp
noinst_HEADERS += detail/quic_crypto_provider.hpp
noinst_HEADERS += httpserver/detail/quic_key_state.hpp

libhttpserver_v3core_la_SOURCES += detail/quic_amplification.cpp
noinst_HEADERS += httpserver/detail/quic_amplification.hpp

if NATIVE_V3_TLS
libhttpserver_v3tls_la_SOURCES += detail/quic_address_token.cpp detail/quic_server_admission.cpp
endif
noinst_HEADERS += httpserver/detail/quic_address_token.hpp httpserver/detail/quic_server_admission.hpp

# TASK-154: private stream lifecycle and bounded, owned receive ranges.
libhttpserver_v3core_la_SOURCES += detail/quic_stream_state.cpp detail/quic_reassembly.cpp
noinst_HEADERS += httpserver/detail/quic_stream_state.hpp httpserver/detail/quic_reassembly.hpp

if NATIVE_V3_TLS
libhttpserver_v3tls_la_SOURCES += detail/quic_tls_buffers.cpp detail/quic_tls_callbacks.cpp detail/quic_tls_session.cpp
endif
noinst_HEADERS += httpserver/detail/quic_tls_session.hpp detail/quic_tls_callbacks.hpp

libhttpserver_v3core_la_SOURCES += detail/quic_ack_state.cpp detail/quic_recovery.cpp detail/quic_recovery_timer.cpp detail/quic_repacketize.cpp
noinst_HEADERS += httpserver/detail/quic_recovery.hpp detail/quic_recovery_internal.hpp

# Connection-owned QUIC flow ledger and protected storage envelope.
libhttpserver_v3core_la_SOURCES += detail/quic_flow_control.cpp detail/quic_flow_receive.cpp detail/quic_flow_send.cpp detail/quic_storage.cpp detail/quic_recovery_flow.cpp
noinst_HEADERS += httpserver/detail/quic_flow_control.hpp httpserver/detail/quic_storage.hpp detail/quic_flow_internal.hpp
