#!/bin/bash

# coverage gate for the runtime sources
# builds the coverage preset, runs the unit tests, merges the counters
# and fails when any production translation unit drops below the line floor
# tests/ translation units are excluded, because the gtest macro machinery
# carries unreachable branches of its own

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# the floor every production unit must hold, which the team may only raise (§27)
FLOOR=95

# configure and build the coverage instrumented test runner
cmake --preset coverage > /dev/null
cmake --build --preset coverage --target albor-tests -j8 > /dev/null

# the profile lives beside the runner, which the vcs never sees
cd .build-coverage/tests
rm -f default.profraw
./albor-tests > /dev/null
xcrun llvm-profdata merge -sparse default.profraw -o albor.profdata
xcrun llvm-cov report albor-tests -instr-profile=albor.profdata > coverage-report.txt

# every production unit below the floor is listed, which is what a reviewer reads
FAILURES=$(awk -v floor="$FLOOR" '
    $0 ~ /\.(cpp|c\+\+|mm)\.?[a-z]*[ \t]*[0-9]+[ \t]+[0-9]+[ \t]+[0-9]+\.[0-9]+%/ &&
    $1 !~ /^tests\// {
        # the line coverage is the third numeric field, which llvm-cov reports as N%
        for (i = 1; i <= NF; i++)
            if ($i ~ /^[0-9]+\.[0-9]%$/ || $i ~ /^[0-9]+\.[0-9]+%$/) {
                pct = $i
                sub("%", "", pct)
                if (pct + 0 < floor + 0)
                    print $0
                break
            }
    }' coverage-report.txt)

if [ -n "$FAILURES" ]; then
    echo "coverage gate failed: production units below ${FLOOR}% lines"
    echo "$FAILURES"
    exit 1
fi

# the total is reported, which keeps the trend visible in the build log
awk '$1 ~ /^TOTAL$/ && NF >= 5 { print "coverage gate passed: total line coverage " $4 }' coverage-report.txt
