#!/usr/bin/env bash
# Exercise the README gate on a native manual and independently broken documents.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GATE="$ROOT/scripts/check-readme.sh"
FIXTURES="$(mktemp -d)"
trap 'rm -rf "$FIXTURES"' EXIT
cp "$ROOT/README.md" "$FIXTURES/native.md"

# Keep the real tutorial as the fixture: no synthetic duplicate token inventory.
# Each mutation removes one guarantee while retaining the rest of the document.
python3 - "$FIXTURES" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
native = (root / 'native.md').read_text()
mutations = {
    'legacy-api': native + '\nUse `create_webserver(8080)` to start.\n',
    'legacy-shortcut': native + '\nUse `on_get` to register.\n',
    'legacy-alias': native + '\n### v1 aliases\n',
    'legacy-heading': native + '\n## Legacy v2 reference manual\n',
    'missing-section': native.replace('## Request\n', '## Input details\n'),
    'broken-link': native + '\n[Source](examples/does-not-exist.cpp)\n',
    'broken-anchor': native + '\n[More](#does-not-exist)\n',
    'broken-file-anchor': native + '\n[More](docs/v3-api.md#does-not-exist)\n',
    'broken-fence': native + '\n```cpp\nint unfinished;\n',
    'extra-h1': native + '\n# Another manual\n',
    'code-tab': native.replace('int main() {', 'int main() {\n\t// tab'),
    'missing-native-hello': native.replace('server.route_sync(', 'server.other_route(', 1),
}
for name, content in mutations.items():
    (root / (name + '.md')).write_text(content)
PY

if ! bash "$GATE" "$FIXTURES/native.md" > "$FIXTURES/output" 2>&1; then
    cat "$FIXTURES/output" >&2
    echo 'FAIL: native manual must pass without historical hello equality or migration chapters' >&2
    exit 1
fi
echo 'OK: native manual accepted'
cases=(
    'legacy-api|obsolete instructional API'
    'legacy-shortcut|obsolete instructional API'
    'legacy-alias|historical or migration scaffolding'
    'legacy-heading|historical or migration scaffolding'
    'missing-section|missing section: Request'
    'broken-link|broken local link'
    'broken-anchor|broken local anchor'
    'broken-file-anchor|broken local anchor'
    'broken-fence|unbalanced fences'
    'extra-h1|expected exactly one H1'
    'code-tab|fenced code must use spaces'
    'missing-native-hello|opening hello missing: route_sync('
)
for test_case in "${cases[@]}"; do
    name="${test_case%%|*}"
    diagnostic="${test_case#*|}"
    if bash "$GATE" "$FIXTURES/$name.md" > "$FIXTURES/output" 2>&1; then
        echo "FAIL: $name mutation was accepted" >&2
        exit 1
    fi
    if ! grep -qF "$diagnostic" "$FIXTURES/output"; then
        cat "$FIXTURES/output" >&2
        echo "FAIL: $name failed for an unexpected reason" >&2
        exit 1
    fi
    echo "OK: $name rejected"
done
