#!/usr/bin/env bash
#
# check-dr008-lanes.sh — structural gate for TASK-092 ("Wire the DR-008
# concurrency/deadlock stress gates into per-PR CI").
#
# TASK-092 added two dedicated CI steps to `.github/workflows/verify-build.yml`
# on top of the ordinary `make check` run:
#   - "Run route_table_concurrency under TSan (TASK-092)": loops the DR-008
#     lock-order stress binary RTC_ITERATIONS times on the tsan lane via
#     `make -C test check-route-table-concurrency`.
#   - "Run stop()-from-handler deadlock contract (TASK-092)": exercises the
#     DR-008 stop()-from-handler-deadlocks contract via
#     `make -C test check-stop-from-handler`, on exactly one lane (the
#     baseline Ubuntu gcc/nodebug/dynamic/classic job).
#
# Unlike the msan/valgrind/parallel-install gates, there is no real-time-cost
# reason these two steps could not run locally -- but a silent removal or
# misspelling of either step (or of their `if:` guard / `timeout-minutes:`
# safety net) would quietly drop DR-008 coverage from CI without failing any
# build. This LOCAL structural gate guards against that drift. Mirrors the
# self-testing scripts/check-*.sh gate idiom paired with a
# scripts/test_check_*.sh unit test (see check-msan-lane.sh, check-valgrind-lane.sh).
#
# Assertions against `.github/workflows/verify-build.yml`:
#   (a) the file parses as valid YAML (via python3 + PyYAML; NOT actionlint
#       or yamllint, which are not guaranteed present on the runner);
#   (b) a step exists whose `run:` invokes
#       `make -C test check-route-table-concurrency`, gated by an `if:`
#       condition that references `matrix.build-type == 'tsan'`, and that
#       carries a `timeout-minutes:` value (the documented failure mode --
#       a lock-order regression under TSan -- can hang);
#   (c) a step exists whose `run:` invokes
#       `make -C test check-stop-from-handler`, gated by an `if:` condition
#       that pins it to the single baseline lane (`matrix.os-type == 'ubuntu'`
#       and `matrix.build-type == 'classic'`), and that carries a
#       `timeout-minutes:` value (the documented failure mode is a hang).
#
# Usage: check-dr008-lanes.sh [workflow-file]
#
# Exit codes:
#   0  workflow is wired correctly
#   1  one or more assertions failed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

WF="${1:-.github/workflows/verify-build.yml}"

echo "check-dr008-lanes: inspecting $WF"

if [ ! -f "$WF" ]; then
    echo "check-dr008-lanes: FAIL — workflow file not found: $WF" >&2
    exit 1
fi

fail=0

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

steps = None
for job_name, job in (doc.get("jobs") or {}).items():
    if not isinstance(job, dict):
        continue
    candidate = job.get("steps")
    if isinstance(candidate, list):
        steps = candidate
        break
if steps is None:
    print("  could not locate a job with a `steps:` list", file=sys.stderr)
    sys.exit(3)

problems = []

def find_step(invocation):
    matches = [s for s in steps
               if isinstance(s, dict) and invocation in (s.get("run") or "")]
    if not matches:
        problems.append(f"no step found invoking `{invocation}`")
        return None
    if len(matches) > 1:
        problems.append(f"expected exactly one step invoking `{invocation}`, found {len(matches)}")
    return matches[0]

rtc = find_step("make -C test check-route-table-concurrency")
if rtc is not None:
    if_cond = rtc.get("if") or ""
    if "matrix.build-type == 'tsan'" not in if_cond:
        problems.append(
            "check-route-table-concurrency step's `if:` does not gate on "
            f"matrix.build-type == 'tsan' (got: {if_cond!r})")
    if rtc.get("timeout-minutes") is None:
        problems.append("check-route-table-concurrency step has no timeout-minutes")

sfh = find_step("make -C test check-stop-from-handler")
if sfh is not None:
    if_cond = sfh.get("if") or ""
    if "matrix.os-type == 'ubuntu'" not in if_cond or "matrix.build-type == 'classic'" not in if_cond:
        problems.append(
            "check-stop-from-handler step's `if:` is not pinned to the "
            f"baseline ubuntu/classic lane (got: {if_cond!r})")
    if sfh.get("timeout-minutes") is None:
        problems.append("check-stop-from-handler step has no timeout-minutes")

if problems:
    for p in problems:
        print(f"  (b)/(c) {p}", file=sys.stderr)
    sys.exit(4)

print("  (a) YAML parses; (b) check-route-table-concurrency step present, "
      "tsan-gated, timeout-boxed; (c) check-stop-from-handler step present, "
      "baseline-lane-gated, timeout-boxed")
PY
then
    echo "  (a)/(b)/(c) FAILED" >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "check-dr008-lanes: FAIL — the DR-008 stress gates are not fully wired" >&2
    exit 1
fi

echo "check-dr008-lanes: PASS — DR-008 concurrency/deadlock stress gates structurally wired"
