#!/usr/bin/env bash
# Native, sequential libFuzzer lane. Explicit requests never silently skip.
set -euo pipefail
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
    case "$target" in http1_head|http1_body|websocket_codec|hpack|http2_engine|quic_parser|quic_state|quic_recovery|quic_tls_callbacks|http3_engine|qpack) ;; *) echo "unknown fuzz target: $target" >&2; exit 2 ;; esac
done
command -v "$compiler" >/dev/null || { echo 'libFuzzer compiler unavailable' >&2; exit 1; }
provider_cflags=() provider_libs=()
for target in $targets; do
    case "$target" in
        quic_state|quic_recovery|http3_engine|qpack)
            [ -f "$native_config/config.h" ] || { echo "$target fuzz requires --native-config-dir with a configured TLS-off build" >&2; exit 1; }
            if grep -q '^#define NATIVE_V3_TLS' "$native_config/config.h"; then
                echo "$target fuzz requires a TLS-off native configuration" >&2; exit 1
            fi ;;
        quic_tls_callbacks)
            [ -f "$native_config/config.h" ] && grep -q '^#define NATIVE_V3_TLS' "$native_config/config.h" || {
                echo 'QUIC callbacks fuzz requires a TLS-on native configuration' >&2; exit 1;
            }
            # Read literal configure substitutions, never shell-evaluate Makefile text.
            provider_args=$(mktemp -d)
            trap 'rm -rf "$provider_args"' EXIT
            python3 - "$native_config/src/Makefile" "$provider_args" <<'PYTHON'
import pathlib, re, shlex, sys
try:
    text = pathlib.Path(sys.argv[1]).read_text()
    for name, suffix in [('V3_TLS_CFLAGS', 'cflags'), ('V3_TLS_LIBS', 'libs')]:
        match = re.search(r'^' + name + r' = (.+)$', text, re.MULTILINE)
        if not match or '$' in match[1]:
            raise ValueError('missing literal ' + name)
        flags = shlex.split(match[1])
        if not flags or any('\n' in flag or '\0' in flag for flag in flags):
            raise ValueError('empty or invalid ' + name)
        pathlib.Path(sys.argv[2], suffix).write_bytes(b''.join(flag.encode() + b'\0' for flag in flags))
except (OSError, ValueError) as error:
    print('QUIC callbacks fuzz requires configured provider flags: ' + str(error), file=sys.stderr)
    sys.exit(1)
PYTHON
            while IFS= read -r -d '' argument; do provider_cflags+=("$argument"); done < "$provider_args/cflags"
            while IFS= read -r -d '' argument; do provider_libs+=("$argument"); done < "$provider_args/libs"
            rm -rf "$provider_args"
            trap - EXIT ;;
    esac
done
case "$runs:$seconds" in *[!0-9:]*) echo 'invalid fuzz bounds' >&2; exit 2 ;; esac
[ "$runs" -gt 0 ] && [ "$seconds" -gt 0 ] || exit 2
mkdir -p "$build"
build=$(cd "$build" && pwd)
printf '%s\n' '#include <cstddef>' '#include <cstdint>' 'extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) { return 0; }' > "$build/probe.cpp"
flags=(-std=c++20 -pthread -g -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -DHTTPSERVER_COMPILATION -DPROTOCOL_LIBFUZZER -I"$root/src")
"$compiler" "${flags[@]}" "$build/probe.cpp" -o "$build/probe" > "$build/capability.log" 2>&1 || { echo "libFuzzer instrumentation unavailable; see $build/capability.log" >&2; exit 1; }
"$build/probe" -runs=1 -seed=128 -artifact_prefix="$build/probe-" >> "$build/capability.log" 2>&1 || exit 1
{ python3 "$root/scripts/lib/v3_release_artifact.py" --root "$root"; "$compiler" --version; printf '%s ' "${flags[@]}"; printf '\n'; } > "$build/environment.log"
for target in $targets; do
    mkdir -p "$build/$target/seeds" "$build/$target/artifacts"
    # Only copies are writable by libFuzzer; source fixtures remain immutable.
    max_len=65536
    case "$target" in
        http3_engine) cp "$root"/test/data/http3/*.seed "$build/$target/seeds/"; max_len=4096 ;;
        qpack) cp "$root"/test/data/qpack/seeds/*.seed "$build/$target/seeds/"; max_len=512 ;;
        quic_parser) cp "$root"/test/data/quic/parser/*.seed "$build/$target/seeds/"; max_len=4096 ;;
        quic_recovery) cp "$root"/test/data/quic/recovery/*.seed "$build/$target/seeds/"; max_len=4096 ;;
        quic_tls_callbacks) cp "$root"/test/data/quic/callbacks/*.seed "$build/$target/seeds/"; max_len=4096 ;;
        quic_state) cp "$root"/test/data/quic/state/*.seed "$build/$target/seeds/"; max_len=4096 ;;
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
    [ -n "$(find "$build/$target/seeds" -type f -print -quit)" ] || { echo "empty corpus: $target" >&2; exit 1; }
    sources=("$root/test/fuzz/${target}_fuzz.cpp")
    if [ "$target" = http1_head ]; then sources+=("$root/src/detail/net_address.cpp"); fi
    if [ "$target" = websocket_codec ]; then sources+=("$root/src/detail/websocket_codec.cpp"); fi
    if [ "$target" = quic_parser ]; then
        sources+=("$root/src/detail/quic_invariant_header.cpp" "$root/src/detail/quic_varint.cpp"
            "$root/src/detail/quic_packet.cpp" "$root/src/detail/quic_frame.cpp"
            "$root/src/detail/quic_frame_write.cpp" "$root/src/detail/quic_transport_parameters.cpp")
    fi
    if [ "$target" = quic_state ]; then sources+=("$root/test/support/quic_network_harness.cpp"); fi
    target_flags=()
    if [ "$target" = quic_parser ]; then target_flags+=(-DQUIC_PARSER_LIBFUZZER -I"$root/test"); fi
    if [ "$target" = quic_state ]; then target_flags+=(-DQUIC_STATE_LIBFUZZER -I"$root/test" -I"$native_config"); fi
    if [ "$target" = quic_recovery ]; then target_flags+=(-DQUIC_RECOVERY_LIBFUZZER -I"$root/test" -I"$native_config"); fi
    if [ "$target" = quic_tls_callbacks ]; then
        target_flags+=(-DQUIC_TLS_CALLBACKS_LIBFUZZER -I"$root/test" -I"$native_config" "${provider_cflags[@]}")
        printf '%s\n' "${provider_cflags[@]}" "${provider_libs[@]}" > "$build/$target/provider.log"
    fi
    if [ "$target" = http3_engine ]; then target_flags+=(-DHTTP3_ENGINE_LIBFUZZER -I"$root/test" -I"$native_config"); fi
    if [ "$target" = qpack ]; then target_flags+=(-DQPACK_LIBFUZZER -I"$root/test" -I"$native_config"); fi
    if [ "$target" = hpack ]; then target_flags+=(-DHPACK_LIBFUZZER -I"$root/test"); fi
    if [ "$target" = http2_engine ]; then
        [ -f "$native_config/config.h" ] || { echo 'HTTP/2 fuzz requires --native-config-dir with a configured TLS-off build' >&2; exit 1; }
        if grep -q '^#define NATIVE_V3_TLS' "$native_config/config.h"; then
            echo 'HTTP/2 fuzz requires a TLS-off native configuration' >&2; exit 1
        fi
        target_flags+=(-DHTTP2_ENGINE_LIBFUZZER -I"$root/test" -I"$native_config")
    fi
    if [ "$target" = http3_engine ] || [ "$target" = qpack ] || [ "$target" = http2_engine ] || [ "$target" = quic_state ] || [ "$target" = quic_recovery ] || [ "$target" = quic_tls_callbacks ]; then
        # Compile configured native project sources with the same instrumentation;
        # linking an ordinary library would leave the engine uninstrumented.
        python3 - "$root/src/Makefile.am" "$target" > "$build/$target/sources.list" <<'PYTHON'
import pathlib, re, sys
names = ['v3core']
if sys.argv[2] == 'quic_tls_callbacks':
    names.append('v3tls')
found = []
for line in pathlib.Path(sys.argv[1]).read_text().splitlines():
    match = re.match(r'^libhttpserver_(v3core|v3tls)_la_SOURCES\s*\+?=\s*(.*)$', line)
    if match and match[1] in names:
        for source in match[2].split():
            if not source.endswith('.cpp'):
                raise SystemExit('unsupported native source entry: ' + source)
            found.append(source)
if not found:
    raise SystemExit('missing native instrumentation sources')
print('\n'.join(dict.fromkeys(found)))
PYTHON
        while IFS= read -r source; do sources+=("$root/src/$source"); done < "$build/$target/sources.list"
    fi
    link_flags=()
    if [ "$target" = quic_tls_callbacks ]; then link_flags+=("${provider_libs[@]}"); fi
    printf '%q ' "$compiler" "${flags[@]}" "${target_flags[@]}" "${sources[@]}" "${link_flags[@]}" -o "$build/$target/fuzz" > "$build/$target/compile.sh"
    printf '\n' >> "$build/$target/compile.sh"
    "$compiler" "${flags[@]}" "${target_flags[@]}" "${sources[@]}" "${link_flags[@]}" -o "$build/$target/fuzz" > "$build/$target/build.log" 2>&1
    printf '%q ' "$build/$target/fuzz" "$build/$target/seeds" -runs="$runs" -max_total_time="$seconds" -max_len="$max_len" -timeout=5 -rss_limit_mb=512 -seed=128 -artifact_prefix="$build/$target/artifacts/" > "$build/$target/reproduce.sh"
    printf '\n' >> "$build/$target/reproduce.sh"
    "$build/$target/fuzz" "$build/$target/seeds" -runs="$runs" -max_total_time="$seconds" -max_len="$max_len" -timeout=5 -rss_limit_mb=512 -seed=128 -artifact_prefix="$build/$target/artifacts/" > "$build/$target/run.log" 2>&1 || { echo "fuzz failure: $target; see $build/$target/run.log and artifacts" >&2; exit 1; }
    echo "PASS fuzz $target"
done
