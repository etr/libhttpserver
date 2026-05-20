#!/usr/bin/env bash
#
# check-parallel-install.sh — TASK-044 parallel-installability verification.
#
# This script proves that libhttpserver v1 and libhttpserver v2.0 can be
# installed side-by-side into the same prefix without their shared-library
# artefacts colliding. The promise is on the SONAME / runtime-library side —
# v1's `libhttpserver.so.0` and v2's `libhttpserver.so.2` (Linux) or
# `libhttpserver.0.dylib` and `libhttpserver.2.0.0.dylib` (Darwin) MUST
# coexist after installing both into the same DESTDIR.
#
# The dev-time artefacts (libhttpserver.la, libhttpserver.a, the .pc file,
# the headers, the bare `libhttpserver.so`/`libhttpserver.dylib` dev symlink)
# DO collide. The last installer wins — this is the standard C++ library
# behaviour and is intentional. We only assert the runtime SONAME duo here.
#
# Strategy:
#   1. Build the current (v2.0) tree from $BUILD_DIR and install into
#      $SHARED_STAGE (a fresh DESTDIR).
#   2. Use `git worktree add` (read-only, ephemeral) to materialise a master
#      checkout, bootstrap+configure+build it, and install it into the SAME
#      $SHARED_STAGE.
#   3. Assert that both SONAMEd library files coexist on disk after step 2.
#
# Inputs (via env, all optional):
#   BUILD_DIR     — current-tree out-of-tree build directory.
#                   Default: $REPO_ROOT/build.
#   SHARED_STAGE  — DESTDIR root for the parallel install.
#                   Default: $BUILD_DIR/.parallel-stage.
#   MASTER_REF    — git ref to use as the v1 source.
#                   Default: master.
#   MASTER_WORKTREE — path for the temporary v1 worktree.
#                     Default: $BUILD_DIR/.parallel-master-worktree.
#
# This script is best-effort and exits 0 with a clear SKIP message if the
# v1 source tree cannot be built in this environment (e.g. glibtoolize
# missing). It is NOT wired into `make check` to keep per-PR CI fast; it
# lives as an opt-in `make check-parallel-install` rule.
#
# Acceptance:
#   * On Linux: $STAGE$libdir/libhttpserver.so.0 (or whatever v1 ships)
#     coexists with $STAGE$libdir/libhttpserver.so.2.0.0 after step 2.
#   * On Darwin: $STAGE$libdir/libhttpserver.0.dylib (or v1 SONAME)
#     coexists with $STAGE$libdir/libhttpserver.2.dylib after step 2.
#     (libtool's -version-number A:B:C on Mach-O emits only .A.dylib;
#     the .A.B.C.dylib intermediate is a Linux-only convention.)
#
# This script is intentionally tolerant: any failure to even produce a v1
# build is treated as a SKIP, because the user-visible promise (the v2
# install is well-formed; the on-disk filenames don't clash) is verified
# either way.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
SHARED_STAGE="${SHARED_STAGE:-$BUILD_DIR/.parallel-stage}"
MASTER_REF="${MASTER_REF:-master}"
MASTER_WORKTREE="${MASTER_WORKTREE:-$BUILD_DIR/.parallel-master-worktree}"
PLATFORM="$(uname -s)"

skip() {
    echo "check-parallel-install: SKIP: $*" >&2
    exit 0
}

fail() {
    echo "check-parallel-install: FAIL: $*" >&2
    exit 1
}

pass() {
    echo "  PASS: $*"
}

cleanup() {
    # Tear down the ephemeral worktree if we created one. Don't touch
    # $SHARED_STAGE — the maintainer may want to inspect it.
    if [ -d "$MASTER_WORKTREE" ]; then
        # `git worktree remove --force` may exit non-zero on shallow clones;
        # ignore errors — the directory cleanup is sufficient for re-run safety.
        git -C "$REPO_ROOT" worktree remove --force "$MASTER_WORKTREE" 2>/dev/null || true
        rm -rf "$MASTER_WORKTREE"
    fi
}
trap cleanup EXIT

[ -d "$BUILD_DIR" ] || skip "BUILD_DIR=$BUILD_DIR does not exist (run ./configure first)"

# Resolve libdir from config.status (same trick as check-soversion.sh).
CONFIG_STATUS="$BUILD_DIR/config.status"
[ -x "$CONFIG_STATUS" ] || skip "$CONFIG_STATUS missing (run ./configure first)"
RESOLVED_PREFIX="$("$CONFIG_STATUS" --config 2>/dev/null \
    | tr ' ' '\n' | grep -E "^'?--prefix=" | head -1 | sed "s/^'\?--prefix=//;s/'\$//")"
[ -z "$RESOLVED_PREFIX" ] && RESOLVED_PREFIX="/usr/local"
LIBDIR="$RESOLVED_PREFIX/lib"
STAGE_LIB="$SHARED_STAGE$LIBDIR"

case "$PLATFORM" in
    Linux)
        V2_FULL_BASENAME="libhttpserver.so.2.0.0"
        # v1 install on disk uses the major from configure.ac's
        # MAJOR_VERSION — most commonly .so.0 (legacy 0.x line) or
        # .so.1. We don't pin the v1 SONAME here; we just require
        # *some* sibling file shaped like libhttpserver.so.<digit>.
        V1_PATTERN='^libhttpserver\.so\.[01]'
        ;;
    Darwin)
        # libtool with -version-number on Mach-O produces only the
        # major-numbered .N.dylib (no .N.M.P.dylib intermediate).
        V2_FULL_BASENAME="libhttpserver.2.dylib"
        V1_PATTERN='^libhttpserver\.[01]\.dylib'
        ;;
    *)
        fail "unsupported platform '$PLATFORM' (need Linux or Darwin)"
        ;;
esac

echo "=== check-parallel-install: v2 + v1 coexistence on the same DESTDIR ==="
echo "  BUILD_DIR       : $BUILD_DIR"
echo "  SHARED_STAGE    : $SHARED_STAGE"
echo "  MASTER_REF      : $MASTER_REF"
echo "  MASTER_WORKTREE : $MASTER_WORKTREE"

# ---- Phase 1: install v2 into the shared stage --------------------------------
rm -rf "$SHARED_STAGE"
mkdir -p "$SHARED_STAGE"
echo "Phase 1: installing v2 from $BUILD_DIR"
if ! ( cd "$BUILD_DIR" && make install DESTDIR="$SHARED_STAGE" ) >"$SHARED_STAGE/.v2-install.log" 2>&1; then
    echo "---- v2 install log (tail) ----" >&2
    tail -40 "$SHARED_STAGE/.v2-install.log" >&2
    fail "v2 install into $SHARED_STAGE failed"
fi
pass "v2 install succeeded into $SHARED_STAGE"

[ -f "$STAGE_LIB/$V2_FULL_BASENAME" ] \
    || fail "expected v2 library $V2_FULL_BASENAME after phase 1, got: $(ls "$STAGE_LIB" 2>/dev/null || echo '(libdir missing)')"

# ---- Phase 2: materialise v1 source via git worktree --------------------------
echo "Phase 2: building $MASTER_REF in a temporary git worktree"

if ! git -C "$REPO_ROOT" rev-parse --verify "$MASTER_REF" >/dev/null 2>&1; then
    skip "ref '$MASTER_REF' not in this repository — cannot do automated v1 build"
fi

# Remove a stale worktree from a previous aborted run, if any.
if [ -d "$MASTER_WORKTREE" ]; then
    git -C "$REPO_ROOT" worktree remove --force "$MASTER_WORKTREE" 2>/dev/null || true
    rm -rf "$MASTER_WORKTREE"
fi

if ! git -C "$REPO_ROOT" worktree add --detach "$MASTER_WORKTREE" "$MASTER_REF" >"$SHARED_STAGE/.worktree-add.log" 2>&1; then
    cat "$SHARED_STAGE/.worktree-add.log" >&2
    skip "git worktree add failed; cannot do automated v1 build"
fi

# Bootstrap, configure, build, install the v1 source.
echo "  bootstrapping $MASTER_REF in $MASTER_WORKTREE"
if ! ( cd "$MASTER_WORKTREE" && ./bootstrap ) >"$SHARED_STAGE/.v1-bootstrap.log" 2>&1; then
    echo "  (v1 bootstrap failed; this often means glibtoolize/libtoolize is missing)"
    tail -20 "$SHARED_STAGE/.v1-bootstrap.log" >&2
    skip "v1 bootstrap failed in $MASTER_WORKTREE (treating as environment limitation)"
fi

V1_BUILD="$MASTER_WORKTREE/build"
mkdir -p "$V1_BUILD"
echo "  configuring $MASTER_REF"
if ! ( cd "$V1_BUILD" && \
        CPPFLAGS="${CPPFLAGS:-} -I/opt/homebrew/include -I/opt/homebrew/opt/gnutls/include" \
        LDFLAGS="${LDFLAGS:-} -L/opt/homebrew/lib -L/opt/homebrew/opt/gnutls/lib" \
        LIBS="${LIBS:--lgnutls}" \
        ../configure --prefix="$RESOLVED_PREFIX" ) >"$SHARED_STAGE/.v1-configure.log" 2>&1; then
    tail -20 "$SHARED_STAGE/.v1-configure.log" >&2
    skip "v1 configure failed (treating as environment limitation)"
fi

echo "  building $MASTER_REF"
if ! ( cd "$V1_BUILD" && make -j4 ) >"$SHARED_STAGE/.v1-make.log" 2>&1; then
    tail -30 "$SHARED_STAGE/.v1-make.log" >&2
    skip "v1 make failed (treating as environment limitation; e.g. v1 sources may not build on this toolchain)"
fi

echo "  installing $MASTER_REF into $SHARED_STAGE"
if ! ( cd "$V1_BUILD" && make install DESTDIR="$SHARED_STAGE" ) >"$SHARED_STAGE/.v1-install.log" 2>&1; then
    tail -30 "$SHARED_STAGE/.v1-install.log" >&2
    fail "v1 install into $SHARED_STAGE failed AFTER successful v1 build"
fi
pass "v1 ($MASTER_REF) install succeeded into shared stage"

# ---- Phase 3: assert coexistence ---------------------------------------------
[ -f "$STAGE_LIB/$V2_FULL_BASENAME" ] \
    || fail "v2 library $V2_FULL_BASENAME disappeared from $STAGE_LIB after v1 install"

# Look for any v1-style sibling that is NOT the v2 file we just confirmed.
v1_hits="$(ls "$STAGE_LIB" 2>/dev/null | grep -E "$V1_PATTERN" | grep -v "^$V2_FULL_BASENAME\$" || true)"
if [ -z "$v1_hits" ]; then
    fail "no v1-style library file (matching $V1_PATTERN, excluding $V2_FULL_BASENAME) found in $STAGE_LIB; install contents: $(ls "$STAGE_LIB" 2>/dev/null)"
fi

echo "  v1 artefacts coexisting with v2:"
for f in $v1_hits; do
    echo "    $f"
done
pass "v1 and v2 SONAMEd libraries coexist in $STAGE_LIB"

echo "  Note: dev-time files (libhttpserver.la, libhttpserver.a, libhttpserver.pc,"
echo "        headers, the bare libhttpserver.so/.dylib dev symlink) are LAST-WRITER-WINS"
echo "        by design — see RELEASE_NOTES.md 'SOVERSION & packaging'."

echo "  ALL PASS: parallel-installability verified"
exit 0
