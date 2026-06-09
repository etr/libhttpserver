#!/usr/bin/env bash
#
# check-server-ready-helper.sh — guard against bare server-ready sleeps
# in the hooks_* integration tests (TASK-075).
#
# TASK-049 introduced wait_for_server_ready (now extracted to
# test/integ/server_ready.hpp) to replace the fixed 50 ms sleep used as a
# server-ready wait in test/integ/hooks_*.cpp. The old pattern races with
# MHD_start_daemon on loaded CI runners and causes intermittent
# CURLE_COULDNT_CONNECT failures. TASK-075 propagated the helper to every
# affected file; this gate prevents the regression from reappearing.
#
# Watched files: every test/integ/hooks_*.cpp at scan time. Discovery is
# dynamic so newly-added hooks_*.cpp files are automatically gated
# without updating a static list.
#
# Detection logic:
#   1. Any line matching `std::this_thread::sleep_for(.*ms)` or
#      `std::this_thread::sleep_for(std::chrono::milliseconds(...))` in a
#      watched file is a candidate violation. Whitespace before the call
#      is allowed; the call must syntactically be at statement scope.
#   2. A candidate is allowed iff the candidate line OR the previous
#      non-blank line contains the marker
#          // NON-READINESS-SLEEP: <reason>
#      This is the documented escape hatch for legitimate non-readiness
#      sleeps (e.g. a test deliberately exercising connection-timeout
#      semantics). The marker must include a free-text reason after the
#      colon.
#   3. Any candidate without an allowance marker is reported as a
#      violation. Exit 1 if any violation is found, 0 otherwise.
#
# Exit codes:
#   0  no violations
#   1  one or more watched files carries a bare server-ready sleep
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

WATCHED_FILES=()
while IFS= read -r f; do
    WATCHED_FILES+=("$f")
done < <(find test/integ -maxdepth 1 -name 'hooks_*.cpp' 2>/dev/null | sort)

echo "check-server-ready-helper: scanning ${#WATCHED_FILES[@]} file(s)"

violations=0
# Guard against the empty-array case: bash's `${arr[@]}` is unset rather
# than empty under `set -u`. The `${arr[@]+"${arr[@]}"}` idiom expands to
# nothing when the array is empty and to the expected element list
# otherwise. Without this, an empty test/integ/ directory would crash
# the gate.
for file in ${WATCHED_FILES[@]+"${WATCHED_FILES[@]}"}; do
    # Find every candidate sleep_for line. The pattern matches either
    # the .*ms literal form or the std::chrono::milliseconds(...) form,
    # with optional leading whitespace.
    while IFS=: read -r lineno _; do
        # An allow-marker on the candidate line itself, or on the
        # nearest previous non-blank line, exempts the sleep.
        allowed=$(awk -v target="$lineno" '
            NR == target {
                if ($0 ~ /\/\/[[:space:]]*NON-READINESS-SLEEP:/) {
                    print "yes"; exit
                }
                # Look backwards for the nearest non-blank line.
                for (i = target - 1; i >= 1; i--) {
                    if (lines[i] !~ /^[[:space:]]*$/) {
                        if (lines[i] ~ /\/\/[[:space:]]*NON-READINESS-SLEEP:/) {
                            print "yes"
                        } else {
                            print "no"
                        }
                        exit
                    }
                }
                print "no"
            }
            { lines[NR] = $0 }
        ' "$file")

        if [ "$allowed" != "yes" ]; then
            echo "  $file:$lineno: bare std::this_thread::sleep_for used as server-ready wait" >&2
            violations=$((violations + 1))
        fi
    done < <(grep -nE '^[[:space:]]*std::this_thread::sleep_for\(([^)]*ms\)|std::chrono::milliseconds)' "$file" || true)
done

if [ "$violations" -gt 0 ]; then
    echo "check-server-ready-helper: FAIL — $violations bare server-ready sleep(s) found" >&2
    echo "  Replace with httpserver_test::wait_for_server_ready(PORT) from test/integ/server_ready.hpp," >&2
    echo "  or add a '// NON-READINESS-SLEEP: <reason>' marker if the sleep is intentionally not a readiness wait." >&2
    exit 1
fi

echo "check-server-ready-helper: PASS — no bare server-ready sleeps in hooks_* integ tests"
