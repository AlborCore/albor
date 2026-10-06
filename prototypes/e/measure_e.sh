#!/bin/bash
# measure the Prototype E thresholds recorded in plan.md
# the layout claim is a comparison, so both configurations are built and their output diffed

cd "$(dirname "$0")/../.." || exit 1

echo "=== building both configurations ==="
cmake --preset release > /dev/null 2>&1
cmake --preset debug > /dev/null 2>&1
cmake --build --preset release --target albor-prototype-e-layout albor-prototype-e-conformance -j12 > /dev/null 2>&1
cmake --build --preset debug --target albor-prototype-e-layout albor-prototype-e-conformance -j12 > /dev/null 2>&1

REL=.build-release/prototypes/e
DEV=.build-debug/prototypes/e

echo ""
echo "=== release layout_check ==="
"$REL/albor-prototype-e-layout"
echo "  exit=$?"

echo ""
echo "=== dev layout_check ==="
"$DEV/albor-prototype-e-layout"
echo "  exit=$?"

echo ""
echo "=== T1/T2, layout invariance: diff the two dumps, ignoring the hook line ==="
# the hook line is the one expected difference: the slot exists in both, and only Dev installs a body into it.
# comparing everything else is the actual layout claim
diff <("$REL/albor-prototype-e-layout" | grep -v '^dev_hook') <("$DEV/albor-prototype-e-layout" | grep -v '^dev_hook')
if [ $? -eq 0 ]; then
    echo "  T1/T2 PASS: sizes, field offsets and vtable dispatch identical across configurations"
else
    echo "  T1/T2 FAIL: layout differs between Release and Dev"
fi

rel_hook=$("$REL/albor-prototype-e-layout" | awk '/^dev_hook/{print $3}')
dev_hook=$("$DEV/albor-prototype-e-layout" | awk '/^dev_hook/{print $3}')
echo ""
echo "=== T7, extension hook attaches only in Dev ==="
echo "  release: $rel_hook, dev: $dev_hook"
if [ "$rel_hook" = "null" ] && [ "$dev_hook" = "non-null" ]; then
    echo "  T7 PASS: the hook slot is present in both, installed only in Dev"
else
    echo "  T7 FAIL: expected release=null and dev=non-null"
fi

echo ""
echo "=== T3/T4/T5, dual-engine conformance (release and dev) ==="
"$REL/albor-prototype-e-conformance"
echo "  release exit=$?"
"$DEV/albor-prototype-e-conformance"
echo "  dev exit=$?"

echo ""
echo "=== repeat conformance 5x, since plan.md asks for >= 5 runs ==="
for i in 1 2 3 4 5; do
    out=$("$REL/albor-prototype-e-conformance" 2>&1 | tr '\n' ' ')
    echo "  run $i: $out"
done