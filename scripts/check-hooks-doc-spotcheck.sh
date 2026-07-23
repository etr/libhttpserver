#!/usr/bin/env bash
#
# check-hooks-doc-spotcheck.sh — TASK-052 documentation regression gate.
#
# Asserts that the hook bus public surface carries the documentation the
# v2.0 PRD / DR-012 / §4.10 / §5.6 promised:
#
#   H1. The four public hook headers (hook_phase, hook_action, hook_handle,
#       hook_context) each carry a `/** @file ... */` block.
#   H2. Each header's principal public symbol carries an `@brief` block.
#   H3. webserver::add_hook (in webserver_hooks.hpp) enumerates the eleven
#       phases AND has dropped the stale TASK-045 "skeleton-only" caveat.
#   H4. http_resource::add_hook (in http_resource.hpp) enumerates the five
#       permitted phases AND names the six rejected ones.
#   H5. Each of the five v1 alias setters (log_access, not_found_handler,
#       method_not_allowed_handler, internal_error_handler, auth_handler)
#       carries the "this is an alias for add_hook" callout in its Doxygen
#       block.
#   H6. The webserver class-level Threading-contract block mentions hook
#       concurrency.
#
# This is a static grep-based gate -- it does NOT shell out to doxygen.
# `make doxygen-run` is the heavier independent gate.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO_ROOT/src/httpserver"

# Header doc-comment invariants are platform-independent and fully enforced on
# the Linux and macOS lanes. This gate relies on GNU/POSIX grep behavior that is
# unreliable under the MSYS2/mingw shell (git autocrlf CRLF + word-boundary
# matching), so skip on Windows rather than re-validate identical repo content.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        echo "check-hooks-doc-spotcheck: SKIP on Windows/MSYS (content gate enforced on POSIX lanes)"
        exit 0
        ;;
esac

fail() {
    echo "check-hooks-doc-spotcheck: FAIL: $*" >&2
    exit 1
}

# ---- H1: file-level @file blocks --------------------------------------------

for f in hook_phase.hpp hook_action.hpp hook_handle.hpp hook_context.hpp; do
    full="$SRC/$f"
    [ -f "$full" ] || fail "H1: $full does not exist"
    if ! grep -qE '^[[:space:]]*\* @file '"$f"'\b' "$full"; then
        fail "H1: $f is missing a '@file $f' Doxygen tag"
    fi
done

# ---- H2: principal public symbols have @brief blocks ------------------------

require_brief_before() {
    # $1 = file, $2 = regex matching the symbol's declaration line
    local file="$1" sym_re="$2"
    # Find the line number of the symbol declaration, then scan upward
    # for the nearest /** block and ensure it carries @brief.
    local sym_line
    sym_line="$(grep -nE "$sym_re" "$file" | head -1 | cut -d: -f1)" || true
    if [ -z "$sym_line" ]; then
        fail "H2: cannot find symbol matching '$sym_re' in $file"
    fi
    # Walk back up to 60 lines and find the most recent '/**'.
    local start_line
    start_line="$(awk -v end="$sym_line" 'NR<end && /\/\*\*/ { last=NR } END { print last+0 }' "$file")"
    if [ "$start_line" -eq 0 ] || [ "$((sym_line - start_line))" -gt 60 ]; then
        fail "H2: no /** block within 60 lines preceding '$sym_re' in $(basename "$file")"
    fi
    # The block ends at the next '*/'.
    local end_line
    end_line="$(awk -v start="$start_line" -v end="$sym_line" 'NR>=start && NR<=end && /\*\// { print NR; exit }' "$file")"
    if [ -z "$end_line" ]; then
        fail "H2: unterminated /** block preceding '$sym_re' in $(basename "$file")"
    fi
    if ! awk -v s="$start_line" -v e="$end_line" 'NR>=s && NR<=e' "$file" | grep -qE '@brief\b'; then
        fail "H2: /** block preceding '$sym_re' in $(basename "$file") has no @brief"
    fi
}

require_brief_before "$SRC/hook_phase.hpp"   '^enum class hook_phase '
require_brief_before "$SRC/hook_phase.hpp"   '^constexpr std::string_view to_string\(hook_phase'
require_brief_before "$SRC/hook_action.hpp"  '^class hook_action '
require_brief_before "$SRC/hook_handle.hpp"  '^class hook_handle '
require_brief_before "$SRC/hook_context.hpp" '^struct peer_address '
require_brief_before "$SRC/hook_context.hpp" '^struct before_handler_ctx '
require_brief_before "$SRC/hook_context.hpp" '^struct response_sent_ctx '
require_brief_before "$SRC/hook_context.hpp" '^struct request_completed_ctx '

# ---- H3: webserver_hooks.hpp -- eleven phases, no stale skeleton caveat ----

WS_HOOKS="$SRC/webserver_hooks.hpp"
[ -f "$WS_HOOKS" ] || fail "H3: $WS_HOOKS does not exist"
if ! grep -qiE '(eleven|11)[[:space:]]+overloads' "$WS_HOOKS" && \
   ! grep -qiE '(eleven|11)[[:space:]]+phases' "$WS_HOOKS"; then
    fail "H3: webserver_hooks.hpp Doxygen block must mention 'eleven' (or '11') phases / overloads"
fi
for phase in connection_opened accept_decision connection_closed request_received \
             body_chunk route_resolved before_handler handler_exception \
             after_handler response_sent request_completed; do
    if ! grep -qE "\b${phase}\b" "$WS_HOOKS"; then
        fail "H3: webserver_hooks.hpp Doxygen block does not name phase '$phase'"
    fi
done
if grep -qiE 'skeleton-only at TASK-045' "$WS_HOOKS"; then
    fail "H3: webserver_hooks.hpp still contains the stale 'Skeleton-only at TASK-045' note (the feature is fully wired post-TASK-051)"
fi

# ---- H4: http_resource::add_hook lists five permitted + six rejected -------

RES_HPP="$SRC/http_resource.hpp"
[ -f "$RES_HPP" ] || fail "H4: $RES_HPP does not exist"
res_block="$(awk '
    BEGIN { in_block = 0; printed = 0; saved = "" }
    /\/\*\*/ { in_block = 1; saved = $0; next }
    in_block { saved = saved "\n" $0 }
    in_block && /\*\// { in_block = 2; next }
    in_block == 2 && /hook_handle add_hook\(hook_phase phase,/ {
        print saved; printed = 1; exit
    }
    in_block == 2 { in_block = 0; saved = "" }
' "$RES_HPP")"
if [ -z "$res_block" ]; then
    fail "H4: http_resource.hpp has no /** block immediately preceding add_hook(hook_phase, ...)"
fi
if ! echo "$res_block" | grep -qiE '(five|5)[[:space:]]+permitted[[:space:]]+phases|five[[:space:]]+post-route-resolution'; then
    fail "H4: http_resource.hpp add_hook block must mention 'five' permitted phases"
fi
for phase in before_handler handler_exception after_handler response_sent request_completed; do
    if ! echo "$res_block" | grep -qE "\b${phase}\b"; then
        fail "H4: http_resource.hpp add_hook block does not name permitted phase '$phase'"
    fi
done
for phase in connection_opened accept_decision connection_closed request_received body_chunk route_resolved; do
    if ! echo "$res_block" | grep -qE "\b${phase}\b"; then
        fail "H4: http_resource.hpp add_hook block does not name rejected phase '$phase'"
    fi
done
if ! echo "$res_block" | grep -qE 'invalid_argument'; then
    fail "H4: http_resource.hpp add_hook block must document the std::invalid_argument throw"
fi

# ---- H5: alias-setter Doxygen mentions add_hook + hook_phase + alias -------

CW_HPP="$SRC/create_webserver.hpp"
# TASK-086: the handler/callback alias setters (not_found_handler,
# method_not_allowed_handler, internal_error_handler, auth_handler) were
# split out of create_webserver.hpp into the create_webserver_setters.hpp
# class-body fragment to keep both headers under the per-file LOC ceiling.
# log_access remains in create_webserver.hpp. Locate each setter in
# whichever of the two files declares it.
CW_SETTERS="$SRC/create_webserver_setters.hpp"
[ -f "$CW_HPP" ] || fail "H5: $CW_HPP does not exist"
[ -f "$CW_SETTERS" ] || fail "H5: $CW_SETTERS does not exist"
for setter in log_access not_found_handler method_not_allowed_handler \
              internal_error_handler auth_handler; do
    cw_file="$CW_HPP"
    sym_line="$(grep -nE "^[[:space:]]*create_webserver& ${setter}\(" "$cw_file" \
                | head -1 | cut -d: -f1)" || true
    if [ -z "$sym_line" ]; then
        cw_file="$CW_SETTERS"
        sym_line="$(grep -nE "^[[:space:]]*create_webserver& ${setter}\(" "$cw_file" \
                    | head -1 | cut -d: -f1)" || true
    fi
    if [ -z "$sym_line" ]; then
        fail "H5: cannot find setter '$setter' in create_webserver.hpp or create_webserver_setters.hpp"
    fi
    start_line="$(awk -v end="$sym_line" 'NR<end && /\/\*\*/ { last=NR } END { print last+0 }' "$cw_file")"
    if [ "$start_line" -eq 0 ]; then
        fail "H5: no /** block precedes setter '$setter'"
    fi
    end_line="$(awk -v s="$start_line" -v e="$sym_line" 'NR>=s && NR<=e && /\*\// { print NR; exit }' "$cw_file")"
    block="$(awk -v s="$start_line" -v e="$end_line" 'NR>=s && NR<=e' "$cw_file")"
    if ! echo "$block" | grep -qiE '\balias\b|\baliases\b'; then
        fail "H5: setter '$setter' Doxygen block does not call out 'alias'"
    fi
    if ! echo "$block" | grep -qE '\badd_hook\b'; then
        fail "H5: setter '$setter' Doxygen block does not mention add_hook"
    fi
    if ! echo "$block" | grep -qE '\bhook_phase::'; then
        fail "H5: setter '$setter' Doxygen block does not mention a hook_phase:: value"
    fi
done

# ---- H6: webserver class-level threading block mentions hook concurrency ---

WS_HPP="$SRC/webserver.hpp"
[ -f "$WS_HPP" ] || fail "H6: $WS_HPP does not exist"
# Extract from the '### Threading contract' marker to the next '###' / '*/'.
thread_block="$(awk '
    /\*[[:space:]]+###[[:space:]]+Threading contract/ { in_block = 1; next }
    in_block && /\*[[:space:]]+###[[:space:]]+/ { exit }
    in_block && /\*\// { exit }
    in_block { print }
' "$WS_HPP")"
if [ -z "$thread_block" ]; then
    fail "H6: webserver.hpp class-level Doxygen has no '### Threading contract' block"
fi
if ! echo "$thread_block" | grep -qE 'add_hook|hook[[:space:]]*\('; then
    fail "H6: Threading-contract block does not mention hooks / add_hook"
fi
if ! echo "$thread_block" | grep -qiE 'may[[:space:]]+run[[:space:]]+concurrently'; then
    fail "H6: Threading-contract block must include the 'may run concurrently' wording for hooks"
fi
if ! echo "$thread_block" | grep -qiE 'thread-safe'; then
    fail "H6: Threading-contract block must mention thread-safety"
fi

echo "check-hooks-doc-spotcheck: OK (H1 @file blocks; H2 @brief on principal symbols; H3 eleven phases in webserver_hooks.hpp; H4 five-permitted / six-rejected on http_resource::add_hook; H5 alias callouts on five v1 setters; H6 hook concurrency in webserver threading block)"
exit 0
