#!/usr/bin/env bash
#
# test_check_parallel_install.sh — unit test for scripts/lib/skip-or-fail.sh
# (TASK-089).
#
# The helper defines skip_or_fail(), the exit-code contract that governs the
# five environment-quirk paths in check-parallel-install.sh. Before TASK-089
# those paths degraded to `exit 0` (SKIP-becomes-pass), so a shallow-clone or
# broken-v1-build CI runner silently reported the parallel-install acceptance
# gate as green. skip_or_fail() flips that: a SKIP now FAILS the job (exit 1)
# unless HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1 explicitly authorizes it.
#
# This test sources the helper in isolated subshells and asserts the
# exit-code contract without triggering check-parallel-install.sh's heavy
# Phase-1 build. Mirrors the PASS/FAIL harness style of
# test_check_valgrind_lane.sh.
#
# -e is intentionally omitted so a subshell that exits non-zero (the expected
# outcome for most cases) does not abort the harness before we capture $?.
set -uo pipefail

PASS=0
FAIL=0
ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

HELPER="$(cd "$(dirname "$0")" && pwd)/lib/skip-or-fail.sh"

if [ ! -f "$HELPER" ]; then
    echo "  FAIL: helper not found: $HELPER" >&2
    echo ""
    echo "Results: 0 passed, 1 failed"
    exit 1
fi

ERRLOG="$(mktemp)"
trap 'rm -f "$ERRLOG"' EXIT

# Run skip_or_fail in a clean subshell with a controlled value of
# HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP. stderr is captured to $ERRLOG; the
# subshell's exit code is returned to the caller via the trailing $?.
#   $1 = value to assign, or the sentinel __unset__ to leave it unset
#   $2 = the SKIP message text
run_case() {
    local val="$1"
    local msg="$2"
    (
        set +e
        if [ "$val" = "__unset__" ]; then
            unset HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP
        else
            export HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP="$val"
        fi
        # shellcheck source=scripts/lib/skip-or-fail.sh
        source "$HELPER"
        skip_or_fail "$msg"
    ) 2>"$ERRLOG"
}

# Assert exit code and that stderr always carries the SKIP marker + message.
assert_case() {
    local label="$1"
    local val="$2"
    local msg="$3"
    local want_rc="$4"
    run_case "$val" "$msg"
    local rc=$?
    local passed=1

    # Evaluate all three checks independently (rather than returning on
    # the first failure) so a single bad run reports every symptom, not
    # just the first one encountered.
    if [ "$rc" -ne "$want_rc" ]; then
        fail "$label: expected exit $want_rc, got $rc"
        passed=0
    fi
    if ! grep -q "SKIP:" "$ERRLOG"; then
        fail "$label: stderr missing 'SKIP:' marker"
        passed=0
    fi
    if ! grep -qF "$msg" "$ERRLOG"; then
        fail "$label: stderr missing message text '$msg'"
        passed=0
    fi
    if [ "$passed" -eq 1 ]; then
        ok "$label: exit $rc, stderr carries SKIP + message"
    fi
}

# 1. Unauthorized (unset) — a SKIP must FAIL the job.
assert_case "unset -> fail" "__unset__" "master ref missing" 1

# 2. Authorized with the exact literal 1 — SKIP is allowed, exit 0.
assert_case "=1 -> pass" "1" "git worktree add failed" 0

# 3. =0 must NOT authorize (only the literal 1 does).
assert_case "=0 -> fail" "0" "v1 bootstrap failed" 1

# 4. =yes must NOT authorize.
assert_case "=yes -> fail" "yes" "v1 configure failed" 1

# 5. Empty string must NOT authorize.
assert_case "=empty -> fail" "" "v1 make failed" 1

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
