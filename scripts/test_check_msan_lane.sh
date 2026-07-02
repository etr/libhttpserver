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

# Test 4: no MSAN_OPTIONS / scoped TESTS wiring (assertion d) — exit 1
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

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
