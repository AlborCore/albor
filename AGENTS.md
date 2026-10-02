# AGENTS.md

Albor is a cross-platform declarative UI framework for C++ (architecture: `albor-design-v3.md`).
This document is the agent contract for this repository. Everything here is mandatory.

## Language

- All code style rules live in `STYLE-GUIDE.md`
- Preserve existing architecture unless explicitly requested
- Headers use the `.h` extension, sources use `.cpp`; do not introduce another extension

## Source of Truth

- `albor-design-v3.md` is the architecture specification. It outranks any implementation habit.
- Its rules are identified as **Decided**, **Proposed** or **Open** (§0.1, §29). Never treat a *Proposed* decision as settled, and never treat an *Open* decision as decided without a documented revision.
- Do not weaken a boundary the spec draws. The load-bearing ones are listed under **Non-negotiable Boundaries** below.
- Reference sections as `§N` / `AD-NN` in commit messages, code comments and reviews so decisions stay traceable.

## Non-negotiable Boundaries

Violating any of these is a defect, not a style issue.

- **No interpreter in Release** (AD-01, §2.1): production builds contain no `.ui` parser, no IR evaluator, no type registry, no property-by-name reflection, no automation channel.
- **Layout invariance** (AD-02, §2.3): runtime class layout and virtual interfaces are identical in Release, Dev and Test. Dev/Test features attach only through registration hooks that exist in every build, side tables keyed by node identity, and separately linked/generated translation units. Never change class layout or inline code under `#ifdef`.
- **Automation is excluded from Release at compile/link time** (AD-10, §19.6), not by a runtime flag. Local-only transport, per-session token, application allow-list, off unless explicitly enabled.
- **MCP is an adapter over the automation layer only** (AD-20, §19.7). It must not reach commands or runtime internals directly.
- **Identity is explicit** (AD-11, §18.1): a node's `id:` is its identity; compiler-generated implicit keys are correlation hints for live reload and must never be used by automation selectors or semantic keys.
- **Two-stage frame** (AD-18, §10.2): scene build runs on the UI thread and produces an immutable `FrameScene`; render execution may run on a render thread and must never call application code.
- **Immutable snapshots cross threads** (§10.5, §13.2): application data reaches custom nodes and bindings as `std::shared_ptr<const T>` in an observable `Snapshot<T>`. `Observable<T>::post_set` is the thread-safe path; `set` is UI-thread only.
- **The manifest is the single source of truth for native components** (AD-17, §15.1): C++ glue is generated from the manifest, and the compiler never parses C++ headers.
- **No platform or Skia types in public headers** (§3.5, AD-05, AD-19).
- **The compiler is not the runtime, and development tooling is not the production application** (§37).

## Build & Unit Tests

- Build system: `cmake`
  - Configure: `cmake --preset debug`
  - Build: `cmake --build --preset debug`
- Dependencies: `vcpkg` (manifest mode, pinned in `vcpkg.json`)
- Unit Tests, avoid running unit tests using `ctest`. Execute unit tests by executing the process: `./.build-debug/tests/albor-tests`
- Unit tests must be self-contained: no helper functions, no test utilities, no external fixtures. The only shared object is the test double.
- Debug builds define `ALBOR_DEV` and link the dev runtime extension; Test builds define `ALBOR_TEST` and link the automation core and the software renderer (§2.2).

## Workflow

Before editing:

1. Search for existing implementations
2. Understand ownership and lifetime
3. Follow conventions in `STYLE-GUIDE.md` and the surrounding code
4. Make minimal changes

After editing:

1. Build the project
2. Fix compiler warnings/errors
3. Run the linter check on all changed C++ files: `bash build/check-format.sh`
4. Summarize changed files
5. Do not execute unit tests unless explicitly asked

## Repository Layout

The layout follows §32. Module directories map to the runtime modules of §6 and must stay synchronized with them.

```text
core/ application/ ui/ layout/ input/ focus/ commands/ model/
text/ style/ animation/ window/
platform/ (macos/ windows/ linux/{wayland,x11}/ headless/)
render/ render-skia/ render-software/ widgets/ semantics/ accessibility/
services/ resources/ i18n/
compiler/ manifest/ language-server/ preview/ dev-service/
devrt/ automation/ inspector/
tools/ examples/ tests/
prototypes/    # not a runtime module: throwaway measurement code from §30
```

- `prototypes/` holds the Phase 0 experiments (A1, A2, B, C, D, E). Prototype code is measured, not production: it may be deleted once its decision is recorded in §29. It must never be linked by a production module.
- `devrt/`, `automation/` and `inspector/` are linked only in Dev and Test configurations. A `core/`, `ui/`, `window/` or `render/` file that references them breaks AD-10.

## Architecture Map

Where each subsystem is specified, so that changes land in the right module.

| Area | Spec | Module | Phase |
| --- | --- | --- | --- |
| Application, event loop, tasks, timers, lifetime tokens | §7 | `application/` | 1 |
| Window roles, modality, `WindowHandle`, close request | §8 | `window/` | 1 |
| Platform abstraction, per-platform backends | §24 | `platform/` | 1 |
| Render surface, metrics, scale factor | §9 | `render/` | 1 |
| Frame pipeline, scheduler, per-window pacing | §10 | `render/` | 1 |
| Custom rendering: `CustomRenderNode`, `RenderContext`, retained resources | §11 | `render/` | 1 (A1) |
| UI tree, properties, bindings, environment properties | §13 | `ui/` | 2, 3 |
| Semantic tree, explicit/implicit keys | §18, §19 | `semantics/` | 2 |
| Input normalization, coalescing, focus | §12 | `input/`, `focus/` | 2 |
| Automation core (Dev/Test only) | §19 | `automation/` | 2 |
| Animation core | §21.4 | `animation/` | 3 |
| Resources, fonts, images | §23.2 | `resources/` | 4 |
| Text, measurement, shaping, IME | §21.2 | `text/` | 4 |
| Layout contract | §21.1 | `layout/` | 4 |
| i18n core, catalogs, direction | §23.3 | `i18n/` | 4 |
| Compiler frontend, typed IR, codegen | §16 | `compiler/` | 5 |
| Manifest parser, validator, glue generator | §15 | `manifest/` | 5 |
| Widgets, models, virtualization, commands, themes | §20, §21.3, §22 | `widgets/`, `model/`, `commands/`, `style/` | 6 |
| Clipboard, dialogs, menus, drag/drop | §22 | `services/` | 7 |
| LSP, preview, inspector, diagnostics | §17, §26 | `language-server/`, `preview/`, `inspector/` | 8 |
| Dev runtime extension, IR loader, reload adapter | §17.2 | `devrt/` | 9 |

## Codegen Conventions

For generated C++ (§14) and any hand-written stand-in for it:

- `.ui` compiles to native C++. A property binding is a **plain static function** plus **declared dependencies as `PropertyId`s**. No `std::function` per binding, no per-binding heap allocation (§13.3).
- Generated public headers expose a **concrete, non-virtual facade** over an opaque implementation: stable interface, private body (§14.1, AD-03). Never generate an abstract base with virtual dispatch.
- The generator writes a generated file **only when its content changes** (§14.2).
- Editing a `.ui` body must rebuild only that component's generated `.cpp`; changing a declared interface rebuilds the header and its includers (§14.3).
- Generated headers follow the `.h` extension and the include order of `STYLE-GUIDE.md`; the codegen must produce files that already pass `bash build/check-format.sh`.

## Testing

C++ uses Google Test; Python uses `unittest` (see `STYLE-GUIDE.md`). Beyond per-file unit tests, the architecture defines cross-cutting suites (§27):

- **Dual-engine conformance**: one `.ui` corpus through compiled output and the dev IR evaluator, same observable behavior (AD-25). This is what makes §2.4 true.
- **GPU/software parity**: every Level 2 resource kind and primitive behaves the same in both renderers within tolerance (§11.7, AD-26). The software renderer is the screenshot reference.
- **LSP/compiler agreement**: same corpus, same diagnostics (§16).
- **Manifest consistency**: generated glue, descriptor tables and preview stubs agree with the manifest.
- **Semantic tree tests**: role, name, states and actions of every widget; a widget is not done without them.
- **Build-scope tests**: a scripted check that a `.ui` body edit rebuilds only the expected translation units (§14.3).
- **Automation compatibility**: the reference test in Appendix B runs against a sample application.

## Phasing and Prototypes

Implementation follows §31. Phase 0 runs the prototypes in §30 and their pass/fail thresholds must be **recorded before the prototype runs** (§28.4) — never derived from the results afterwards.

A prototype exists to be able to fail. Its deliverable is a recorded measurement and a decision in §29, not production code. When reporting prototype results, state the workload, the hardware and the threshold, and then the measurement.