#!/usr/bin/env bash
#
# resolve-prefix.sh — shared helper for check-soversion.sh and
# check-parallel-install.sh.
#
# Sourced (not executed) by both gate scripts to avoid copy-pasting the
# config.status prefix-resolution pipeline in two places.
#
# Inputs (must be set by the caller before sourcing):
#   CONFIG_STATUS — absolute path to the config.status file in BUILD_DIR.
#   fail          — a shell function that prints a message and exits non-zero.
#
# Outputs (set in the caller's environment):
#   RESOLVED_PREFIX — the configured installation prefix (e.g. /usr/local).
#
# After sourcing, callers typically derive:
#   LIBDIR="$RESOLVED_PREFIX/lib"
#   INCDIR="$RESOLVED_PREFIX/include"
#
# Design note: this helper is intentionally kept standalone (no sourcing of
# further helpers) so that each gate script remains easy to invoke in
# isolation without a complex dependency chain.

# Resolve prefix by asking config.status.  The --config flag prints the
# original configure arguments, one per line after splitting on spaces.  We
# grep for the --prefix= token, strip surrounding single-quotes (an autoconf
# quoting convention), and fall back to /usr/local when absent.
#
# POSIX-portable sed is used: avoid GNU \? (undefined in POSIX BRE/ERE).
RESOLVED_PREFIX="$("$CONFIG_STATUS" --config 2>/dev/null \
    | tr ' ' '\n' \
    | grep -E "^'?--prefix=" \
    | head -1 \
    | sed "s/^'*--prefix=//;s/'*$//")"
[ -z "$RESOLVED_PREFIX" ] && RESOLVED_PREFIX="/usr/local"

# Guard: an absolute path is required for safe use in rm -rf / mkdir -p.
case "$RESOLVED_PREFIX" in
    /*) ;;
    *) fail "resolved prefix is not absolute: $RESOLVED_PREFIX" ;;
esac
