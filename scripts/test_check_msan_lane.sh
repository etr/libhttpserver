#!/usr/bin/env bash
# Unit test for check-msan-lane.sh
#
# Exercises the four structural assertions the gate makes against a
# verify-build.yml-shaped workflow file, using synthetic minimal fixtures
# in a temp directory (no dependency on the real workflow's exact line
# numbers). Mirrors the fixture-driven style of
# test_check_warning_suppressions.sh.
#
# The gate takes the workflow path as $1, so each fixture is passed
# directly — no REPO_ROOT juggling required.
set -euo pipefail

PASS=0
FAIL=0

ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check-msan-lane.sh"

TMPDIR_BASE="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_BASE"' EXIT

# A fully-wired workflow fixture: valid YAML, msan include entry with the
# mirrored keys, no stale ubuntu-18.04 / clang-6.0 block, and the Run tests
# step wiring MSAN_OPTIONS + a scoped `make check TESTS=`.
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
            build-type: asan
            compiler-family: clang
            c-compiler: clang-18
            cc-compiler: clang++-18
            debug: debug
            coverage: nocoverage
            shell: bash
          - test-group: extra
            os: ubuntu-latest
            os-type: ubuntu
            build-type: msan
            compiler-family: clang
            c-compiler: clang-18
            cc-compiler: clang++-18
            debug: debug
            coverage: nocoverage
            shell: bash
    steps:
      - name: Run tests
        run: |
          cd build
          if [ "$BUILD_TYPE" = "msan" ]; then
            export MSAN_OPTIONS="abort_on_error=1" ;
            make check TESTS="$MSAN_TESTS" ;
          else
            make check ;
          fi
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

# Test 2: missing msan include entry (assertion b) — exit 1
NO_MSAN="$TMPDIR_BASE/no_msan.yml"
# Drop the msan build-type by renaming it to something else.
sed 's/build-type: msan/build-type: none/' "$GOOD" > "$NO_MSAN"
if ! bash "$SCRIPT" "$NO_MSAN" >/dev/null 2>&1; then
    ok "missing msan include entry exits 1"
else
    fail "missing msan include entry should exit 1"
fi

# Test 3: stale ubuntu-18.04 / clang-6.0 commented block present (assertion c) — exit 1
STALE="$TMPDIR_BASE/stale.yml"
write_good "$STALE"
cat >> "$STALE" <<'EOF'
          # stale retired block kept by mistake:
          #  os: ubuntu-18.04
          #  c-compiler: clang-6.0
EOF
if ! bash "$SCRIPT" "$STALE" >/dev/null 2>&1; then
    ok "stale ubuntu-18.04/clang-6.0 comment exits 1"
else
    fail "stale ubuntu-18.04/clang-6.0 comment should exit 1"
fi

# Test 4: no MSAN_OPTIONS / scoped TESTS wiring at all (assertion d) — exit 1
NO_WIRING="$TMPDIR_BASE/no_wiring.yml"
# Rebuild good but strip the Run tests msan branch down to a plain make check.
awk '
  /if \[ "\$BUILD_TYPE" = "msan" \]; then/ { skip=1 }
  skip==1 && /make check ;/ { print "          make check ;"; skip=0; next }
  skip==1 { next }
  { print }
' "$GOOD" > "$NO_WIRING"
if ! bash "$SCRIPT" "$NO_WIRING" >/dev/null 2>&1; then
    ok "missing MSAN_OPTIONS/scoped-TESTS wiring exits 1"
else
    fail "missing MSAN_OPTIONS/scoped-TESTS wiring should exit 1"
fi

# Test 4a: MSAN_OPTIONS retained but scoped TESTS= dropped (assertion d2
# only) — exit 1. Isolates d2 from d1 so a regression that removed only the
# scoped-TESTS check could not hide behind a still-passing d1.
NO_TESTS_SCOPE="$TMPDIR_BASE/no_tests_scope.yml"
sed 's/make check TESTS="\$MSAN_TESTS" ;/make check ;/' "$GOOD" > "$NO_TESTS_SCOPE"
if ! bash "$SCRIPT" "$NO_TESTS_SCOPE" >/dev/null 2>&1; then
    ok "MSAN_OPTIONS present but scoped TESTS= missing exits 1 (d2)"
else
    fail "MSAN_OPTIONS present but scoped TESTS= missing should exit 1 (d2)"
fi

# Test 4b: scoped TESTS= retained but MSAN_OPTIONS dropped (assertion d1
# only) — exit 1. Isolates d1 from d2 the same way.
NO_MSAN_OPTIONS="$TMPDIR_BASE/no_msan_options.yml"
sed '/export MSAN_OPTIONS="abort_on_error=1" ;/d' "$GOOD" > "$NO_MSAN_OPTIONS"
if ! bash "$SCRIPT" "$NO_MSAN_OPTIONS" >/dev/null 2>&1; then
    ok "scoped TESTS= present but MSAN_OPTIONS missing exits 1 (d1)"
else
    fail "scoped TESTS= present but MSAN_OPTIONS missing should exit 1 (d1)"
fi

# Test 5: invalid YAML (assertion a) — exit 1
# The fixture includes the literal strings checked by assertions (d1) and
# (d2) so that only assertion (a) — the Python YAML parse check — causes the
# failure. Without these strings, assertion (d) would independently trigger
# exit 1 and a regression that removed the Python parse check would go
# undetected (Test 5 would still pass via (d)).
BAD_YAML="$TMPDIR_BASE/bad.yml"
cat > "$BAD_YAML" <<'EOF'
name: verify
jobs:
  verify:
    strategy:
      matrix:
        include:
          - build-type: msan
        : this is not valid yaml : : :
      unbalanced: [
# d1/d2 literals present so only assertion (a) fires:
# MSAN_OPTIONS=abort_on_error=1 ; make check TESTS="$MSAN_TESTS" ;
EOF
if ! bash "$SCRIPT" "$BAD_YAML" >/dev/null 2>&1; then
    ok "invalid YAML exits 1"
else
    fail "invalid YAML should exit 1"
fi

# Test 6: workflow file does not exist (missing-workflow-file guard) — exit 1
if ! bash "$SCRIPT" "$TMPDIR_BASE/does_not_exist.yml" >/dev/null 2>&1; then
    ok "missing workflow file exits 1"
else
    fail "missing workflow file should exit 1"
fi

# Test 7: two build-type: msan include entries (duplicate-entry branch,
# Python sys.exit(5)) — exit 1.
DUP_MSAN="$TMPDIR_BASE/dup_msan.yml"
cat > "$DUP_MSAN" <<'EOF'
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
            build-type: msan
            compiler-family: clang
            c-compiler: clang-18
            cc-compiler: clang++-18
            debug: debug
            coverage: nocoverage
            shell: bash
          - test-group: extra
            os: ubuntu-latest
            os-type: ubuntu
            build-type: msan
            compiler-family: clang
            c-compiler: clang-18
            cc-compiler: clang++-18
            debug: debug
            coverage: nocoverage
            shell: bash
    steps:
      - name: Run tests
        run: |
          cd build
          if [ "$BUILD_TYPE" = "msan" ]; then
            export MSAN_OPTIONS="abort_on_error=1" ;
            make check TESTS="$MSAN_TESTS" ;
          else
            make check ;
          fi
EOF
if ! bash "$SCRIPT" "$DUP_MSAN" >/dev/null 2>&1; then
    ok "duplicate msan include entries exits 1"
else
    fail "duplicate msan include entries should exit 1"
fi

# Test 8: msan entry present but with a wrong fixed-key value
# (compiler-family: gcc instead of clang) — exit 1 via sys.exit(6).
WRONG_KEY="$TMPDIR_BASE/wrong_key.yml"
awk '
  /build-type: msan/ { in_msan=1 }
  in_msan && /compiler-family: clang/ { sub(/compiler-family: clang/, "compiler-family: gcc"); in_msan=0 }
  { print }
' "$GOOD" > "$WRONG_KEY"
if ! bash "$SCRIPT" "$WRONG_KEY" >/dev/null 2>&1; then
    ok "msan entry with wrong fixed-key value exits 1 (b, exit 6)"
else
    fail "msan entry with wrong fixed-key value should exit 1 (b, exit 6)"
fi

# Test 9: msan entry present but missing a presence-only key (os) — exit 1
# via sys.exit(6). Built directly (not derived from $GOOD via sed/awk) since
# the "os:" key precedes "build-type: msan" in the entry, making a reliable
# block-scoped removal awkward with line-oriented tools.
MISSING_KEY="$TMPDIR_BASE/missing_key.yml"
cat > "$MISSING_KEY" <<'EOF'
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
            build-type: asan
            compiler-family: clang
            c-compiler: clang-18
            cc-compiler: clang++-18
            debug: debug
            coverage: nocoverage
            shell: bash
          - test-group: extra
            os-type: ubuntu
            build-type: msan
            compiler-family: clang
            c-compiler: clang-18
            cc-compiler: clang++-18
            debug: debug
            coverage: nocoverage
            shell: bash
    steps:
      - name: Run tests
        run: |
          cd build
          if [ "$BUILD_TYPE" = "msan" ]; then
            export MSAN_OPTIONS="abort_on_error=1" ;
            make check TESTS="$MSAN_TESTS" ;
          else
            make check ;
          fi
EOF
if ! bash "$SCRIPT" "$MISSING_KEY" >/dev/null 2>&1; then
    ok "msan entry missing presence-only key (os) exits 1 (b, exit 6)"
else
    fail "msan entry missing presence-only key (os) should exit 1 (b, exit 6)"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
