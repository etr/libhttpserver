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
# Native objects form both the shipped library and the private linkage gate.
noinst_LTLIBRARIES = libhttpserver_v3core.la
libhttpserver_v3core_la_SOURCES = detail/tls_psk.cpp server/tls_psk.cpp server/tls_credentials.cpp features.cpp detail/io_operation.cpp detail/io_udp_backend.cpp detail/quic_invariant_header.cpp detail/quic_datagram_dispatch.cpp detail/io_connection_owner.cpp detail/fake_io_backend.cpp detail/io_poll_backend.cpp detail/io_epoll_backend.cpp detail/io_kqueue_backend.cpp detail/io_iocp_backend.cpp detail/io_managed_backend.cpp detail/io_managed_socket_backend.cpp detail/io_external_adapter.cpp detail/io_wake_source.cpp detail/worker_pool.cpp detail/connection_engine.cpp detail/connection_engine_request.cpp detail/listener_engine.cpp detail/drain_scope.cpp server/server.cpp auth/basic_auth.cpp auth/digest_auth.cpp forms/urlencoded.cpp forms/multipart.cpp forms/multipart_files.cpp server/hooks.cpp detail/request_lifecycle.cpp net/address.cpp detail/peer_policy.cpp detail/websocket_codec.cpp detail/websocket_driver.cpp detail/websocket_session_state.cpp websocket/session.cpp detail/http1_websocket_handshake.cpp detail/connection_engine_websocket.cpp

libhttpserver_la_SOURCES =
# Select the C++ driver for this object-only aggregate, including platforms
# where AM_LDFLAGS supplies no language runtime. EXTRA sources are not compiled.
EXTRA_libhttpserver_la_SOURCES = features.cpp
# noinst_HEADERS: shipped in the tarball but NEVER installed under $prefix/include.
# Detail headers (httpserver/detail/*.hpp) live here so they cannot leak to
# downstream consumers — the public surface comes in through <httpserver.hpp>.
noinst_HEADERS = httpserver/detail/secure_zero.hpp httpserver/detail/unescape_helpers.hpp httpserver/detail/exchange_runner.hpp httpserver/detail/io_operation.hpp httpserver/detail/io_connection_owner.hpp httpserver/detail/fake_io_backend.hpp httpserver/detail/io_poll_backend.hpp httpserver/detail/io_epoll_backend.hpp httpserver/detail/io_kqueue_backend.hpp httpserver/detail/io_iocp_backend.hpp httpserver/detail/io_completion_storage.hpp httpserver/detail/io_socket_backend.hpp httpserver/detail/io_managed_backend.hpp httpserver/detail/io_managed_socket_backend.hpp httpserver/detail/io_poll_sys.hpp httpserver/detail/io_timer_sweep.hpp httpserver/detail/worker_pool.hpp httpserver/detail/connection_engine.hpp httpserver/detail/listener_engine.hpp httpserver/detail/drain_scope.hpp httpserver/detail/http1_error_synth.hpp httpserver/detail/digest_hex.hpp httpserver/detail/digest_nonce.hpp httpserver/detail/digest_ledger.hpp httpserver/detail/digest_params.hpp httpserver/detail/digest_response.hpp httpserver/detail/forms_urlencoded.hpp httpserver/detail/forms_multipart.hpp httpserver/detail/forms_verdict.hpp httpserver/detail/auth_text.hpp httpserver/detail/lifecycle_sink.hpp httpserver/detail/request_lifecycle.hpp httpserver/detail/http1_websocket_handshake.hpp httpserver/detail/websocket_driver.hpp httpserver/detail/websocket_utf8.hpp httpserver/detail/websocket_codec.hpp httpserver/detail/websocket_session_state.hpp gettext.h
nobase_include_HEADERS = httpserver.hpp httpserver/http.hpp httpserver/http/outcome.hpp httpserver/http/protocol.hpp httpserver/http/method.hpp httpserver/http/fields.hpp httpserver/http/request_head.hpp httpserver/concurrency/executor.hpp httpserver/concurrency/task.hpp httpserver/concurrency/cancellation.hpp httpserver/concurrency/resume_signal.hpp httpserver/concurrency/concurrency.hpp httpserver/server/budgets.hpp httpserver/server/options.hpp httpserver/server/tls_peer_metadata.hpp httpserver/server/routes.hpp httpserver/server/configuration.hpp httpserver/server/server.hpp httpserver/server/readiness.hpp httpserver/http/status.hpp httpserver/exchange.hpp httpserver/body_reader.hpp httpserver/response_writer.hpp httpserver/response_sources.hpp httpserver/response_definition.hpp httpserver/server/route_sync.hpp httpserver/auth/basic_auth.hpp httpserver/auth/digest_auth.hpp httpserver/forms/urlencoded.hpp httpserver/forms/multipart.hpp httpserver/server/hooks.hpp httpserver/net/address.hpp httpserver/server/peer_policy.hpp httpserver/websocket/message.hpp httpserver/websocket/options.hpp httpserver/websocket/session.hpp

AM_CXXFLAGS += -fPIC -Wall

if COND_GCOV
AM_CFLAGS += -O0 --coverage --no-inline
AM_CXXFLAGS += -O0 --coverage --no-inline
AM_LDFLAGS += -O0 --coverage -lgcov --no-inline
endif

libhttpserver_la_LIBADD = libhttpserver_v3core.la

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
libhttpserver_v3core_la_LIBADD = $(LHT_LIBDEPS)
if NATIVE_V3_TLS
noinst_LTLIBRARIES += libhttpserver_v3tls.la
libhttpserver_v3tls_la_SOURCES = detail/tls_provider.cpp detail/tls_session.cpp detail/tls_io_backend.cpp detail/tls_socket_backend.cpp detail/tls_credentials.cpp detail/tls_psk_runtime.cpp detail/tls_psk_attempt.cpp detail/quic_key_derivation.cpp detail/quic_packet_crypto.cpp detail/quic_retry_integrity.cpp detail/quic_key_state.cpp
libhttpserver_v3tls_la_CPPFLAGS = $(V3_TLS_CFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS)
libhttpserver_v3tls_la_LDFLAGS = $(V3_TLS_LDFLAGS)
libhttpserver_v3tls_la_LIBADD = $(V3_TLS_LIBS)
libhttpserver_v3core_la_LIBADD += libhttpserver_v3tls.la
endif
noinst_HEADERS += httpserver/detail/tls_build_probe.hpp
nobase_include_HEADERS += httpserver/features.hpp

noinst_HEADERS += httpserver/detail/tls_socket_backend.hpp httpserver/detail/tls_io_backend.hpp httpserver/detail/tls_session.hpp httpserver/detail/tls_credentials.hpp httpserver/detail/tls_psk.hpp httpserver/detail/tls_psk_runtime.hpp httpserver/detail/tls_psk_attempt.hpp

# HPACK primitives and their compiled operations remain private.
noinst_HEADERS += httpserver/detail/hpack_primitives.hpp httpserver/detail/hpack_static_table.hpp httpserver/detail/hpack_huffman_table.hpp

noinst_HEADERS += httpserver/detail/hpack_dynamic_table.hpp httpserver/detail/hpack_field_section.hpp \
                  httpserver/detail/hpack_decoder.hpp httpserver/detail/hpack_encoder.hpp httpserver/detail/hpack_connection.hpp

noinst_HEADERS += httpserver/detail/http2_frame.hpp httpserver/detail/http2_connection.hpp
libhttpserver_v3core_la_SOURCES += detail/http2_frame.cpp detail/http2_connection.cpp

noinst_HEADERS += httpserver/detail/http2_request_engine.hpp
libhttpserver_v3core_la_SOURCES += detail/http2_request_engine.cpp detail/http2_native_stream.cpp detail/http2_native_timers.cpp detail/http2_tcp_owner.cpp
noinst_HEADERS += httpserver/detail/resource_scope.hpp \
	httpserver/detail/http2_dispatch.hpp httpserver/detail/http2_tcp_owner.hpp
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
noinst_HEADERS += httpserver/detail/quic_crypto_provider.hpp
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
noinst_HEADERS += httpserver/detail/quic_tls_session.hpp httpserver/detail/quic_tls_callbacks.hpp

libhttpserver_v3core_la_SOURCES += detail/quic_ack_state.cpp detail/quic_recovery.cpp detail/quic_recovery_timer.cpp detail/quic_repacketize.cpp
noinst_HEADERS += httpserver/detail/quic_recovery.hpp httpserver/detail/quic_recovery_internal.hpp

# Connection-owned QUIC flow ledger and protected storage envelope.
libhttpserver_v3core_la_SOURCES += detail/quic_flow_control.cpp detail/quic_flow_receive.cpp detail/quic_flow_send.cpp detail/quic_storage.cpp detail/quic_recovery_flow.cpp
noinst_HEADERS += httpserver/detail/quic_flow_control.hpp httpserver/detail/quic_storage.hpp httpserver/detail/quic_flow_internal.hpp

libhttpserver_v3core_la_SOURCES += detail/quic_congestion.cpp detail/quic_pacing.cpp detail/quic_send_schedule.cpp detail/quic_persistent_congestion.cpp detail/quic_recovery_emission.cpp
noinst_HEADERS += httpserver/detail/quic_congestion.hpp httpserver/detail/quic_pacing.hpp

# Static-only QPACK remains below semantic HTTP fields.
noinst_HEADERS += httpserver/detail/qpack_static_table.hpp httpserver/detail/qpack_primitives.hpp httpserver/detail/qpack_field_section.hpp httpserver/detail/qpack_decoder.hpp httpserver/detail/qpack_encoder.hpp

libhttpserver_v3core_la_SOURCES += detail/http3_frame.cpp detail/http3_connection.cpp detail/http3_settings.cpp
noinst_HEADERS += httpserver/detail/http3_frame.hpp httpserver/detail/http3_connection.hpp

# TASK-161: private HTTP/3 semantic request bridge.
libhttpserver_v3core_la_SOURCES += detail/http3_request_head.cpp
noinst_HEADERS += httpserver/detail/http3_request_head.hpp
libhttpserver_v3core_la_SOURCES += detail/http3_request_engine.cpp detail/http3_body_stream.cpp detail/http3_response_stream.cpp
noinst_HEADERS += httpserver/detail/http3_request_engine.hpp httpserver/detail/http3_request_state.hpp httpserver/detail/http3_body_stream.hpp
noinst_HEADERS += httpserver/detail/request_handler_frames.hpp httpserver/detail/multiplexed_request_head.hpp

libhttpserver_v3core_la_SOURCES += detail/quic_connection_identity.cpp
noinst_HEADERS += httpserver/detail/quic_connection_identity.hpp

libhttpserver_v3core_la_SOURCES += detail/quic_path_mtu.cpp
noinst_HEADERS += httpserver/detail/quic_path_mtu.hpp
libhttpserver_v3core_la_SOURCES += detail/quic_recovery_mtu.cpp
libhttpserver_v3core_la_SOURCES += detail/quic_datagram_sizing.cpp detail/quic_recovery_datagram.cpp
noinst_HEADERS += httpserver/detail/quic_datagram_sizing.hpp

libhttpserver_v3core_la_SOURCES += detail/quic_connection_lifecycle.cpp
noinst_HEADERS += httpserver/detail/quic_connection_lifecycle.hpp

noinst_HEADERS += httpserver/detail/qpack_dynamic_table.hpp httpserver/detail/qpack_instructions.hpp

libhttpserver_v3core_la_SOURCES += detail/http3_qpack.cpp

libhttpserver_v3core_la_SOURCES += detail/http3_control_drain.cpp
libhttpserver_v3core_la_SOURCES += detail/http3_drain.cpp

noinst_HEADERS += httpserver/detail/http3_websocket_handshake.hpp
libhttpserver_v3core_la_SOURCES += detail/http3_websocket_handshake.cpp detail/http3_websocket_handshake_stream.cpp

libhttpserver_v3core_la_SOURCES += detail/http3_websocket_stream.cpp

noinst_HEADERS += httpserver/detail/resource_charge.hpp

libhttpserver_v3core_la_SOURCES += server/diagnostics.cpp
nobase_include_HEADERS += httpserver/server/diagnostics.hpp
noinst_HEADERS += httpserver/detail/diagnostics_access.hpp

# Transitive native helpers must also ship in source distributions.
noinst_HEADERS += httpserver/detail/base64.hpp httpserver/detail/entropy_sys.hpp httpserver/detail/http1_body_decoder.hpp \
    httpserver/detail/http1_body_mode.hpp httpserver/detail/http1_body_source.hpp httpserver/detail/http1_head_limits.hpp \
    httpserver/detail/http1_head_parser.hpp httpserver/detail/http1_response_framer.hpp httpserver/detail/http1_response_mode.hpp \
    httpserver/detail/http1_response_outbox.hpp httpserver/detail/http1_target.hpp httpserver/detail/md5.hpp \
    httpserver/detail/secure_compare.hpp httpserver/detail/sha1.hpp httpserver/detail/sha256.hpp

libhttpserver_v3core_la_SOURCES += http/cookie.cpp
nobase_include_HEADERS += httpserver/http/cookie.hpp

nobase_include_HEADERS += httpserver/server/tls_credentials.hpp

noinst_HEADERS += httpserver/detail/tls_credentials_access.hpp

libhttpserver_v3core_la_SOURCES += detail/http3_native_stream.cpp detail/http3_semantic_stream.cpp
if NATIVE_V3_TLS
libhttpserver_v3tls_la_SOURCES += detail/quic_network_connection.cpp detail/quic_network_packets.cpp detail/quic_network_emission.cpp detail/quic_udp_listener.cpp
endif
noinst_HEADERS += httpserver/detail/quic_network_connection.hpp httpserver/detail/quic_udp_listener.hpp

# Compiled non-template operations, partitioned by their owning headers.
libhttpserver_v3core_la_SOURCES += http/method.cpp http/fields.cpp server/budgets.cpp server/route_sync.cpp server/routes.cpp server/options.cpp detail/net_address.cpp detail/diagnostics_access.cpp detail/tls_credentials_access.cpp detail/tls_psk_access.cpp detail/hook_bus.cpp
noinst_HEADERS += httpserver/detail/tls_psk_state.hpp httpserver/detail/net_address.hpp httpserver/detail/hook_bus.hpp httpserver/detail/tls_credentials_state.hpp httpserver/detail/diagnostics_state.hpp

# Compiled non-template operations, partitioned by their owning headers.
libhttpserver_v3core_la_SOURCES += response_definition.cpp body_reader.cpp exchange.cpp response_sources.cpp response_writer.cpp server/peer_policy.cpp detail/io_poll_sys.cpp detail/hpack_primitives.cpp detail/qpack_encoder.cpp detail/lifecycle_sink.cpp detail/hpack_decoder.cpp detail/qpack_decoder.cpp detail/qpack_instructions.cpp detail/sha1.cpp detail/secure_compare.cpp detail/http1_response_mode.cpp detail/digest_response.cpp detail/forms_verdict.cpp detail/resource_charge.cpp detail/hpack_encoder.cpp detail/websocket_utf8.cpp detail/quic_recovery_internal.cpp detail/request_handler_frames.cpp detail/http1_error_synth.cpp detail/md5.cpp detail/base64.cpp detail/sha256.cpp detail/http1_body_decoder.cpp detail/http1_body_mode.cpp detail/http1_response_outbox.cpp detail/exchange_runner.cpp detail/http1_response_framer.cpp detail/qpack_field_section.cpp detail/digest_ledger.cpp detail/entropy_sys.cpp detail/digest_nonce.cpp detail/http1_head_parser.cpp detail/http1_body_source.cpp detail/qpack_primitives.cpp detail/qpack_dynamic_table.cpp detail/forms_multipart.cpp detail/digest_hex.cpp detail/http2_request_state.cpp detail/io_timer_sweep.cpp detail/hpack_field_section.cpp detail/forms_urlencoded.cpp detail/auth_text.cpp detail/http1_target.cpp detail/hpack_dynamic_table.cpp detail/digest_params.cpp detail/http3_request_state.cpp detail/quic_flow_internal.cpp detail/multiplexed_request_head.cpp concurrency/cancellation.cpp concurrency/executor.cpp concurrency/task.cpp concurrency/resume_signal.cpp
noinst_HEADERS += httpserver/detail/drain_ticket_state.hpp httpserver/detail/peer_policy.hpp

# Compiled non-template operations, partitioned by their owning headers.
libhttpserver_v3core_la_SOURCES += websocket/options.cpp detail/unescape_helpers.cpp detail/http1_host.cpp detail/io_poll_backend_timers.cpp detail/http2_dispatch.cpp detail/connection_engine_config.cpp detail/io_completion_storage.cpp
