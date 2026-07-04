#!/usr/bin/env bash
#
# check-readme.sh — enforce TASK-041 invariants on README.md.
#
# This is a static-source check, mirroring scripts/check-examples.sh. It asserts
# that the v2.0 README:
#
#   A1. Quotes examples/hello_world.cpp byte-for-byte in its first ```cpp fence.
#   A2. Contains no v1-era tokens (sweet_kill, *_response subclasses, no_* setters,
#       render_GET-style virtuals, ban_ip/allow_ip family, raw-pointer registration).
#   A3. Mentions every load-bearing v2 surface (on_get, register_path, http_response::*,
#       block_ip/unblock_ip, stop_and_wait, features(), feature_unavailable, ...).
#   A4. The Threading and Error-propagation sections cite their architecture sources
#       (DR-008/§5.1 and DR-009/§5.2) and mention the load-bearing details
#       (stop() deadlock per DR-008; internal_error_handler + feature_unavailable).
#   A5. The eleven structural sections from TASK-041 exist (case-insensitive H2 match).
#   A6. Cross-links to examples/ and RELEASE_NOTES.md exist.
#   A6b. Every relative Markdown link resolves to a file on disk.
#
# Plus markdown sanity:
#   - balanced ``` fences;
#   - exactly one top-level `# ` H1;
#   - no tab characters inside fenced code blocks.
#
# Exits non-zero on the first violation.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
README="$REPO_ROOT/README.md"
HELLO="$REPO_ROOT/examples/hello_world.cpp"

fail() {
    echo "check-readme: FAIL: $*" >&2
    exit 1
}

[ -f "$README" ] || fail "README.md does not exist at $README"
[ -f "$HELLO" ]  || fail "examples/hello_world.cpp does not exist at $HELLO"

# ---- A1: hello-world snippet matches examples/hello_world.cpp ----------------
# Extract the first fenced ```cpp ... ``` block from README.md.

extract_first_cpp_block() {
    awk '
        BEGIN { in_block = 0; printed = 0 }
        {
            gsub(/\r/, "")   # strip CRLF line endings (mirrors check-examples.sh)
            if (printed) next
            if (!in_block && $0 ~ /^```cpp[[:space:]]*$/) { in_block = 1; next }
            if (in_block && $0 ~ /^```[[:space:]]*$/) { in_block = 0; printed = 1; next }
            if (in_block) print
        }
    ' "$1"
}

tmp_block="$(mktemp)"
tmp_hello="$(mktemp)"
trap 'rm -f "$tmp_block" "$tmp_hello"' EXIT
extract_first_cpp_block "$README" | tr -d '\r' > "$tmp_block"
# Strip CRLFs from the cpp file too: msys2/MINGW Windows runners check out
# README.md with CRLF (git autocrlf=true) and the .cpp with LF, which makes
# the byte-for-byte diff fail even though the visible content is identical.
tr -d '\r' < "$HELLO" > "$tmp_hello"

if [ ! -s "$tmp_block" ]; then
    fail "A1: README.md contains no \`\`\`cpp fenced block (need first block to match $HELLO byte-for-byte)"
fi

if ! diff -u "$tmp_hello" "$tmp_block" >/dev/null 2>&1; then
    diff -u "$tmp_hello" "$tmp_block" >&2 || true
    fail "A1: first \`\`\`cpp block in README.md does not match examples/hello_world.cpp byte-for-byte"
fi

# ---- A2: no v1-era tokens ----------------------------------------------------
# Use a single grep -nE pass over README.md. Any hit fails.

# The full v1 no_* setter family is enumerated from the shared data file
# scripts/lib/v1-no-setters.txt (same source of truth as check-release-notes.sh)
# so no removed setter can silently leak back into the v2 README.
NO_SETTERS_FILE="$REPO_ROOT/scripts/lib/v1-no-setters.txt"
[ -f "$NO_SETTERS_FILE" ] || fail "A2: enumeration file missing: $NO_SETTERS_FILE"
V1_NO_SETTERS=()
while IFS= read -r _name || [ -n "$_name" ]; do
    case "$_name" in
        ''|\#*) continue ;;   # skip blank lines and # comments
    esac
    V1_NO_SETTERS+=("\\b${_name}\\b")
done < "$NO_SETTERS_FILE"
[ "${#V1_NO_SETTERS[@]}" -gt 0 ] || fail "A2: $NO_SETTERS_FILE enumerated no setter names"

V1_TOKENS_ARR=(
    '\bsweet_kill\b'
    '\bban_ip\b'
    '\bunban_ip\b'
    '\ballow_ip\b'
    '\bdisallow_ip\b'
    "${V1_NO_SETTERS[@]}"
    '\brender_(GET|POST|PUT|DELETE|HEAD|OPTIONS|PATCH|CONNECT|TRACE)\b'
    '\bstring_response\b'
    '\bfile_response\b'
    '\biovec_response\b'
    '\bpipe_response\b'
    '\bdeferred_response\b'
    '\bempty_response\b'
    '\bbasic_auth_fail_response\b'
    '\bdigest_auth_fail_response\b'
    'new[[:space:]]+[A-Za-z_]*_response[[:space:]]*\('
    'register_resource[[:space:]]*\([^,]*,[[:space:]]*new[[:space:]]+'
    '\bnot_found_resource\b'
    '\bmethod_not_allowed_resource\b'
    '\binternal_error_resource\b'
)
# Join array entries with | for a single grep pass.
V1_TOKENS="${V1_TOKENS_ARR[0]}"
for _tok in "${V1_TOKENS_ARR[@]:1}"; do V1_TOKENS="$V1_TOKENS|$_tok"; done

if hits="$(grep -nE "$V1_TOKENS" "$README")"; then
    echo "check-readme: FAIL: A2: README.md contains v1-era tokens:" >&2
    echo "$hits" >&2
    exit 1
fi

# ---- A3: required v2 tokens appear at least once -----------------------------
# Each pattern must match. Collect misses and report them all in one go.

REQUIRED_V2_TOKENS=(
    '\bon_get\b'
    '\bon_post\b'
    '\broute[[:space:]]*\('
    '\bregister_path\b'
    '\bregister_prefix\b'
    '\bregister_ws_resource\b'
    '\bhttp_response::string\b'
    '\bhttp_response::file\b'
    '\bhttp_response::iovec\b'
    '\bhttp_response::pipe\b'
    '\bhttp_response::empty\b'
    '\bhttp_response::deferred\b'
    '\bhttp_response::unauthorized\b'
    '\bwith_header\b'
    '\bwith_status\b'
    '\bblock_ip\b'
    '\bunblock_ip\b'
    '\bstop_and_wait\b'
    '\bfeatures\(\)'
    '\bfeature_unavailable\b'
    '\binternal_error_handler\b'
    '\bnot_found_handler\b'
    '\bmethod_not_allowed_handler\b'
    '\bhttp_method\b'
    '\bmethod_set\b'
    '\biovec_entry\b'
    '\badd_hook\b'
    '\bhook_phase\b'
    '\bhook_action\b'
    '\bhook_handle\b'
)

missing=()
for tok in "${REQUIRED_V2_TOKENS[@]}"; do
    if ! grep -qE "$tok" "$README"; then
        missing+=("$tok")
    fi
done
if [ "${#missing[@]}" -gt 0 ]; then
    echo "check-readme: FAIL: A3: README.md is missing required v2 tokens:" >&2
    for tok in "${missing[@]}"; do echo "  $tok" >&2; done
    exit 1
fi

# ---- A4: threading & error sections cite architecture + key terms -----------
# Find each section body (between its H2 heading and the next H2/EOF) and grep.

extract_section_body() {
    # $1 = file, $2 = regex matching the H2 heading line
    awk -v re="$2" '
        BEGIN { in_section = 0 }
        /^##[ \t]+/ {
            if (in_section) { exit }
            if (tolower($0) ~ tolower(re)) { in_section = 1; next }
        }
        in_section { print }
    ' "$1"
}

thread_body="$(extract_section_body "$README" '^##[ \t]+.*threading')"
err_body="$(extract_section_body "$README" '^##[ \t]+.*error[ \t-]+propag')"

if [ -z "$thread_body" ]; then
    fail "A4: README.md is missing a '## Threading...' section"
fi
if ! echo "$thread_body" | grep -qE '(DR-008|§[[:space:]]*5\.1|specs/architecture/05-cross-cutting\.md|specs/architecture/11-decisions/DR-008)'; then
    fail "A4: Threading section must cite DR-008 or §5.1"
fi
if ! echo "$thread_body" | grep -qE '\bstop\(\)'; then
    fail "A4: Threading section must mention stop() (the stop-from-handler deadlock, per DR-008)"
fi
if ! echo "$thread_body" | grep -qiE 'deadlock'; then
    fail "A4: Threading section must mention the deadlock contract"
fi

if [ -z "$err_body" ]; then
    fail "A4: README.md is missing a '## Error propagation' section"
fi
if ! echo "$err_body" | grep -qE '(DR-009|§[[:space:]]*5\.2|specs/architecture/05-cross-cutting\.md|specs/architecture/11-decisions/DR-009)'; then
    fail "A4: Error-propagation section must cite DR-009 or §5.2"
fi
if ! echo "$err_body" | grep -qE '\binternal_error_handler\b'; then
    fail "A4: Error-propagation section must mention internal_error_handler"
fi
if ! echo "$err_body" | grep -qE '\bfeature_unavailable\b'; then
    fail "A4: Error-propagation section must mention feature_unavailable"
fi

# ---- A5: required structural sections exist ---------------------------------

REQUIRED_SECTIONS=(
    'build.*install'
    'hello.*world'
    'class-form'
    'request'
    'response'
    'routing'
    'lifecycle[ \t-]+hook'
    'threading'
    'error[ \t-]+propag'
    'feature[ \t-]+avail'
    'websocket'
    'migrat'
)

missing_sections=()
for sec in "${REQUIRED_SECTIONS[@]}"; do
    if ! grep -qiE "^##[ \t]+.*${sec}" "$README"; then
        missing_sections+=("$sec")
    fi
done
if [ "${#missing_sections[@]}" -gt 0 ]; then
    echo "check-readme: FAIL: A5: README.md is missing required ## sections:" >&2
    for sec in "${missing_sections[@]}"; do echo "  pattern: $sec" >&2; done
    exit 1
fi

# ---- A4b: Lifecycle hooks section content (TASK-052) -----------------------
# The new "## Lifecycle hooks" section must mention the five v1 aliases
# alongside the word "alias", and reference all four hook examples.

hooks_body="$(extract_section_body "$README" '^##[ \t]+.*lifecycle[ \t-]+hook')"
if [ -z "$hooks_body" ]; then
    fail "A4b: README.md is missing a '## Lifecycle hooks' section"
fi
if ! echo "$hooks_body" | grep -qiE '\balias\b'; then
    fail "A4b: Lifecycle hooks section must call out v1 setters as aliases"
fi
for alias_name in log_access not_found_handler method_not_allowed_handler internal_error_handler auth_handler; do
    if ! echo "$hooks_body" | grep -qE "\\b${alias_name}\\b"; then
        fail "A4b: Lifecycle hooks section must mention v1 alias '${alias_name}'"
    fi
done
for example_base in banned_ip_log early_413 clf_access_log per_route_auth; do
    if ! echo "$hooks_body" | grep -qE "${example_base}\\.cpp"; then
        fail "A4b: Lifecycle hooks section must reference example ${example_base}.cpp"
    fi
done

# ---- A6: cross-links to examples/ and RELEASE_NOTES.md ----------------------

if ! grep -qE '\]\(examples/' "$README"; then
    fail "A6: README.md must contain at least one Markdown link to examples/ (e.g., [examples/](examples/) or [foo](examples/foo.cpp))"
fi
if ! grep -qE '\]\(RELEASE_NOTES\.md' "$README"; then
    fail "A6: README.md must contain at least one Markdown link to RELEASE_NOTES.md"
fi

# ---- A6b: relative Markdown links resolve to existing files -----------------
# Extract Markdown link targets of the form ](<target>) where the target
# looks like a file path (no spaces, no C++ keywords). Skip http/https URLs
# and in-page anchors (#...). Verify each relative target exists on disk.
broken_links=()
# Note: grep exit-1 (no matches) is not propagated from process substitution under bash set -e;
# this is an intentional reliance on this bash-specific behavior (the script requires bash already).
while IFS= read -r target; do
    case "$target" in
        http://*|https://*) continue ;;         # absolute URLs — not checked
        \#*)                continue ;;         # in-page anchors — not checked
    esac
    # Strip fragment identifiers (#section) before existence check to avoid
    # false failures when a link like [foo](examples/foo.cpp#line) is added.
    target="${target%%#*}"
    if [ ! -e "$REPO_ROOT/$target" ]; then
        broken_links+=("$target")
    fi
done < <(grep -oE '\]\([^) ]+\)' "$README" | sed 's/^](//;s/)$//')

if [ "${#broken_links[@]}" -gt 0 ]; then
    echo "check-readme: FAIL: A6b: README.md contains relative links to non-existent files:" >&2
    for lnk in "${broken_links[@]}"; do echo "  $lnk" >&2; done
    exit 1
fi

# ---- Markdown sanity --------------------------------------------------------
# (S1) Balanced ``` fences, verified by an ordered open/close state machine
# (scripts/lib/check-fence-balance.sh). This replaces the earlier `count % 2`
# parity heuristic, which silently accepted two consecutive opening fences
# plus two closing fences (four lines, even) as "balanced".
if ! "$(dirname "$0")/lib/check-fence-balance.sh" "$README"; then
    fail "S1: README.md has unbalanced \`\`\` fences (see diagnostic above)"
fi

# (S2) Exactly one top-level `# ` H1 line.
h1_count="$(grep -cE '^#[[:space:]]' "$README" || true)"
if [ "$h1_count" -ne 1 ]; then
    fail "S2: README.md must have exactly one H1 (\`# \`) heading, found $h1_count"
fi

# (S3) No literal tab characters inside fenced code blocks.
awk '
    BEGIN { in_block = 0; bad = 0 }
    /^```/ { in_block = !in_block; next }
    in_block && /\t/ { print NR": "$0; bad = 1 }
    END { exit bad }
' "$README" >&2 || fail "S3: fenced code blocks in README.md contain tab characters (must use spaces)"

# (S4) No CRLF line endings in README.md.
# The grep -P below requires a POSIX-extended grep that understands \r; on macOS
# use LC_ALL=C grep -P to avoid locale issues.
if LC_ALL=C grep -qP '\r$' "$README" 2>/dev/null; then
    fail "S4: README.md contains CRLF line endings; convert to LF before committing"
fi

# ---- markdownlint: strict by default ----------------------------------------
# A markdownlint finding fails the script by default (findings go to stderr so
# they are visible in CI logs). Set LIBHTTPSERVER_MARKDOWNLINT_ADVISORY=1 to
# downgrade findings to advisory (non-gating) — the documented escape hatch for
# a lane whose markdownlint version flags rules the docs do not yet satisfy.
# The legacy MARKDOWNLINT_STRICT knob is still honored (default now 'yes');
# MARKDOWNLINT_STRICT=no also downgrades to advisory.
if command -v markdownlint >/dev/null 2>&1; then
    if ! markdownlint -q "$README" 2>&1; then
        if [ "${LIBHTTPSERVER_MARKDOWNLINT_ADVISORY:-0}" = "1" ] \
           || [ "${MARKDOWNLINT_STRICT:-yes}" = "no" ]; then
            echo "check-readme: NOTE: markdownlint reported issues above (advisory only, not gating)" >&2
        else
            fail "markdownlint reported issues (set LIBHTTPSERVER_MARKDOWNLINT_ADVISORY=1 to downgrade to advisory)"
        fi
    fi
fi

echo "check-readme: OK (A1 byte-for-byte snippet; A2 no v1 tokens; A3 ${#REQUIRED_V2_TOKENS[@]} v2 tokens; A4 threading+error citations; A5 ${#REQUIRED_SECTIONS[@]} sections; A6 cross-links; fences balanced)"
exit 0
