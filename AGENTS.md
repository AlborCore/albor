# AGENTS.md

Albor is a cross-platform declarative UI framework for C++ (architecture: [`docs/framework-design.md`](docs/framework-design.md)).
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

- The file carries the infix of the platform it targets, so `render/render-albor/albor_metal_backend.macos.mm` is correct and `albor_metal_backend.mm` is not
- The base name never repeats the platform, so the file is `platform_window.macos.mm` and not `macos_platform_window.mm`; the platform is already carried by the infix and by the directory
- A source that reaches no platform API stays portable, even inside a platform module, so `render/render-blend2d/blend2d_render_context.cpp` stays `.cpp` because it only calls blend2d
- A header stays portable whenever it can: the module's public header carries no platform type (BDG-08), so it is `platform/macos/platform_window.h` and not `platform_window.macos.h`
- A header that cannot be portable, because it declares platform types or imports platform headers, carries the infix as well, for example `native_surface.macos.h`
- The infix is part of the file's identity: renaming a file is a build change, so the module `CMakeLists.txt` and every `#include` are updated in the same change

A shader is `<name>.metal`, for example `render/render-albor/albor_shaders.metal`, or its direct3d twin
`<name>.hlsl`, for example `render/render-albor/albor_shaders.hlsl`. The compiled artifact (`<name>.metallib`,
`<name>.dxbc` or the vulkan container `<name>.spv`) and a `.sha256` stamp of the pair are **committed build inputs**, because a framework consumer
must not need the shader toolchain to build (§4, BDG-10): every platform build embeds the artifact and verifies the stamp, so a
shader change without regeneration fails. `bash build/shaders.sh [--d3d|--vulkan|--all] --write` regenerates them, and is the only place the
toolchain runs. A shader is not a C++ source: clang-format does not rewrite it, and the comment sweep in
`build/check-format.sh` checks its comment length and adjacency like any other file.

Never introduce an extension outside this list. Adding a platform extension family means updating the
pattern in `build/check-format.sh` in the same change.


## Source of Truth

- [`docs/framework-design.md`](docs/framework-design.md) is the architecture and acceptance contract. It outranks any implementation habit.
- All three platforms — macOS, Windows and Linux — stay at the same level; a platform change carries its equivalents on the other two in the same change. `docs/status.md` is the shared per-abstraction support table.
- Architectural decisions are settled in the framework design contract. Do not reopen a settled boundary without a documented revision of that contract.
- Do not weaken a boundary the spec draws. The load-bearing ones are listed under **Non-negotiable Boundaries** below.
- Reference contract clauses as `BDG-NN`, gaps as `GAP-NN`, and section numbers as `§N` of `docs/framework-design.md` in commit messages, code comments and reviews so decisions stay traceable.

## Non-negotiable Boundaries

Violating any of these is a defect, not a style issue.

- **No interpreter in Release** (BDG-01): production builds contain no `.ui` parser, no IR evaluator, no type registry, no property-by-name reflection, no automation channel.
- **Layout invariance** (BDG-02): runtime class layout and virtual interfaces are identical in Release, Dev and Test. Dev/Test features attach only through registration hooks that exist in every build, side tables keyed by node identity, and separately linked/generated translation units. Never change class layout or inline code under `#ifdef`.
- **Dev/Test-only code is excluded at link time, never with `#ifdef`** (BDG-03). Keep the source byte-identical across configurations, put the feature in its own linked target, and install it through the registration hook. A Release reference to an excluded symbol must fail the link, not compile away silently.
- **Automation is excluded from Release at compile/link time** (BDG-04), not by a runtime flag. Local-only transport, per-session token, application allow-list, off unless explicitly enabled. The embedded host is linked via the whole-archive umbrella when `ALBOR_AUTOMATION` is on, and is independent of the dev runtime extension (BDG-03).
- **MCP is an adapter over the automation layer only** (BDG-04). It must not reach commands or runtime internals directly.
- **Identity is explicit** (BDG-05): a node's `id:` is its identity; compiler-generated implicit keys are correlation hints for live reload and must never be used by automation selectors or semantic keys.
- **Two-stage frame** (BDG-06): scene build runs on the UI thread and produces an immutable `FrameScene`; render execution may run on a render thread and must never call application code.
- **Immutable snapshots cross threads** (BDG-06): application data reaches custom nodes and bindings as `std::shared_ptr<const T>` in an observable `Snapshot<T>`. `Observable<T>::post_set` is the thread-safe path; `set` is UI-thread only.
- **The manifest is the single source of truth for native components** (BDG-07): C++ glue is generated from the manifest, and the compiler never parses C++ headers.
- **No platform or renderer types in public headers** (BDG-08).
- **Native backend parity** (BDG-09): `render-albor` ships three native GPU backends, Metal on macOS, Direct3D 11 on Windows and Vulkan on Linux, and they stay at the same level. Every capability, fix and optimization is applied to all three in the same change: the same retained resources, cache policy, base-vertex handling, profiling coverage and tests. A change carried by one backend only is a defect, not a follow-up, and the backends that cannot be built on the host are verified in CI before the change is done.
- **Platform parity** (BDG-10): macOS, Windows and Linux stay at the same level. Any change to a platform-specific source (`*.macos.mm`, AppKit, CoreText, CoreVideo, Metal, NSAccessibility; `*.win32.c++`, Win32, DirectWrite, Direct3D, UI Automation; `*.linux.c++`, Wayland, X11) carries its equivalents on the other two platforms in the same change, with the same tests; a change carried by one platform only is a defect, not a follow-up. The platforms that cannot be built on the host are verified in CI before the change is done. Wayland is Tier 1 and X11 is Tier 2 (§5.3 of the design contract), and both adapters carry the same tests.
- **The compiler is not the runtime, and development tooling is not the production application** (BDG-11).

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
- Debug builds define `ALBOR_DEV` and link the dev runtime extension and automation host; Test builds define `ALBOR_TEST` and link the automation core and the software renderer (§4).

## Workflow

Before editing:

1. Search for existing implementations
2. Understand ownership and lifetime
3. Follow conventions in `STYLE-GUIDE.md` and the surrounding code
4. Make minimal changes

**Documentation is mandatory for every feature.** Any change that adds or alters a feature must
include a task that updates the affected documentation in the same change: the relevant page under
`docs/`, `docs/status.md` when an implementation level changes, and `docs/framework-design.md` when
a contract or gap changes. A change that implements a `GAP-NN` item must close that gap in the
contract. A feature without its documentation task is incomplete, not a follow-up.

A platform-specific change is not complete without its counterparts on the other two platforms: update all three platform sources and their test suites in the same change.

After editing:

1. Build the project
2. Fix compiler warnings/errors
3. Run the linter check on all changed C++ files: `bash build/check-format.sh`
4. Fix every consecutive comment line in the files you changed: one comment line above a statement, and when a test marker is followed by an explanation only the `// arrange` / `// act` / `// assert` marker stays (`STYLE-GUIDE.md`; the sweep state is tracked in the issue tracker)
5. Summarize changed files
6. Do not execute unit tests unless explicitly asked

## Repository Layout

The layout follows §3.1 of the design contract. Module directories map to runtime modules and must stay synchronized with them.

```text
albor/ (every framework module lives under this include folder, so consumers
        include "albor/<module>/<header>.h" and the paths match the albor:: namespace)
├── core/ application/ ui/ layout/ input/ focus/ commands/ model/
├── text/ style/ animation/ window/
├── platform/ (macos/ windows/ linux/ headless/)
├── render/ render-software/ render-blend2d/ render-albor/ widgets/ semantics/ accessibility/
├── resources/ i18n/
├── compiler/ manifest/ language-server/ preview/
├── devrt/ automation/ inspector/
build/ docs/ ports/ third-party/ tools/ examples/ tests/
```

- Phase 0 prototype code lives exclusively on the `prototypes` git branch for historical reference. The implementation branch (branched out of `main`) contains only clean production modules and tests; no `prototypes/` directory exists here.
- `devrt/`, `automation/` and `inspector/` are linked only in Dev and Test configurations. A `core/`, `ui/`, `window/` or `render/` file that references them violates BDG-04.

## Architecture Map

Where each subsystem is specified, so that changes land in the right module.

Section numbers refer to [`docs/framework-design.md`](docs/framework-design.md).

| Area | Design contract | Module |
| --- | --- | --- |
| Application, event loop, tasks, timers, lifetime tokens | §5.1 | `application/` |
| Window roles, modality, `WindowHandle`, close request | §5.2 | `window/` |
| Platform abstraction, macOS/Win32/Wayland/X11 HAL | §5.3 | `platform/` |
| Render surface, metrics, scale factor | §6 | `render/` |
| Frame pipeline, two-stage frame, scheduler | §6.1 | `render/` |
| Custom rendering: `CustomRenderNode`, `RenderContext`, retained resources | §6.2 | `render/` |
| Reactive properties, eager-push bindings, `Property<T>` | §7 | `ui/`, `runtime/` |
| UI tree, `UiNode`, components, event routing | §8.1 | `ui/` |
| Semantic tree, explicit/implicit keys, `SemanticProvider` | §10.1 | `semantics/` |
| Input normalization, focus | §8.3 | `input/`, `focus/` |
| Layout contract: measure & arrange, containers | §8.2 | `layout/` |
| Text, measurement, Unicode shaping, HarfBuzz | §9 | `text/` |
| Standard widgets (Button, Label, Slider, Canvas) | §8 | `widgets/` |
| Animation core & frame scheduler integration | §3.1 | `animation/` |
| Compiler frontend, .ui parser, AST, typed IR | §11 | `compiler/` |
| Manifest parser, validator, glue generator | §11 | `manifest/` |
| C++ code generator, concrete facade emission | §11 | `compiler/` |
| Dev runtime extension, IR loader, reload adapter | §12 | `devrt/` |
| LSP, preview host, inspector diagnostics | §12 | `language-server/`, `preview/`, `inspector/` |
| Automation core (Dev/Test only), MCP server adapter | §10.3 | `automation/` |
| Platform accessibility adapters (NSAccessibility, UIA, AT-SPI) | §10.2 | `accessibility/` |

## Codegen Conventions

For generated C++ (§11 of the design contract) and any hand-written stand-in for it:

- `.ui` compiles to native C++. A property binding is a **plain static function** plus **declared dependencies as `PropertyId`s**. No `std::function` per binding, no per-binding heap allocation (§7).
- Generated public headers expose a **concrete, non-virtual facade** over an opaque implementation: stable interface, private body (§11). Never generate an abstract base with virtual dispatch.
- The generator writes a generated file **only when its content changes** (§11).
- Editing a `.ui` body must rebuild only that component's generated `.cpp`; changing a declared interface rebuilds the header and its includers (§13).
- Generated headers follow the `.h` extension and the include order of `STYLE-GUIDE.md`; the codegen must produce files that already pass `bash build/check-format.sh`.

## Testing

C++ uses Google Test; Python uses `unittest` (see `STYLE-GUIDE.md`). Beyond per-file unit tests, the design contract defines cross-cutting suites (§13.2):

- **Dual-engine conformance**: one `.ui` corpus through compiled output and the dev IR evaluator, same observable behavior (§13.2).
- **GPU/software parity**: every resource kind and primitive behaves the same in the GPU backends and the software reference within tolerance (§13.2). The software renderer is the screenshot reference.
- **Native backend parity**: every behavior or performance guarantee proven in the Metal suite is proven in the Direct3D and Vulkan suites, and vice versa; the three suites stay mirrored, so a backend-only test gap is a defect (BDG-09).
- **Platform parity**: every platform-specific behavior is proven in the suites of the other two platforms; a platform-only test gap is a defect, and `docs/status.md` records the per-platform support levels (BDG-10).
- **LSP/compiler agreement**: same corpus, same diagnostics (§13.2).
- **Manifest consistency**: generated glue, descriptor tables and preview stubs agree with the manifest.
- **Semantic tree tests**: role, name, states and actions of every widget; a widget is not done without them.
- **Build-scope tests**: a scripted check that a `.ui` body edit rebuilds only the expected translation units (§13).
- **Automation compatibility**: the reference test runs against a sample application (§13.2).

## Implementation Phasing

The five implementation phases delivered their runtime surfaces, including the Phase 5 Python automation compatibility harness (GAP-01, #25), and the remaining stable-tag blockers are recorded in [`docs/status.md`](docs/status.md#pre-release-scope). The current architecture is described in [`docs/framework-design.md`](docs/framework-design.md). Phase 0 exploratory prototyping is concluded; all Phase 0 code is preserved on the `prototypes` git branch for reference.
The implementation codebase branched from `main` is clean: only production modules, runtime libraries, tooling, and unit tests exist.