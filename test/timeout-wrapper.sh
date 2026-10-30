#!/bin/sh
# Bound one native test program so a single hung binary cannot consume
# a whole CI make-check command budget. Hosts without timeout(1)
# (macOS by default) run tests exactly as before.
if command -v timeout >/dev/null 2>&1; then
    exec timeout -k 60 600 "$@"
fi
exec "$@"
