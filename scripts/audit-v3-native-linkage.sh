#!/usr/bin/env bash
#
# audit-v3-native-linkage.sh — TASK-108 native linkage audit gate
# (PRD-V3N-REQ-001/002: the native TLS-off target depends only on the
# platform and the C++ runtime — no MHD, no TLS library, no curl).
#
# Two independent probes:
#
#   A1. Source scan: the v3 public headers (every header under
#       src/httpserver/ the v3 surface ships) and the v3core engine
#       sources must contain no include (or other reference) to
#       microhttpd / gnutls / openssl / wslay. The v3 native engine is
#       engine-only vocabulary; any backend include here is a leak.
#
#       The designated private provider unit is a separate scoped target.
#   A2. Binary probe: the built native_linkage program (which links
#       libhttpserver_v3core.la and nothing else) permits libssl/libcrypto
#       only in the enabled native lane. Other providers are rejected. Inspected with otool -L on Darwin
#       and ldd elsewhere. Missing artifacts or inspectors fail closed.
#       The native archive is checked independently for static leakage.
#
# Inputs (via env, all optional):
#   BUILD_DIR — build directory holding test/native_linkage;
#               defaults to $REPO_ROOT/build.
#   SRC_DIR   — source root; defaults to the script's parent.
#   V3_TLS_MODE — yes/no; default no. The Make target supplies its configured mode.
#
# Exits 0 (PASS), 1 (FAIL), or 2 (usage error).
# This script is a static check: it starts no servers and opens no ports.

set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${SRC_DIR:-$REPO_ROOT}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"

fail() {
    echo "audit-v3-native-linkage: FAIL — $*" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# A1: banned references in the v3 public headers and v3core sources.
# ---------------------------------------------------------------------------
BANNED_RE='microhttpd|gnutls|openssl|wslay'

V3_PUBLIC_HEADERS="
src/httpserver/features.hpp
src/httpserver/http.hpp
src/httpserver/http/outcome.hpp
src/httpserver/http/protocol.hpp
src/httpserver/http/method.hpp
src/httpserver/http/fields.hpp
src/httpserver/http/request_head.hpp
src/httpserver/http/status.hpp
src/httpserver/concurrency/concurrency.hpp
src/httpserver/concurrency/executor.hpp
src/httpserver/concurrency/task.hpp
src/httpserver/concurrency/cancellation.hpp
src/httpserver/concurrency/resume_signal.hpp
src/httpserver/server/budgets.hpp
src/httpserver/server/options.hpp
src/httpserver/server/tls_credentials.hpp
src/httpserver/server/routes.hpp
src/httpserver/server/configuration.hpp
src/httpserver/server/server.hpp
src/httpserver/server/readiness.hpp
src/httpserver/exchange.hpp
src/httpserver/body_reader.hpp
src/httpserver/response_writer.hpp
src/httpserver/auth/basic_auth.hpp
src/httpserver/auth/digest_auth.hpp
src/httpserver/forms/urlencoded.hpp
src/httpserver/forms/multipart.hpp
src/httpserver/server/hooks.hpp
src/httpserver/net/address.hpp
src/httpserver/server/peer_policy.hpp
src/httpserver/websocket/message.hpp
src/httpserver/websocket/options.hpp
src/httpserver/websocket/session.hpp
"

V3CORE_SOURCES="
src/detail/tls_psk.cpp
src/server/tls_psk.cpp
src/server/tls_credentials.cpp
src/features.cpp
src/detail/io_operation.cpp
src/detail/io_udp_backend.cpp
src/detail/quic_invariant_header.cpp
src/detail/quic_datagram_dispatch.cpp
src/detail/io_connection_owner.cpp
src/detail/fake_io_backend.cpp
src/detail/io_poll_backend.cpp
src/detail/io_epoll_backend.cpp
src/detail/io_kqueue_backend.cpp
src/detail/io_iocp_backend.cpp
src/detail/io_managed_backend.cpp
src/detail/io_managed_socket_backend.cpp
src/detail/io_external_adapter.cpp
src/detail/io_wake_source.cpp
src/detail/worker_pool.cpp
src/detail/connection_engine.cpp
src/detail/connection_engine_request.cpp
src/detail/listener_engine.cpp
src/detail/drain_scope.cpp
src/server/server.cpp
src/auth/basic_auth.cpp
src/auth/digest_auth.cpp
src/forms/urlencoded.cpp
src/forms/multipart.cpp
src/forms/multipart_files.cpp
src/server/hooks.cpp
src/detail/request_lifecycle.cpp
src/net/address.cpp
src/detail/peer_policy.cpp
src/detail/websocket_codec.cpp
src/detail/websocket_driver.cpp
src/detail/websocket_session_state.cpp
src/websocket/session.cpp
src/detail/http1_websocket_handshake.cpp
src/detail/connection_engine_websocket.cpp
src/detail/http2_frame.cpp
src/detail/http2_connection.cpp
src/detail/http2_request_engine.cpp
src/detail/http2_native_stream.cpp
src/detail/http2_native_timers.cpp
src/detail/http2_tcp_owner.cpp
src/detail/http2_request_head.cpp
src/detail/http2_body_stream.cpp
src/detail/http2_response_stream.cpp
src/detail/http2_stream_flow.cpp
src/detail/websocket_handshake.cpp
src/detail/http2_websocket_handshake.cpp
src/detail/http2_websocket_stream.cpp
src/detail/quic_varint.cpp
src/detail/quic_packet.cpp
src/detail/quic_frame.cpp
src/detail/quic_frame_write.cpp
src/detail/quic_transport_parameters.cpp
src/detail/quic_amplification.cpp
src/detail/quic_stream_state.cpp
src/detail/quic_reassembly.cpp
src/detail/quic_ack_state.cpp
src/detail/quic_recovery.cpp
src/detail/quic_recovery_timer.cpp
src/detail/quic_repacketize.cpp
src/detail/quic_flow_control.cpp
src/detail/quic_flow_receive.cpp
src/detail/quic_flow_send.cpp
src/detail/quic_storage.cpp
src/detail/quic_recovery_flow.cpp
src/detail/quic_congestion.cpp
src/detail/quic_pacing.cpp
src/detail/quic_send_schedule.cpp
src/detail/quic_persistent_congestion.cpp
src/detail/quic_recovery_emission.cpp
src/detail/http3_frame.cpp
src/detail/http3_connection.cpp
src/detail/http3_settings.cpp
src/detail/http3_request_head.cpp
src/detail/http3_request_engine.cpp
src/detail/http3_body_stream.cpp
src/detail/http3_response_stream.cpp
src/detail/quic_connection_identity.cpp
src/detail/quic_path_mtu.cpp
src/detail/quic_recovery_mtu.cpp
src/detail/quic_datagram_sizing.cpp
src/detail/quic_recovery_datagram.cpp
src/detail/quic_connection_lifecycle.cpp
src/detail/http3_qpack.cpp
src/detail/http3_control_drain.cpp
src/detail/http3_drain.cpp
src/detail/http3_websocket_handshake.cpp
src/detail/http3_websocket_handshake_stream.cpp
src/detail/http3_websocket_stream.cpp
src/server/diagnostics.cpp
src/http/cookie.cpp
src/detail/http3_native_stream.cpp
src/detail/http3_semantic_stream.cpp
src/http/method.cpp
src/http/fields.cpp
src/server/budgets.cpp
src/server/route_sync.cpp
src/server/routes.cpp
src/server/options.cpp
src/detail/net_address.cpp
src/detail/diagnostics_access.cpp
src/detail/tls_credentials_access.cpp
src/detail/tls_psk_access.cpp
src/detail/hook_bus.cpp
src/response_definition.cpp
src/body_reader.cpp
src/exchange.cpp
src/response_sources.cpp
src/response_writer.cpp
src/server/peer_policy.cpp
src/detail/io_poll_sys.cpp
src/detail/hpack_primitives.cpp
src/detail/qpack_encoder.cpp
src/detail/lifecycle_sink.cpp
src/detail/hpack_decoder.cpp
src/detail/qpack_decoder.cpp
src/detail/qpack_instructions.cpp
src/detail/sha1.cpp
src/detail/secure_compare.cpp
src/detail/http1_response_mode.cpp
src/detail/digest_response.cpp
src/detail/forms_verdict.cpp
src/detail/resource_charge.cpp
src/detail/hpack_encoder.cpp
src/detail/websocket_utf8.cpp
src/detail/quic_recovery_internal.cpp
src/detail/request_handler_frames.cpp
src/detail/http1_error_synth.cpp
src/detail/md5.cpp
src/detail/base64.cpp
src/detail/sha256.cpp
src/detail/http1_body_decoder.cpp
src/detail/http1_body_mode.cpp
src/detail/http1_response_outbox.cpp
src/detail/exchange_runner.cpp
src/detail/http1_response_framer.cpp
src/detail/qpack_field_section.cpp
src/detail/digest_ledger.cpp
src/detail/entropy_sys.cpp
src/detail/digest_nonce.cpp
src/detail/http1_head_parser.cpp
src/detail/http1_body_source.cpp
src/detail/qpack_primitives.cpp
src/detail/qpack_dynamic_table.cpp
src/detail/forms_multipart.cpp
src/detail/digest_hex.cpp
src/detail/http2_request_state.cpp
src/detail/io_timer_sweep.cpp
src/detail/hpack_field_section.cpp
src/detail/forms_urlencoded.cpp
src/detail/auth_text.cpp
src/detail/http1_target.cpp
src/detail/hpack_dynamic_table.cpp
src/detail/digest_params.cpp
src/detail/http3_request_state.cpp
src/detail/quic_flow_internal.cpp
src/detail/multiplexed_request_head.cpp
src/concurrency/cancellation.cpp
src/concurrency/executor.cpp
src/concurrency/task.cpp
src/concurrency/resume_signal.cpp
src/websocket/options.cpp
src/detail/unescape_helpers.cpp
src/detail/http1_host.cpp
src/detail/io_poll_backend_timers.cpp
src/detail/http2_dispatch.cpp
src/detail/connection_engine_config.cpp
src/detail/io_completion_storage.cpp
"

# TASK-114: the in-tree codec/hash/entropy primitives the auth TU and
# the ws/digest milestones consume. Private (never installed), but they
# are part of the native TLS-off surface and stay under the same ban.
# TASK-115 adds the Digest nonce/ledger/parser/response chain.
V3_DETAIL_HEADERS="
src/httpserver/detail/tls_build_probe.hpp
src/httpserver/detail/io_socket_backend.hpp
src/httpserver/detail/io_datagram.hpp
src/httpserver/detail/io_udp_backend.hpp
src/httpserver/detail/quic_invariant_header.hpp
src/httpserver/detail/quic_datagram_dispatch.hpp
src/httpserver/detail/io_epoll_backend.hpp
src/httpserver/detail/io_kqueue_backend.hpp
src/httpserver/detail/io_iocp_backend.hpp
src/httpserver/detail/io_completion_storage.hpp
src/httpserver/detail/io_managed_backend.hpp
src/httpserver/detail/io_managed_socket_backend.hpp
src/httpserver/detail/http1_websocket_handshake.hpp
src/httpserver/detail/websocket_driver.hpp
src/httpserver/detail/websocket_codec.hpp
src/httpserver/detail/websocket_session_state.hpp
src/httpserver/detail/websocket_utf8.hpp
src/httpserver/detail/base64.hpp
src/httpserver/detail/sha1.hpp
src/httpserver/detail/md5.hpp
src/httpserver/detail/sha256.hpp
src/httpserver/detail/secure_compare.hpp
src/httpserver/detail/entropy_sys.hpp
src/httpserver/detail/digest_hex.hpp
src/httpserver/detail/digest_nonce.hpp
src/httpserver/detail/digest_ledger.hpp
src/httpserver/detail/digest_params.hpp
src/httpserver/detail/digest_response.hpp
src/httpserver/detail/auth_text.hpp
src/httpserver/detail/forms_urlencoded.hpp
src/httpserver/detail/forms_multipart.hpp
src/httpserver/detail/forms_verdict.hpp
src/httpserver/detail/lifecycle_sink.hpp
src/httpserver/detail/request_lifecycle.hpp
"

violations=0
for f in $V3_PUBLIC_HEADERS $V3CORE_SOURCES $V3_DETAIL_HEADERS; do
    path="$SRC_DIR/$f"
    if [[ ! -f "$path" ]]; then
        echo "audit-v3-native-linkage: missing source $f" >&2
        violations=$((violations + 1))
        continue
    fi
    while IFS= read -r hit; do
        [[ -z "$hit" ]] && continue
        echo "audit-v3-native-linkage: $f:$hit" >&2
        violations=$((violations + 1))
    done < <(grep -nEi "$BANNED_RE" "$path" || true)
done

if [[ $violations -gt 0 ]]; then
    fail "banned third-party references in the v3 native surface"
fi
echo "audit-v3-native-linkage: A1 PASS — no backend/TLS references in the v3 public headers or v3core sources"

# ---------------------------------------------------------------------------
# A2: the linked audit binary resolves no third-party runtime library.
# ---------------------------------------------------------------------------
MODE="${V3_TLS_MODE:-no}"
case "$MODE" in yes|no) ;; *) fail "invalid native TLS mode: $MODE" ;; esac
# Libtool programs can be shell wrappers. Inspect their real executable.
SUFFIX=""
HOST_SYSTEM="$(uname -s)"
case "$HOST_SYSTEM" in MINGW*|MSYS*|CYGWIN*) SUFFIX=".exe" ;; esac
BIN="$BUILD_DIR/test/native_linkage$SUFFIX"
if [[ -x "$BUILD_DIR/test/.libs/native_linkage$SUFFIX" ]]; then
    BIN="$BUILD_DIR/test/.libs/native_linkage$SUFFIX"
fi
[[ -x "$BIN" ]] || fail "native audit executable missing: $BIN"
ARCHIVE="$BUILD_DIR/src/.libs/libhttpserver_v3core.a"
[[ -f "$ARCHIVE" ]] || fail "native archive missing: $ARCHIVE"
command -v nm >/dev/null 2>&1 || fail "nm is required"

if [[ "$SUFFIX" == .exe ]]; then
    command -v objdump >/dev/null 2>&1 || fail "objdump is required on MinGW64"
    deps="$(objdump -p "$BIN")" || fail "objdump failed"
    [[ "$deps" == *"file format pei"* ]] || fail "malformed PE inspection"
    libraries="$(printf '%s\n' "$deps" | awk '/DLL Name:/ {print $3}')"
elif [[ "$HOST_SYSTEM" == FreeBSD ]]; then
    deps="$(ldd "$BIN" 2>&1)" || fail "ldd failed"
    # Retain resolved paths so the selected C++ runtime and system ABI stay distinct.
    libraries="$(printf '%s\n' "$deps" | awk '/=>/ {print $3} /^[[:space:]]*\/libexec\// {print $1}')"
elif command -v otool >/dev/null 2>&1; then
    deps="$(otool -L "$BIN")" || fail "otool failed"
    libraries="$(printf '%s\n' "$deps" | tail -n +2 | awk '{print $1}')"
elif command -v ldd >/dev/null 2>&1; then
    deps="$(ldd "$BIN" 2>&1)" || fail "ldd failed"
    libraries="$(printf '%s\n' "$deps" | awk '{print $1}')"
elif command -v objdump >/dev/null 2>&1; then
    deps="$(objdump -p "$BIN")" || fail "objdump failed"
    libraries="$(printf '%s\n' "$deps" | awk '/DLL Name:/ {print $3}')"
else
    fail "a platform dependency inspector is required"
fi

[[ -n "$libraries" ]] || fail "dependency inspector returned no libraries"

while IFS= read -r library; do
    [[ -z "$library" ]] && continue
    name="$(basename "$library" | tr '[:upper:]' '[:lower:]')"
    if [[ "$HOST_SYSTEM" == FreeBSD ]]; then
        case "$name" in
            libc++.so*)
                [[ -n "${V3_PACKAGE_TOOLCHAIN:-}" ]] || fail "selected FreeBSD C++ runtime prefix missing"
                toolchain="$(cd "$V3_PACKAGE_TOOLCHAIN" && pwd -P)" || fail "invalid C++ runtime prefix"
                resolved="$(realpath "$library")" || fail "unresolved C++ runtime: $library"
                case "$resolved" in
                    "$toolchain/lib/libc++.so.1"|"$toolchain/lib/libc++.so.1.0") continue ;;
                    *) fail "unselected FreeBSD C++ runtime: $library" ;;
                esac ;;
            libcxxrt.*)
                [[ "$library" == /lib/libcxxrt.so.1 ]] || fail "unselected FreeBSD ABI runtime: $library"
                continue ;;
        esac
    fi
    case "$name" in
        libssl.*|libcrypto.*|libssl-*|libcrypto-*)
            [[ "$MODE" == yes ]] || fail "TLS-off load dependency: $library" ;;
        libmicrohttpd*|libgnutls*|libnettle*|libhogweed*|libwslay*|libcurl*|libmbedtls*|libwolfssl*)
            fail "unexpected third-party load dependency: $library" ;;
        *)
            case "$name" in
                linux-vdso.*|ld-linux*|ld-musl*|ld-elf*.so*|libc.so*|libm.so*|libstdc++.*|libstdc++-*|libgcc_s.*|libgcc_s-*|libgcc_s_seh-1.dll|libwinpthread-*|libpthread.*|libthr.*|libexecinfo.*|libatomic.*|libdl.*|librt.*|libresolv.*|libc++.*|libc++abi.*|libsystem.*|kernel32.dll|ws2_32.dll|msvcrt.dll|ucrtbase.dll|api-ms-win-*.dll|advapi32.dll|bcrypt.dll|bcryptprimitives.dll|crypt32.dll|secur32.dll|user32.dll|kernelbase.dll|ntdll.dll) ;;
                *) fail "unexpected third-party load dependency: $library" ;;
            esac ;;
    esac
done <<< "$libraries"

# Inspect every archive member, including objects not pulled into this consumer.
# Normalize nm output: Mach-O prefixes C symbols with _, GNU nm prints 'U name'.
for artifact in "$BIN" "$ARCHIVE"; do
    symbols="$(nm -u "$artifact" 2>/dev/null)" || fail "nm failed: $artifact"
    names="$(printf '%s\n' "$symbols" | awk '{print $NF}' | sed -E 's/^_*imp_+//; s/^_+//; s/@[0-9]+$//')"
    if printf '%s\n' "$names" | grep -Eq '^(MHD_|gnutls_|nettle_|hogweed_|wslay_|curl_|wolfSSL_|mbedtls_)'; then
        fail "non-native provider symbols in $artifact"
    fi
    if [[ "$MODE" == no ]] && printf '%s\n' "$names" | grep -Eq '^(SSL_|TLS_|DTLS_|OPENSSL_|OpenSSL_|CRYPTO_|EVP_|BIO_|ERR_|OSSL_|X509_|RAND_|BN_|PEM_|ASN1_)'; then
        fail "TLS-off provider symbols in $artifact"
    fi
done
echo "audit-v3-native-linkage: A2 PASS — native TLS=$MODE dependencies and archive symbols"
echo "audit-v3-native-linkage: PASS"
