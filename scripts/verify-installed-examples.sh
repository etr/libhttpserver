#!/usr/bin/env bash
# Compile native public examples using a staged installed package.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
STAGE="$(mktemp -d -t libhttpserver-installed-examples-XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
CONFIG_STATUS="$BUILD_DIR/config.status"
fail() { echo "verify-installed-examples: FAIL: $*" >&2; exit 1; }
source "$(dirname "$0")/lib/resolve-prefix.sh"
PREFIX="$STAGE$RESOLVED_PREFIX"
make -C "$BUILD_DIR" install DESTDIR="$STAGE" >"$STAGE/install.log" 2>&1 || {
    cat "$STAGE/install.log"; exit 1;
}
read -r -a CXX_CMD <<< "${CXX:-c++}"
read -r -a FLAGS <<< "$(PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig" PKG_CONFIG_PATH= \
    pkg-config --define-variable=prefix="$PREFIX" --libs --static libhttpserver)"
for name in v3_external_event_loop v3_routes v3_protocol_options v3_websocket_echo; do
    "${CXX_CMD[@]}" -std=c++20 -I"$PREFIX/include" "$REPO_ROOT/examples/$name.cpp" \
        "${FLAGS[@]}" -Wl,-rpath,"$PREFIX/lib" -o "$STAGE/$name"
    echo "PASS installed native example: $name"
done
