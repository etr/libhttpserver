# v3 source ownership

Public compiled operations live at `src/<logical>.cpp`, owned by installed
`src/httpserver/<logical>.hpp`. Private operations live at
`src/detail/<logical>.cpp`, owned by noninstalled
`src/httpserver/detail/<logical>.hpp`. Cohesive engine splits share their actual
owning header; no forwarding/dummy headers or recombined engines are needed.

This completed inventory covers every baseline tracked library `.cpp`/`.hpp`,
including the umbrella and directly stored private shared headers, and every
new library source/header. Ownership follows declarations, definitions and
their call sites, not namespace spelling alone. Installed template/representation
support in `detail` namespaces stays installed when needed; substantive bodies
are compiled. Normal library linkage supplies non-template behavior.

## Mixed ownership

- Credential and PSK handles are public; registry/lookup bridges and shared
  opaque state remain private. The existing callback thread-local identity is
  shared by the private lookup and public retirement code.
- Diagnostics dispatch/counters are public; recording/accounting is private.
  Hook handle/bus ownership is public; registration storage/snapshot firing is
  private behind one creation bridge.
- Address text parsing/formatting is public; byte normalization remains a
  private seam. Its installed support declaration is retained for compatibility.
- Peer policy wrappers are public; immutable generation publication and storage
  remain private. Only internal bridge declarations move to a private header.
- Multipart integration/sinks are public, with source-local adapter state;
  reusable decoding algorithms remain private. Drain-ticket construction stays
  private. Existing nested public opaque representations are unchanged.
- HTTP/2, HTTP/3 and QUIC splits retain their engine/recovery owners. The small
  connection configuration and polling timer units have explicit owners and
  avoid growing pre-existing large engine files.

## Manifest and verification mapping

All compiled rows are listed in `src/Makefile.am` in the native core or the
existing provider-conditioned target. Public headers use `nobase_include_HEADERS`;
private headers use `noinst_HEADERS`, so both ship while only public headers
install. The aggregate retains a C++ EXTRA source. `test/Makefile.am` supplies
normal linkage to behavioral consumers; compile-only/constexpr contracts remain.
Native audits include the final core source inventory; the direct fuzz script
instruments extracted dependencies too. The installed package runner compiles
the two-translation-unit operation consumer against staged pkg-config flags in
both shared/static and TLS-off/on forms.

The primary evidence is the existing complete local C++ suites in both TLS lanes,
normal examples, header/native-linkage/install gates, fresh distribution/package
consumers, affected corpus/fuzz replay and focused ASan/UBSan lifetime suites.
The compiled-operation contract checks library definitions; the consumer checks
cross-translation-unit task/executor/resume identity and ordinary public use.
Size/alignment/type-trait receipts compare selected representations to baseline.
Evidence is retained outside disposable worktrees under
`build/FEATURE-v3-source-layout/` in the integration workspace. BSD, macOS and
Windows execution belongs to CI/the v3 PR.

## Baseline file dispositions

| Baseline file | Final source/header destinations | Owning header(s) | Final disposition |
|---|---|---|---|
| `src/cookie.cpp` | `src/http/cookie.cpp` | `src/httpserver/http/cookie.hpp` | Relocated public implementation |
| `src/detail/auth_basic.cpp` | `src/auth/basic_auth.cpp` | `src/httpserver/auth/basic_auth.hpp` | Relocated public implementation |
| `src/detail/auth_digest.cpp` | `src/auth/digest_auth.cpp` | `src/httpserver/auth/digest_auth.hpp` | Relocated public implementation |
| `src/detail/connection_engine.cpp` | `src/detail/connection_engine.cpp` | `src/httpserver/detail/connection_engine.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/connection_engine_request.cpp` | `src/detail/connection_engine_request.cpp` | `src/httpserver/detail/connection_engine.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/connection_engine_websocket.cpp` | `src/detail/connection_engine_websocket.cpp` | `src/httpserver/detail/connection_engine.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/drain_scope.cpp` | `src/detail/drain_scope.cpp` | `src/httpserver/detail/drain_scope.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/fake_io_backend.cpp` | `src/detail/fake_io_backend.cpp` | `src/httpserver/detail/fake_io_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/features.cpp` | `src/features.cpp` | `src/httpserver/features.hpp` | Relocated public implementation |
| `src/detail/forms_multipart.cpp` | `src/forms/multipart.cpp`, `src/detail/forms_multipart.cpp` | `src/httpserver/forms/multipart.hpp`, `src/httpserver/detail/forms_multipart.hpp` | Public/private ownership split; existing semantics and opaque representation preserved |
| `src/detail/forms_multipart_files.cpp` | `src/forms/multipart_files.cpp` | `src/httpserver/forms/multipart.hpp` | Relocated public implementation |
| `src/detail/forms_urlencoded.cpp` | `src/forms/urlencoded.cpp`, `src/detail/forms_urlencoded.cpp` | `src/httpserver/forms/urlencoded.hpp`, `src/httpserver/detail/forms_urlencoded.hpp` | Public/private ownership split; existing semantics and opaque representation preserved |
| `src/detail/http1_websocket_handshake.cpp` | `src/detail/http1_websocket_handshake.cpp` | `src/httpserver/detail/http1_websocket_handshake.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_body_stream.cpp` | `src/detail/http2_body_stream.cpp` | `src/httpserver/detail/http2_body_stream.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_connection.cpp` | `src/detail/http2_connection.cpp` | `src/httpserver/detail/http2_connection.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_frame.cpp` | `src/detail/http2_frame.cpp` | `src/httpserver/detail/http2_frame.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_native_stream.cpp` | `src/detail/http2_native_stream.cpp` | `src/httpserver/detail/http2_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_native_timers.cpp` | `src/detail/http2_native_timers.cpp` | `src/httpserver/detail/http2_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_request_engine.cpp` | `src/detail/http2_request_engine.cpp` | `src/httpserver/detail/http2_request_engine.hpp`, `src/httpserver/detail/http2_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_request_head.cpp` | `src/detail/http2_request_head.cpp` | `src/httpserver/detail/http2_request_head.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_response_stream.cpp` | `src/detail/http2_response_stream.cpp` | `src/httpserver/detail/http2_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_stream_flow.cpp` | `src/detail/http2_stream_flow.cpp` | `src/httpserver/detail/http2_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_tcp_owner.cpp` | `src/detail/http2_tcp_owner.cpp` | `src/httpserver/detail/http2_tcp_owner.hpp`, `src/httpserver/detail/connection_engine.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_websocket_handshake.cpp` | `src/detail/http2_websocket_handshake.cpp` | `src/httpserver/detail/http2_websocket_handshake.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http2_websocket_stream.cpp` | `src/detail/http2_websocket_stream.cpp` | `src/httpserver/detail/http2_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_body_stream.cpp` | `src/detail/http3_body_stream.cpp` | `src/httpserver/detail/http3_body_stream.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_connection.cpp` | `src/detail/http3_connection.cpp` | `src/httpserver/detail/http3_connection.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_control_drain.cpp` | `src/detail/http3_control_drain.cpp` | `src/httpserver/detail/http3_connection.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_drain.cpp` | `src/detail/http3_drain.cpp` | `src/httpserver/detail/http3_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_frame.cpp` | `src/detail/http3_frame.cpp` | `src/httpserver/detail/http3_frame.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_native_stream.cpp` | `src/detail/http3_native_stream.cpp` | `src/httpserver/detail/http3_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_qpack.cpp` | `src/detail/http3_qpack.cpp` | `src/httpserver/detail/http3_connection.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_request_engine.cpp` | `src/detail/http3_request_engine.cpp` | `src/httpserver/detail/http3_request_engine.hpp`, `src/httpserver/detail/http3_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_request_head.cpp` | `src/detail/http3_request_head.cpp` | `src/httpserver/detail/http3_request_head.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_response_stream.cpp` | `src/detail/http3_response_stream.cpp` | `src/httpserver/detail/http3_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_semantic_stream.cpp` | `src/detail/http3_semantic_stream.cpp` | `src/httpserver/detail/http3_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_settings.cpp` | `src/detail/http3_settings.cpp` | `src/httpserver/detail/http3_connection.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_websocket_handshake.cpp` | `src/detail/http3_websocket_handshake.cpp` | `src/httpserver/detail/http3_websocket_handshake.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_websocket_handshake_stream.cpp` | `src/detail/http3_websocket_handshake_stream.cpp` | `src/httpserver/detail/http3_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/http3_websocket_stream.cpp` | `src/detail/http3_websocket_stream.cpp` | `src/httpserver/detail/http3_request_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_connection_owner.cpp` | `src/detail/io_connection_owner.cpp` | `src/httpserver/detail/io_connection_owner.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_epoll_backend.cpp` | `src/detail/io_epoll_backend.cpp` | `src/httpserver/detail/io_epoll_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_external_adapter.cpp` | `src/detail/io_external_adapter.cpp` | `src/httpserver/detail/io_poll_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_iocp_backend.cpp` | `src/detail/io_iocp_backend.cpp` | `src/httpserver/detail/io_iocp_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_kqueue_backend.cpp` | `src/detail/io_kqueue_backend.cpp` | `src/httpserver/detail/io_kqueue_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_managed_backend.cpp` | `src/detail/io_managed_backend.cpp` | `src/httpserver/detail/io_managed_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_managed_socket_backend.cpp` | `src/detail/io_managed_socket_backend.cpp` | `src/httpserver/detail/io_managed_socket_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_operation.cpp` | `src/detail/io_operation.cpp` | `src/httpserver/detail/io_operation.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_poll_backend.cpp` | `src/detail/io_poll_backend.cpp` | `src/httpserver/detail/io_poll_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_udp_backend.cpp` | `src/detail/io_udp_backend.cpp` | `src/httpserver/detail/io_udp_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/io_wake_source.cpp` | `src/detail/io_wake_source.cpp` | `src/httpserver/detail/io_poll_sys.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/listener_engine.cpp` | `src/detail/listener_engine.cpp` | `src/httpserver/detail/listener_engine.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/net_address.cpp` | `src/net/address.cpp`, `src/detail/net_address.cpp`, `src/httpserver/detail/net_address.hpp` | `src/httpserver/net/address.hpp`, `src/httpserver/detail/net_address.hpp` | Public/private ownership split; existing semantics and opaque representation preserved |
| `src/detail/peer_policy.cpp` | `src/detail/peer_policy.cpp`, `src/server/peer_policy.cpp`, `src/httpserver/detail/peer_policy.hpp` | `src/httpserver/detail/peer_policy.hpp`, `src/httpserver/server/peer_policy.hpp` | Public/private ownership split; existing semantics and opaque representation preserved |
| `src/detail/quic_ack_state.cpp` | `src/detail/quic_ack_state.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_address_token.cpp` | `src/detail/quic_address_token.cpp` | `src/httpserver/detail/quic_address_token.hpp`, `src/httpserver/detail/quic_crypto.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_amplification.cpp` | `src/detail/quic_amplification.cpp` | `src/httpserver/detail/quic_amplification.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_congestion.cpp` | `src/detail/quic_congestion.cpp` | `src/httpserver/detail/quic_congestion.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_connection_identity.cpp` | `src/detail/quic_connection_identity.cpp` | `src/httpserver/detail/quic_connection_identity.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_connection_lifecycle.cpp` | `src/detail/quic_connection_lifecycle.cpp` | `src/httpserver/detail/quic_connection_lifecycle.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_crypto_provider.hpp` | `src/httpserver/detail/quic_crypto_provider.hpp` | `src/httpserver/detail/quic_crypto_provider.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/detail/quic_datagram_dispatch.cpp` | `src/detail/quic_datagram_dispatch.cpp` | `src/httpserver/detail/quic_datagram_dispatch.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_datagram_sizing.cpp` | `src/detail/quic_datagram_sizing.cpp` | `src/httpserver/detail/quic_datagram_sizing.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_flow_control.cpp` | `src/detail/quic_flow_control.cpp` | `src/httpserver/detail/quic_flow_control.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_flow_internal.hpp` | `src/httpserver/detail/quic_flow_internal.hpp`, `src/detail/quic_flow_internal.cpp` | `src/httpserver/detail/quic_flow_internal.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/detail/quic_flow_receive.cpp` | `src/detail/quic_flow_receive.cpp` | `src/httpserver/detail/quic_flow_internal.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_flow_send.cpp` | `src/detail/quic_flow_send.cpp` | `src/httpserver/detail/quic_flow_internal.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_frame.cpp` | `src/detail/quic_frame.cpp` | `src/httpserver/detail/quic_frame.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_frame_write.cpp` | `src/detail/quic_frame_write.cpp` | `src/httpserver/detail/quic_frame.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_invariant_header.cpp` | `src/detail/quic_invariant_header.cpp` | `src/httpserver/detail/quic_invariant_header.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_key_derivation.cpp` | `src/detail/quic_key_derivation.cpp` | `src/httpserver/detail/quic_crypto_provider.hpp`, `src/httpserver/detail/quic_crypto.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_key_state.cpp` | `src/detail/quic_key_state.cpp` | `src/httpserver/detail/quic_key_state.hpp`, `src/httpserver/detail/quic_crypto.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_network_connection.cpp` | `src/detail/quic_network_connection.cpp` | `src/httpserver/detail/quic_network_connection.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_network_emission.cpp` | `src/detail/quic_network_emission.cpp` | `src/httpserver/detail/quic_network_connection.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_network_packets.cpp` | `src/detail/quic_network_packets.cpp` | `src/httpserver/detail/quic_network_connection.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_pacing.cpp` | `src/detail/quic_pacing.cpp` | `src/httpserver/detail/quic_pacing.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_packet.cpp` | `src/detail/quic_packet.cpp` | `src/httpserver/detail/quic_packet.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_packet_crypto.cpp` | `src/detail/quic_packet_crypto.cpp` | `src/httpserver/detail/quic_crypto_provider.hpp`, `src/httpserver/detail/quic_crypto.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_path_mtu.cpp` | `src/detail/quic_path_mtu.cpp` | `src/httpserver/detail/quic_path_mtu.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_persistent_congestion.cpp` | `src/detail/quic_persistent_congestion.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_reassembly.cpp` | `src/detail/quic_reassembly.cpp` | `src/httpserver/detail/quic_reassembly.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_recovery.cpp` | `src/detail/quic_recovery.cpp` | `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_recovery_datagram.cpp` | `src/detail/quic_recovery_datagram.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_recovery_emission.cpp` | `src/detail/quic_recovery_emission.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_recovery_flow.cpp` | `src/detail/quic_recovery_flow.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_recovery_internal.hpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/detail/quic_recovery_internal.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/detail/quic_recovery_mtu.cpp` | `src/detail/quic_recovery_mtu.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_recovery_timer.cpp` | `src/detail/quic_recovery_timer.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_repacketize.cpp` | `src/detail/quic_repacketize.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_retry_integrity.cpp` | `src/detail/quic_retry_integrity.cpp` | `src/httpserver/detail/quic_crypto_provider.hpp`, `src/httpserver/detail/quic_crypto.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_send_schedule.cpp` | `src/detail/quic_send_schedule.cpp` | `src/httpserver/detail/quic_recovery_internal.hpp`, `src/httpserver/detail/quic_recovery.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_server_admission.cpp` | `src/detail/quic_server_admission.cpp` | `src/httpserver/detail/quic_server_admission.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_storage.cpp` | `src/detail/quic_storage.cpp` | `src/httpserver/detail/quic_storage.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_stream_state.cpp` | `src/detail/quic_stream_state.cpp` | `src/httpserver/detail/quic_stream_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_tls_buffers.cpp` | `src/detail/quic_tls_buffers.cpp` | `src/httpserver/detail/quic_tls_callbacks.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_tls_callbacks.cpp` | `src/detail/quic_tls_callbacks.cpp` | `src/httpserver/detail/quic_tls_callbacks.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_tls_callbacks.hpp` | `src/httpserver/detail/quic_tls_callbacks.hpp`, `src/detail/quic_tls_callbacks.cpp` | `src/httpserver/detail/quic_tls_callbacks.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/detail/quic_tls_session.cpp` | `src/detail/quic_tls_session.cpp` | `src/httpserver/detail/quic_tls_session.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_transport_parameters.cpp` | `src/detail/quic_transport_parameters.cpp` | `src/httpserver/detail/quic_transport_parameters.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_udp_listener.cpp` | `src/detail/quic_udp_listener.cpp` | `src/httpserver/detail/quic_udp_listener.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/quic_varint.cpp` | `src/detail/quic_varint.cpp` | `src/httpserver/detail/quic_varint.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/request_lifecycle.cpp` | `src/detail/request_lifecycle.cpp` | `src/httpserver/detail/request_lifecycle.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/server.cpp` | `src/server/server.cpp`, `src/detail/drain_scope.cpp`, `src/httpserver/detail/drain_ticket_state.hpp` | `src/httpserver/server/server.hpp`, `src/httpserver/detail/drain_scope.hpp`, `src/httpserver/detail/drain_ticket_state.hpp` | Public/private ownership split; existing semantics and opaque representation preserved |
| `src/detail/server_diagnostics.cpp` | `src/server/diagnostics.cpp`, `src/detail/diagnostics_access.cpp`, `src/httpserver/detail/diagnostics_state.hpp` | `src/httpserver/server/diagnostics.hpp`, `src/httpserver/detail/diagnostics_access.hpp`, `src/httpserver/detail/diagnostics_state.hpp` | Public/private ownership split; existing semantics and opaque representation preserved |
| `src/detail/server_hooks.cpp` | `src/server/hooks.cpp`, `src/detail/hook_bus.cpp`, `src/httpserver/detail/hook_bus.hpp` | `src/httpserver/server/hooks.hpp`, `src/httpserver/detail/hook_bus.hpp` | Public/private ownership split; existing semantics and opaque representation preserved |
| `src/detail/server_tls_credentials.cpp` | `src/server/tls_credentials.cpp`, `src/detail/tls_credentials_access.cpp`, `src/httpserver/detail/tls_credentials_state.hpp` | `src/httpserver/server/tls_credentials.hpp`, `src/httpserver/detail/tls_credentials_access.hpp`, `src/httpserver/detail/tls_credentials_state.hpp` | Public/private ownership split; existing semantics and opaque representation preserved |
| `src/detail/server_tls_psk.cpp` | `src/server/tls_psk.cpp`, `src/detail/tls_psk_access.cpp`, `src/httpserver/detail/tls_psk_state.hpp` | `src/httpserver/server/tls_credentials.hpp`, `src/httpserver/detail/tls_psk_state.hpp`, `src/httpserver/detail/tls_credentials_access.hpp` | Public/private ownership split; existing semantics and opaque representation preserved |
| `src/detail/tls_credentials.cpp` | `src/detail/tls_credentials.cpp` | `src/httpserver/detail/tls_credentials.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/tls_io_backend.cpp` | `src/detail/tls_io_backend.cpp` | `src/httpserver/detail/tls_io_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/tls_provider.cpp` | `src/detail/tls_provider.cpp` | `src/httpserver/detail/tls_build_probe.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/tls_psk.cpp` | `src/detail/tls_psk.cpp` | `src/httpserver/detail/tls_psk.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/tls_psk_attempt.cpp` | `src/detail/tls_psk_attempt.cpp` | `src/httpserver/detail/tls_psk_attempt.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/tls_psk_runtime.cpp` | `src/detail/tls_psk_runtime.cpp` | `src/httpserver/detail/tls_psk_runtime.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/tls_session.cpp` | `src/detail/tls_session.cpp` | `src/httpserver/detail/tls_session.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/tls_socket_backend.cpp` | `src/detail/tls_socket_backend.cpp` | `src/httpserver/detail/tls_socket_backend.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/websocket_codec.cpp` | `src/detail/websocket_codec.cpp` | `src/httpserver/detail/websocket_codec.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/websocket_driver.cpp` | `src/detail/websocket_driver.cpp` | `src/httpserver/detail/websocket_driver.hpp`, `src/httpserver/detail/websocket_session_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/websocket_handshake.cpp` | `src/detail/websocket_handshake.cpp` | `src/httpserver/detail/websocket_handshake.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/websocket_session.cpp` | `src/websocket/session.cpp` | `src/httpserver/websocket/session.hpp` | Relocated public implementation |
| `src/detail/websocket_session_state.cpp` | `src/detail/websocket_session_state.cpp` | `src/httpserver/detail/websocket_session_state.hpp` | Retained cohesive implementation with explicit owner |
| `src/detail/worker_pool.cpp` | `src/detail/worker_pool.cpp` | `src/httpserver/detail/worker_pool.hpp` | Retained cohesive implementation with explicit owner |
| `src/httpserver.hpp` | `src/httpserver.hpp` | `src/httpserver.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/auth/basic_auth.hpp` | `src/httpserver/auth/basic_auth.hpp`, `src/auth/basic_auth.cpp` | `src/httpserver/auth/basic_auth.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/auth/digest_auth.hpp` | `src/httpserver/auth/digest_auth.hpp`, `src/auth/digest_auth.cpp` | `src/httpserver/auth/digest_auth.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/body_reader.hpp` | `src/httpserver/body_reader.hpp`, `src/body_reader.cpp` | `src/httpserver/body_reader.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/concurrency/cancellation.hpp` | `src/httpserver/concurrency/cancellation.hpp`, `src/concurrency/cancellation.cpp` | `src/httpserver/concurrency/cancellation.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/concurrency/concurrency.hpp` | `src/httpserver/concurrency/concurrency.hpp` | `src/httpserver/concurrency/concurrency.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/concurrency/executor.hpp` | `src/httpserver/concurrency/executor.hpp`, `src/concurrency/executor.cpp` | `src/httpserver/concurrency/executor.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/concurrency/resume_signal.hpp` | `src/httpserver/concurrency/resume_signal.hpp`, `src/concurrency/resume_signal.cpp` | `src/httpserver/concurrency/resume_signal.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/concurrency/task.hpp` | `src/httpserver/concurrency/task.hpp`, `src/concurrency/task.cpp` | `src/httpserver/concurrency/task.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/auth_text.hpp` | `src/httpserver/detail/auth_text.hpp`, `src/detail/auth_text.cpp` | `src/httpserver/detail/auth_text.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/base64.hpp` | `src/httpserver/detail/base64.hpp`, `src/detail/base64.cpp` | `src/httpserver/detail/base64.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/connection_engine.hpp` | `src/httpserver/detail/connection_engine.hpp`, `src/detail/connection_engine.cpp` | `src/httpserver/detail/connection_engine.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/diagnostics_access.hpp` | `src/httpserver/detail/diagnostics_access.hpp`, `src/detail/diagnostics_access.cpp` | `src/httpserver/detail/diagnostics_access.hpp`, `src/httpserver/detail/diagnostics_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/digest_hex.hpp` | `src/httpserver/detail/digest_hex.hpp`, `src/detail/digest_hex.cpp` | `src/httpserver/detail/digest_hex.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/digest_ledger.hpp` | `src/httpserver/detail/digest_ledger.hpp`, `src/detail/digest_ledger.cpp` | `src/httpserver/detail/digest_ledger.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/digest_nonce.hpp` | `src/httpserver/detail/digest_nonce.hpp`, `src/detail/digest_nonce.cpp` | `src/httpserver/detail/digest_nonce.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/digest_params.hpp` | `src/httpserver/detail/digest_params.hpp`, `src/detail/digest_params.cpp` | `src/httpserver/detail/digest_params.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/digest_response.hpp` | `src/httpserver/detail/digest_response.hpp`, `src/detail/digest_response.cpp` | `src/httpserver/detail/digest_response.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/drain_scope.hpp` | `src/httpserver/detail/drain_scope.hpp`, `src/detail/drain_scope.cpp` | `src/httpserver/detail/drain_scope.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/entropy_sys.hpp` | `src/httpserver/detail/entropy_sys.hpp`, `src/detail/entropy_sys.cpp` | `src/httpserver/detail/entropy_sys.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/exchange_runner.hpp` | `src/httpserver/detail/exchange_runner.hpp`, `src/detail/exchange_runner.cpp` | `src/httpserver/detail/exchange_runner.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/fake_io_backend.hpp` | `src/httpserver/detail/fake_io_backend.hpp`, `src/detail/fake_io_backend.cpp` | `src/httpserver/detail/fake_io_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/forms_multipart.hpp` | `src/httpserver/detail/forms_multipart.hpp`, `src/detail/forms_multipart.cpp` | `src/httpserver/detail/forms_multipart.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/forms_urlencoded.hpp` | `src/httpserver/detail/forms_urlencoded.hpp`, `src/detail/forms_urlencoded.cpp` | `src/httpserver/detail/forms_urlencoded.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/forms_verdict.hpp` | `src/httpserver/detail/forms_verdict.hpp`, `src/detail/forms_verdict.cpp` | `src/httpserver/detail/forms_verdict.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/hpack_connection.hpp` | `src/httpserver/detail/hpack_connection.hpp` | `src/httpserver/detail/hpack_connection.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/hpack_decoder.hpp` | `src/httpserver/detail/hpack_decoder.hpp`, `src/detail/hpack_decoder.cpp` | `src/httpserver/detail/hpack_decoder.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/hpack_dynamic_table.hpp` | `src/httpserver/detail/hpack_dynamic_table.hpp`, `src/detail/hpack_dynamic_table.cpp` | `src/httpserver/detail/hpack_dynamic_table.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/hpack_encoder.hpp` | `src/httpserver/detail/hpack_encoder.hpp`, `src/detail/hpack_encoder.cpp` | `src/httpserver/detail/hpack_encoder.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/hpack_field_section.hpp` | `src/httpserver/detail/hpack_field_section.hpp`, `src/detail/hpack_field_section.cpp` | `src/httpserver/detail/hpack_field_section.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/hpack_huffman_table.hpp` | `src/httpserver/detail/hpack_huffman_table.hpp` | `src/httpserver/detail/hpack_huffman_table.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/hpack_primitives.hpp` | `src/httpserver/detail/hpack_primitives.hpp`, `src/detail/hpack_primitives.cpp` | `src/httpserver/detail/hpack_primitives.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/hpack_static_table.hpp` | `src/httpserver/detail/hpack_static_table.hpp` | `src/httpserver/detail/hpack_static_table.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/http1_body_decoder.hpp` | `src/httpserver/detail/http1_body_decoder.hpp`, `src/detail/http1_body_decoder.cpp` | `src/httpserver/detail/http1_body_decoder.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_body_mode.hpp` | `src/httpserver/detail/http1_body_mode.hpp`, `src/detail/http1_body_mode.cpp` | `src/httpserver/detail/http1_body_mode.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_body_source.hpp` | `src/httpserver/detail/http1_body_source.hpp`, `src/detail/http1_body_source.cpp` | `src/httpserver/detail/http1_body_source.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_error_synth.hpp` | `src/httpserver/detail/http1_error_synth.hpp`, `src/detail/http1_error_synth.cpp` | `src/httpserver/detail/http1_error_synth.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_head_limits.hpp` | `src/httpserver/detail/http1_head_limits.hpp` | `src/httpserver/detail/http1_head_limits.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/http1_head_parser.hpp` | `src/httpserver/detail/http1_head_parser.hpp`, `src/detail/http1_head_parser.cpp` | `src/httpserver/detail/http1_head_parser.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_host.hpp` | `src/httpserver/detail/http1_host.hpp`, `src/detail/http1_host.cpp` | `src/httpserver/detail/http1_host.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_response_framer.hpp` | `src/httpserver/detail/http1_response_framer.hpp`, `src/detail/http1_response_framer.cpp` | `src/httpserver/detail/http1_response_framer.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_response_mode.hpp` | `src/httpserver/detail/http1_response_mode.hpp`, `src/detail/http1_response_mode.cpp` | `src/httpserver/detail/http1_response_mode.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_response_outbox.hpp` | `src/httpserver/detail/http1_response_outbox.hpp`, `src/detail/http1_response_outbox.cpp` | `src/httpserver/detail/http1_response_outbox.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_target.hpp` | `src/httpserver/detail/http1_target.hpp`, `src/detail/http1_target.cpp` | `src/httpserver/detail/http1_target.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http1_websocket_handshake.hpp` | `src/httpserver/detail/http1_websocket_handshake.hpp`, `src/detail/http1_websocket_handshake.cpp` | `src/httpserver/detail/http1_websocket_handshake.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http2_body_stream.hpp` | `src/httpserver/detail/http2_body_stream.hpp`, `src/detail/http2_body_stream.cpp` | `src/httpserver/detail/http2_body_stream.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http2_connection.hpp` | `src/httpserver/detail/http2_connection.hpp`, `src/detail/http2_connection.cpp` | `src/httpserver/detail/http2_connection.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http2_dispatch.hpp` | `src/httpserver/detail/http2_dispatch.hpp`, `src/detail/http2_dispatch.cpp` | `src/httpserver/detail/http2_dispatch.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http2_flow_control.hpp` | `src/httpserver/detail/http2_flow_control.hpp` | `src/httpserver/detail/http2_flow_control.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/http2_frame.hpp` | `src/httpserver/detail/http2_frame.hpp`, `src/detail/http2_frame.cpp` | `src/httpserver/detail/http2_frame.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http2_request_engine.hpp` | `src/httpserver/detail/http2_request_engine.hpp`, `src/detail/http2_request_engine.cpp` | `src/httpserver/detail/http2_request_engine.hpp`, `src/httpserver/detail/http2_request_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http2_request_head.hpp` | `src/httpserver/detail/http2_request_head.hpp`, `src/detail/http2_request_head.cpp` | `src/httpserver/detail/http2_request_head.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http2_request_state.hpp` | `src/httpserver/detail/http2_request_state.hpp`, `src/detail/http2_request_state.cpp` | `src/httpserver/detail/http2_request_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http2_tcp_owner.hpp` | `src/httpserver/detail/http2_tcp_owner.hpp`, `src/detail/http2_tcp_owner.cpp` | `src/httpserver/detail/http2_tcp_owner.hpp`, `src/httpserver/detail/connection_engine.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http2_websocket_handshake.hpp` | `src/httpserver/detail/http2_websocket_handshake.hpp`, `src/detail/http2_websocket_handshake.cpp` | `src/httpserver/detail/http2_websocket_handshake.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http3_body_stream.hpp` | `src/httpserver/detail/http3_body_stream.hpp`, `src/detail/http3_body_stream.cpp` | `src/httpserver/detail/http3_body_stream.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http3_connection.hpp` | `src/httpserver/detail/http3_connection.hpp`, `src/detail/http3_connection.cpp` | `src/httpserver/detail/http3_connection.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http3_frame.hpp` | `src/httpserver/detail/http3_frame.hpp`, `src/detail/http3_frame.cpp` | `src/httpserver/detail/http3_frame.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http3_request_engine.hpp` | `src/httpserver/detail/http3_request_engine.hpp`, `src/detail/http3_request_engine.cpp` | `src/httpserver/detail/http3_request_engine.hpp`, `src/httpserver/detail/http3_request_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http3_request_head.hpp` | `src/httpserver/detail/http3_request_head.hpp`, `src/detail/http3_request_head.cpp` | `src/httpserver/detail/http3_request_head.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http3_request_state.hpp` | `src/httpserver/detail/http3_request_state.hpp`, `src/detail/http3_request_state.cpp` | `src/httpserver/detail/http3_request_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/http3_websocket_handshake.hpp` | `src/httpserver/detail/http3_websocket_handshake.hpp`, `src/detail/http3_websocket_handshake.cpp` | `src/httpserver/detail/http3_websocket_handshake.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_completion_storage.hpp` | `src/httpserver/detail/io_completion_storage.hpp`, `src/detail/io_completion_storage.cpp` | `src/httpserver/detail/io_completion_storage.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_connection_owner.hpp` | `src/httpserver/detail/io_connection_owner.hpp`, `src/detail/io_connection_owner.cpp` | `src/httpserver/detail/io_connection_owner.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_datagram.hpp` | `src/httpserver/detail/io_datagram.hpp` | `src/httpserver/detail/io_datagram.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/io_epoll_backend.hpp` | `src/httpserver/detail/io_epoll_backend.hpp`, `src/detail/io_epoll_backend.cpp` | `src/httpserver/detail/io_epoll_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_iocp_backend.hpp` | `src/httpserver/detail/io_iocp_backend.hpp`, `src/detail/io_iocp_backend.cpp` | `src/httpserver/detail/io_iocp_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_kqueue_backend.hpp` | `src/httpserver/detail/io_kqueue_backend.hpp`, `src/detail/io_kqueue_backend.cpp` | `src/httpserver/detail/io_kqueue_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_managed_backend.hpp` | `src/httpserver/detail/io_managed_backend.hpp`, `src/detail/io_managed_backend.cpp` | `src/httpserver/detail/io_managed_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_managed_socket_backend.hpp` | `src/httpserver/detail/io_managed_socket_backend.hpp`, `src/detail/io_managed_socket_backend.cpp` | `src/httpserver/detail/io_managed_socket_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_operation.hpp` | `src/httpserver/detail/io_operation.hpp`, `src/detail/io_operation.cpp` | `src/httpserver/detail/io_operation.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_poll_backend.hpp` | `src/httpserver/detail/io_poll_backend.hpp`, `src/detail/io_poll_backend.cpp` | `src/httpserver/detail/io_poll_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_poll_sys.hpp` | `src/httpserver/detail/io_poll_sys.hpp`, `src/detail/io_poll_sys.cpp` | `src/httpserver/detail/io_poll_sys.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_socket_backend.hpp` | `src/httpserver/detail/io_socket_backend.hpp` | `src/httpserver/detail/io_socket_backend.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/io_timer_sweep.hpp` | `src/httpserver/detail/io_timer_sweep.hpp`, `src/detail/io_timer_sweep.cpp` | `src/httpserver/detail/io_timer_sweep.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/io_udp_backend.hpp` | `src/httpserver/detail/io_udp_backend.hpp`, `src/detail/io_udp_backend.cpp` | `src/httpserver/detail/io_udp_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/lifecycle_sink.hpp` | `src/httpserver/detail/lifecycle_sink.hpp`, `src/detail/lifecycle_sink.cpp` | `src/httpserver/detail/lifecycle_sink.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/listener_engine.hpp` | `src/httpserver/detail/listener_engine.hpp`, `src/detail/listener_engine.cpp` | `src/httpserver/detail/listener_engine.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/md5.hpp` | `src/httpserver/detail/md5.hpp`, `src/detail/md5.cpp` | `src/httpserver/detail/md5.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/multiplexed_request_head.hpp` | `src/httpserver/detail/multiplexed_request_head.hpp`, `src/detail/multiplexed_request_head.cpp` | `src/httpserver/detail/multiplexed_request_head.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/qpack_decoder.hpp` | `src/httpserver/detail/qpack_decoder.hpp`, `src/detail/qpack_decoder.cpp` | `src/httpserver/detail/qpack_decoder.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `src/httpserver/detail/qpack_dynamic_table.hpp`, `src/detail/qpack_dynamic_table.cpp` | `src/httpserver/detail/qpack_dynamic_table.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/qpack_encoder.hpp` | `src/httpserver/detail/qpack_encoder.hpp`, `src/detail/qpack_encoder.cpp` | `src/httpserver/detail/qpack_encoder.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/qpack_field_section.hpp` | `src/httpserver/detail/qpack_field_section.hpp`, `src/detail/qpack_field_section.cpp` | `src/httpserver/detail/qpack_field_section.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/qpack_instructions.hpp` | `src/httpserver/detail/qpack_instructions.hpp`, `src/detail/qpack_instructions.cpp` | `src/httpserver/detail/qpack_instructions.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/qpack_primitives.hpp` | `src/httpserver/detail/qpack_primitives.hpp`, `src/detail/qpack_primitives.cpp` | `src/httpserver/detail/qpack_primitives.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/qpack_static_table.hpp` | `src/httpserver/detail/qpack_static_table.hpp` | `src/httpserver/detail/qpack_static_table.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/quic_address_token.hpp` | `src/httpserver/detail/quic_address_token.hpp`, `src/detail/quic_address_token.cpp` | `src/httpserver/detail/quic_address_token.hpp`, `src/httpserver/detail/quic_crypto.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_amplification.hpp` | `src/httpserver/detail/quic_amplification.hpp`, `src/detail/quic_amplification.cpp` | `src/httpserver/detail/quic_amplification.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_codec.hpp` | `src/httpserver/detail/quic_codec.hpp` | `src/httpserver/detail/quic_codec.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/quic_congestion.hpp` | `src/httpserver/detail/quic_congestion.hpp`, `src/detail/quic_congestion.cpp` | `src/httpserver/detail/quic_congestion.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_connection_identity.hpp` | `src/httpserver/detail/quic_connection_identity.hpp`, `src/detail/quic_connection_identity.cpp` | `src/httpserver/detail/quic_connection_identity.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_connection_lifecycle.hpp` | `src/httpserver/detail/quic_connection_lifecycle.hpp`, `src/detail/quic_connection_lifecycle.cpp` | `src/httpserver/detail/quic_connection_lifecycle.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_crypto.hpp` | `src/httpserver/detail/quic_crypto.hpp` | `src/httpserver/detail/quic_crypto.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/quic_datagram_dispatch.hpp` | `src/httpserver/detail/quic_datagram_dispatch.hpp`, `src/detail/quic_datagram_dispatch.cpp` | `src/httpserver/detail/quic_datagram_dispatch.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_datagram_sizing.hpp` | `src/httpserver/detail/quic_datagram_sizing.hpp`, `src/detail/quic_datagram_sizing.cpp` | `src/httpserver/detail/quic_datagram_sizing.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_flow_control.hpp` | `src/httpserver/detail/quic_flow_control.hpp`, `src/detail/quic_flow_control.cpp` | `src/httpserver/detail/quic_flow_control.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_frame.hpp` | `src/httpserver/detail/quic_frame.hpp`, `src/detail/quic_frame.cpp` | `src/httpserver/detail/quic_frame.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_invariant_header.hpp` | `src/httpserver/detail/quic_invariant_header.hpp`, `src/detail/quic_invariant_header.cpp` | `src/httpserver/detail/quic_invariant_header.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_key_state.hpp` | `src/httpserver/detail/quic_key_state.hpp`, `src/detail/quic_key_state.cpp` | `src/httpserver/detail/quic_key_state.hpp`, `src/httpserver/detail/quic_crypto.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_network_connection.hpp` | `src/httpserver/detail/quic_network_connection.hpp`, `src/detail/quic_network_connection.cpp` | `src/httpserver/detail/quic_network_connection.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_pacing.hpp` | `src/httpserver/detail/quic_pacing.hpp`, `src/detail/quic_pacing.cpp` | `src/httpserver/detail/quic_pacing.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_packet.hpp` | `src/httpserver/detail/quic_packet.hpp`, `src/detail/quic_packet.cpp` | `src/httpserver/detail/quic_packet.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_path_mtu.hpp` | `src/httpserver/detail/quic_path_mtu.hpp`, `src/detail/quic_path_mtu.cpp` | `src/httpserver/detail/quic_path_mtu.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_reassembly.hpp` | `src/httpserver/detail/quic_reassembly.hpp`, `src/detail/quic_reassembly.cpp` | `src/httpserver/detail/quic_reassembly.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_recovery.hpp` | `src/httpserver/detail/quic_recovery.hpp`, `src/detail/quic_recovery.cpp` | `src/httpserver/detail/quic_recovery.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_server_admission.hpp` | `src/httpserver/detail/quic_server_admission.hpp`, `src/detail/quic_server_admission.cpp` | `src/httpserver/detail/quic_server_admission.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_storage.hpp` | `src/httpserver/detail/quic_storage.hpp`, `src/detail/quic_storage.cpp` | `src/httpserver/detail/quic_storage.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_stream_state.hpp` | `src/httpserver/detail/quic_stream_state.hpp`, `src/detail/quic_stream_state.cpp` | `src/httpserver/detail/quic_stream_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_tls_session.hpp` | `src/httpserver/detail/quic_tls_session.hpp`, `src/detail/quic_tls_session.cpp` | `src/httpserver/detail/quic_tls_session.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_transport_parameters.hpp` | `src/httpserver/detail/quic_transport_parameters.hpp`, `src/detail/quic_transport_parameters.cpp` | `src/httpserver/detail/quic_transport_parameters.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_udp_listener.hpp` | `src/httpserver/detail/quic_udp_listener.hpp`, `src/detail/quic_udp_listener.cpp` | `src/httpserver/detail/quic_udp_listener.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/quic_varint.hpp` | `src/httpserver/detail/quic_varint.hpp`, `src/detail/quic_varint.cpp` | `src/httpserver/detail/quic_varint.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/request_handler_frames.hpp` | `src/httpserver/detail/request_handler_frames.hpp`, `src/detail/request_handler_frames.cpp` | `src/httpserver/detail/request_handler_frames.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/request_lifecycle.hpp` | `src/httpserver/detail/request_lifecycle.hpp`, `src/detail/request_lifecycle.cpp` | `src/httpserver/detail/request_lifecycle.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/resource_charge.hpp` | `src/httpserver/detail/resource_charge.hpp`, `src/detail/resource_charge.cpp` | `src/httpserver/detail/resource_charge.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/resource_scope.hpp` | `src/httpserver/detail/resource_scope.hpp` | `src/httpserver/detail/resource_scope.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/secure_compare.hpp` | `src/httpserver/detail/secure_compare.hpp`, `src/detail/secure_compare.cpp` | `src/httpserver/detail/secure_compare.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/secure_zero.hpp` | `src/httpserver/detail/secure_zero.hpp` | `src/httpserver/detail/secure_zero.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/sha1.hpp` | `src/httpserver/detail/sha1.hpp`, `src/detail/sha1.cpp` | `src/httpserver/detail/sha1.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/sha256.hpp` | `src/httpserver/detail/sha256.hpp`, `src/detail/sha256.cpp` | `src/httpserver/detail/sha256.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/tls_build_probe.hpp` | `src/httpserver/detail/tls_build_probe.hpp` | `src/httpserver/detail/tls_build_probe.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/detail/tls_credentials.hpp` | `src/httpserver/detail/tls_credentials.hpp`, `src/detail/tls_credentials.cpp` | `src/httpserver/detail/tls_credentials.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/tls_credentials_access.hpp` | `src/httpserver/detail/tls_credentials_access.hpp`, `src/detail/tls_credentials_access.cpp` | `src/httpserver/detail/tls_credentials_access.hpp`, `src/httpserver/detail/tls_credentials_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/tls_io_backend.hpp` | `src/httpserver/detail/tls_io_backend.hpp`, `src/detail/tls_io_backend.cpp` | `src/httpserver/detail/tls_io_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/tls_psk.hpp` | `src/httpserver/detail/tls_psk.hpp`, `src/detail/tls_psk.cpp` | `src/httpserver/detail/tls_psk.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/tls_psk_attempt.hpp` | `src/httpserver/detail/tls_psk_attempt.hpp`, `src/detail/tls_psk_attempt.cpp` | `src/httpserver/detail/tls_psk_attempt.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/tls_psk_runtime.hpp` | `src/httpserver/detail/tls_psk_runtime.hpp`, `src/detail/tls_psk_runtime.cpp` | `src/httpserver/detail/tls_psk_runtime.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/tls_session.hpp` | `src/httpserver/detail/tls_session.hpp`, `src/detail/tls_session.cpp` | `src/httpserver/detail/tls_session.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/tls_socket_backend.hpp` | `src/httpserver/detail/tls_socket_backend.hpp`, `src/detail/tls_socket_backend.cpp` | `src/httpserver/detail/tls_socket_backend.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/unescape_helpers.hpp` | `src/httpserver/detail/unescape_helpers.hpp`, `src/detail/unescape_helpers.cpp` | `src/httpserver/detail/unescape_helpers.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/websocket_codec.hpp` | `src/httpserver/detail/websocket_codec.hpp`, `src/detail/websocket_codec.cpp` | `src/httpserver/detail/websocket_codec.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/websocket_driver.hpp` | `src/httpserver/detail/websocket_driver.hpp`, `src/detail/websocket_driver.cpp` | `src/httpserver/detail/websocket_driver.hpp`, `src/httpserver/detail/websocket_session_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/websocket_handshake.hpp` | `src/httpserver/detail/websocket_handshake.hpp`, `src/detail/websocket_handshake.cpp` | `src/httpserver/detail/websocket_handshake.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/websocket_session_state.hpp` | `src/httpserver/detail/websocket_session_state.hpp`, `src/detail/websocket_session_state.cpp` | `src/httpserver/detail/websocket_session_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/websocket_utf8.hpp` | `src/httpserver/detail/websocket_utf8.hpp`, `src/detail/websocket_utf8.cpp` | `src/httpserver/detail/websocket_utf8.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/detail/worker_pool.hpp` | `src/httpserver/detail/worker_pool.hpp`, `src/detail/worker_pool.cpp` | `src/httpserver/detail/worker_pool.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/exchange.hpp` | `src/httpserver/exchange.hpp`, `src/exchange.cpp` | `src/httpserver/exchange.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/features.hpp` | `src/httpserver/features.hpp`, `src/features.cpp` | `src/httpserver/features.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/forms/multipart.hpp` | `src/httpserver/forms/multipart.hpp`, `src/forms/multipart.cpp`, `src/forms/multipart_files.cpp` | `src/httpserver/forms/multipart.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/forms/urlencoded.hpp` | `src/httpserver/forms/urlencoded.hpp`, `src/forms/urlencoded.cpp` | `src/httpserver/forms/urlencoded.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/http.hpp` | `src/httpserver/http.hpp` | `src/httpserver/http.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/http/cookie.hpp` | `src/httpserver/http/cookie.hpp`, `src/http/cookie.cpp` | `src/httpserver/http/cookie.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/http/fields.hpp` | `src/httpserver/http/fields.hpp`, `src/http/fields.cpp` | `src/httpserver/http/fields.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/http/method.hpp` | `src/httpserver/http/method.hpp`, `src/http/method.cpp` | `src/httpserver/http/method.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/http/outcome.hpp` | `src/httpserver/http/outcome.hpp` | `src/httpserver/http/outcome.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/http/protocol.hpp` | `src/httpserver/http/protocol.hpp` | `src/httpserver/http/protocol.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/http/request_head.hpp` | `src/httpserver/http/request_head.hpp` | `src/httpserver/http/request_head.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/http/status.hpp` | `src/httpserver/http/status.hpp` | `src/httpserver/http/status.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/net/address.hpp` | `src/httpserver/net/address.hpp`, `src/net/address.cpp` | `src/httpserver/net/address.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/response_definition.hpp` | `src/httpserver/response_definition.hpp`, `src/response_definition.cpp` | `src/httpserver/response_definition.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/response_sources.hpp` | `src/httpserver/response_sources.hpp`, `src/response_sources.cpp` | `src/httpserver/response_sources.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/response_writer.hpp` | `src/httpserver/response_writer.hpp`, `src/response_writer.cpp` | `src/httpserver/response_writer.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/budgets.hpp` | `src/httpserver/server/budgets.hpp`, `src/server/budgets.cpp` | `src/httpserver/server/budgets.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/configuration.hpp` | `src/httpserver/server/configuration.hpp` | `src/httpserver/server/configuration.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/server/diagnostics.hpp` | `src/httpserver/server/diagnostics.hpp`, `src/server/diagnostics.cpp` | `src/httpserver/server/diagnostics.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/hooks.hpp` | `src/httpserver/server/hooks.hpp`, `src/server/hooks.cpp` | `src/httpserver/server/hooks.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/options.hpp` | `src/httpserver/server/options.hpp`, `src/server/options.cpp` | `src/httpserver/server/options.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/peer_policy.hpp` | `src/httpserver/server/peer_policy.hpp`, `src/server/peer_policy.cpp` | `src/httpserver/server/peer_policy.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/readiness.hpp` | `src/httpserver/server/readiness.hpp` | `src/httpserver/server/readiness.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/server/route_sync.hpp` | `src/httpserver/server/route_sync.hpp`, `src/server/route_sync.cpp` | `src/httpserver/server/route_sync.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/routes.hpp` | `src/httpserver/server/routes.hpp`, `src/server/routes.cpp` | `src/httpserver/server/routes.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/server.hpp` | `src/httpserver/server/server.hpp`, `src/server/server.cpp` | `src/httpserver/server/server.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/tls_credentials.hpp` | `src/httpserver/server/tls_credentials.hpp`, `src/server/tls_credentials.cpp`, `src/server/tls_psk.cpp` | `src/httpserver/server/tls_credentials.hpp`, `src/httpserver/detail/tls_psk_state.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/server/tls_peer_metadata.hpp` | `src/httpserver/server/tls_peer_metadata.hpp` | `src/httpserver/server/tls_peer_metadata.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/websocket/message.hpp` | `src/httpserver/websocket/message.hpp` | `src/httpserver/websocket/message.hpp` | Declarations/representation and justified inline exceptions; no substantive runtime body requires another unit |
| `src/httpserver/websocket/options.hpp` | `src/httpserver/websocket/options.hpp`, `src/websocket/options.cpp` | `src/httpserver/websocket/options.hpp` | Compiled non-template operations; retained inline exceptions are listed below |
| `src/httpserver/websocket/session.hpp` | `src/httpserver/websocket/session.hpp`, `src/websocket/session.cpp` | `src/httpserver/websocket/session.hpp` | Compiled non-template operations; retained inline exceptions are listed below |

## Additional compiled units and shared headers

| New file | Owning header(s) | Disposition |
|---|---|---|
| `src/detail/connection_engine_config.cpp` | `src/httpserver/detail/connection_engine.hpp` | Compiled operations extracted without algorithm changes |
| `src/detail/io_poll_backend_timers.cpp` | `src/httpserver/detail/io_poll_backend.hpp`, `src/httpserver/detail/io_timer_sweep.hpp` | Compiled operations extracted without algorithm changes |
| `src/httpserver/server/protocol_limits.hpp` | `src/httpserver/server/protocol_limits.hpp` | Installed capacity option/report value types; implementation in public options/server units |
| `src/httpserver/detail/protocol_limits.hpp` | `src/httpserver/detail/protocol_limits.hpp` | Private immutable plan and planning declarations |
| `src/detail/protocol_limits.cpp` | `src/httpserver/detail/protocol_limits.hpp` | Common semantic capacity resolution and compiled availability |
| `src/detail/http3_capacity.cpp` | `src/httpserver/detail/protocol_limits.hpp` | Pure prepaid H3/QUIC envelope using owner accounting helpers |
| `src/httpserver/detail/capacity_math.hpp` | `src/httpserver/detail/capacity_math.hpp` | Private checked arithmetic primitives |

## Retained inline definitions

Defaulted/deleted special members and abstract declarations keep their original
implicit/trivial behavior. The table records every remaining explicitly defined
operation, including true templates and constant evaluation operations.

| Header | Operation | Reason to remain inline |
|---|---|---|
| `src/httpserver/http/outcome.hpp` | `httpserver::http::outcome::outcome()` | constexpr/consteval contract |
| `src/httpserver/http/outcome.hpp` | `httpserver::http::outcome::outcome(outcome_code, std::string)` | trivial accessor/value operation |
| `src/httpserver/http/outcome.hpp` | `httpserver::http::outcome::ok()` | constexpr/consteval contract |
| `src/httpserver/http/outcome.hpp` | `httpserver::http::outcome::code()` | constexpr/consteval contract |
| `src/httpserver/http/outcome.hpp` | `httpserver::http::outcome::message()` | trivial accessor/value operation |
| `src/httpserver/http/outcome.hpp` | `httpserver::http::outcome::okay()` | trivial accessor/value operation |
| `src/httpserver/http/protocol.hpp` | `httpserver::http::to_string(protocol)` | constexpr/consteval contract |
| `src/httpserver/http/protocol.hpp` | `httpserver::http::parse(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::detail::is_token(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::detail::ascii_upper(const char)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method::known(method_id)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method::parse(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method::id()` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method::is_extension()` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method::valid()` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method::name()` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::operator==(const method &, const method &)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::operator!=(const method &, const method &)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method::method(method_id)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method::method(method_id, std::string)` | trivial accessor/value operation |
| `src/httpserver/http/method.hpp` | `httpserver::http::to_string(const method &)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method_set::set(method_id)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method_set::contains(const method &)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method_set::any()` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method_set::merge(const method_set &)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::operator==(const method_set &, const method_set &)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::operator!=(const method_set &, const method_set &)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method_set::is_known(method_id)` | constexpr/consteval contract |
| `src/httpserver/http/method.hpp` | `httpserver::http::method_set::bit(method_id)` | constexpr/consteval contract |
| `src/httpserver/http/fields.hpp` | `httpserver::http::fields::empty()` | trivial accessor/value operation |
| `src/httpserver/http/fields.hpp` | `httpserver::http::fields::size()` | trivial accessor/value operation |
| `src/httpserver/http/fields.hpp` | `httpserver::http::operator!=(const fields &, const fields &)` | trivial accessor/value operation |
| `src/httpserver/http/fields.hpp` | `httpserver::http::fields::ascii_lower(const char)` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::from_code(std::uint16_t)` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::with_shoutcast()` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::is_shoutcast()` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::code()` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::valid()` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::informational()` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::success()` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::redirection()` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::client_error()` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::server_error()` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::operator==(const status &, const status &)` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::operator!=(const status &, const status &)` | constexpr/consteval contract |
| `src/httpserver/http/status.hpp` | `httpserver::http::status::status(std::uint16_t)` | constexpr/consteval contract |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::concurrency::unique_function<R (Args...)>::unique_function<type-parameter-0-0 (type-parameter-0-1...)>(F &&)` | template instantiation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::concurrency::unique_function<R (Args...)>::operator=(std::nullptr_t)` | template instantiation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::concurrency::unique_function<R (Args...)>::operator bool()` | template instantiation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::concurrency::unique_function<R (Args...)>::operator()(Args...)` | template instantiation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::concurrency::unique_function<R (Args...)>::model<F>::model<F>(F)` | template instantiation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::concurrency::unique_function<R (Args...)>::model<F>::invoke(Args...)` | template instantiation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::executor::~executor()` | trivial accessor/value operation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::executor::executor()` | trivial accessor/value operation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::current_executor()` | trivial accessor/value operation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::inline_executor::is_current()` | trivial accessor/value operation |
| `src/httpserver/concurrency/executor.hpp` | `httpserver::manual_executor::is_current()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_frame_base::state()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_frame_base::frame_executor()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_frame_base::frame_witness_ptr()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_frame_base::await_transform(AW &&)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_frame_base::self_handle()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::final_awaiter::await_ready()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::final_awaiter::await_suspend(std::coroutine_handle<Promise>)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::final_awaiter::await_resume()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<T>::has_value()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<T>::is_exception()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<T>::has_outcome()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<T>::value()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<T>::value()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<T>::value()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<T>::exception()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<T>::outcome()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<T>::task_result<T>(detail::task_kind, std::optional<T>, std::exception_ptr, http::outcome_code)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<void>::has_value()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<void>::is_exception()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<void>::has_outcome()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<void>::exception()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task_result<void>::outcome()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::spawn_block<T>::run()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::spawn_block<void>::run()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_awaiter<T>::task_awaiter<T>(handle_type)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_awaiter<T>::bind_frame(task_frame_base *)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_awaiter<T>::await_ready()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_awaiter<T>::await_suspend(std::coroutine_handle<>)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_awaiter<T>::await_resume()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::task_awaiter<T>::fail_consumer(const char *)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::promise_type::promise_type()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::promise_type::get_return_object()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::promise_type::initial_suspend()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::promise_type::final_suspend()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::promise_type::return_value(V &&)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::promise_type::unhandled_exception()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::promise_type::pack_result(void *, void *)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::task<T>(task<T> &&)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::operator=(task<T> &&)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::~task<T>()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::valid()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::operator co_await()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::release()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::task<T>(std::coroutine_handle<promise_type>)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<T>::destroy_frame()` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::promise_type::get_return_object()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::promise_type::initial_suspend()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::promise_type::final_suspend()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::promise_type::return_void()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::promise_type::unhandled_exception()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::task(task<void> &&)` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::~task()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::valid()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::operator co_await()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::release()` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::task<void>::task(std::coroutine_handle<promise_type>)` | trivial accessor/value operation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::detail::spawn_impl(executor &, task<T>, concurrency::unique_function<void (task_result<T>)>)` | template instantiation |
| `src/httpserver/concurrency/task.hpp` | `httpserver::spawn(executor &, task<T>, concurrency::unique_function<void (task_result<std::type_identity_t<T>>)>)` | template instantiation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_source::request_stop()` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_source::stop_requested()` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_source::stop_possible()` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_token::stop_requested()` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_token::stop_possible()` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_token::cancellation_awaiter::cancellation_awaiter(std::stop_token)` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_token::cancellation_awaiter::await_ready()` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_token::cancellation_awaiter::fire::operator()()` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_token::cancellation_awaiter::claim()` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_token::cancelled()` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_token::stop_token(std::stop_token)` | trivial accessor/value operation |
| `src/httpserver/concurrency/cancellation.hpp` | `httpserver::stop_source::get_token()` | trivial accessor/value operation |
| `src/httpserver/concurrency/resume_signal.hpp` | `httpserver::detail::timer_queue::ticket::operator bool()` | trivial accessor/value operation |
| `src/httpserver/concurrency/resume_signal.hpp` | `httpserver::detail::timer_queue::timer_queue()` | trivial accessor/value operation |
| `src/httpserver/concurrency/resume_signal.hpp` | `httpserver::resume_signal::signal()` | trivial accessor/value operation |
| `src/httpserver/concurrency/resume_signal.hpp` | `httpserver::resume_signal::cancel()` | trivial accessor/value operation |
| `src/httpserver/concurrency/resume_signal.hpp` | `httpserver::resume_waiter::await_resume()` | trivial accessor/value operation |
| `src/httpserver/concurrency/resume_signal.hpp` | `httpserver::resume_signal::wait()` | trivial accessor/value operation |
| `src/httpserver/concurrency/resume_signal.hpp` | `httpserver::resume_signal::wait_for(std::chrono::steady_clock::duration)` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::to_string(resource)` | constexpr/consteval contract |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::max_capacity(resource)` | constexpr/consteval contract |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::default_capacity(resource)` | constexpr/consteval contract |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::budget_limits::budget_limits()` | constexpr/consteval contract |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::budget_limits::get(resource)` | constexpr/consteval contract |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::budget_limits::set(resource, std::size_t)` | constexpr/consteval contract |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::detail::budget_node::capacity(resource)` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::detail::budget_node::in_use(resource)` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::reservation::reservation()` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::reservation::~reservation()` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::reservation::owns()` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::reservation::kind()` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::reservation::units()` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::resource_budget::resource_budget()` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::resource_budget::root(const budget_limits &)` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::resource_budget::valid()` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::resource_budget::capacity(resource)` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::resource_budget::in_use(resource)` | trivial accessor/value operation |
| `src/httpserver/server/budgets.hpp` | `httpserver::server::resource_budget::resource_budget(std::shared_ptr<detail::budget_node>)` | trivial accessor/value operation |
| `src/httpserver/server/diagnostics.hpp` | `httpserver::server::diagnostics::diagnostics()` | trivial accessor/value operation |
| `src/httpserver/server/hooks.hpp` | `httpserver::server::hook_action::hook_action()` | trivial accessor/value operation |
| `src/httpserver/server/hooks.hpp` | `httpserver::server::hook_action::~hook_action()` | trivial accessor/value operation |
| `src/httpserver/server/hooks.hpp` | `httpserver::server::hook_action::pass()` | trivial accessor/value operation |
| `src/httpserver/server/hooks.hpp` | `httpserver::server::hook_action::is_pass()` | trivial accessor/value operation |
| `src/httpserver/server/hooks.hpp` | `httpserver::server::hook_handle::armed()` | trivial accessor/value operation |
| `src/httpserver/server/hooks.hpp` | `httpserver::server::hook_bus::add(F &&)` | template instantiation |
| `src/httpserver/server/hooks.hpp` | `httpserver::server::hook_bus::any_hooks(hook_phase)` | trivial accessor/value operation |
| `src/httpserver/server/hooks.hpp` | `httpserver::server::hook_bus::fire(typename detail::phase_ctx_t<P> &)` | template instantiation |
| `src/httpserver/server/hooks.hpp` | `httpserver::server::hook_bus::fire(typename detail::phase_ctx_t<P> &, std::exception_ptr &)` | template instantiation |
| `src/httpserver/server/options.hpp` | `httpserver::server::protocol_set::protocol_set()` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::protocol_set::contains(http::protocol)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::protocol_set::enable(http::protocol)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::protocol_set::disable(http::protocol)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::protocol_set::mask(http::protocol)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::is_digit(char)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::valid_ipv4_field(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::valid_ipv4(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::hex_value(char)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::valid_ipv6_group(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::ipv6_field_weight(std::string_view, bool)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::count_ipv6_groups(std::string_view, bool)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::valid_ipv6(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::valid_listen_address(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::same_listener(const listener_options &, const listener_options &)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::multi_version_enabled(const protocol_set &)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::valid_prefix_bits(std::string_view, unsigned int)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::valid_v4_wildcard(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::valid_peer_pattern(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::detail::valid_client_certificate_mode(tls_client_certificate_mode)` | constexpr/consteval contract |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::diagnostic_channel()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::diagnostic_channel()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::add_listener(listener_options)` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::listener_count()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::listener(std::size_t)` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::loop()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::loop()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::concurrency()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::concurrency()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::timeouts()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::timeouts()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::tls()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::tls()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::protocols()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::protocols()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::budgets()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::budgets()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::not_found_response(response_factory)` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::not_found_response()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::method_not_allowed_response(response_factory)` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::method_not_allowed_response()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::peer_policy()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::peer_policy()` | trivial accessor/value operation |
| `src/httpserver/server/routes.hpp` | `httpserver::server::detail::is_name_byte(char)` | constexpr/consteval contract |
| `src/httpserver/server/routes.hpp` | `httpserver::server::detail::valid_parameter_name(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/server/routes.hpp` | `httpserver::server::route_pattern::text()` | trivial accessor/value operation |
| `src/httpserver/server/routes.hpp` | `httpserver::server::route_pattern::segment_count()` | trivial accessor/value operation |
| `src/httpserver/server/routes.hpp` | `httpserver::server::route_pattern::parameter_count()` | trivial accessor/value operation |
| `src/httpserver/server/routes.hpp` | `httpserver::server::route_registry::size()` | trivial accessor/value operation |
| `src/httpserver/server/routes.hpp` | `httpserver::server::route_registry::budget()` | trivial accessor/value operation |
| `src/httpserver/server/routes.hpp` | `httpserver::server::route_registry::route_registry(resource_budget)` | trivial accessor/value operation |
| `src/httpserver/server/readiness.hpp` | `httpserver::server::socket_key::socket_key(std::uint64_t)` | constexpr/consteval contract |
| `src/httpserver/server/readiness.hpp` | `httpserver::server::readiness_driver::~readiness_driver()` | trivial accessor/value operation |
| `src/httpserver/body_reader.hpp` | `httpserver::detail::body_source::~body_source()` | trivial accessor/value operation |
| `src/httpserver/body_reader.hpp` | `httpserver::detail::body_wait::await_ready()` | trivial accessor/value operation |
| `src/httpserver/body_reader.hpp` | `httpserver::detail::body_wait::await_resume()` | trivial accessor/value operation |
| `src/httpserver/body_reader.hpp` | `httpserver::detail::body_wait::cancel_token()` | trivial accessor/value operation |
| `src/httpserver/body_reader.hpp` | `httpserver::body_reader::body_reader(stop_token)` | trivial accessor/value operation |
| `src/httpserver/body_reader.hpp` | `httpserver::body_reader::close()` | trivial accessor/value operation |
| `src/httpserver/body_reader.hpp` | `httpserver::body_reader::note_disconnect(const http::outcome &)` | trivial accessor/value operation |
| `src/httpserver/body_reader.hpp` | `httpserver::body_reader::bodyless()` | trivial accessor/value operation |
| `src/httpserver/body_reader.hpp` | `httpserver::body_reader::collect_want(std::uint64_t)` | trivial accessor/value operation |
| `src/httpserver/response_writer.hpp` | `httpserver::detail::body_sink::~body_sink()` | trivial accessor/value operation |
| `src/httpserver/response_writer.hpp` | `httpserver::detail::body_write_wait::await_ready()` | trivial accessor/value operation |
| `src/httpserver/response_writer.hpp` | `httpserver::detail::body_write_wait::await_resume()` | trivial accessor/value operation |
| `src/httpserver/response_writer.hpp` | `httpserver::detail::body_write_wait::cancel_token()` | trivial accessor/value operation |
| `src/httpserver/response_writer.hpp` | `httpserver::response_writer::response_writer(stop_token)` | trivial accessor/value operation |
| `src/httpserver/response_writer.hpp` | `httpserver::response_writer::close()` | trivial accessor/value operation |
| `src/httpserver/response_writer.hpp` | `httpserver::response_writer::note_disconnect(const http::outcome &)` | trivial accessor/value operation |
| `src/httpserver/websocket/session.hpp` | `httpserver::websocket::session::session(std::shared_ptr<detail::session_state>)` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::detail::exchange_sink::~exchange_sink()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::detail::exchange_sink::on_start_response(const http::status &, const http::fields &)` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::head()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::state()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::terminal()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::suspended()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::disconnected()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::disconnect_reason()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::connection_id()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::peer()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::tls()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::body()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::writer()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::cancellation()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::set_path_args(std::vector<server::route_captures>)` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::path_args()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::concurrent_flag::concurrent_flag(const concurrent_flag &)` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::concurrent_flag::operator bool()` | trivial accessor/value operation |
| `src/httpserver/exchange.hpp` | `httpserver::exchange::closed_failure()` | trivial accessor/value operation |
| `src/httpserver/response_sources.hpp` | `httpserver::body_lease::body_lease(std::shared_ptr<T>)` | template instantiation |
| `src/httpserver/response_sources.hpp` | `httpserver::body_lease::valid()` | trivial accessor/value operation |
| `src/httpserver/response_sources.hpp` | `httpserver::detail::transferred_handle::transferred_handle(std::FILE *, owned_close_fn)` | trivial accessor/value operation |
| `src/httpserver/response_sources.hpp` | `httpserver::detail::transferred_handle::~transferred_handle()` | trivial accessor/value operation |
| `src/httpserver/response_sources.hpp` | `httpserver::detail::transferred_handle::get()` | trivial accessor/value operation |
| `src/httpserver/response_sources.hpp` | `httpserver::detail::send_cursor::file_size()` | trivial accessor/value operation |
| `src/httpserver/response_definition.hpp` | `httpserver::response_definition::owned_file(const http::status &, http::fields, std::FILE *, response_definition &)` | trivial accessor/value operation |
| `src/httpserver/response_definition.hpp` | `httpserver::response_definition::owned_pipe(const http::status &, http::fields, std::FILE *, response_definition &)` | trivial accessor/value operation |
| `src/httpserver/response_definition.hpp` | `httpserver::response_definition::kind()` | trivial accessor/value operation |
| `src/httpserver/response_definition.hpp` | `httpserver::response_definition::valid()` | trivial accessor/value operation |
| `src/httpserver/auth/digest_auth.hpp` | `httpserver::auth::digest_auth_test_access::settle(const digest_auth_policy &, digest_auth_verdict &, digest_auth_result, digest_auth_policy::entropy_fill)` | trivial accessor/value operation |
| `src/httpserver/forms/urlencoded.hpp` | `httpserver::forms::form_fields::form_fields(std::vector<std::pair<std::string, std::string>>)` | trivial accessor/value operation |
| `src/httpserver/forms/urlencoded.hpp` | `httpserver::forms::form_fields::entries()` | trivial accessor/value operation |
| `src/httpserver/forms/urlencoded.hpp` | `httpserver::forms::form_fields::size()` | trivial accessor/value operation |
| `src/httpserver/detail/auth_text.hpp` | `httpserver::detail::auth_text::lowered(char)` | trivial accessor/value operation |
| `src/httpserver/detail/base64.hpp` | `httpserver::detail::octet(const std::byte)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_head_limits.hpp` | `httpserver::detail::http1_head_budget::from_budget_limits(const server::budget_limits &)` | small aggregate value factory from constexpr budget accessors |
| `src/httpserver/detail/resource_charge.hpp` | `httpserver::detail::resource_charge::resource_charge(const server::resource_budget &, server::resource)` | trivial accessor/value operation |
| `src/httpserver/detail/resource_charge.hpp` | `httpserver::detail::resource_charge::~resource_charge()` | trivial accessor/value operation |
| `src/httpserver/detail/resource_charge.hpp` | `httpserver::detail::resource_charge::units()` | trivial accessor/value operation |
| `src/httpserver/detail/unescape_helpers.hpp` | `httpserver::detail::hex_digit_value(char)` | constexpr/consteval contract |
| `src/httpserver/detail/http1_target.hpp` | `httpserver::detail::http1_target::is_http_scheme(std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_target.hpp` | `httpserver::detail::http1_target::keeps_segment(std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_head_parser.hpp` | `httpserver::detail::http1_head_parser::state()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_head_parser.hpp` | `httpserver::detail::http1_head_parser::close_policy()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_head_parser.hpp` | `httpserver::detail::http1_head_parser::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_head_parser.hpp` | `httpserver::detail::detail_head::is_ctl(char)` | constexpr/consteval contract |
| `src/httpserver/detail/http1_head_parser.hpp` | `httpserver::detail::detail_head::is_ows(char)` | constexpr/consteval contract |
| `src/httpserver/detail/http1_head_parser.hpp` | `httpserver::detail::detail_head::ascii_lower(const char)` | constexpr/consteval contract |
| `src/httpserver/detail/http1_head_parser.hpp` | `httpserver::detail::http1_head_parser::residue_size()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_mode.hpp` | `httpserver::detail::http1_body_mode::ascii_iequals(std::string_view, std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::trailers()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::close_policy()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::kind()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::message_complete()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::length_remaining()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::staged_bytes()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::is_ascii_alnum(const char)` | constexpr/consteval contract |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::is_qdtext(const char)` | constexpr/consteval contract |
| `src/httpserver/detail/http1_body_decoder.hpp` | `httpserver::detail::http1_body_decoder::is_quoted_pair_target(const char)` | constexpr/consteval contract |
| `src/httpserver/detail/http1_body_source.hpp` | `httpserver::detail::http1_body_source::http1_body_source(const http1_body_mode &, http1_body_budget, std::function<void ()>, server::resource_budget)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_source.hpp` | `httpserver::detail::http1_body_source::trailers()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_body_source.hpp` | `httpserver::detail::http1_body_source::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_framer.hpp` | `httpserver::detail::http1_response_framer::http1_response_framer(clock_source)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_framer.hpp` | `httpserver::detail::http1_response_framer::mode()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_framer.hpp` | `httpserver::detail::http1_response_framer::failed()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_framer.hpp` | `httpserver::detail::http1_response_framer::append_raw(std::string &, std::span<const std::byte>)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_framer.hpp` | `httpserver::detail::http1_response_framer::hex_digit(std::size_t)` | constexpr/consteval contract |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::is_tls_control(io_op_kind)` | constexpr/consteval contract |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::is_udp(io_op_kind)` | constexpr/consteval contract |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::kind()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::owner()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::connection()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::payload()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::sequence()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::set_sequence(std::uint64_t)` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::mark_submitted()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::mark_awaited()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::is_terminal()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_state::applied()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::io_backend::~io_backend()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_awaiter::op_awaiter(std::shared_ptr<op_state>)` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_awaiter::await_ready()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_awaiter::await_resume()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_handle::kind()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_handle::connection()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_handle::sequence()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_handle::is_terminal()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_handle::state()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_handle::op_handle(std::shared_ptr<op_state>)` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::op_handle::~op_handle()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::read_operation::buffer()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::write_operation::bytes()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::timer_operation::deadline()` | trivial accessor/value operation |
| `src/httpserver/detail/io_operation.hpp` | `httpserver::detail::cancel_operation::target()` | trivial accessor/value operation |
| `src/httpserver/detail/io_connection_owner.hpp` | `httpserver::detail::datagram_delivery::datagram_delivery(std::shared_ptr<datagram_sink>)` | trivial accessor/value operation |
| `src/httpserver/detail/io_connection_owner.hpp` | `httpserver::detail::io_connection_owner::datagram_port::datagram_port(std::weak_ptr<shared_state>, executor *)` | trivial accessor/value operation |
| `src/httpserver/detail/io_connection_owner.hpp` | `httpserver::detail::io_connection_owner::datagrams()` | trivial accessor/value operation |
| `src/httpserver/detail/io_connection_owner.hpp` | `httpserver::detail::io_connection_owner::draining()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_outbox.hpp` | `httpserver::detail::http1_response_sink::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_outbox.hpp` | `httpserver::detail::http1_response_sink::http1_response_sink(http1_response_outbox &, stop_token)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_outbox.hpp` | `httpserver::detail::http1_response_outbox::failed_result()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_outbox.hpp` | `httpserver::detail::http1_response_sink::push(std::span<const std::byte>)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_outbox.hpp` | `httpserver::detail::http1_response_sink::push_end(const http::fields &)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_outbox.hpp` | `httpserver::detail::http1_response_sink::park(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/http1_response_outbox.hpp` | `httpserver::detail::http1_response_sink::unpark(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/lifecycle_sink.hpp` | `httpserver::detail::lifecycle_sink::begin(provenance)` | trivial accessor/value operation |
| `src/httpserver/detail/lifecycle_sink.hpp` | `httpserver::detail::lifecycle_sink::responded()` | trivial accessor/value operation |
| `src/httpserver/detail/lifecycle_sink.hpp` | `httpserver::detail::lifecycle_sink::refused()` | trivial accessor/value operation |
| `src/httpserver/detail/lifecycle_sink.hpp` | `httpserver::detail::lifecycle_sink::committed_status()` | trivial accessor/value operation |
| `src/httpserver/detail/lifecycle_sink.hpp` | `httpserver::detail::lifecycle_sink::aborted()` | trivial accessor/value operation |
| `src/httpserver/detail/lifecycle_sink.hpp` | `httpserver::detail::lifecycle_sink::on_admit(const body_policy &)` | trivial accessor/value operation |
| `src/httpserver/detail/lifecycle_sink.hpp` | `httpserver::detail::lifecycle_sink::on_respond(const http::status &, const http::fields &)` | trivial accessor/value operation |
| `src/httpserver/detail/lifecycle_sink.hpp` | `httpserver::detail::lifecycle_sink::on_start_response(const http::status &, const http::fields &)` | trivial accessor/value operation |
| `src/httpserver/detail/io_poll_sys.hpp` | `httpserver::detail::pollsys::wake_source::read_handle()` | trivial accessor/value operation |
| `src/httpserver/detail/io_poll_sys.hpp` | `httpserver::detail::pollsys::wake_source::valid()` | trivial accessor/value operation |
| `src/httpserver/detail/io_socket_backend.hpp` | `httpserver::detail::io_socket_backend::begin_retirement()` | trivial accessor/value operation |
| `src/httpserver/detail/io_socket_backend.hpp` | `httpserver::detail::io_socket_backend::retirement_progress(std::chrono::milliseconds)` | trivial accessor/value operation |
| `src/httpserver/detail/connection_engine.hpp` | `httpserver::detail::connection_engine::id()` | trivial accessor/value operation |
| `src/httpserver/detail/connection_engine.hpp` | `httpserver::detail::wake_body_sink::wake_body_sink(connection_engine &)` | trivial accessor/value operation |
| `src/httpserver/detail/connection_engine.hpp` | `httpserver::detail::wake_body_sink::bind(body_sink &)` | trivial accessor/value operation |
| `src/httpserver/detail/connection_engine.hpp` | `httpserver::detail::wake_body_sink::ended()` | trivial accessor/value operation |
| `src/httpserver/detail/connection_engine.hpp` | `httpserver::detail::wake_body_sink::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/connection_engine.hpp` | `httpserver::detail::wake_body_sink::park(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/connection_engine.hpp` | `httpserver::detail::wake_body_sink::unpark(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/diagnostics_state.hpp` | `httpserver::server::diagnostics::state::state(callback)` | trivial accessor/value operation |
| `src/httpserver/detail/digest_ledger.hpp` | `httpserver::detail::digest::digest_ledger::digest_ledger(std::size_t)` | trivial accessor/value operation |
| `src/httpserver/detail/sha256.hpp` | `httpserver::detail::sha256_rotate_right(std::uint32_t, int)` | constexpr/consteval contract |
| `src/httpserver/detail/sha256.hpp` | `httpserver::detail::sha256_sigma0(std::uint32_t)` | constexpr/consteval contract |
| `src/httpserver/detail/sha256.hpp` | `httpserver::detail::sha256_sigma1(std::uint32_t)` | constexpr/consteval contract |
| `src/httpserver/detail/sha256.hpp` | `httpserver::detail::sha256_big_sigma0(std::uint32_t)` | constexpr/consteval contract |
| `src/httpserver/detail/sha256.hpp` | `httpserver::detail::sha256_big_sigma1(std::uint32_t)` | constexpr/consteval contract |
| `src/httpserver/detail/sha256.hpp` | `httpserver::detail::sha256_choose(std::uint32_t, std::uint32_t, std::uint32_t)` | constexpr/consteval contract |
| `src/httpserver/detail/sha256.hpp` | `httpserver::detail::sha256_majority(std::uint32_t, std::uint32_t, std::uint32_t)` | constexpr/consteval contract |
| `src/httpserver/detail/digest_params.hpp` | `httpserver::detail::digest::params_detail::is_ows(char)` | trivial accessor/value operation |
| `src/httpserver/detail/md5.hpp` | `httpserver::detail::md5_rotate_left(std::uint32_t, int)` | constexpr/consteval contract |
| `src/httpserver/detail/md5.hpp` | `httpserver::detail::md5_word_index(const int)` | constexpr/consteval contract |
| `src/httpserver/detail/md5.hpp` | `httpserver::detail::md5_step(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, int)` | constexpr/consteval contract |
| `src/httpserver/detail/digest_response.hpp` | `httpserver::detail::digest::md5_hex(std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/digest_response.hpp` | `httpserver::detail::digest::sha256_hex(std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/digest_response.hpp` | `httpserver::detail::digest::compute_ha1(const hash_descriptor &, std::string_view, std::string_view, std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/digest_response.hpp` | `httpserver::detail::digest::compute_ha2(const hash_descriptor &, std::string_view, std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/digest_response.hpp` | `httpserver::detail::digest::compute_response_qop(const hash_descriptor &, std::string_view, std::string_view, std::string_view, std::string_view, std::string_view, std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/digest_response.hpp` | `httpserver::detail::digest::compute_response_legacy(const hash_descriptor &, std::string_view, std::string_view, std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/drain_scope.hpp` | `httpserver::detail::drain_scope::drain_scope(server::diagnostics)` | trivial accessor/value operation |
| `src/httpserver/detail/drain_scope.hpp` | `httpserver::detail::drain_scope::unit::unit(drain_scope &)` | trivial accessor/value operation |
| `src/httpserver/detail/drain_scope.hpp` | `httpserver::detail::drain_scope::unit::~unit()` | trivial accessor/value operation |
| `src/httpserver/detail/drain_ticket_state.hpp` | `httpserver::server::drain_ticket::impl::impl(std::shared_ptr<httpserver::detail::drain_scope>)` | trivial accessor/value operation |
| `src/httpserver/detail/exchange_runner.hpp` | `httpserver::detail::recording_sink::on_abort()` | trivial accessor/value operation |
| `src/httpserver/detail/forms_multipart.hpp` | `httpserver::detail::multipart_params::is_hspace(char)` | trivial accessor/value operation |
| `src/httpserver/detail/forms_multipart.hpp` | `httpserver::detail::multipart_decoder::parts_completed()` | trivial accessor/value operation |
| `src/httpserver/detail/forms_urlencoded.hpp` | `httpserver::detail::urlencoded_decoder::urlencoded_decoder(std::uint64_t, std::uint64_t)` | trivial accessor/value operation |
| `src/httpserver/detail/forms_urlencoded.hpp` | `httpserver::detail::urlencoded_decoder::take_fields()` | trivial accessor/value operation |
| `src/httpserver/detail/forms_urlencoded.hpp` | `httpserver::detail::urlencoded_decoder::is_hex(unsigned char)` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_huffman_table.hpp` | `httpserver::detail::hpack_valid_huffman_code(hpack_huffman_code)` | constexpr/consteval contract |
| `src/httpserver/detail/hpack_huffman_table.hpp` | `httpserver::detail::hpack_insert_huffman_code(hpack_huffman_tree &, std::size_t)` | constexpr/consteval contract |
| `src/httpserver/detail/hpack_huffman_table.hpp` | `httpserver::detail::hpack_make_huffman_tree()` | constexpr/consteval contract |
| `src/httpserver/detail/hpack_primitives.hpp` | `httpserver::detail::hpack_status::ok()` | constexpr/consteval contract |
| `src/httpserver/detail/hpack_primitives.hpp` | `httpserver::detail::hpack_status::code()` | constexpr/consteval contract |
| `src/httpserver/detail/hpack_primitives.hpp` | `httpserver::detail::hpack_status::message()` | constexpr/consteval contract |
| `src/httpserver/detail/hpack_primitives.hpp` | `httpserver::detail::hpack_valid_integer_parameters(unsigned int, hpack_integer_limits)` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_static_table.hpp` | `httpserver::detail::hpack_static_lookup(std::uint64_t)` | constexpr/consteval contract |
| `src/httpserver/detail/hpack_static_table.hpp` | `httpserver::detail::hpack_static_find_name(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/detail/hpack_static_table.hpp` | `httpserver::detail::hpack_static_find(std::string_view, std::string_view)` | constexpr/consteval contract |
| `src/httpserver/detail/hpack_dynamic_table.hpp` | `httpserver::detail::hpack_dynamic_table::hpack_dynamic_table(server::resource_budget, std::size_t)` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_dynamic_table.hpp` | `httpserver::detail::hpack_dynamic_table::bytes()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_dynamic_table.hpp` | `httpserver::detail::hpack_dynamic_table::size()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_dynamic_table.hpp` | `httpserver::detail::hpack_dynamic_table::capacity()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_dynamic_table.hpp` | `httpserver::detail::hpack_dynamic_table::insert_entry(std::size_t, std::size_t, MakeEntry)` | template instantiation |
| `src/httpserver/detail/hpack_field_section.hpp` | `httpserver::detail::hpack_field::view()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_field_section.hpp` | `httpserver::detail::hpack_octets(std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_decoder.hpp` | `httpserver::detail::hpack_decoder::hpack_decoder(server::resource_budget)` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_decoder.hpp` | `httpserver::detail::hpack_decoder::acknowledged_maximum()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_decoder.hpp` | `httpserver::detail::hpack_decoder::usable()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_decoder.hpp` | `httpserver::detail::hpack_decoder::table()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_encoder.hpp` | `httpserver::detail::hpack_encoder::hpack_encoder(server::resource_budget)` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_encoder.hpp` | `httpserver::detail::hpack_encoder::usable()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_encoder.hpp` | `httpserver::detail::hpack_encoder::table()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_encoder.hpp` | `httpserver::detail::hpack_encoder::peer_maximum()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_encoder.hpp` | `httpserver::detail::hpack_encoder::encode_sequence(const Range &, View, hpack_section_limits, bool)` | template instantiation |
| `src/httpserver/detail/hpack_encoder.hpp` | `httpserver::detail::hpack_encoder::process(const Range &, View, hpack_section_limits, bool)` | template instantiation |
| `src/httpserver/detail/hpack_connection.hpp` | `httpserver::detail::hpack_connection::hpack_connection(const server::resource_budget &)` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_connection.hpp` | `httpserver::detail::hpack_connection::encoder()` | trivial accessor/value operation |
| `src/httpserver/detail/hpack_connection.hpp` | `httpserver::detail::hpack_connection::decoder()` | trivial accessor/value operation |
| `src/httpserver/detail/http1_host.hpp` | `httpserver::detail::http1_host::alphanumeric(char)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_flow_control.hpp` | `httpserver::detail::http2_window::debit(std::uint32_t)` | small checked window arithmetic primitive |
| `src/httpserver/detail/http2_flow_control.hpp` | `httpserver::detail::http2_window::debit_data(std::uint32_t, bool)` | small checked window arithmetic primitive |
| `src/httpserver/detail/http2_flow_control.hpp` | `httpserver::detail::http2_window::increase(std::uint32_t)` | small checked window arithmetic primitive |
| `src/httpserver/detail/http2_flow_control.hpp` | `httpserver::detail::http2_window::adjust(std::int64_t)` | small checked window arithmetic primitive |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::http2_body_stream(server::resource_budget)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::trailers()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::queued()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::received_total()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::unread()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::receive_capacity()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::admitted()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::receive_ended()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::finished()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::sent_trailers()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::failed()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::observe_progress(executor::handler)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::mutation::mutation(http2_body_stream &)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::mutation::~mutation()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::failed_locked()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_body_stream.hpp` | `httpserver::detail::http2_body_stream::receive_capacity_locked()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_frame.hpp` | `httpserver::detail::http2_frame_parser::http2_frame_parser(server::resource_budget)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_frame.hpp` | `httpserver::detail::http2_frame_parser::maximum_frame_size(std::uint32_t)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_frame.hpp` | `httpserver::detail::http2_frame_parser::header()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_frame.hpp` | `httpserver::detail::http2_frame_parser::payload()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_frame.hpp` | `httpserver::detail::http2_frame_parser::control_payload()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_frame.hpp` | `httpserver::detail::http2_frame_parser::settings()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_frame.hpp` | `httpserver::detail::http2_frame_parser::window_peak()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_frame.hpp` | `httpserver::detail::http2_frame_parser::table_minimum()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_frame.hpp` | `httpserver::detail::http2_frame_parser::peer_settings(const http2_settings &)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::begin_turn()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::processed_stream(std::uint32_t)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::terminate(http2_error)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::release_frame()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::header()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::payload()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::peer_settings()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::connect_protocol_at_frame_start()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::local_settings()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::compression()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::graceful_complete()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::output_idle()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::acknowledged_window()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::peer_window_peak()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::pending_settings()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::settings_deadline()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_connection.hpp` | `httpserver::detail::http2_connection::header_pending()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_dispatch.hpp` | `httpserver::detail::http2_dispatch::http2_dispatch(executor &, const void *)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_dispatch.hpp` | `httpserver::detail::http2_dispatch::on_overflow(handler)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_dispatch.hpp` | `httpserver::detail::http2_dispatch::is_current()` | trivial accessor/value operation |
| `src/httpserver/detail/multiplexed_request_head.hpp` | `httpserver::detail::multiplexed_head::pseudo_fields::append(const Field &, bool)` | template instantiation |
| `src/httpserver/detail/multiplexed_request_head.hpp` | `httpserver::detail::multiplexed_head::trailer_field(std::string_view, std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/multiplexed_request_head.hpp` | `httpserver::detail::multiplexed_head::append_request_fields(std::span<const Field>, http::request_head &, pseudo_fields &, bool)` | template instantiation |
| `src/httpserver/detail/multiplexed_request_head.hpp` | `httpserver::detail::multiplexed_head::convert(std::span<const Field>, http::request_head &, http::protocol, connect_metadata *)` | template instantiation |
| `src/httpserver/detail/resource_scope.hpp` | `httpserver::detail::child_budget(const server::resource_budget &)` | small value factory selecting one budget scope |
| `src/httpserver/detail/request_handler_frames.hpp` | `httpserver::detail::request_handler_frames::route_executor::route_executor(executor &, std::shared_ptr<executor>)` | trivial accessor/value operation |
| `src/httpserver/detail/request_handler_frames.hpp` | `httpserver::detail::request_handler_frames::route_executor::is_current()` | trivial accessor/value operation |
| `src/httpserver/detail/request_handler_frames.hpp` | `httpserver::detail::request_handler_frames::route_executor::running()` | trivial accessor/value operation |
| `src/httpserver/detail/request_handler_frames.hpp` | `httpserver::detail::request_handler_frames::route_executor::disable()` | trivial accessor/value operation |
| `src/httpserver/detail/request_handler_frames.hpp` | `httpserver::detail::request_handler_frames::route_executor::after_resume(handler)` | trivial accessor/value operation |
| `src/httpserver/detail/request_handler_frames.hpp` | `httpserver::detail::request_handler_frames::route_task::~route_task()` | trivial accessor/value operation |
| `src/httpserver/detail/request_handler_frames.hpp` | `httpserver::detail::request_handler_frames::route_task::done()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_helpers::connection_error(http2_error_code, http::outcome_code)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::stream::done()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::stream::on_upgrade(const ws_upgrade_options &)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::stream::http_trailers_allowed(bool)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::stream::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::stream::park(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::stream::unpark(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::bus()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::drain_output_retired()` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::reserve(server::resource, std::size_t, server::reservation &)` | trivial accessor/value operation |
| `src/httpserver/detail/http2_request_state.hpp` | `httpserver::detail::http2_request_engine::state::has_borrowed_output()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::http3_body_stream(server::resource_budget)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::prepare_receive(std::size_t, std::optional<std::uint64_t>)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::admit(std::size_t)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::room()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::end_receive(http::fields)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::observe_progress(executor::handler)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::admitted()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::take_consumed()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::receive_ended()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::discard()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::fail(http::outcome_code)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::on_consumed(std::function<void (std::size_t)>)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::trailers()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::park(body_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::unpark(body_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::prepare_send(std::size_t, std::optional<std::uint64_t>, bool)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::push(std::span<const std::byte>)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::push_end(const http::fields &)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::park(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::unpark(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::retire(std::size_t)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::queued()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::finished()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_body_stream.hpp` | `httpserver::detail::http3_body_stream::sent_trailers()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_primitives.hpp` | `httpserver::detail::qpack_status::ok()` | constexpr/consteval contract |
| `src/httpserver/detail/qpack_primitives.hpp` | `httpserver::detail::qpack_status::code()` | constexpr/consteval contract |
| `src/httpserver/detail/qpack_primitives.hpp` | `httpserver::detail::qpack_status::message()` | constexpr/consteval contract |
| `src/httpserver/detail/qpack_primitives.hpp` | `httpserver::detail::qpack_codec::valid(qpack_integer_limits)` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_primitives.hpp` | `httpserver::detail::qpack_codec::valid_string(unsigned int, std::uint8_t)` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_reference_hold::qpack_reference_hold(qpack_reference_hold &&)` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_reference_hold::~qpack_reference_hold()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_reference_hold::size()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::qpack_dynamic_table(qpack_table_role)` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::initialized()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::role()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::capacity()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::maximum_capacity()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::bytes()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::size()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::insert_count()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::known_received_count()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::encoder_relative(std::uint64_t)` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::section_relative(std::uint64_t, std::uint64_t)` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_dynamic_table.hpp` | `httpserver::detail::qpack_dynamic_table::post_base(std::uint64_t, std::uint64_t)` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_static_table.hpp` | `httpserver::detail::qpack_static_lookup(std::uint64_t)` | constexpr/consteval contract |
| `src/httpserver/detail/qpack_static_table.hpp` | `httpserver::detail::qpack_static_find_name(std::string_view)` | constexpr/consteval contract |
| `src/httpserver/detail/qpack_static_table.hpp` | `httpserver::detail::qpack_static_find(std::string_view, std::string_view)` | constexpr/consteval contract |
| `src/httpserver/detail/qpack_field_section.hpp` | `httpserver::detail::qpack_field::view()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_field_section.hpp` | `httpserver::detail::qpack_octets(std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_cursor::quic_cursor(std::span<const std::byte>)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_cursor::position()` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_cursor::remaining()` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_cursor::code()` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_cursor::fail(quic_codec_code)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_cursor::take(std::uint64_t)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_cursor::fixed(std::size_t)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_writer::quic_writer()` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_writer::quic_writer(std::span<std::byte>)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_writer::position()` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_writer::code()` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_writer::fail(quic_codec_code)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_writer::bytes(std::span<const std::byte>)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_writer::zeros(std::size_t)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_writer::fixed(std::uint64_t, std::size_t)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_writer::reserve(std::size_t)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/quic_codec.hpp` | `httpserver::detail::quic_transactional_write(std::span<std::byte>, Emit)` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/http3_frame.hpp` | `httpserver::detail::h3_error(std::uint64_t, std::string_view, std::uint64_t)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_frame.hpp` | `httpserver::detail::h3_ordered_extent(std::uint64_t, std::size_t, std::uint64_t)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_frame.hpp` | `httpserver::detail::http3_frame_parser::event()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_frame.hpp` | `httpserver::detail::http3_frame_parser::type()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_frame.hpp` | `httpserver::detail::http3_frame_parser::length()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_frame.hpp` | `httpserver::detail::http3_frame_parser::offset()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_frame.hpp` | `httpserver::detail::http3_frame_parser::at_frame_start()` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_decoder.hpp` | `httpserver::detail::qpack_decoder::complete(qpack_status)` | trivial accessor/value operation |
| `src/httpserver/detail/qpack_encoder.hpp` | `httpserver::detail::qpack_encoder::encode_sequence(std::size_t, View, qpack_section_limits, bool)` | template instantiation |
| `src/httpserver/detail/quic_reassembly.hpp` | `httpserver::detail::quic_stream_result::operator bool()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_reassembly.hpp` | `httpserver::detail::quic_reassembly::consumed()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_reassembly.hpp` | `httpserver::detail::quic_reassembly::buffered_bytes()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_reassembly.hpp` | `httpserver::detail::quic_reassembly::ranges()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_ids::quic_stream_ids(quic_endpoint_role)` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_ids::local_role()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::id()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::receive_state()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::send_state()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::final_size()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::highest_received()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::highest_sent()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::consumed()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::buffered_bytes()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::ranges()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_stream_state.hpp` | `httpserver::detail::quic_stream_state::retained_storage()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_connection.hpp` | `httpserver::detail::http3_connection::peer_settings()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_connection.hpp` | `httpserver::detail::http3_connection::goaway_retired()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_connection.hpp` | `httpserver::detail::http3_connection::begin_turn()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_flow_control.hpp` | `httpserver::detail::quic_flow_result::operator bool()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_congestion.hpp` | `httpserver::detail::quic_congestion::snapshot()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_recovery.hpp` | `httpserver::detail::quic_recovery_result::operator bool()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_request_state.hpp` | `httpserver::detail::http3_request_engine::state::stream::done()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_request_state.hpp` | `httpserver::detail::http3_request_engine::state::stream::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_request_state.hpp` | `httpserver::detail::http3_request_engine::state::stream::park(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_request_state.hpp` | `httpserver::detail::http3_request_engine::state::stream::unpark(body_write_wait &)` | trivial accessor/value operation |
| `src/httpserver/detail/http3_request_state.hpp` | `httpserver::detail::http3_request_engine::state::bus()` | trivial accessor/value operation |
| `src/httpserver/detail/http3_request_state.hpp` | `httpserver::detail::http3_request_engine::state::pump_guard::pump_guard(state &)` | trivial accessor/value operation |
| `src/httpserver/detail/io_completion_storage.hpp` | `httpserver::detail::owned_completion_storage::data()` | trivial accessor/value operation |
| `src/httpserver/detail/io_completion_storage.hpp` | `httpserver::detail::owned_completion_storage::size()` | trivial accessor/value operation |
| `src/httpserver/detail/io_managed_socket_backend.hpp` | `httpserver::detail::io_managed_socket_backend::activate_external()` | trivial accessor/value operation |
| `src/httpserver/detail/io_managed_socket_backend.hpp` | `httpserver::detail::io_managed_socket_backend::interests()` | trivial accessor/value operation |
| `src/httpserver/detail/io_managed_socket_backend.hpp` | `httpserver::detail::io_managed_socket_backend::registration::registration(pollsys::native_socket_t, std::uint64_t, bool)` | trivial accessor/value operation |
| `src/httpserver/detail/io_managed_socket_backend.hpp` | `httpserver::detail::io_managed_socket_backend::registration::~registration()` | trivial accessor/value operation |
| `src/httpserver/detail/io_poll_backend.hpp` | `httpserver::detail::io_poll_backend::registration_lifetime::registration_lifetime(pollsys::native_socket_t, server::socket_key)` | trivial accessor/value operation |
| `src/httpserver/detail/io_poll_backend.hpp` | `httpserver::detail::io_poll_backend::registration_lifetime::~registration_lifetime()` | trivial accessor/value operation |
| `src/httpserver/detail/listener_engine.hpp` | `httpserver::detail::listener_engine::bound_port()` | trivial accessor/value operation |
| `src/httpserver/detail/tls_psk.hpp` | `httpserver::detail::secure_bytes::bytes()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_datagram_dispatch.hpp` | `httpserver::detail::quic_datagram_dispatch::cid_registration::cid_registration(std::string, const std::shared_ptr<route> &)` | trivial accessor/value operation |
| `src/httpserver/detail/quic_datagram_dispatch.hpp` | `httpserver::detail::quic_datagram_dispatch::reset_registration::reset_registration(const std::shared_ptr<route> &)` | trivial accessor/value operation |
| `src/httpserver/detail/quic_datagram_dispatch.hpp` | `httpserver::detail::quic_datagram_dispatch::route::route(io_connection_owner::datagram_port, std::shared_ptr<datagram_sink>)` | trivial accessor/value operation |
| `src/httpserver/detail/quic_connection_identity.hpp` | `httpserver::detail::quic_identity_result::operator bool()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_connection_lifecycle.hpp` | `httpserver::detail::quic_connection_lifecycle::state()` | trivial accessor/value operation |
| `src/httpserver/detail/secure_zero.hpp` | `httpserver::detail::secure_zero(void *, std::size_t)` | small zeroization primitive; preserve platform dispatch and volatile barrier |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_suite(quic_cipher_suite)` | small provider-only lookup/byte/predicate primitive |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_temporary<N>::~quic_temporary<N>()` | template instantiation |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_scratch_guard::~quic_scratch_guard()` | small provider-only lookup/byte/predicate primitive |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_data(std::span<const std::byte>)` | small provider-only lookup/byte/predicate primitive |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_data(std::span<std::byte>)` | small provider-only lookup/byte/predicate primitive |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_data(std::array<std::byte, N> &)` | template instantiation |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_overlap(std::span<const std::byte>, std::span<const std::byte>)` | small provider-only lookup/byte/predicate primitive |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_keys_valid(const quic_packet_keys &)` | small provider-only lookup/byte/predicate primitive |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_level_matches(const quic_packet_keys &, quic_packet_kind)` | small provider-only lookup/byte/predicate primitive |
| `src/httpserver/detail/quic_crypto_provider.hpp` | `httpserver::detail::quic_reject_unavailable(const quic_clear_header &, void *)` | small provider-only lookup/byte/predicate primitive |
| `src/httpserver/detail/quic_flow_internal.hpp` | `httpserver::detail::flow_add(std::uint64_t, std::uint64_t)` | trivial accessor/value operation |
| `src/httpserver/detail/quic_key_state.hpp` | `httpserver::detail::quic_key_state::authentication_failures()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_server_admission.hpp` | `httpserver::detail::quic_admission_reply::packet()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_server_admission.hpp` | `httpserver::detail::quic_admission_reply::complete_send()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_server_admission.hpp` | `httpserver::detail::quic_admission_reply::cancel_unsent()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_server_admission.hpp` | `httpserver::detail::quic_server_admission::pending_count()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_server_admission.hpp` | `httpserver::detail::quic_server_admission::retained_bytes()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_server_admission.hpp` | `httpserver::detail::quic_server_admission::routes()` | trivial accessor/value operation |
| `src/httpserver/detail/tls_io_backend.hpp` | `httpserver::detail::tls_handshake_operation::tls_handshake_operation(io_connection_owner &, std::uint64_t, std::chrono::steady_clock::time_point)` | trivial accessor/value operation |
| `src/httpserver/detail/tls_io_backend.hpp` | `httpserver::detail::tls_shutdown_operation::tls_shutdown_operation(io_connection_owner &, std::uint64_t, std::chrono::steady_clock::time_point)` | trivial accessor/value operation |
| `src/httpserver/detail/tls_credentials.hpp` | `httpserver::detail::tls_credentials_snapshot::generation()` | trivial accessor/value operation |
| `src/httpserver/detail/tls_credentials.hpp` | `httpserver::detail::tls_credentials_snapshot::default_host()` | trivial accessor/value operation |
| `src/httpserver/detail/tls_credentials.hpp` | `httpserver::detail::tls_credentials_snapshot::hosts()` | trivial accessor/value operation |
| `src/httpserver/detail/tls_credentials.hpp` | `httpserver::detail::tls_credentials_snapshot::select_acme(std::string_view)` | trivial accessor/value operation |
| `src/httpserver/detail/tls_credentials.hpp` | `httpserver::detail::tls_credentials_snapshot::select_default()` | trivial accessor/value operation |
| `src/httpserver/detail/tls_credentials.hpp` | `httpserver::detail::tls_credentials_registry::tls_credentials_registry(std::function<std::chrono::system_clock::time_point ()>)` | trivial accessor/value operation |
| `src/httpserver/detail/tls_credentials.hpp` | `httpserver::detail::tls_credentials_registry::acquire()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_tls_session.hpp` | `httpserver::detail::quic_tls_result::operator bool()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_network_connection.hpp` | `httpserver::detail::quic_network_connection::connected()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_network_connection.hpp` | `httpserver::detail::quic_network_connection::terminal()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_network_connection.hpp` | `httpserver::detail::quic_network_connection::pending()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_network_connection.hpp` | `httpserver::detail::quic_network_connection::active()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_pacing.hpp` | `httpserver::detail::quic_pacing::quic_pacing(std::size_t)` | trivial accessor/value operation |
| `src/httpserver/detail/quic_path_mtu.hpp` | `httpserver::detail::quic_path_mtu::progress()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_path_mtu.hpp` | `httpserver::detail::quic_path_mtu::deadline()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_path_mtu.hpp` | `httpserver::detail::quic_path_mtu::confirmed()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_tls_callbacks.hpp` | `httpserver::detail::quic_tls_callbacks::failure()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_tls_callbacks.hpp` | `httpserver::detail::quic_tls_callbacks::local_parameters()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_tls_callbacks.hpp` | `httpserver::detail::quic_tls_callbacks::peer_parameters()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_udp_listener.hpp` | `httpserver::detail::quic_udp_listener::bound_port()` | trivial accessor/value operation |
| `src/httpserver/detail/quic_udp_listener.hpp` | `httpserver::detail::quic_udp_listener::retired()` | trivial accessor/value operation |
| `src/httpserver/detail/sha1.hpp` | `httpserver::detail::sha1_rotate_left(std::uint32_t, int)` | constexpr/consteval contract |
| `src/httpserver/detail/sha1.hpp` | `httpserver::detail::sha1_choice(std::uint32_t, std::uint32_t, std::uint32_t)` | constexpr/consteval contract |
| `src/httpserver/detail/sha1.hpp` | `httpserver::detail::sha1_parity(std::uint32_t, std::uint32_t, std::uint32_t)` | constexpr/consteval contract |
| `src/httpserver/detail/sha1.hpp` | `httpserver::detail::sha1_majority(std::uint32_t, std::uint32_t, std::uint32_t)` | constexpr/consteval contract |
| `src/httpserver/detail/tls_psk_state.hpp` | `httpserver::server::tls_psk_key::impl::impl(std::span<const std::byte>)` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_utf8.hpp` | `httpserver::detail::websocket_utf8::complete()` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_codec.hpp` | `httpserver::detail::websocket_codec::websocket_codec(websocket::options)` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_codec.hpp` | `httpserver::detail::websocket_codec::incoming_bytes()` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_codec.hpp` | `httpserver::detail::websocket_codec::incoming_messages()` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_codec.hpp` | `httpserver::detail::websocket_codec::input_ready()` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_codec.hpp` | `httpserver::detail::websocket_codec::has_message()` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_session_state.hpp` | `httpserver::websocket::detail::session_state::session_state(options, session::close_callback)` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_session_state.hpp` | `httpserver::websocket::detail::session_state::input_stopped()` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_session_state.hpp` | `httpserver::websocket::detail::session_operation::admitted()` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_session_state.hpp` | `httpserver::websocket::detail::session_wait::session_wait(std::shared_ptr<session_state>, bool)` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_session_state.hpp` | `httpserver::websocket::detail::session_wait::bind_frame(httpserver::detail::task_frame_base *)` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_session_state.hpp` | `httpserver::websocket::detail::session_wait::await_ready()` | trivial accessor/value operation |
| `src/httpserver/detail/websocket_session_state.hpp` | `httpserver::websocket::detail::session_wait::await_resume()` | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::http2()` (mutable/const) | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::http3()` (mutable/const) | trivial accessor/value operation |
| `src/httpserver/server/options.hpp` | `httpserver::server::server_options::quic()` (mutable/const) | trivial accessor/value operation |
| `src/httpserver/detail/capacity_math.hpp` | `httpserver::detail::capacity_add()` | small checked byte/arithmetic primitive |
| `src/httpserver/detail/capacity_math.hpp` | `httpserver::detail::capacity_multiply()` | small checked byte/arithmetic primitive |
