#!/usr/bin/env bash
# Check the standalone native README's tutorial, structure and local links.
# An optional document path supports mutation fixtures; paths remain repo-relative.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
README="${1:-$REPO_ROOT/README.md}"
fail() { echo "check-readme: FAIL: $*" >&2; exit 1; }
[ "$#" -le 1 ] || fail 'usage: check-readme.sh [README-path]'
[ -f "$README" ] || fail "document does not exist: $README"

# Keep the shared ordered-state fence check and markdownlint policy.
"$REPO_ROOT/scripts/lib/check-fence-balance.sh" "$README" || fail 'unbalanced fences'
[ "$(grep -cE '^#[[:space:]]' "$README" || true)" -eq 1 ] || fail 'expected exactly one H1'
awk '
    /^```/ { inside = !inside; next }
    inside && /\t/ { print NR ": tab inside fenced code"; bad = 1 }
    END { exit bad }
' "$README" >&2 || fail 'fenced code must use spaces'
if LC_ALL=C grep -q $'\r$' "$README"; then fail 'convert CRLF to LF'; fi

legacy='\b(webserver|create_webserver|http_method|http_resource|http_request|http_response'
legacy+='|on_get|on_post|on_put|on_delete|on_patch|on_head|on_options'
legacy+='|feature_unavailable|register_resource|register_path|register_prefix|register_ws_resource'
legacy+='|stop_and_wait|INTERNAL_SELECT|THREAD_PER_CONNECTION|HAVE_GNUTLS|MHD_[A-Z_]+'
legacy+='|with_header|with_status|sweet_kill|ban_ip|unban_ip|disallow_ip'
legacy+='|render_GET|render_get|string_response|file_response|deferred_response)\b'
if hits="$(grep -nE "$legacy" "$README")"; then
    echo "$hits" >&2
    fail 'obsolete instructional API'
fi
if grep -qiE '^#{2,6}.*(legacy.*manual|migrat|native v3 entry point|v1 aliases)' "$README"; then
    fail 'historical or migration scaffolding in native manual'
fi

sections=(
    'Introduction' 'Requirements' 'Building and installation'
    'Getting started: Hello, world' 'Callable handlers and shared state'
    'Structures and classes' 'Create and work with a server' 'Resource ownership'
    'Routing' 'Request' 'Response' 'IP access control' 'Authentication and TLS'
    'WebSocket support' 'Server introspection and external event loops'
    'HTTP utilities' 'Lifecycle hooks' 'Threading contract' 'Error propagation'
    'Feature availability' 'Examples' 'Community' 'Copying' 'Thanks'
)
for section in "${sections[@]}"; do
    grep -qiFx "## $section" "$README" || fail "missing section: $section"
done

first_cpp="$(awk '
    /^```cpp[[:space:]]*$/ && !done { inside = 1; next }
    /^```/ && inside { done = 1; inside = 0 }
    inside { print }
' "$README")"
for token in '#include <httpserver.hpp>' 'int main()' 'server_options' 'native_server' 'route_sync(' 'listen()' '.ok()' 'stop()'; do
    grep -qF "$token" <<< "$first_cpp" || fail "opening hello missing: $token"
done

section_body() {
    awk -v heading="## $1" '
        /^## / { if (inside) exit; inside = ($0 == heading); next }
        inside { print }
    ' "$README"
}
require_in_section() {
    local body
    body="$(section_body "$1")"
    grep -qF "$2" <<< "$body" || fail "$1 must explain $2"
}
require_in_section 'Threading contract' 'request_stop()'
require_in_section 'Threading contract' 'would_deadlock'
require_in_section 'Error propagation' 'handler_exception'
require_in_section 'Error propagation' '500'
require_in_section 'Feature availability' 'query_features()'
for phase in accept_decision request_received route_resolved before_handler handler_exception after_handler response_sent request_completed; do
    require_in_section 'Lifecycle hooks' "$phase"
done
grep -qE '\]\(examples/' "$README" || fail 'missing example source links'
grep -qE '\]\(RELEASE_NOTES\.md' "$README" || fail 'missing release notes link'

# Check README anchors as well as file targets/fragments. GitHub heading slugs
# remove punctuation, preserve hyphens and suffix duplicate headings.
python3 - "$REPO_ROOT" "$README" <<'PY'
import pathlib
import re
import sys

root, document = map(pathlib.Path, sys.argv[1:])

def prose(text):
    lines = []
    fenced = False
    for line in text.splitlines():
        if line.startswith('```'):
            fenced = not fenced
        elif not fenced:
            lines.append(line)
    return '\n'.join(lines)

def anchors(text):
    found, counts = set(), {}
    for heading in re.findall(r'^#{1,6}\s+(.+)$', prose(text), re.M):
        heading = re.sub(r'\[([^]]+)\]\([^)]*\)', r'\1', heading)
        slug = re.sub(r'[^\w\- ]', '', heading.lower()).replace(' ', '-')
        count = counts.get(slug, 0)
        counts[slug] = count + 1
        found.add(slug + (f'-{count}' if count else ''))
    return found

text = document.read_text()
local_anchors = anchors(text)
for target in re.findall(r'\]\(([^)\s]+)\)', prose(text)):
    if re.match(r'^[a-zA-Z][\w+.-]*:', target):
        continue
    path, _, fragment = target.partition('#')
    resolved = (root / path).resolve() if path else document.resolve()
    if path and not resolved.is_relative_to(root.resolve()):
        sys.exit(f'check-readme: FAIL: link escapes repository: {target}')
    if not resolved.exists():
        sys.exit(f'check-readme: FAIL: broken local link: {target}')
    if fragment and (not path or resolved.suffix == '.md'):
        available = local_anchors if not path else anchors(resolved.read_text())
        if fragment not in available:
            sys.exit(f'check-readme: FAIL: broken local anchor: {target}')
PY

# shellcheck source=scripts/lib/check-markdownlint.sh
source "$REPO_ROOT/scripts/lib/check-markdownlint.sh"
run_markdownlint_advisory 'check-readme' "$README"
echo 'check-readme: OK (native hello, guide sections, hooks, links/anchors and Markdown)'
