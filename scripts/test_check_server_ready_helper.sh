#!/usr/bin/env bash
# Unit test for check-server-ready-helper.sh
#
# Verifies the gate's detection logic against a synthetic test/integ/ tree:
#   1.  No hooks_*.cpp files                         -> exit 0
#   2.  A file using the shared helper               -> exit 0
#   3.  A file with a bare server-ready sleep        -> exit 1
#   4.  A file with bare sleep + allow-list marker
#       on the previous non-blank line               -> exit 0
#   4b. A file with bare sleep + allow-list marker
#       on the SAME line as the sleep_for call       -> exit 0
#   5.  A non-hooks_*.cpp file with a bare sleep     -> exit 0 (out-of-scope)
#   5b. A hooks_*.cpp file with a non-milliseconds
#       bare sleep (e.g. std::chrono::seconds)       -> exit 1
#
# Run from any directory; uses a temporary directory for fixtures.
set -euo pipefail

PASS=0
FAIL=0

ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }
reset_fixtures() { rm -f "$FAKE_REPO/test/integ"/*.cpp; }

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check-server-ready-helper.sh"

TMPDIR_BASE="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_BASE"' EXIT

FAKE_REPO="$TMPDIR_BASE/repo"
mkdir -p "$FAKE_REPO/scripts" "$FAKE_REPO/test/integ"

cp "$SCRIPT" "$FAKE_REPO/scripts/check-server-ready-helper.sh"
chmod +x "$FAKE_REPO/scripts/check-server-ready-helper.sh"

# Test 1: empty test/integ -> exit 0
if (cd "$FAKE_REPO" && bash scripts/check-server-ready-helper.sh 2>/dev/null); then
    ok "empty test/integ exits 0"
else
    fail "empty test/integ should exit 0"
fi

# Test 2: a hooks_*.cpp file using the shared helper -> exit 0
cat > "$FAKE_REPO/test/integ/hooks_clean.cpp" <<'EOF'
#include "./server_ready.hpp"
void f(int port) {
    httpserver_test::wait_for_server_ready(port);
}
EOF
if (cd "$FAKE_REPO" && bash scripts/check-server-ready-helper.sh 2>/dev/null); then
    ok "clean hooks_*.cpp exits 0"
else
    fail "clean hooks_*.cpp should exit 0"
fi

# Test 3: a hooks_*.cpp file with a bare server-ready sleep -> exit 1
reset_fixtures
cat > "$FAKE_REPO/test/integ/hooks_bare.cpp" <<'EOF'
#include <chrono>
#include <thread>
void f() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-server-ready-helper.sh 2>/dev/null); then
    ok "bare sleep exits 1"
else
    fail "bare sleep should exit 1"
fi

# Test 4: a hooks_*.cpp file with bare sleep + allow-list marker on the
# previous non-blank line -> exit 0
reset_fixtures
cat > "$FAKE_REPO/test/integ/hooks_marker.cpp" <<'EOF'
#include <chrono>
#include <thread>
void f() {
    // NON-READINESS-SLEEP: testing connect timeout semantics
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
EOF
if (cd "$FAKE_REPO" && bash scripts/check-server-ready-helper.sh 2>/dev/null); then
    ok "marker allows sleep, exits 0"
else
    fail "marker should allow sleep and exit 0"
fi

# Test 4b: a hooks_*.cpp file with bare sleep + allow-list marker on the
# SAME line as the sleep_for call -> exit 0. Independently pins the
# NR==target same-line branch of the awk detection logic (distinct from
# Test 4's previous-non-blank-line branch).
reset_fixtures
cat > "$FAKE_REPO/test/integ/hooks_marker_sameline.cpp" <<'EOF'
#include <chrono>
#include <thread>
void f() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // NON-READINESS-SLEEP: testing connect timeout semantics
}
EOF
if (cd "$FAKE_REPO" && bash scripts/check-server-ready-helper.sh 2>/dev/null); then
    ok "same-line marker allows sleep, exits 0"
else
    fail "same-line marker should allow sleep and exit 0"
fi

# Test 5: non-hooks_*.cpp file with a bare sleep -> out of scope, exit 0
reset_fixtures
cat > "$FAKE_REPO/test/integ/other_test.cpp" <<'EOF'
#include <chrono>
#include <thread>
void f() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
EOF
if (cd "$FAKE_REPO" && bash scripts/check-server-ready-helper.sh 2>/dev/null); then
    ok "non-hooks_*.cpp out of scope, exits 0"
else
    fail "non-hooks_*.cpp should be out of scope"
fi

# Test 5b: a hooks_*.cpp file with a bare non-milliseconds sleep (e.g.
# std::chrono::seconds) -> exit 1. Pins the broadened detection pattern,
# which catches sleep_for regardless of duration unit.
reset_fixtures
cat > "$FAKE_REPO/test/integ/hooks_seconds.cpp" <<'EOF'
#include <chrono>
#include <thread>
void f() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
}
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-server-ready-helper.sh 2>/dev/null); then
    ok "non-milliseconds bare sleep exits 1"
else
    fail "non-milliseconds bare sleep should exit 1"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
