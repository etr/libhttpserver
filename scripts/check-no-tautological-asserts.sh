#!/usr/bin/env bash
#
# TASK-076 regression gate: forbid `LT_CHECK_EQ(1, 1)` in
# test/integ/ws_start_stop.cpp.
#
# The pattern was used as a fake "skip" before LT_SKIP / LT_SKIP_IF
# existed in test/littletest.hpp. A build that silently lost TLS support
# would still report green because every catch block in the TLS suite
# swallowed the exception with a tautological `LT_CHECK_EQ(1, 1)`.
# TASK-076 replaced all 33 occurrences with either:
#   (a) `LT_SKIP_IF(cond, reason)` for environment-conditional skips
#       (e.g. gnutls-cli absent from PATH); or
#   (b) a real postcondition assertion / typed catch + re-throw for
#       cases where the implementation should be exercised; or
#   (c) deletion of redundant tail-position liveness pseudo-asserts.
#
# This gate keeps the regression visible by failing if any future patch
# reintroduces the pattern in ws_start_stop.cpp. The scope is
# intentionally narrow — `test/integ/basic.cpp:1877` and
# `test/unit/no_v1_compat_shim_test.cpp:96` both use the same textual
# pattern as intentional liveness/compile-sentinel assertions and are
# NOT covered by this gate (the task wording scopes only to
# ws_start_stop.cpp).
#

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FILE="$REPO_ROOT/test/integ/ws_start_stop.cpp"

if [ ! -f "$FILE" ]; then
    echo "ERROR: $FILE missing" >&2
    exit 1
fi

if grep -nE 'LT_CHECK_EQ\(1, *1\)' "$FILE"; then
    echo "ERROR: LT_CHECK_EQ(1, 1) found in $FILE." >&2
    echo "       Use LT_SKIP_IF(...) for environment-conditional skips" >&2
    echo "       (see TASK-076). The tautological assertion masks broken" >&2
    echo "       TLS builds as PASS instead of SKIP." >&2
    exit 1
fi

echo "PASS — no tautological LT_CHECK_EQ(1, 1) in $(realpath --relative-to="$REPO_ROOT" "$FILE" 2>/dev/null || echo "$FILE")"
