#!/usr/bin/env bash
#
# check-release-notes.sh — enforce TASK-042 invariants on RELEASE_NOTES.md.
#
# Like scripts/check-readme.sh, this is a static-source check. It asserts that
# RELEASE_NOTES.md is a complete v1→v2.0 porting summary:
#
#   A1. RELEASE_NOTES.md exists at REPO_ROOT.
#   A2. Required v1-era tokens appear at least once (this is the inverse of
#       check-readme.sh A2 — the rename/removal source list MUST be present
#       so v1 users can grep for any old name).
#   A3. Required v2-era tokens appear at least once (reuses the
#       REQUIRED_V2_TOKENS list from check-readme.sh so the two checks agree).
#   A4. Required H2 sections are present: TL;DR, What's gone, What's new,
#       What's renamed, What changed semantically, Build prerequisites,
#       SOVERSION (and packaging).
#   A5. Rename-completeness: each high-value (v1 → v2) pair must appear on
#       the SAME LINE (typically a Markdown table row). This is the
#       load-bearing acceptance check — "a v1 user can grep for any v1
#       method name and find what replaced it."
#   A6. Threading & error sub-sections cite the architecture sources
#       (DR-008 / §5.1 and DR-009 / §5.2), mirroring check-readme.sh's A4.
#   A7. An explicit "not a compatibility commitment" disclaimer is present
#       (case-insensitive).
#
# Plus markdown sanity (S1–S3):
#   S1. Balanced ``` fences (count is even).
#   S2. Exactly one top-level `# ` H1 line.
#   S3. No tab characters inside fenced code blocks.
#
# This script assumes LF-only line endings. Repo's .gitattributes /
# .editorconfig should enforce this; if RELEASE_NOTES.md is ever saved with
# CRLF, the A1/A5 string matches and table-row extractor will silently see
# a trailing \r on every line, which will surface as confusing pair-not-found
# failures rather than a clear "CRLF detected" error. A dedicated CRLF guard
# near A1 catches this early with an explicit diagnostic.
#
# Exits non-zero on the first violation.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOTES="$REPO_ROOT/RELEASE_NOTES.md"

fail() {
    echo "check-release-notes: FAIL: $*" >&2
    exit 1
}

# Helper: check that every token in the given array appears at least once in
# the target file.  Usage: check_tokens_present <label> <file> "${ARRAY[@]}"
# Calls fail() (which calls exit 1) directly — intended for fail-fast use only,
# not for tolerate-and-accumulate loops.
check_tokens_present() {
    local label="$1" file="$2"; shift 2
    local missing=()
    for tok in "$@"; do
        grep -qE "$tok" "$file" || missing+=("$tok")
    done
    if [ "${#missing[@]}" -gt 0 ]; then
        fail "${label}: missing tokens:"$'\n'"$(printf '  %s\n' "${missing[@]}")"
    fi
}

# Helper: extract body of a named section (between its H2 heading and the next H2/EOF).
# $1 = file, $2 = regex matching the heading line.
# The heading_re is matched case-insensitively via tolower(); callers need not
# pre-lowercase their pattern.  The re value is passed via -v and must not
# contain backslashes or double-quotes (all current call sites use string literals).
extract_section_body() {
    awk -v re="$2" '
        BEGIN { in_section = 0 }
        /^##[[:space:]]+/ {
            if (in_section) { exit }
            if (tolower($0) ~ tolower(re)) { in_section = 1; next }
        }
        in_section { print }
    ' "$1"
}

# Helper: check that a named section exists and contains a required citation.
# Usage: check_section_cites <label> <file> <heading_re> <citation_re> <section_name>
check_section_cites() {
    local label="$1" file="$2" heading_re="$3" citation_re="$4" section_name="$5"
    local body
    body="$(extract_section_body "$file" "$heading_re")"
    [ -n "$body" ] || fail "${label}: RELEASE_NOTES.md is missing a '${section_name}' section"
    echo "$body" | grep -qE "$citation_re" \
        || fail "${label}: ${section_name} section must cite ${citation_re}"
}

# ---- A1: RELEASE_NOTES.md exists --------------------------------------------

[ -f "$NOTES" ] || fail "A1: RELEASE_NOTES.md does not exist at $NOTES"

# Guard against CRLF line endings: a trailing \r on every line causes A5 pair
# checks to fail with confusing 'pair-not-found' errors rather than a clear
# diagnostic.  Catch it early.
if grep -qP '\r' "$NOTES" 2>/dev/null; then
    fail "A1: CRLF line endings detected in RELEASE_NOTES.md — convert to LF before committing"
fi

# ---- A2: required v1-era tokens appear (porting source list) ----------------
# The rename/removal source-of-truth. Mirrors V1_TOKENS in check-readme.sh,
# but inverted polarity: here every name MUST appear at least once so v1
# users can grep for it.
# Note: the no_* setter family is spot-checked (no_ssl, no_basic_auth,
# no_digest_auth); the remaining nine no_* names are documented in prose but
# not individually checked here — intentional partial coverage.

REQUIRED_V1_TOKENS=(
    '\bsweet_kill\b'
    '\bban_ip\b'
    '\bunban_ip\b'
    '\ballow_ip\b'
    '\bdisallow_ip\b'
    '\bno_ssl\b'
    '\bno_basic_auth\b'
    '\bno_digest_auth\b'
    '\bnot_found_resource\b'
    '\bmethod_not_allowed_resource\b'
    '\binternal_error_resource\b'
    '\brender_GET\b'
    '\brender_POST\b'
    '\brender_PUT\b'
    '\brender_DELETE\b'
    '\brender_HEAD\b'
    '\brender_OPTIONS\b'
    '\brender_PATCH\b'
    '\brender_CONNECT\b'
    '\brender_TRACE\b'
    '\bstring_response\b'
    '\bfile_response\b'
    '\biovec_response\b'
    '\bpipe_response\b'
    '\bempty_response\b'
    '\bdeferred_response\b'
    '\bbasic_auth_fail_response\b'
    '\bdigest_auth_fail_response\b'
    '\bgnutls_session_t\b'
    '\bget_raw_response\b'
    '\bDEFAULT_WS_PORT\b'
    '\bDEFAULT_WS_TIMEOUT\b'
    '\bdecorate_response\b'
    '\benqueue_response\b'
    '\bHAVE_GNUTLS\b'
    '\bHAVE_BAUTH\b'
    '\bHAVE_DAUTH\b'
    '\bregister_resource\b'
)

check_tokens_present "A2: RELEASE_NOTES.md is missing required v1-era tokens" "$NOTES" "${REQUIRED_V1_TOKENS[@]}"

# ---- A3: required v2-era tokens appear --------------------------------------
# Replacement list. Kept in lock-step with check-readme.sh A3 (shared core).
# Note: check-readme.sh may carry additional tokens not required in RELEASE_NOTES.md
# (e.g. tokens that appear in README.md code examples but not in release notes prose).
# When adding a new v2 surface, update both scripts.

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
    '\bhttpserver::constants\b'
)

check_tokens_present "A3: RELEASE_NOTES.md is missing required v2 tokens" "$NOTES" "${REQUIRED_V2_TOKENS[@]}"

# ---- A3b: TASK-052 — hook-bus closes-list issue numbers ----------------------
# The "What's new" hook-bus bullet must reference the four (+1 partial) issues
# the hook bus is documented to close. Tokens are matched anywhere in the file.

REQUIRED_CLOSES_ISSUES=(
    '#332'
    '#281'
    '#69'
    '#273'
    '#272'
)
check_tokens_present "A3b: RELEASE_NOTES.md is missing hook-bus closes-list issue numbers" "$NOTES" "${REQUIRED_CLOSES_ISSUES[@]}"

# Also gate on the "eleven phases" summary — distinguishes the complete bullet
# from the partial M5-skeleton wording that shipped with TASK-046.
if ! grep -qiE '(eleven|11)[[:space:]]+phases?' "$NOTES"; then
    fail "A3c: 'What's new' hook-bus bullet must mention 'eleven phases' (or '11 phases')"
fi

# ---- A4: required H2 sections present ---------------------------------------

# REQUIRED_SECTIONS must remain a fully-static, hardcoded list.  The values are
# interpolated directly into grep ERE patterns; any ERE metacharacters in a
# section name would silently change match semantics instead of failing loudly.
REQUIRED_SECTIONS=(
    'tl;dr'
    "what's gone"
    "what's new"
    "what's renamed"
    'what changed semantically'
    'build prerequisites'
    'soversion'
    'threading'
    'error propagation'
)

missing_sections=()
for sec in "${REQUIRED_SECTIONS[@]}"; do
    if ! grep -qiE "^##[[:space:]]+.*${sec}" "$NOTES"; then
        missing_sections+=("$sec")
    fi
done
if [ "${#missing_sections[@]}" -gt 0 ]; then
    echo "check-release-notes: FAIL: A4: RELEASE_NOTES.md is missing required ## sections:" >&2
    for sec in "${missing_sections[@]}"; do echo "  pattern: $sec" >&2; done
    exit 1
fi

# ---- A5: rename-completeness — each pair on the same line -------------------
# Each entry is "<v1_regex>;<v2_regex>" (semicolon delimiter — v1/v2 tokens
# never contain semicolons). The check requires both regexes to match the
# same line somewhere in RELEASE_NOTES.md. Markdown table rows satisfy this
# by construction; inline prose like "- sweet_kill → stop_and_wait" also
# works.
#
# Scope note: the no_* setter family is enforced by A2 (presence) and prose
# explanation, NOT by per-pair grep. The setter-name replacement (drop the
# no_ prefix and pass a bool) does not have a single mechanical pair.

# IMPORTANT: v2 values may use ERE alternation groups intentionally (e.g.
# '(block_ip|unblock_ip)').  Any new pair must be valid ERE on both sides.
#
# Word-boundary anchors (\b) on v1 values prevent false matches where the v1
# token appears as a substring of a different, longer token on the same line.
# Example: '\ballow_ip\b' prevents the 'disallow_ip' prose line from
# satisfying the allow_ip pair check (the 'disallow_ip' line already contains
# 'block_ip'/'unblock_ip', which would be a false positive without \b).
# 'disallow_ip' needs no \b because no shorter token contains it as a
# substring — the asymmetry is intentional, not an oversight.
RENAME_PAIRS=(
    'sweet_kill;stop_and_wait'
    '\bban_ip\b;block_ip'
    'unban_ip;unblock_ip'
    '\ballow_ip\b;(block_ip|unblock_ip)'
    'disallow_ip;(block_ip|unblock_ip)'  # no \b needed — no shorter token contains disallow_ip as a substring
    'not_found_resource;not_found_handler'
    'method_not_allowed_resource;method_not_allowed_handler'
    'internal_error_resource;internal_error_handler'
    'render_GET;render_get'
    'render_POST;render_post'
    'render_PUT;render_put'
    'render_DELETE;render_delete'
    'render_HEAD;render_head'
    'render_OPTIONS;render_options'
    'render_PATCH;render_patch'
    'render_CONNECT;render_connect'
    'render_TRACE;render_trace'
    'string_response;http_response::string'
    'file_response;http_response::file'
    'iovec_response;http_response::iovec'
    'pipe_response;http_response::pipe'
    'empty_response;http_response::empty'
    'deferred_response;http_response::deferred'
    'basic_auth_fail_response;http_response::unauthorized'
    'digest_auth_fail_response;http_response::unauthorized'
    'webserver.*create_webserver;explicit'
)

missing_pairs=()
for pair in "${RENAME_PAIRS[@]}"; do
    v1="${pair%%;*}"
    v2="${pair#*;}"
    # Match either order on the same line.
    if ! grep -qE "(${v1}.*${v2}|${v2}.*${v1})" "$NOTES"; then
        missing_pairs+=("$v1 ↔ $v2")
    fi
done
if [ "${#missing_pairs[@]}" -gt 0 ]; then
    echo "check-release-notes: FAIL: A5: RELEASE_NOTES.md is missing same-line rename pairs:" >&2
    for p in "${missing_pairs[@]}"; do echo "  $p" >&2; done
    exit 1
fi

# ---- A6: threading & error sections cite architecture sources ---------------

check_section_cites "A6" "$NOTES" \
    '^##[[:space:]]+.*threading' \
    '(DR-008|§[[:space:]]*5\.1|specs/architecture/05-cross-cutting\.md|specs/architecture/11-decisions/DR-008)' \
    'Threading'

check_section_cites "A6" "$NOTES" \
    '^##[[:space:]]+.*error[[:space:]-]+propag' \
    '(DR-009|§[[:space:]]*5\.2|specs/architecture/05-cross-cutting\.md|specs/architecture/11-decisions/DR-009)' \
    'Error propagation'

# ---- A7: explicit "not a compatibility commitment" disclaimer ---------------

if ! grep -qiE 'not[[:space:]]+a[[:space:]]+compatibility[[:space:]]+commitment' "$NOTES"; then
    fail "A7: RELEASE_NOTES.md must contain an explicit 'not a compatibility commitment' disclaimer"
fi

# ---- Markdown sanity --------------------------------------------------------
# (S1) Balanced ``` fences (count of ``` lines must be even).
fence_count="$(grep -cE '^```' "$NOTES" || true)"
if [ "$((fence_count % 2))" -ne 0 ]; then
    fail "S1: RELEASE_NOTES.md has an odd number of \`\`\` fence lines ($fence_count); fences are unbalanced"
fi

# (S2) Exactly one top-level `# ` H1 line.
h1_count="$(grep -cE '^#[[:space:]]' "$NOTES" || true)"
if [ "$h1_count" -ne 1 ]; then
    fail "S2: RELEASE_NOTES.md must have exactly one H1 (\`# \`) heading, found $h1_count"
fi

# (S3) No literal tab characters inside fenced code blocks.
awk '
    BEGIN { in_block = 0; bad = 0 }
    /^```/ { in_block = !in_block; next }
    in_block && /\t/ { print NR": "$0; bad = 1 }
    END { exit bad }
' "$NOTES" >&2 || fail "S3: fenced code blocks in RELEASE_NOTES.md contain tab characters (must use spaces)"

# ---- Optional: markdownlint advisory ----------------------------------------
if command -v markdownlint >/dev/null 2>&1; then
    if ! markdownlint -q "$NOTES" 2>/dev/null; then
        echo "check-release-notes: NOTE: markdownlint reported issues (advisory only, not gating)" >&2
    fi
fi

echo "check-release-notes: OK (A1 exists; A2 ${#REQUIRED_V1_TOKENS[@]} v1 tokens; A3 ${#REQUIRED_V2_TOKENS[@]} v2 tokens; A4 ${#REQUIRED_SECTIONS[@]} sections; A5 ${#RENAME_PAIRS[@]} rename pairs; A6 threading+error citations; A7 disclaimer; fences balanced)"
exit 0
