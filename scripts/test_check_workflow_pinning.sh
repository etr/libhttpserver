#!/usr/bin/env bash
# Unit test for check-workflow-pinning.sh
#
# Exercises the cross-workflow assertions the gate makes (SHA-pinning of both
# workflows, Codecov fail_ci_if_error: true, doxygen invariance-exclusion
# documentation) using synthetic minimal fixtures in a temp directory. Mirrors
# the fixture-driven style of test_check_valgrind_lane.sh.
#
# The gate takes two workflow paths ($1 codeql-shaped, $2 verify-build-shaped),
# so each pair of fixtures is passed directly.
set -euo pipefail

PASS=0
FAIL=0

ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check-workflow-pinning.sh"

TMPDIR_BASE="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_BASE"' EXIT

SHA="dddddddddddddddddddddddddddddddddddddddd"

# A SHA-pinned codeql-shaped fixture (only pinning matters here).
write_codeql() {
    cat > "$1" <<EOF
name: "CodeQL"
on: [push]
jobs:
  analyze:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@${SHA}  # v4.2.2
      - uses: github/codeql-action/init@${SHA}  # v3.36.3
EOF
}

# A verify-build-shaped fixture: SHA-pinned uses, a Codecov upload with
# fail_ci_if_error: true, and the doxygen invariance-exclusion label.
write_verify() {
    cat > "$1" <<EOF
name: verify
on: [push]
jobs:
  verify:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@${SHA}  # v4.2.2
      - name: Install MinGW64 packages
        # Invariance exclusion (doxygen): doxygen + graphviz intentionally
        # excluded on Windows; the invariant runs on the Linux lanes.
        run: echo install
      - name: Upload coverage to Codecov
        uses: codecov/codecov-action@${SHA}  # v5.5.4
        with:
          fail_ci_if_error: true
EOF
}

CODEQL_GOOD="$TMPDIR_BASE/codeql_good.yml"
VERIFY_GOOD="$TMPDIR_BASE/verify_good.yml"
write_codeql "$CODEQL_GOOD"
write_verify "$VERIFY_GOOD"

# Test 1: both hardened fixtures — exit 0
if bash "$SCRIPT" "$CODEQL_GOOD" "$VERIFY_GOOD" >/dev/null 2>&1; then
    ok "both hardened workflows exit 0"
else
    fail "both hardened workflows should exit 0"
fi

# Test 2: Codecov fail_ci_if_error: false (assertion c) — exit 1
VERIFY_FALSE="$TMPDIR_BASE/verify_false.yml"
sed 's/fail_ci_if_error: true/fail_ci_if_error: false/' "$VERIFY_GOOD" > "$VERIFY_FALSE"
if ! bash "$SCRIPT" "$CODEQL_GOOD" "$VERIFY_FALSE" >/dev/null 2>&1; then
    ok "fail_ci_if_error: false exits 1"
else
    fail "fail_ci_if_error: false should exit 1"
fi

# Test 3: a floating @vN ref in the codeql workflow (assertion b) — exit 1
CODEQL_FLOAT="$TMPDIR_BASE/codeql_float.yml"
awk '/codeql-action\/init@/ { print "      - uses: github/codeql-action/init@v3"; next } { print }' "$CODEQL_GOOD" > "$CODEQL_FLOAT"
if ! bash "$SCRIPT" "$CODEQL_FLOAT" "$VERIFY_GOOD" >/dev/null 2>&1; then
    ok "floating @v3 ref in codeql workflow exits 1"
else
    fail "floating @v3 ref in codeql workflow should exit 1"
fi

# Test 4: a floating @vN ref in the verify workflow (assertion b) — exit 1
VERIFY_FLOAT="$TMPDIR_BASE/verify_float.yml"
awk '/codecov\/codecov-action@/ { print "        uses: codecov/codecov-action@v5"; next } { print }' "$VERIFY_GOOD" > "$VERIFY_FLOAT"
if ! bash "$SCRIPT" "$CODEQL_GOOD" "$VERIFY_FLOAT" >/dev/null 2>&1; then
    ok "floating @v5 ref in verify workflow exits 1"
else
    fail "floating @v5 ref in verify workflow should exit 1"
fi

# Test 5: doxygen invariance-exclusion label missing (assertion d) — exit 1
VERIFY_NODOX="$TMPDIR_BASE/verify_nodox.yml"
grep -v 'Invariance exclusion (doxygen)' "$VERIFY_GOOD" > "$VERIFY_NODOX"
if ! bash "$SCRIPT" "$CODEQL_GOOD" "$VERIFY_NODOX" >/dev/null 2>&1; then
    ok "missing doxygen invariance-exclusion label exits 1"
else
    fail "missing doxygen invariance-exclusion label should exit 1"
fi

# Test 6: invalid YAML in the verify workflow (assertion a) — exit 1
VERIFY_BAD="$TMPDIR_BASE/verify_bad.yml"
cat > "$VERIFY_BAD" <<EOF
name: verify
# fail_ci_if_error: true  and  Invariance exclusion (doxygen)  present so
# only assertion (a) fires on the parse failure below.
jobs:
  verify:
    steps: [
EOF
if ! bash "$SCRIPT" "$CODEQL_GOOD" "$VERIFY_BAD" >/dev/null 2>&1; then
    ok "invalid YAML exits 1"
else
    fail "invalid YAML should exit 1"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
