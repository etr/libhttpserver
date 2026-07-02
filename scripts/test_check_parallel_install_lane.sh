#!/usr/bin/env bash
# Unit test for check-parallel-install-lane.sh (TASK-089).
#
# Exercises the structural assertions the gate makes against a
# verify-build.yml-shaped workflow file, using synthetic minimal fixtures
# in a temp directory (no dependency on the real workflow's exact line
# numbers). Mirrors the fixture-driven style of test_check_valgrind_lane.sh.
#
# The gate takes the workflow path as $1, so each fixture is passed
# directly — no REPO_ROOT juggling required.
set -euo pipefail

PASS=0
FAIL=0

ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check-parallel-install-lane.sh"

TMPDIR_BASE="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_BASE"' EXIT

# A fully-wired workflow fixture: valid YAML, a matrix include entry carrying
# `parallel-install: check`, a "Fetch master ref" step that fetches
# refs/heads/master, a step invoking `make check-parallel-install`, and NO
# authorization of the SKIP escape hatch (so environment-quirk SKIPs stay
# fatal in CI).
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
            build-type: none
            compiler-family: gcc
            debug: nodebug
            parallel-install: check
            shell: bash
    steps:
      - name: Run tests
        run: |
          cd build ;
          make check ;
      - name: Fetch master ref for parallel-install
        if: ${{ matrix.parallel-install == 'check' }}
        run: |
          git fetch --no-tags --depth=1 origin +refs/heads/master:refs/remotes/origin/master
      - name: Run parallel-install gate
        if: ${{ matrix.parallel-install == 'check' }}
        run: |
          cd build ;
          MASTER_REF=origin/master make check-parallel-install ;
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

# Test 2: no matrix entry carries parallel-install: check (assertion b) — exit 1
NO_KEY="$TMPDIR_BASE/no_key.yml"
sed 's/parallel-install: check/parallel-install: none/' "$GOOD" > "$NO_KEY"
if ! bash "$SCRIPT" "$NO_KEY" >/dev/null 2>&1; then
    ok "missing parallel-install: check matrix key exits 1"
else
    fail "missing parallel-install: check matrix key should exit 1"
fi

# Test 3: no `make check-parallel-install` invocation (assertion c) — exit 1
NO_INVOKE="$TMPDIR_BASE/no_invoke.yml"
sed 's/make check-parallel-install/make check/' "$GOOD" > "$NO_INVOKE"
if ! bash "$SCRIPT" "$NO_INVOKE" >/dev/null 2>&1; then
    ok "missing make check-parallel-install invocation exits 1"
else
    fail "missing make check-parallel-install invocation should exit 1"
fi

# Test 4: no master fetch step (assertion d) — exit 1
NO_FETCH="$TMPDIR_BASE/no_fetch.yml"
sed 's#+refs/heads/master:refs/remotes/origin/master#+refs/heads/main:refs/remotes/origin/main#' "$GOOD" > "$NO_FETCH"
if ! bash "$SCRIPT" "$NO_FETCH" >/dev/null 2>&1; then
    ok "missing master fetch step exits 1"
else
    fail "missing master fetch step should exit 1"
fi

# Test 5: the run step authorizes the SKIP escape hatch (assertion e) — exit 1
# In CI, environment-quirk SKIPs must stay fatal: setting the authorization
# env var on the gate step would silently re-open the SKIP-becomes-pass hole.
AUTHORIZED="$TMPDIR_BASE/authorized.yml"
sed 's/MASTER_REF=origin\/master make check-parallel-install/HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1 MASTER_REF=origin\/master make check-parallel-install/' "$GOOD" > "$AUTHORIZED"
if ! bash "$SCRIPT" "$AUTHORIZED" >/dev/null 2>&1; then
    ok "SKIP-authorization on the gate step exits 1"
else
    fail "SKIP-authorization on the gate step should exit 1"
fi

# Test 6: invalid YAML (assertion a) — exit 1
# The fixture includes the literal strings checked by assertions (c) and (d)
# in comments so that only assertion (a) — the Python YAML parse check —
# causes the failure. Without these, a later assertion would independently
# trigger exit 1 and a regression removing the parse check would go undetected.
BAD_YAML="$TMPDIR_BASE/bad.yml"
cat > "$BAD_YAML" <<'EOF'
name: verify
jobs:
  verify:
    strategy:
      matrix:
        include:
          - build-type: none
            parallel-install: check
        : this is not valid yaml : : :
      unbalanced: [
# c/d literals present so only assertion (a) fires:
# make check-parallel-install ;
# git fetch --no-tags --depth=1 origin +refs/heads/master:refs/remotes/origin/master
EOF
if ! bash "$SCRIPT" "$BAD_YAML" >/dev/null 2>&1; then
    ok "invalid YAML exits 1"
else
    fail "invalid YAML should exit 1"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
