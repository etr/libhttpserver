#!/usr/bin/env bash
#
# skip-or-fail.sh — shared helper for check-parallel-install.sh (TASK-089).
#
# Sourced (not executed) by the gate script to define skip_or_fail(), the
# exit-code contract governing the five environment-quirk paths (master ref
# missing, git worktree add failed, v1 bootstrap/configure/make failed).
#
# Before TASK-089 those paths degraded to `exit 0` (SKIP-becomes-pass), which
# meant a shallow CI clone or a v1 tree that could not build silently reported
# the TASK-044 parallel-install acceptance gate as green. skip_or_fail()
# closes that hole: a SKIP now FAILS the job (exit 1) UNLESS the environment
# variable HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP is set to exactly `1`, which
# is the documented escape hatch for genuine infrastructure breakage.
#
# Contract of `skip_or_fail "<message>"`:
#   * Always prints `check-parallel-install: SKIP: <message>` to stderr.
#   * If HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP == exactly "1": prints an
#     "authorized" note and `exit 0`.
#   * Otherwise: prints a "not authorized — failing" note and `exit 1`.
#   * Only the literal value "1" authorizes; "0", "", "yes", etc. do not.
#
# Design note: no top-level `exit` runs at source time — the exit lives only
# inside the function, so sourcing the helper is side-effect free and safe to
# unit-test in a subshell.

skip_or_fail() {
    local msg="$*"
    echo "check-parallel-install: SKIP: $msg" >&2
    if [ "${HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP:-}" = "1" ]; then
        echo "check-parallel-install: skip authorized via HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1 — exiting 0" >&2
        exit 0
    fi
    echo "check-parallel-install: skip NOT authorized — failing the job (set HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1 to allow)" >&2
    exit 1
}
