#!/usr/bin/env bash
# Native public examples must remain registered and present in distributions.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$REPO_ROOT" <<'PY'
from pathlib import Path
import re
import sys
root = Path(sys.argv[1])
text = (root / 'examples/Makefile.am').read_text()
programs = set()
for targets in re.findall(r'^noinst_PROGRAMS[ \t]*\+?=[ \t]*(.*)$', text, re.M):
    programs.update(targets.split())
expected = {'v3_external_event_loop', 'v3_routes', 'v3_protocol_options', 'v3_websocket_echo', 'v3_http3'}
if programs != expected:
    raise SystemExit('FAIL: native public example manifest differs: ' + str(programs))
for program in programs:
    if not (root / 'examples' / (program + '.cpp')).is_file():
        raise SystemExit('FAIL: missing native example: ' + program)
print('PASS native public example sources and build targets')
PY
