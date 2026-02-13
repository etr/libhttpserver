#!/usr/bin/env bash
# Extract release notes for a given version from the ChangeLog.
# Usage: extract-release-notes.sh [VERSION]
# If VERSION is omitted, extracts the first (most recent) section.

set -euo pipefail

VERSION="${1:-}"

# Strip leading 'v' if present
VERSION="${VERSION#v}"

CHANGELOG="${CHANGELOG:-ChangeLog}"

if [ ! -f "$CHANGELOG" ]; then
    echo "Error: $CHANGELOG not found" >&2
    exit 1
fi

if [ -z "$VERSION" ]; then
    # Extract the first version section (everything between the first and second headers)
    awk '
        /^Version [0-9]+\.[0-9]+\.[0-9]+/ {
            if (found) exit
            found = 1
            next
        }
        found && /^$/ && !started { next }
        found { started = 1; print }
    ' "$CHANGELOG" | sed -e :a -e '/^[[:space:]]*$/{ $d; N; ba; }'
else
    # Extract notes for the specific version
    awk -v ver="$VERSION" '
        /^Version [0-9]+\.[0-9]+\.[0-9]+/ {
            if (found) exit
            if (index($0, "Version " ver " ") == 1 || $0 == "Version " ver) {
                found = 1
                next
            }
        }
        found && /^$/ && !started { next }
        found { started = 1; print }
    ' "$CHANGELOG" | sed -e :a -e '/^[[:space:]]*$/{ $d; N; ba; }'
fi
