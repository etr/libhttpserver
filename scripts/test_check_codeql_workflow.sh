#!/usr/bin/env bash
# Unit test for check-codeql-workflow.sh
#
# Exercises the structural assertions the gate makes against a
# codeql-analysis.yml-shaped workflow file, using synthetic minimal fixtures
# in a temp directory (no dependency on the real workflow's exact line
# numbers). Mirrors the fixture-driven style of test_check_valgrind_lane.sh /
# test_check_parallel_install_lane.sh.
#
# The gate takes the workflow path as $1, so each fixture is passed directly.
set -euo pipefail

PASS=0
FAIL=0

ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check-codeql-workflow.sh"

TMPDIR_BASE="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_BASE"' EXIT

# Two distinct dummy 40-hex SHAs for the pin fixtures.
SHA_A="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
SHA_B="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
SHA_CHECKOUT="cccccccccccccccccccccccccccccccccccccccc"

# A fully-hardened workflow fixture: valid YAML, all three actions SHA-pinned,
# init + analyze share one SHA, no autobuild anywhere, an explicit
# ./configure + make build step, a permissions block with security-events:
# write, and sha256sum verification on the libmicrohttpd download. Must
# exit 0.
write_good() {
    cat > "$1" <<EOF
name: "CodeQL"
on:
  push:
    branches: [master]
permissions:
  actions: read
  contents: read
  security-events: write
jobs:
  analyze:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout repository
        uses: actions/checkout@${SHA_CHECKOUT}  # v4.2.2
      - name: Install libmicrohttpd dependency
        run: |
          curl -fsSL https://s3.amazonaws.com/libhttpserver/libmicrohttpd_releases/libmicrohttpd-1.0.3.tar.gz -o libmicrohttpd-1.0.3.tar.gz ;
          echo "7816b57aae199cf5c3645e8770e1be5f0a4dfafbcb24b3772173dc4ee634126a  libmicrohttpd-1.0.3.tar.gz" | sha256sum -c ;
          tar -xzf libmicrohttpd-1.0.3.tar.gz ;
          cd libmicrohttpd-1.0.3 ;
          ./configure --disable-examples ;
          make ;
      - name: Initialize CodeQL
        uses: github/codeql-action/init@${SHA_A}  # v3.36.3
        with:
          languages: cpp
      - name: Build the library (CodeQL manual mode)
        run: |
          ./bootstrap ;
          ./configure --enable-same-directory-build ;
          make ;
      - name: Perform CodeQL Analysis
        uses: github/codeql-action/analyze@${SHA_A}  # v3.36.3
EOF
}

# Test 1: fully-hardened fixture — exit 0
GOOD="$TMPDIR_BASE/good.yml"
write_good "$GOOD"
if bash "$SCRIPT" "$GOOD" >/dev/null 2>&1; then
    ok "fully-hardened workflow exits 0"
else
    fail "fully-hardened workflow should exit 0"
fi

# Test 2: a floating @vN ref present (assertions b + c) — exit 1
FLOATING="$TMPDIR_BASE/floating.yml"
awk '/codeql-action\/analyze@/ { print "        uses: github/codeql-action/analyze@v3"; next } { print }' "$GOOD" > "$FLOATING"
if ! bash "$SCRIPT" "$FLOATING" >/dev/null 2>&1; then
    ok "floating @v3 ref exits 1"
else
    fail "floating @v3 ref should exit 1"
fi

# Test 2b: a short-hash pin (not a full 40-hex SHA) present — exercises
# assertion (c) independently of (b) (a short hash is not a floating @vN tag).
SHORT_HASH="$TMPDIR_BASE/short_hash.yml"
awk '/codeql-action\/analyze@/ { print "        uses: github/codeql-action/analyze@abc1234"; next } { print }' "$GOOD" > "$SHORT_HASH"
if ! bash "$SCRIPT" "$SHORT_HASH" >/dev/null 2>&1; then
    ok "short-hash pin (not 40-hex) exits 1"
else
    fail "short-hash pin (not 40-hex) should exit 1"
fi

# Test 3: a commented autobuild line present (assertion d) — exit 1
AUTOBUILD="$TMPDIR_BASE/autobuild.yml"
awk '1; /Initialize CodeQL/ {print "      # - uses: github/codeql-action/autobuild@'"${SHA_A}"'"}' "$GOOD" > "$AUTOBUILD"
if ! bash "$SCRIPT" "$AUTOBUILD" >/dev/null 2>&1; then
    ok "residual commented autobuild reference exits 1"
else
    fail "residual commented autobuild reference should exit 1"
fi

# Test 4: init / analyze pinned to DIFFERENT SHAs (assertion f) — exit 1
MISMATCH="$TMPDIR_BASE/mismatch.yml"
# Flip only the analyze pin to SHA_B, leaving init on SHA_A.
awk -v a="${SHA_A}" -v b="${SHA_B}" '
    /codeql-action\/analyze@/ { gsub("@" a, "@" b) }
    { print }
' "$GOOD" > "$MISMATCH"
if ! bash "$SCRIPT" "$MISMATCH" >/dev/null 2>&1; then
    ok "mismatched init/analyze SHAs exits 1"
else
    fail "mismatched init/analyze SHAs should exit 1"
fi

# Test 5: missing explicit build step (assertion e) — exit 1
NO_BUILD="$TMPDIR_BASE/no_build.yml"
grep -v -e './configure' -e 'make ;' "$GOOD" > "$NO_BUILD"
if ! bash "$SCRIPT" "$NO_BUILD" >/dev/null 2>&1; then
    ok "missing ./configure + make build step exits 1"
else
    fail "missing ./configure + make build step should exit 1"
fi

# Test 6: invalid YAML (assertion a) — exit 1
# Note: this fixture also lacks a permissions block and sha256sum
# verification, so assertions (g) and (h) independently fire alongside (a);
# the test only asserts exit 1, which holds regardless.
BAD_YAML="$TMPDIR_BASE/bad.yml"
cat > "$BAD_YAML" <<EOF
name: "CodeQL"
jobs:
  analyze:
    steps:
      - uses: github/codeql-action/init@${SHA_A}
      - run: ./configure ; make ;
      - uses: github/codeql-action/analyze@${SHA_A}
    : this is not valid yaml : : :
  unbalanced: [
EOF
if ! bash "$SCRIPT" "$BAD_YAML" >/dev/null 2>&1; then
    ok "invalid YAML exits 1"
else
    fail "invalid YAML should exit 1"
fi

# Test 7: missing permissions block / security-events: write (assertion g) — exit 1
NO_PERMS="$TMPDIR_BASE/no_perms.yml"
grep -v 'security-events' "$GOOD" > "$NO_PERMS"
if ! bash "$SCRIPT" "$NO_PERMS" >/dev/null 2>&1; then
    ok "missing security-events: write exits 1"
else
    fail "missing security-events: write should exit 1"
fi

# Test 8: missing sha256sum verification on the libmicrohttpd download (assertion h) — exit 1
NO_SHA256="$TMPDIR_BASE/no_sha256.yml"
grep -v 'sha256sum' "$GOOD" > "$NO_SHA256"
if ! bash "$SCRIPT" "$NO_SHA256" >/dev/null 2>&1; then
    ok "missing sha256sum verification exits 1"
else
    fail "missing sha256sum verification should exit 1"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
