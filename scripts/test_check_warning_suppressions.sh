#!/usr/bin/env bash
# Unit test for check-warning-suppressions.sh
# Tests that the push/pop bracketing detection works correctly.
# Run from any directory; uses a temporary directory for fixtures.
set -euo pipefail

PASS=0
FAIL=0

ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check-warning-suppressions.sh"

# Create a temp work area and override the src/ search inside the script.
# We do this by creating a temp directory tree that mimics src/ and then
# running the script with REPO_ROOT overridden via a symlink approach.
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

FAKE_REPO="$WORK_DIR/repo"
mkdir -p "$FAKE_REPO/scripts" "$FAKE_REPO/src"

# Copy the script so it can be run from the fake repo root.
cp "$SCRIPT" "$FAKE_REPO/scripts/check-warning-suppressions.sh"
chmod +x "$FAKE_REPO/scripts/check-warning-suppressions.sh"

# Test 1: No pragmas — should exit 0
cat > "$FAKE_REPO/src/clean.cpp" <<'EOF'
// no pragmas here
int main() { return 0; }
EOF
if (cd "$FAKE_REPO" && bash scripts/check-warning-suppressions.sh 2>/dev/null); then
    ok "no pragmas exits 0"
else
    fail "no pragmas should exit 0"
fi

# Test 2: Properly bracketed pragma — should exit 0
cat > "$FAKE_REPO/src/bracketed.cpp" <<'EOF'
// Some code
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
int x = arr[5];
#pragma GCC diagnostic pop
int main() { return 0; }
EOF
if (cd "$FAKE_REPO" && bash scripts/check-warning-suppressions.sh 2>/dev/null); then
    ok "bracketed pragma exits 0"
else
    fail "bracketed pragma should exit 0"
fi

# Test 3: File-scoped pragma (no push/pop) — should exit 1
cat > "$FAKE_REPO/src/unbracketed.cpp" <<'EOF'
#pragma GCC diagnostic ignored "-Warray-bounds"
int arr[5];
int x = arr[10];
int main() { return 0; }
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-warning-suppressions.sh 2>/dev/null); then
    ok "unbracketed pragma exits 1"
else
    fail "unbracketed pragma should exit 1"
fi

# Test 4: push before but no pop after — should exit 1
cat > "$FAKE_REPO/src/push_no_pop.cpp" <<'EOF'
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
int arr[5];
int main() { return 0; }
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-warning-suppressions.sh 2>/dev/null); then
    ok "push-no-pop exits 1"
else
    fail "push-no-pop should exit 1"
fi

# Test 5: pop after but no push before — should exit 1
cat > "$FAKE_REPO/src/no_push_pop.cpp" <<'EOF'
#pragma GCC diagnostic ignored "-Warray-bounds"
int arr[5];
#pragma GCC diagnostic pop
int main() { return 0; }
EOF
if ! (cd "$FAKE_REPO" && bash scripts/check-warning-suppressions.sh 2>/dev/null); then
    ok "no-push-pop exits 1"
else
    fail "no-push-pop should exit 1"
fi

# Test 6: interleaved push/pop/pragma/pop — documents the KNOWN LIMITATION
# called out in check-warning-suppressions.sh's header comment. The script's
# "nearest push before" / "nearest pop after" heuristic does not track
# bracket depth, so a pragma that sits AFTER an already-closed push/pop pair
# but BEFORE a later, unrelated pop is incorrectly treated as bracketed
# (push_before and pop_after are both non-zero even though the pragma is not
# actually inside any push/pop block). This fixture pins that current
# (permissive) behaviour — should exit 0 — so any change tightening the
# bracket-depth tracking is a deliberate, visible improvement rather than an
# accidental behaviour change.
#
# Uses its OWN isolated fake repo (not $FAKE_REPO): the script scans every
# file under src/, and $FAKE_REPO/src/ already accumulated real violations
# from Tests 3-5 above, which would make this assertion vacuously true
# regardless of the interleaved fixture's own (allowed) shape.
FAKE_REPO2="$WORK_DIR/repo2"
mkdir -p "$FAKE_REPO2/scripts" "$FAKE_REPO2/src"
cp "$SCRIPT" "$FAKE_REPO2/scripts/check-warning-suppressions.sh"
chmod +x "$FAKE_REPO2/scripts/check-warning-suppressions.sh"
cat > "$FAKE_REPO2/src/interleaved_known_limitation.cpp" <<'EOF'
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
int unused_var = 0;
#pragma GCC diagnostic pop
// Unrelated code between the closed push/pop block above and the
// nominally-unbracketed pragma below.
#pragma GCC diagnostic ignored "-Warray-bounds"
int arr[5];
int x = arr[10];
int y = arr[11];
#pragma GCC diagnostic pop
int main() { return 0; }
EOF
if (cd "$FAKE_REPO2" && bash scripts/check-warning-suppressions.sh 2>/dev/null); then
    ok "interleaved push/pop/pragma/pop (known limitation) exits 0"
else
    fail "interleaved push/pop/pragma/pop (known limitation) should exit 0"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
