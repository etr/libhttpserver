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
# failures rather than a clear "CRLF detected" error.
#
# Exits non-zero on the first violation.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOTES="$REPO_ROOT/RELEASE_NOTES.md"

fail() {
    echo "check-release-notes: FAIL: $*" >&2
    exit 1
}

# ---- A1: RELEASE_NOTES.md exists --------------------------------------------

[ -f "$NOTES" ] || fail "A1: RELEASE_NOTES.md does not exist at $NOTES"

# ---- A2: required v1-era tokens appear (porting source list) ----------------
# The rename/removal source-of-truth. Mirrors V1_TOKENS in check-readme.sh,
# but inverted polarity: here every name MUST appear at least once so v1
# users can grep for it.

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
    '\bHAVE_GNUTLS\b'
)

missing_v1=()
for tok in "${REQUIRED_V1_TOKENS[@]}"; do
    if ! grep -qE "$tok" "$NOTES"; then
        missing_v1+=("$tok")
    fi
done
if [ "${#missing_v1[@]}" -gt 0 ]; then
    echo "check-release-notes: FAIL: A2: RELEASE_NOTES.md is missing required v1-era tokens:" >&2
    for tok in "${missing_v1[@]}"; do echo "  $tok" >&2; done
    exit 1
fi

# ---- A3: required v2-era tokens appear --------------------------------------
# Replacement list. Kept in lock-step with check-readme.sh A3.

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
)

missing_v2=()
for tok in "${REQUIRED_V2_TOKENS[@]}"; do
    if ! grep -qE "$tok" "$NOTES"; then
        missing_v2+=("$tok")
    fi
done
if [ "${#missing_v2[@]}" -gt 0 ]; then
    echo "check-release-notes: FAIL: A3: RELEASE_NOTES.md is missing required v2 tokens:" >&2
    for tok in "${missing_v2[@]}"; do echo "  $tok" >&2; done
    exit 1
fi

# ---- A4: required H2 sections present ---------------------------------------

REQUIRED_SECTIONS=(
    'tl;dr'
    "what's gone"
    "what's new"
    "what's renamed"
    'what changed semantically'
    'build prerequisites'
    'soversion'
)

missing_sections=()
for sec in "${REQUIRED_SECTIONS[@]}"; do
    if ! grep -qiE "^##[ \t]+.*${sec}" "$NOTES"; then
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

RENAME_PAIRS=(
    'sweet_kill;stop_and_wait'
    'ban_ip;block_ip'
    'unban_ip;unblock_ip'
    'allow_ip;(block_ip|unblock_ip)'
    'disallow_ip;(block_ip|unblock_ip)'
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

extract_section_body() {
    # $1 = file, $2 = regex matching the heading line (case-insensitive).
    # Captures lines after the first matching heading up to the next H2 or EOF.
    awk -v re="$2" '
        BEGIN { in_section = 0 }
        /^##[ \t]+/ {
            if (in_section) { exit }
            if (tolower($0) ~ tolower(re)) { in_section = 1; next }
        }
        in_section { print }
    ' "$1"
}

thread_body="$(extract_section_body "$NOTES" '^##[ \t]+.*threading')"
err_body="$(extract_section_body "$NOTES" '^##[ \t]+.*error[ \t-]+propag')"

if [ -z "$thread_body" ]; then
    fail "A6: RELEASE_NOTES.md is missing a '## Threading...' section"
fi
if ! echo "$thread_body" | grep -qE '(DR-008|§[[:space:]]*5\.1|specs/architecture/05-cross-cutting\.md|specs/architecture/11-decisions/DR-008)'; then
    fail "A6: Threading section must cite DR-008 or §5.1"
fi

if [ -z "$err_body" ]; then
    fail "A6: RELEASE_NOTES.md is missing an '## Error propagation' section"
fi
if ! echo "$err_body" | grep -qE '(DR-009|§[[:space:]]*5\.2|specs/architecture/05-cross-cutting\.md|specs/architecture/11-decisions/DR-009)'; then
    fail "A6: Error-propagation section must cite DR-009 or §5.2"
fi

# ---- A7: explicit "not a compatibility commitment" disclaimer ---------------

if ! grep -qiE 'not[[:space:]]+a[[:space:]]+compatibility[[:space:]]+commitment' "$NOTES"; then
    fail "A7: RELEASE_NOTES.md must contain an explicit 'not a compatibility commitment' disclaimer"
fi

# ---- Markdown sanity --------------------------------------------------------
# (S1) Balanced ``` fences (count of ``` lines must be even).
fence_count="$(grep -cE '^```' "$NOTES" || true)"
if [ "$(( ${fence_count:-0} % 2 ))" -ne 0 ]; then
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
    if ! markdownlint -q "$NOTES" 2>&1; then
        echo "check-release-notes: NOTE: markdownlint reported issues (advisory only, not gating)" >&2
    fi
fi

echo "check-release-notes: OK (A1 exists; A2 ${#REQUIRED_V1_TOKENS[@]} v1 tokens; A3 ${#REQUIRED_V2_TOKENS[@]} v2 tokens; A4 ${#REQUIRED_SECTIONS[@]} sections; A5 ${#RENAME_PAIRS[@]} rename pairs; A6 threading+error citations; A7 disclaimer; fences balanced)"
exit 0
