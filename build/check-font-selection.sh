#!/bin/bash
# font selection check: prove the codegen receives the font asset list and rejects an uncarried selection (§11.1, AD-34)
# usage: bash build/check-font-selection.sh
# the build directory may be named with ALBOR_BUILD_DIR, which the ci matrix sets per preset
set -euo pipefail

# the repository root, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# the build tree the codegen binary lives in, which a preset names
BUILD="${ALBOR_BUILD_DIR:-.build-debug}"
# the codegen driver the build produced, which is what this check exercises
CODEGEN="$BUILD/tools/albor-codegen/albor-codegen"
# a windows runner appends the executable suffix, which the probe and the runs both use
if [ ! -x "$CODEGEN" ] && [ -x "$CODEGEN.exe" ]; then
    CODEGEN="$CODEGEN.exe"
fi

# a missing driver means the build did not produce one, which must not read as a pass
if [ ! -x "$CODEGEN" ]; then
    echo "error: no codegen driver at $CODEGEN" >&2
    exit 1
fi

# the generated files land in a scratch directory, which is removed on every exit path
OUT="$(mktemp -d "${TMPDIR:-/tmp}/albor-font-selection.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

# the faces the fixture declares, which both the positive and negative runs carry
FONTS=(--default-font-family Lato --font Lato regular tests/fonts/Lato-Regular.ttf --font Lato bold tests/fonts/Lato-Bold.ttf)

# a panel that names a carried family at a carried weight must compile
if ! "$CODEGEN" --input tests/vectors/font_panel.ui --output "$OUT" "${FONTS[@]}" > /dev/null; then
    echo "error: a carried font selection did not compile" >&2
    exit 1
fi

# a panel that names an uncarried family must fail with E0510
if "$CODEGEN" --input tests/vectors/font_missing.ui --output "$OUT" "${FONTS[@]}" > /dev/null 2> "$OUT/errors"; then
    echo "error: an uncarried font selection compiled" >&2
    exit 1
fi
if ! grep -q "E0510" "$OUT/errors"; then
    echo "error: the uncarried family did not report E0510" >&2
    cat "$OUT/errors" >&2
    exit 1
fi

# an unknown weight is a usage error, which the driver reports with exit 2
set +e
"$CODEGEN" --input tests/vectors/font_panel.ui --output "$OUT" --default-font-family Lato --font Lato heavy tests/fonts/Lato-Regular.ttf > /dev/null 2>&1
bad_weight=$?
# a font flag without its file is a usage error too
"$CODEGEN" --input tests/vectors/font_panel.ui --output "$OUT" --default-font-family Lato --font Lato regular > /dev/null 2>&1
missing_file=$?
# a font file that does not exist is a usage error, which a direct caller must not read past
"$CODEGEN" --input tests/vectors/font_panel.ui --output "$OUT" --default-font-family Lato --font Lato regular /albor/no-such-font.ttf > /dev/null 2>&1
missing_font=$?
set -e

# every misuse must exit 2, which is what a build script checks
if [ "$bad_weight" -ne 2 ] || [ "$missing_file" -ne 2 ] || [ "$missing_font" -ne 2 ]; then
    echo "error: a malformed font flag did not exit 2" >&2
    exit 1
fi

# the check is visible in the build log, so a pass is not silent
echo "font selection: ok (carried panel compiles, uncarried family reports E0510)"
