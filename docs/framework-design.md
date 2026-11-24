# Albor Framework Design

**Status:** Audited contract. Linked from `AGENTS.md`, `README.md` and `docs/index.md`.
**Version:** 1.1
**Date:** 2026-11-21
**Scope:** Normative contract for framework code, platform parity, and feature acceptance.

This document starts its own history here. It does not inherit, summarise, or rely on any earlier
design document. When it is approved it becomes, together with `AGENTS.md`, the contract that code
changes and features are measured against.

## Sources of truth

This document states contracts, not changing details. Where a detail changes independently of the
contract, the authoritative source is:

| Subject | Authority |
| --- | --- |
| Public API signatures | the public headers under `albor/` |
| Per-platform implementation levels | [`status.md`](status.md) |
| Build tool pins, floors, CI images | [`status.md`](status.md) and `.github/workflows/` |
| Consumer link targets and options | [`consuming.md`](consuming.md) |
| File naming and source layout rules | `AGENTS.md` |
| C++ style and comment rules | `STYLE-GUIDE.md` |
| Component and language usage | `docs/components/`, `docs/ui-language/` |

**Normative language.** *Must* and *must not* are contractual. *Should* is a strong recommendation
whose violation needs a recorded reason. *May* is optional. A "gap" is a named capability that is
not yet implemented; gaps are listed in §14 and are not part of the current contract.

**Conformance.** A change conforms when it satisfies every applicable *must* in this document, keeps
the three platforms and three native GPU backends at parity (BDG-10, BDG-09), and adds the tests and
documentation updates that §13 requires.

---

## 1. Product scope

Albor is a C++20 declarative UI framework for desktop applications. It compiles `.ui` components to
native C++ and runs the same component tree on macOS, Windows and Linux.

- Release builds contain no `.ui` parser, no IR evaluator, no type registry, and no property-by-name
  reflection. Declarative UI is compiled ahead of time.
- Dev and Test builds add live reload and automation through separately linked targets. Application
  source is identical across configurations.
- A frame is built on the UI thread and executed without calling application code.
- The framework provides text editing, Unicode shaping, native accessibility, command abstractions,
  custom rendering for high-throughput views, and local automation for Dev/Test tooling.

---

## 2. Non-negotiable boundaries

Each boundary has a stable identifier so reviews and commit messages can cite it.

### BDG-01 — Release purity

Production builds contain no `.ui` parser, IR evaluator, type registry, property-by-name reflection,
or automation channel. The compiler is a build-time tool; the dev runtime is a Dev-only link choice.

### BDG-02 — Layout invariance

Runtime class layout and virtual interfaces are identical in Release, Dev and Test. Dev/Test features
attach through registration hooks present in every build, side tables keyed by node identity, and
separately linked translation units. Class layout and inline code must never vary under `#ifdef`.

### BDG-03 — Link-time exclusion

Dev/Test-only code is excluded at link time, never by a runtime flag. Source is byte-identical across
configurations. A Release reference to an excluded symbol must fail the link.

### BDG-04 — Automation and MCP boundary

Automation is a Dev/Test, local-only, token-authenticated capability with an application allow-list.
The embedded host is linked through a whole-archive umbrella when `ALBOR_AUTOMATION` is on,
independently of the dev runtime. MCP is an adapter over the automation layer only; it must not reach
commands, controls, or runtime internals directly.

### BDG-05 — Explicit identity

An author-assigned `id:` is identity. Compiler-generated implicit keys are live-reload correlation
hints only and must never be used as automation selectors or semantic keys. A node or command with no
explicit id is not addressable.

### BDG-06 — Two-stage frame and immutable data across threads

Scene build runs on the UI thread and produces an immutable `FrameScene`. Render execution may run on
a render thread and must never call application code. Application data crosses threads only as
`std::shared_ptr<const T>` in a `Snapshot<T>`; `Observable<T>::post_set` is the thread-safe write path
and `set` is UI-thread only.

### BDG-07 — Manifest as single source of truth

Native C++ components exposed to `.ui` are declared in a manifest. C++ glue is generated from it; the
compiler never parses application C++ headers.

### BDG-08 — No platform or renderer types in public headers

Public headers name only framework and standard types. No `NSView*`, `HWND`, Wayland object,
`NSAccessibility` type, COM interface, or GPU API object appears in a public framework header.
Platform types stay in platform headers and implementation files with the required platform infix.

### BDG-09 — Native backend parity

`render-albor` ships three native GPU backends: Metal (macOS), Direct3D 11 (Windows) and Vulkan
(Linux). Every capability, fix, optimisation, cache-policy change, retained-resource change and test
is applied to all three in the same change. A backend-only change is a defect, not a follow-up.
Backends that cannot be built on the host must be verified in CI before the change is complete.

### BDG-10 — Platform parity

macOS, Windows and Linux stay at the same level. A change to a platform-specific source carries its
equivalents on the other two platforms, with the same tests, in the same change. A change carried by
one platform only is a defect, not a follow-up. Wayland is Tier 1 and X11 is Tier 2; both adapters
carry the same tests. The platform that cannot be built on the host is verified in CI.

### BDG-11 — Compiler is not runtime; tooling is not production

Build-time tooling, the dev runtime, preview, LSP, automation and the inspector are never linked into
a production application unless the build is explicitly Dev or Test.

### BDG-12 — Tests are self-contained

Unit tests need no helper functions, test utilities, or external fixtures. The only shared object is
the test double. Tests must assert behaviour, not implementation detail, and must not be skipped to
hide missing coverage.

---

## 3. Architecture map

### 3.1 Module directories

The repository has a top-level `albor/` include tree with one directory per runtime module, and a
top-level `tests/` tree. Every public header is included as `albor/<module>/<header>.h` and declares
types in namespace `albor`.

| Module | Responsibility |
| --- | --- |
| `core/` | Handles, geometry, key types, lifetime tokens, dev hook, shared primitives |
| `application/` | `Application`, event loop seam, tasks, timers, automation host support |
| `window/` | Window roles, options, registry, handles, close lifecycle |
| `platform/` | `macos/`, `windows/`, `linux/` (`wayland/`, `x11/`, `common/`) and `headless/` adapters |
| `render/` | Frame pipeline, scene, HAL, surface, draw commands, resources |
| `render/render-albor/` | Metal, Direct3D 11 and Vulkan backends and their committed shader artifacts |
| `render/render-blend2d/`, `render/render-software/` | Portable CPU backend and deterministic Test reference |
| `ui/`, `layout/`, `input/`, `focus/` | Node tree, measure/arrange, event model, focus and cue |
| `runtime/` | `Property<T>`, `Observable<T>`, binding engine, snapshot/threads support |
| `style/`, `animation/`, `widgets/`, `commands/`, `model/` | Themes, scheduling, controls, shared actions, selection models |
| `text/`, `text/text-harfbuzz/` | Text engine contract and vendored HarfBuzz/ICU implementation |
| `resources/`, `i18n/` | Embedded fonts and assets, translation catalogs and locale |
| `semantics/`, `accessibility/` | Semantic tree and native accessibility adapters |
| `compiler/`, `manifest/` | `.ui` parser/checker/codegen and native manifest tooling |
| `language-server/`, `preview/` | LSP implementation and out-of-process preview |
| `devrt/`, `automation/`, `inspector/` | Dev/Test-only extension, host and scene instrumentation |

### 3.2 Source naming

A portable source is `<name>.cpp` / `.h`. A source that reaches a platform API carries the platform
infix between the base name and the extension: `<name>.macos.mm`, `<name>.win32.c++`,
`<name>.linux.c++`. A header stays portable unless it must declare platform types. The infix is part
of the file's identity: a rename is a build change and must update `CMakeLists.txt` and every
`#include` in the same change.

### 3.3 Dependency direction

- `core/` depends on nothing in the framework.
- `application/`, `window/`, `ui/`, `runtime/`, `render/`, `commands/` depend downward on `core/`.
- `widgets/` and `input/` depend on `ui/`, `layout/`, `focus/` and `commands/`.
- Platform and renderer adapters install themselves through registration seams; higher modules never
  include platform or renderer headers.
- `devrt/`, `automation/` and `inspector/` depend on production modules, never the reverse. A
  production module that names one of them violates BDG-03.

---

## 4. Build and distribution contract

- Build system is CMake with vcpkg in manifest mode. The framework targets C++20.
- Where a desktop platform adapter exists, the public application targets are `albor` (GPU renderer)
  and `albor-blend2d` (portable CPU renderer). Platform and text engines are selected at configure
  time inside the framework.
- Expert composition uses the granular `albor-*-auto` whole-archive `INTERFACE` targets. Exactly one
  platform, one text engine and one renderer are linked into a binary.
- The principal configuration options are `ALBOR_DEV`, `ALBOR_TEST` and `ALBOR_AUTOMATION`;
  automation is forbidden in Release. Other options select examples, tests, coverage, sanitizers and
  the portable renderer. The repository presets are `debug`, `release`, `test`, `asan` and
  `coverage`.
- Public headers install under `include/albor/<module>/`. Consumers link the framework include root;
  a consumer directory must not be able to shadow a framework header through include order.
- Shader sources, compiled artifacts and `.sha256` stamps are committed build inputs. A shader change
  without regeneration fails the build.
- Authoritative commands: `cmake --preset debug`, `cmake --build --preset debug`,
  `./.build-debug/tests/albor-tests`, `bash build/check-format.sh`. Tests are run as processes, not
  through `ctest`.
- Toolchain pins, OS floors and CI images live in [`status.md`](status.md) and the workflow files.
  This document must not duplicate version numbers that change with CI.

---

## 5. Application, windows, and platform HAL

### 5.1 Application

`Application` is the process-wide instance that owns the event loop seam, window registry, task
queue, timers, command registry, renderer backend, text engine and window stages.

- `run()` starts frame pacing and blocks until the loop is asked to stop; it ends when there are no
  windows left or `quit()` is called. Engines are released before `run()` returns, not by a static
  destructor.
- `create_window` has a raw form, a root-builder form and a facade form. The facade form builds a
  concrete component over the window's engines and returns the facade without a downcast.
- `post` is thread-safe and returns a `TaskHandle`; the overload taking a `LifetimeToken` drops the
  task if the guarded object has been destroyed.
- Handlers are installed at application level. `set_close_handler` is the veto path;
  `set_closing_handler` runs while the tree is still valid so a host can detach borrowed pointers;
  `set_closed_handler` runs after teardown and must not touch the tree.
- A window created inside a frame pass is deferred; teardown never reads a stage after release.

### 5.2 Windows

- `WindowRole` is creation-time and immutable: `Normal`, `Dialog`, `Tool`, `Popup`.
- `Modality` is a framework-enforced behavioural constraint: `None`, `WindowModal`, `ApplicationModal`.
  `WindowModal` requires a live parent. The gate drops input before dispatch, so commands and the
  host observer never see a blocked event.
- `WindowOptions` carries title, size, role, modality, parent, logical popup anchor, visibility,
  resize flag and dialog conventions. An owned window closes with its owner; transients are torn
  down before dialogs, deepest first. A vetoed close leaves the subtree open.
- Opening a dialog activates it and remembers the focused parent control by author `id`; closing it
  activates the parent and restores that control.
- A `WindowHandle` is non-owning. Application code never holds an owning raw `Window*`.

### 5.3 Platform HAL

`PlatformWindow` and `PlatformEventLoop` are the only seams through which the framework reaches an
OS. Public declarations live in portable headers; platform implementations carry the platform infix.
A platform adapter owns native window lifetime, DPI metrics, appearance, input translation, surface
handoff and accessibility attachment. The headless adapter has no native window: it exposes
synthetic metrics, explicit `raise_resize` / `raise_event` / `raise_close_request`, an empty native
handle, and explicit frame pumping. It is a test surface, not an application surface.

A surface that returns to screen after a hide repeats the platform's map handshake before the
platform attaches a buffer: the platform commits the surface without a buffer and dispatches the
compositor's answer, and the CPU window surface drops a frame built before that cycle completes
instead of committing it. A connection whose display reports a fatal error is never handed out
again, so a protocol failure costs the surface that raised it, not the process.

---

## 6. Rendering and frame pipeline

### 6.1 Two-stage frame

1. **Stage 1 — UI thread:** evaluate bindings, run layout when dirty or when `WindowMetrics`
   changed, visit visible nodes, record an immutable `FrameScene`.
2. **Stage 2 — render executor:** replay the scene, submit to the graphics queue and present. Stage 2
   never calls application code and never mutates UI state.

`FrameScene` carries the metrics it was built for, a background colour, an optional damage rectangle,
a culling flag and the ordered `DrawCommand` list. A frame whose metrics do not match the surface is
dropped, never stretched. A surface that retains its target can cull draws outside the damage region;
a scene refused on such a surface is rebuilt whole.

### 6.2 Render HAL and vocabulary

The HAL is declared in `albor/render/render_context.h`, `albor/render/renderer_backend.h` and
`albor/render/draw_command.h`. `RendererBackend::execute_frame` returns a `FrameOutcome` that
distinguishes presentation from refusal for stale metrics or missing retention.

`DrawCommandKind` is the complete, shared command vocabulary for frames and retained pictures:
`Rect`, `RoundedRect`, `Polyline`, `Text`, `LineBatch`, `Path`, `Picture`, `Image`, `Mesh`,
`GlyphOutline`, `Shadow`, `LayerPush`, `LayerPop`. Every backend and the software reference replay
the same list in order; none may reorder, reinterpret a clip, or skip a non-empty command.

`RenderContext` is the single drawing contract for widgets and custom nodes. It includes primitives
for rectangles, rounded rectangles, polylines, shadows, text, layers and opacity, plus retained
resources (`LineBatch`, `PathResource`, `PictureResource`, `ImageResource`, `MeshResource`,
`GlyphOutline`). A new primitive must be added to the HAL, every recorder, all three native backends,
the Blend2D reference and the parity suite in the same change.

### 6.3 Backends

- `render-albor` provides Metal, Direct3D 11 and Vulkan over one portable geometry core. Shaders,
  artifacts and stamps are committed and verified.
- `render-blend2d` is the portable CPU backend and windowed surface.
- `render-software` is the deterministic Test reference: it uses the same portable context, needs no
  native window, and exposes readback and a frame count for conformance tests.
- Texture reuse, base-vertex handling, batch buffering, damage replay, profiling coverage and
  parity tests are backend-parity properties (BDG-09).

---

## 7. Properties, bindings, and data flow

- `Property<T>` is the typed public property object; `Observable<T>` adds UI-thread `set` and
  thread-safe `post_set` (which reports whether posting was possible); `Snapshot<T>` is
  `std::shared_ptr<const T>`.
- Bindings are plain static functions plus declared `PropertyId` dependencies. No binding allocates
  per evaluation and no binding owns a `std::function`.
- Evaluation is eager-push over a dependency graph. A fixed evaluation budget stops cycles at
  runtime; the compiler rejects static cycles.
- Write precedence: an imperative `set` breaks the declarative binding; `rebind()` restores it;
  animation writes use a distinct presence channel and do not erase either.
- Properties are typed members of components. There is no property-name table, no slab allocator and
  no runtime reflection in production.
- Cross-thread data for custom nodes and bindings is immutable snapshots only (BDG-06).

---

## 8. UI tree, layout, input, focus, tooltips

### 8.1 Nodes and components

`UiNode` is the node contract: measure, arrange, render, hit-test, dispatch, bounds, invalidation and
parent/child access. `Component` is the composite; its `render` is `final` and runs background,
content, then children. A subclass overrides `paint_content`, never `render`. A subtree that leaves
the tree is announced to borrowed-pointer observers while it is still alive.

### 8.2 Layout

Layout is logical pixels, measured then arranged. It runs when a node is layout-dirty or when window
metrics changed; a clean node reuses its bounds. Any write that can change a measured extent must
invalidate layout, not only repaint. A `Row`/`Column` shares overflow only among children that
declare stretch on the main axis. A self-scrolling content passes its viewport height as the measure
ceiling; a virtualising content keeps an unbounded ceiling. Standard containers include `Column`,
`Row`, `Stack`, `Grid`, `ScrollView`, `Padding`, `SizedBox` and the widget-level containers.

### 8.3 Input and focus

- Platform adapters normalise input into one tagged `Event` value with a kind, button, key, text,
  position and modifiers. There is no per-device event class hierarchy.
- Hover enter/leave is synthesised from the hit-test chain, deepest first; leaving the content area
  is reported by the platform and ends the hover.
- A viewport claims the wheel only while its offset moved. A secondary press becomes a context-menu
  request; a capture remembers its arming button and ends with exactly one `PointerCancel` when
  aborted, including Escape, focus loss, hide and detach.
- Focus ownership, the keyboard focus cue and pointer capture are three independent facts. A pointer
  policy decides whether a control takes focus while preserving the cue. Losing key status hides the
  cue without dropping the owner.
- Tooltips are node state with an optional rich builder. One controller per window arms, shows,
  warms and dismisses them; presentation is an inert in-tree overlay, never a native popup. The
  accessible name or description comes from the tooltip.

---

## 9. Text and typography

- The shipped engine is the vendored HarfBuzz/ICU engine. It exposes path, embedded and platform
  creation paths. Embedded faces are registered by family and weight; a run resolves the exact face.
- `ShapedRun` is the replayable unit: glyph ids, positions, baselines and resolved faces.
  `TextLayout` is a shared immutable handle to it. Stage two draws those glyphs at those positions;
  it never reshapes or re-measures.
- The shape cache is keyed by text, wrapping width, font size, face content hash and direction. A
  cache hit must not allocate.
- Platform font resolvers exist for all three platforms. Embedded text does not depend on them.
- Cross-platform identity is a bounded claim: layout is a function of declared inputs and is exact;
  rasterisation agrees with the software reference within per-content-class budgets. Flat interiors
  and integer-aligned edges are exact; antialiased glyphs carry a measured budget. The checked-in
  layout corpus currently covers Latin, accents and combining marks; expanding its script coverage
  is gap GAP-06 in §14.

---

## 10. Semantics, accessibility, and automation

### 10.1 Semantics

Every widget publishes a `SemanticNode` with role, name, states, actions and an explicit id.
Custom views publish virtualised semantics through `SemanticProvider`. A value action and range
action write through the control's own edit path so undo and `on_change` observe one real edit.

### 10.2 Accessibility

All three platforms ship a bridge from the same semantic tree with mirrored tests:
NSAccessibility on macOS, UI Automation on Windows, and AT-SPI over D-Bus on Linux. A new
semantic role or state must reach every bridge and its conformance suite in the same change.

### 10.3 Automation and MCP

- The standard surface is: `semantic.*` (`tree`, `find`, `value`, `bounds`, `read`, `invoke`,
  `actions`, `set_value`, `set_focused`), `input.*` (`type`, `click`, `pointer`, `key`),
  `command.*` (`list`, `get`, `invoke`, `can_undo`, `undo`, `redo`) and `capture.screenshot`.
- Answers are typed: text, number, boolean, json, image or binary. The wire is line-oriented;
  arguments that need whitespace are percent-escaped.
- The host discovers through a per-process `host.json`, token and socket or named pipe, with
  local-only transport and a per-session token. Session state ends when the last bound tree goes
  away.
- MCP is a thin adapter: every tool call becomes one automation command, and no tool reaches a
  control, command object or runtime state directly (BDG-04).

---

## 11. `.ui` language and compiler

The compiler is a build-time C++ tool that parses `.ui` files, checks them, and emits concrete
facades and implementation files.

- A file imports another with a quoted relative path or a dotted module name and exports one
  component named after its stem. An imported component may be instantiated but not used as a base.
- Components declare typed properties, signals, local `struct`/`enum` types, `fn` functions and
  `global` values. A handler body is a closed grammar: assignment, signal invocation, a small set of
  built-in methods, and record field writes. A handler cannot call arbitrary application code.
- `if` instantiates only while true; `visible:` keeps identity and state but removes layout and
  drawing. `Repeat` materialises rows; a handler inside a repeated body is one shared callback and
  cannot capture the row value directly.
- `@tr("…")` marks translatable text; `action:` or `command:` binds a shared command and `invoke()`
  fires it from a handler. `tooltip:` is a common element property.
- Manifests declare native components, their properties and signals, their generated header and
  factory. The compiler never parses C++ headers (BDG-07).
- Generated headers are non-virtual concrete facades written only when their content changes. A body
  edit rebuilds only its own translation unit; an interface edit rebuilds the header and its
  includers. The CMake `IMPORTS` helper orders generated units. Codegen output must already pass the
  repository format check.

---

## 12. Development tooling

- The dev hook is a fixed set of registration functions present in every configuration. The dev
  runtime and automation host register independently, so a coverage build runs both.
- The dev runtime parses a serialised IR tree and applies a reload keyed by explicit `id`, producing a
  reload report. No out-of-process dev service delivers definitions yet (GAP-03).
- The preview host builds a checked `.ui` tree with the real runtime. The LSP provides diagnostics,
  definition, document symbols, semantic tokens and editor completion/hover support; full
  component-aware completion, references and rename are still open (GAP-04).
- The VS Code client lives under `tools/`. Automation is a Dev/Test link choice, never part of the
  public application target.

---

## 13. Verification and acceptance

### 13.1 Standard verification

| Check | Command |
| --- | --- |
| Configure | `cmake --preset debug` |
| Build | `cmake --build --preset debug` |
| Unit tests | `./.build-debug/tests/albor-tests` |
| Format/lint | `bash build/check-format.sh` |
| Link boundary | `bash build/check-link-boundary.sh` (after the release build; scans `.build-release/albor`, or pass one archive) |

Additional test binaries are built where a platform or feature needs isolation; the authoritative
list lives in `tests/CMakeLists.txt`. `docs/status.md` records the current per-platform support
levels and CI results.

### 13.2 Conformance suites

A feature is accepted only when the relevant cross-cutting suites pass:

- **Dual-engine**: the same `.ui` corpus through compiled output and the dev IR evaluator.
- **GPU/software parity**: every resource kind and primitive agrees with the software reference.
- **Native backend parity**: Metal, Direct3D 11 and Vulkan carry mirrored suites; a backend-only test
  gap is a defect (BDG-09).
- **Platform parity**: macOS, Windows and Linux carry mirrored platform suites; a platform-only test
  gap is a defect (BDG-10).
- **LSP/compiler agreement**, **manifest consistency**, **semantic tree**, **build scope** and
  **automation compatibility** suites as specified by their owning modules. The automation
  compatibility suite drives a running application out of process through the Python harness in
  `tools/automation-harness/` (§10.3).

A comparison that reads nothing from one side is not an agreement. A tolerance is written and
derived from measurement, not guessed. A suite that cannot run is recorded, not shipped red.

### 13.3 Requirement for every change

1. State which contract clause the change implements or alters.
2. Carry the equivalent change to the other two platforms and the other two native backends, with
   the same tests, unless the change is provably portable (BDG-09, BDG-10).
3. Add or update self-contained tests.
4. Update `docs/status.md` when implementation levels change, and this document when a contract
   changes.
5. Run the standard verification commands and fix every warning and format finding.
6. Cite this document's clause identifiers and section numbers in commits and reviews.

---

## 14. Open gaps

These are not part of the current contract. A change that closes one must update this section and
`docs/status.md` in the same change.

- **GAP-02** RTL layout mirroring and text-level bidi/caret navigation (#98, #99).
- **GAP-03** Out-of-process dev service that delivers reload definitions to `devrt`.
- **GAP-04** Full LSP component-aware completion, hover, references and rename (#126–#129).
- **GAP-05** Input coalescing, touch/pen event model and focus scopes.
- **GAP-06** Script coverage of the golden layout corpus (non-Latin scripts) and formal pixel-identity
  budgets per content class.
- **GAP-07** Per-argument values carried by `.ui` handler `invoke()` calls.
- **GAP-08** Subtree-level relayout and cached bounds side table. Damage-region repaint already ships;
  subtree-level relayout does not.

---

## 15. Amending this document

This document is versioned independently of any earlier specification. An amendment must:

1. state the clause affected and the reason;
2. keep the number of *must* clauses unchanged or explicitly record the removal;
3. update the affected code and tests in the same change;
4. add a dated entry to the revision history below;
5. keep the reference crosswalk in §16 current when an identifier is retired.

The audit completed on 2026-11-21. This file is the contract referenced by `AGENTS.md`,
`README.md` and `docs/index.md`; §16 resolves the historical identifiers that older
scripts, workflows and issues still carry.

---

## 16. Legacy reference crosswalk

Architecture decisions and suite numbers were recorded under identifiers that predate this
contract. Those identifiers remain in build scripts, workflows, issue titles and git history.
They are **not** additional normative clauses: the clauses named below are the authority, and
new work cites this document's `BDG-NN`, `GAP-NN` and § numbers only.

| Historical identifier cluster | Current clause |
| --- | --- |
| `AD-01`, `AD-02`, `AD-04` | §12 — dev runtime, hot swap and their link-time exclusion |
| `AD-05`, `AD-06`, `AD-26` | §6.3, §6.4, BDG-09 — native backend parity, retained resources and conformance |
| `AD-15`, §18 | §7, §8.3 — commands, actions and input routing |
| `AD-16`, `AD-22` | §5.3, BDG-10 — platform parity, CI images, floors and Linux CI details |
| `AD-19` | §4, §13.2 — build and distribution, package and consumer evidence |
| `AD-25`, `AD-34` | §9.1, §6.4 — embedded text, outlines and text conformance |
| `AD-27` | §6 — density is a property of the surface |
| §16 (conformance suites) | §13.2 — cross-cutting conformance suites |
| §27 (coverage gate) | §13.1, §13.3 — verification commands and per-change requirements |

## Revision history

| Version | Date | Change |
| --- | --- | --- |
| 1.0 | 2026-11-20 | Initial contract drafted from the verified current tree. |
| 1.1 | 2026-11-21 | Audit completed: authoritative status, legacy reference crosswalk, pre-release gap decisions recorded in `status.md` (#263). |
| 1.2 | 2026-11-22 | §5.3 records the remap handshake a hidden surface repeats before presenting, and the eviction of a connection whose display reports a fatal error (#271). |
| 1.3 | 2026-11-24 | §13.2 names the out-of-process Python automation compatibility harness; GAP-01 (#25) is closed with the harness, its fixture app and three-platform CI; `status.md` drops it from the stable-tag blockers. |
