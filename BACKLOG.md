# Backlog

Work that is agreed but not done yet. The tracker is the GitHub issue list; this file points at
it rather than duplicating it, so an entry here is a pointer, not a parallel record.

## Open work

The spec gaps, the conformance suites and the planned widget and example work are tracked on
GitHub. The dependencies between them are wired there, so the blocked chains are visible in one
place. Notable chains:

- The showcase example is blocked by the widget set, the application facade and the style remainder.
- The GPU/software parity suite is unblocked on the framework side (the Metal teardown trap was
  worked around, the fault upstream with a repro in `tools/metal_teardown_probe`) but its text half
  is blocked by the vendored baseline identity.
- The automation reference suite is blocked by the Python harness.
- The dual-engine corpus is blocked by Snapshot<T> properties, because a corpus that cannot name
  the framework's own data-flow model does not decide anything.

## Open work that is not yet on GitHub

- **Embedded text: no example installs the resolver.** The caller shipped in `1d2eb55` and every
  widget draws embedded text through the ordinary `draw_text`, but nothing in `examples/` calls
  `set_outline_resolver`, so no application reaches the embedded path. Wiring it into an example is
  the honest test of whether AD-34's text story works outside a test.
- **The replay allocates a `PathResource` per glyph per frame**, because a contour needs verbs and
  the outline stores only points. Building the verbs once, at outline construction, removes it. The
  embedded path also does a cache lookup per glyph per frame, a hash lookup rather than an
  allocation, and is the thing to revisit if text ever shows up in a profile.
- **A handler body cannot add or remove a row.** A handler body is a closed grammar of exactly two
  statements (`compiler/ast.h` `HandlerStatementKind`): assign a property of this component, or
  invoke this component's signal. So even a working repeater cannot add or remove a row from a
  `.ui` file. That is a language design change rather than codegen work, and it is not scoped here.

## Measurement limits

`tools/alloc-probe` replaces global `operator new`, so it is blind to direct `malloc`, which is how
Skia allocates. Scene build figures it reports are sound (it over-counts: 1000 objects read as
1011, the extra being the vector's own growth), but any figure that includes rasterization is not.
Closing that means intercepting `malloc` in the probe; it was attempted and recurses, so it needs a
reentrancy guard. Until then, do not quote a render-side allocation number, and do not quote a
release number at all, because the optimizer scalar-replaces the small allocations the probe counts.

## Phase plan

Phase 5 (devrt, automation, language server, standalone preview host, accessibility adapters,
inspector, windows/linux platform adapters) starts only after the platform renders a real
application. The application-readiness plan and its cluster order live in `PRE-PHASE-5-PLAN.md`.

## Comment sweep state

The sweep covered every file the recent changes touched, plus the two compiler suites the sweep
had never reached. What remains is tracked on GitHub: the two test suites that still carry
consecutive comment pairs, and the `tests/manifest.test.cpp` inconsistency, where each marker was
merged with its explanation into one line (commit `0ba2c38`) rather than keeping the marker alone.
