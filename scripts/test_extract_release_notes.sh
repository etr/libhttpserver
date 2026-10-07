#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
printf 'Version 2.0.1\n\nFix crash.\n\nRetain paragraph.\n\n\nVersion 2.0.0\n\nPrior release.\n\n' > "$fixture/ChangeLog"
printf 'Fix crash.\n\nRetain paragraph.\n' > "$fixture/new"
printf 'Prior release.\n' > "$fixture/old"
for version in '' 2.0.1 v2.0.1; do
 CHANGELOG="$fixture/ChangeLog" scripts/extract-release-notes.sh "$version" > "$fixture/actual"
 cmp "$fixture/new" "$fixture/actual"
done
CHANGELOG="$fixture/ChangeLog" scripts/extract-release-notes.sh 2.0.0 > "$fixture/actual"
cmp "$fixture/old" "$fixture/actual"
printf 'Release extraction fixture PASS\n'
