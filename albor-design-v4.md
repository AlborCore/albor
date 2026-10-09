# Albor — C++ Declarative UI Framework

## Architecture and Design Specification — Version 4

**Status:** Implementation Specification  
**Version:** 4.3  
**Date:** 2026-10-05 (4.0) / 2026-10-08 (4.1, 4.2, 4.3)  
**Project:** AlborCore / Albor  
**Supersedes:** v3.0 (and closes Phase 0 exploratory prototyping)

### Revision Log

**4.3 — the product objective, added to the specification.** An application must render the same
pixels on macOS, Windows and Linux, text included. That requirement was not stated anywhere in this
document, and AD-26 answers a different and weaker question. §15 records it, AD-34 registers it, and
§16 is the conformance section that earlier revisions of this document cited as §27.

The consequence worth noticing is that **compile time glyph embedding stops being an optimisation and
becomes a precondition**, because three platforms do not resolve the same system font. §15 also splits
the claim in two, because layout identity is exact and achievable while rasterization identity is not,
and conflating them would let the goal be quietly missed.

**4.2 — the headless platform and the CPU reference backend.** AD-26 mandated a software rasterizer
reference and AD-16 mandated a headless platform; both modules now exist. §5.3 states what headless
is and is not, §6.3 states the three properties that make the reference contractual, and §6.2 drops
`resource_factory()`, which the previous revision declared and the interface never had.

**4.1 — clarifications from Phases 1 to 3 implementation.** No settled decision was reopened. Five
sections were amended because the text permitted a defect that was then found and fixed, and one
section was added because the module it describes had no written contract at all. Every addition
is marked "settled during implementation" in place, and AD-29 through AD-33 record them.

| Section | Change | Reason |
| :--- | :--- | :--- |
| §5.3 | Surface ownership rule; stale-metric frames are dropped | resize was running on a thread that did not own the layer |
| §6.1 | The scene command vocabulary, defined | a scene had no command type; frames and pictures needed one shared list |
| §6.2 | Replay obligation of the backend | the vocabulary needed a stated execution contract |
| §8.1 | The `Component` composite; `render` declared `final` | widgets as leaves silently dropped their children |
| §8.2 | When the layout phases run, plus the size-change invariant | the layout dirty flag had no consumer; a repaint-only invalidation would have kept stale bounds |
| §8.3 | Synthesized hover transitions; detachment announced before the mutation | a hover is not derivable from raw moves; a removed hovered node left a dangling pointer |
| §8.4 | **New.** Style and theming contract | `style/` was named in §3.2 but had no written contract |
| §9.1 | `ShapedRun` as the replayable unit; cache lookup must not allocate | measured and drawn text could disagree; a cache key that owned its string paid per lookup |
| §5.3 | What headless is and is not | a test surface and a headless application surface are different things |
| §6.2 | `resource_factory()` removed from `RendererBackend`; the declaration was never implemented | the spec showed a method the interface does not have |
| §6.3 | The CPU reference backend and its three contractual properties | AD-26 made the module mandatory, and its portability is the point |
| §15 | **New.** Cross-platform pixel identity, and its dependency order | the objective was stated and mandated nowhere |
| §16 | Renumbered from §15; the conformance suites the earlier revisions cited as §27 |

Two conventions are worth stating, because both were arrived at by correcting a mistake:

- **A document that describes intent rather than the tree will be believed.** A status line once
  described a contract a header did not contain, and no test failed, because tests pass either
  way. Where this document states a rule, the rule is stated as the code enforces it.
- **A doc comment form the comment rule exempts is still missing.** `STYLE-GUIDE.md` forbids
  consecutive comment lines, so a multi-line contract cannot be written in a header where a caller
  would read it. The affected contracts live in the canvas draw policy issue instead. This is a
  gap in the style guide, not a settled position.

---

## 0. Document Purpose and Changes from v3

### 0.1 Purpose of Version 4

Version 3 of this document recorded an extensive series of exploratory Phase 0 prototypes (A1, A1-Dawn, A1-Parity, A1-Graphite-Dawn, A1-Build-Cost, A2, A2-macOS, B, B2, D, D2, D3, E). While these experiments provided valuable data on micro-scale performance, they introduced severe analysis paralysis: dozens of open sub-questions, micro-benchmarks, and circular dependency gates stalled the actual implementation of the Albor framework.

**Version 4 fundamentally shifts the project from exploratory prototyping to production implementation.**

The load-bearing architectural principle of Version 4 is **Decisive Abstraction & Modular HALs**:
1. **Abstract Foundational Subsystems Behind Stable Interfaces:** Rendering, windowing, platform event dispatch, property storage, and text shaping are isolated behind clean, abstract C++ interfaces.
2. **Settle Open Questions with Production-Grade Defaults:** Rather than waiting indefinitely for micro-benchmarks or platform parity across every OS before starting, Version 4 selects battle-tested, high-performance defaults for Phase 1.
3. **Preserve Future Swappability:** Because interfaces are strictly decoupled and no third-party or platform types leak into public headers, any subsystem (such as the rendering backend or property index structure) can be optimized or swapped in the future without breaking user application code or `.ui` component syntax.
4. **Archive Prototype History on `prototypes` Branch:** The extensive nanosecond-level micro-benchmark logs, hardware load caveats, and prototype-specific debug code from Phase 0 are concluded and preserved exclusively on the `prototypes` git branch for historical reference; Version 4 focuses purely on framework architecture, component contracts, and implementation engineering for the clean production branch.

### 0.2 Summary of Major Architectural Decisions in v4

| Subsystem | Decision in v4 | Rationale & Swappability |
| :--- | :--- | :--- |
| **Rendering Backend** (AD-05, AD-28) | **Skia is the baseline rendering backend for Phase 1.** | Skia provides mature HarfBuzz text shaping, font fallbacks, complex 2D vector path rasterization, GPU acceleration (Metal/DirectX/Vulkan), and software rasterization out of the box. Google Dawn/WebGPU is preserved as a future pluggable backend candidate (`render-dawn`) behind `RendererBackend`, avoiding the multi-year task of rebuilding a 2D path/text engine from scratch. |
| **Custom Rendering** (AD-06) | **Level 2 retained & batched immediate primitives via `RenderContext`.** | Retained immutable GPU-managed geometry (`LineBatch`, `PathResource`, `Picture`) plus batch immediate calls. Level 3 (custom app shaders/compute) remains deferred to future iterations behind the same abstraction. |
| **Property Storage & Evaluation** (AD-04a/b/c/d) | **Typed per-component storage with Eager-Push topological evaluation.** | Public properties use `Property<T>`. Components store properties as typed member fields with component-local offsets. Evaluation uses Eager-Push with topological/dirty propagation, which guarantees sub-millisecond settle times and completely avoids the stale-reconvergence correctness defects measured in hybrid pull. Internal index representation is an engine implementation detail. |
| **Windowing & Platform** (AD-08, AD-16, AD-21) | **Abstract `PlatformWindow` and `PlatformEventLoop`. Initial platform backend: macOS (AppKit + Metal + `CVDisplayLink`).** | Public APIs (`Application`, `Window`, `WindowHandle`) know zero platform details. macOS is implemented first using AppKit with CoreVideo display-link pacing (proven at 60 Hz in A2-macOS), followed by Win32 and Linux Wayland/X11. |
| **Production Execution Model** (AD-01, AD-03) | **No interpreter in Release. Concrete facade headers, write-if-changed code generation.** | Production compiles `.ui` directly to native C++. Public headers expose clean, non-virtual facades. Component translation unit batching is a configurable compiler option. |
| **Layout Invariance & Dev Extension** (AD-02, AD-04) | **Memory layout is byte-identical across Release, Dev, and Test.** | Dev/Test features (`albor-devrt`, automation core) attach solely via link-time symbol inclusion and null-checked extension points, strictly enforced without `#ifdef` in runtime class layouts. |
| **Identity & Semantics** (AD-09, AD-11, AD-20) | **Explicit `id:` is identity; one unified semantic tree.** | Automation and accessibility consume the same semantic model. Automation is excluded from Release at link time. MCP routes exclusively through the automation layer. |

---

## 1. Purpose and Product Requirements

Albor is a modern, high-performance, cross-platform C++ declarative UI framework designed for desktop applications that demand:
- **Native C++ production builds** with zero interpreter or reflection overhead in Release;
- A **compact, readable declarative `.ui` language** that compiles directly to optimized C++;
- **Fast incremental compilation** and clean separation of interface and implementation;
- **GPU-accelerated rendering** alongside first-class support for high-throughput custom views (CAD schematics, waveforms, plots, scientific visualizations);
- **Robust multi-window behavior**, proper window modality, and native OS integration;
- **Serious text editing, Unicode shaping, IME support, and accessibility**;
- Modern developer tooling including language server (LSP), preview, and live reload in development builds.

Albor is engineered so that applications combining conventional desktop controls (buttons, menus, forms, tables) with intensive, domain-specific visual rendering operate under a unified, elegant reactive architecture.

---

## 2. Non-Negotiable Boundaries

The following architectural invariants are mandatory. Any violation is considered an architectural defect:

1. **No Interpreter in Release (AD-01, §2.1):** Production builds contain no `.ui` parser, no IR evaluator, no expression interpreter, no type registry, no property-by-name reflection, and no automation channel.
2. **Layout Invariance (AD-02, §2.3):** Runtime class layout and virtual method tables are byte-identical across Release, Dev, and Test configurations. Dev and Test features attach exclusively through registration hooks present in all configurations (null pointer checks), side tables keyed by node identity, and separately linked translation units. Class layout or inline methods must never vary under `#ifdef`.
3. **Link-Time Exclusion of Dev/Test Code (AD-04, AD-10):** Features specific to Dev (such as `albor-devrt`) or Test (such as `albor-automation`) are excluded at link time, not disabled by runtime boolean flags. A Release build referencing an excluded symbol must fail at link time.
4. **No Platform or Renderer Types in Public Headers (§3.5, AD-05):** Public headers declare only Albor framework types. No Skia types, Metal/DirectX/Vulkan objects, AppKit `NSView*`, Win32 `HWND`, or Wayland surface pointers appear in public framework APIs.
5. **Explicit Identity (AD-11, §18.1):** A node's `id:` is its authoritative identity. Compiler-generated implicit keys are correlation hints for development live reload and must never be used by automation selectors or semantic queries.
6. **Two-Stage Frame Pipeline (AD-18, §10.2):** Frame generation is divided into an application/UI-thread **Scene Build** stage that outputs an immutable `FrameScene`, and a decoupled **Render Execution** stage that executes GPU drawing commands without calling application code.
7. **Immutable Snapshots Across Threads (§10.5, §13.2):** Worker threads communicate with the UI thread strictly via immutable data snapshots (`std::shared_ptr<const T>` in an observable `Snapshot<T>`). Workers never mutate active UI objects.
8. **Manifest as Single Source of Truth (AD-17, §15.1):** Native C++ components exposed to `.ui` are declared in native manifests. C++ glue code is generated from the manifest; the compiler never parses C++ application headers.
9. **Automation Security and MCP Routing (AD-10, AD-20):** Automation is restricted to local authenticated connections with explicit application allow-lists. Model Context Protocol (MCP) tooling communicates solely through the automation layer and never accesses runtime internals directly.

---

## 3. High-Level Architecture and Modules

### 3.1 Subsystem Diagram

```text
                  +-----------------------------------+
                  |      .ui + Native Manifests       |
                  +-----------------------------------+
                                    |
                    +---------------+---------------+
                    |                               |
                    v                               v
         [ Albor Compiler ]               [ Development Tooling ]
         - Lexer / Parser / IR            - Language Server (LSP)
         - C++ Code Generator             - Out-of-Process Preview
         - Manifest Glue Generator        - Dev Service (File Watcher)
                    |
                    v
         [ Generated C++ Code ]
                    |
+-------------------v-----------------------------------------------------+
|                          Albor Production Runtime                       |
|                                                                         |
|  +--------------------+  +---------------------+  +------------------+  |
|  |    Application     |  |       Window        |  |     Platform     |  |
|  | - Event Loop       |  | - WindowManager     |  | - Native Window  |  |
|  | - Tasks & Timers   |  | - WindowMetrics     |  | - Input Adapter  |  |
|  | - Lifetime Tokens  |  | - Modality / Roles  |  | - Display Link   |  |
|  +--------------------+  +---------------------+  +------------------+  |
|                                                                         |
|  +--------------------+  +---------------------+  +------------------+  |
|  |      UI Tree       |  |     Properties      |  |  Layout & Input  |  |
|  | - Node Hierarchy   |  | - Property<T>       |  | - Measure/Arrange|  |
|  | - Component Facade |  | - Eager-Push Engine |  | - FocusManager   |  |
|  | - Event Routing    |  | - Snapshots/Signals |  | - Pointer/Key/IME|  |
|  +--------------------+  +---------------------+  +------------------+  |
|                                                                         |
|  +--------------------+  +---------------------+  +------------------+  |
|  |   Text & Styling   |  |    Semantic Tree    |  |  Render Pipeline |  |
|  | - Unicode / Shaping|  | - Accessibility     |  | - Scene Build    |  |
|  | - Themes & Brushes |  | - SemanticProvider  |  | - FrameScene     |  |
|  | - Font Fallback    |  | - Action Routing    |  | - RenderContext  |  |
|  +--------------------+  +---------------------+  +------------------+  |
|                                     |                                   |
|                                     v                                   |
|                    +----------------------------------+                 |
|                    |     Renderer HAL Abstraction     |                 |
|                    +----------------------------------+                 |
|                                     |                                   |
|               +---------------------+---------------------+             |
|               |                                           |             |
|               v                                           v             |
|      [ Skia GPU Backend ]                        [ Software Backend ]   |
|   (Metal / Vulkan / DirectX)                     (CPU Fallback / CI)    |
+-------------------------------------------------------------------------+
                                      | (Dev / Test Configurations Only)
                                      v
+-------------------------------------------------------------------------+
|                        Dev & Test Extensions                            |
|  - albor-devrt: Type Registry, IR Loader, Restricted Expr VM, Reload    |
|  - albor-automation: Out-of-Process Test Control, Token Auth, MCP       |
|  - albor-inspector: Diagnostics, Bounds Visualizer, Scene Debugger      |
+-------------------------------------------------------------------------+
```

### 3.2 Runtime Modules and Directory Layout

The repository layout directly mirrors the runtime architectural modules:

```text
albor/
├── application/         # Application lifecycle, event loop, tasks, timers, tokens
├── window/              # Window roles, modality, WindowHandle, window manager
├── platform/            # Native platform adapters
│   ├── macos/           # AppKit, CVDisplayLink, Metal surface integration
│   ├── windows/         # Win32, DirectComposition, DXGI, TSF input
│   ├── linux/
│   │   ├── wayland/     # Wayland client, libdecor, xkbcommon, EGL
│   │   └── x11/         # X11 fallback (Tier 2)
│   └── headless/        # Headless platform for CI and automation testing
├── render/              # Core rendering HAL: RenderContext, RenderSurface, FrameScene
│   ├── render-skia/     # Skia GPU and text rendering implementation
│   └── render-software/ # CPU rasterizer reference implementation
├── ui/                  # UI tree, node lifecycle, components, event routing
├── runtime/             # Reactive property storage, eager-push binding engine
├── layout/              # Two-phase measure/arrange layout contracts and containers
├── input/               # Pointer, keyboard, gestures, and event coalescing
├── focus/               # Focus traversal, scopes, focus restoration
├── text/                # Font management, text layout, shaping, bidi
├── semantics/           # Semantic tree and node descriptors
├── accessibility/       # Native accessibility adapters (NSAccessibility, UI Automation, AT-SPI)
├── widgets/             # Standard widget library (Button, Label, Slider, ScrollView, etc.)
├── commands/            # Action and command abstractions, undo/redo integration
├── model/               # Model interfaces, virtualization, selection models
├── style/               # Themes, palette, styling definitions
├── animation/           # Frame-scheduler integrated animation core
├── resources/           # Asset loading, image decoders, font registries
├── i18n/                # Localization catalogs, translated strings, RTL handling
├── compiler/            # Lexer, parser, AST, typed IR, C++ code generator
├── manifest/            # Manifest parser, validator, and C++ glue generator
├── devrt/               # [Dev only] Dev runtime extension (IR loader, live reload adapter)
├── automation/          # [Dev/Test only] Automation core, socket transport, test runner
├── inspector/           # [Dev only] Visual debugging and scene inspection
└── tests/               # Self-contained unit and conformance tests
```

*Note: Phase 0 exploratory prototype code lives exclusively on the `prototypes` git branch for historical reference. The implementation branch contains only clean production modules and tests.*

---

## 4. Application, Event Loop, and Threading

### 4.1 Application Ownership

`Application` is the central singleton that owns the primary run loop, window registry, task dispatcher, and platform integration.

```cpp
namespace albor {

class Application {
public:
    Application();
    ~Application();

    // Runs the main event loop. Blocks until quit() is called. Returns exit code.
    int run();

    // Request application termination. Thread-safe.
    void quit(int exit_code = 0);

    // Thread-safe task posting to the UI event loop.
    void post(std::function<void()> task);
    TaskHandle post(std::function<void()> task, const LifetimeToken& guard);

    // Window creation (UI thread only).
    WindowHandle create_window(const WindowOptions& options);

    // Authoritative access to application state.
    static Application& instance();

private:
    std::unique_ptr<PlatformEventLoop> m_event_loop;
    std::unique_ptr<WindowManager>     m_window_manager;
    std::unique_ptr<RendererBackend>   m_renderer;
};

} // namespace albor
```

### 4.2 Lifetime Safety and Tokens

Callbacks scheduled from background threads or timers run safely via `LifetimeToken`:
- A UI object derives from or owns a `LifetimeTokenSource`.
- When passed to `Application::post(task, token)`, if the token source has been destroyed before the task executes, the task is dropped silently without invoking dead object references.
- `Timer` is an RAII object bound to the UI thread. Destroying the timer cancels future firings immediately.

### 4.3 Nested Native Loops

Certain native operations run nested modal event loops on the OS side (such as Win32 modal resize/drag loops or macOS native modal sheets). Albor bridges nested loops by:
- Registering framework event sources (timers, task queues, display-link triggers) in all active OS run-loop modes (e.g., `NSRunLoopCommonModes` on macOS).
- Using a re-entrancy guard to detect nested loops, ensuring live resizing continuously pumps the frame pipeline to eliminate visual hitching during window drags.

---

## 5. Window Architecture and Platform Abstraction

### 5.1 Window Roles and Modality

Window role and window modality are strictly separated:
- **Role:** An immutable creation-time OS window style (`Normal`, `Dialog`, `Tool`, `Popup`).
- **Modality:** A framework-managed behavioral constraint (`None`, `WindowModal`, `ApplicationModal`).

```cpp
namespace albor {

enum class WindowRole {
    Normal,  // Standard top-level application window
    Dialog,  // Secondary window, typically with a parent
    Tool,    // Floating utility palette or inspector window
    Popup    // Temporary non-activating surface (context menu, tooltip, dropdown)
};

enum class Modality {
    None,
    WindowModal,
    ApplicationModal
};

struct WindowOptions {
    std::string   title       = "Albor Application";
    Size          size        = {800, 600};
    WindowRole    role        = WindowRole::Normal;
    Modality      modality    = Modality::None;
    WindowHandle  parent      = {};
    bool          resizable   = true;
    bool          visible     = true;
};

} // namespace albor
```

### 5.2 Window Handles

`WindowHandle` is a lightweight, non-owning handle representing an open window.
- Application code never holds owning raw pointers to `Window` instances.
- Closing a window triggers a two-step sequence: an application `CloseRequest` event (which the application may veto to confirm unsaved changes), followed by clean resource teardown and `Closed` notification.

### 5.3 Platform HAL Interface

The platform interface isolates all OS-specific window management and input dispatch:

```cpp
namespace albor {

class PlatformWindow {
public:
    virtual ~PlatformWindow() = default;

    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void set_title(std::string_view title) = 0;
    virtual void set_size(Size size) = 0;
    virtual WindowMetrics metrics() const = 0;

    // Native handle exposure for renderer surface creation.
    virtual NativeWindowHandle native_handle() const = 0;

    // Request that the window be presented.
    virtual void request_frame() = 0;
};

} // namespace albor
```

**Surface Ownership Rule (clarified during implementation):**

A render surface is owned by the thread that created the layer behind it, and **surface
resize runs on that thread**. `RendererBackend::resize_surface` (AD-05) is therefore not
callable from the render executor as an act of self repair, because the executor does not own
the layer. The UI thread holds authoritative `WindowMetrics`, applies them to the surface, and
the executor only ever receives an already-correct scene.

The consequence for the executor is a rule it must obey rather than a capability it may use: a
frame whose scene metrics do not match the surface metrics is **dropped**, not stretched. A
stale-metric frame is discarded and a fresh one is requested, so a resize can never present a
scene built against the previous surface size. This was not an abstract concern; building the
resize path on the executor produced a real defect, because the executor was the only component
that knew both the surface and the incoming metrics and was therefore given authority over both.

**Platform Implementation Strategy (AD-16 Decided):**
- **macOS:** Implemented via direct Objective-C++/AppKit integration. Presentation attaches via `CAMetalLayer`. Pacing is driven by `CVDisplayLink` running on a CoreVideo callback thread and marshaling frame scheduling safely to the UI thread.
- **Windows:** Implemented via direct Win32 APIs, using DirectComposition, DXGI swapchains, and Windows Text Services Framework (TSF) for IME.
- **Linux Wayland:** Implemented via Wayland client libraries, `libdecor` for client-side window decorations, `xkbcommon` for key mapping, and `xdg_positioner` for popup surfaces.
- **Linux X11:** Tier 2 fallback implementation using `xcb` and `xkbcommon`.
- **Headless:** No native window, synthetic metrics, and an event loop that pumps once per call
  rather than on a display link. It exists for CI testing and automation screenshots (§3.2, AD-26).

**What Headless Is and Is Not (settled during implementation):**

Headless is a **test surface, not a headless application surface.** That distinction decides its
pacing and should be stated before anyone builds on it:

- **Frames are pumped explicitly, not requested by a clock.** There is no display link, so
  `request_frame` records a flag and a caller draws it. A frame is therefore *deterministic* and
  ordered, which is exactly what a parity or screenshot check needs and exactly what a running
  application does not.
- **`native_handle()` is empty, not fabricated.** There is no platform object. A backend that
  needs one, such as the Metal backend, must refuse a headless window rather than receive a
  pointer to nothing.
- **Resize and input are synthetic and raised by the caller.** A headless window exposes
  `raise_resize`, `raise_event` and `raise_close_request`, so a test drives the same paths the
  application uses instead of reaching around the adapter.
- **It does not replace a runner.** Win32, Wayland and X11 (§3.2, AD-16) still need real CI
  runners, because "written" and "verified on the target" are different claims and only the second
  one is worth anything.

---

## 6. Rendering Subsystem and Frame Pipeline

### 6.1 Two-Stage Frame Pipeline (AD-18)

Frame processing is divided into two distinct stages:

```text
[ UI Thread ]                                       [ Render Executor ]
Input / Timer / Invalidation
       |
       v
Reactive Binding Evaluation
       |
       v
Layout Measure & Arrange
       |
       v
Scene Build (UiNode::render + CustomRenderNode::render)
       |
       v
Immutable FrameScene Built
       |
       +================ Hand-Off ================> FrameScene Consumed
                                                           |
                                                           v
                                                    GPU Command Encoding
                                                           |
                                                           v
                                                    Flush & Submit to GPU
                                                           |
                                                           v
                                                    Display Presentation
```

1. **Stage 1 — Scene Build (UI Thread):**
   - Handles incoming input, timer ticks, and posted tasks.
   - Evaluates reactive property bindings and resolves invalidated layout subtrees.
   - Visits visible UI nodes and calls `CustomRenderNode::render()`.
   - Produces an immutable, fully self-contained `FrameScene`.
2. **Stage 2 — Render Execution (Render Thread / Executor):**
   - Receives the immutable `FrameScene`.
   - Encodes rendering commands into the GPU backend or executes software rasterization.
   - Submits work to the graphics queue and triggers presentation.
   - **Crucial Rule:** Stage 2 never executes application callbacks or touches mutable UI state.

**The Scene Command Vocabulary (AD-07, settled during implementation):**

A `FrameScene` is not an interface, a callback list, or a backend handle. It is **one ordered
list of `DrawCommand` values**, and that list is the only thing stage 2 executes. The
vocabulary is a tagged union over exactly the primitive kinds `RenderContext` declares in §6.2:

```cpp
enum class DrawCommandKind { Rect, RoundedRect, Text, LineBatch, Path, Picture, Image };

class DrawCommand {
public:
    using Geometry = std::variant<RectGeometry, TextGeometry, BatchGeometry,
                                  PathGeometry, PictureGeometry, ImageGeometry>;

    DrawCommandKind kind() const;
    const Rect& clip() const;      // stamped at record time, scissored by the replay
    const Paint& paint() const;
    const Transform& transform() const;  // pictures nest under it
    // ... geometry accessors, one per alternative
};
```

Four properties of this decision are load-bearing, and each of them was reached by fixing a
real defect rather than by preference:

- **One vocabulary for frames and pictures.** A `PictureResource` (§6.4) stores the same
  `DrawCommand` list a frame stores. Replay for a frame and replay for a nested picture run
  through one function, so the two can never disagree about what a clip or a radius means.
  Two vocabularies is how a rounded plate ends up square in a picture.
- **The clip is stamped per command at record time, not replayed as a stack operation.**
  `save`, `restore`, and `clip` are recording-time bookkeeping inside the scene recorder. Stage 2
  receives commands that each carry the clip they were recorded under, which makes the immutable
  scene genuinely immutable: a consumer can reorder, filter, or cache commands without
  reconstructing a transform stack that was never sent.
- **Order is the payload.** There is no category field and no per-kind list. Paint order is the
  list order, which is the only order a 2D compositor can honour.
- **Cull at record time.** A command whose clip and paint are empty is dropped during recording,
  so an invisible node costs nothing in stage 2 and the frame carries no dead weight.

This is the concrete form of AD-07 and AD-18a. It does not relax either: it is what makes them
implementable without a callback reaching across the thread boundary.

### 6.2 Rendering HAL: `RendererBackend` and `RenderContext`

The framework rendering architecture is strictly abstracted from underlying GPU driver libraries:

```cpp
namespace albor {

class RenderContext {
public:
    virtual ~RenderContext() = default;

    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void set_transform(const Transform& transform) = 0;
    virtual void clip_rect(const Rect& rect) = 0;
    virtual void clip_rounded_rect(const Rect& rect, float radius) = 0;

    // Basic 2D primitives
    virtual void draw_rect(const Rect& rect, const Paint& paint) = 0;
    virtual void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) = 0;
    virtual void draw_text(const TextLayout& layout, Point origin, const Paint& paint) = 0;

    // Batched Level 2 primitives (high-throughput custom rendering)
    virtual void draw_segments(std::span<const Segment> segments, const StrokeStyle& style) = 0;
    virtual void draw_polyline(std::span<const Point> points, const StrokeStyle& style) = 0;

    // Retained immutable GPU resources (O(1) draw call)
    virtual void draw_line_batch(const LineBatch& batch, const Transform& transform) = 0;
    virtual void draw_path(const PathResource& path, const Paint& paint) = 0;
    virtual void draw_picture(const PictureResource& picture) = 0;
    virtual void draw_image(const ImageResource& image, const Rect& dst, const Paint& paint) = 0;
};

class RendererBackend {
public:
    virtual ~RendererBackend() = default;

    virtual bool ready() const = 0;
    virtual std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) = 0;
    virtual void resize_surface(RenderSurface& surface, const WindowMetrics& metrics) = 0;
    virtual void execute_frame(RenderSurface& surface, const FrameScene& scene) = 0;
};

} // namespace albor
```

`resource_factory()` is deliberately **not** on this interface. Retained resources are created through
a `ResourceFactory` that the composition root owns (§6.4), and putting a factory accessor on every
backend would imply a backend owns one, which the level 2 contract does not require. The earlier
revision of this section declared it; the implementation does not have it and does not need it.

`RendererBackend::execute_frame` receives a `FrameScene` whose only payload is the ordered
`DrawCommand` list of §6.1, and its obligation is to replay that list in order. The replay is one
shared routine used for both a frame and a nested `PictureResource`, so the backend never has a
second interpretation of a command. A backend is free to translate the vocabulary into whatever
its native API wants, but it may not reorder commands, may not reinterpret a clip, and may not
skip a command that is not visibly empty, because all three change the picture a frame means.

`resize_surface` is subject to the surface ownership rule in §5.3: the caller must be the thread
that owns the layer, which in the shipped architecture means the UI thread.

### 6.3 Baseline Backend: Skia (AD-05, AD-28 Decided)

**Skia is the baseline primary rendering backend for Phase 1.**
- Skia provides battle-tested Metal, Vulkan, and DirectX GPU backends.
- It includes native integration with HarfBuzz for Unicode text shaping, bidi layout, font fallback, and complex path rasterization.
- Skia includes a software rasterizer (`SkBitmap` / `SkCanvas`) which directly powers Albor's software rendering engine for headless CI tests and pixel-diff regression testing (AD-26).
- **Pluggability:** Because all rendering operations route through `RenderContext` and `RendererBackend`, an alternative backend (such as Google Dawn / WebGPU or direct Metal) can be added as a separate module (`render-dawn/`) in the future without modifying any UI nodes, custom rendering components, or application code.

**The CPU Reference Backend (`render-software`, AD-26):**

AD-26 makes the software rasterizer the deterministic reference that CI compares against, and that
obliges the module to be more than a convenience. Three properties are contractual:

- **It draws through the same `RenderContext` as the GPU backend.** The Metal backend and the
  reference share one portable Skia context implementation; only the surface differs. A second
  context implementation would be a second interpretation of §6.2, which is exactly the drift the
  shared replay of §6.1 exists to prevent.
- **It needs no native window.** `create_surface` ignores the `NativeWindowHandle` entirely and
  allocates a raster from `WindowMetrics` alone. That is what makes the reference reproducible: the
  same metrics and the same immutable scene must produce the same bytes, on any machine, with no
  display attached.
- **The portable half of the Skia module builds everywhere.** Only the Metal backend is Apple-only.
  A CI runner is not an Apple machine, so gating the whole module on `APPLE` would have made the
  reference untestable on the platform it exists for (§3.5, AD-16).

The reference also exposes what the HAL does not: `read_pixels()` for a screenshot comparison and a
finished-frame count for a test to synchronize on. These are concrete-backend affordances for the
conformance suites, not part of `RenderSurface`.

### 6.4 Custom Rendering Contract (Level 2)

High-performance domain views (CAD editors, circuit schematics, multi-channel waveforms) inherit from `CustomRenderNode`:
- Custom rendering uses **retained immutable GPU batches** (`LineBatch`, `PathResource`, `PictureResource`) created off-thread via a thread-safe `ResourceFactory`.
- During pan/zoom, the node only updates its view transform and calls `context.draw_line_batch(batch, transform)`. This yields constant $O(1)$ CPU scene-build time regardless of geometry size (millions of segments).
- Dynamic data updates (e.g. streaming waveform data) are prepared on worker threads and handed to the UI thread as immutable `Snapshot<T>` pointers.

---

## 7. Reactive Property and Binding System

### 7.1 Public Property Types

Applications and components declare reactive state using clean, type-safe templates:

```cpp
namespace albor {

template <typename T>
class Property {
public:
    Property() = default;
    explicit Property(T initial_value);

    // Read current value (UI thread).
    const T& get() const;
    operator const T&() const { return get(); }

    // Imperative assignment. Breaks any existing active declarative binding.
    void set(T new_value);
    Property& operator=(T new_value) { set(std::move(new_value)); return *this; }

    // Restore declarative binding after an imperative override.
    void rebind();

    // Unique property identifier within component instance.
    PropertyId id() const;
};

template <typename T>
class Observable {
public:
    // UI-thread mutation
    void set(T new_value);

    // Thread-safe mutation from background worker threads.
    // Posts an update task to the UI event loop safely.
    void post_set(T new_value);

    const T& get() const;
};

template <typename T>
using Snapshot = std::shared_ptr<const T>;

} // namespace albor
```

### 7.2 Storage Layout (AD-04b Decided)

- **Typed Component Member Layout:** In generated C++ and native components, properties are declared as typed member variables within the component struct or class.
- Properties possess predictable, compile-time memory offsets.
- Dynamic nodes allocate property backing stores through a lightweight component slab allocator, achieving compact memory footprint (~16–24 bytes per scalar property slot) without unnecessary heap allocations.

### 7.3 Invalidation and Evaluation Model (AD-04a, AD-04c Decided)

- **Eager-Push Evaluation:** When a property changes, the reactive engine immediately propagates dirty state along the declared dependency graph using a topological sort pass.
- **Why Eager-Push:** Phase 0 prototyping (D, D2, D3) proved that Eager-Push settles realistic UI graphs in microseconds (< 0.1 ms) and completely avoids the stale-reconvergence bugs that plagued hybrid push-pull models.
- **Cycle Detection:** Static dependency cycles are detected and rejected at compile time by the `.ui` compiler. Dynamic runtime cycles are halted by an execution pass budget guard (maximum 64 iterations) and flagged with a diagnostic assertion.
- **Write Precedence (AD-24 Decided):**
  1. Imperative C++ assignment (`property.set(...)`) overrides and breaks a declarative binding.
  2. Calling `property.rebind()` reinstates the declarative binding.
  3. Active property animations write through an animation presence channel, allowing animations to run without permanently erasing user bindings or imperative overrides.

---

## 8. UI Tree, Layout, and Input System

### 8.1 Node Hierarchy and Component Facade

Every visual element in an Albor scene is a `UiNode`. Native and generated components derive from `Component`:

```cpp
namespace albor {

class UiNode {
public:
    virtual ~UiNode() = default;

    virtual void measure(const SizeConstraints& constraints, Size& out_size) = 0;
    virtual void arrange(const Rect& bounds) = 0;
    virtual void render(RenderContext& context) = 0;

    virtual bool hit_test(Point logical_point) const;
    virtual void dispatch_event(Event& event);

    Rect bounds() const { return m_bounds; }
    void invalidate();
    void invalidate_layout();

protected:
    Rect m_bounds;
    UiNode* m_parent = nullptr;
    std::vector<std::unique_ptr<UiNode>> m_children;
};

} // namespace albor
```

**The `Component` Composite (settled during implementation):**

`UiNode` above is the node contract, not the composition contract. Every hand-written and
generated component derives from `Component`, which is where the three phases of a composite
are fixed so that a subclass cannot accidentally drop one of them:

```cpp
class Component : public UiNode {
public:
    // the paint the background hook records, and the radius it records it with
    virtual Color background() const;      // transparent by default
    virtual float corner_radius() const;    // zero, a hard rectangle, by default

    // runs background, then content, then children; final, so no subclass may replace it (§6.1)
    void render(RenderContext& context) final;

protected:
    virtual void paint_background(RenderContext& context);   // records background()
    virtual void paint_content(RenderContext& context) {}   // the subclass's own drawing
};
```

`Component::render` runs **background, then content, then children**, and it is declared
`final`. A component overrides `paint_content`, not `render`. This exists because of a real
defect: the widgets had been written as leaves, each overriding the whole contract and none
deferring to the composite parts. The hierarchy permitted composition all along, since
`add_child` is inherited, so a leaf-only widget broke the contract **silently** — it compiled, it
rendered, and its subtree simply never appeared.

`final` is what makes the rule durable rather than conventional. A plain `override` is still
virtual in C++, so a subclass could still replace `render` and reintroduce the defect while
satisfying the compiler; `final` turns that into a compile error. Making the declaration `final`
is a change that immediately paid for itself: two call sites were using the old pattern, one in
an example and one in a test double, and both were rewritten to `paint_content` rather than
worked around.

The same reasoning governs the phase order. A plate is recorded once by the background hook and
consumed once, so a themed control changes colour by resolving a role rather than by drawing a
second shape over the first.

### 8.2 Layout Contract

Layout operates in logical coordinates (`px`) and executes a two-phase contract:
1. **Measure Phase:** Parent queries each child's minimum, preferred, and maximum dimensions given available size constraints. Text elements shape and calculate intrinsic bounds via the text engine.
2. **Arrange Phase:** Parent assigns final logical `Rect` bounds to each child.
3. Standard layout containers:
   - `Column`: Vertical linear layout with configurable spacing and alignment.
   - `Row`: Horizontal linear layout.
   - `Stack`: Layered overlay layout.
   - `Grid`: Multi-cell grid layout with row/column span support.

**When the Phases Run (settled during implementation):**

Layout is **not** a full tree pass on every frame. Each node carries a render-dirty and a
layout-dirty flag; invalidation bubbles to the root, and the scene build consults the root's
state before it measures:

- layout dirty, or the window metrics changed since the last build: run measure and arrange;
- layout clean: **skip both** and reuse the stored bounds, running only the record pass.

This is the concrete form of §6.1's "resolves invalidated layout subtrees", and it is the
difference between a colour change costing a repaint and costing a relayout of every node.

Two rules make the skip sound, and both are contracts rather than implementation notes:

1. **Every path that can change a measured size must invalidate layout, not only repaint.** A
   property write whose value can alter a node's measured extent calls `invalidate_layout()`; a
   write that only affects paint calls `invalidate()`. A repaint-only invalidation on a
   size-changing property leaves a stale bound, silently, with every test still green. The
   compiler in §11.3 generates property observers, so **the generated binding is where this rule
   is most likely to be broken**, and it is a rule the generator must therefore satisfy rather
   than one each widget satisfies by hand.
2. **A change of `WindowMetrics` forces a layout pass regardless of the flags.** The build
   remembers the metrics of the previous build and lays out again when they differ. Without this
   a resize that no handler invalidated would keep the bounds of the old surface — which the
   unconditional passes used to hide, so introducing the skip without this guard would have
   turned a forgotten invalidation into a visible layout bug.

The remaining refinement, deliberately not built, is subtree-level relayout and partial repaint:
a side table keyed by node identity holding cached screen bounds, per-item dirty tracking, dirty
regions capped at a small fixed number of rectangles, and back-buffer-age history so a
multi-buffered swapchain repaints only what changed across swaps. It needs per-node measured-size
retention and range re-recording of the command list, both of which the scene model of §6.1 now
provides.

### 8.3 Input Normalization and Focus

- **Input Events:** Native mouse, touch, wheel, key, and text input events are normalized by platform adapters into portable `PointerEvent`, `KeyEvent`, and `TextCompositionEvent` objects.
- **High-Frequency Coalescing:** High-frequency mouse moves and touch events are coalesced to one event per display frame interval, with intermediate samples preserved for precise drawing tools.
- **Focus Management:** A central `FocusManager` per window controls tab key traversal, focus scopes, focus restoration on window reactivation, and accessible keyboard navigation.
- **IME Architecture:** Text input supports active IME pre-edit composition strings, candidate windows, and cursor positioning as distinct events from physical key strokes.

**Hover Transitions Are Synthesized, Not Delivered (settled during implementation):**

A platform delivers pointer moves. A widget needs to know when the pointer *entered* and *left*
it, and those are different facts: a hover visual cannot be built from raw moves, because a move
inside a widget and a move that crosses onto it are indistinguishable to the widget. The
normalized event set therefore carries `PointerEnter` and `PointerLeave`, and the input
controller **synthesizes** them from the pointer position plus the hit test it already runs for
routing (§6.1 stage one).

The synthesis is a diff, not a resend:

- the dispatcher builds the **chain** of nodes whose bounds contain the point, deepest first,
  stopping at the topmost node that contains it;
- the controller diffs the new chain against the previous one and replays `PointerLeave` to
  every node that left and `PointerEnter` to every node that arrived;
- a move whose chain is unchanged produces no transition at all, because a hover is not a move;
- a press synthesizes the enter it needs, so a hover visual does not lag the click behind it;
- a key press synthesizes nothing, because a key carries no position.

The chain order matters and is fixed: **deepest first**. An exit has to reach the inner node
before its parent, and a transition delivered parent-first would leave a child painted hovered
while its parent was told otherwise.

**A Detaching Subtree Is Announced Before It Leaves (settled during implementation):**

The input controller holds the hovered nodes as **borrowed raw pointers**, because the tree owns
its nodes. That makes tree mutation a lifetime hazard: removing a hovered node and letting it be
destroyed leaves a dangling pointer in the chain, and the next pointer move dispatches into
freed memory. This was a real use-after-free, not a theoretical one.

The rule is therefore part of the input contract: **a subtree that is about to leave the tree is
announced while it is still alive and still in the tree.** `UiNode::remove_child` and
`UiNode::set_visible(false)` notify before the mutation takes effect; the observer replays the
exits and cuts the subtree from the chain; only then does the tree destroy anything.

The ordering is the entire fix. An observer that ran *after* the removal could not work, because
the node is already gone and cannot be told anything. This is why the notification lives in the
tree rather than in the caller, and it is the general shape to follow for any module that borrows
node pointers across a mutation.

---

### 8.4 Style and Theming (settled during implementation)

§3.2 names `style/` for themes and palettes. This is its contract, which until now existed only
as per-widget literals. The rule the module exists to enforce: **a widget resolves a colour by
role, and never carries a literal.**

```cpp
enum class ControlState { Normal, Hovered, Pressed, Disabled };

struct Palette {                       // one colour per role, per state
    Color button_plate[4];
    Color button_label[4];
    Color checkbox_fill[4], checkbox_outline[4];
    Color slider_track[4], slider_thumb[4];
    Color label_foreground[4];
};

struct ControlMetrics {                // sizing decisions, not paint
    Size button_minimum;      // a push control must stay grabbable
    Size checkbox_box;        float checkbox_outline;
    Size slider_extent;       float slider_track_thickness, slider_thumb, slider_step;
    float corner_radius;       // the shape a plate is drawn with
};

class Theme {
    const Palette& palette() const;
    const ControlMetrics& metrics() const;
    Color surface() const;
    // setters per role, per state, so a builder can write all four
};
```

Four properties are load-bearing:

1. **Resolution is by role *and* state, and the precedence is fixed:** `Disabled`, then
   `Pressed`, then `Hovered`, then `Normal`. A control that refuses input reads disabled whatever
   the pointer is doing, because that is the fact a user needs to see.
2. **State lives on the widget base, not on each control.** The transitions are handled once, so
   every control inherits hover and press for free, and no control can implement them slightly
   differently.
3. **A palette is *derived*, not written out.** `ThemeBuilder` turns four accents and a scheme
   into every entry of every role, so an application chooses an accent and a light or dark scheme
   rather than authoring sixteen literals. A hover lifts toward the text colour and a press sinks;
   the two directions differ because a press that lifted would read as a release.
4. **No theme installed means no behaviour change.** A widget with a null theme keeps its own
   values, both colour and sizing. This is a hard contract, not a convenience: it is what allows a
   theme to be introduced into a codebase with existing widgets and existing tests without a
   single behavioural difference until one is installed.

Installation is a single call, `apply_theme(root, theme)`, which walks a subtree and hands the
theme to every widget in it. It lives in `widgets/` rather than on `UiNode`, because the ui tree
must not know what a widget or a theme is (§3.2, AD-05).

---

## 9. Text, Typography, and Internationalization

### 9.1 Text Engine Contract

Text rendering and measurement are foundational to UI layout:
- `TextEngine` provides font discovery, fallback resolution, HarfBuzz Unicode shaping, and BiDi line breaking.
- **The shipped engine does not yet do this, and the divergence is recorded.** The running engine
  shapes through the renderer's bundled paragraph module rather than a vendored HarfBuzz and ICU,
  so the dependency the specification names is present only transitively. A vendored shaper is now
  built (`text/text-harfbuzz/`) and agrees with the current engine on glyph positions, but it issues
  no drawable typeface, because the renderer build exposes no portable way to create one from font
  bytes, so the drawing typeface is resolved from the platform font manager by family name while the
  shaping stays vendored. That is the boundary this phase draws: it removes the shaping dependency,
  and compile time glyph embedding is what removes font discovery. The two engines still disagree on
  where a line's baseline sits, which is what keeps the current engine in place; see the
  vendored baseline issue.
- Text measurement is computed during the layout measure phase so containers accurately adapt to localized text lengths.
- Fonts are managed as immutable resources through the `ResourceManager`.

**The Shaped Run Is the Replayable Unit (AD-14, settled during implementation):**

Measurement and drawing must agree exactly, or text is measured at one width and drawn at
another. The unit that guarantees agreement is the **`ShapedRun`**: the result of shaping one
string at one font size, carrying per-glyph ids and positions, per-line baselines, and the
typefaces each line resolved to.

- A `TextLayout` is a **shared, immutable** handle to a `ShapedRun` plus its measured box. It is
  cheap to hold and to copy, and it is what a widget stores, what the scene command carries, and
  what a picture retains.
- Stage two draws the **glyphs the shaper produced**, at the **positions the shaper produced**. It
  does not re-measure and it does not re-shape. Re-deriving glyph positions in the renderer is the
  defect that made measured and drawn text disagree, and it is why `ShapedRun` crosses the thread
  boundary as data rather than being recomputed from a string.
- Because a run is immutable and shared, a subtree drawn many times records the same handle each
  time, and a retained picture (§6.4) stores it without copying the glyph array.

**Measure Results Are Cached, and a Cache Lookup Must Not Allocate:**

Shaping is the most expensive thing in a frame, and a stable tree re-measures the same strings
every time it lays out. The engine therefore caches shaped runs keyed by the text, the wrapping
width, and the font size.

The contract has a performance half as well as a correctness half: **a cache hit must not
allocate.** A lookup is a *view* of the caller's text, compared through a transparent hash and
equality, and the string is owned only when an entry is inserted. This matters more than it looks,
because a key that owns its string pays on every lookup rather than every miss, and the layout
measure pass is a lookup. It is measured, not asserted: the allocation probe in §15 turns both
properties into test bounds.

The cache is keyed by wrapping width because a run is shaped *for* a width. A cache keyed only by
text and size would return a run shaped for a different constraint, which is a silent layout
error rather than a slow one.


### 9.2 Internationalization (i18n)

- Translatable strings are declared in `.ui` using the `@tr("text")` syntax.
- The `.ui` compiler extracts translatable strings into translation catalogs at build time.
- Current locale and reading direction (`LTR` vs `RTL`) are framework environment properties; switching locale triggers automatic reactive re-evaluation of translated bindings and layout re-arrangements without application restart.

---

## 10. Semantics, Accessibility, and Automation

### 10.1 Unified Semantic Tree (AD-09)

Albor maintains a single semantic tree alongside the visual UI tree:
- Nodes expose semantic roles (e.g. `Button`, `Checkbox`, `Heading`, `Slider`, `Canvas`), current states (disabled, focused, checked), accessible names, and actionable handlers.
- Custom rendering nodes provide semantic elements via a `SemanticProvider` interface, allowing high-scale custom views (such as individual components in a CAD schematic) to be accessible to screen readers and automated tests without materializing heavy `UiNode` instances.

### 10.2 Accessibility Adapters

Platform accessibility bridges consume the semantic tree:
- macOS: Bridges to `NSAccessibility`.
- Windows: Bridges to UI Automation (UIA).
- Linux: Bridges to AT-SPI via D-Bus.

### 10.3 Automation Core and MCP Integration (AD-10, AD-20)

- **Test Automation:** The `albor-automation` library exposes out-of-process test control via a local, token-authenticated UNIX domain socket or named pipe.
- **Release Exclusion:** The automation engine is excluded at link time from Release configurations.
- **Model Context Protocol (MCP):** AI and developer tooling agents interact with the application via an MCP server adapter that connects to the automation layer. MCP operations are subject to the same authentication and application command allow-lists.

---

## 11. Compiler, Declarative Language, and Codegen

### 11.1 The `.ui` Language Overview

The `.ui` language is a concise, type-safe declarative language for UI layout, styling, and reactive bindings:

```text
// Example: MainView.ui
import albor.controls;

component MainView {
    in property<string> user_name: "World";
    in property<int> click_count: 0;

    signal run_clicked();

    Column {
        spacing: 12px;
        padding: 16px;

        Label {
            text: @tr("Hello, ") + user_name + "!";
            font_size: 18px;
        }

        Row {
            spacing: 8px;

            Button {
                id: action_button;
                text: @tr("Increment");
                on_clicked: {
                    click_count = click_count + 1;
                    run_clicked();
                }
            }

            Label {
                text: @tr("Count: ") + string(click_count);
            }
        }
    }
}
```

### 11.2 Native Component Manifests (AD-17)

Application-defined custom views and native C++ objects exposed to `.ui` are declared in `.ui-manifest` files:

```text
// Example: chart.ui-manifest
native component WaveformView {
    in property<Snapshot<WaveformData>> data;
    in property<color> line_color: #00FF00;
    signal selection_changed(float time_pos);
    semantic role: "chart";
    preview {
        line_color: #FF0000;
    }
}
```

The Albor manifest tool generates C++ facade glue classes. The application C++ code inherits from or implements these facades. The compiler never parses application C++ headers.

### 11.3 C++ Code Generation Model (AD-03a, AD-03b Decided)

- **Concrete Facades:** For each component, the compiler generates a stable public header (`MainView.h`) exposing a concrete, non-virtual facade class.
- **Write-If-Changed:** Generated headers and implementation files are rewritten only if their content has changed, ensuring build systems (CMake / Ninja) do not trigger unnecessary recompilations.
- **Translation Unit Batching:** By default, each component generates its own `.cpp` implementation file. For large projects with hundreds of components, the compiler supports a configurable chunking option (`--tu-chunk-size=25`) to optimize clean build times without compromising incremental edit speed.

---

## 12. Development Tooling and Live Reload

### 12.1 Dev Runtime Extension (`albor-devrt`)

In `Dev` configuration, applications link the `albor-devrt` extension library:
- **Serialized IR Loader:** Loads updated component definitions compiled on-the-fly by the external compiler.
- **Reload Adapter:** Applies `UiPatch` updates to the running UI tree, reconciling node state and preserving user interaction state using explicit `id:` identifiers.
- **Layout Invariance Maintained:** The core runtime classes link against the dev extension through pre-defined registration hooks (null pointer checks), ensuring that memory layout and virtual table indices remain identical to Release builds.

### 12.2 Preview and LSP

- **Language Server Protocol (LSP):** Implemented in `compiler/` to provide autocompletion, real-time diagnostics, jump-to-definition, and semantic highlighting in editors (VS Code, Clangd).
- **Standalone Preview:** An out-of-process preview host loads `.ui` components and manifest stubs using the actual Albor runtime, giving developers instant visual feedback during design.

---

## 13. Architectural Decision Register (Settled)

Every architectural decision from Phase 0 is formally settled for production implementation:

| Decision | Subject | Status in v4 | Settled Decision & Implementation Strategy |
| :--- | :--- | :--- | :--- |
| **AD-01** | Production Execution Model | **Decided** | Native compiled C++ in Release; no interpreter, reflection, or parser shipped in production. |
| **AD-02** | Dev Runtime Extension | **Decided** | `albor-devrt` linked in Dev only; runtime memory layout is byte-identical across configurations. |
| **AD-03a**| Generated Headers | **Decided** | Concrete non-virtual facades; write-if-changed emission to guarantee fast incremental builds. |
| **AD-03b**| TU Granularity | **Decided** | Default 1 TU per component; configurable chunking flag (`--tu-chunk-size`) available for large suites. |
| **AD-04a**| Invalidation Index | **Decided** | Eager-push topological invalidation; dependency index encapsulated within `runtime/` engine. |
| **AD-04b**| Property Storage | **Decided** | Typed member layout in component classes with component offsets; dynamic slab allocation. |
| **AD-04c**| Evaluation Model | **Decided** | Eager-push evaluation with cycle budget protection; avoids hybrid pull reconvergence defects. |
| **AD-04d**| Animation Shadowing | **Decided** | Animation presence record channels; animations yield cleanly to explicit user overrides. |
| **AD-05** | Renderer Architecture | **Decided** | Fully decoupled HAL (`RendererBackend`, `RenderContext`). **Skia is the baseline backend.** |
| **AD-06** | Custom Rendering API | **Decided** | Level 2 batched primitives and retained immutable geometry batches (`LineBatch`, `Picture`). |
| **AD-07** | Render Scene Model | **Decided** | UI thread builds immutable `FrameScene`; render executor processes commands decoupled from UI code. |
| **AD-08** | Window Roles & Modality | **Decided** | Roles (`Normal`, `Dialog`, `Tool`, `Popup`) are immutable; modality is framework-enforced. |
| **AD-09** | Semantic Model | **Decided** | Single unified semantic tree; custom views provide virtualized semantics via `SemanticProvider`. |
| **AD-10** | Automation Security | **Decided** | Excluded at link time from Release; local authenticated transport and command allow-lists in Test. |
| **AD-11** | Node Identity | **Decided** | Author-assigned `id:` is authoritative identity; implicit keys are live-reload hints only. |
| **AD-12** | Live Reload Model | **Decided** | Out-of-process dev service emits serialized IR/patches; in-app reload adapter updates live UI tree. |
| **AD-13** | Virtualization & Keys | **Decided** | Model views virtualize materialized nodes; rows keyed by stable `ItemKey` identifiers. |
| **AD-14** | Text/Layout Dependency | **Decided** | Text measurement via `TextEngine` is a prerequisite contract for layout calculation. |
| **AD-15** | Command Abstraction | **Decided** | `Command` objects bind actions across buttons, shortcuts, menus, and automation. |
| **AD-16** | Platform Strategy | **Decided** | Direct platform adapters behind `PlatformWindow`. macOS AppKit + Metal + `CVDisplayLink` first. |
| **AD-17** | Native Manifest | **Decided** | Single source of truth for native components; compiler generates C++ glue and never parses C++. |
| **AD-18a**| Stage Division | **Decided** | UI thread produces `FrameScene`; render thread executes GPU drawing commands. |
| **AD-18b**| Presentation Isolation| **Decided** | Multi-window pacing decoupled via per-window display-link pacing; UI thread is never blocked. |
| **AD-19** | ABI & Distribution | **Decided** | Pre-1.0 distributes as source/CMake package with pinned toolchain; public headers expose no backend types. |
| **AD-20** | MCP Routing | **Decided** | MCP routes strictly through the authenticated automation layer; never reaches internals directly. |
| **AD-21** | Nested Loops | **Decided** | Event sources registered in all run-loop modes; re-entrant pump keeps live resizing smooth. |
| **AD-22** | Linux X11 Tier | **Decided** | Linux Wayland is Tier 1; X11 is supported as Tier 2 fallback. |
| **AD-23** | Compiler Language | **Decided** | Compiler is implemented in modern C++23. |
| **AD-24** | Write Precedence | **Decided** | Imperative write breaks binding; `rebind()` restores; animation writes through distinct channel. |
| **AD-25** | Dual-Engine Equivalence| **Decided** | Compiled C++ and Dev IR evaluator validated against a shared conformance test corpus. |
| **AD-26** | Software Parity | **Decided** | Skia software rasterizer provides deterministic reference rendering for CI headless testing. |
| **AD-27** | Units Definition | **Decided** | `px` denotes logical pixels; `phx` denotes physical device pixels. |
| **AD-28** | Candidate Primary HAL | **Decided** | **Skia is the primary HAL.** WebGPU/Dawn remains an optional future pluggable backend candidate. |
| **AD-29** | Scene Command Vocabulary | **Decided** | A `FrameScene` is one ordered list of tagged `DrawCommand` values, shared verbatim with `PictureResource`; clips are stamped per command at record time rather than replayed as a stack. |
| **AD-30** | Style Resolution | **Decided** | Widgets resolve colour by role and state through a `Theme`, never carrying literals; palettes are derived from a few accents; a null theme preserves existing values exactly. |
| **AD-31** | Composite Contract Enforcement | **Decided** | `Component::render` is not overridable: it runs background, content, then children, and subclasses override `paint_content`, so a missing phase is a compile error rather than a silently absent subtree. |
| **AD-32** | Borrowed Node Lifetime | **Decided** | A subtree leaving the tree is announced to observers while it is still alive and still in the tree; observers of borrowed node pointers must be told before the mutation, never after. |
| **AD-33** | Surface Ownership | **Decided** | A surface is owned by the thread that created its layer; resize runs on that thread, and the render executor drops a frame whose metrics are stale rather than stretching it. |
| **AD-34** | Cross-Platform Pixel Identity | **Decided** | An application renders the same pixels on macOS, Windows and Linux: fonts are embedded, shaping is vendored, layout is exact, and every platform's rasterization agrees with the CPU reference within a written tolerance. |

**How AD-29 through AD-33 came about.** None of these reopened an existing decision. They are
recorded separately because each was reached by fixing a defect that the earlier text permitted,
which means the earlier text was genuinely incomplete rather than merely undocumented:

- AD-29 came from a scene that needed a command vocabulary and did not have one; two vocabularies
  for frames and pictures is how a rounded plate becomes square inside a picture.
- AD-30 came from every widget carrying its own colours, so a design change meant editing every
  widget.
- AD-31 came from widgets being written as leaves that compiled, rendered, and silently dropped
  their children. Declaring `Component::render` `final` is what enforces it, and doing so exposed
  two call sites still using the old pattern.
- AD-32 came from a use-after-free: a hovered node removed from the tree left the input controller
  holding a freed pointer.
- AD-33 came from resize being run on the render thread, which does not own the layer.
- AD-34 is not derived from a defect. It is the product objective, added once the question of what the
  reference engine is for surfaced the fact that "deterministic in CI" and "identical across
  platforms" are different requirements with different consequences.

A settled decision is only reopened with a documented revision. These five were additions to
unstated ground, and the sections carrying them are marked "settled during implementation" so a
later reader can tell clarified text from original intent.

---

## 14. Phased Implementation Plan

With Phase 0 prototypes formally concluded, engineering proceeds directly along concrete implementation phases:

### Phase 1: Minimum Viable Framework (Current Focus)
- **Goal:** Open a native OS window on macOS, initialize the Skia GPU rendering backend, run an event loop, and render basic shapes, text, and custom 2D geometry at 60 Hz.
- **Deliverables:**
  1. `application/`: Implement `Application`, `EventLoop`, `Timer`, and task posting.
  2. `window/` & `platform/macos/`: Implement `Window`, `WindowHandle`, `PlatformWindow` using AppKit, `CAMetalLayer`, and `CVDisplayLink`.
  3. `render/` & `render/render-skia/`: Implement `RenderContext`, `RendererBackend`, and `RenderSurface` wrapping Skia with Metal GPU acceleration.
  4. Working demo executable displaying an interactive window with background, custom rectangles, text, and resize handling.

### Phase 2: UI Tree, Reactive Properties, and Semantics
- **Goal:** Implement the reactive component hierarchy and eager-push property engine.
- **Deliverables:**
  1. `runtime/`: Implement `Property<T>`, `Observable<T>`, `Snapshot<T>`, and the Eager-Push `BindingEngine`.
  2. `ui/`: Implement `UiNode`, base `Component`, and parent/child scene graph management.
  3. `semantics/`: Implement the semantic node structure and `SemanticProvider`.
  4. Unit test suite validating reactive propagation, diamond dependency graphs, and imperative override rules.

### Phase 3: Layout Engine, Basic Widgets, and Text
- **Goal:** Implement the two-pass layout contract, standard controls, and typography.
- **Deliverables:**
  1. `layout/`: Implement measure/arrange passes, `Column`, `Row`, `Stack`.
  2. `text/`: Implement `TextEngine` wrapping Skia's `SkParagraph` and HarfBuzz text shaping.
  3. `widgets/`: Implement foundational widgets: `Button`, `Label`, `Slider`, `CheckBox`, and `CustomCanvas`.
  4. `input/` & `focus/`: Implement pointer routing, keyboard navigation, and the `FocusManager`.

### Phase 4: Compiler Frontend and C++ Code Generator
- **Goal:** Compile `.ui` files and manifests directly to native C++ translation units.
- **Deliverables:**
  1. `compiler/`: Implement lexer, parser, AST, and typed IR for `.ui` syntax.
  2. `compiler/codegen/`: Implement C++ code generator producing concrete facade headers and write-if-changed implementation files.
  3. `manifest/`: Implement manifest parser and C++ glue generator.
  4. Integration with CMake build system via custom CMake rules.

### Phase 5: Dev Runtime, Live Reload, and Platform Hardening
- **Goal:** Complete developer tooling, automation test core, and multi-platform expansion.
- **Deliverables:**
  1. `devrt/`: Implement IR loader and live-reload tree patching.
  2. `automation/`: Implement automation server, token authentication, and Python test harness.
  3. `platform/windows/` & `platform/linux/wayland/`: Implement Win32 and Wayland native adapters.
  4. Language Server Protocol (LSP) and standalone preview host.

---

## 15. Cross-Platform Pixel Identity (AD-34)

**The objective, stated plainly: an application built on this framework renders the same pixels on
macOS, Windows and Linux.** Given the same theme and the same content, the three platforms produce
the same image. Text is part of that, which makes text the hardest case rather than an exception to it.

This is not currently mandated anywhere in this document, and it was not mandated when AD-26 was
settled. AD-26 asks for a deterministic reference for **CI**; AD-34 asks for **identity across
platforms**, which is a stronger and different requirement. It is recorded here because it changes
several decisions that were otherwise being made on other grounds.

### What identity requires, in dependency order

Each item below is a precondition for the one above it. Skipping one does not make the next harder;
it makes the goal unreachable.

1. **The same font bytes.** A system font is not the same file on three platforms, and font
   *versions* differ even on one. **Compile time glyph embedding (§9.1, phase 4) is therefore a
   requirement of this decision, not an optional optimisation.** This is the single largest
   consequence: the text plan's phase 4 moves from "the strongest form of the dependency free goal"
   to "the thing that makes AD-34 possible at all".
2. **The same shaper, with no platform fallback.** Vendored HarfBuzz with an explicit font and an
   explicit fallback list. A fallback chain resolved by the platform produces different glyphs on
   each platform, and every later stage then differs.
3. **The same metrics and the same line box.** Text metrics come from the vendored engine and from
   the embedded font, never from the platform's text system. This is why the vendored engine defines
   the metrics rather than matching a platform paragraph engine (§9.1).
4. **The same rounding.** Positions are quantised identically before they reach a rasterizer, because
   a half pixel difference at twenty font sizes compounds into a visibly different image.
5. **The same rasterizer, or a bounded agreement with it.** See the split below.

### The split between layout identity and rasterization identity

These are different claims, and conflating them is how this goal gets quietly missed. **The font
engine is not measured in pixels.** Its output is a layout, and a layout is arithmetic.

### The target for the font engine: exactness, and a total function of declared inputs

The engine's target is stated as a property of the program, not as a measurement between engines:

- **The engine is a total function of its declared inputs.** Text, wrapping width, font size,
  direction, locale and the embedded font's content hash are the whole input set. If nothing else can
  reach it, two platforms running the same build produce the same layout *by construction* and no
  tolerance is involved at any point. This is the whole target, and it is a stronger claim than
  "the engines agreed when we tested them".
- **Therefore no platform reaches into the engine.** No platform font manager, no system DPI, no
  ambient locale, no thread locale, no system text settings, no default font fallback. Anything an
  engine needs must be an argument. A dependency that cannot be closed becomes an explicit parameter
  rather than a silent input, because a silent input is the one thing that makes two platforms differ.
- **The embedded font is identified by content hash.** A family name, a file name and a path all
  resolve differently per platform; a hash of the font bytes cannot. The hash is part of the engine's
  input, so a swapped font is a different input and shows up as a different result rather than as a
  silent visual change.
- **Geometry is quantized to a declared grid.** Glyph positions, advances and baseline origins are
  expressed in **1/64 of a logical pixel**, which is a quarter-pixel subpixel position and is exactly
  representable in a float. The layout is then a set of integers, so equality is exact and two runs
  cannot differ by an amount too small to name.
- **Verification is a golden layout corpus, not engine against engine.** The expected layouts are
  checked-in data: for a set of inputs covering Latin, accents, combining marks, Arabic, Hebrew,
  CJK, emoji, mixed scripts and long wrapped paragraphs, the corpus holds the exact glyph sequence,
  positions, advances, baselines and extents. Every platform runs the corpus and must match it exactly.

Two engines agreeing is weaker evidence than it looks: it proves they are alike, not that either is
right. A golden corpus is the only formulation that can fail when the layout is genuinely wrong, and
it is exact, so it needs no tolerance.

### The target for rasterization: bounded, per content class, and derived

Rasterization is where the platforms genuinely differ, and the requirement is deliberately narrower
than the word "pixel perfect" suggests. **There is no single global tolerance.** A global number wide
enough to cover glyph antialiasing would also cover a three pixel baseline error, which is how a
failing suite becomes a passing one. The tolerance is therefore **per content class**:

| content | required agreement | why |
| :--- | :--- | :--- |
| flat interior fills | **exact** | no antialiasing is involved, so any difference is a bug |
| axis-aligned edges on integer positions | **exact** | likewise, the rasterizer has no freedom here |
| axis-aligned edges on fractional positions | small, one device pixel band | only the sampling rule differs |
| rounded rects and paths | small, per channel | curve flattening differs per rasterizer |
| glyph rasters | measured budget, per channel | antialiasing and hinting genuinely differ |

Two further rules make the numbers honest:

- **Every budget is derived, not chosen.** Each is written as the maximum observed across the
  backends under test, plus stated margin, and it is re-measured whenever a backend or a Skia version
  changes. A budget with no measurement behind it is a guess wearing a number's clothes.
- **The reference rasterizer defines correct output.** AD-26's software backend is the yardstick:
  every platform's output is measured against it, so "correct" is a single well-defined image rather
  than three plausible ones.

### What the conformance suite therefore asserts

1. **Layout: exact.** Every corpus entry reproduces its checked-in layout byte for byte on every
   platform. This is the strong claim and it is the one AD-34 actually turns on.
2. **Rasterization: bounded by class.** Flat interiors and integer-aligned edges are asserted exactly;
   the remaining classes are asserted against the budgets in the table above.
3. **No class is allowed a wider budget than the one above it.** The table is ordered by how much
   freedom the rasterizer legitimately has, and a regression that widens a budget is a failure rather
   than a re-baselining.

The practical consequence for the work: an engine that is a total function of its inputs needs no
cross-platform tolerance at all, so the expensive work — embedding the font and closing the input set —
is what buys the guarantee. Tolerance appears only where a rasterizer genuinely has freedom, and it is
narrower than "pixel perfect" implies.

### What this rules out

- **A per-platform font manager.** Resolving a typeface from the platform is precisely what this
  decision forbids, because the three platforms will not resolve the same file. The typeface resolver
  in `text/text-harfbuzz/` exists today as a transitional measure and is not the end state.
- **Platform text measurement.** Any measured extent taken from a platform text system is a per
  platform number, and layout follows measured extents (§8.2).
- **Platform colour management.** A per platform colour transform makes identical RGBA values render
  as different pixels, so the pipeline is fixed by the framework rather than by the OS.

### Honest limits

Bit-exact identity of *rendered glyphs* across three GPU backends is not a claim this document makes,
because it is not one Skia can currently deliver. What is claimed instead is stronger where it matters
and narrower where it does not:

- **Layout is exact and the guarantee is structural**, not measured: an engine that is a total function
  of declared inputs cannot differ between platforms, so there is no tolerance in the text pipeline at
  all.
- **Rasterization is bounded per content class**, with flat interiors and integer-aligned edges asserted
  exactly and only genuinely antialiased content carrying a budget.

A tolerance wide enough to absorb a three pixel baseline error has defeated the purpose, which is why
the baseline gap in the vendored baseline issue is a blocker and not a rounding difference. It
sits in layout, where the budget is zero.

### Consequences for work already planned

- **Phase 4 of `TEXT-RENDERING-PLAN.md` is on the critical path**, not optional.
- **The Metal teardown trap blocks a stated objective**, because the reference cannot
  be compared against a GPU backend that cannot be destroyed (§15, AD-26).
- **The vendored engine defines the metrics.** The open question in the vendored baseline issue
  is answered by this section: matching a platform paragraph engine was never the goal.

---

## 16. Cross-Cutting Conformance Suites

Unit tests cover a module against itself. The suites below cover the properties that only fail when
two modules meet, which is why each one is defined against a **corpus** rather than a single case.

| Suite | Decides | Requires |
| :--- | :--- | :--- |
| **Dual-engine conformance** (AD-25) | one `.ui` corpus produces the same observable behaviour through compiled C++ and through the Dev IR evaluator | the compiler, the evaluator, one shared corpus |
| **GPU / software parity** (AD-26) | a GPU frame and a CPU reference frame agree within a stated per-channel tolerance | both backends, `RenderSurface::read_pixels()`, a tolerance |
| **LSP / compiler agreement** | the same corpus produces the same diagnostics from both | the compiler and the language server |
| **Manifest consistency** | generated glue, descriptor tables and preview stubs agree with the manifest | the manifest, the glue generator, the preview host |
| **Semantic tree** | role, name, states and actions of every widget are declared | the semantic providers (§10.1) |
| **Build scope** (§14.3) | a `.ui` body edit rebuilds only the expected translation units | the write-if-changed generator |
| **Automation compatibility** | the reference automation test runs against a sample application | the automation core (AD-10) |

Three rules apply to every suite here, and each one was learned by a suite that could otherwise
pass dishonestly:

1. **A refused read is not an agreement.** A parity check that reads nothing from one side passes any
   tolerance trivially. Every comparison asserts that **both** sides produced a frame before it
   compares them (§6.3).
2. **A tolerance is written, not guessed.** The bound is a per-channel value stated in the suite, and
   a flat interior is expected to agree exactly while only antialiased edges may differ (§6.3, AD-26).
3. **A suite that traps is worse than no suite.** A crash takes the whole test binary down and hides
   every other failure, so a suite that cannot yet run is held out of the default build and recorded,
   rather than shipped red.

The CPU reference backend of §6.3 exists to make the parity suite possible, and it is deliberately
deterministic: the same metrics and the same immutable scene produce the same bytes.

---

## 17. Conclusion

Albor Design Version 4 closes the era of speculative prototyping and equips the project with a clean, decoupled, and actionable blueprint. By relying on strict modular abstractions and pragmatic, battle-tested defaults—starting with Skia for rendering and Eager-Push for reactive bindings—Albor is now ready for immediate production implementation.
