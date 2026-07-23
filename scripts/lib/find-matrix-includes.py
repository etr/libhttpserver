#!/usr/bin/env python3
# find-matrix-includes.py — shared helper for the workflow lane gates
# (check-valgrind-lane.sh, check-parallel-install-lane.sh, and any
# future lane gate).
#
# Parses a GitHub Actions workflow file with PyYAML and prints its
# strategy.matrix.include list to stdout as JSON. The job name is not
# hard-coded so the gates survive a job rename; the first job that
# defines an include list wins.
#
# Exit codes (mirrored by the calling gates' (a)/(b) assertions):
#   0   include list printed as JSON on stdout
#   2   YAML parse error
#   3   no strategy.matrix.include found in any job
#   10  PyYAML not importable
import json
import sys

try:
    import yaml
except ImportError:
    print("  PyYAML not importable — install with 'pip install pyyaml'",
          file=sys.stderr)
    sys.exit(10)

path = sys.argv[1]
try:
    doc = yaml.safe_load(open(path))
except Exception as e:  # noqa: BLE001 - surface any parse failure
    print(f"  (a) YAML parse error: {e}", file=sys.stderr)
    sys.exit(2)

include = None
for job_name, job in (doc.get("jobs") or {}).items():
    if not isinstance(job, dict):
        continue
    inc = (((job.get("strategy") or {}).get("matrix") or {}).get("include"))
    if isinstance(inc, list):
        include = inc
        break
if include is None:
    print("  (b) could not locate strategy.matrix.include in any job",
          file=sys.stderr)
    sys.exit(3)

print(json.dumps(include))
