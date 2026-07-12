#!/usr/bin/env bash
# Unit test for check-dr008-lanes.sh
#
# Exercises the structural assertions the gate makes against a
# verify-build.yml-shaped workflow file, using synthetic minimal fixtures in
# a temp directory (no dependency on the real workflow's exact line
# numbers). Mirrors the fixture-driven style of test_check_msan_lane.sh.
#
# The gate takes the workflow path as $1, so each fixture is passed
# directly — no REPO_ROOT juggling required.
set -euo pipefail

PASS=0
FAIL=0

ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check-dr008-lanes.sh"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# A fully-wired workflow fixture: both DR-008 steps present, correctly
# gated, and timeout-boxed.
write_good() {
    cat > "$1" <<'EOF'
name: verify
on: [push]
jobs:
  verify:
    runs-on: ${{ matrix.os }}
    steps:
      - name: Run route_table_concurrency under TSan (TASK-092)
        timeout-minutes: 5
        run: |
          cd build ;
          export TSAN_OPTIONS="suppressions=$(pwd)/../test/tsan.supp" ;
          make -C test check-route-table-concurrency ;
        if: ${{ matrix.build-type == 'tsan' }}

      - name: Run stop()-from-handler deadlock contract (TASK-092)
        timeout-minutes: 3
        run: |
          cd build ;
          make -C test check-stop-from-handler ;
        if: ${{ matrix.os-type == 'ubuntu' && matrix.test-group == 'basic' && matrix.c-compiler == 'gcc' && matrix.debug == 'nodebug' && matrix.linking == 'dynamic' && matrix.build-type == 'classic' }}
EOF
}

# Test 1: fully-wired fixture — exit 0
GOOD="$WORK_DIR/good.yml"
write_good "$GOOD"
if bash "$SCRIPT" "$GOOD" >/dev/null 2>&1; then
    ok "fully-wired workflow exits 0"
else
    fail "fully-wired workflow should exit 0"
fi

# Test 2: check-route-table-concurrency step missing entirely — exit 1
NO_RTC="$WORK_DIR/no_rtc.yml"
awk '
  /Run route_table_concurrency under TSan/ { skip=1 }
  skip==1 && /Run stop\(\)-from-handler/ { skip=0 }
  skip==1 { next }
  { print }
' "$GOOD" > "$NO_RTC"
if ! bash "$SCRIPT" "$NO_RTC" >/dev/null 2>&1; then
    ok "missing check-route-table-concurrency step exits 1"
else
    fail "missing check-route-table-concurrency step should exit 1"
fi

# Test 3: check-stop-from-handler step missing entirely — exit 1
NO_SFH="$WORK_DIR/no_sfh.yml"
awk '
  /Run stop\(\)-from-handler/ { skip=1 }
  skip==1 { next }
  { print }
' "$GOOD" > "$NO_SFH"
if ! bash "$SCRIPT" "$NO_SFH" >/dev/null 2>&1; then
    ok "missing check-stop-from-handler step exits 1"
else
    fail "missing check-stop-from-handler step should exit 1"
fi

# Test 4: check-route-table-concurrency step present but not gated on tsan — exit 1
WRONG_RTC_IF="$WORK_DIR/wrong_rtc_if.yml"
sed "s/if: \${{ matrix.build-type == 'tsan' }}/if: \${{ matrix.build-type == 'ubsan' }}/" "$GOOD" > "$WRONG_RTC_IF"
if ! bash "$SCRIPT" "$WRONG_RTC_IF" >/dev/null 2>&1; then
    ok "check-route-table-concurrency step not tsan-gated exits 1"
else
    fail "check-route-table-concurrency step not tsan-gated should exit 1"
fi

# Test 5: check-stop-from-handler step present but not pinned to the baseline
# lane — exit 1.
WRONG_SFH_IF="$WORK_DIR/wrong_sfh_if.yml"
sed "s/matrix.build-type == 'classic'/matrix.build-type == 'asan'/" "$GOOD" > "$WRONG_SFH_IF"
if ! bash "$SCRIPT" "$WRONG_SFH_IF" >/dev/null 2>&1; then
    ok "check-stop-from-handler step not baseline-lane-gated exits 1"
else
    fail "check-stop-from-handler step not baseline-lane-gated should exit 1"
fi

# Test 6: check-route-table-concurrency step present but missing timeout-minutes — exit 1
NO_RTC_TIMEOUT="$WORK_DIR/no_rtc_timeout.yml"
awk '
  /Run route_table_concurrency under TSan/ { print; getline; if ($0 !~ /timeout-minutes/) print; next }
  { print }
' "$GOOD" > "$NO_RTC_TIMEOUT"
if ! bash "$SCRIPT" "$NO_RTC_TIMEOUT" >/dev/null 2>&1; then
    ok "check-route-table-concurrency step missing timeout-minutes exits 1"
else
    fail "check-route-table-concurrency step missing timeout-minutes should exit 1"
fi

# Test 7: invalid YAML — exit 1
BAD_YAML="$WORK_DIR/bad.yml"
cat > "$BAD_YAML" <<'EOF'
name: verify
jobs:
  verify:
    steps:
      - name: broken
        : this is not valid yaml : : :
      unbalanced: [
EOF
if ! bash "$SCRIPT" "$BAD_YAML" >/dev/null 2>&1; then
    ok "invalid YAML exits 1"
else
    fail "invalid YAML should exit 1"
fi

# Test 8: workflow file does not exist (missing-workflow-file guard) — exit 1
if ! bash "$SCRIPT" "$WORK_DIR/does_not_exist.yml" >/dev/null 2>&1; then
    ok "missing workflow file exits 1"
else
    fail "missing workflow file should exit 1"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
