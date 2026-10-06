#!/bin/bash
# Repeat the graphite+dawn measurement and keep every run, because one run on this machine is not a measurement.
#
# A1.3 already recorded that the same binary produced W2 recording p50 of 5.92, 6.16, 47.13 and 47.48 ms across four
# runs, and A1-Build-Cost recorded a 52% spread between two runs of an identical cold build. A single figure from a
# single process therefore carries no information about its own uncertainty, which is exactly the mistake this script
# exists to prevent.
#
# Runs are strictly sequential. Two instances share the cpu and the single gpu and every timing either reports is
# then meaningless; the driver also refuses to start while another holds its lock file, and this script checks that
# no instance is running before each pass.
#
# usage: bash prototypes/a1_graphite_dawn/run_repeated.sh [passes]

set -u

passes="${1:-3}"
binary=".build-debug/prototypes/a1_graphite_dawn/albor-prototype-a1-graphite-dawn-bench"
raw="/tmp/gd_pass"

if [ ! -x "$binary" ]; then
    echo "error: $binary is missing; build the debug preset first" >&2
    exit 1
fi

# load is sampled before and after every pass, so each number carries the machine state it was taken under
load_now() { sysctl -n vm.loadavg | tr -d '{}' | awk '{print $1", "$2", "$3}'; }

echo "=== repeated graphite+dawn measurement, $passes sequential passes ==="
echo "binary: $binary"
echo "start load: $(load_now)"

for pass in $(seq 1 "$passes"); do
    running="$(pgrep -f 'albor-prototype-a1-graphite-dawn-bench$' | wc -l | tr -d ' ')"
    if [ "$running" != "0" ]; then
        echo "error: $running instance(s) already running; refusing to overlap passes" >&2
        exit 1
    fi

    echo ""
    echo "--- pass $pass of $passes, load before: $(load_now) ---"
    script -q "${raw}${pass}.txt" "$binary" >/dev/null 2>&1
    status=$?
    echo "--- pass $pass finished with status $status, load after: $(load_now) ---"
done

echo ""
echo "=== done. per-pass figures ==="
for pass in $(seq 1 "$passes"); do
    echo ""
    echo "pass $pass:"
    grep -v '^\[skia\]' "${raw}${pass}.txt" | sed -n '/part D/,/part E/p' | grep -E '^  (w1|w2|  workload)'
done
