#!/usr/bin/env bash
#
# check-msan-lane.sh — structural gate for TASK-087 ("Restore msan CI lane").
#
# The v2-branch-gap audit flagged that the MemorySanitizer (msan) CI lane
# went silently absent: its matrix entry in
# `.github/workflows/verify-build.yml` was commented out when Ubuntu 18.04 /
# clang-6.0 were retired, and asan/lsan/tsan/ubsan carried on without it.
# TASK-087 restores a current, instrumented-libc++ msan lane.
#
# A full GitHub Actions msan build cannot run on a developer's machine (it
# needs an instrumented libc++ and libmicrohttpd built in-CI), so acceptance
# is split: this LOCAL structural gate asserts the workflow is wired
# correctly, and the REAL green run happens on the PR. This mirrors the
# repo idiom of a self-testing `scripts/check-*.sh` gate paired with a
# `scripts/test_check_*.sh` unit test (see check-warning-suppressions.sh).
#
# Assertions against `.github/workflows/verify-build.yml`:
#   (a) the file parses as valid YAML (via python3 + PyYAML; NOT actionlint
#       or yamllint, which are not guaranteed present on the runner);
#   (b) a matrix `include:` entry with `build-type: msan` exists and carries
#       the keys mirrored from the asan entry (test-group/os/os-type/
#       compiler-family/c-compiler/cc-compiler/debug/coverage/shell);
#   (c) the stale `ubuntu-18.04` / `clang-6.0` commented msan block is GONE;
#   (d) the "Run tests" step wires `MSAN_OPTIONS` and a scoped `TESTS=`
#       invocation (msan coverage is deliberately scoped to backend-free
#       unit tests because curl is uninstrumented).
#
# Exit codes:
#   0  workflow is wired correctly
#   1  one or more assertions failed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

WF="${1:-.github/workflows/verify-build.yml}"

echo "check-msan-lane: inspecting $WF"

if [ ! -f "$WF" ]; then
    echo "check-msan-lane: FAIL — workflow file not found: $WF" >&2
    exit 1
fi

fail=0

# (a) valid YAML  +  (b) msan include entry with mirrored keys.
# Both are structural, so PyYAML parses the file and inspects the matrix.
if ! python3 - "$WF" <<'PY'
import sys
try:
    import yaml
except ImportError:
    print("  PyYAML not importable — install with 'pip install pyyaml'", file=sys.stderr)
    sys.exit(10)

path = sys.argv[1]
try:
    doc = yaml.safe_load(open(path))
except Exception as e:  # noqa: BLE001 - surface any parse failure
    print(f"  (a) YAML parse error: {e}", file=sys.stderr)
    sys.exit(2)

# Locate the matrix include list in whichever job defines it (job name is
# not hard-coded so the gate survives a rename).
include = None
for job_name, job in (doc.get("jobs") or {}).items():
    if not isinstance(job, dict):
        continue
    inc = (((job.get("strategy") or {}).get("matrix") or {}).get("include"))
    if isinstance(inc, list):
        include = inc
        break
if include is None:
    print("  (b) could not locate strategy.matrix.include in any job", file=sys.stderr)
    sys.exit(3)

msan = [e for e in include if isinstance(e, dict) and e.get("build-type") == "msan"]
if not msan:
    print("  (b) no matrix include entry with build-type: msan", file=sys.stderr)
    sys.exit(4)
if len(msan) > 1:
    print(f"  (b) expected exactly one msan include entry, found {len(msan)}", file=sys.stderr)
    sys.exit(5)
entry = msan[0]

# Keys mirrored from the asan entry: fixed-value keys must match, and the
# per-lane keys must simply be present (value-agnostic so a future os/compiler
# bump does not break the gate).
fixed = {
    "test-group": "extra",
    "compiler-family": "clang",
    "debug": "debug",
    "coverage": "nocoverage",
    "shell": "bash",
}
present = ("os", "os-type", "c-compiler", "cc-compiler")
problems = []
for k, v in fixed.items():
    if entry.get(k) != v:
        problems.append(f"{k}={entry.get(k)!r} (want {v!r})")
for k in present:
    if not entry.get(k):
        problems.append(f"{k} missing")
if problems:
    print("  (b) msan include entry missing/incorrect keys: " + ", ".join(problems), file=sys.stderr)
    sys.exit(6)

print(f"  (a) YAML parses; (b) msan include entry present with mirrored keys "
      f"(os={entry['os']}, c-compiler={entry['c-compiler']})")
PY
then
    echo "  (a)/(b) FAILED" >&2
    fail=1
fi

# (c) The stale ubuntu-18.04 / clang-6.0 commented msan block must be gone.
# Look for a commented-out matrix entry that pairs a msan build-type with the
# retired ubuntu-18.04 / clang-6.0 toolchain.
if grep -qE '^[[:space:]]*#.*ubuntu-18\.04' "$WF" || grep -qE '^[[:space:]]*#.*clang-6\.0' "$WF"; then
    echo "  (c) stale commented msan block (ubuntu-18.04 / clang-6.0) is still present" >&2
    fail=1
else
    echo "  (c) stale ubuntu-18.04 / clang-6.0 commented msan block removed"
fi

# (d) The "Run tests" step must wire MSAN_OPTIONS and a scoped TESTS= run.
if grep -q 'MSAN_OPTIONS' "$WF"; then
    echo "  (d1) MSAN_OPTIONS wiring present"
else
    echo "  (d1) MSAN_OPTIONS wiring absent from the workflow" >&2
    fail=1
fi
if grep -qE 'make check TESTS=' "$WF"; then
    echo "  (d2) scoped 'make check TESTS=' invocation present"
else
    echo "  (d2) scoped 'make check TESTS=' invocation absent from the workflow" >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "check-msan-lane: FAIL — the msan lane is not fully wired" >&2
    exit 1
fi

echo "check-msan-lane: PASS — msan lane structurally wired"
