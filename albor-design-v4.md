# Albor — C++ Declarative UI Framework

## Architecture and Design Specification — Version 4

**Status:** Implementation Specification  
**Version:** 4.0  
**Date:** 2026-10-05  
**Project:** AlborCore / Albor  
**Supersedes:** v3.0 (and closes Phase 0 exploratory prototyping)

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

**Platform Implementation Strategy (AD-16 Decided):**
- **macOS:** Implemented via direct Objective-C++/AppKit integration. Presentation attaches via `CAMetalLayer`. Pacing is driven by `CVDisplayLink` running on a CoreVideo callback thread and marshaling frame scheduling safely to the UI thread.
- **Windows:** Implemented via direct Win32 APIs, using DirectComposition, DXGI swapchains, and Windows Text Services Framework (TSF) for IME.
- **Linux Wayland:** Implemented via Wayland client libraries, `libdecor` for client-side window decorations, `xkbcommon` for key mapping, and `xdg_positioner` for popup surfaces.
- **Linux X11:** Tier 2 fallback implementation using `xcb` and `xkbcommon`.
- **Headless:** Headless backend with no native window for headless CI testing and automation screenshots.

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

    virtual std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) = 0;
    virtual void resize_surface(RenderSurface& surface, const WindowMetrics& metrics) = 0;
    virtual void execute_frame(RenderSurface& surface, const FrameScene& scene) = 0;

    virtual ResourceFactory& resource_factory() = 0;
};

} // namespace albor
```

### 6.3 Baseline Backend: Skia (AD-05, AD-28 Decided)

**Skia is the baseline primary rendering backend for Phase 1.**
- Skia provides battle-tested Metal, Vulkan, and DirectX GPU backends.
- It includes native integration with HarfBuzz for Unicode text shaping, bidi layout, font fallback, and complex path rasterization.
- Skia includes a software rasterizer (`SkBitmap` / `SkCanvas`) which directly powers Albor's software rendering engine for headless CI tests and pixel-diff regression testing (AD-26).
- **Pluggability:** Because all rendering operations route through `RenderContext` and `RendererBackend`, an alternative backend (such as Google Dawn / WebGPU or direct Metal) can be added as a separate module (`render-dawn/`) in the future without modifying any UI nodes, custom rendering components, or application code.

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

### 8.2 Layout Contract

Layout operates in logical coordinates (`px`) and executes a two-phase contract:
1. **Measure Phase:** Parent queries each child's minimum, preferred, and maximum dimensions given available size constraints. Text elements shape and calculate intrinsic bounds via the text engine.
2. **Arrange Phase:** Parent assigns final logical `Rect` bounds to each child.
3. Standard layout containers:
   - `Column`: Vertical linear layout with configurable spacing and alignment.
   - `Row`: Horizontal linear layout.
   - `Stack`: Layered overlay layout.
   - `Grid`: Multi-cell grid layout with row/column span support.

### 8.3 Input Normalization and Focus

- **Input Events:** Native mouse, touch, wheel, key, and text input events are normalized by platform adapters into portable `PointerEvent`, `KeyEvent`, and `TextCompositionEvent` objects.
- **High-Frequency Coalescing:** High-frequency mouse moves and touch events are coalesced to one event per display frame interval, with intermediate samples preserved for precise drawing tools.
- **Focus Management:** A central `FocusManager` per window controls tab key traversal, focus scopes, focus restoration on window reactivation, and accessible keyboard navigation.
- **IME Architecture:** Text input supports active IME pre-edit composition strings, candidate windows, and cursor positioning as distinct events from physical key strokes.

---

## 9. Text, Typography, and Internationalization

### 9.1 Text Engine Contract

Text rendering and measurement are foundational to UI layout:
- `TextEngine` provides font discovery, fallback resolution, HarfBuzz Unicode shaping, and BiDi line breaking.
- Text measurement is computed during the layout measure phase so containers accurately adapt to localized text lengths.
- Fonts are managed as immutable resources through the `ResourceManager`.

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

## 15. Conclusion

Albor Design Version 4 closes the era of speculative prototyping and equips the project with a clean, decoupled, and actionable blueprint. By relying on strict modular abstractions and pragmatic, battle-tested defaults—starting with Skia for rendering and Eager-Push for reactive bindings—Albor is now ready for immediate production implementation.
