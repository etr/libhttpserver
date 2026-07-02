#!/usr/bin/env bash
# Unit test for check-valgrind-lane.sh
#
# Exercises the structural assertions the gate makes against a
# verify-build.yml-shaped workflow file, using synthetic minimal fixtures
# in a temp directory (no dependency on the real workflow's exact line
# numbers). Mirrors the fixture-driven style of test_check_msan_lane.sh.
#
# The gate takes the workflow path as $1, so each fixture is passed
# directly — no REPO_ROOT juggling required.
set -euo pipefail

PASS=0
FAIL=0

ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check-valgrind-lane.sh"

TMPDIR_BASE="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_BASE"' EXIT

# A fully-wired workflow fixture: valid YAML, a valgrind include entry, a
# valgrind configure branch with NO --disable-valgrind-* flags, a Run
# Valgrind checks step invoking `make check-valgrind`, and a results-print
# step surfacing memcheck + helgrind + drd logs.
write_good() {
    cat > "$1" <<'EOF'
name: verify
on: [push]
jobs:
  verify:
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - test-group: extra
            os: ubuntu-latest
            os-type: ubuntu
            build-type: valgrind
            compiler-family: gcc
            debug: debug
            shell: bash
    steps:
      - name: Configure
        run: |
          if [ "$BUILD_TYPE" = "valgrind" ]; then
            ../configure --enable-debug --disable-fastopen;
          fi
      - name: Run Valgrind checks
        run: |
          cd build ;
          make check-valgrind ;
      - name: Print Valgrind results
        run: |
          cd build ;
          for log in test-suite-memcheck.log test-suite-helgrind.log test-suite-drd.log; do
            if [ -f "test/$log" ]; then cat "test/$log" ; fi
          done
EOF
}

# Test 1: fully-wired fixture — exit 0
GOOD="$TMPDIR_BASE/good.yml"
write_good "$GOOD"
if bash "$SCRIPT" "$GOOD" >/dev/null 2>&1; then
    ok "fully-wired workflow exits 0"
else
    fail "fully-wired workflow should exit 0"
fi

# Test 2: missing valgrind include entry (assertion b) — exit 1
NO_VG="$TMPDIR_BASE/no_valgrind.yml"
sed 's/build-type: valgrind/build-type: none/' "$GOOD" > "$NO_VG"
if ! bash "$SCRIPT" "$NO_VG" >/dev/null 2>&1; then
    ok "missing valgrind include entry exits 1"
else
    fail "missing valgrind include entry should exit 1"
fi

# Test 3: a --disable-valgrind-* flag still present (assertion c) — exit 1
DISABLED="$TMPDIR_BASE/disabled.yml"
sed 's/--enable-debug --disable-fastopen;/--enable-debug --disable-fastopen --disable-valgrind-helgrind;/' "$GOOD" > "$DISABLED"
if ! bash "$SCRIPT" "$DISABLED" >/dev/null 2>&1; then
    ok "residual --disable-valgrind-helgrind flag exits 1"
else
    fail "residual --disable-valgrind-helgrind flag should exit 1"
fi

# Test 4: missing make check-valgrind invocation (assertion d) — exit 1
NO_CHECK="$TMPDIR_BASE/no_check.yml"
sed 's/make check-valgrind ;/make check ;/' "$GOOD" > "$NO_CHECK"
if ! bash "$SCRIPT" "$NO_CHECK" >/dev/null 2>&1; then
    ok "missing make check-valgrind exits 1"
else
    fail "missing make check-valgrind should exit 1"
fi

# Test 5: results-print step surfaces only memcheck (assertion e) — exit 1
NO_LOGS="$TMPDIR_BASE/no_logs.yml"
sed 's/test-suite-memcheck.log test-suite-helgrind.log test-suite-drd.log/test-suite-memcheck.log/' "$GOOD" > "$NO_LOGS"
if ! bash "$SCRIPT" "$NO_LOGS" >/dev/null 2>&1; then
    ok "helgrind/drd logs not surfaced exits 1"
else
    fail "helgrind/drd logs not surfaced should exit 1"
fi

# Test 6: invalid YAML (assertion a) — exit 1
# The fixture includes the literal strings checked by assertions (c), (d),
# and (e) so that only assertion (a) — the Python YAML parse check — causes
# the failure. Without these, a later assertion would independently trigger
# exit 1 and a regression removing the parse check would go undetected.
BAD_YAML="$TMPDIR_BASE/bad.yml"
cat > "$BAD_YAML" <<'EOF'
name: verify
jobs:
  verify:
    strategy:
      matrix:
        include:
          - build-type: valgrind
        : this is not valid yaml : : :
      unbalanced: [
# c/d/e literals present so only assertion (a) fires:
# ../configure --enable-debug --disable-fastopen ; make check-valgrind ;
# cat test-suite-helgrind.log test-suite-drd.log ;
EOF
if ! bash "$SCRIPT" "$BAD_YAML" >/dev/null 2>&1; then
    ok "invalid YAML exits 1"
else
    fail "invalid YAML should exit 1"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
