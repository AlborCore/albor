#!/bin/bash

# check-link-boundary.sh - verify the shipping Release archives carry no dev-only symbol
# a dev-only feature must be excluded at link time, so its absence has to be checked, not assumed (BDG-03, BDG-04, §2.3)
# usage: check-link-boundary.sh [<static-library> [<forbidden-substring> ...]]
# with no library argument, every Release archive under ALBOR_RELEASE_DIR is inspected

set -uo pipefail

# the dev runtime defines this symbol in Debug and Test, never in a shipping Release build
FORBIDDEN=("albor5devrt11last_report" "last_report@devrt@albor")
if [ $# -ge 2 ]; then
    FORBIDDEN=("${@:2}")
fi

LIBRARIES=()
RELEASE_DIR="${ALBOR_RELEASE_DIR:-.build-release/albor}"
if [ $# -ge 1 ] && [ -n "$1" ]; then
    LIBRARIES+=("$1")
else
    # the shipping archives exclude tools and tests, which are not part of the consumer surface
    while IFS= read -r library; do
        [ -f "$library" ] && LIBRARIES+=("$library")
    done < <(find "$RELEASE_DIR" -type f -name 'libalbor-*.a' \
        -not -path '*/tests/*' -not -path '*/tools/*' -not -path '*/compiler/*' | sort)
fi

if [ ${#LIBRARIES[@]} -eq 0 ]; then
    echo "error: no Release archive to inspect under $RELEASE_DIR; configure and build the release preset first" >&2
    exit 1
fi

# a pattern that no longer names a Dev symbol would let anything pass, so prove it still fires
REFERENCE="${ALBOR_DEV_REFERENCE:-.build-debug/albor/devrt/libalbor-devrt.a}"
if [ -f "$REFERENCE" ]; then
    reference_symbols="$(nm --defined-only "$REFERENCE" 2>/dev/null)"
    matched=0
    for pattern in "${FORBIDDEN[@]}"; do
        if printf '%s\n' "$reference_symbols" | grep -F "$pattern" > /dev/null; then
            matched=1
        fi
    done
    if [ "$matched" -eq 0 ]; then
        echo "error: no forbidden pattern appears in $REFERENCE; update the dev-only symbol before trusting this gate" >&2
        exit 1
    fi
fi

status=0
for LIBRARY in "${LIBRARIES[@]}"; do
    if [ ! -f "$LIBRARY" ]; then
        echo "error: no library to inspect: $LIBRARY" >&2
        status=1
        continue
    fi
    symbols="$(nm --defined-only "$LIBRARY" 2>/dev/null)"
    for pattern in "${FORBIDDEN[@]}"; do
        found="$(printf '%s\n' "$symbols" | grep -F "$pattern" || true)"
        if [ -n "$found" ]; then
            echo "error: dev-only symbol '$pattern' is linked into $LIBRARY" >&2
            echo "$found" >&2
            status=1
        fi
    done
done

if [ "$status" -eq 0 ]; then
    echo "link-boundary: ${#LIBRARIES[@]} Release archive(s) carry no dev-only symbol"
fi
exit "$status"
