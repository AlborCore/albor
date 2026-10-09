# Backlog

Work that is agreed but not done yet. Every entry names its fix precisely, so it can be
picked up without re-deciding anything.

## The vendored text engine places the baseline differently from the oracle

`text/text-harfbuzz/` shapes correctly and draws, and it agrees with the Skia oracle on glyph
count, every glyph position and measured width. It disagrees on line box geometry: Arial's first
baseline sits at 15.0 in the oracle and 18.105 in the vendored engine, so the two rendered pictures
differ completely. Full record in `DEFECT-TEXT-VENDOR-BASELINE.md`.

This is what blocks the rest of phase 3, because `text/text-skia/` may only be deleted once the two
engines produce the same picture.

**The decision is made, by AD-34 and section 15 of the specification.** Cross-platform pixel identity
is the objective, so the vendored engine defines the metrics and the oracle is compared against it.
Matching a macOS paragraph engine was never the requirement, because two other platforms cannot match
it either. The fix is to model ascent, descent and leading in the vendored line box.

The Metal teardown trap below also stops being a nice to have: it blocks AD-26, and AD-26 is now the
definition of correct rasterization for AD-34.

## The Metal backend traps when it is destroyed

`SkiaRendererBackend` cannot be destroyed. Constructing one and letting it leave scope
traps with `EXC_BAD_INSTRUCTION` inside the AMD driver, at
`MTLIOAccelCommandQueue dealloc` → `dispatch_release` → `_dispatch_semaphore_dispose`.
Until this is fixed, **no GPU frame can be tested**, so the AD-26 parity suite and real
screenshot verification cannot run. The CPU reference path is complete and tested
(`tests/headless.test.cpp`, `HeadlessPipelineChecks`).

Deferred deliberately, and revisited once the missing modules are implemented, because
the parity suite only becomes worth running once there is more than one surface's worth
of content to compare.

The full record, including the crash signature and everything already eliminated, is
`DEFECT-METAL-BACKEND-TEARDOWN.md`. Read that before changing anything: five ownership
hypotheses have been built and tested, and every one still traps. Specifically ruled out:

| Hypothesis | Test | Result |
| --- | --- | --- |
| a smart pointer is a second owner | plain pointer, explicit delete | still traps |
| `abandonContext()` triggers it | removed | still traps |
| the framework and Ganesh both own | Ganesh made sole owner | still traps |
| an over-release of the queue | leak the state | **does not trap** |
| Metal or Skia setup is at fault | standalone probe, same calls and order | exits 0 |

Two findings came out of that work and are worth keeping:

- **`sk_cfp` copy assignment retains.** `sk_cfp`'s `operator=` calls `SkCFSafeRetain`, so
  Ganesh is a second owner of the device and the queue, not a borrower. The comment in
  `skia_renderer_backend.macos.mm` claimed the opposite and has been corrected.
- **This is not an ownership counting problem.** The counts balance on paper and still
  trap, so the remaining suspect is that the queue is released while Ganesh still has work
  in flight, which makes it a shutdown ordering question.

**Next step, and the only one worth trying:** stop changing ownership and test whether a GPU
idle wait, or an explicit `GrDirectContext` teardown *before* the queue is released, avoids
the trap. Past that, a trap inside a vendor `dealloc` belongs in a platform issue against
the driver, which is where it ultimately sits.

Before restoring the parity suite, land a lifecycle test that constructs and destroys a
backend: a suite that traps takes the whole test binary down and hides every other failure.

## Phase 5 is deferred

Phase 5 (devrt, automation, language server, standalone preview host, accessibility
adapters, inspector, windows/linux platform adapters) starts only after the platform
renders a real application. The application-readiness plan and its cluster order live in
`PRE-PHASE-5-PLAN.md`.

## Collapse the consecutive comments in the test suites

Every test file pairs an `// arrange` / `// act` / `// assert` marker with an explanatory
comment line beneath it. Two comment lines above one statement violate the single-line rule
in `STYLE-GUIDE.md` (§Comments), so the sweep keeps only the marker and drops the
explanation: `// assert` alone above the assertion, because the test name and the assertion
already say what the check means.

162 pairs across 6 files:

| file | pairs |
| --- | --- |
| `tests/compiler_type_checker.test.cpp` | 83 (82 marker pairs plus one markerless pair) |
| `tests/compiler_parser.test.cpp` | 35 |
| `tests/manifest.test.cpp` | 21 |
| `tests/compiler_value_type.test.cpp` | 18 |
| `tests/compiler_lexer.test.cpp` | 3 |
| `tests/compiler_ast.test.cpp` | 2 |

The one markerless pair sits at `tests/compiler_type_checker.test.cpp:84` — two explanation
lines above `catalog.add_native_data`, which collapse into a single line instead.

`tests/compiler_diagnostic.test.cpp` is already clean.

Until the sweep runs, the rule applies per file: whenever a file is modified and carries
this violation, it is fixed in the same change (see `AGENTS.md`, Workflow).
