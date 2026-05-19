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
# Exits non-zero on the first compile failure.

set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
PREFIX="${INSTALL_PREFIX:-$(mktemp -d -t libhttpserver-installed-examples-XXXXXX)}"

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
fail() { echo "verify-installed-examples: FAIL: $*" >&2; exit 1; }

[ -d "$BUILD_DIR" ] || fail "build dir $BUILD_DIR does not exist; run ./configure first"

log "installing into $PREFIX ..."
( cd "$BUILD_DIR" && make install DESTDIR="" prefix="$PREFIX" exec_prefix="$PREFIX" >/dev/null 2>&1 ) \
    || fail "make install into $PREFIX failed"

INCLUDE="$PREFIX/include"
LIBDIR="$PREFIX/lib"

[ -f "$INCLUDE/httpserver.hpp" ] || fail "installed umbrella header missing at $INCLUDE/httpserver.hpp"
[ -f "$LIBDIR/libhttpserver.dylib" ] || [ -f "$LIBDIR/libhttpserver.so" ] || [ -f "$LIBDIR/libhttpserver.a" ] \
    || fail "installed libhttpserver shared/static library missing in $LIBDIR"

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
        fail "$base failed to build against installed headers"
    fi
    rm -f "$out" "${out}.err"
    ok=$((ok + 1))
    log "ok:      $base"
done

log "summary: $ok built, $skipped skipped"
exit 0
