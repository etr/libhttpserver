#!/usr/bin/env bash
#
# check-parallel-install.sh — TASK-044 parallel-installability verification.
#
# This script proves that libhttpserver v1 and libhttpserver v2.0 can be
# installed side-by-side into the same prefix without their shared-library
# artefacts colliding. The promise is on the SONAME / runtime-library side —
# v1's `libhttpserver.so.0` and v2's `libhttpserver.so.2` (Linux) or
# `libhttpserver.0.dylib` and `libhttpserver.2.dylib` (Darwin) MUST
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
# TASK-089: this gate is wired into per-PR CI on the Linux gcc/libstdc++ lane
# (see .github/workflows/verify-build.yml, matrix key `parallel-install: check`).
# The five environment-quirk paths (master ref missing, git worktree add
# failed, v1 bootstrap/configure/make failed) emit a clear SKIP and FAIL the
# job (exit 1) via skip_or_fail() — UNLESS HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1
# is set, the documented escape hatch for genuine infrastructure breakage. The
# two developer-ran-without-configure pre-flight guards (BUILD_DIR /
# config.status missing) remain plain `skip` (exit 0): in CI the build always
# exists before this runs, so they never fire there.
#
# Acceptance:
#   * On Linux: $STAGE$libdir/libhttpserver.so.0 (or whatever v1 ships)
#     coexists with $STAGE$libdir/libhttpserver.so.2.0.0 after step 2.
#   * On Darwin: $STAGE$libdir/libhttpserver.0.dylib (or v1 SONAME)
#     coexists with $STAGE$libdir/libhttpserver.2.dylib after step 2.
#     (libtool's -version-number A:B:C on Mach-O emits only .A.dylib;
#     the .A.B.C.dylib intermediate is a Linux-only convention.)
#
# A failure to even produce a v1 build is reported as a SKIP that, in CI,
# fails the job (see skip_or_fail() above) so a silently-broken v1 tree
# cannot mask the parallel-install promise behind a green signal.

# -e is intentionally omitted: every significant command uses explicit
# '|| fail/skip' error handling for clear diagnostic messages.
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

remove_master_worktree() {
    # Tear down the ephemeral worktree if it exists.  Used by both the
    # pre-add stale-cleanup guard and the EXIT trap.
    # `git worktree remove --force` may exit non-zero on shallow clones;
    # ignore errors — the directory cleanup is sufficient for re-run safety.
    if [ -d "$MASTER_WORKTREE" ]; then
        git -C "$REPO_ROOT" worktree remove --force "$MASTER_WORKTREE" 2>/dev/null || true
        rm -rf "$MASTER_WORKTREE"
    fi
}

cleanup() {
    # Don't touch $SHARED_STAGE — the maintainer may want to inspect it.
    remove_master_worktree
}
trap cleanup EXIT

[ -d "$BUILD_DIR" ] || skip "BUILD_DIR=$BUILD_DIR does not exist (run ./configure first)"

# Resolve libdir from config.status via the shared helper.
CONFIG_STATUS="$BUILD_DIR/config.status"
[ -x "$CONFIG_STATUS" ] || skip "$CONFIG_STATUS missing (run ./configure first)"
# shellcheck source=scripts/lib/resolve-prefix.sh
source "$(dirname "$0")/lib/resolve-prefix.sh"
# TASK-089: skip_or_fail() — the five environment-quirk paths below emit SKIP
# and FAIL the job (exit 1) unless HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1.
# shellcheck source=scripts/lib/skip-or-fail.sh
source "$(dirname "$0")/lib/skip-or-fail.sh"
LIBDIR="$RESOLVED_PREFIX/lib"
STAGE_LIB="$SHARED_STAGE$LIBDIR"

case "$PLATFORM" in
    Linux)
        V2_FULL_BASENAME="libhttpserver.so.2.0.0"
        # V1_SONAME_PATTERN: fallback pattern when the exact v1 SONAME cannot
        # be read from the .la file (see Phase 3 below).  [01] covers:
        #   .so.0 — the legacy 0.x line (master MAJOR_VERSION=0)
        #   .so.1 — a hypothetical 1.x bump (not shipped but possible)
        # The exact v1 SONAME is always preferred; this pattern is a safety net.
        V1_SONAME_PATTERN='^libhttpserver\.so\.[01]'
        ;;
    Darwin)
        # libtool with -version-number on Mach-O produces only the
        # major-numbered .N.dylib (no .N.M.P.dylib intermediate).
        V2_FULL_BASENAME="libhttpserver.2.dylib"
        # V1_SONAME_PATTERN: fallback pattern — matches .0.dylib (legacy 0.x)
        # or .1.dylib (hypothetical 1.x).  Same rationale as Linux above.
        V1_SONAME_PATTERN='^libhttpserver\.[01]\.dylib'
        ;;
    *)
        fail "unsupported platform '$PLATFORM' (need Linux or Darwin)"
        ;;
esac
# Alias for backward-compatibility with the fallback grep path below.
V1_PATTERN="$V1_SONAME_PATTERN"

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
    skip_or_fail "ref '$MASTER_REF' not in this repository — cannot do automated v1 build"
fi

# Remove a stale worktree from a previous aborted run, if any.
remove_master_worktree

if ! git -C "$REPO_ROOT" worktree add --detach "$MASTER_WORKTREE" "$MASTER_REF" >"$SHARED_STAGE/.worktree-add.log" 2>&1; then
    cat "$SHARED_STAGE/.worktree-add.log" >&2
    skip_or_fail "git worktree add failed; cannot do automated v1 build"
fi

# Bootstrap, configure, build, install the v1 source.
echo "  bootstrapping $MASTER_REF in $MASTER_WORKTREE"
if ! ( cd "$MASTER_WORKTREE" && ./bootstrap ) >"$SHARED_STAGE/.v1-bootstrap.log" 2>&1; then
    echo "  (v1 bootstrap failed; this often means glibtoolize/libtoolize is missing)"
    tail -20 "$SHARED_STAGE/.v1-bootstrap.log" >&2
    skip_or_fail "v1 bootstrap failed in $MASTER_WORKTREE (treating as environment limitation)"
fi

V1_BUILD="$MASTER_WORKTREE/build"
mkdir -p "$V1_BUILD"
echo "  configuring $MASTER_REF"
# Build Darwin-specific include/lib hints using the active Homebrew prefix
# (if brew is on PATH) so that arm64 (/opt/homebrew) and x86_64 (/usr/local)
# Homebrew installs are both handled correctly.  On Linux these variables
# remain empty and configure falls back to its own search paths.
_HOMEBREW_CPPFLAGS=""
_HOMEBREW_LDFLAGS=""
if [ "$PLATFORM" = "Darwin" ]; then
    if command -v brew >/dev/null 2>&1; then
        _BREW_PREFIX="$(brew --prefix 2>/dev/null || true)"
    else
        _BREW_PREFIX="/opt/homebrew"   # best-effort fallback for arm64
    fi
    if [ -n "$_BREW_PREFIX" ]; then
        _HOMEBREW_CPPFLAGS="-I${_BREW_PREFIX}/include -I${_BREW_PREFIX}/opt/gnutls/include"
        _HOMEBREW_LDFLAGS="-L${_BREW_PREFIX}/lib -L${_BREW_PREFIX}/opt/gnutls/lib"
    fi
fi
if ! ( cd "$V1_BUILD" && \
        CPPFLAGS="${CPPFLAGS:-} ${_HOMEBREW_CPPFLAGS}" \
        LDFLAGS="${LDFLAGS:-} ${_HOMEBREW_LDFLAGS}" \
        LIBS="${LIBS:--lgnutls}" \
        ../configure --prefix="$RESOLVED_PREFIX" ) >"$SHARED_STAGE/.v1-configure.log" 2>&1; then
    tail -20 "$SHARED_STAGE/.v1-configure.log" >&2
    skip_or_fail "v1 configure failed (treating as environment limitation)"
fi

# Use all available cores; mirror what the parent make would use.
_NJOBS="$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
echo "  building $MASTER_REF (make -j${_NJOBS})"
if ! ( cd "$V1_BUILD" && make -j"${_NJOBS}" ) >"$SHARED_STAGE/.v1-make.log" 2>&1; then
    tail -30 "$SHARED_STAGE/.v1-make.log" >&2
    skip_or_fail "v1 make failed (treating as environment limitation; e.g. v1 sources may not build on this toolchain)"
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

# Derive the expected v1 runtime library basename from the v1 build's .la
# file (the `library_names` field), falling back to the broad V1_PATTERN
# grep when the .la is absent (older libtool layouts).
V1_LA="$V1_BUILD/.libs/libhttpserver.la"
V1_EXPECTED_BASENAME=""
if [ -f "$V1_LA" ]; then
    # `library_names='libhttpserver.so.0.x.y libhttpserver.so.0 libhttpserver.so'`
    # The last token in the library_names value is the dev link; the first is
    # the versioned runtime file.  Extract the first token.
    _raw="$(grep '^library_names=' "$V1_LA" | head -1 | sed "s/^library_names='*//;s/'*$//")"
    V1_EXPECTED_BASENAME="${_raw%% *}"   # first whitespace-delimited token
fi

if [ -n "$V1_EXPECTED_BASENAME" ]; then
    # Strict check: the exact filename must appear in $STAGE_LIB and must
    # differ from V2_FULL_BASENAME (ensures they coexist, not just that v2
    # is present).
    if [ "$V1_EXPECTED_BASENAME" = "$V2_FULL_BASENAME" ]; then
        fail "v1 and v2 appear to have the SAME versioned filename ($V1_EXPECTED_BASENAME); SONAME collision detected"
    fi
    [ -f "$STAGE_LIB/$V1_EXPECTED_BASENAME" ] \
        || fail "expected v1 library '$V1_EXPECTED_BASENAME' (from $V1_LA) not found in $STAGE_LIB; install contents: $(ls "$STAGE_LIB" 2>/dev/null)"
    echo "  v1 artefact coexisting with v2: $V1_EXPECTED_BASENAME"
    pass "v1 ($V1_EXPECTED_BASENAME) and v2 ($V2_FULL_BASENAME) SONAMEd libraries coexist in $STAGE_LIB"
else
    # Fallback: broad pattern-match (V1_PATTERN covers .so.0, .so.1, etc.).
    v1_hits="$(ls "$STAGE_LIB" 2>/dev/null | grep -E "$V1_PATTERN" | grep -v "^$V2_FULL_BASENAME\$" || true)"
    if [ -z "$v1_hits" ]; then
        fail "no v1-style library file (matching $V1_PATTERN, excluding $V2_FULL_BASENAME) found in $STAGE_LIB; install contents: $(ls "$STAGE_LIB" 2>/dev/null)"
    fi
    echo "  v1 artefacts coexisting with v2:"
    while IFS= read -r f; do
        echo "    $f"
    done <<< "$v1_hits"
    pass "v1 and v2 SONAMEd libraries coexist in $STAGE_LIB"
fi

echo "  Note: dev-time files (libhttpserver.la, libhttpserver.a, libhttpserver.pc,"
echo "        headers, the bare libhttpserver.so/.dylib dev symlink) are LAST-WRITER-WINS"
echo "        by design — see RELEASE_NOTES.md 'SOVERSION & packaging'."

echo "  ALL PASS: parallel-installability verified"
exit 0
