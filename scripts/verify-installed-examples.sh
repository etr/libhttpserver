#!/usr/bin/env bash
# Compile and run all examples using an exclusively staged installed package.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
STAGE="$(mktemp -d -t libhttpserver-installed-examples-XXXXXX)"
CONSUMER="$(mktemp -d -t libhttpserver-example-consumers-XXXXXX)"
trap 'rm -rf "$STAGE" "$CONSUMER"' EXIT
CONFIG_STATUS="$BUILD_DIR/config.status"
fail() { echo "verify-installed-examples: FAIL: $*" >&2; exit 1; }
source "$(dirname "$0")/lib/resolve-prefix.sh"
PREFIX="$STAGE$RESOLVED_PREFIX"
make -C "$BUILD_DIR" install DESTDIR="$STAGE" >"$STAGE/install.log" 2>&1 || {
    cat "$STAGE/install.log"; exit 1;
}
"${PYTHON:-python3}" "$REPO_ROOT/scripts/lib/example_consumers.py" \
    "$REPO_ROOT" "$BUILD_DIR" "$PREFIX" "$CONSUMER"
