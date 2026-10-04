#!/bin/bash
# Cold dependency build measurement (prototypes/a1_build_cost/plan.md, W1/W2/W3).
#
# Every cache that could answer a compile is defeated by isolation rather than deletion, so this cannot
# destroy the caches the repository depends on: a dedicated CCACHE_DIR, the vcpkg binary cache cleared for
# this invocation, and fresh build-tree and install roots. Coldness is then verified from ccache's own
# statistics, not asserted (§28.4).

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

VCPKG="${VCPKG_ROOT:-/Users/rogelio/Projects/vcpkg}/vcpkg"
TRIPLET="x64-osx"
SCRATCH="${SCRATCH:-$ROOT/.build-cost}"
OUT="$ROOT/prototypes/a1_build_cost/raw"
SKIA_SPEC="skia[graphite,metal]"
DAWN_SPEC="dawn[metal]"

mkdir -p "$OUT" "$SCRATCH"

# report what the user's own caches hold, so the isolation is visible in the record (§28.2)
{
    echo "host: $(sw_vers -productVersion) $(uname -m)"
    echo "cpu: $(sysctl -n machdep.cpu.brand_string)"
    echo "cores: $(sysctl -n hw.ncpu) logical / $(sysctl -n hw.physicalcpu) physical"
    echo "ccache: $(ccache --version | head -1)"
    echo "vcpkg: $($VCPKG version)"
    echo "user ccache dir: ${CCACHE_DIR:-~/.cache/ccache} (left untouched)"
    echo "user ccache stats before:"
    ccache -s 2>/dev/null | sed 's/^/  /'
} >"$OUT/environment.log" 2>&1

# run one workload in its own isolated roots and report what it cost
#
# $1 workload name, $2 vcpkg spec, $3 scratch subdirectory, $4 "cold" or "warm"
run_workload() {
    local name="$1"
    local spec="$2"
    local sub="$3"
    local mode="$4"

    local roots="$SCRATCH/$sub"
    local cache="$roots/ccache"
    local buildtrees="$roots/buildtrees"
    local install="$roots/installed"
    local scratch_cwd="$roots/manifest"
    local log="$OUT/$name.log"

    if [ "$mode" = "cold" ]; then
        # start from nothing: a new cache directory, a new build tree, a new install root (§28.4)
        rm -rf "$roots"
        mkdir -p "$cache" "$buildtrees" "$install"
    fi

    # vcpkg is in manifest mode here because the repository ships a vcpkg.json, and manifest mode refuses
    # package arguments: asking for one port would silently install the whole manifest instead. Each workload
    # therefore gets its own scratch directory holding a manifest naming exactly the spec under test, so the
    # workload is the port and its closure and nothing else (§28.2)
    mkdir -p "$scratch_cwd"

    # the port name and its features are separate manifest keys, because "skia[graphite,metal]" is not a valid
    # package name; python writes the manifest so the feature list is real json rather than shell string surgery,
    # which produced a single joined string and a repeated key on the first attempt (§28.4)
    python3 - "$scratch_cwd/vcpkg.json" "albor-build-cost-$sub" "$spec" <<'MANIFEST'
import json
import sys

path, name, spec = sys.argv[1], sys.argv[2], sys.argv[3]

# split "port[feature,feature]" the way vcpkg's own syntax does
port, _, feature_text = spec.partition("[")
features = [feature for feature in feature_text.rstrip("]").split(",") if feature]

manifest = {
    "name": name,
    "version-string": "0.1.0",
    "dependencies": [{"name": port, "features": features}],
}

with open(path, "w", encoding="utf-8") as handle:
    json.dump(manifest, handle, indent=4)
    handle.write("\n")
MANIFEST

    {
        echo "WORKLOAD $name spec=$spec mode=$mode"
        echo "  ccache_dir=$cache"
        echo "  buildtrees_root=$buildtrees"
        echo "  install_root=$install"
        echo "  manifest_dir=$scratch_cwd (isolated, so the port under test is the whole workload)"
        echo "  binary cache: cleared for this invocation"
    } >"$log"

    # the dedicated cache must be empty before a cold workload, or it is not cold
    if [ "$mode" = "cold" ]; then
        CCACHE_DIR="$cache" ccache -z >/dev/null 2>&1
        CCACHE_DIR="$cache" ccache -C >/dev/null 2>&1
    fi

    # time the build, and capture the compiler's peak memory while it runs (§28.2)
    local start_cpu start_wall
    start_wall=$(python3 -c 'import time; print(time.time())')
    start_cpu=$(python3 -c 'import time; print(time.process_time())')

    (
        # the scratch manifest directory, so vcpkg resolves this workload's manifest rather than the
        # repository's (which would install skia, dawn and every other pinned package) (§28.2)
        cd "$scratch_cwd"
        # VCPKG_BINARY_SOURCES=clear is what stops a package being restored from ~/.cache/vcpkg/archives;
        # without it vcpkg would reinstall skia from its 205 cached archives in seconds (§28.4)
        CCACHE_DIR="$cache" \
        VCPKG_BINARY_SOURCES=clear \
        "$VCPKG" install \
            --triplet "$TRIPLET" \
            --x-buildtrees-root="$buildtrees" \
            --x-install-root="$install" \
            >>"$log" 2>&1
    )
    local status=$?

    local wall cpu
    wall=$(python3 -c "import time; print(f'{time.time() - $start_wall:.1f}')")
    cpu=$(python3 -c "import time; print(f'{time.process_time() - $start_cpu:.1f}')")

    # coldness is verified from the dedicated cache's own statistics, not assumed (§28.4)
    local hits
    hits=$(CCACHE_DIR="$cache" ccache -s 2>/dev/null | grep -E "^  Hits:" | tr -s ' ' | cut -d' ' -f3)

    {
        echo "RESULT $name status=$status wall_s=$wall cpu_s=$cpu ccache_hits=${hits:-0}"
        echo "  packages installed:"
        # vcpkg's status file lists what it actually installed, which is the dependency count (§28.2)
        if [ -f "$install/vcpkg/status" ]; then
            grep -c "^Package:" "$install/vcpkg/status" | sed 's/^/    /'
            grep "^Package:" "$install/vcpkg/status" | sed 's/Package: /    /' | head -40
        fi
        echo "  ccache stats after:"
        CCACHE_DIR="$cache" ccache -s 2>/dev/null | sed 's/^/  /'
    } >>"$log"

    echo "$name: status=$status wall=${wall}s cpu=${cpu}s ccache_hits=${hits:-0}"
    return $status
}

echo "=== W1: cold skia[graphite,metal] ==="
run_workload w1_skia_cold "$SKIA_SPEC" skia-cold cold

echo "=== W2: warm skia rebuild from W1's state ==="
run_workload w2_skia_warm "$SKIA_SPEC" skia-cold warm

echo "=== W3: cold dawn[metal] (the honest comparator) ==="
run_workload w3_dawn_cold "$DAWN_SPEC" dawn-cold cold

echo "raw logs in $OUT"
