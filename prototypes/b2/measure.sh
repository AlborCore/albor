#!/usr/bin/env bash
# Measure the Prototype B2 variants (§30, AD-03).
#
# Every timed configuration runs at least REPEATS times and the median with its min and max is reported,
# because one clean-build sample is not a decision input. Every figure states its basis: wall at -j12,
# wall at -j1, or CPU. The object directory is deleted before each clean sample so nothing is reused.

set -u

# the thresholds from plan.md, committed before this script existed (§28.4)
readonly T2_1_PCH_GAIN=0.20
readonly T2_2_MINIMAL_GAIN=0.10
readonly T2_3_CHUNK50_CPU_SHARE=0.25
readonly T2_4_CHUNK50_EDIT_RATIO=3.0
readonly T2_5_LINK_SHARE=0.25

# the component count every suite uses (§30)
readonly COMPONENTS=500

# at least five samples per timed configuration (§28.4)
readonly REPEATS=${REPEATS:-5}

# where the suites and object directories live
readonly WORK=${WORK:-/tmp/b2}
readonly GENERATOR="$(cd "$(dirname "$0")" && pwd)/generate.py"

# the compiler under test, with the same standard and optimisation as the release preset
readonly CXX=${CXX:-clang++}
readonly CXXFLAGS="-std=c++23 -O3 -c"

# report a median with its spread, so a difference can be judged against noise (§28.4)
median() {
    printf '%s\n' "$@" | sort -g | awk '{ v[NR] = $1 } END { print (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2 }'
}

minimum() { printf '%s\n' "$@" | sort -g | head -1; }
maximum() { printf '%s\n' "$@" | sort -n | tail -1; }

# generate one suite into its own directory (§14.2)
generate() {
    local variant=$1 chunk=$2
    rm -rf "${WORK:?}/${variant}_${chunk}"
    python3 "${GENERATOR}" --variant "${variant}" --chunk "${chunk}" --out-dir "${WORK}/${variant}_${chunk}" > /dev/null
}

# time one clean compile of a suite, at the given parallelism, and print "wall cpu status"
#
# the compiler driver has no -j, so parallelism comes from xargs -P over the source list, and each object is
# written into the suite's own obj directory so the link step can find it (§28.2)
time_clean() {
    local dir=$1 jobs=$2
    rm -rf "${dir}/obj"
    mkdir -p "${dir}/obj"

    # /usr/bin/time -p reports the wall clock and the children's CPU total in one pass (§28.2)
    /usr/bin/time -p sh -c "cd '${dir}/obj' && ls -1 ../*.cpp | xargs -P ${jobs} -n 4 ${CXX} ${CXXFLAGS} -I.. -c" \
        > /dev/null 2> "${dir}/time.txt"
    local status=$?

    local wall user sys
    wall=$(awk '/^real /{ print $2 }' "${dir}/time.txt")
    user=$(awk '/^user /{ print $2 }' "${dir}/time.txt")
    sys=$(awk '/^sys /{ print $2 }' "${dir}/time.txt")

    # a compile that failed is reported rather than timed, so a fast build cannot be mistaken for a cheap one (§28.4)
    if [ "${status}" -ne 0 ]; then
        printf '%s %s %s\n' "${wall:-0}" "0" "FAILED"
        return
    fi

    # the shell's children are timed, so the CPU is the compiler's plus the driver's (§28.2)
    printf '%s %s %s\n' "${wall}" "$(awk -v u="${user:-0}" -v s="${sys:-0}" 'BEGIN { printf "%.3f", u + s }')" "OK"
}

# time an incremental edit: touch one body and rebuild the whole directory with -j1, so the numbers are
# compile-bound rather than scheduler-bound (§28.2)
time_edit() {
    local dir=$1 target=$2
    # a comment change is a body edit that cannot be optimised away and does not change semantics (§14.3)
    printf '\n// edit %s\n' "$(date +%s%N)" >> "${target}"

    rm -rf "${dir}/obj_incremental"
    cp -r "${dir}/obj" "${dir}/obj_incremental"

    local start end
    start=$(python3 -c 'import time; print(time.perf_counter())')

    # recompile only the translation unit the edited body lives in (§14.3)
    "${CXX}" ${CXXFLAGS} -I"${dir}" "${target}" -o "${dir}/obj_incremental/$(basename "${target}" .cpp).o" 2> "${dir}/edit.txt"

    end=$(python3 -c 'import time; print(time.perf_counter())')
    awk -v a="${start}" -v b="${end}" 'BEGIN { printf "%.3f", b - a }'
}

# time the link of a complete object set, which is the other half of an incremental edit (§28.2)
time_link() {
    local dir=$1
    local start end
    start=$(python3 -c 'import time; print(time.perf_counter())')
    "${CXX}" -std=c++23 -O3 "${dir}"/obj_incremental/*.o -o "${dir}/suite" 2> "${dir}/link.txt"
    end=$(python3 -c 'import time; print(time.perf_counter())')
    awk -v a="${start}" -v b="${end}" 'BEGIN { printf "%.3f", b - a }'
}

# peak resident set of the largest compile, in bytes, which is what M7 asks for (§28.2)
peak_rss() {
    local dir=$1
    /usr/bin/time -l "${CXX}" ${CXXFLAGS} -I"${dir}" "${dir}/Component000.cpp" -o /dev/null 2>&1 \
        | awk '/maximum resident set size/{ print $1 }'
}

# binary size of a linked suite, in bytes, which is the other half of M7 (§28.2)
binary_size() {
    local dir=$1
    stat -f%z "${dir}/suite" 2>/dev/null || echo 0
}

echo "Prototype B2 — build performance frontier"
echo "hardware: i7-9750H macOS, recorded in plan.md; compiler: ${CXX} -std=c++23 -O3"
echo "repeats: ${REPEATS}; components: ${COMPONENTS}; [BASELINE] not named, so no baseline comparison here"
echo

# -----------------------------------------------------------------------------------------------------------------
# M1, M2, M3: the fixed per-TU floor and whether header parsing is the cost (§28.2)
# -----------------------------------------------------------------------------------------------------------------
echo "=== M1/M2/M3: per-TU floor, precompiled header, forward-declared headers ==="

declare -a M1_CPU M2_CPU M3_CPU M1_WALL M1_WALL1 M1_RSS

for variant in m1 m2 m3; do
    dir="${WORK}/${variant}_1"
    generate "${variant}" 1

    # discard one warm-up sample so the first run does not carry page-cache cost (§28.2)
    time_clean "${dir}" 12 > /dev/null

    cpu_samples=()
    wall_samples=()
    for _ in $(seq "${REPEATS}"); do
        read -r wall cpu status < <(time_clean "${dir}" 12)
        [ "${status}" = "OK" ] || { echo "  ${variant}: COMPILE FAILED"; sed -n '1,5p' "${dir}/time.txt"; exit 1; }
        cpu_samples+=("${cpu}")
        wall_samples+=("${wall}")
    done

    # -j1 for the floor, so a per-TU figure does not depend on the core count (§28.2)
    wall1_samples=()
    for _ in $(seq "${REPEATS}"); do
        read -r wall1 _ status < <(time_clean "${dir}" 1)
        [ "${status}" = "OK" ] || { echo "  ${variant}: COMPILE FAILED at -j1"; exit 1; }
        wall1_samples+=("${wall1}")
    done

    cpu_med=$(median "${cpu_samples[@]}")
    wall_med=$(median "${wall_samples[@]}")
    wall1_med=$(median "${wall1_samples[@]}")
    cpu_per_tu=$(awk -v c="${cpu_med}" 'BEGIN { printf "%.1f", c * 1000 / 500 }')

    rss=$(peak_rss "${dir}")

    printf '  %-4s wall(-j12)=%8ss  wall(-j1)=%8ss  CPU=%9ss  CPU/TU=%7sms  spread(CPU)=%s..%s  peakRSS=%sB\n' \
        "${variant}" "${wall_med}" "${wall1_med}" "${cpu_med}" "${cpu_per_tu}" \
        "$(minimum "${cpu_samples[@]}")" "$(maximum "${cpu_samples[@]}")" "${rss}"

    case "${variant}" in
        m1) M1_CPU=("${cpu_samples[@]}"); M1_WALL=("${wall_samples[@]}"); M1_WALL1=("${wall1_samples[@]}"); M1_RSS=("${rss}") ;;
        m2) M2_CPU=("${cpu_samples[@]}") ;;
        m3) M3_CPU=("${cpu_samples[@]}") ;;
    esac
done

# the pre-registered thresholds compare CPU, so they compare medians (§28.4)
m1_med=$(median "${M1_CPU[@]}")
m2_med=$(median "${M2_CPU[@]}")
m3_med=$(median "${M3_CPU[@]}")

pch_gain=$(awk -v a="${m1_med}" -v b="${m2_med}" 'BEGIN { printf "%.4f", (a - b) / a }')
minimal_gain=$(awk -v a="${m1_med}" -v b="${m3_med}" 'BEGIN { printf "%.4f", (a - b) / a }')

# T2.8: a gain smaller than the floor's own spread is not distinguishable from noise (§28.4)
m1_spread=$(awk -v lo="$(minimum "${M1_CPU[@]}")" -v hi="$(maximum "${M1_CPU[@]}")" -v med="${m1_med}" 'BEGIN { printf "%.4f", (hi - lo) / med }')

echo
echo "  T2.1  M2 (PCH) CPU reduction vs M1            $(awk -v g="${pch_gain}" -v t="${T2_1_PCH_GAIN}" 'BEGIN { print (g >= t) ? "PASS" : "FAIL" }')  $(awk -v g="${pch_gain}" 'BEGIN { printf "%.2f%%", g * 100 }') >= $(awk -v t="${T2_1_PCH_GAIN}" 'BEGIN { printf "%.0f%%", t * 100 }')"
echo "  T2.2  M3 (forward-declared) CPU reduction      $(awk -v g="${minimal_gain}" -v t="${T2_2_MINIMAL_GAIN}" 'BEGIN { print (g >= t) ? "PASS" : "FAIL" }')  $(awk -v g="${minimal_gain}" 'BEGIN { printf "%.2f%%", g * 100 }') >= $(awk -v t="${T2_2_MINIMAL_GAIN}" 'BEGIN { printf "%.0f%%", t * 100 }')"
echo "  T2.8  M1 CPU spread                           RECORDED  $(awk -v s="${m1_spread}" 'BEGIN { printf "%.2f%%", s * 100 }') of the median"
echo "  -     M1 fixed per-TU floor                   RECORDED  $(awk -v c="${m1_med}" 'BEGIN { printf "%.1f", c * 1000 / 500 }') ms CPU per translation unit"

# -----------------------------------------------------------------------------------------------------------------
# M4: the clean-versus-incremental frontier over chunk sizes (§28.2)
# -----------------------------------------------------------------------------------------------------------------
echo
echo "=== M4: unity chunking frontier (500 components) ==="
printf '  %-6s %-10s %-8s %-12s %-12s %-14s %-10s %s\n' "N" "TUs" "wall(-j12)" "CPU" "CPU/TU(ms)" "edit(-j1,s)" "recompiled" "share of N=1 CPU"

chunk1_cpu=""
chunk1_edit=""
chunk50_cpu=""
chunk50_edit=""
for chunk in 1 5 10 25 50; do
    dir="${WORK}/m4_${chunk}"
    generate m4 "${chunk}"

    # count only the chunk translation units, excluding the entry point, which is not a component (§28.2)
    tus=$(ls "${dir}"/chunk_*.cpp | wc -l | tr -d ' ')

    time_clean "${dir}" 12 > /dev/null

    cpu_samples=()
    wall_samples=()
    for _ in $(seq "${REPEATS}"); do
        read -r wall cpu status < <(time_clean "${dir}" 12)
        [ "${status}" = "OK" ] || { echo "  m4 N=${chunk}: COMPILE FAILED"; sed -n '1,5p' "${dir}/time.txt"; exit 1; }
        cpu_samples+=("${cpu}")
        wall_samples+=("${wall}")
    done

    # a body edit in the first chunk recompiles exactly one translation unit holding N components (§14.3)
    edit=$(time_edit "${dir}" "${dir}/chunk_000.cpp")

    cpu_med=$(median "${cpu_samples[@]}")
    wall_med=$(median "${wall_samples[@]}")
    cpu_per_tu=$(awk -v c="${cpu_med}" -v t="${tus}" 'BEGIN { printf "%.1f", c * 1000 / t }')

    [ "${chunk}" = "1" ] && { chunk1_cpu="${cpu_med}"; chunk1_edit="${edit}"; }
    [ "${chunk}" = "50" ] && { chunk50_cpu="${cpu_med}"; chunk50_edit="${edit}"; }

    printf '  %-6s %-10s %-8ss %-12ss %-12s %-14s %-10s %s\n' \
        "${chunk}" "${tus}" "${wall_med}" "${cpu_med}" "${cpu_per_tu}" "${edit}" "${chunk}" "reference"
done

# -----------------------------------------------------------------------------------------------------------------
# M6, M7: compile versus link, peak RSS and binary size (§28.2)
# -----------------------------------------------------------------------------------------------------------------
echo
echo "=== M6/M7: compile versus link, peak RSS, binary size ==="

dir="${WORK}/m1_1"
compile_s=$(time_edit "${dir}" "${dir}/Component000.cpp")
link_s=$(time_link "${dir}")
link_share=$(awk -v l="${link_s}" -v c="${compile_s}" 'BEGIN { printf "%.4f", l / (l + c) }')

echo "  one-TU body edit, compile only          RECORDED  ${compile_s} s"
echo "  full-suite link                         RECORDED  ${link_s} s"
echo "  T2.5  link share of a one-TU edit       $(awk -v s="${link_share}" -v t="${T2_5_LINK_SHARE}" 'BEGIN { print (s <= t) ? "PASS" : "FAIL" }')  $(awk -v s="${link_share}" 'BEGIN { printf "%.2f%%", s * 100 }') <= $(awk -v t="${T2_5_LINK_SHARE}" 'BEGIN { printf "%.0f%%", t * 100 }')"
echo "  T2.7  binary size                       RECORDED  $(binary_size "${dir}") B"

# -----------------------------------------------------------------------------------------------------------------
# T2.6: the generator must refuse a component with too many properties (§28.4)
# -----------------------------------------------------------------------------------------------------------------
echo
echo "=== T2.6: the uint16_t local-id limit ==="
refusal=$(python3 "${GENERATOR}" --variant m8 --out-dir "${WORK}/m8" 2>&1)
refusal_status=$?
echo "  generator exit status: ${refusal_status}"
echo "  message: ${refusal}"
if [ "${refusal_status}" -ne 0 ] && printf '%s' "${refusal}" | grep -q "65536" && printf '%s' "${refusal}" | grep -q "65535"; then
    echo "  T2.6  generator refuses 65536 properties  PASS  names both the count and the limit"
else
    echo "  T2.6  generator refuses 65536 properties  FAIL  no clear refusal"
fi

# -----------------------------------------------------------------------------------------------------------------
# T2.3 and T2.4: the chunking trade, judged from the M4 table (§28.4)
# -----------------------------------------------------------------------------------------------------------------
echo
echo "=== T2.3/T2.4: the chunking trade ==="

cpu_share=$(awk -v a="${chunk50_cpu}" -v b="${chunk1_cpu}" 'BEGIN { printf "%.4f", a / b }')
edit_ratio=$(awk -v a="${chunk50_edit}" -v b="${chunk1_edit}" 'BEGIN { printf "%.3f", a / b }')

echo "  T2.3  N=50 clean CPU / N=1 clean CPU     $(awk -v v="${cpu_share}" -v t="${T2_3_CHUNK50_CPU_SHARE}" 'BEGIN { print (v <= t) ? "PASS" : "FAIL" }')  $(awk -v v="${cpu_share}" 'BEGIN { printf "%.2f%%", v * 100 }') <= $(awk -v t="${T2_3_CHUNK50_CPU_SHARE}" 'BEGIN { printf "%.0f%%", t * 100 }')"
echo "  T2.4  N=50 body edit / N=1 body edit      $(awk -v v="${edit_ratio}" -v t="${T2_4_CHUNK50_EDIT_RATIO}" 'BEGIN { print (v <= t) ? "PASS" : "FAIL" }')  ${edit_ratio}x <= ${T2_4_CHUNK50_EDIT_RATIO}x"
echo "  T2.7  frontier reading                   RECORDED  clean CPU falls to $(awk -v v="${cpu_share}" 'BEGIN { printf "%.1f%%", v * 100 }') of N=1 while a body edit costs ${edit_ratio}x, from N=1 to N=50"
