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

## Embedded text draws, but no application installs the resolver yet

The caller shipped in `1d2eb55`. A `ShapedRun` carries the content hash of the font it was shaped from,
the vendored engine reports its own hash, and `SceneRecorder::draw_text` expands any run carrying one
into a `GlyphOutline` command per glyph through a cache. `FramePipeline` forwards a resolver, so an
application installs one line at startup and every widget draws embedded text through the ordinary
`draw_text` it already called.

Two things remain, and both are downstream of the platform rather than of this path.

**No example installs the resolver.** Nothing in `examples/` calls `set_outline_resolver`, so no
application reaches the embedded path at all; every one still goes through the Skia paragraph engine.
Wiring it into an example is the honest test of whether AD-34's text story works outside a test.

**The replay allocates a `PathResource` per glyph per frame**, because a contour needs verbs and the
outline stores only points. Building the verbs once, at outline construction, removes it. The embedded
path also does a cache lookup per glyph per frame, which is a hash lookup rather than an allocation,
and is the thing to revisit if text ever shows up in a profile.

**A measurement caveat worth keeping.** `tools/alloc-probe` replaces global `operator new`, so it is
blind to direct `malloc`, which is how Skia allocates. Scene build figures it reports are sound (it
over-counts, not under-counts: 1000 objects read as 1011, the extra being the vector's own growth),
but any figure that includes rasterization is not. Closing that means intercepting `malloc` in the
probe; it was attempted and recurses, so it needs a reentrancy guard. Until then, do not quote a
render-side allocation number.

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

## The allocation probe measures nothing in a release build

`tools/alloc-probe` replaces global `operator new`, which is how it counts. Under the
optimizer a release build enables, a small allocation such as `std::make_unique<int>`
is scalar-replaced and the operator is never called, so the count reads zero.
`AllocationProbeChecks.TheProbeCountsWhatIsAllocated` fails in a release configure and
has done since before the automation work; debug passes.

This is pre-existing, verified by running the suite against the previous commit, and
it is not fixed here. It matters because the probe is the only instrument the project
has for the allocation questions in `ALLOCATION-REVIEW.md`. Either the probe needs a
large enough allocation that the optimizer cannot elide it, or the suite needs to be
skipped in release with the reason stated. Do not quote a release allocation number
until one of those lands.

## A .ui file cannot express a list, so no application can show one

**The target.** A component declares an ordered collection and renders one row per item,
with rows keyed by a stable identity so a row survives its item moving or being deleted:

```
component TaskList {
    inout property<vector<Task>> tasks;

    Column {
        Repeat(task in tasks) {
            Row {
                CheckBox { checked: task.done; }
                Label { text: task.title; }
            }
        }
    }
}
```

This is the MVP gap that matters most. Every other part of the framework is further along
than this one, and a list is the single most common thing a UI framework does.

**Where it stands.** The manifest half is done and committed. `record` declarations parse,
require a `key` field (`E0308` when absent, because identity is explicit per AD-11), and
generate a plain aggregate whose members carry the declared types and re-emitted defaults.
An untranslatable field type reports `E0309` and empties the output rather than emitting a
struct that would not compile. The generated header was verified to pass `-Wall -Wextra
-Wpedantic -fsyntax-only` and `clang-format --dry-run --Werror`, which §14.2 requires of
generated code.

The front end was already finished and is not the problem. The lexer accepts `vector<T>`, the
parser parses `Repeat(item in source [, index]) { one element }`, and the type checker
resolves the repeat's item name and index against the source's element type.

**The three steps**, in order, because each is written against the one before. Steps 2 and 3
remain; step 1 is still open and still blocks the example above, which uses a record:

1. **Register records with the compiler catalog.** `Task` currently dies in
   `resolve_type_name` as `E0400`, because only scalars and manifest *components* are known
   types. Registering records makes `vector<Task>` resolve and lets `item.title` be typed as
   a field read rather than an unknown property. Nothing is generated yet at this step.
2. ~~**Lift `E0501` for collections.**~~ **Done.** `ArrayExpression` is the only way an
   author writes a collection value, so the parser reads `[1, 2]` as one expression rather
   than as three. The checker types a list as the element type its elements agree on, and
   `E0408` reports a list that disagrees, once, because the first mismatch settles what the
   list cannot be. An empty list has no element to infer from, so it types as a collection of
   an unknown element and assigns into any collection: the declared target is the only
   evidence of what an empty list holds.

   Codegen spells `vector<T>` as `std::vector<T>` and recurses, so a nested collection needs
   no special case. A collection accessor returns `const std::vector<T>&` rather than a copy,
   because copying a list on every read would be a cost the read path should not carry.
   `tests/vectors/vector_panel.ui` compiles through the real generator into
   `tests/vector_codegen.test.cpp`, which constructs the facade and reads it back, so the
   generated collection code is compiled and executed rather than only inspected.
3. **Lift `E0500` for `Repeat`.** `validate_node` rejects the node outright. The emitter
   needs to materialize one row per element and reconcile rows by `key` rather than by
   position.

**The decision that shapes step 3.** AD-13 is settled: model views virtualize materialized
nodes, rows keyed by stable `ItemKey` identifiers. A repeater emitted as a plain loop in
`build()` would contradict it and be rewritten when virtualization lands, because row
identity is the thing virtualization depends on. Rows are therefore materialized and keyed
now, and virtualization becomes an optimization over that rather than a replacement for it.
`model/` is the reserved home for the keyed view and does not exist yet.

**Why records had to come from the manifest.** AD-17 says the compiler never parses C++
headers and the manifest is the single source of truth for native types. A `.ui` file that
must know a task's fields can only learn them from the manifest, so extending the manifest
was forced rather than chosen.

**What still blocks a real application even after the above.** A todo application also needs
a text entry widget: `widgets/` holds only button, canvas, checkbox, label and slider, while
`EventKind::TextInput` is normalized, routed through focus, and consumed by nothing.
`SemanticRole::TextField` is declared but no widget produces it. Separately, a handler body
is a closed grammar of exactly two statements (`compiler/ast.h` `HandlerStatementKind`):
assign a property of *this* component, or invoke *this* component's signal. So even a
working repeater cannot add or remove a row from a `.ui` file. That is a language design
change rather than codegen work, and it is not scoped here.

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

`tests/manifest.test.cpp` was already swept in this direction (commit `0ba2c38`) by merging
each marker with its explanation into one line rather than dropping the explanation, which
satisfies the rule but leaves that file inconsistent with the rest of the sweep. Align it or
record the exception before the sweep reaches it.

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
