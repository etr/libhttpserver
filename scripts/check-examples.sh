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
sys.path.insert(0, str(root / 'scripts/lib'))
from example_inventory import SOURCES
if programs != set(SOURCES):
    raise SystemExit('FAIL: native public example manifest differs: ' + str(programs))
for program, source in SOURCES.items():
    path = root / source
    if not path.is_file():
        raise SystemExit('FAIL: missing native example: ' + source)
    mapping = re.search(r'^' + program + r'_SOURCES\s*=\s*(.*)$', text, re.M)
    if not mapping or (source.removeprefix('examples/') if source.startswith('examples/') else '$(top_srcdir)/' + source) not in mapping[1].split():
        raise SystemExit('FAIL: incorrect canonical source mapping: ' + program)
    for helper in re.findall(r'^#include "(?:\./)?([^"/]+)"', path.read_text(), re.M):
        if not (root / 'examples' / helper).is_file():
            raise SystemExit('FAIL: missing example helper: ' + helper)
print('PASS native public example sources and build targets')
PY
