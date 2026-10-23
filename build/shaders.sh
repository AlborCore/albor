#!/bin/bash

# the shaders are compiled by a developer and committed as artifacts, so a consumer build needs no toolchain
# this script is the only place the toolchain runs: it writes the artifact, or checks the one that is committed
# usage: bash build/shaders.sh [--write]                  compile and verify the metal artifact
#        bash build/shaders.sh --d3d [--write]           compile and verify the direct3d artifact
#        bash build/shaders.sh --all [--write]           compile and verify both artifacts

# any failing step aborts, because a check that measures nothing must not report a pass
set -euo pipefail

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# the intermediate is always built outside the tree, so a check leaves nothing behind
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# the modes the caller named, which default to the metal check the script always ran
# the loop reads the arguments through the portable expansion, because an empty list under set -u is an
# unbound variable on the bash 3.2 a macos runner ships, while the quoted form yields no runs anywhere
WRITE=no
D3D=no
METAL=no
for argument in ${1+"$@"}; do
    case "$argument" in
    --write) WRITE=yes ;;
    --d3d) D3D=yes ;;
    --all) D3D=yes; METAL=yes ;;
    *) echo "error: unknown option $argument (supported: --write --d3d --all)" >&2; exit 1 ;;
    esac
done
if [ "$D3D" = no ] && [ "$METAL" = no ]; then
    METAL=yes
fi

# the digest tool differs by host: coreutils ships sha256sum, while the macos runner only ships shasum
sha256_of() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

check_metal() {
    local SOURCE=render/render-albor/albor_shaders.metal ARTIFACT=render/render-albor/albor_shaders.metallib STAMP=render/render-albor/albor_shaders.metal.sha256

    # the language and the system floor the artifact is built for, which a consumer cannot raise by accident
    # metal 3.0 is the language the 13.3 floor provides, and the oldest one this shader needs (AD-16)
    local STD=metal3.0 DEPLOYMENT=13.3

    # every name the backend looks up in the library, because a missing one fails where the frame is drawn
    local ENTRY_POINTS="albor_vertex albor_fragment albor_fragment_exact shadow_vertex shadow_shape shadow_blur"

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
    local entry
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
    if [ "$WRITE" = yes ]; then
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
    local SOURCE_HASH ARTIFACT_HASH
    SOURCE_HASH="$(sha256_of "$SOURCE")"
    ARTIFACT_HASH="$(sha256_of "$ARTIFACT")"

    # a rewrite records both digests, because the pair is what the build compares against
    if [ "$WRITE" = yes ]; then
        printf '%s\n%s\n' "$SOURCE_HASH" "$ARTIFACT_HASH" > "$STAMP"
    fi

    # the stamp is read as two lines, because the first names the source and the second the artifact
    local EXPECTED_SOURCE EXPECTED_ARTIFACT
    EXPECTED_SOURCE="$(sed -n 1p "$STAMP" | tr -d '[:space:]')"
    EXPECTED_ARTIFACT="$(sed -n 2p "$STAMP" | tr -d '[:space:]')"

    if [ "$EXPECTED_SOURCE" != "$SOURCE_HASH" ] || [ "$EXPECTED_ARTIFACT" != "$ARTIFACT_HASH" ]; then
        echo "error: $ARTIFACT is stale against $SOURCE, regenerate it with: bash build/shaders.sh --write" >&2
        exit 1
    fi

    # the result is named on a pass too, so a silent pass is distinguishable from a check that never ran
    echo "shaders: $ARTIFACT matches $SOURCE and targets macosx$DEPLOYMENT"
}

check_d3d() {
    local SOURCE=render/render-albor/albor_shaders.hlsl ARTIFACT=render/render-albor/albor_shaders.dxbc STAMP=render/render-albor/albor_shaders.hlsl.sha256

    # the entry points and the profiles the artifact stitches, which the backend loads in this exact order (§6.3)
    # one container section is a uint32 little endian length followed by that entry point's compiled blob
    local ENTRY_PAIRS="albor_vertex:vs_5_0 albor_fragment:ps_5_0 albor_fragment_exact:ps_5_0 shadow_vertex:vs_5_0 shadow_shape:ps_5_0 shadow_blur:ps_5_0"

    # the toolchain ships with every windows sdk, so a host without it is told where it ships
    local FXC
    FXC="$(find "/c/Program Files (x86)/Windows Kits/10/bin" -name fxc.exe -path "*x64*" 2> /dev/null | head -n 1)"
    if [ -z "$FXC" ]; then
        echo "error: the fxc toolchain is missing, install the windows 10 sdk (c:/program files (x86)/windows kits/10/bin)" >&2
        exit 1
    fi

    # the container header the backend refuses an artifact without, because the format moved
    local MAGIC=ALBDXBC1

    # every section is compiled fresh, because a source that lost an entry point would refuse to compile
    local SECTIONS=$OUT/sections
    mkdir -p "$SECTIONS"
    local pair ENTRY PROFILE SOURCE_HASH ARTIFACT_HASH EXPECTED_SOURCE EXPECTED_ARTIFACT
    for pair in $ENTRY_PAIRS; do
        ENTRY=${pair%%:*}
        PROFILE=${pair##*:}
        # the msys runtime a windows runner ships rewrites a slash argument into a drive path, which
        # would hand fxc a '/' profile as 'X:/', so the conversion is switched off for this invocation
        MSYS2_ARG_CONV_EXCL='*' "$FXC" /nologo /T "$PROFILE" /E "$ENTRY" /Fo "$(cygpath -w "$SECTIONS/$ENTRY.dxbc")" "$(cygpath -w "$SOURCE")"
    done

    # the container is the header, then one length prefixed blob per entry point in the declared order
    local BLOB=$OUT/albor_shaders.dxbc
    printf '%s' "$MAGIC" > "$BLOB"
    for pair in $ENTRY_PAIRS; do
        ENTRY=${pair%%:*}
        local SIZE
        SIZE="$(stat -c %s "$SECTIONS/$ENTRY.dxbc")"
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((SIZE & 255)) $((SIZE >> 8 & 255)) $((SIZE >> 16 & 255)) $((SIZE >> 24 & 255)))" >> "$BLOB"
        cat "$SECTIONS/$ENTRY.dxbc" >> "$BLOB"
    done

    # the committed artifact is replaced only after the fresh build passed every check
    if [ "$WRITE" = yes ]; then
        cp "$BLOB" "$ARTIFACT"
    fi

    # the committed artifact is what ships, so its container shape is checked on every run
    # a container without the header or the sections is a corrupt checkout rather than a stale pair
    if [ ! -e "$ARTIFACT" ] || [ "$(head -c 8 "$ARTIFACT")" != "$MAGIC" ] || [ "$(stat -c %s "$ARTIFACT")" -le 8 ]; then
        echo "error: $ARTIFACT is not the container $SOURCE compiles into, regenerate it with: bash build/shaders.sh --d3d --write" >&2
        exit 1
    fi

    # the stamp names the source and the artifact, so a partial restore of either is caught rather than shipped
    SOURCE_HASH="$(sha256_of "$SOURCE")"
    ARTIFACT_HASH="$(sha256_of "$ARTIFACT")"

    # a rewrite records both digests, because the pair is what the build compares against
    if [ "$WRITE" = yes ]; then
        printf '%s\n%s\n' "$SOURCE_HASH" "$ARTIFACT_HASH" > "$STAMP"
    fi

    # the stamp is read as two lines, because the first names the source and the second the artifact
    EXPECTED_SOURCE="$(sed -n 1p "$STAMP" | tr -d '[:space:]')"
    EXPECTED_ARTIFACT="$(sed -n 2p "$STAMP" | tr -d '[:space:]')"

    if [ "$EXPECTED_SOURCE" != "$SOURCE_HASH" ] || [ "$EXPECTED_ARTIFACT" != "$ARTIFACT_HASH" ]; then
        echo "error: $ARTIFACT is stale against $SOURCE, regenerate it with: bash build/shaders.sh --d3d --write" >&2
        exit 1
    fi

    # the result is named on a pass too, so a silent pass is distinguishable from a check that never ran
    echo "shaders: $ARTIFACT matches $SOURCE across the six entry points"
}

if [ "$METAL" = yes ]; then
    check_metal
fi
if [ "$D3D" = yes ]; then
    check_d3d
fi
