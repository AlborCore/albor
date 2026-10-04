#!/bin/bash
# measure the Prototype B thresholds recorded in plan.md
# every number comes from a real timed run; nothing is estimated

cd "$(dirname "$0")/../.." || exit 1
ROOT="$PWD"
BUILD="$ROOT/.build-release"
JOBS=12

clean_target() {
    # a clean build means no object files and no archive survive from the previous run, so the measured time is a
    # compile of every component rather than a link. the objects live under the library target's directory and the
    # driver's own directory, and both have to go or the driver links against a prebuilt library.
    local cfg="$1"
    find "$BUILD" -name '*.o' -path "*albor-prototype-b-generated*" -delete 2>/dev/null
    find "$BUILD" -name '*.o' -path "*albor-prototype-b-baseline*" -delete 2>/dev/null
    find "$BUILD" -name '*.o' -path "*bench-${cfg}*" -delete 2>/dev/null
    find "$BUILD" -name "libalbor-prototype-b-generated.a" -delete 2>/dev/null
    find "$BUILD" -name "libalbor-prototype-b-baseline.a" -delete 2>/dev/null
    rm -f "$BUILD/prototypes/b/albor-prototype-b-bench-$cfg" 2>/dev/null
    # report what survived, so a run that failed to clean cannot be mistaken for a fast clean build
    local left
    left=$(find "$BUILD" -name '*.o' -path "*albor-prototype-b-generated*" | wc -l | tr -d ' ')
    if [ "$left" != "0" ]; then
        echo "  warning: $left generated objects survived the clean"
    fi
}

echo "=== T1, clean build time, 5 runs per configuration ==="
for cfg in generated baseline; do
    target="albor-prototype-b-bench-$cfg"
    times=()
    for run in 1 2 3 4 5; do
        clean_target "$target"
        start=$(python3 -c 'import time; print(time.perf_counter())')
        cmake --build "$BUILD" --target "$target" -j"$JOBS" > /dev/null 2>&1
        rc=$?
        end=$(python3 -c 'import time; print(time.perf_counter())')
        if [ $rc -ne 0 ]; then
            echo "  $cfg run $run: BUILD FAILED"
            continue
        fi
        elapsed=$(python3 -c "print(f'{$end - $start:.3f}')")
        times+=("$elapsed")
        echo "  $cfg run $run: ${elapsed}s"
    done
    python3 - "$cfg" "${times[@]}" <<'PY'
import sys, statistics
cfg = sys.argv[1]
vals = sorted(float(v) for v in sys.argv[2:])
if not vals:
    print(f"  {cfg}: no successful runs")
    sys.exit(0)
print(f"  {cfg}: median {statistics.median(vals):.3f}s  min {vals[0]:.3f}s  max {vals[-1]:.3f}s  n={len(vals)}")
PY
done