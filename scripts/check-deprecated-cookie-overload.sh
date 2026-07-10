#!/usr/bin/env bash
#
# check-deprecated-cookie-overload.sh — negative-compile gate pinning the
# [[deprecated]] attribute on the legacy string-blob cookie overload
# http_response::with_cookie(std::string, std::string) (TASK-064).
#
# test/unit/cookie_deprecation_sentinel_test.cpp pins the POSITIVE side
# (the deprecated overload still compiles behind a #pragma suppression
# block). The negative side — that the [[deprecated]] warning IS emitted
# on a suppression-free compile — was previously unverified: if someone
# removed the attribute, nothing would catch it (code-review finding on
# TASK-064). This gate closes that hole:
#
#   1. Positive control: a TU calling the structured overload
#      with_cookie(cookie) MUST compile cleanly under
#      -Werror=deprecated-declarations. This proves a failure in step 2
#      comes from the deprecation, not from a broken header or
#      environment.
#   2. Negative check: a TU calling the deprecated overload
#      with_cookie(std::string, std::string) MUST FAIL to compile under
#      -Werror=deprecated-declarations, and the compiler output must
#      mention "deprecated" (so an unrelated compile error cannot fake a
#      pass).
#
# Both TUs include only the umbrella <httpserver.hpp> (public headers
# reject direct inclusion) and are compiled with -fsyntax-only, so no
# build tree, config.h, or libmicrohttpd is needed — the script runs
# standalone from a fresh checkout. -std=c++20 matches the umbrella's
# own version gate (httpserver.hpp #errors below C++20) and the
# CHECK_HEADERS_CXX invocation in Makefile.am.
#
# The compiler is ${CXX:-c++}; the Makefile target passes the configured
# $(CXX) through.
#
# Exit codes:
#   0  attribute in place (positive control compiles, negative TU fails
#      with a deprecation diagnostic)
#   1  regression (deprecated overload compiled cleanly, positive
#      control broke, or the negative TU failed for the wrong reason)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# The configured $(CXX) may be multi-word (e.g. "g++ -std=c++20"), so
# split it into an argv array rather than quoting it as one command name.
read -r -a CXX_CMD <<< "${CXX:-c++}"
COMMON_FLAGS=(-std=c++20 -fsyntax-only -I"$REPO_ROOT/src"
              -Werror=deprecated-declarations)

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/check-deprecated-cookie.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT

# Positive control: structured overload, must compile cleanly.
cat > "$WORKDIR/positive.cpp" <<'EOF'
#include <httpserver.hpp>
void structured_overload_ok(httpserver::http_response& r) {
    r.with_cookie(httpserver::cookie{}.with_name("sid").with_value("v"));
}
EOF

# Negative TU: deprecated legacy overload, must trip
# -Werror=deprecated-declarations.
cat > "$WORKDIR/negative.cpp" <<'EOF'
#include <httpserver.hpp>
#include <string>
void deprecated_overload_trips(httpserver::http_response& r) {
    r.with_cookie(std::string("sid"), std::string("v"));
}
EOF

echo "check-deprecated-cookie-overload: compiler: ${CXX_CMD[*]}"

if ! "${CXX_CMD[@]}" "${COMMON_FLAGS[@]}" "$WORKDIR/positive.cpp" \
        2>"$WORKDIR/positive.log"; then
    echo "check-deprecated-cookie-overload: FAIL — positive control did not compile" >&2
    echo "  the structured with_cookie(cookie) TU must build cleanly under" >&2
    echo "  -Werror=deprecated-declarations; fix the header (or this gate) first:" >&2
    sed 's/^/    /' "$WORKDIR/positive.log" >&2
    exit 1
fi
echo "  positive control: structured with_cookie(cookie) compiles cleanly"

if "${CXX_CMD[@]}" "${COMMON_FLAGS[@]}" "$WORKDIR/negative.cpp" \
        2>"$WORKDIR/negative.log"; then
    echo "check-deprecated-cookie-overload: FAIL — with_cookie(std::string, std::string) compiled without a deprecation error" >&2
    echo "  the [[deprecated]] attribute on the legacy overload in" >&2
    echo "  src/httpserver/http_response.hpp has been removed or no longer fires" >&2
    exit 1
fi

if ! grep -qi "deprecated" "$WORKDIR/negative.log"; then
    echo "check-deprecated-cookie-overload: FAIL — negative TU failed for the wrong reason" >&2
    echo "  expected a -Wdeprecated-declarations diagnostic; got:" >&2
    sed 's/^/    /' "$WORKDIR/negative.log" >&2
    exit 1
fi
echo "  negative check: legacy with_cookie(string, string) trips -Werror=deprecated-declarations"

echo "check-deprecated-cookie-overload: PASS — [[deprecated]] attribute is in place"
