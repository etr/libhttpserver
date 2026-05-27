#!/usr/bin/env bash
#
# check-soversion.sh — TASK-044 SOVERSION acceptance gate.
#
# This script installs libhttpserver into a clean DESTDIR=$STAGE and asserts
# that the on-disk layout matches the v2.0 SOVERSION contract:
#
#   A1. `make install DESTDIR=$STAGE` succeeds.
#   A2. On Linux: $libdir/libhttpserver.so.2.0.0 exists as a regular file.
#       On Darwin: $libdir/libhttpserver.2.dylib exists as a regular file.
#       (Note: with libtool's `-version-number A:B:C` on Darwin, only the
#       major-numbered .2.dylib is produced. Linux produces the full
#       three-file chain .so / .so.2 / .so.2.0.0.)
#   A3. On Linux: $libdir/libhttpserver.so.2 exists and is a symlink that
#       (transitively) resolves to libhttpserver.so.2.0.0.
#       On Darwin: skipped (no intermediate symlink layer on Mach-O).
#   A4. On Linux: $libdir/libhttpserver.so dev symlink exists and resolves
#       to libhttpserver.so.2.0.0.
#       On Darwin: $libdir/libhttpserver.dylib dev symlink exists and
#       resolves to libhttpserver.2.dylib.
#   A5. SONAME / install-name: on Linux `readelf -d` (if present) must report
#       SONAME = libhttpserver.so.2; on Darwin `otool -L` (if present) must
#       list libhttpserver.2.dylib as the install name. When neither tool is
#       on PATH, this assertion degrades to a filename-only check (A2-A4).
#   A6. pkg-config: `pkg-config --modversion libhttpserver` prints 2.0.0,
#       `--cflags` includes -I.../include, `--libs` includes -lhttpserver.
#   A7. libhttpserver.la was installed and lists library_names that include
#       the SONAME (libhttpserver.so.2 on Linux or libhttpserver.2.dylib on
#       Darwin).
#   A8. libhttpserver.a static archive was installed.
#
# Inputs (via env, all optional):
#   BUILD_DIR — out-of-tree build directory; defaults to $REPO_ROOT/build.
#   STAGE     — DESTDIR root for the staged install; defaults to a fresh
#               $BUILD_DIR/.soversion-stage. The script removes and recreates
#               it on entry.
#
# This is a static (no-process) check; the assertions look at filesystem
# layout and pkg-config output only. No webserver is started.
#
# Exits non-zero on the first violation, with a clear FAIL line that names
# the failing assertion.

# -e is intentionally omitted: every significant command uses explicit
# '|| fail ...' error handling so that failures produce a clear, named
# assertion message rather than a silent non-zero exit.  Adding -e would
# break the 'true'-guarded grep calls and confuse the intentional design.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
STAGE="${STAGE:-$BUILD_DIR/.soversion-stage}"
PLATFORM="$(uname -s)"

fail() {
    echo "check-soversion: FAIL: $*" >&2
    exit 1
}

pass() {
    echo "  PASS: $*"
}

# Portably resolve a chain of symlinks to the final real path.
# Used instead of `readlink -f` which is not available on all platforms.
resolve_symlink_chain() {
    local p="$1"
    local d
    local target
    while [ -L "$p" ]; do
        target="$(readlink "$p")"
        d="$(dirname "$p")"
        case "$target" in
            /*) p="$target" ;;
            *)  p="$d/$target" ;;
        esac
    done
    printf '%s\n' "$p"
}

# Detect the configured libdir/includedir from libtool's view of the build.
# The Makefile substitutes ${prefix}, so we fish them out of config.status
# (the canonical record of `./configure`'s decisions).
if [ ! -d "$BUILD_DIR" ]; then
    fail "BUILD_DIR=$BUILD_DIR does not exist; run ./configure first"
fi

CONFIG_STATUS="$BUILD_DIR/config.status"
if [ ! -x "$CONFIG_STATUS" ]; then
    fail "$CONFIG_STATUS not found; run ./configure in $BUILD_DIR first"
fi

# Resolve prefix by asking config.status (shared helper; see scripts/lib/).
# shellcheck source=scripts/lib/resolve-prefix.sh
source "$(dirname "$0")/lib/resolve-prefix.sh"

# libdir & includedir default to $prefix/lib and $prefix/include for our
# configure setup; configure.ac doesn't override AC_PROG_LIBTOOL defaults.
LIBDIR="$RESOLVED_PREFIX/lib"
INCDIR="$RESOLVED_PREFIX/include"

echo "=== check-soversion: SOVERSION acceptance gate ==="
echo "  BUILD_DIR : $BUILD_DIR"
echo "  STAGE     : $STAGE"
echo "  PLATFORM  : $PLATFORM"
echo "  libdir    : $LIBDIR"

# ---- A1: clean install --------------------------------------------------------
rm -rf "$STAGE"
mkdir -p "$STAGE"
# Use tee so CI consoles see streaming progress while the log is also captured.
( cd "$BUILD_DIR" && make install DESTDIR="$STAGE" ) 2>&1 | tee "$STAGE/.install.log"
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "---- install log (tail) ----" >&2
    tail -40 "$STAGE/.install.log" >&2
    fail "A1: 'make install DESTDIR=$STAGE' failed"
fi
pass "A1: staged install succeeded"

STAGE_LIB="$STAGE$LIBDIR"
STAGE_INC="$STAGE$INCDIR"

# ---- A2/A3/A4: platform-specific filename + symlink checks --------------------
case "$PLATFORM" in
    Linux)
        FULL="$STAGE_LIB/libhttpserver.so.2.0.0"
        SONAME_LINK="$STAGE_LIB/libhttpserver.so.2"
        DEV_LINK="$STAGE_LIB/libhttpserver.so"
        FULL_BASENAME="libhttpserver.so.2.0.0"
        SONAME_BASENAME="libhttpserver.so.2"
        HAS_INTERMEDIATE_SONAME_LINK=yes
        ;;
    Darwin)
        # On Darwin libtool with -version-number A:B:C produces ONLY
        # libhttpserver.A.dylib (no .A.B.C.dylib intermediate). The dev
        # symlink (libhttpserver.dylib) points straight at it.
        FULL="$STAGE_LIB/libhttpserver.2.dylib"
        SONAME_LINK=""   # not produced on Mach-O
        DEV_LINK="$STAGE_LIB/libhttpserver.dylib"
        FULL_BASENAME="libhttpserver.2.dylib"
        SONAME_BASENAME="libhttpserver.2.dylib"
        HAS_INTERMEDIATE_SONAME_LINK=no
        ;;
    *)
        fail "unsupported platform '$PLATFORM' (need Linux or Darwin)"
        ;;
esac

[ -f "$FULL" ] || fail "A2: expected regular file at $FULL"
pass "A2: $FULL_BASENAME exists"

# A3: SONAME link (only meaningful on Linux).
if [ "$HAS_INTERMEDIATE_SONAME_LINK" = "yes" ]; then
    [ -L "$SONAME_LINK" ] || fail "A3: expected symlink at $SONAME_LINK"
    SONAME_TARGET="$(readlink "$SONAME_LINK")"
    case "$SONAME_TARGET" in
        "$FULL_BASENAME"|*"/$FULL_BASENAME")
            pass "A3: $SONAME_BASENAME -> $SONAME_TARGET"
            ;;
        *)
            fail "A3: $SONAME_LINK points to '$SONAME_TARGET', expected '$FULL_BASENAME'"
            ;;
    esac
else
    echo "  SKIP A3: no intermediate SONAME symlink on $PLATFORM (libtool emits only $FULL_BASENAME)"
fi

# A4: dev symlink — must resolve (possibly through SONAME_LINK) to FULL.
[ -L "$DEV_LINK" ] || fail "A4: expected symlink at $DEV_LINK"
DEV_RESOLVED="$(resolve_symlink_chain "$DEV_LINK")"
if [ "$DEV_RESOLVED" != "$FULL" ]; then
    fail "A4: $DEV_LINK ultimately resolves to '$DEV_RESOLVED', expected '$FULL'"
fi
pass "A4: $(basename "$DEV_LINK") chains to $FULL_BASENAME"

# ---- A5: SONAME / install-name as embedded in the binary ----------------------
case "$PLATFORM" in
    Linux)
        if command -v readelf >/dev/null 2>&1; then
            soname_line="$(readelf -d "$FULL" 2>/dev/null | grep SONAME || true)"
            if [ -z "$soname_line" ]; then
                fail "A5: readelf -d $FULL produced no SONAME line"
            fi
            # Extract the bracketed SONAME value for a strict equality check.
            # readelf format: (SONAME)  Library soname: [libhttpserver.so.2]
            extracted_soname="$(printf '%s\n' "$soname_line" | sed 's/.*\[\(.*\)\].*/\1/')"
            if [ "$extracted_soname" = "$SONAME_BASENAME" ]; then
                pass "A5: ELF SONAME = $SONAME_BASENAME"
            else
                fail "A5: ELF SONAME mismatch — got '$extracted_soname', expected '$SONAME_BASENAME'"
            fi
        else
            echo "  SKIP A5: readelf not on PATH (filename-only verification accepted)"
        fi
        ;;
    Darwin)
        if command -v otool >/dev/null 2>&1; then
            # Mach-O install-name lives on the LC_ID_DYLIB line, surfaced as
            # the first non-header line of `otool -L`.
            install_name="$(otool -L "$FULL" 2>/dev/null | awk 'NR==2{print $1}')"
            case "$install_name" in
                *"$SONAME_BASENAME"*)
                    pass "A5: Mach-O install-name contains $SONAME_BASENAME"
                    ;;
                *)
                    fail "A5: Mach-O install-name mismatch — got: '$install_name', expected to contain '$SONAME_BASENAME'"
                    ;;
            esac
        else
            echo "  SKIP A5: otool not on PATH (filename-only verification accepted)"
        fi
        ;;
esac

# ---- A6: pkg-config -----------------------------------------------------------
PC_DIR="$STAGE_LIB/pkgconfig"
PC_FILE="$PC_DIR/libhttpserver.pc"
[ -f "$PC_FILE" ] || fail "A6: pkg-config file not found at $PC_FILE"

if command -v pkg-config >/dev/null 2>&1; then
    PKG_CONFIG_PATH="$PC_DIR" pkg-config --exists libhttpserver \
        || fail "A6: pkg-config --exists libhttpserver failed"

    modver="$(PKG_CONFIG_PATH="$PC_DIR" pkg-config --modversion libhttpserver)"
    [ "$modver" = "2.0.0" ] \
        || fail "A6: pkg-config --modversion = '$modver', expected '2.0.0'"

    # `pkg-config --cflags` reports the canonical (installed, not DESTDIR'd)
    # include path; we just verify the canonical $INCDIR appears, NOT
    # $STAGE_INC. (DESTDIR is a packaging staging concept; it doesn't
    # propagate into substituted ${prefix} inside the .pc file.)
    cflags="$(PKG_CONFIG_PATH="$PC_DIR" pkg-config --cflags libhttpserver)"
    case "$cflags" in
        *"-I$INCDIR"*|*"-I${INCDIR%/}"*) ;;
        *) fail "A6: pkg-config --cflags missing -I$INCDIR ; got: $cflags" ;;
    esac

    libs="$(PKG_CONFIG_PATH="$PC_DIR" pkg-config --libs libhttpserver)"
    case "$libs" in
        *"-lhttpserver"*) ;;
        *) fail "A6: pkg-config --libs missing -lhttpserver ; got: $libs" ;;
    esac
    pass "A6: pkg-config reports modversion=2.0.0 and consistent -I/-l flags"
else
    # Fall back to grepping the .pc file directly. The @VERSION@ substitution
    # is performed by configure, so a literal '2.0.0' must be present.
    grep -q "^Version: 2\.0\.0\$" "$PC_FILE" \
        || fail "A6: pkg-config not installed AND $PC_FILE lacks 'Version: 2.0.0'"
    echo "  PARTIAL A6: pkg-config not installed; verified .pc Version line directly"
fi

# ---- A7: libtool .la --------------------------------------------------------
LA_FILE="$STAGE_LIB/libhttpserver.la"
[ -f "$LA_FILE" ] || fail "A7: libtool control file missing: $LA_FILE"
# Verify the SONAME appears specifically in the library_names= field, not just
# anywhere in the file (e.g. not in a comment or dlname-only reference).
if ! grep -E "^library_names=.*$SONAME_BASENAME" "$LA_FILE" >/dev/null 2>&1; then
    fail "A7: $LA_FILE library_names= field does not reference $SONAME_BASENAME"
fi
pass "A7: $(basename "$LA_FILE") library_names references $SONAME_BASENAME"

# ---- A8: static archive -----------------------------------------------------
STATIC="$STAGE_LIB/libhttpserver.a"
[ -f "$STATIC" ] || fail "A8: static archive missing: $STATIC"
pass "A8: libhttpserver.a installed"

echo "  ALL PASS: SOVERSION acceptance contract satisfied"
exit 0
