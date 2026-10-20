# AGENTS.md

Albor is a cross-platform declarative UI framework for C++ (architecture: `albor-design-v4.md`).
This document is the agent contract for this repository. Everything here is mandatory.

## Language

- All code style rules live in `STYLE-GUIDE.md`
- Preserve existing architecture unless explicitly requested
- Headers use the `.h` extension, sources use `.cpp`; do not introduce another extension

## File Naming Convention

The extension of a source file states the platform it is written against, so a build system can exclude a
platform without inspecting the file. The infix goes between the base name and the extension.

- A portable source is `<name>.cpp` and a portable header is `<name>.h`, for example `application/application.cpp` and `window/window.h`
- A macOS source is `<name>.macos.mm`, for example `platform/macos/platform_window.macos.mm`
- A Linux source is `<name>.linux.c++`, for example `platform/linux/wayland/window.linux.c++`
- A Windows source is `<name>.win32.c++`, for example `platform/windows/window.win32.c++`

Mandatory rules for a source that reaches a platform API (`AppKit`, `Metal`, `CoreVideo`, `Win32`, `xcb`, `wayland`):

- The file carries the infix of the platform it targets, so `render/render-skia/skia_renderer_backend.macos.mm` is correct and `skia_renderer_backend.mm` is not
- The base name never repeats the platform, so the file is `platform_window.macos.mm` and not `macos_platform_window.mm`; the platform is already carried by the infix and by the directory
- A source that reaches no platform API stays portable, even inside a platform module, so `skia_render_context.cpp` stays `.cpp` because it only calls skia
- A header stays portable whenever it can: the module's public header carries no platform type (§3.5, AD-05), so it is `platform/macos/platform_window.h` and not `platform_window.macos.h`
- A header that cannot be portable, because it declares platform types or imports platform headers, carries the infix as well, for example `native_surface.macos.h`
- The infix is part of the file's identity: renaming a file is a build change, so the module `CMakeLists.txt` and every `#include` are updated in the same change

A shader is `<name>.metal`, for example `render/render-albor/albor_shaders.metal`. Its compiled artifact
(`<name>.metallib`) and a `.sha256` stamp of the pair are **committed build inputs**, because a framework consumer
must not need the Metal toolchain to build (§6.3, AD-16): every Apple build embeds the artifact and verifies the stamp, so a
shader change without regeneration fails. `bash build/shaders.sh --write` regenerates both, and is the only place the
toolchain runs. A shader is not a C++ source: clang-format does not rewrite it, and the comment sweep in
`build/check-format.sh` checks its comment length and adjacency like any other file.

Never introduce an extension outside this list. Adding a platform extension family means updating the
pattern in `build/check-format.sh` in the same change.


## Source of Truth

- `albor-design-v4.md` is the architecture specification. It outranks any implementation habit.
- Architectural decisions are settled (**Decided**, §13) with modular hardware and subsystem abstraction layers. Do not reopen settled decisions without a documented revision.
- Do not weaken a boundary the spec draws. The load-bearing ones are listed under **Non-negotiable Boundaries** below.
- Reference sections as `§N` / `AD-NN` in commit messages, code comments and reviews so decisions stay traceable.

## Non-negotiable Boundaries

Violating any of these is a defect, not a style issue.

- **No interpreter in Release** (AD-01, §2.1): production builds contain no `.ui` parser, no IR evaluator, no type registry, no property-by-name reflection, no automation channel.
- **Layout invariance** (AD-02, §2.3): runtime class layout and virtual interfaces are identical in Release, Dev and Test. Dev/Test features attach only through registration hooks that exist in every build, side tables keyed by node identity, and separately linked/generated translation units. Never change class layout or inline code under `#ifdef`.
- **Dev/Test-only code is excluded at link time, never with `#ifdef`** (AD-04, §2.3). Keep the source byte-identical across configurations, put the feature in its own linked target, and install it through the registration hook. A Release reference to an excluded symbol must fail the link, not compile away silently.
- **Automation is excluded from Release at compile/link time** (AD-10, §10.3), not by a runtime flag. Local-only transport, per-session token, application allow-list, off unless explicitly enabled. The embedded host is linked via the whole-archive umbrella when `ALBOR_AUTOMATION` is on, and is independent of the dev runtime extension (AD-02, §10.3).
- **MCP is an adapter over the automation layer only** (AD-20, §10.3). It must not reach commands or runtime internals directly.
- **Identity is explicit** (AD-11, §18.1): a node's `id:` is its identity; compiler-generated implicit keys are correlation hints for live reload and must never be used by automation selectors or semantic keys.
- **Two-stage frame** (AD-18, §10.2): scene build runs on the UI thread and produces an immutable `FrameScene`; render execution may run on a render thread and must never call application code.
- **Immutable snapshots cross threads** (§10.5, §13.2): application data reaches custom nodes and bindings as `std::shared_ptr<const T>` in an observable `Snapshot<T>`. `Observable<T>::post_set` is the thread-safe path; `set` is UI-thread only.
- **The manifest is the single source of truth for native components** (AD-17, §11.2): C++ glue is generated from the manifest, and the compiler never parses C++ headers.
- **No platform or Skia types in public headers** (§3.5, AD-05, AD-19).
- **The compiler is not the runtime, and development tooling is not the production application** (§2).

## Recovery Points

- **Treat failure as a possible outcome, and snapshot before an operation that can fail.** A scripted rewrite, a bulk rename, a `git reset`, a `rm -rf`, or an agent pass over many files can all destroy work. `bash build/snapshot.sh <label>` first, then perform the operation.
- Snapshots live **outside** the repository, in `$ALBOR_SNAPSHOT_DIR` (default `~/albor-snapshots`). A recovery point must not live inside the thing it protects, because the same operation that damages the tree can damage a snapshot stored beside it.
- Recover with `bash build/restore.sh LATEST` or `bash build/restore.sh <label>`, which unpacks over the working tree without touching git history. Verify the archive before unpacking, so a corrupt snapshot fails while the tree is still intact.
- After a recovery, always confirm with `git status --short` and `git diff` before continuing.
- A commit is **not** a substitute for a snapshot: it records what the tree should be, while a snapshot records what it is. Commit only when the user asks.
- Take a snapshot before any non-trivial change, so the recovery point is cheap and unconditional rather than something to remember under pressure.

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
4. Fix every consecutive comment line in the files you changed: one comment line above a statement, and when a test marker is followed by an explanation only the `// arrange` / `// act` / `// assert` marker stays (`STYLE-GUIDE.md`; the sweep state is tracked in the issue tracker)
5. Summarize changed files
6. Do not execute unit tests unless explicitly asked

## Repository Layout

The layout follows §3.2. Module directories map to runtime modules and must stay synchronized with them.

```text
core/ application/ ui/ layout/ input/ focus/ commands/ model/
text/ style/ animation/ window/
platform/ (macos/ windows/ linux/{wayland,x11}/ headless/)
render/ render-skia/ render-software/ render-blend2d/ render-albor/ widgets/ semantics/ accessibility/
services/ resources/ i18n/
compiler/ manifest/ language-server/ preview/ dev-service/
devrt/ automation/ inspector/
tools/ examples/ tests/
```

- Phase 0 prototype code lives exclusively on the `prototypes` git branch for historical reference. The implementation branch (branched out of `main`) contains only clean production modules and tests; no `prototypes/` directory exists here.
- `devrt/`, `automation/` and `inspector/` are linked only in Dev and Test configurations. A `core/`, `ui/`, `window/` or `render/` file that references them breaks AD-10.

## Architecture Map

Where each subsystem is specified, so that changes land in the right module.

| Area | Spec | Module | Phase |
| --- | --- | --- | --- |
| Application, event loop, tasks, timers, lifetime tokens | §4 | `application/` | 1 |
| Window roles, modality, `WindowHandle`, close request | §5 | `window/` | 1 |
| Platform abstraction, macOS/Win32/Wayland HAL | §5.3 | `platform/` | 1 |
| Render surface, metrics, scale factor | §5, §6 | `render/` | 1 |
| Frame pipeline, two-stage frame, scheduler | §6.1 | `render/` | 1 |
| Custom rendering: `CustomRenderNode`, `RenderContext`, retained resources | §6.4 | `render/` | 1 |
| Reactive properties, eager-push bindings, `Property<T>` | §7 | `ui/`, `runtime/` | 2 |
| UI tree, `UiNode`, components, event routing | §8.1 | `ui/` | 2 |
| Semantic tree, explicit/implicit keys, `SemanticProvider` | §10.1 | `semantics/` | 2 |
| Input normalization, coalescing, focus | §8.3 | `input/`, `focus/` | 3 |
| Layout contract: measure & arrange, containers | §8.2 | `layout/` | 3 |
| Text, measurement, Unicode shaping, HarfBuzz | §9.1 | `text/` | 3 |
| Standard widgets (Button, Label, Slider, Canvas) | §8 | `widgets/` | 3 |
| Animation core & frame scheduler integration | §7.3 | `animation/` | 3 |
| Compiler frontend, .ui parser, AST, typed IR | §11.1 | `compiler/` | 4 |
| Manifest parser, validator, glue generator | §11.2 | `manifest/` | 4 |
| C++ code generator, concrete facade emission | §11.3 | `compiler/` | 4 |
| Dev runtime extension, IR loader, reload adapter | §12.1 | `devrt/` | 5 |
| LSP, preview host, inspector diagnostics | §12.2 | `language-server/`, `preview/`, `inspector/` | 5 |
| Automation core (Dev/Test only), MCP server adapter | §10.3 | `automation/` | 5 |
| Platform accessibility adapters (NSAccessibility, UIA, AT-SPI) | §10.2 | `accessibility/` | 5 |

## Codegen Conventions

For generated C++ (§14) and any hand-written stand-in for it:

- `.ui` compiles to native C++. A property binding is a **plain static function** plus **declared dependencies as `PropertyId`s**. No `std::function` per binding, no per-binding heap allocation (§13.3).
- Generated public headers expose a **concrete, non-virtual facade** over an opaque implementation: stable interface, private body (§14.1, AD-03). Never generate an abstract base with virtual dispatch.
- The generator writes a generated file **only when its content changes** (§14.2).
- Editing a `.ui` body must rebuild only that component's generated `.cpp`; changing a declared interface rebuilds the header and its includers (§14.3).
- Generated headers follow the `.h` extension and the include order of `STYLE-GUIDE.md`; the codegen must produce files that already pass `bash build/check-format.sh`.

## Testing

C++ uses Google Test; Python uses `unittest` (see `STYLE-GUIDE.md`). Beyond per-file unit tests, the architecture defines cross-cutting suites (§16):

- **Dual-engine conformance**: one `.ui` corpus through compiled output and the dev IR evaluator, same observable behavior (AD-25). This is what makes §2.4 true.
- **GPU/software parity**: every Level 2 resource kind and primitive behaves the same in both renderers within tolerance (§11.7, AD-26). The software renderer is the screenshot reference.
- **LSP/compiler agreement**: same corpus, same diagnostics (§16).
- **Manifest consistency**: generated glue, descriptor tables and preview stubs agree with the manifest.
- **Semantic tree tests**: role, name, states and actions of every widget; a widget is not done without them.
- **Build-scope tests**: a scripted check that a `.ui` body edit rebuilds only the expected translation units (§14.3).
- **Automation compatibility**: the reference test in Appendix B runs against a sample application.

## Implementation Phasing

Implementation follows §14 (Phases 1 through 5). Phase 0 exploratory prototyping is concluded; all Phase 0 code is preserved on the `prototypes` git branch for reference.
The implementation codebase branched from `main` is clean: only production modules, runtime libraries, tooling, and unit tests exist.