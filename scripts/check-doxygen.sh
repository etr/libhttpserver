#!/usr/bin/env bash
#
# check-doxygen.sh — enforce TASK-043 invariant on the doxygen build.
#
# Runs `make doxygen-run` in the active build tree and asserts that the
# output contains zero substantive warnings or errors. Substantive means:
# anything from the doxygen parser about the SOURCE under
# `src/httpserver/` — e.g. an undocumented @param, a mismatched parameter
# name, a stale @copydoc target.
#
# Lines we deliberately filter OUT (environmental, not doc-content issues):
#
#   E1. "Tag '<NAME>' at line N of file '...doxyconfig.in' has become
#       obsolete." — older config tags carried over from doxywizard.
#   E2. "doxygen no longer ships with the FreeSans font." — packaging.
#   E3. "the dot tool could not be found ..." — graphviz absent locally.
#   E4. "Failed to rename ... .dot.png to ... .png" — dot post-processing
#       failure (typically dot absent or installed late).
#   E5. "Problems running dot: exit code=..." — same root cause as E4.
#   E6. "Caller graph for '<sym>' not generated, too many nodes (N),
#       threshold is M." — informational, DOT_GRAPH_MAX_NODES threshold.
#
# Everything else under the keywords `warning:` or `error:` is a real
# doc issue and FAILS the gate. Exits non-zero on the first violation.
#
# Behaviour when doxygen is not installed: SKIP (exit 0). The gate is
# CI-runnable on developer machines without doxygen; CI is expected to
# install it. This mirrors how scripts/check-readme.sh and friends are
# tolerant of missing tools.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Locate the build tree. Prefer $BUILD_DIR, else REPO_ROOT/build, else
# fall back to the current working directory (assumes invoker is already
# inside the build dir, mirroring `make` behaviour).
BUILD_DIR="${BUILD_DIR:-}"
if [ -z "$BUILD_DIR" ]; then
    if [ -d "$REPO_ROOT/build" ] && [ -f "$REPO_ROOT/build/Makefile" ]; then
        BUILD_DIR="$REPO_ROOT/build"
    elif [ -f "Makefile" ]; then
        BUILD_DIR="$(pwd)"
    else
        echo "check-doxygen: FAIL: no build directory found (set BUILD_DIR)" >&2
        exit 1
    fi
fi

# Sanity-guard: BUILD_DIR must be an absolute path to prevent a
# caller-controlled relative value from silently resolving to an
# unexpected location (e.g. via a symlink or a shell cd side-effect).
# This also catches the common mistake of exporting BUILD_DIR=build
# (relative) rather than BUILD_DIR="$(pwd)/build" (absolute).
if [[ "$BUILD_DIR" != /* ]]; then
    echo "check-doxygen: FAIL: BUILD_DIR must be an absolute path (got: '$BUILD_DIR')" >&2
    exit 1
fi

if ! command -v doxygen >/dev/null 2>&1; then
    echo "check-doxygen: SKIP — doxygen not installed (gate is enforced in CI)"
    exit 0
fi

# mktemp semantics differ between macOS (-t places file under /var/folders)
# and Linux (-t places the template suffix after any system-chosen prefix).
# Using an explicit path under $BUILD_DIR is more portable and avoids
# leaving build metadata in world-readable /tmp on shared build hosts.
LOGFILE="$(mktemp "$BUILD_DIR/check-doxygen.XXXXXX")"
trap 'rm -f "$LOGFILE"' EXIT

echo "check-doxygen: invoking 'make doxygen-run' in $BUILD_DIR"
# Force a fresh doxygen invocation.
# The make rule only fires when an input file is newer than the tag file.
# Removing the tag file forces the rule to fire every time so CI reruns
# and local re-checks always exercise the current source.
# NOTE: the tag file path is derived from Doxyfile OUTPUT_DIRECTORY and
# PROJECT_NAME; if either changes this rm becomes a no-op and the
# freshness guarantee is silently lost. Keep this path in sync with
# doxyconfig.in (currently: OUTPUT_DIRECTORY=doxygen-doc, PROJECT_NAME=libhttpserver).
rm -f "$BUILD_DIR/doxygen-doc/libhttpserver.tag"
if ! ( cd "$BUILD_DIR" && make doxygen-run ) >"$LOGFILE" 2>&1; then
    echo "check-doxygen: FAIL: 'make doxygen-run' exited non-zero" >&2
    sed -n '1,200p' "$LOGFILE" >&2
    exit 1
fi

# Sanity-check that doxygen actually ran and produced output.
# A zero-byte log means doxygen was skipped by make (e.g. an incomplete
# freshness-force above) — treat this as a failure rather than a false PASS.
if [ ! -s "$LOGFILE" ]; then
    echo "check-doxygen: FAIL: doxygen log is empty — doxygen may have been skipped by make" >&2
    exit 1
fi

# Strip the noisy-but-harmless environmental lines. The grep is portable
# (BSD/GNU): -E for ERE alternation, single anchored pattern per line.
# Representative match for each arm (from real doxygen output):
#   E1: "Tag 'HTML_FOOTER' at line 42 of file 'doxyconfig.in' has become obsolete."
#   E2: "doxygen no longer ships with the FreeSans font"
#   E3: "the dot tool could not be found at /usr/bin/dot"
#   E4: "Failed to rename /build/doxygen-doc/foo.dot.png to /build/doxygen-doc/foo.png"
#   E5: "Problems running dot: exit code=1, command='dot ...'"
#   E6: "Caller graph for 'webserver::start' not generated, too many nodes (300), threshold is 50."
FILTER='Tag .* has become obsolete\.'          # E1: obsolete config tag
FILTER+='|doxygen no longer ships with the FreeSans font'  # E2: font packaging
FILTER+='|the dot tool could not be found'     # E3: graphviz absent
FILTER+='|Failed to rename .*\.dot\.png to'   # E4: dot post-processing failure
FILTER+='|Problems running dot: exit code='   # E5: same root cause as E4
FILTER+='|Caller graph for .* not generated, too many nodes'  # E6: DOT_GRAPH_MAX_NODES

REAL_WARNINGS="$(grep -E '(^|: )(warning|error):' "$LOGFILE" | grep -Ev "$FILTER" || true)"

if [ -n "$REAL_WARNINGS" ]; then
    echo "check-doxygen: FAIL: substantive doxygen warnings/errors found:" >&2
    echo "$REAL_WARNINGS" >&2
    echo "" >&2
    echo "(Full log: $LOGFILE — copy aside if needed before this script exits.)" >&2
    # Preserve log on failure for inspection.
    trap - EXIT
    exit 1
fi

echo "check-doxygen: PASS — doxygen-run produced zero substantive warnings"
