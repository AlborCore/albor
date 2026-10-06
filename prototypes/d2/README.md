# Prototype D2 — Binding Engine Follow-up

Measurement code for **AD-04b** (property storage layout) and **AD-04c** (binding evaluation model), following
Prototype D. It exists because D left both open and because the structures §18 and §20 require — virtualized
delegates, `if`/`for` constructs, popups and live reload — need a mutable dependency graph that D never built.

This is a prototype, not a runtime module. Nothing under `core/`, `ui/`, `window/` or `render/` may link it
(AGENTS.md, §32).

## Contents

| Path | What it is |
| --- | --- |
| [`plan.md`](plan.md) | Hypotheses, workloads, thresholds and commands, committed before any run (§28.4) |
| [`RESULTS.md`](RESULTS.md) | Every measured number, every pass/fail, and the AD-04b/AD-04c recommendations |
| `src/value.*` | Value model covering every §13.1 category, including translated strings and snapshots |
| `src/schema.*` | Component schemas, inline layout, and the region table that maps component-local ids through base offsets |
| `src/storage.*` | The two storage candidates: typed per-component slab, and the type-erased slot |
| `src/index.*` | The two index designs: Prototype D's sealed CSR, and a mutable segmented index |
| `src/graph.*` | Binding table, the three evaluation models, mutation, `ReplaceBinding`, the animation layer |
| `src/cycles.h` | Kahn-based cycle detection over the binding graph |
| `src/workloads.h` | Graph builders: chains, diamonds, layered reconvergent graphs, and D's node shape |
| `benchmark.cpp` | The eight experiments, carrying the thresholds from `plan.md` as constants |
| `../../tests/d2/` | 52 regression tests, one per observed defect or edge case |

Tests live in `tests/d2/` rather than `prototypes/d2/tests/` because that is where the repository keeps unit tests
(`tests/CMakeLists.txt`, STYLE-GUIDE.md "Testing").

## Running

```sh
cmake --preset release
cmake --build --preset release

# correctness
./.build-release/tests/albor-tests --gtest_filter='D2*'

# measurements, one section at a time
./.build-release/prototypes/d2/albor-prototype-d2-benchmark            # all sections
./.build-release/prototypes/d2/albor-prototype-d2-benchmark storage    # 1. typed storage
./.build-release/prototypes/d2/albor-prototype-d2-benchmark chains      # 2. deep chains
./.build-release/prototypes/d2/albor-prototype-d2-benchmark diamonds   # 3. diamonds and reconvergence
./.build-release/prototypes/d2/albor-prototype-d2-benchmark mutation   # 4. runtime graph mutation
./.build-release/prototypes/d2/albor-prototype-d2-benchmark ids        # 5. component-local ids
./.build-release/prototypes/d2/albor-prototype-d2-benchmark replace    # 6. ReplaceBinding
./.build-release/prototypes/d2/albor-prototype-d2-benchmark cycles     # 7. cycle detection
./.build-release/prototypes/d2/albor-prototype-d2-benchmark animation  # 8. animation interaction
```

The benchmark exits non-zero if any pre-registered threshold failed, so a script can gate on it. Thresholds are
transcribed from `plan.md`; editing a constant in `benchmark.cpp` to change a verdict is a defect, not a fix.

## Reading the results

Three things in `RESULTS.md` are worth knowing before reading it:

1. **Prototype D is untouched.** Its numbers are quoted as the baseline they were declared as, and nothing in
   `prototypes/d/` was modified or re-run.
2. **Two thresholds failed and are reported as failures** (T1.5, T1.6), with the diagnosis for each and an explicit
   statement of why the benchmark was not changed to make them pass.
3. **One pass is not robust** (T4.5): two different statistics on the same comparison disagree, and `RESULTS.md` says
   which one the pre-registered threshold used and what the other gives.

## Limits

`RESULTS.md` lists six gaps. The two that most constrain the recommendations:

- **No `.ui` corpus.** The property mix behind both storage figures is a guess, so Prototype D's own open item about
  confirming the fan-out stays open.
- **Typed storage was not combined with the hybrid evaluation model.** §2–§8 ran on the type-erased store so their
  numbers stay comparable to D's recorded figures. The recommendation for AD-04b assumes the two are independent, and
  that assumption is untested.