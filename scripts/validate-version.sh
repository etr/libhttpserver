#!/usr/bin/env bash
# Validate version consistency between a tag, configure.ac, and ChangeLog.
# Usage: validate-version.sh VERSION
# VERSION should NOT have a 'v' prefix (e.g., "0.19.0").

set -euo pipefail

VERSION="${1:-}"

if [ -z "$VERSION" ]; then
    echo "Usage: validate-version.sh VERSION" >&2
    exit 1
fi

# Strip leading 'v' if present
VERSION="${VERSION#v}"

CONFIGURE_AC="${CONFIGURE_AC:-configure.ac}"
CHANGELOG="${CHANGELOG:-ChangeLog}"

errors=0

# Parse expected major.minor.revision
IFS='.' read -r expected_major expected_minor expected_revision <<< "${VERSION%%-*}"

if [ -z "$expected_major" ] || [ -z "$expected_minor" ] || [ -z "$expected_revision" ]; then
    echo "Error: VERSION must be in X.Y.Z format (got: $VERSION)" >&2
    exit 1
fi

# Check configure.ac
if [ ! -f "$CONFIGURE_AC" ]; then
    echo "Error: $CONFIGURE_AC not found" >&2
    exit 1
fi

actual_major=$(grep 'm4_define(\[libhttpserver_MAJOR_VERSION\]' "$CONFIGURE_AC" | sed 's/.*\[\([0-9]*\)\].*/\1/')
actual_minor=$(grep 'm4_define(\[libhttpserver_MINOR_VERSION\]' "$CONFIGURE_AC" | sed 's/.*\[\([0-9]*\)\].*/\1/')
actual_revision=$(grep 'm4_define(\[libhttpserver_REVISION\]' "$CONFIGURE_AC" | sed 's/.*\[\([0-9]*\)\].*/\1/')

if [ "$actual_major" != "$expected_major" ] || [ "$actual_minor" != "$expected_minor" ] || [ "$actual_revision" != "$expected_revision" ]; then
    echo "Error: configure.ac version ($actual_major.$actual_minor.$actual_revision) does not match tag ($VERSION)" >&2
    errors=$((errors + 1))
else
    echo "OK: configure.ac version matches ($actual_major.$actual_minor.$actual_revision)"
fi

# Check ChangeLog has a Version header for this version
if [ ! -f "$CHANGELOG" ]; then
    echo "Error: $CHANGELOG not found" >&2
    exit 1
fi

# Match "Version X.Y.Z" at start of line (allowing trailing date or text)
base_version="${VERSION%%-*}"
if grep -q "^Version ${base_version}" "$CHANGELOG"; then
    echo "OK: ChangeLog contains Version ${base_version} header"
else
    echo "Error: ChangeLog missing 'Version ${base_version}' header" >&2
    errors=$((errors + 1))
fi

if [ "$errors" -gt 0 ]; then
    echo "Validation failed with $errors error(s)" >&2
    exit 1
fi

echo "Version validation passed for $VERSION"
