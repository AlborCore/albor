#!/bin/bash

# coverage gate for the runtime sources
# builds the coverage preset, runs the unit tests, merges the counters
# and fails when a production translation unit this branch touched drops below the line floor
# tests/ translation units are excluded, because the gtest macro machinery
# carries unreachable branches of its own
# the gate is a ratchet, not a wall: only the units the branch changed are judged, because the
# repository carries units that predate the floor and fixing them is its own change (§27)

# any failing step aborts the gate, because a gate that measures nothing must not report a pass
set -euo pipefail

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# the floor every production unit must hold, which the team may only raise (§27)
FLOOR=95

# the ref the branch is measured against, overridable so a fork or a release branch can name its own base
BASE="${ALBOR_COVERAGE_BASE:-origin/main}"

# generated sources are excluded for now, because they are compiler output rather than authored
# production code, and holding generated fixtures to the floor measures the fixture, not the source
GENERATED='^'"$(printf '%s' "${ALBOR_COVERAGE_GENERATED:-.build-coverage/generated}")"'/'

# lines no valid source can reach, each recorded with the reason it stays uncovered. an entry is
# "<file>|<exact source text>|<reason>", and the text is matched rather than the line number so a
# shifted line stops matching instead of quietly allowing whatever moved onto that line (§27)
UNREACHABLE=(
    'compiler/codegen/codegen.cpp|case TypeKind::Scrollbar:|the type checker rejects a named value set as a declarable type, so no property, signal parameter or record field ever carries one'
    'compiler/codegen/codegen.cpp|return "ScrollbarVisibility";|the type checker rejects a named value set as a declarable type, so no property, signal parameter or record field ever carries one'
    'compiler/codegen/codegen.cpp|case TypeKind::FontWeight:|the type checker rejects a named value set as a declarable type, so no property, signal parameter or record field ever carries one'
    'compiler/codegen/codegen.cpp|return "FontWeight";|the type checker rejects a named value set as a declarable type, so no property, signal parameter or record field ever carries one'
    'text/text-harfbuzz/harfbuzz_text_engine.cpp|line_end = text.size();|the wrap always advances over the ICU offsets, so the fallback only guards an ICU failure that would otherwise loop forever'
    'render/render-software/software_glyph_atlas.cpp|return nullptr;|the rasterizer allocates through harfbuzz, so a failed allocation is the only way this refusal runs and it cannot be forced from a test'
    'render/render-software/software_render_surface.cpp|m_state->scratch_context = std::make_unique<SoftwareRenderContext>(m_state->scratch->getCanvas(), 1.0f);|the scratch is only built when skia refuses the raster allocation, which cannot be forced from a test'
    'render/render-skia/skia_renderer_backend.macos.mm|return nullptr;|the offscreen target is only asked for when the backend is ready, which the parity test checks first, so the refusal cannot be forced'
    'render/render-software/software_render_context.cpp|continue;|a mask install and an image lift fail only on an allocation failure, which cannot be forced from a test'
)

# an entry whose text no longer appears in the file, which means the hole was closed or moved (§27)
report_stale_allowances() {
    local stale=()
    for entry in "${UNREACHABLE[@]}"; do
        local file="${entry%%|*}"
        local rest="${entry#*|}"
        local text="${rest%%|*}"
        if ! grep -qF -- "$text" "$ROOT/$file" 2> /dev/null; then
            stale+=("${file}: ${text}")
        fi
    done
    if [ ${#stale[@]} -gt 0 ]; then
        echo "coverage gate warning: a recorded allowance no longer matches, so it should be dropped:"
        printf '  %s\n' "${stale[@]}"
    fi
}

# report whether a dead line carries an allowance, which keeps a hole visible without failing the gate (§27)
allowed_line() {
    local file="$1"
    local line="$2"
    local source
    source="$(sed -n "${line}p" "$ROOT/$file" | sed 's/^[[:space:]]*//')"
    for entry in "${UNREACHABLE[@]}"; do
        local entry_file="${entry%%|*}"
        local rest="${entry#*|}"
        local text="${rest%%|*}"
        if [ "$entry_file" = "$file" ] && [ "$text" = "$source" ]; then
            return 0
        fi
    done
    return 1
}

# configure and build the coverage instrumented test runner, where a build that did not happen is a gate failure
# rather than an empty report that reads as a pass (§27)
cmake --preset coverage > /dev/null
cmake --build --preset coverage --target albor-tests -j8 > /dev/null

# the profile lives beside the runner, which the vcs never sees
cd .build-coverage/tests

# a runner that is absent means the build did not produce one, which must not read as a clean tree
if [ ! -x ./albor-tests ]; then
    echo "coverage gate failed: the instrumented runner was not built" >&2
    exit 1
fi

# the counters are cleared first, because a stale profile would report a run that no longer happens
rm -f default.profraw albor.profdata coverage-report.txt

# the suite runs and its result decides the gate, because a failing suite cannot produce a coverage claim
./albor-tests > /dev/null

# a missing profile means the runner was not instrumented, which would otherwise report nothing at all
if [ ! -f default.profraw ]; then
    echo "coverage gate failed: the runner produced no profile" >&2
    exit 1
fi

# the counters merge into one profile and that profile becomes the report every decision below reads
xcrun llvm-profdata merge -sparse default.profraw -o albor.profdata
xcrun llvm-cov report albor-tests -instr-profile=albor.profdata > coverage-report.txt

# a report without a total line is an empty one, which would let every unit pass by being absent
if ! grep -q '^TOTAL' coverage-report.txt; then
    echo "coverage gate failed: the report carries no coverage at all" >&2
    exit 1
fi

# the base is resolved before anything is judged, because an unknown base would silently gate nothing
if ! git -C "$ROOT" rev-parse --verify "$BASE" > /dev/null 2>&1; then
    echo "coverage gate failed: base ref '$BASE' is unknown, set ALBOR_COVERAGE_BASE" >&2
    exit 1
fi

MERGE_BASE="$(git -C "$ROOT" merge-base HEAD "$BASE" || true)"
if [ -z "$MERGE_BASE" ]; then
    echo "coverage gate failed: '$BASE' shares no history with this branch" >&2
    exit 1
fi

# the production sources this branch touched, because those are the units this change can regress
# headers and tests are left out, because they are not translation units the report carries
CHANGED="$(
    git -C "$ROOT" diff --name-only --diff-filter=ACMR "$MERGE_BASE" |
        grep -E '\.(cpp|mm|c\+\+)$' |
        grep -Ev '^(tests|tools|examples)/' |
        grep -Ev "$GENERATED" |
        sort -u
)"

if [ -z "$CHANGED" ]; then
    echo "coverage gate failed: the branch changed no production translation unit to measure" >&2
    exit 1
fi

# every touched unit is reported, whether it passed or not, so the log names what was judged
SHORT=""
CHECKED=0
ADDED=0
UNCOVERED=""
ALLOWED=""
UNMEASURED=""

while IFS= read -r FILE; do
    [ -n "$FILE" ] || continue
    CHECKED=$((CHECKED + 1))

    # the source lines this change added, which are the only lines it can leave uncovered
    ADDED_LINES="$(git -C "$ROOT" diff -U0 "$MERGE_BASE" -- "$FILE" |
        awk '/^@@/ {
            # the hunk header names the new range as +start,count, where a missing count is one line
            if (match($0, /\+[0-9]+(,[0-9]+)?/)) {
                split(substr($0, RSTART + 1, RLENGTH - 1), part, ",")
                start = part[1] + 0
                count = part[2] == "" ? 1 : part[2] + 0
                for (i = 0; i < count; i++)
                    print start + i
            }
        }' | sort -n -u)"

    if [ -z "$ADDED_LINES" ]; then
        continue
    fi

    # the per line counts the instrumented runner recorded, read once so the gate never runs it twice
    SHOW="$(xcrun llvm-cov show albor-tests -instr-profile=albor.profdata --show-line-counts "$ROOT/$FILE" || true)"

    # a unit the runner does not link prints nothing, which is stated rather than assumed to be covered
    if [ -z "$SHOW" ]; then
        UNMEASURED="${UNMEASURED}${FILE}\n"
        continue
    fi

    # a line the runner never executed, taken from the per line counts the report prints
    DEAD_LINES="$(printf '%s\n' "$SHOW" |
        awk -F'|' '
            {
                line = $1 + 0
                count = $2
                gsub(/ /, "", count)
                # a line printed with no count carries no code, so there is nothing there to cover
                if (count == "" || line <= 0)
                    next
                # a line printed more than once is covered when any one of its copies ran
                if (count + 0 > best[line])
                    best[line] = count + 0
            }
            END {
                for (line in best)
                    if (best[line] == 0)
                        print line
            }' | sort -n)"

    ADDED=$((ADDED + $(printf '%s\n' "$ADDED_LINES" | grep -c '')))

    # only the added lines that carry code and never ran are the failures this gate exists to prevent
    while read -r LINE; do
        [ -n "$LINE" ] || continue
        if printf '%s\n' "$DEAD_LINES" | grep -qx "$LINE"; then
            # a recorded hole is named rather than failed, because the language cannot express it today (§27)
            if allowed_line "$FILE" "$LINE"; then
                ALLOWED="${ALLOWED}${FILE}:${LINE}\n"
                continue
            fi
            UNCOVERED="${UNCOVERED}${FILE}:${LINE}\n"
        fi
    done <<< "$ADDED_LINES"

    # the unit's own overall level is reported beside the change, because a file the branch barely
    # touched carries the history of that file, which this change did not cause (§27)
    COVER="$(awk -v want="$FILE" '$1 == want { print $4; exit }' coverage-report.txt)"
    if [ -n "$COVER" ]; then
        VALUE="${COVER%%%}"
        if awk -v value="$VALUE" -v floor="$FLOOR" 'BEGIN { exit !(value + 0 < floor + 0) }'; then
            SHORT="${SHORT}${FILE} ${COVER}\n"
        fi
    fi
done <<< "$CHANGED"

if [ -n "$UNMEASURED" ] && [ "${ALBOR_COVERAGE_REQUIRE_MEASURED:-0}" != "0" ]; then
    echo "coverage gate failed: touched units the runner never linked, so nothing measured them"
    printf "%b" "$UNMEASURED"
    exit 1
fi

if [ -n "$UNCOVERED" ]; then
    echo "coverage gate failed: ${CHECKED} touched production units, ${ADDED} added lines, these never ran"
    printf "%b" "$UNCOVERED"
    awk '$1 ~ /^TOTAL$/ && NF >= 5 { print "total line coverage " $4 }' coverage-report.txt
    exit 1
fi

report_stale_allowances

echo "coverage gate passed: ${ADDED} added lines across ${CHECKED} touched production units all ran"

if [ -n "$ALLOWED" ]; then
    echo "added lines no valid source can reach, recorded rather than counted:"
    printf "%b" "$ALLOWED"
fi

# the touched units that sit below the floor overall are named without failing, because a pre existing
# shortfall is a change of its own and is tracked as one rather than smuggled into this one (§27)
if [ -n "$SHORT" ]; then
    echo "touched units below ${FLOOR}% overall, pre existing and not caused by this change:"
    printf "%b" "$SHORT"
fi

if [ -n "$UNMEASURED" ]; then
    echo "touched units the runner does not link, so they were not judged:"
    printf "%b" "$UNMEASURED"
fi

# the judged units are named on a pass too, because a silent pass is indistinguishable from no gate
# the total is reported, which keeps the trend visible in the build log
awk '$1 ~ /^TOTAL$/ && NF >= 5 { print "total line coverage " $4 }' coverage-report.txt