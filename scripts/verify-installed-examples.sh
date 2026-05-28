#!/usr/bin/env bash
#
# verify-installed-examples.sh — compile every example against the installed
# v2.0 headers and library, to prove the suite still works as a stand-alone
# consumer of the public API (TASK-040 action item: "Each example should
# compile against the installed v2.0 headers as a minimal Makefile or CMake
# snippet").
#
# Strategy:
#   1. Build and `make install` libhttpserver into a throwaway --prefix.
#   2. For every .cpp in examples/, invoke $CXX -std=c++20 with only the
#      installed include path (-I$prefix/include) and library path
#      (-L$prefix/lib -lhttpserver), exercising the public umbrella header
#      via -I$prefix/include and the link line via pkg-config / -lhttpserver.
#   3. Discard the resulting binary; we only care that the compile + link
#      succeeded.
#
# Skips files that the example Makefile.am gates behind a feature macro the
# build host does not provide (websocket_echo when libmicrohttpd_ws is
# absent, minimal_https_psk when gnutls is absent, etc.). The skip list is
# computed by reading config.h from the build dir.
#
# Reports all compile failures before exiting non-zero, rather than stopping
# at the first failure, so a single CI run surfaces all broken examples.

set -eu

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
# Create a temp prefix only when INSTALL_PREFIX is not externally supplied.
# When we own the directory, register a trap to clean it up on exit.
if [ -n "${INSTALL_PREFIX:-}" ]; then
    PREFIX="$INSTALL_PREFIX"
else
    PREFIX="$(mktemp -d -t libhttpserver-installed-examples-XXXXXX)"
    trap 'rm -rf "$PREFIX"' EXIT
fi

CXX="${CXX:-g++}"
CXXFLAGS_EXTRA="${CXXFLAGS:-}"
# macOS / Homebrew: pick up gnutls + microhttpd if installed.
EXTRA_INC=""
EXTRA_LIB=""
if [ -d /opt/homebrew/include ]; then
    EXTRA_INC="-I/opt/homebrew/include -I/opt/homebrew/opt/gnutls/include"
    EXTRA_LIB="-L/opt/homebrew/lib -L/opt/homebrew/opt/gnutls/lib"
fi

log() { echo "verify-installed-examples: $*"; }
# fatal() is for infrastructure failures that abort the script immediately.
# Per-example compile failures use the ok/failed counter pattern instead.
fatal() { echo "verify-installed-examples: FAIL: $*" >&2; exit 1; }

[ -d "$BUILD_DIR" ] || fatal "build dir $BUILD_DIR does not exist; run ./configure first"

log "installing into $PREFIX ..."
_install_log="$(mktemp -t verify-install-XXXXXX.log)"
if ! ( cd "$BUILD_DIR" && make install DESTDIR="" prefix="$PREFIX" exec_prefix="$PREFIX" >/dev/null 2>"$_install_log" ); then
    cat "$_install_log" >&2
    rm -f "$_install_log"
    fatal "make install into $PREFIX failed"
fi
rm -f "$_install_log"

INCLUDE="$PREFIX/include"
LIBDIR="$PREFIX/lib"

[ -f "$INCLUDE/httpserver.hpp" ] || fatal "installed umbrella header missing at $INCLUDE/httpserver.hpp"
[ -f "$LIBDIR/libhttpserver.dylib" ] || [ -f "$LIBDIR/libhttpserver.so" ] || [ -f "$LIBDIR/libhttpserver.a" ] \
    || fatal "installed libhttpserver shared/static library missing in $LIBDIR"

# Compute skip list based on what the in-tree build was actually configured
# with. The HAVE_* macros are not in config.h — they are injected via
# AM_CXXFLAGS in the generated top-level Makefile.
TOP_MAKEFILE="$BUILD_DIR/Makefile"
HAVE_GNUTLS=0
HAVE_BAUTH=0
HAVE_DAUTH=0
HAVE_WS=0
if [ -f "$TOP_MAKEFILE" ]; then
    cxxflags_line="$(grep -E '^AM_CXXFLAGS' "$TOP_MAKEFILE" | head -1)"
    case "$cxxflags_line" in
        *-DHAVE_GNUTLS*) HAVE_GNUTLS=1 ;;
    esac
    case "$cxxflags_line" in
        *-DHAVE_BAUTH*)  HAVE_BAUTH=1 ;;
    esac
    case "$cxxflags_line" in
        *-DHAVE_DAUTH*)  HAVE_DAUTH=1 ;;
    esac
    case "$cxxflags_line" in
        *-DHAVE_WEBSOCKET*) HAVE_WS=1 ;;
    esac
fi

# should_skip() mirrors the conditional noinst_PROGRAMS blocks in
# examples/Makefile.am. When a new conditionally-compiled example is added
# or renamed there, update this function to match — they must stay in sync.
should_skip() {
    local base="$1"
    case "$base" in
        websocket_echo)
            [ "$HAVE_WS" = "1" ] || return 0 ;;
        minimal_https_psk)
            [ "$HAVE_GNUTLS" = "1" ] || return 0 ;;
        basic_authentication|centralized_authentication)
            [ "$HAVE_BAUTH" = "1" ] || return 0 ;;
        digest_authentication)
            [ "$HAVE_DAUTH" = "1" ] || return 0 ;;
        # client_cert_auth.cpp ships as a documentation artifact; not in
        # noinst_PROGRAMS in Makefile.am. Skip from this consumer check too,
        # since it depends on extra GnuTLS APIs that are not part of the
        # public libhttpserver consumer surface.
        client_cert_auth)
            return 0 ;;
    esac
    return 1
}

ok=0
skipped=0
failed=0
failed_list=""
for src in "$REPO_ROOT"/examples/*.cpp; do
    base="$(basename "$src" .cpp)"
    if should_skip "$base"; then
        skipped=$((skipped + 1))
        log "skip:    $base (feature disabled in build)"
        continue
    fi

    extra_ldlibs="-lhttpserver"
    extra_defines=""
    [ "$HAVE_GNUTLS" = "1" ] && extra_defines="$extra_defines -DHAVE_GNUTLS"
    [ "$HAVE_BAUTH" = "1" ]  && extra_defines="$extra_defines -DHAVE_BAUTH"
    [ "$HAVE_DAUTH" = "1" ]  && extra_defines="$extra_defines -DHAVE_DAUTH"
    if [ "$base" = "websocket_echo" ]; then
        extra_ldlibs="$extra_ldlibs -lmicrohttpd_ws"
    fi

    out="$(mktemp -t "${base}.XXXXXX")"
    # The installed prefix must appear before Homebrew paths so that the
    # freshly-installed headers win over any stale system/Homebrew copies.
    # shellcheck disable=SC2086
    if ! "$CXX" -std=c++20 $CXXFLAGS_EXTRA $extra_defines \
            -I"$INCLUDE" $EXTRA_INC \
            "$src" \
            -L"$LIBDIR" $EXTRA_LIB \
            -Wl,-rpath,"$LIBDIR" \
            $extra_ldlibs -lmicrohttpd \
            -o "$out" 2> "${out}.err"; then
        echo "--- $base: COMPILE/LINK FAILED ---" >&2
        cat "${out}.err" >&2
        rm -f "$out" "${out}.err"
        failed=$((failed + 1))
        failed_list="$failed_list $base"
        continue
    fi
    rm -f "$out" "${out}.err"
    ok=$((ok + 1))
    log "ok:      $base"
done

log "summary: $ok built, $skipped skipped, $failed failed"
# Guard 1: no .cpp files found at all — broken glob or empty directory.
if [ "$ok" -eq 0 ] && [ "$skipped" -eq 0 ] && [ "$failed" -eq 0 ]; then
    fatal "no .cpp files found in examples/ — check glob expansion or examples directory"
fi
# Guard 2: every example was skipped but none compiled — AM_CXXFLAGS detection
# may have silently failed (e.g., continuation lines, changed variable casing).
# Fail loudly so the false-all-skip case surfaces as an error, not a silent pass.
if [ "$ok" -eq 0 ] && [ "$skipped" -gt 0 ]; then
    fatal "no examples were compiled — feature-flag detection may have failed; check $TOP_MAKEFILE for AM_CXXFLAGS"
fi
if [ "$failed" -gt 0 ]; then
    echo "verify-installed-examples: FAIL: the following examples failed to build:$failed_list" >&2
    exit 1
fi
exit 0
