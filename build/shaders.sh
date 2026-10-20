#!/bin/bash

# the metal shader is compiled by a developer and committed as an artifact, so a consumer build needs no toolchain
# this script is the only place the toolchain runs: it writes the artifact, or checks the one that is committed
# usage: bash build/shaders.sh          compile to a temporary directory and verify the committed artifact
#        bash build/shaders.sh --write  compile into the tree, rewrite the artifact and its stamp

# any failing step aborts, because a check that measures nothing must not report a pass
set -euo pipefail

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# the shader, the artifact it produces and the stamp that records which source the artifact came from
SOURCE="render/render-albor/albor_shaders.metal"
ARTIFACT="render/render-albor/albor_shaders.metallib"
STAMP="render/render-albor/albor_shaders.metal.sha256"

# the language and the system floor the artifact is built for, which a consumer cannot raise by accident
# metal 3.0 is the language the 13.3 floor provides, and the oldest one this shader needs (AD-16)
STD="metal3.0"
DEPLOYMENT="13.3"

# every name the backend looks up in the library, because a missing one fails where the frame is drawn
ENTRY_POINTS="albor_vertex albor_fragment albor_fragment_exact shadow_vertex shadow_shape shadow_blur"

# the intermediate is always built outside the tree, so a check leaves nothing behind
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# the toolchain ships as a separate xcode component, so a host without it is told how to install it
if ! xcrun -sdk macosx metal --version > /dev/null 2>&1; then
    echo "error: the metal toolchain is missing, install it with: xcodebuild -downloadComponent MetalToolchain" >&2
    exit 1
fi

# the build declares the same floor, because the artifact and the binaries ship together
if ! grep -q "set(ALBOR_OSX_MINIMUM \"$DEPLOYMENT\"" CMakeLists.txt; then
    echo "error: CMakeLists.txt does not declare macOS $DEPLOYMENT as its deployment target" >&2
    exit 1
fi

# the compiler writes the intermediate the linker reads
xcrun -sdk macosx metal -c "$SOURCE" -std="$STD" -mmacosx-version-min="$DEPLOYMENT" -o "$OUT/albor_shaders.air"

# the link produces the library a device loads, which is this source's own output
xcrun -sdk macosx metallib "$OUT/albor_shaders.air" -o "$OUT/albor_shaders.metallib"

# the strings of a library are read once, because a pipe closes early and hides the tool's own status
strings -a "$OUT/albor_shaders.metallib" > "$OUT/fresh.txt"

# the fresh build must name every entry point, because a source that lost one would link but never draw
for entry in $ENTRY_POINTS; do
    if ! grep -q "$entry" "$OUT/fresh.txt"; then
        echo "error: $SOURCE builds a library without the $entry entry point" >&2
        exit 1
    fi
done

# the fresh build must target the floor this script declares, because a newer one would not load on the minimum
if ! grep -q "apple-macosx$DEPLOYMENT" "$OUT/fresh.txt"; then
    echo "error: $SOURCE builds a library that does not target macosx$DEPLOYMENT" >&2
    exit 1
fi

# the committed artifact is replaced only after the fresh build passed every check
if [ "${1:-}" = "--write" ]; then
    cp "$OUT/albor_shaders.metallib" "$ARTIFACT"
fi

# the committed artifact is what ships, so its entry points and its floor are checked on every run
strings -a "$ARTIFACT" > "$OUT/committed.txt"

# a library that lost an entry point would fail where the pipeline is built, which is why it is caught here
for entry in $ENTRY_POINTS; do
    if ! grep -q "$entry" "$OUT/committed.txt"; then
        echo "error: $ARTIFACT carries no $entry entry point, so it does not come from $SOURCE" >&2
        exit 1
    fi
done

# the committed artifact must carry the system floor this script builds for
if ! grep -q "apple-macosx$DEPLOYMENT" "$OUT/committed.txt"; then
    echo "error: $ARTIFACT does not target macosx$DEPLOYMENT, so it would not load on the supported minimum" >&2
    exit 1
fi

# the stamp names the source and the artifact, so a partial restore of either is caught rather than shipped
SOURCE_HASH="$(shasum -a 256 "$SOURCE" | cut -d' ' -f1)"
ARTIFACT_HASH="$(shasum -a 256 "$ARTIFACT" | cut -d' ' -f1)"

# a rewrite records both digests, because the pair is what the build compares against
if [ "${1:-}" = "--write" ]; then
    printf '%s\n%s\n' "$SOURCE_HASH" "$ARTIFACT_HASH" > "$STAMP"
fi

# the stamp is read as two lines, because the first names the source and the second the artifact
EXPECTED_SOURCE="$(sed -n 1p "$STAMP" | tr -d '[:space:]')"
EXPECTED_ARTIFACT="$(sed -n 2p "$STAMP" | tr -d '[:space:]')"

if [ "$EXPECTED_SOURCE" != "$SOURCE_HASH" ] || [ "$EXPECTED_ARTIFACT" != "$ARTIFACT_HASH" ]; then
    echo "error: $ARTIFACT is stale against $SOURCE, regenerate it with: bash build/shaders.sh --write" >&2
    exit 1
fi

# the result is named on a pass too, so a silent pass is distinguishable from a check that never ran
echo "shaders: $ARTIFACT matches $SOURCE and targets macosx$DEPLOYMENT"
