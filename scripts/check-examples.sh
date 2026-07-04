#!/usr/bin/env bash
#
# check-examples.sh — enforce TASK-040 invariants on examples/.
#
# This script is a static-source check, not a build check. It asserts that:
#
#   1. examples/hello_world.cpp exists, is at most 10 lines (by the rule
#      defined below), defines no class deriving from http_resource, and
#      registers nothing via raw pointers.
#
#   2. examples/shared_state.cpp exists, defines a class deriving from
#      http_resource, holds a std::mutex, overrides both render_get and
#      render_post, and registers itself via register_path with
#      std::make_unique.
#
#   3. Every program listed in examples/Makefile.am noinst_PROGRAMS has a
#      corresponding .cpp source file on disk. This catches renames or
#      deletions that would silently break `make examples`.
#
# LOC counting rule (TASK-040 plan D2):
#   The LOC count is the number of non-empty, non-comment lines from the
#   first non-comment line to EOF, after stripping any leading /* ... */
#   license header. Implemented with awk below.
#
# Exits non-zero on the first violation.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HELLO="$REPO_ROOT/examples/hello_world.cpp"
SHARED="$REPO_ROOT/examples/shared_state.cpp"
MAKEFILE_AM="$REPO_ROOT/examples/Makefile.am"

fail() {
    echo "check-examples: FAIL: $*" >&2
    exit 1
}

count_loc() {
    # Strip leading /* ... */ block(s), then count non-blank non-// lines.
    awk '
        BEGIN { in_block = 0; seen_first = 0 }
        {
            line = $0
            # Handle block comments.
            if (in_block) {
                if (line ~ /\*\//) { in_block = 0; sub(/.*\*\//, "", line) }
                else { next }
            }
            # Strip inline block-open comments that close on the same line.
            while (match(line, /\/\*[^*]*\*\//)) {
                line = substr(line, 1, RSTART-1) substr(line, RSTART+RLENGTH)
            }
            if (match(line, /\/\*/)) {
                in_block = 1
                line = substr(line, 1, RSTART-1)
            }
            # Strip line comments.
            sub(/\/\/.*/, "", line)
            # Trim whitespace.
            gsub(/^[ \t\r]+|[ \t\r]+$/, "", line)
            if (line == "") next
            count++
        }
        END { print count + 0 }
    ' "$1"
}

# ---- hello_world.cpp ----------------------------------------------------

[ -f "$HELLO" ] || fail "examples/hello_world.cpp does not exist"

loc=$(count_loc "$HELLO")
if [ "$loc" -gt 10 ]; then
    fail "examples/hello_world.cpp is $loc LOC, must be <= 10"
fi
if [ "$loc" -lt 5 ]; then
    fail "examples/hello_world.cpp is $loc LOC, suspiciously short (must be >= 5)"
fi

if grep -Eq 'class[[:space:]]+[A-Za-z_][A-Za-z_0-9]*[[:space:]]*:[[:space:]]*public[[:space:]]+[A-Za-z_:]*http_resource' "$HELLO"; then
    fail "examples/hello_world.cpp defines an http_resource subclass; the lambda form is required"
fi

# No raw-pointer registration like ws.register_path("/x", new Foo()).
if grep -Eq 'register_(path|prefix|resource)[[:space:]]*\([^,]*,[[:space:]]*new[[:space:]]+' "$HELLO"; then
    fail "examples/hello_world.cpp uses raw-pointer registration; use lambda or smart-pointer form"
fi

# ---- shared_state.cpp ---------------------------------------------------

[ -f "$SHARED" ] || fail "examples/shared_state.cpp does not exist"

grep -Eq ':[[:space:]]*public[[:space:]]+[A-Za-z_:]*http_resource' "$SHARED" \
    || fail "examples/shared_state.cpp must define a class inheriting from http_resource"

grep -q 'std::mutex' "$SHARED" \
    || fail "examples/shared_state.cpp must use std::mutex to demonstrate locking"

grep -Eq '\brender_get\b' "$SHARED" \
    || fail "examples/shared_state.cpp must override render_get"

grep -Eq '\brender_post\b' "$SHARED" \
    || fail "examples/shared_state.cpp must override render_post"

grep -Eq 'register_path[[:space:]]*\(.*std::make_unique' "$SHARED" \
    || fail "examples/shared_state.cpp must register via register_path with std::make_unique"

# ---- Makefile.am coverage ---------------------------------------------------
# Two-directional check:
#   (a) Every program listed in noinst_PROGRAMS must have a .cpp source file.
#   (b) Every .cpp in examples/ must be listed in noinst_PROGRAMS or be an
#       explicitly acknowledged non-program artifact.
#
# KNOWN_ARTIFACTS: .cpp files that are intentionally not in noinst_PROGRAMS.
# Empty by design: every example .cpp — including client_cert_auth.cpp, now
# built under the HAVE_GNUTLS conditional in examples/Makefile.am (TASK-091) —
# is listed in noinst_PROGRAMS, so nothing is allow-listed out of coverage.
KNOWN_ARTIFACTS=""

[ -f "$MAKEFILE_AM" ] || fail "examples/Makefile.am does not exist"

# Extract all tokens from noinst_PROGRAMS lines (handles = and +=).
# Strip the variable name and assignment operator, then collect program names.
# Use awk for portability (avoids sed \s and \? which differ between GNU/BSD).
programs="$(awk '/^[[:space:]]*noinst_PROGRAMS[[:space:]]*(=|\+=)/ {
    sub(/^[[:space:]]*noinst_PROGRAMS[[:space:]]*(=|\+=)[[:space:]]*/, "")
    gsub(/\\$/, "")
    print
}' "$MAKEFILE_AM")"

# (a) Makefile.am → disk: every listed program must have a .cpp file.
missing=0
for prog in $programs; do
    src="$REPO_ROOT/examples/${prog}.cpp"
    if [ ! -f "$src" ]; then
        echo "check-examples: FAIL: noinst_PROGRAMS lists '$prog' but examples/${prog}.cpp does not exist" >&2
        missing=$((missing + 1))
    fi
done
if [ "$missing" -gt 0 ]; then
    fail "$missing program(s) listed in Makefile.am noinst_PROGRAMS have no .cpp source"
fi

# (b) disk → Makefile.am: every .cpp must be in noinst_PROGRAMS or KNOWN_ARTIFACTS.
unlisted=0
for src in "$REPO_ROOT"/examples/*.cpp; do
    base="$(basename "$src" .cpp)"
    # Check if in noinst_PROGRAMS.
    found=0
    for prog in $programs; do
        if [ "$prog" = "$base" ]; then
            found=1
            break
        fi
    done
    if [ "$found" -eq 0 ]; then
        # Check if in KNOWN_ARTIFACTS allowlist.
        in_artifacts=0
        for artifact in $KNOWN_ARTIFACTS; do
            if [ "$artifact" = "$base" ]; then
                in_artifacts=1
                break
            fi
        done
        if [ "$in_artifacts" -eq 0 ]; then
            echo "check-examples: FAIL: examples/${base}.cpp is on disk but not listed in noinst_PROGRAMS or KNOWN_ARTIFACTS" >&2
            unlisted=$((unlisted + 1))
        fi
    fi
done
if [ "$unlisted" -gt 0 ]; then
    fail "$unlisted .cpp file(s) in examples/ are not listed in Makefile.am noinst_PROGRAMS — add them or add to KNOWN_ARTIFACTS"
fi

# ---- TASK-052: hook-example documentation coverage --------------------------
# The four lifecycle-hook examples (banned_ip_log, early_413, clf_access_log,
# per_route_auth) must be listed in both examples/README.md and the top-level
# README.md so the user-visible resolution of issues #332, #281, #69, #273 is
# discoverable. Per TASK-052 / Phase 3.
EXAMPLES_README="$REPO_ROOT/examples/README.md"
TOP_README="$REPO_ROOT/README.md"
HOOK_EXAMPLES="banned_ip_log early_413 clf_access_log per_route_auth"

for f in "$EXAMPLES_README" "$TOP_README"; do
    [ -f "$f" ] || fail "$(basename "$f") does not exist"
done

missing_doc=0
for ex in $HOOK_EXAMPLES; do
    if ! grep -q "${ex}\\.cpp" "$EXAMPLES_README"; then
        echo "check-examples: FAIL: examples/README.md does not mention ${ex}.cpp" >&2
        missing_doc=$((missing_doc + 1))
    fi
    if ! grep -q "${ex}\\.cpp" "$TOP_README"; then
        echo "check-examples: FAIL: README.md does not mention ${ex}.cpp" >&2
        missing_doc=$((missing_doc + 1))
    fi
done
if [ "$missing_doc" -gt 0 ]; then
    fail "$missing_doc hook-example reference(s) missing from README docs (TASK-052)"
fi

echo "check-examples: OK (hello_world.cpp = $loc LOC; shared_state.cpp asserted; Makefile.am coverage verified bidirectionally; hook examples listed in both READMEs)"
exit 0
