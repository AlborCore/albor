#!/bin/bash

# check-link-boundary.sh - verify that a shipping library carries no dev-only symbol
# a dev-only feature must be excluded at link time, so its absence has to be checked, not assumed (AD-04, §2.3)
# usage: check-link-boundary.sh <static-library> [forbidden-symbol-substring]

# the library to inspect
LIBRARY="$1"

# the dev-only symbol that must not appear in it
FORBIDDEN="${2:-DirtyBitsetStrategy}"

if [ -z "$LIBRARY" ] || [ ! -f "$LIBRARY" ]; then
    echo "error: no library to inspect: $LIBRARY" >&2
    exit 1
fi

# collect the symbols the library defines
symbols="$(nm --defined-only "$LIBRARY" 2>/dev/null)"

# report the offending symbols so the failure names the leak
found="$(echo "$symbols" | grep -F "$FORBIDDEN")"

if [ -n "$found" ]; then
    echo "error: dev-only symbol '$FORBIDDEN' is linked into $LIBRARY" >&2
    echo "$found" >&2
    echo "move it to its own linked target instead of keeping it in the shipping library (AD-04)" >&2
    exit 1
fi

# report an intact boundary so the check is visible in the build log
echo "link-boundary: $LIBRARY carries no '$FORBIDDEN' symbol"
