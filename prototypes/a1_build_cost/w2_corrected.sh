#!/bin/bash
# Corrected W2: does a ccache recover the cold build? (plan.md W2)
#
# The first W2 was invalid: it reused the same install root, so vcpkg found every package already
# installed and did nothing in 3.0 s. That measures vcpkg's no-op, not a rebuild.
#
# It also revealed that the repository's ccache launcher never applies to vcpkg builds at all: the preset
# sets CMAKE_C_COMPILER_LAUNCHER for OUR CMake, while vcpkg drives its own CMake and Ninja invocations.
# So this run wires the launcher through vcpkg's own variables, warms the cache with one build, then wipes
# the build tree and install root and rebuilds - which is exactly what a CI job restoring a ccache does.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

VCPKG="${VCPKG_ROOT:-/Users/rogelio/Projects/vcpkg}/vcpkg"
TRIPLET="x64-osx"
ROOTS="$ROOT/.build-cost/skia-cold"
CACHE="$ROOTS/ccache"
BUILDTREES="$ROOTS/buildtrees"
INSTALL="$ROOTS/installed"
CWD="$ROOTS/manifest"
LOG="$ROOT/prototypes/a1_build_cost/raw/w2_corrected.log"

# the launcher is wired through vcpkg, not through our preset, because vcpkg runs its own build (§28.4)
export VCPKG_C_COMPILER_LAUNCHER=ccache
export VCPKG_CXX_COMPILER_LAUNCHER=ccache

{
    echo "CORRECTED W2: ccache recovery"
    echo "  phase 1: populate a dedicated cache with one full build"
    echo "  phase 2: wipe build tree and install root, keep only the cache, rebuild"
    echo "  compiler launcher: VCPKG_C/CXX_COMPILER_LAUNCHER=ccache (the repository preset does not reach vcpkg)"
} >"$LOG"

# phase 1: wipe the build tree and install root, keep an empty cache, and build to populate it
rm -rf "$BUILDTREES" "$INSTALL"
mkdir -p "$CACHE" "$BUILDTREES" "$INSTALL"
CCACHE_DIR="$CACHE" ccache -z >/dev/null 2>&1
CCACHE_DIR="$CACHE" ccache -C >/dev/null 2>&1

phase1_start=$(python3 -c 'import time; print(time.time())')
(cd "$CWD" && CCACHE_DIR="$CACHE" VCPKG_BINARY_SOURCES=clear "$VCPKG" install --triplet "$TRIPLET" \
    --x-buildtrees-root="$BUILDTREES" --x-install-root="$INSTALL" >>"$LOG" 2>&1)
echo "PHASE1 status=$? wall_s=$(python3 -c "import time; print(f'{time.time() - $phase1_start:.1f}')")" >>"$LOG"
CCACHE_DIR="$CACHE" ccache -s 2>/dev/null | sed 's/^/  phase1 /' >>"$LOG"

# phase 2: what a CI job restoring this cache would pay
rm -rf "$BUILDTREES" "$INSTALL"
mkdir -p "$BUILDTREES" "$INSTALL"
phase2_start=$(python3 -c 'import time; print(time.time())')
(cd "$CWD" && CCACHE_DIR="$CACHE" VCPKG_BINARY_SOURCES=clear "$VCPKG" install --triplet "$TRIPLET" \
    --x-buildtrees-root="$BUILDTREES" --x-install-root="$INSTALL" >>"$LOG" 2>&1)
echo "PHASE2 status=$? wall_s=$(python3 -c "import time; print(f'{time.time() - $phase2_start:.1f}')")" >>"$LOG"
CCACHE_DIR="$CACHE" ccache -s 2>/dev/null | sed 's/^/  phase2 /' >>"$LOG"

echo "log: $LOG"
