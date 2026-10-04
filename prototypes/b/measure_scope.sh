#!/bin/bash
# measure the rebuild scope thresholds T2, T3, T4, T7 from plan.md
# each edit is made by appending a comment, so the content really changes and the scope really is measured

cd "$(dirname "$0")/../.." || exit 1
BUILD=".build-release"
GEN="prototypes/b/generated_suite"

count_rebuilt() {
    # count the object files make actually recompiled, by timestamp newer than the mark
    find "$BUILD" -name '*.o' -newer "$1" 2>/dev/null | wc -l | tr -d ' '
}

full_build() {
    cmake --build "$BUILD" --target albor-prototype-b-bench-generated -j12 > /dev/null 2>&1
}

echo "=== establishing a fully built state ==="
full_build
echo "  built: $(find "$BUILD" -name '*.o' -path '*albor-prototype-b-generated*' | wc -l | tr -d ' ') component objects"

# T2/T3: a body edit inside one component's .cpp
echo ""
echo "=== T2/T3, local .ui body edit (one component's .cpp) ==="
touch /tmp/b_mark
printf '\n// body edit for the rebuild scope measurement\n' >> "$GEN/Component250.cpp"
TIMEFORMAT='%3R'
{ time cmake --build "$BUILD" --target albor-prototype-b-bench-generated -j12 > /dev/null 2>&1; } 2>/tmp/b_body_time
rebuilt=$(count_rebuilt /tmp/b_mark)
elapsed=$(cat /tmp/b_body_time)
echo "  TUs rebuilt: $rebuilt (threshold: exactly 1)"
echo "  wall time: ${elapsed}s (threshold: <= 1.5s)"
python3 prototypes/b/generate.py --style generated --out-dir "$GEN" > /dev/null

# T4: an interface edit in one component's header
echo ""
echo "=== T4, interface edit (one component's .h) ==="
full_build
touch /tmp/b_mark
printf '\n// interface edit for the rebuild scope measurement\n' >> "$GEN/Component250.h"
TIMEFORMAT='%3R'
{ time cmake --build "$BUILD" --target albor-prototype-b-bench-generated -j12 > /dev/null 2>&1; } 2>/tmp/b_iface_time
rebuilt=$(count_rebuilt /tmp/b_mark)
elapsed=$(cat /tmp/b_iface_time)
echo "  TUs rebuilt: $rebuilt"
echo "  wall time: ${elapsed}s"
echo "  rebuilt list (component objects only):"
find "$BUILD" -name '*.o' -newer /tmp/b_mark -path '*albor-prototype-b-generated*' 2>/dev/null | sed 's|.*/||' | sort | head -20
python3 prototypes/b/generate.py --style generated --out-dir "$GEN" > /dev/null

# T7: an unrelated edit, the application entry point only
echo ""
echo "=== T7, unrelated edit (main driver only) ==="
# the driver is hand-written rather than generated, so it cannot be restored by re-running the generator
cp prototypes/b/bench_generated.cpp /tmp/b_bench_gen
full_build
touch /tmp/b_mark
printf '\n// unrelated edit for the rebuild scope measurement\n' >> prototypes/b/bench_generated.cpp
cmake --build "$BUILD" --target albor-prototype-b-bench-generated -j12 > /dev/null 2>&1
rebuilt=$(count_rebuilt /tmp/b_mark)
echo "  TUs rebuilt: $rebuilt (threshold: 1, only main.cpp)"
find "$BUILD" -name '*.o' -newer /tmp/b_mark 2>/dev/null | sed 's|.*/||' | sort | head -10
cp /tmp/b_bench_gen prototypes/b/bench_generated.cpp

echo ""
echo "=== T5, write-if-changed: re-run the generator twice, no .ui change ==="
# the digest is taken after one throwaway run, so the measurement starts from a settled state rather than from
# whatever the previous build left behind
python3 prototypes/b/generate.py --style generated --out-dir "$GEN" > /dev/null
python3 prototypes/b/generate.py --style baseline --out-dir prototypes/b/baseline > /dev/null

sum_before=$(find prototypes/b/generated_suite prototypes/b/baseline \( -name '*.h' -o -name '*.cpp' \) -exec stat -f '%m' {} \; | sort | md5)
gen_out=$(python3 prototypes/b/generate.py --style generated --out-dir "$GEN")
base_out=$(python3 prototypes/b/generate.py --style baseline --out-dir prototypes/b/baseline)
sum_after=$(find prototypes/b/generated_suite prototypes/b/baseline \( -name '*.h' -o -name '*.cpp' \) -exec stat -f '%m' {} \; | sort | md5)

echo "  generator reports: generated [$gen_out], baseline [$base_out]"
if [ "$sum_before" = "$sum_after" ]; then
    echo "  T5 PASS: 0 files restamped, timestamp digest unchanged"
else
    echo "  T5 FAIL: timestamps changed on a re-run with no source change"
fi

echo ""
echo "=== T6, PropertyId isolation: change a property in component 0 only ==="
full_build
# a property value that no other component could depend on: if the ids were global and monomorphic, editing one
# component's declaration would change every later component's ids and force their recompilation
sed -i '' 's/kPropColor = 0;/kPropColor = 9;/' "$GEN/Component000.h"
touch /tmp/b6
cmake --build "$BUILD" --target albor-prototype-b-bench-generated -j12 > /dev/null 2>&1
echo "  rebuilt after editing component 0's property:"
find "$BUILD" -name '*.o' -newer /tmp/b6 2>/dev/null | sed 's|.*/||' | sort | head -10
last=$(find "$BUILD" -name 'Component499.cpp.o' -newer /tmp/b6 2>/dev/null | wc -l | tr -d ' ')
echo "  component 499 rebuilt: $last (threshold: not rebuilt)"
if [ "$last" = "0" ]; then
    echo "  T6 PASS: component 499's object was not rebuilt"
else
    echo "  T6 FAIL: component 499 was rebuilt"
fi
python3 prototypes/b/generate.py --style generated --out-dir "$GEN" > /dev/null
