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

# A fully-wired workflow fixture: valid YAML, three valgrind-* include entries
# (valgrind-memcheck / valgrind-helgrind / valgrind-drd), a valgrind configure
# branch with NO --disable-valgrind-* flags, a Run Valgrind checks step
# invoking `make "check-valgrind-${TOOL}"` (real AX_VALGRIND_CHECK per-tool
# target), and a results-print step surfacing memcheck + helgrind + drd logs.
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
            build-type: valgrind-memcheck
            compiler-family: gcc
            debug: nodebug
            shell: bash
          - test-group: extra
            os: ubuntu-latest
            os-type: ubuntu
            build-type: valgrind-helgrind
            compiler-family: gcc
            debug: nodebug
            shell: bash
          - test-group: extra
            os: ubuntu-latest
            os-type: ubuntu
            build-type: valgrind-drd
            compiler-family: gcc
            debug: nodebug
            shell: bash
    steps:
      - name: Configure
        run: |
          case "$BUILD_TYPE" in
            valgrind-*) ../configure --enable-debug --disable-fastopen ;;
          esac
      - name: Run Valgrind checks
        timeout-minutes: 90
        run: |
          cd build ;
          TOOL="${BUILD_TYPE#valgrind-}" ;
          make "check-valgrind-${TOOL}" ;
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

# Test 2: missing valgrind include entries (assertion b) — exit 1
# Replace all three valgrind-* build-type values with non-matching ones.
NO_VG="$TMPDIR_BASE/no_valgrind.yml"
sed 's/valgrind-memcheck/none-memcheck/g;s/valgrind-helgrind/none-helgrind/g;s/valgrind-drd/none-drd/g' "$GOOD" > "$NO_VG"
if ! bash "$SCRIPT" "$NO_VG" >/dev/null 2>&1; then
    ok "missing valgrind include entries exits 1"
else
    fail "missing valgrind include entries should exit 1"
fi

# Test 3: a --disable-valgrind-* flag still present (assertion c) — exit 1
DISABLED="$TMPDIR_BASE/disabled.yml"
sed 's/--enable-debug --disable-fastopen/--enable-debug --disable-fastopen --disable-valgrind-helgrind/' "$GOOD" > "$DISABLED"
if ! bash "$SCRIPT" "$DISABLED" >/dev/null 2>&1; then
    ok "residual --disable-valgrind-helgrind flag exits 1"
else
    fail "residual --disable-valgrind-helgrind flag should exit 1"
fi

# Test 4: missing per-tool check-valgrind invocation (assertion d) — exit 1
# The gate looks for the literal string 'check-valgrind-${TOOL}' in the workflow.
NO_CHECK="$TMPDIR_BASE/no_check.yml"
sed 's/make "check-valgrind-\${TOOL}" ;/make check ;/' "$GOOD" > "$NO_CHECK"
if ! bash "$SCRIPT" "$NO_CHECK" >/dev/null 2>&1; then
    ok "missing per-tool check-valgrind-\${TOOL} invocation exits 1"
else
    fail "missing per-tool check-valgrind-\${TOOL} invocation should exit 1"
fi

# Test 5a: results-print step surfaces memcheck + drd but not helgrind
# (assertion e1) — exit 1. Isolates e1 from e2 so a regression that only
# breaks the helgrind-log check cannot hide behind a still-passing e2.
NO_HELGRIND_LOG="$TMPDIR_BASE/no_helgrind_log.yml"
sed 's/test-suite-memcheck.log test-suite-helgrind.log test-suite-drd.log/test-suite-memcheck.log test-suite-drd.log/' "$GOOD" > "$NO_HELGRIND_LOG"
if ! bash "$SCRIPT" "$NO_HELGRIND_LOG" >/dev/null 2>&1; then
    ok "helgrind log not surfaced exits 1 (e1)"
else
    fail "helgrind log not surfaced should exit 1 (e1)"
fi

# Test 5b: results-print step surfaces memcheck + helgrind but not drd
# (assertion e2) — exit 1. Isolates e2 from e1 the same way.
NO_DRD_LOG="$TMPDIR_BASE/no_drd_log.yml"
sed 's/test-suite-memcheck.log test-suite-helgrind.log test-suite-drd.log/test-suite-memcheck.log test-suite-helgrind.log/' "$GOOD" > "$NO_DRD_LOG"
if ! bash "$SCRIPT" "$NO_DRD_LOG" >/dev/null 2>&1; then
    ok "drd log not surfaced exits 1 (e2)"
else
    fail "drd log not surfaced should exit 1 (e2)"
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
          - build-type: valgrind-memcheck
          - build-type: valgrind-helgrind
          - build-type: valgrind-drd
        : this is not valid yaml : : :
      unbalanced: [
# c/d/e literals present so only assertion (a) fires:
# ../configure --enable-debug --disable-fastopen ; make "check-valgrind-${TOOL}" ;
# cat test-suite-helgrind.log test-suite-drd.log ;
EOF
if ! bash "$SCRIPT" "$BAD_YAML" >/dev/null 2>&1; then
    ok "invalid YAML exits 1"
else
    fail "invalid YAML should exit 1"
fi

# Test 7: step invokes bare check-${TOOL} (missing valgrind- prefix) — exit 1
# A workflow step that runs `make "check-${TOOL}"` instead of
# `make "check-valgrind-${TOOL}"` must fail assertion (d) because the target
# does not exist in AX_VALGRIND_CHECK (which only generates check-valgrind-*).
BARE_TARGET="$TMPDIR_BASE/bare_target.yml"
# The fixture is generated independently (not derived from write_good) using
# the bare "check-${TOOL}" target, missing the "valgrind-" prefix. write_good
# correctly uses "check-valgrind-${TOOL}"; this fixture intentionally uses
# the old, non-existent bare form so assertion (d) must catch it.
cat > "$BARE_TARGET" <<'EOF'
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
            build-type: valgrind-memcheck
            compiler-family: gcc
            debug: nodebug
            shell: bash
          - test-group: extra
            os: ubuntu-latest
            os-type: ubuntu
            build-type: valgrind-helgrind
            compiler-family: gcc
            debug: nodebug
            shell: bash
          - test-group: extra
            os: ubuntu-latest
            os-type: ubuntu
            build-type: valgrind-drd
            compiler-family: gcc
            debug: nodebug
            shell: bash
    steps:
      - name: Configure
        run: |
          case "$BUILD_TYPE" in
            valgrind-*) ../configure --enable-debug --disable-fastopen ;;
          esac
      - name: Run Valgrind checks
        timeout-minutes: 90
        run: |
          cd build ;
          TOOL="${BUILD_TYPE#valgrind-}" ;
          make "check-${TOOL}" ;
      - name: Print Valgrind results
        run: |
          cd build ;
          for log in test-suite-memcheck.log test-suite-helgrind.log test-suite-drd.log; do
            if [ -f "test/$log" ]; then cat "test/$log" ; fi
          done
EOF
if ! bash "$SCRIPT" "$BARE_TARGET" >/dev/null 2>&1; then
    ok "bare check-\${TOOL} (missing valgrind- prefix) exits 1"
else
    fail "bare check-\${TOOL} (missing valgrind- prefix) should exit 1 — gate too loose"
fi

# Test 8: workflow file does not exist (early file-existence guard) — exit 1
if ! bash "$SCRIPT" "$TMPDIR_BASE/does-not-exist.yml" >/dev/null 2>&1; then
    ok "missing workflow file exits 1"
else
    fail "missing workflow file should exit 1"
fi

# Test 9: configure.ac has AX_VALGRIND_CHECK before
# AX_VALGRIND_DFLT([sgcheck]...) (assertion f) — exit 1. Uses the optional
# second positional argument so assertion (f) is exercised against a
# synthetic configure.ac instead of the real repo file.
BAD_CONFIGURE_AC="$TMPDIR_BASE/bad_configure.ac"
cat > "$BAD_CONFIGURE_AC" <<'EOF'
AC_INIT([libhttpserver], [2.0.0])
AX_VALGRIND_CHECK
AX_VALGRIND_DFLT([sgcheck], [off])
EOF
if ! bash "$SCRIPT" "$GOOD" "$BAD_CONFIGURE_AC" >/dev/null 2>&1; then
    ok "AX_VALGRIND_DFLT([sgcheck]...) after AX_VALGRIND_CHECK exits 1 (f)"
else
    fail "AX_VALGRIND_DFLT([sgcheck]...) after AX_VALGRIND_CHECK should exit 1 (f)"
fi

# Test 10: configure.ac missing AX_VALGRIND_DFLT([sgcheck]...) entirely
# (assertion f) — exit 1.
MISSING_DFLT_CONFIGURE_AC="$TMPDIR_BASE/missing_dflt_configure.ac"
cat > "$MISSING_DFLT_CONFIGURE_AC" <<'EOF'
AC_INIT([libhttpserver], [2.0.0])
AX_VALGRIND_CHECK
EOF
if ! bash "$SCRIPT" "$GOOD" "$MISSING_DFLT_CONFIGURE_AC" >/dev/null 2>&1; then
    ok "missing AX_VALGRIND_DFLT([sgcheck]...) exits 1 (f)"
else
    fail "missing AX_VALGRIND_DFLT([sgcheck]...) should exit 1 (f)"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
