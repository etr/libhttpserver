#!/usr/bin/env bash
#
# TASK-076 Cycle 1 / sentinel for `LT_SKIP` exit-code plumbing.
#
# Asserts that the littletest binary semantics for SKIP are:
#
#   (a) A binary whose only outcomes are SKIPs exits 77 (Automake's SKIP
#       code from `test-driver` line 131: `77:*) col=$blu res=SKIP …`).
#       This is what makes "all dependencies missing" reportable to
#       Automake as SKIP rather than PASS — the regression the rest of
#       TASK-076 patches at every TLS test site.
#
#   (b) A binary that mixes passes and skips exits 0 (the existing
#       behaviour; SKIPs do not "infect" a successful run).
#
# The script compiles two tiny throwaway TUs against the in-tree
# `test/littletest.hpp` and runs them. It is wired into Makefile.am as a
# `lint-`-style script (no library link required); it does NOT depend on
# anything libhttpserver-specific so it can run on any host with a C++
# compiler.
#
# Exit codes:
#   0 — both assertions held.
#   1 — at least one assertion failed.
#

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LITTLETEST_HDR="$REPO_ROOT/test/littletest.hpp"
if [ ! -f "$LITTLETEST_HDR" ]; then
    echo "ERROR: $LITTLETEST_HDR missing" >&2
    exit 1
fi

CXX="${CXX:-c++}"

TMPDIR_=$(mktemp -d)
trap 'rm -rf "$TMPDIR_"' EXIT

# ---- (a) all-skip → exit 77 -------------------------------------------------
cat > "$TMPDIR_/only_skips.cpp" <<'EOF'
#include "littletest.hpp"

LT_BEGIN_SUITE(only_skips_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(only_skips_suite)

LT_BEGIN_AUTO_TEST(only_skips_suite, this_test_only_skips)
    LT_SKIP("dependency missing");
LT_END_AUTO_TEST(this_test_only_skips)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
EOF

"$CXX" -std=c++20 -I"$REPO_ROOT/test" \
    "$TMPDIR_/only_skips.cpp" -o "$TMPDIR_/only_skips"

set +e
"$TMPDIR_/only_skips" > "$TMPDIR_/only_skips.log" 2>&1
ec_a=$?
set -e

if [ "$ec_a" -ne 77 ]; then
    echo "FAIL (a): all-skip binary exited $ec_a, expected 77" >&2
    cat "$TMPDIR_/only_skips.log" >&2
    exit 1
fi

# ---- (b) pass + skip → exit 0 ----------------------------------------------
cat > "$TMPDIR_/mixed.cpp" <<'EOF'
#include "littletest.hpp"

LT_BEGIN_SUITE(mixed_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(mixed_suite)

LT_BEGIN_AUTO_TEST(mixed_suite, this_passes)
    LT_CHECK_EQ(1 + 1, 2);
LT_END_AUTO_TEST(this_passes)

LT_BEGIN_AUTO_TEST(mixed_suite, this_skips)
    LT_SKIP("dependency missing");
LT_END_AUTO_TEST(this_skips)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
EOF

"$CXX" -std=c++20 -I"$REPO_ROOT/test" \
    "$TMPDIR_/mixed.cpp" -o "$TMPDIR_/mixed"

set +e
"$TMPDIR_/mixed" > "$TMPDIR_/mixed.log" 2>&1
ec_b=$?
set -e

if [ "$ec_b" -ne 0 ]; then
    echo "FAIL (b): mixed pass+skip binary exited $ec_b, expected 0" >&2
    cat "$TMPDIR_/mixed.log" >&2
    exit 1
fi

# Confirm the binary emitted the `[SKIP]` marker and the `-> N skipped`
# runner summary line — these are the textual contracts the CI lane
# greps for to assert per-test SKIPs in ws_start_stop fired.
if ! grep -q '^\[SKIP\] ' "$TMPDIR_/mixed.log"; then
    echo "FAIL (b): mixed binary stdout missing '[SKIP] ' marker" >&2
    cat "$TMPDIR_/mixed.log" >&2
    exit 1
fi
if ! grep -qE '^-> [0-9]+ skipped' "$TMPDIR_/mixed.log"; then
    echo "FAIL (b): runner summary missing '-> N skipped' line" >&2
    cat "$TMPDIR_/mixed.log" >&2
    exit 1
fi

echo "PASS — littletest SKIP exit-code semantics held (77 for all-skip, 0 for mixed)"
