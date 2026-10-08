#!/usr/bin/env bash
# Native, sequential libFuzzer lane. Explicit requests never silently skip.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
compiler=clang++ build= runs=2000 seconds=30 targets='http1_head http1_body websocket_codec' native_config=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --compiler) compiler=$2; shift 2 ;;
        --build-dir) build=$2; shift 2 ;;
        --native-config-dir) native_config=$2; shift 2 ;;
        --targets) targets=$2; shift 2 ;;
        --runs) runs=$2; shift 2 ;;
        --seconds) seconds=$2; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ -n "$build" ] || { echo 'required: --build-dir' >&2; exit 2; }
[ -n "${targets//[[:space:]]/}" ] || { echo 'empty fuzz target selection' >&2; exit 2; }
for target in $targets; do
    case "$target" in http1_head|http1_body|websocket_codec|hpack|http2_engine) ;; *) echo "unknown fuzz target: $target" >&2; exit 2 ;; esac
done
command -v "$compiler" >/dev/null || { echo 'libFuzzer compiler unavailable' >&2; exit 1; }
case "$runs:$seconds" in *[!0-9:]*) echo 'invalid fuzz bounds' >&2; exit 2 ;; esac
[ "$runs" -gt 0 ] && [ "$seconds" -gt 0 ] || exit 2
mkdir -p "$build"
build=$(cd "$build" && pwd)
printf '%s\n' '#include <cstddef>' '#include <cstdint>' 'extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) { return 0; }' > "$build/probe.cpp"
flags=(-std=c++20 -pthread -g -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined -DHTTPSERVER_COMPILATION -DPROTOCOL_LIBFUZZER -I"$root/src")
"$compiler" "${flags[@]}" "$build/probe.cpp" -o "$build/probe" > "$build/capability.log" 2>&1 || { echo "libFuzzer instrumentation unavailable; see $build/capability.log" >&2; exit 1; }
"$build/probe" -runs=1 >> "$build/capability.log" 2>&1 || exit 1
{ git -C "$root" rev-parse HEAD; "$compiler" --version; printf '%s ' "${flags[@]}"; printf '\n'; } > "$build/environment.log"
for target in $targets; do
    mkdir -p "$build/$target/seeds" "$build/$target/artifacts"
    # Only copies are writable by libFuzzer; source fixtures remain immutable.
    max_len=65536
    case "$target" in
        hpack) cp "$root"/test/data/hpack/seeds/*.seed "$build/$target/seeds/"; max_len=512 ;;
        http2_engine) cp "$root"/test/conformance/http2-engine/*.wire "$build/$target/seeds/" ;;
        websocket_codec) cp "$root"/test/conformance/websocket/*.wire "$build/$target/seeds/" ;;
        http1_head) cp "$root"/test/conformance/http1/*.wire "$build/$target/seeds/" ;;
        http1_body)
            python3 - "$root/test/conformance/http1" "$build/$target/seeds" <<'PY'
import pathlib, sys
for wire in pathlib.Path(sys.argv[1]).glob('*.wire'):
    head, separator, body = wire.read_bytes().partition(b'\r\n\r\n')
    if body:
        pathlib.Path(sys.argv[2], wire.name).write_bytes(body)
PY
            ;;
    esac
    sources=("$root/test/fuzz/${target}_fuzz.cpp")
    if [ "$target" = http1_head ]; then sources+=("$root/src/detail/net_address.cpp"); fi
    if [ "$target" = websocket_codec ]; then sources+=("$root/src/detail/websocket_codec.cpp"); fi
    target_flags=()
    if [ "$target" = hpack ]; then target_flags+=(-DHPACK_LIBFUZZER -I"$root/test"); fi
    if [ "$target" = http2_engine ]; then
        [ -f "$native_config/config.h" ] || { echo 'HTTP/2 fuzz requires --native-config-dir with a configured TLS-off build' >&2; exit 1; }
        if grep -q '^#define NATIVE_V3_TLS' "$native_config/config.h"; then
            echo 'HTTP/2 fuzz requires a TLS-off native configuration' >&2; exit 1
        fi
        target_flags+=(-DHTTP2_ENGINE_LIBFUZZER -I"$root/test" -I"$native_config")
        # Compile the native TLS-off sources with the same instrumentation;
        # linking an ordinary library would leave the engine uninstrumented.
        while IFS= read -r source; do sources+=("$root/src/$source"); done < <(
            python3 - "$root/src/Makefile.am" <<'PYTHON'
import pathlib, re, sys
for line in pathlib.Path(sys.argv[1]).read_text().splitlines():
    match = re.match(r'^libhttpserver_v3core_la_SOURCES\s*\+?=\s*(.*)$', line)
    if match:
        for source in match[1].split():
            if not source.endswith('.cpp'):
                raise SystemExit('unsupported native source entry: ' + source)
            print(source)
PYTHON
        )
    fi
    "$compiler" "${flags[@]}" "${target_flags[@]}" "${sources[@]}" -o "$build/$target/fuzz" > "$build/$target/build.log" 2>&1
    printf '%q ' "$build/$target/fuzz" "$build/$target/seeds" -runs="$runs" -max_total_time="$seconds" -max_len="$max_len" -timeout=5 -rss_limit_mb=512 -seed=128 -artifact_prefix="$build/$target/artifacts/" > "$build/$target/reproduce.sh"
    printf '\n' >> "$build/$target/reproduce.sh"
    "$build/$target/fuzz" "$build/$target/seeds" -runs="$runs" -max_total_time="$seconds" -max_len="$max_len" -timeout=5 -rss_limit_mb=512 -seed=128 -artifact_prefix="$build/$target/artifacts/" > "$build/$target/run.log" 2>&1 || { echo "fuzz failure: $target; see $build/$target/run.log and artifacts" >&2; exit 1; }
    echo "PASS fuzz $target"
done
