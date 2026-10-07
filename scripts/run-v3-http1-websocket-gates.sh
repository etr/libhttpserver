#!/usr/bin/env bash
# Serial TASK-128 gate; logs and the failing program's status survive failure.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
build= logs= independent=false fuzz=false python=python3 compiler=clang++ selected=false
tests='http1_conformance native_http1_conformance websocket_conformance protocol_fuzz_replay native_protocol_memory http1_parser http1_body_mode http1_body_decoder http1_body_source http1_response_outbox native_http1_e2e native_http1_parity websocket_codec websocket_utf8 websocket_session websocket_session_race websocket_progress http1_websocket_handshake http1_websocket_upgrade websocket_upgrade_decision websocket_drain websocket_drain_race routing_corpus hooks_corpus basic_auth_corpus digest_auth_corpus forms_urlencoded_corpus forms_multipart_corpus ip_controls_corpus'
while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir) build=$2; shift 2 ;;
        --log-dir) logs=$2; shift 2 ;;
        --tests) selected=true; tests=$2; shift 2 ;;
        --python) python=$2; shift 2 ;;
        --compiler) compiler=$2; shift 2 ;;
        --independent) independent=true; shift ;;
        --fuzz) fuzz=true; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ -n "$build" ] || { echo 'required: --build-dir' >&2; exit 2; }
case "$tests" in
    *[![:space:]]*) ;;
    *) echo 'empty test selection' >&2; exit 2 ;;
esac
if $independent; then
    command -v "$python" >/dev/null || { echo 'missing Python client tool' >&2; exit 1; }
    "$python" -c 'import websockets; assert websockets.__version__ == "15.0.1"' || exit 1
fi
build=$(cd "$build" && pwd)
logs=${logs:-$build/protocol-gate-logs}
mkdir -p "$logs"
logs=$(cd "$logs" && pwd)
{ if git -C "$root" rev-parse HEAD 2>/dev/null; then
      git -C "$root" status --short;
  else
      printf 'SOURCE_REVISION=source-archive (Git metadata unavailable)\n';
  fi; "$compiler" --version; printf 'CXXFLAGS=%s\n' "${CXXFLAGS:-from build Makefile}"; [ ! -f "$build/Makefile" ] || sed -n '/^CXXFLAGS =/p; /^CXX =/p' "$build/Makefile"; } > "$logs/environment.log"
for name in $tests; do
    case "$name" in *[!a-zA-Z0-9_]*) echo "invalid test name: $name" >&2; exit 2 ;; esac
    [ -x "$build/test/$name" ] || { echo "missing executable: $name" >&2; exit 1; }
    echo "RUN $name"
    status=0
    (cd "$build/test" && "./$name") > "$logs/$name.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then cat "$logs/$name.log" >&2; exit "$status"; fi
done
status=0
if $independent; then
    "$python" "$root/test/integ/native_websocket_client.py" "$build/test/native_websocket_fixture" --independent > "$logs/independent.log" 2>&1 || status=$?
else
    if ! $selected; then
        [ -x "$build/test/native_websocket_fixture" ] || { echo 'missing executable: native_websocket_fixture' >&2; exit 1; }
        command -v "$python" >/dev/null || { echo 'missing Python client tool' >&2; exit 1; }
    fi
    # Restricted --tests probes need only their selected programs.
    if [ -x "$build/test/native_websocket_fixture" ]; then
        "$python" "$root/test/integ/native_websocket_client.py" "$build/test/native_websocket_fixture" > "$logs/raw-client.log" 2>&1 || status=$?
    fi
fi
[ "$status" -eq 0 ] || { cat "$logs/"*client.log "$logs/independent.log" 2>/dev/null >&2 || true; exit "$status"; }
if $fuzz; then "$root/scripts/run-v3-protocol-fuzz.sh" --build-dir "$build/fuzz" --compiler "$compiler"; fi
echo 'TASK-128 focused gate PASS'
