#!/usr/bin/env bash
# Unit test for check-skip-rationales.sh
#
# TASK-077: every `#ifndef _WINDOWS` / `#ifndef DARWIN` (and the equivalent
# `#if !defined(_WINDOWS)` form) block in `test/integ/` must carry a
# `// reason:` comment within the 5 lines immediately preceding the directive
# so that the underlying platform limitation is recorded in
# `test/PORTABILITY.md` or a follow-up task.
#
# Tests use a temporary directory tree that mimics test/integ/ so the script
# can be exercised against synthetic fixtures without touching the live tree.
set -euo pipefail

PASS=0
FAIL=0

ok()   { echo "  OK: $1";   PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check-skip-rationales.sh"

TMPDIR_BASE="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_BASE"' EXIT

FAKE_REPO="$TMPDIR_BASE/repo"
mkdir -p "$FAKE_REPO/scripts" "$FAKE_REPO/test/integ"

cp "$SCRIPT" "$FAKE_REPO/scripts/check-skip-rationales.sh"
chmod +x "$FAKE_REPO/scripts/check-skip-rationales.sh"

reset_repo() {
    rm -f "$FAKE_REPO"/test/integ/*.cpp "$FAKE_REPO"/test/integ/*.hpp
}

# Test 1: No `#ifndef _WINDOWS` directives — should exit 0.
reset_repo
cat > "$FAKE_REPO/test/integ/clean.cpp" <<'EOF'
// No platform skips at all.
int main() { return 0; }
EOF
if (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 1: no platform skips exits 0"
else
    fail "test 1: no platform skips should exit 0"
fi

# Test 2: `#ifndef _WINDOWS` with `// reason:` on the immediately preceding
# line — should exit 0.
reset_repo
cat > "$FAKE_REPO/test/integ/adjacent.cpp" <<'EOF'
// Some code
// reason: MHD's Windows path can't drive INTERNAL_SELECT thread pools.
#ifndef _WINDOWS
int x = 1;
#endif
EOF
if (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 2: adjacent reason exits 0"
else
    fail "test 2: adjacent reason should exit 0"
fi

# Test 3: `#ifndef _WINDOWS` with `// reason:` 3 lines above (blank lines
# allowed between) — should exit 0.
reset_repo
cat > "$FAKE_REPO/test/integ/within_window.cpp" <<'EOF'
// reason: see test/PORTABILITY.md §threaded.cpp.

// (blank line above is fine)
#ifndef _WINDOWS
int x = 1;
#endif
EOF
if (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 3: reason 3 lines above exits 0"
else
    fail "test 3: reason 3 lines above should exit 0"
fi

# Test 3b: `#ifndef _WINDOWS` with `// reason:` exactly 5 lines above the
# directive (the awk window's inclusive boundary, `NR >= target - 5`) —
# should exit 0. Closes the gap between test 3 (3 lines, in-window) and
# test 4 (6 lines, out-of-window).
reset_repo
cat > "$FAKE_REPO/test/integ/window_boundary.cpp" <<'EOF'
// reason: exactly five lines above the directive (boundary case).
//
//
//
//
#ifndef _WINDOWS
int x = 1;
#endif
EOF
if (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 3b: reason exactly 5 lines above (boundary) exits 0"
else
    fail "test 3b: reason exactly 5 lines above (boundary) should exit 0"
fi

# Test 4: `#ifndef _WINDOWS` with `// reason:` 6 lines above (outside window)
# — should exit 1.
reset_repo
cat > "$FAKE_REPO/test/integ/outside_window.cpp" <<'EOF'
// reason: this comment sits more than 5 lines above the directive.
//
//
//
//
//
#ifndef _WINDOWS
int x = 1;
#endif
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 4: reason outside window exits 1"
else
    fail "test 4: reason outside window should exit 1"
fi

# Test 5: `#ifndef _WINDOWS` with no `// reason:` anywhere — should exit 1.
reset_repo
cat > "$FAKE_REPO/test/integ/bare.cpp" <<'EOF'
#ifndef _WINDOWS
int x = 1;
#endif
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 5: bare _WINDOWS exits 1"
else
    fail "test 5: bare _WINDOWS should exit 1"
fi

# Test 6: `#ifndef DARWIN` with no `// reason:` — should exit 1
# (verifies DARWIN is also covered).
reset_repo
cat > "$FAKE_REPO/test/integ/darwin_bare.cpp" <<'EOF'
#ifndef DARWIN
int x = 1;
#endif
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 6: bare DARWIN exits 1"
else
    fail "test 6: bare DARWIN should exit 1"
fi

# Test 7: `#ifdef _WINDOWS` (opposite polarity — additive, not a skip)
# does NOT require `// reason:`; should exit 0.
reset_repo
cat > "$FAKE_REPO/test/integ/additive.cpp" <<'EOF'
// No reason comment here.
#ifdef _WINDOWS
int x = 1;
#endif
EOF
if (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 7: #ifdef _WINDOWS (additive) exits 0"
else
    fail "test 7: #ifdef _WINDOWS (additive) should exit 0"
fi

# Test 7b: `#ifdef DARWIN` (opposite polarity — additive, not a skip)
# does NOT require `// reason:`; should exit 0. Mirrors test 7 for DARWIN.
reset_repo
cat > "$FAKE_REPO/test/integ/additive_darwin.cpp" <<'EOF'
// No reason comment here.
#ifdef DARWIN
int x = 1;
#endif
EOF
if (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 7b: #ifdef DARWIN (additive) exits 0"
else
    fail "test 7b: #ifdef DARWIN (additive) should exit 0"
fi

# Test 8: `#if !defined(_WINDOWS) && defined(HAVE_DAUTH)` form (the
# authentication.cpp digest-auth guard) without a reason — should exit 1.
reset_repo
cat > "$FAKE_REPO/test/integ/if_not_defined.cpp" <<'EOF'
#if !defined(_WINDOWS) && defined(HAVE_DAUTH)
int x = 1;
#endif
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 8: #if !defined(_WINDOWS) bare exits 1"
else
    fail "test 8: #if !defined(_WINDOWS) bare should exit 1"
fi

# Test 9: same `#if !defined(_WINDOWS)` form with `// reason:` adjacent — should exit 0.
reset_repo
cat > "$FAKE_REPO/test/integ/if_not_defined_ok.cpp" <<'EOF'
// reason: MinGW64 curl's --digest parser rejects the MHD challenge.
#if !defined(_WINDOWS) && defined(HAVE_DAUTH)
int x = 1;
#endif
EOF
if (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 9: #if !defined(_WINDOWS) with reason exits 0"
else
    fail "test 9: #if !defined(_WINDOWS) with reason should exit 0"
fi

# Test 9b: `#if !defined(DARWIN)` form (no `&&` conjunction) without a
# reason — should exit 1, mirroring test 8 but for DARWIN.
reset_repo
cat > "$FAKE_REPO/test/integ/if_not_defined_darwin.cpp" <<'EOF'
#if !defined(DARWIN)
int x = 1;
#endif
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 9b: #if !defined(DARWIN) bare exits 1"
else
    fail "test 9b: #if !defined(DARWIN) bare should exit 1"
fi

# Test 9c: bare `#if !defined(_WINDOWS)` (no `&&` conjunction) without a
# reason — should exit 1. Tests 8/9 only ever exercise the compound
# `&& defined(HAVE_DAUTH)` form; this pins the conjunction-free form.
reset_repo
cat > "$FAKE_REPO/test/integ/if_not_defined_bare.cpp" <<'EOF'
#if !defined(_WINDOWS)
int x = 1;
#endif
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 9c: #if !defined(_WINDOWS) (no conjunction) bare exits 1"
else
    fail "test 9c: #if !defined(_WINDOWS) (no conjunction) bare should exit 1"
fi

# Test 9d: same conjunction-free `#if !defined(_WINDOWS)` form with a
# `// reason:` comment adjacent — should exit 0.
reset_repo
cat > "$FAKE_REPO/test/integ/if_not_defined_bare_ok.cpp" <<'EOF'
// reason: MinGW64 lacks the syscall this test exercises.
#if !defined(_WINDOWS)
int x = 1;
#endif
EOF
if (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
    ok "test 9d: #if !defined(_WINDOWS) (no conjunction) with reason exits 0"
else
    fail "test 9d: #if !defined(_WINDOWS) (no conjunction) with reason should exit 0"
fi

# Test 9e: two guarded directives in one file — the first compliant (a
# `// reason:` comment precedes it), the second not. Exercises the
# inner while-read loop's handling of multiple candidates per file, which
# no single-directive fixture above touches. Should exit 1 and stderr
# should name the second (non-compliant) directive's line number.
reset_repo
cat > "$FAKE_REPO/test/integ/two_directives.cpp" <<'EOF'
// reason: first block is documented.
#ifndef _WINDOWS
int x = 1;
#endif
//
//
//
//
#ifndef _WINDOWS
int y = 2;
#endif
EOF
STDERR_OUT="$TMPDIR_BASE/two_directives.stderr"
if ! (cd "$FAKE_REPO" && bash scripts/check-skip-rationales.sh 2>"$STDERR_OUT"); then
    if grep -q "two_directives.cpp:9:" "$STDERR_OUT"; then
        ok "test 9e: second (non-compliant) directive of two exits 1 and is named at its line"
    else
        fail "test 9e: stderr should name the second directive's line (5); got: $(cat "$STDERR_OUT")"
    fi
else
    fail "test 9e: file with one compliant and one non-compliant directive should exit 1"
fi

# Test 10: the live repo's test/integ/ — should exit 0 after rationale
# comments have been added to every skip site. Skipped if the live tree
# does not exist or is in the middle of the implementation (the
# regression guard runs against the real repo, not this fake one).
REAL_REPO="$(cd "$(dirname "$0")/.." && pwd)"
if [ -d "$REAL_REPO/test/integ" ]; then
    if (cd "$REAL_REPO" && bash scripts/check-skip-rationales.sh 2>/dev/null); then
        ok "test 10: live test/integ/ tree exits 0"
    else
        fail "test 10: live test/integ/ tree should exit 0 (add // reason: comments to all #ifndef _WINDOWS/DARWIN blocks)"
    fi
else
    echo "  SKIP: test 10 — live test/integ/ not found"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
