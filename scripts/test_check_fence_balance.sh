#!/usr/bin/env bash
# Unit test for scripts/lib/check-fence-balance.sh
#
# The parity-counting fence check in check-readme.sh / check-release-notes.sh
# had a known gap: two consecutive opening fences plus two closing fences
# produce an even count and slip past a `count % 2` test even though the
# document is structurally unbalanced. This test pins the replacement
# ordered-state-machine helper against synthetic fixtures.
#
# The gate takes the markdown file path as $1 (mirrors the fixture-driven
# style of test_check_codeql_workflow.sh), exiting non-zero on an unbalanced
# document.
set -euo pipefail

PASS=0
FAIL=0

ok() { echo "  OK: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

GATE="$(cd "$(dirname "$0")" && pwd)/lib/check-fence-balance.sh"

TMPDIR_BASE="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_BASE"' EXIT

# Fixture (i): a single balanced ```cpp ... ``` block — must exit 0.
BALANCED="$TMPDIR_BASE/balanced.md"
cat > "$BALANCED" <<'EOF'
# Title

```cpp
int main() { return 0; }
```

Done.
EOF

# Fixture (ii): two consecutive opening fences + two closing fences. The
# total ``` line count is even (4), so the old parity check passed it, but
# it is structurally unbalanced — the second ```cpp opens while already
# inside a block. Must exit non-zero.
TWO_OPENERS="$TMPDIR_BASE/two_openers.md"
cat > "$TWO_OPENERS" <<'EOF'
# Title

```cpp
```cpp
code
```
```
EOF

# Fixture (iii): an odd number of fences — an unclosed block. Must exit
# non-zero.
ODD="$TMPDIR_BASE/odd.md"
cat > "$ODD" <<'EOF'
# Title

```cpp
int main() { return 0; }
EOF

# Fixture (iv): two separate, sequentially-balanced ```cpp ... ``` blocks.
# Pins the in_block 1 -> 0 -> 1 -> 0 reset path — a document with a second
# block must not accidentally latch open (or closed) after the first
# block's close. Must exit 0.
TWO_BLOCKS="$TMPDIR_BASE/two_blocks.md"
cat > "$TWO_BLOCKS" <<'EOF'
# Title

```cpp
int main() { return 0; }
```

Some prose in between.

```cpp
int second() { return 1; }
```

Done.
EOF

# Fixture (v): a bare ``` opener and a bare ``` closer (no info string).
# Pins the in_block == 0 branch for a bare opener, distinct from the
# info-string ```cpp opener used by every other fixture. Must exit 0.
BARE="$TMPDIR_BASE/bare.md"
cat > "$BARE" <<'EOF'
# Title

```
plain code, no language tag
```

Done.
EOF

# Test 1: balanced document exits 0.
if bash "$GATE" "$BALANCED" >/dev/null 2>&1; then
    ok "balanced fenced block exits 0"
else
    fail "balanced fenced block should exit 0"
fi

# Test 2: two consecutive openers (even count) exits non-zero — the
# regression the parity check missed.
if ! bash "$GATE" "$TWO_OPENERS" >/dev/null 2>&1; then
    ok "two consecutive openers (even count) exits non-zero"
else
    fail "two consecutive openers should exit non-zero (parity-check regression)"
fi

# Test 3: odd fence count (unclosed block) exits non-zero.
if ! bash "$GATE" "$ODD" >/dev/null 2>&1; then
    ok "odd fence count (unclosed block) exits non-zero"
else
    fail "odd fence count should exit non-zero"
fi

# Test 4: two sequentially-balanced blocks exit 0 (in_block reset path).
if bash "$GATE" "$TWO_BLOCKS" >/dev/null 2>&1; then
    ok "two sequential balanced blocks exit 0"
else
    fail "two sequential balanced blocks should exit 0"
fi

# Test 5: a bare ``` opener/closer pair (no info string) exits 0.
if bash "$GATE" "$BARE" >/dev/null 2>&1; then
    ok "bare fence opener/closer exits 0"
else
    fail "bare fence opener/closer should exit 0"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
