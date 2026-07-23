#!/usr/bin/env bash
#
# check-markdownlint.sh — shared markdownlint advisory-vs-strict helper for
# check-readme.sh and check-release-notes.sh (TASK-091).
#
# Sourced (not executed) by both gate scripts to avoid maintaining two
# near-identical copies of the advisory-vs-strict decision logic.
#
# Inputs (must be defined by the caller before sourcing):
#   fail — a shell function that prints a message and exits non-zero.
#
# Usage (after sourcing): run_markdownlint_advisory <label> <file>
#   <label> — the caller's script prefix used in messages, e.g. "check-readme".
#   <file>  — the Markdown file to lint.
#
# Behavior: if markdownlint is not on PATH, this is a silent no-op (matches
# the historical inline blocks — markdownlint is an optional local/CI tool,
# not a hard prerequisite). If markdownlint reports issues, the finding fails
# the caller via fail() UNLESS LIBHTTPSERVER_MARKDOWNLINT_ADVISORY=1 or the
# legacy MARKDOWNLINT_STRICT=no is set, in which case it downgrades to an
# advisory NOTE on stderr and does not fail the caller.
#
# The `2>&1` on the markdownlint invocation merges markdownlint's stderr onto
# this process's stdout; it does not send findings "to stderr" (a prior
# version of this comment said otherwise) — CI captures both streams
# regardless, so findings remain visible either way.
#
# Design note: no top-level `exit` runs at source time — exits happen only
# inside the function (via the caller's fail()), so sourcing is side-effect
# free.

run_markdownlint_advisory() {
    local label="$1" target="$2"
    if command -v markdownlint >/dev/null 2>&1; then
        if ! markdownlint -q "$target" 2>&1; then
            if [ "${LIBHTTPSERVER_MARKDOWNLINT_ADVISORY:-0}" = "1" ] \
               || [ "${MARKDOWNLINT_STRICT:-yes}" = "no" ]; then
                echo "${label}: NOTE: markdownlint reported issues above (advisory only, not gating)" >&2
            else
                fail "markdownlint reported issues (set LIBHTTPSERVER_MARKDOWNLINT_ADVISORY=1 to downgrade to advisory)"
            fi
        fi
    fi
}
