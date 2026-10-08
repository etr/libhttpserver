#!/bin/sh
# Test packaging only. Unsupported upstream roles/cases must return 127.
set -eu
[ "${ROLE:-}" = server ] || exit 127
case "${TESTCASE:-}" in handshake|transfer) ;; *) exit 127 ;; esac
[ "${LIBHTTPSERVER_INTEROP_TEST_MODE:-}" = 1 ] || { echo 'explicit interop test mode required' >&2; exit 1; }
[ -z "${SERVER_PARAMS:-}" ] || { echo 'SERVER_PARAMS unsupported' >&2; exit 1; }
if [ "${LIBHTTPSERVER_INTEROP_LOCAL_DIRECT:-0}" != 1 ]; then
    /setup.sh
fi
exec "${LIBHTTPSERVER_INTEROP_ENDPOINT:-/quic_interop_endpoint}"
