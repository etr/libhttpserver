#!/usr/bin/env bash
#
# check-codeql-workflow.sh — structural gate for TASK-090 ("Harden CodeQL
# workflow and Codecov upload").
#
# The v2-branch-gap audit flagged that `.github/workflows/codeql-analysis.yml`
# used deprecated, unpinned `@v1`/`@v2` action references and carried dead
# commented-out Autobuild scaffolding — it was never hardened to match the
# SHA-pinning convention `verify-build.yml` uses for every third-party action.
# TASK-090 pins every action to a full 40-hex commit SHA, deletes the Autobuild
# scaffolding, and keeps an explicit `./configure` + `make` build so CodeQL's
# C/C++ extractor traces the real compile commands.
#
# This LOCAL structural gate asserts the workflow stays hardened, guarding
# against silent drift back to floating tags or an autobuild. It mirrors the
# self-testing scripts/check-*.sh gate idiom paired with a
# scripts/test_check_*.sh unit test (see check-valgrind-lane.sh,
# check-parallel-install-lane.sh).
#
# Assertions against `.github/workflows/codeql-analysis.yml`:
#   (a) the file parses as valid YAML (via python3 + PyYAML; NOT actionlint or
#       yamllint, which are not guaranteed present on the runner);
#   (b) NO unpinned action refs remain: no `uses: …@vN` floating-tag pins;
#   (c) EVERY non-comment `uses:` line is pinned to a full 40-hex commit SHA;
#   (d) Autobuild is gone: no `codeql-action/autobuild` reference survives,
#       including in comments (the dead scaffolding must be deleted outright);
#   (e) an explicit build is present: the workflow contains both a `./configure`
#       and a `make` invocation so CodeQL's extractor traces the real compile;
#   (f) the two CodeQL sub-actions (codeql-action/init and codeql-action/analyze)
#       are both present and pinned to the SAME commit SHA — they ship from one
#       repo/release and a split pin is a rotation bug.
#
# Exit codes:
#   0  workflow is hardened correctly
#   1  one or more assertions failed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

WF="${1:-.github/workflows/codeql-analysis.yml}"

echo "check-codeql-workflow: inspecting $WF"

if [ ! -f "$WF" ]; then
    echo "check-codeql-workflow: FAIL — workflow file not found: $WF" >&2
    exit 1
fi

fail=0

# (a) valid YAML. PyYAML parses the file (NOT actionlint/yamllint — not
# guaranteed present on the runner). The import-guard mirrors the other
# check-*.sh gates so a missing PyYAML is a clear, actionable message.
if ! python3 - "$WF" <<'PY'
import sys
try:
    import yaml
except ImportError:
    print("  PyYAML not importable — install with 'pip install pyyaml'", file=sys.stderr)
    sys.exit(10)

path = sys.argv[1]
try:
    yaml.safe_load(open(path))
except Exception as e:  # noqa: BLE001 - surface any parse failure
    print(f"  (a) YAML parse error: {e}", file=sys.stderr)
    sys.exit(2)
print("  (a) YAML parses")
PY
then
    echo "  (a) FAILED" >&2
    fail=1
fi

# (b) No unpinned floating-tag action refs (`uses: …@vN`). A trailing
# `# vX.Y.Z` rotation comment on a SHA-pinned line is fine — the ref itself
# is `@<sha>`, not `@vN` — so the pattern requires `@v<digit>` directly.
if grep -nE 'uses:.*@v[0-9]+' "$WF"; then
    echo "  (b) unpinned floating-tag action ref(s) found — pin to a 40-hex SHA" >&2
    fail=1
else
    echo "  (b) no floating-tag (@vN) action refs remain"
fi

# (c) Every non-comment `uses:` line is pinned to a full 40-hex commit SHA.
uses_lines="$(grep -nE '^[[:space:]]*uses:' "$WF" || true)"
if [ -z "$uses_lines" ]; then
    echo "  (c) no `uses:` action steps found in the workflow" >&2
    fail=1
else
    bad_pins=""
    while IFS= read -r line; do
        [ -z "$line" ] && continue
        if ! printf '%s\n' "$line" | grep -qE '@[0-9a-f]{40}([[:space:]]|$)'; then
            bad_pins="$bad_pins\n    $line"
        fi
    done <<< "$uses_lines"
    if [ -n "$bad_pins" ]; then
        echo "  (c) uses: line(s) not pinned to a 40-hex SHA:" >&2
        printf '%b\n' "$bad_pins" >&2
        fail=1
    else
        echo "  (c) every uses: line pinned to a 40-hex commit SHA"
    fi
fi

# (d) Autobuild must be gone entirely — including commented scaffolding.
if grep -nF 'codeql-action/autobuild' "$WF"; then
    echo "  (d) codeql-action/autobuild reference still present — delete it" >&2
    fail=1
else
    echo "  (d) no codeql-action/autobuild reference remains"
fi

# (e) An explicit build must be present so CodeQL's C/C++ extractor traces
# the real compile commands (autobuild is deliberately not used).
if grep -qF './configure' "$WF" && grep -qE '(^|[[:space:]])make([[:space:]]|;|$)' "$WF"; then
    echo "  (e) explicit ./configure + make build step present"
else
    echo "  (e) explicit ./configure + make build step absent" >&2
    fail=1
fi

# (f) init and analyze must be pinned to the SAME commit SHA.
init_sha="$(grep -oE 'codeql-action/init@[0-9a-f]{40}' "$WF" | head -1 | sed 's/.*@//')"
analyze_sha="$(grep -oE 'codeql-action/analyze@[0-9a-f]{40}' "$WF" | head -1 | sed 's/.*@//')"
if [ -z "$init_sha" ]; then
    echo "  (f) codeql-action/init not present or not SHA-pinned" >&2
    fail=1
elif [ -z "$analyze_sha" ]; then
    echo "  (f) codeql-action/analyze not present or not SHA-pinned" >&2
    fail=1
elif [ "$init_sha" != "$analyze_sha" ]; then
    echo "  (f) codeql-action/init ($init_sha) and analyze ($analyze_sha) pinned to DIFFERENT SHAs" >&2
    fail=1
else
    echo "  (f) codeql-action/init and analyze pinned to the same SHA ($init_sha)"
fi

# (g) A permissions block with security-events: write must be declared.
# Without it the GITHUB_TOKEN defaults to the repo's global permission setting
# (potentially write-all on public repos). The CodeQL analyze step REQUIRES
# security-events: write to upload SARIF results to the security dashboard.
if grep -qE 'security-events:[[:space:]]*write' "$WF"; then
    echo "  (g) permissions block with security-events: write present"
else
    echo "  (g) missing permissions block with security-events: write — add 'permissions: security-events: write' to restrict the GITHUB_TOKEN" >&2
    fail=1
fi

# (h) The libmicrohttpd download must be followed by a sha256sum verification
# step. Without it a tampered S3 object would be compiled and executed inside
# the CodeQL runner with no detection. The checksum used by verify-build.yml
# (libmicrohttpd-1.0.3) is the authoritative source; both workflows must agree.
if grep -qF 'sha256sum -c' "$WF"; then
    echo "  (h) libmicrohttpd download checksum verification (sha256sum -c) present"
else
    echo "  (h) missing sha256sum verification for the libmicrohttpd download — add 'echo \"<sha256>  libmicrohttpd-1.0.3.tar.gz\" | sha256sum -c' after the curl line" >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "check-codeql-workflow: FAIL — the CodeQL workflow is not fully hardened" >&2
    exit 1
fi

echo "check-codeql-workflow: PASS — CodeQL workflow SHA-pinned, autobuild-free, explicit build, least-privilege permissions, checksum-verified download"
