#!/bin/bash
# Pacing triage driver (§28.2, plan.md T3.7).
#
# Runs the mechanism x condition matrix under the launches the plan automates. App
# activation and hiding are sticky, so each condition runs in its own process; the
# mechanisms are compared inside one process because the mechanism is the variable
# under test (plan.md). Thresholds are evaluated separately by evaluate.py so that
# the evidence and the verdict stay separable.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

RUNS="${RUNS:-5}"
SECONDS_PER_RUN="${SECONDS_PER_RUN:-5}"
OUT="${OUT:-prototypes/a2_macos/raw}"
BUILD_DIR=".build-release/prototypes/a2_macos"
BINARY="albor-prototype-a2-pacing"
APP="$BUILD_DIR/AlborPacing.app"
PLIST="$APP/Contents/Info.plist"

mkdir -p "$OUT"

echo "building $BINARY"
if ! cmake --build --preset release --target "$BINARY" -j12 >"$OUT/build.log" 2>&1; then
    echo "build failed; see $OUT/build.log"
    tail -30 "$OUT/build.log"
    exit 1
fi

if [ ! -x "$BUILD_DIR/$BINARY" ]; then
    echo "binary not found at $BUILD_DIR/$BINARY"
    exit 1
fi

echo "building the condition C3 bundle"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp "$BUILD_DIR/$BINARY" "$APP/Contents/MacOS/$BINARY"
cat >"$PLIST" <<'PLIST_EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>albor-prototype-a2-pacing</string>
    <key>CFBundleIdentifier</key>
    <string>org.albor.prototype.pacing</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>AlborPacing</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>0.1</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST_EOF

# which caffeinate the run would use, so "display kept awake" is a recorded fact (T3.7)
CAFFEINATE="$(command -v caffeinate || true)"

run_case() {
    local name="$1"
    local description="$2"
    shift 2

    local log="$OUT/$name.log"
    : >"$log"

    {
        echo "CASE $name: $description"
        echo "DRIVER runs=$RUNS seconds=$SECONDS_PER_RUN"
        echo "DRIVER caffeinate=${CAFFEINATE:-absent}"
        echo "DRIVER launched=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "DRIVER conditions_needing_a_human=C2_unlocked_session C7_gpu_pinning C6_external_display"
    } >>"$log"

    "$@" >>"$log" 2>&1
    local status=$?

    # T3.7: the exit status of the launch, recorded next to the run it belongs to
    echo "DRIVER caffeinate_status=$status" >>"$log"
    echo "CASE $name finished with status $status; log at $log"
}

# case 1: nothing changed. no bundle, no caffeinate, no activation (§28.2)
run_case asfound_raw "unbundled binary, no caffeinate, no explicit activation" \
    "$BUILD_DIR/$BINARY" --matrix --condition as_found --runs "$RUNS" --seconds "$SECONDS_PER_RUN"

# case 2: C1 + C3 + C4. awake, bundled, attended — the load-bearing cells (T3.1..T3.3)
run_case bundled_caffeinated_attended "caffeinate -dimsu, bundled app, window ordered front and app activated" \
    caffeinate -dimsu "$APP/Contents/MacOS/$BINARY" --matrix --condition attended --runs "$RUNS" --seconds "$SECONDS_PER_RUN"

# case 3: C1 + C3 + C5. awake, bundled, window off screen and app hidden (§28.2)
run_case bundled_caffeinated_unattended "caffeinate -dimsu, bundled app, window off screen and app hidden" \
    caffeinate -dimsu "$APP/Contents/MacOS/$BINARY" --matrix --condition unattended --runs "$RUNS" --seconds "$SECONDS_PER_RUN"

# case 4: the same bundle without caffeinate, so C1's contribution is separable (§28.2)
run_case bundled_no_caffeinate_attended "bundled app, no caffeinate, attended" \
    "$APP/Contents/MacOS/$BINARY" --matrix --condition attended --runs "$RUNS" --seconds "$SECONDS_PER_RUN"

echo "raw runs in $OUT; thresholds are evaluated by evaluate.py"
