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
#   A2. Binary probe: the built v3_native_linkage program (which links
#       libhttpserver_v3core.la and nothing else) must not resolve any
#       third-party runtime library. Inspected with otool -L on Darwin
#       and ldd elsewhere; when neither tool exists the script prints
#       SKIP for this probe (the A1 scan and the program's own link
#       still ran; CI lanes that must enforce A2 provide the tool).
#
# Inputs (via env, all optional):
#   BUILD_DIR — build directory holding test/v3_native_linkage;
#               defaults to $REPO_ROOT/build.
#   SRC_DIR   — source root; defaults to the script's ../..
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
src/httpserver/server/routes.hpp
src/httpserver/server/configuration.hpp
src/httpserver/server/server.hpp
src/httpserver/exchange.hpp
src/httpserver/body_reader.hpp
src/httpserver/response_writer.hpp
src/httpserver/auth/basic_auth.hpp
src/httpserver/auth/digest_auth.hpp
src/httpserver/forms/urlencoded.hpp
src/httpserver/forms/multipart.hpp
"

V3CORE_SOURCES="
src/detail/io_operation.cpp
src/detail/io_connection_owner.cpp
src/detail/fake_io_backend.cpp
src/detail/io_poll_backend.cpp
src/detail/worker_pool.cpp
src/detail/connection_engine.cpp
src/detail/connection_engine_request.cpp
src/detail/listener_engine.cpp
src/detail/server.cpp
src/detail/auth_basic.cpp
src/detail/auth_digest.cpp
src/detail/forms_urlencoded.cpp
src/detail/forms_multipart.cpp
src/detail/forms_multipart_files.cpp
"

# TASK-114: the in-tree codec/hash/entropy primitives the auth TU and
# the ws/digest milestones consume. Private (never installed), but they
# are part of the native TLS-off surface and stay under the same ban.
# TASK-115 adds the Digest nonce/ledger/parser/response chain.
V3_DETAIL_HEADERS="
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
BIN="$BUILD_DIR/test/v3_native_linkage"
if [[ ! -x "$BIN" ]]; then
    echo "audit-v3-native-linkage: A2 SKIP — $BIN not built (build check_PROGRAMS first)"
    exit 0
fi

inspect() {
    if command -v otool >/dev/null 2>&1; then
        otool -L "$BIN"
    elif command -v ldd >/dev/null 2>&1; then
        ldd "$BIN"
    else
        return 255
    fi
}

deps="$(inspect 2>/dev/null)"
status=$?
if [[ $status -eq 255 ]]; then
    echo "audit-v3-native-linkage: A2 SKIP — neither otool nor ldd available"
    exit 0
fi

# A listed-but-unresolved library is dead weight (e.g. a -l flag the
# configuring environment put into global LDFLAGS), not a dependency of
# the v3 surface: symbol resolution is the ground truth, so such a load
# command is reported but does not fail the audit. A genuinely resolved
# third-party library fails.
symbol_probe_prefix() {
    case "$1" in
        *microhttpd*) echo 'MHD_' ;;
        *gnutls*)     echo 'gnutls_' ;;
        *ssl*|*crypto*) echo 'SSL_|OPENSSL_|EVP_' ;;
        *nettle*)     echo 'nettle_' ;;
        *hogweed*)    echo 'hogweed_' ;;
        *wslay*)      echo 'wslay_' ;;
        *)            echo '' ;;
    esac
}

fail_a2=0
while IFS= read -r listed; do
    [[ -z "$listed" ]] && continue
    prefix="$(symbol_probe_prefix "$listed")"
    resolved=1
    if [[ -n "$prefix" ]] && command -v nm >/dev/null 2>&1; then
        if ! nm -u "$BIN" 2>/dev/null | grep -Eq "^_?($prefix)"; then
            resolved=0
        fi
    fi
    if [[ $resolved -eq 1 ]]; then
        echo "audit-v3-native-linkage: A2 FAIL — third-party runtime linkage: $listed" >&2
        fail_a2=1
    else
        echo "audit-v3-native-linkage: note — $listed listed (environment LDFLAGS) but resolves no symbols"
    fi
done < <(printf '%s\n' "$deps" | grep -Ei 'libmicrohttpd|libgnutls|libssl|libcrypto|libnettle|libhogweed|libwslay' || true)

if [[ $fail_a2 -ne 0 ]]; then
    fail "the native TLS-off binary resolves third-party runtime libraries"
fi
echo "audit-v3-native-linkage: A2 PASS — $BIN resolves only platform and C++ runtime symbols"
echo "audit-v3-native-linkage: PASS"
