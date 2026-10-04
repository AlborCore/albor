# Albor — C++ Declarative UI Framework

## Architecture and Design Specification — Version 3

**Status:** Architecture draft
**Version:** 3.0
**Date:** 2026-10-02
**Project:** AlborCore / Albor
**Supersedes:** v2.0

---

## 0. Document Status and Changes from v2

v3 incorporates two reviews of v2. Its goals are to remove contradictions, to define the load-bearing mechanisms that v2 left open, and to make the prototype plan able to fail.

### 0.1 Status legend

Every architectural decision in §29 carries one of three statuses:

- **Decided** — settled for this architecture; changing it requires a documented revision.
- **Proposed** — a concrete recommendation made in v3 that must be validated by the named prototype or review before it is treated as decided.
- **Open** — no recommendation yet; the decision criteria are stated.

Body text uses "must" and "should" accordingly: a "must" backed only by a Proposed decision is a requirement of the proposal, not yet a commitment.

### 0.2 Summary of major changes

| Area | Change in v3 |
| --- | --- |
| Core constraint | The "no interpreter / no reflection" rule is made precise. A separate **dev runtime extension** is introduced for the reload adapter, expression evaluator and type registry. A **layout-invariance rule** keeps runtime ABI identical across build configurations (§2, §17). |
| Custom rendering | Level 2 is defined concretely: retained, immutable, thread-safely created render resources plus batched immediate primitives. Level 3 is the deferred app-defined buffer/shader tier. Render threading and data ownership are specified (§10, §11). |
| Frame pipeline | Frames are split into a UI-thread **scene build** stage and a **render execution** stage with per-window pacing, addressing multi-window presentation stalls (§10). |
| Semantics and automation | Custom render nodes can expose virtualized semantic children. Automation is excluded from Release builds at compile time. MCP routes only through the automation layer. The reference test is restored as a compatibility target (§19, App. B). |
| Identity | Explicit keys are *identity*; compiler-generated implicit keys are *correlation hints* only. Models provide item keys (§18, §20). |
| Windows | Role and modality are separated. `WindowHandle` replaces raw references. Option structures are completed. Nested native loops are specified (§7, §8). |
| Native components | The manifest is the single source of truth; C++ glue is generated from it. The compiler never parses C++ (§15). |
| Phasing and prototypes | The semantic tree and an automation core move to Phase 2. Resources, animation and i18n core move earlier. A binding-engine prototype (D) and a dev-runtime prototype (E) are added. Prototype A is split. Prototype B gains a baseline (§30, §31). |
| Decision register | Expanded from 16 to 28 entries, including ABI and distribution, render threading, MCP routing, X11 tier, compiler language, and unit naming (§29). |

Appendix A maps each review finding to the section that addresses it.

---

## 1. Purpose

Albor is a cross-platform C++ declarative UI framework for desktop applications that need:

- native C++ production builds;
- a compact declarative `.ui` language;
- fast incremental builds;
- GPU rendering and first-class custom rendering;
- multi-window behavior;
- serious text, input and accessibility support;
- modern tooling.

Albor is inspired by declarative systems such as Slint but is an independent architecture. Rust is not part of the production runtime.

The framework is particularly suited to applications that combine conventional desktop UI with high-performance custom views: schematic and CAD editors, waveform and plot viewers, scientific visualization, and simulation front ends.

---

## 2. Core Constraint and Build Configurations

### 2.1 Core constraint

Production `.ui` code is compiled to native C++. A production (Release) application does **not** contain:

- a `.ui` parser or compiler frontend;
- an IR evaluator or expression interpreter;
- a type registry or property-by-name reflection facility;
- an automation or inspection control channel.

Development tooling may use richer representations and dynamic mechanisms. Those mechanisms live in separate libraries that are linked only in Dev and Test configurations (§2.2).

### 2.2 Build configurations

| Configuration | Contents |
| --- | --- |
| **Release** | Albor runtime, generated application code, semantic tree and platform accessibility adapters. No dev runtime, no automation core, no inspector, no descriptor tables. |
| **Dev** | Release plus the **dev runtime extension** (`albor-devrt`): type registry, IR loader, restricted expression VM, reload adapter, inspector hooks, generated descriptor tables, and optional automation core. |
| **Test** | Release plus the **automation core**, descriptor tables (type names for selectors), the headless platform backend and the software renderer. |

The automation core and descriptor tables are excluded from Release **at compile/link time**, not by a runtime flag.

### 2.3 Layout-invariance rule

The memory layout and virtual interface of runtime classes must be identical in all configurations. Dev and Test features attach through:

- registration hooks that exist in every build (a null-checked extension point whose cost in Release is a pointer test);
- side tables keyed by node identity;
- separately linked libraries and separately generated translation units.

They must never attach by changing class layout or inline code under a `#ifdef`. This lets one prebuilt runtime serve all configurations and prevents "works in Dev, differs in Release" ABI divergence.

### 2.4 Semantic equivalence rule

The compiled path (generated C++) and the dev path (IR evaluator) implement the same language semantics, defined once by the typed IR specification. Equivalence is enforced by a shared **conformance corpus**, a set of `.ui` programs with expected observable behavior (property values after event sequences, invalidation counts). The corpus runs through both engines in CI (§27, AD-25).

---

## 3. Design Principles

1. **Native C++ production.** No required Rust runtime, scripting engine, or `.ui` interpreter in Release.
2. **Declarative language first.** `.ui` provides components, properties, bindings, events, composition, layout, styling, resources, commands and semantic metadata.
3. **Build performance is a product requirement.** Generated code must be incremental-build friendly; generated files are written only when their content changes.
4. **Prebuilt runtime.** Framework implementation belongs in runtime libraries; one runtime binary serves all build configurations (layout invariance, §2.3).
5. **Renderer independence.** Windowing, platform integration and rendering are separate abstractions. Skia is the initial implementation, not the framework API; no Skia or platform types appear in public headers.
6. **Custom rendering is first-class.** Schematic, CAD, plot, waveform and scientific content render through the same GPU architecture and are never forced through bitmap/image conversion.
7. **One semantic model.** Accessibility and automation consume one semantic tree through separate adapters. The tree exists in Release; the automation channel does not.
8. **Development tooling is separate.** Preview, LSP, inspection and live reload do not define the production architecture.
9. **Simple UI ownership.** UI state is owned by the UI thread. Workers communicate through explicit, lifetime-safe mechanisms and immutable snapshots.
10. **Platform isolation.** Public APIs describe behavior rather than platform implementation.
11. **Explicit identity.** Anything that tests, automation or state preservation relies on must have an explicit, author-controlled identity.
12. **One source of truth per interface.** Native component interfaces are declared once (manifest); everything else is generated from it.

---

## 4. Scope, Platform Tiers and Non-Goals

### 4.1 Platform tiers

| Tier | Platforms | Meaning |
| --- | --- | --- |
| **Tier 1** | macOS, Windows, Linux Wayland | Full feature set; all prototypes and CI gates apply. |
| **Tier 2** | Linux X11 | Supported and tested via automation/headless conformance. Platform limitations (global DPI, IME stack variation) are documented rather than papered over. It is not a blocker for architectural prototypes (AD-22, Proposed). |

### 4.2 Included capabilities

GPU rendering, normal/dialog/tool/popup windows, modality, overlays, menus, dialogs, drag/drop, clipboard, file and folder dialogs, text editing and IME, accessibility, automation, models and virtualization, animation, and internationalization.

### 4.3 Non-goals

- Required Rust in production.
- An embedded production scripting runtime.
- OpenGL as a mandatory API.
- Native child surfaces as the custom-rendering mechanism.
- Requiring application custom content to become an image first.

Internal offscreen GPU targets are allowed and expected for caching, opacity, effects and composition (§10.4). The non-goal prohibits *requiring* applications to rasterize their content into images, not framework-internal layering.

---

## 5. High-Level Architecture

```text
.ui + native manifests
 |
 +-- Parser / Semantic Analysis / Typed IR
 |       +-- C++ Code Generator  (+ manifest glue generator)
 |       +-- LSP
 |       +-- Preview / Dev Service (out of process)
 |
 +-- Albor Runtime  (production)
 |       +-- Application / EventLoop / Tasks
 |       +-- Window / Platform
 |       +-- UI Tree / Properties / Bindings
 |       +-- Layout / Text / Input / Focus
 |       +-- Commands / Models
 |       +-- Semantic Tree  -> Accessibility adapters
 |       +-- Rendering
 |             +-- Scene build (UI thread) -> FrameScene
 |             +-- Render execution -> RenderSurface -> Skia / software
 |
 +-- Dev / Test additions  (not in Release)
         +-- albor-devrt: type registry, IR loader, expression VM, reload adapter
         +-- Automation core -> Python / MCP adapters
         +-- Inspector, headless platform, software renderer
```

The compiler frontend is shared by code generation and development tooling. The production runtime does not depend on the compiler frontend. The dev runtime extension consumes **serialized IR** and does not link the frontend.

---

## 6. Runtime Modules

Provisional modules:

```text
core application ui layout input focus commands model text style animation
window platform render render-skia render-software widgets semantics
accessibility resources i18n services
compiler manifest language-server preview dev-service
devrt automation inspector headless tools
tests (not a runtime module)
```

- `devrt` holds the type registry, IR loader, restricted expression VM and reload adapter. It is linked only in Dev configuration (§2.2).
- `automation` is the automation core and transport adapters. It is linked only in Dev and Test configurations.
- `headless` is a platform backend with no native window and no GPU requirement.
- `services` provides clipboard, dialogs, notifications and drag/drop behind abstract interfaces.
- `manifest` covers the manifest parser, validator and glue generator, shared with `compiler`.

The repository structure (§32) and public module names must remain synchronized.

---

## 7. Application, Event Loop and Threading

### 7.1 Ownership model

`Application` owns the framework lifetime, event loop, window manager, renderer and platform services. There is exactly one public run loop entry point.

```cpp
class Application {
public:
    int run();
    void quit();                                          // thread-safe

    // Thread-safe task posting.
    void post(std::function<void()> task);
    TaskHandle post(std::function<void()> task,
                    const LifetimeToken& guard);          // dropped if guard expired

    WindowHandle create_window(const WindowOptions&);     // UI thread only
};

class Timer {                                             // UI thread only
public:
    explicit Timer(Application&);
    void start(Duration interval, Callback cb, TimerMode = TimerMode::Repeating);
    void stop();                                          // also invoked by destructor
};
```

### 7.2 Thread-safety table

| Operation | Thread |
| --- | --- |
| `Application::post`, `Application::quit` | any thread |
| `ResourceFactory::create_*` (§11) | any thread |
| `Observable<T>::post_set` (§13) | any thread |
| Property mutation, `Timer`, `WindowHandle` use, layout, focus, input dispatch, `CustomRenderNode::render`, `hit_test`, `SemanticProvider` | UI thread only |

The UI thread owns UI objects, layout, focus, input dispatch, property mutation and **scene build** (§10). Workers perform simulation, I/O, networking and computation and hand results back as immutable snapshots (§10.5).

### 7.3 Lifetime safety

- Every UI object that can be a callback target exposes a `LifetimeToken`. A posted task with a guard is dropped if the guard has expired when the task runs.
- Posting with a guard is the supported pattern for worker-to-UI communication. An unguarded `post` documents that the callback must not capture UI object references.
- `Timer` is RAII; destroying it cancels it.
- Cancellation and re-entrancy semantics of `TaskHandle` are explicit: cancel is idempotent, is only effective before the task starts, and never blocks.

### 7.4 Nested native loops and re-entrancy

Several platforms run nested native loops that bypass the application's run loop: Win32 modal move/size loops, macOS menu tracking and modal panels, and blocking native dialogs. Albor handles them as follows (AD-21, Decided as a principle; mechanisms per platform are implementation work):

- The platform layer registers framework event sources (timers, posted tasks, frame scheduling) so that they continue to run during nested loops (for example by registering in all relevant run-loop modes, or by using platform timers armed on nested-loop entry).
- Dispatch during a nested loop goes through a re-entrancy guard. A **nested-loop scope** marks the application state so that code can tell when callbacks arrive during native tracking.
- Application callbacks may be invoked during a nested loop. Framework APIs that are unsafe to call re-entrantly are documented, and violations are diagnosed in Dev/Test builds.
- Blocking native dialogs are avoided in favor of asynchronous APIs wherever the platform provides them (for example portals on Linux). Where only a blocking API exists, it runs inside a nested-loop scope.
- Live resize must present frames during the resize loop so that content tracks the window size.

---

## 8. Window Architecture

```text
Application -> WindowManager -> Window
                              +-- NativeWindow
                              +-- RenderSurface -> Renderer
```

### 8.1 Role and modality are separate

Role is a **platform-visible creation-time property**; platform roles may be fixed at surface creation. Modality is a **framework-enforced behavior** that is independent of role.

```cpp
enum class WindowRole { Normal, Dialog, Tool, Popup };
enum class Modality   { None, Window, Application };

struct PopupOptions {
    Rect                    anchor;            // in parent logical coordinates
    Placement               preferred;
    std::vector<Placement>  fallbacks;
    PlacementConstraints    constraints;       // keep-on-screen, min/max size
    DismissBehavior         dismissal;         // outside-click, focus-loss, escape
    bool                    grab_input = false;
};

struct WindowOptions {
    WindowRole              role     = WindowRole::Normal;
    Modality                modality = Modality::None;
    WindowHandle            parent;            // required for Dialog/Popup
    std::optional<PopupOptions> popup;         // required iff role == Popup
};
```

Validation rules, enforced at `create_window` and diagnosed in all configurations:

- Popup requires a parent and `PopupOptions`.
- A modal Normal or Tool window requires a parent.
- Popup windows cannot be modal.

### 8.2 Platform mapping notes

- Wayland has no true tool-window or always-on-top concept. `Tool` is mapped to the closest behavior available (a transient toplevel) and documented as non-portable.
- Wayland modality is only a hint to the compositor; Albor enforces modality in the framework regardless of the platform.
- Popup placement uses the platform positioner where one exists (for example `xdg_positioner`) and framework computation elsewhere.

### 8.3 Overlays versus popup surfaces

Albor distinguishes **in-window overlays** from separate popup surfaces. Overlays are the default for menus, combo drop-downs and tooltips that fit within the window bounds; they are part of the UI tree, automatable and accessible with no extra work. A separate popup surface is used when content must extend beyond the window. Both are described by the same popup model so that application code does not choose.

### 8.4 Handles and lifetime

`create_window` returns a `WindowHandle`, a lightweight non-owning handle.

```cpp
class WindowHandle {
public:
    bool alive() const;
    Window* get() const;      // nullptr after the window is destroyed; UI thread only
};
```

- A `Window&` obtained from a handle is valid only until control returns to the event loop.
- Closing is a two-step protocol: a **close request** event (the application may veto, for example to prompt about unsaved changes), then destruction. `on_closed` is delivered after destruction.
- `Window` exposes metrics (§9), position where the platform supports it (otherwise an explicit "unsupported" result, never a silent default), activation and focus behavior, and decoration strategy.

### 8.5 Decorations

On platforms and compositors that do not provide server-side decorations (notably GNOME on Wayland), Albor draws client-side decorations (via a library such as libdecor or framework-drawn; AD-16 decides). Decoration strategy is part of the window creation contract.

---

## 9. Render Surface, Metrics and DPI

### 9.1 Window metrics are the single source of truth

```cpp
struct WindowMetrics {
    Size      logical_size;
    Size      pixel_size;
    float     scale_factor;     // fractional scale supported
    DisplayId display;
};

class Window {
public:
    WindowMetrics metrics() const;      // authoritative
    // ... role, title, visibility, activation, etc.
};
```

The scale factor lives in `WindowMetrics` and nowhere else. `RenderSurface` exposes a read-only copy that the renderer updates when the window reports a metrics change.

### 9.2 Surface creation and presentation

`RenderSurface` is created by the **Renderer** from platform window information together with renderer-owned GPU resources (device, queue, context). It is not purely a platform object.

```cpp
class Renderer {
public:
    std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle,
                                                 const WindowMetrics&);
    void resize_surface(RenderSurface&, const WindowMetrics&);
    FrameTicket execute(RenderSurface&, const FrameScene&);   // flush, submit; queues present
};
```

Applications do not call `present()`. Presentation is queued by the renderer after flush/submit and the required synchronization, and is paced per window by the frame scheduler (§10.3).

### 9.3 Units

- Layout uses **logical** units.
- In `.ui`, `px` means logical pixels; `phx` means physical (device) pixels (AD-27, Proposed).
- Rendering uses device pixels where appropriate.
- Scale pipeline: logical coordinates → scale factor → device pixels.
- Rounding rules for fractional scales (which edges snap, how sizes round) are part of the renderer contract and are tested.

---

## 10. Rendering Architecture and Frame Pipeline

### 10.1 Layers

Rendering is divided into four layers:

1. framework UI drawing (widgets, layout);
2. the Albor **renderer abstraction** (`RenderContext`, resources, `FrameScene`);
3. a Skia-based implementation (and a software implementation);
4. a platform GPU backend.

The UI tree is retained. The renderer may use cached layers, dirty regions and offscreen GPU targets internally (AD-07, Proposed).

### 10.2 Two-stage frame

```text
Stage 1 — Scene build (UI thread)
  input / timers / posted tasks
    -> binding evaluation -> layout -> semantic updates
    -> build FrameScene (framework nodes + CustomRenderNode::render())
    -> immutable FrameScene handed off

Stage 2 — Render execution (render executor)
  FrameScene -> GPU/software execution -> flush/submit -> queued present
```

- `CustomRenderNode::render()` runs in **Stage 1 on the UI thread** and records draw commands that reference retained resources. It must be cheap; expensive preparation (tessellation, decimation, spatial indexing, simplification) happens on workers and produces immutable resources (§11.3).
- The **render executor** is the thread or threads that run Stage 2 (AD-18, Proposed). The default is a dedicated render thread that owns the GPU context. A single-thread mode, in which Stage 2 runs on the UI thread, must also be supported for backends or platforms where that is required, and for debugging.
- Stage 2 never calls application code. This is what makes a dedicated render thread safe.

### 10.3 Frame scheduling and pacing

The framework does not continuously render idle windows. A frame is requested by invalidation, layout change, animation, input or state change, explicit redraw, or external data update.

The scheduler:

- tracks per-window in-flight frames with a bounded queue;
- coalesces requests while a frame is in flight instead of blocking the UI thread;
- integrates platform display timing (Wayland frame callbacks, display-link on macOS, waitable swapchains or equivalent on Windows);
- respects per-monitor refresh rates;
- handles occluded or throttled windows without stalling others.

**Multi-window rule:** presentation back-pressure on one window (a vsync wait or driver throttle) must not block input handling, layout or scene build for any other window. Prototype A validates this explicitly (workload W3, §28).

### 10.4 Retained UI tree, display structures and cached layers

- Cached layers (offscreen GPU targets) may be used for effects, opacity groups, scroll caching and application-declared cacheable custom nodes (§11.6).
- Dirty-region tracking is renderer-internal and optional per backend. Where a platform presentation path supports partial updates, the renderer may use them; correctness never depends on it.
- The inspector's render tree and dirty-region views (§26) are views of `FrameScene` and layer caches.

### 10.5 Data ownership between workers and custom nodes

The rule is **immutable snapshots**:

- Application data read by a custom node (for example, simulation results) is exposed as `std::shared_ptr<const T>` held in an observable `Snapshot<T>` property.
- A worker builds a new snapshot and publishes it with a guarded `post`. The UI thread swaps the property, which invalidates dependent nodes.
- Custom nodes and bindings never read mutable worker-owned state.
- For large incremental data (appended waveform samples), applications use a snapshot type that supports structural sharing or append-only views. This is an application concern; the framework guarantees only the swap semantics.

---

## 11. Custom Rendering Contract

Custom rendering is a foundational API, not an image workaround.

### 11.1 Level definitions

| Level | Scope | Status |
| --- | --- | --- |
| **Level 1** | Framework UI drawing | In scope |
| **Level 2** | Application 2D custom rendering for CAD, schematics, plots, waveforms: **batched immediate primitives plus retained, immutable, Albor-managed render resources** | In scope; this section |
| **Level 3** | Application-defined GPU buffers, instancing, shaders, compute, native GPU interop | Deferred |

**What "GPU resources" means at Level 2:** opaque, immutable, Albor-managed resources created through a thread-safe factory — line and polyline batches, path resources, recorded pictures, images and glyph resources. Applications never see GPU buffers, pipelines or shaders at Level 2.

Level 2 must not make Level 3 impossible. Resource kinds and render passes are extensible, and `RenderContext` is designed to accept future custom-pass objects. Applications must not issue native GPU commands into renderer-owned state. Any future native GPU interop must define ownership, synchronization, state transitions, lifetime, device loss and command submission boundaries (AD-06, AD-19).

### 11.2 Node and context (conceptual)

```cpp
class CustomRenderNode {
public:
    virtual void render(RenderContext&) = 0;                // UI thread; must be cheap
    virtual HitTestResult hit_test(const HitQuery&) = 0;    // UI thread
    virtual SemanticProvider* semantics() { return nullptr; }
};

struct HitQuery {
    Point point;           // logical
    float tolerance;       // logical px
    float scale_factor;
};

class RenderContext {
public:
    StateScope save();                                  // scoped state isolation
    void set_transform(const Transform&);
    void clip_rect(const Rect&);
    void clip_path(const PathResource&);

    // Retained draws: O(1) command each
    void draw(const LineBatch&,  const Transform&, const PaintOverrides&);
    void draw(const PathResource&, const Transform&, const PaintOverrides&);
    void draw(const Picture&,    const Transform&, const PaintOverrides&);
    void draw(const ImageResource&, const Rect& dst, const PaintOverrides&);

    // Immediate, batch-oriented: no per-segment calls
    void draw_segments(std::span<const Segment>, const StrokeStyle&);
    void draw_polyline(std::span<const Point>,   const StrokeStyle&);
    void draw_points(std::span<const Point>,     const PointStyle&);
    void draw_text(const TextLayout&, Point origin, const Paint&);

    float pixel_scale() const;                          // device-pixel awareness
};
```

`StrokeStyle` supports width in logical or device pixels, so that "cosmetic" (hairline) strokes remain one device pixel at any zoom or DPI.

### 11.3 Retained resources and the factory

```cpp
class ResourceFactory {                                  // thread-safe
public:
    LineBatch     create_line_batch(std::span<const Segment>, const StrokeStyle&, ResourceFlags);
    PolylineSet   create_polylines(std::span<const Polyline>, const StrokeStyle&, ResourceFlags);
    PathResource  create_path(const Path&);
    Picture       record_picture(const std::function<void(RecordingContext&)>&);
    ImageResource create_image(const ImageData&);
};
```

- Resources are immutable and shared; creation may occur on any thread; GPU upload is scheduled by the renderer.
- A retained draw is a single command regardless of segment count, so pan and zoom re-use geometry through the transform.
- Applications are expected to rebuild resources on workers when data changes, not to mutate them.

### 11.4 Device loss and lifetime

- Resources keep a CPU-side source representation unless created with `ResourceFlags::DiscardSource`. After device loss the renderer recreates them transparently.
- Resources created with `DiscardSource` deliver an `on_device_reset` callback so the application can rebuild them.
- Resource lifetime is tied to the renderer, not to a window, so that multi-window applications can share them.

### 11.5 Hit testing

`hit_test` receives a tolerance in logical pixels and the scale factor. Scene-scale spatial indexing is the application's responsibility; the framework may later ship an optional spatial index helper. A hit result may carry a semantic key (§19.3) so that the hit maps directly to a semantic child.

### 11.6 Caching and invalidation

- `invalidate()` and `invalidate(Rect)` request redraw.
- A custom node may declare `CachePolicy::CachedLayer`; the renderer may render it into an offscreen layer and re-use it until invalidated. This is the sanctioned use of offscreen targets for custom content (§4.3).

### 11.7 Software parity

Every Level 2 resource kind and primitive must have a software-renderer implementation (§25). Functional parity is checked by a conformance suite; pixel-exactness is not required, but tolerances are defined per test.

---

## 12. Input and Focus

Native input is normalized by platform adapters:

```text
Native Event -> Platform Adapter -> Framework Event -> Window
            -> Hit Test / Capture / Focus -> Event Routing
            -> Component / Command / Model
```

The event model includes:

- pointer movement, press and release (mouse, touch, pen, with pressure where available);
- pointer capture;
- wheel/scroll and high-resolution trackpad scrolling, including momentum phases;
- gestures (pinch, pan, rotate where the platform provides them);
- keyboard (physical and logical keys, modifiers, repeat);
- text input and **IME preedit/composition**, as events separate from key events;
- focus, activation, resize, display and DPI changes;
- drag and drop;
- cursor changes.

**Coalescing policy:** high-frequency pointer and pen events are coalesced to at most one dispatched move event per frame by default. The coalesced intermediate samples remain accessible on the event for applications that need them, such as freehand drawing.

A central `FocusManager` owns focus scopes, traversal, keyboard navigation, modal focus trapping, focus restoration and mnemonics. Custom render nodes provide hit testing (§11.5) and may expose focusable semantic children (§19.3).

---

## 13. Reactive Properties and Bindings

The property/binding system is a load-bearing architecture decision, settled by AD-04.

### 13.1 Expression categories

1. compile-time constants;
2. static expressions;
3. property-dependent expressions;
4. observable external state;
5. event-driven state;
6. animation-driven state;
7. layout-dependent values;
8. theme-dependent values;
9. locale-dependent values (including translated strings and text direction).

Categories 8 and 9 are implemented as **environment properties** (theme, locale, direction, scale) that bindings depend on like any other property. A translated string is therefore never folded as a constant.

### 13.2 Observable external state

Arbitrary C++ variables are not observable. State exposed to `.ui` must use `Observable<T>`, `Snapshot<T>` or a model interface.

- All property mutation occurs on the UI thread.
- `Observable<T>::set` is UI-thread only; `post_set` is thread-safe and applies the change on the UI thread.
- Snapshot properties follow §10.5.

### 13.3 Dependency and invalidation mechanism

The implementation must provide a compact dependency/invalidation mechanism that does not turn thousands of properties into memory-heavy listener objects. Candidate strategies include compact dependency records, dirty bitsets, property IDs, shared dependency nodes and batched push/pull evaluation. Prototype D (§30) measures them. Compiled bindings are plain generated functions (static function pointers) with declared dependencies as property IDs; no per-binding `std::function` allocation.

### 13.4 Write precedence (AD-24, Proposed)

Three write sources exist: generated initialization, declarative binding, and imperative C++ assignment.

- **Imperative assignment breaks the binding** on that property. An explicit `rebind` operation restores it.
- Generated initialization uses an internal path that does not break bindings, so initialization order never changes semantics.
- The `.ui` language does not provide two-way bindings in the initial version. This is a language-grammar decision (open question 1), but the precedence rule must remain well-defined if they are added.
- **Live reload interaction:** `ReplaceBinding` applies only to properties whose declared binding changed in source. An imperatively overridden property whose declared binding did not change keeps its override.

### 13.5 Animation integration

Animation-driven state (category 6) is a first-class binding source. The animation system (§21.4) integrates with the frame scheduler and with property precedence: an active animation writes through a defined animation layer that yields to explicit writes according to rules fixed in the animation API. The animation core arrives in Phase 3, alongside bindings (§31).

---

## 14. Generated C++ and Incremental Builds

### 14.1 Public interface only

Generated public headers expose **stable component interfaces** only, as concrete, non-virtual facade classes holding an opaque implementation:

```text
MainWindow.hpp -> stable public interface (properties, callbacks, ids, commands)
MainWindow.cpp -> generated tree + bindings + private implementation
```

"Stable interface" does **not** mean an abstract base class with virtual dispatch. It means a facade whose layout and declarations change only when the component's declared interface changes (AD-03).

### 14.2 Build invariants

- Changing a `.ui` body must not force recompilation of unrelated application translation units.
- **Write-if-changed:** the generator rewrites a generated file only when its content changes, so timestamps do not cascade through Make/Ninja builds.
- Generated headers include only declared interface dependencies (child component headers they need, never their implementations).
- Large components and large applications are splittable into multiple generated compilation units, by component and, for very large components, by node subtree.
- Generated code avoids giant headers, excessive templates and duplicated runtime code.

### 14.3 Interface versus body changes

| Change | Rebuild scope |
| --- | --- |
| Edit within a `.ui` component body (property values, bindings, layout, styles, resources) | that component's generated `.cpp` only |
| Change to a component's declared interface | generated `.hpp` and all includers |
| Change to a native manifest | regenerated glue and its includers |

Prototype B measures these scopes against the baseline (§30).

---

## 15. Native Component Contract (Manifest)

### 15.1 Manifest is the single source of truth (AD-17, Proposed)

Application-defined components, objects, models and services (a `ChartView`, a schematic view, a `simulation` object) are declared once in a machine-readable **manifest**, written in a native-interface declaration syntax that is part of the `.ui` language family. The compiler **never parses application C++ headers**. C++ glue (property storage, signal plumbing, registration) is **generated from the manifest**, and the application's C++ implements or derives from the generated glue.

Header scanners and C++ annotation macros are rejected as sources of truth: both require parsing C++ and create a second declaration to keep in sync. Tooling that helps *write* manifests (for example, from existing C++ types) is allowed, but its output is a manifest that is then reviewed and owned like source.

### 15.2 Declaration kinds

| Kind | Purpose |
| --- | --- |
| `native component` | A visual component (properties, signals, commands, semantic role, children capability) |
| `native object` | A non-visual object exposed to `.ui` (for example `simulation`) |
| `native model` | A model type (row type, roles, key policy) |
| `native global` | A named singleton service or namespace exposed to `.ui` (for example `commands`) |

Each declaration lists names and types, read/write capability, signals, commands/actions, semantic role, thread-affinity (always UI thread for the property surface), and a **preview block**.

### 15.3 Preview block

The preview block provides sample values and stub behavior so that preview and the LSP can function without the application's C++:

- sample property values and sample model rows;
- visual placeholder, or a reference to an optional **preview plugin** (a shared library the application chooses to build against the real implementation);
- signals that the preview inspector can trigger.

Without a plugin, the preview instantiates a generic stub visual from the manifest (AD-02).

---

## 16. Compiler

```text
.ui + manifests -> Lexer -> Parser -> Syntax Model -> Semantic Analysis
                                          -> Typed UI IR
                                               +-- C++ Codegen (+ manifest glue)
                                               +-- LSP
                                               +-- Preview / Dev Service
```

- The compiler supports incremental parsing and semantic analysis and tracks dependencies at component and interface level, using a query-based architecture suited to LSP use.
- The typed IR is versioned and serializable. Serialization is how the compiler talks to the dev service and the reload adapter.
- The compiler's implementation language is **C++** (AD-23, Proposed): one toolchain for contributors and the ability to share IR definitions with `devrt`. Rust remains excluded from the production runtime; whether it is excluded from development tooling is not an architectural requirement.
- Diagnostics are produced by the compiler library, and the LSP surfaces the same diagnostics. "LSP diagnostics agree with compiler diagnostics" is tested by running both over the same corpus.
- Expression purity rules (what `.ui` expressions may call) are part of the grammar decision. Calls to native functions in bindings are permitted only if declared pure in the manifest; impure native calls are event-handler-only.

---

## 17. Production, Dev Runtime, Preview and Live Reload

### 17.1 Production

```text
.ui -> compiler -> generated C++ -> native application -> Albor runtime
```

No compiler frontend, interpreter, registry or reflection is present (§2.1).

### 17.2 Dev runtime extension (AD-02, Proposed)

`albor-devrt` is a separate library linked only in Dev configuration. It contains:

- **Type and property registry** built from generated descriptor tables (separate generated translation units, present only in Dev/Test);
- **IR loader** for serialized IR;
- **Restricted expression VM**: pure expressions over property reads, arithmetic, strings, conditionals and manifest-declared pure functions; event handlers dispatch to commands and native signals through the descriptor tables. It is **not** a general-purpose interpreter;
- **Reload adapter** applying `UiPatch` operations to the live tree;
- **Inspector hooks**.

It attaches through the registration hooks of the layout-invariance rule (§2.3). Compiled and dev paths are kept equivalent by the conformance corpus (§2.4, AD-25).

### 17.3 Preview

```text
.ui + manifests -> shared frontend -> serialized IR
               -> preview host (Albor runtime + devrt) -> actual renderer
```

- Preview exercises the real runtime and renderer.
- Native components are provided by manifest stubs or optional preview plugins (§15.3).
- Preview inherits the same limitation as any IR-driven mode: application-specific logic that is not described in the manifest is not available. This limitation is documented, not hidden.

### 17.4 Live reload

```text
Editor -> Compiler/Dev Service -> serialized IR / UiPatch -> running application
                                                          -> reload adapter -> UI tree
```

- The compiler and semantic analyzer remain out of the application process.
- The in-app side is the reload adapter in `devrt` (§17.2).
- Invalid source leaves the last valid UI active and reports diagnostics.
- Transport for the dev service is local-only and authenticated (§19.6).

---

## 18. Identity and Live Reload Patching

### 18.1 Key classes (AD-11, Decided)

| Key class | Origin | Use |
| --- | --- | --- |
| **Explicit key** | The `id:` of a node (scoped by component) | **Identity.** Used by automation selectors, semantic keys, focus and state preservation. |
| **Implicit key** | Compiler-generated from component, parent key, node type and ordinal among same-type siblings | **Correlation hint** only. Used for best-effort live-reload alignment. Never used by automation. |

- Identity is never derived from tree position alone: explicit keys are author-controlled and survive any structural edit.
- Implicit keys are marked as such. Tooling lints automation selectors that resolve to implicit keys, and the LSP offers a code action to add an explicit `id`.
- Interactive and semantically significant nodes should have explicit ids; a configurable compile diagnostic can require this.

### 18.2 Instance paths

Repeated instances and model delegates are identified by an **instance path** that combines source keys and item keys:

```text
MainWindow::charts
ResultsList::row[<item-key>]::name_label
```

Item keys come from models (§20). A model without keys falls back to row indices with a diagnostic stating that identity is unstable.

### 18.3 Patch operations

```text
CreateNode  DeleteNode  MoveNode  ReorderChildren  ChangeNodeType
SetProperty  ReplaceBinding  ReplaceEventHandler
UpdateStyle  UpdateTheme  UpdateResource  UpdateTransition
```

- Diffing aligns siblings using explicit keys first, then type and implicit keys, so that moves are distinguishable from delete+create wherever explicit keys exist.
- Reloadable edits: property, binding, event, style, resource, transition and compatible structural changes.
- **Rebuild required:** changes to a component's declared public interface, to a native manifest, or to anything that changes generated public C++.

### 18.4 State preservation

Reload preserves UI-owned state where keys allow: scroll offsets, focus, cursor/selection, expanded state and item selection. Application/domain state remains outside the reload system. Imperative property overrides follow §13.4.

---

## 19. Semantic Tree, Accessibility and Automation

### 19.1 One semantic tree

```text
Semantic Tree (Release)
   +-- Accessibility adapters (Release)
   +-- Automation core + adapters (Dev/Test only)
```

Nodes expose stable keys, role, accessible name and description, state and value, bounds, actions, relationships and children. The `type` name used by automation selectors (`ToolbarButton`) comes from descriptor tables and exists only in Dev/Test builds; the Release semantic tree carries role, name and state.

Accessibility and automation share this tree; automation additionally needs synthetic input, raw property access, screenshots and waits, which are outside the semantic tree and exist only in the automation core.

### 19.2 Accessibility adapters

Adapters map the tree to macOS accessibility, Windows UI Automation and Linux AT-SPI. They support active OS-driven callbacks and notifications for focus, value, name, selection, structure, bounds and state changes. Text controls expose text semantics (ranges, selection, caret, attributes) per platform text-accessibility model.

### 19.3 Custom render nodes: semantic providers

A custom-rendered canvas is a semantic node whose children are provided by the application:

```cpp
class SemanticProvider {
public:
    virtual size_t child_count() = 0;                        // logical count
    virtual SemanticNodeInfo child_info(size_t index) = 0;
    virtual std::optional<size_t> find_child(const SemanticKey&) = 0;
    virtual ActionResult perform(const SemanticKey&, Action, const Arguments&) = 0;
};
```

- Children use the same virtualization contract as model views (§20): **logical count versus materialized nodes**. A schematic with thousands of components exposes only viewport-relevant children to accessibility, while automation can look up any child by key (`component:R1`, `net:VCC`) even when it is not materialized.
- `perform` can include a request to bring a child into view.
- Semantic keys on children are application-defined and stable across frames and sessions where the domain permits.

### 19.4 Automation operations and results

Representative operations: connect/launch, get_root, find/query, `get_by_id`, `get_by_type`, `get_by_role`, `get_by_text`, `get_by_property`, `nth`, click/double_click, type/key, hover, focus, `wait_for`/`wait_for_exists`, count, get_property, get_bounds, screenshot.

Every action returns a result with status and message:

```cpp
struct ActionResult {
    enum class Status { Ok, NotFound, Disabled, NotSupported, Failed, Timeout };
    Status      status;
    std::string message;
    Value       value;
};
```

- Stable ids are preferred over positional selectors. `nth()` is permitted for intentionally repeated elements but is not identity.
- **Input injection happens at the framework level.** Platform-level injection is not portable (it is unavailable on Wayland, for example). Injected events pass through the same routing as native input from the Window downward.
- `screenshot` uses a renderer readback API available in Test builds (and the software renderer in headless mode).

### 19.5 Settled condition

`wait_for` and implicit waits use a defined **settled** condition. The UI is settled when all of the following hold:

- no posted tasks are pending in the automation epoch;
- no binding, layout or semantic invalidations are pending;
- no frame is requested or in flight;
- no animations are active;
- no application-registered "busy" operations are outstanding (applications may register async work such as a running simulation, so tests can wait on it explicitly).

### 19.6 Automation security (AD-10, Decided)

- The automation core is excluded from Release **at compile/link time**.
- In Dev/Test builds, the control channel is local-only (loopback or a user-private socket/pipe), requires a per-session token, and is off unless explicitly enabled by launch configuration.
- Command invocation through automation is subject to an allow-list declared by the application.

### 19.7 MCP routing (AD-20, Decided)

MCP is an adapter over the automation layer. It does not call commands or runtime internals directly, and so inherits automation's enablement, authentication and allow-list. MCP is not a core runtime subsystem.

---

## 20. Models and Virtualization

Initial model-driven views: ListView, TableView, TreeView. Virtualization is architectural, not a later optimization.

### 20.1 Model interface requirements (AD-13)

- Models are UI-thread objects. Workers publish changes via batched, guarded posts (`ModelUpdateBatch`), not by mutating models directly.
- A model provides row count, row data by role, change notifications (insert, remove, move, update, reset) and an **optional item key**: `ItemKey item_key(row)`. Keys are the basis for delegate identity, focus retention, selection stability, live-reload reconciliation and automation lookup.
- Selection models are separate from data models.

### 20.2 Required handling

The design must handle explicitly:

| Concern | Mechanism |
| --- | --- |
| Logical count vs. materialized nodes | Views and `SemanticProvider` expose logical count; only visible and pinned rows materialize. |
| Accessibility set-size / position-in-set | Reported from logical count and row index. |
| Focus on recycled items | The focused row is **pinned** and not recycled until focus leaves. |
| Text editing/IME in recycled items | Rows with active text editing or composition are pinned. |
| Automation lookup | By item key, with an explicit `scroll_into_view` request. |
| Stable delegate identity | Instance paths with item keys (§18.2). |
| Live reload of delegates | Delegate components reconcile by item key; state (scroll, selection) is preserved. |

---

## 21. Layout, Text, Styling and Animation

### 21.1 Layout

Layout uses logical coordinates. The layout contract:

- two-phase measure/arrange with intrinsic minimum, preferred and maximum sizes;
- constraints, alignment, spacing, min/max dimensions;
- text-dependent sizing through the text-measurement contract (§21.2);
- RTL and incremental relayout;
- DPI changes trigger relayout and re-rounding.

The concrete layout algorithm remains open (open question 16). The contract above is fixed so that the algorithm choice does not affect text, virtualization or custom nodes.

### 21.2 Text

Text is foundational. Required capabilities: font selection and fallback, Unicode shaping, bidi, line breaking, measurement, ellipsis, selection, cursor/editing, IME and accessibility text semantics.

- Text measurement is a prerequisite contract for layout (AD-14). Text and fonts arrive in Phase 4 together with the resource system (fonts are resources, §23).
- Text library selection (open question 15) is judged against: shaping and bidi quality, IME integration, accessibility text-range support, editing primitives, binary size and licensing, and fit with Skia/software rendering.

### 21.3 Styling

Theme defaults, state-dependent styling, light/dark themes and application overrides. Theme-dependent properties are runtime dependencies (environment properties, §13.1). State-dependent transitions (hover, pressed) use the animation core.

### 21.4 Animation

Animation integrates with the frame scheduler: animated properties invalidate affected UI and request frames only while animation is active. Timing, easing, interruption, reduced-motion behavior and interaction with bindings and write precedence (§13.5) must be defined in the animation API. The animation core (property animation and scheduler integration) is built in Phase 3. Reduced-motion support and polish follow in Phase 11.

---

## 22. Commands, Menus, Dialogs and Platform Services

### 22.1 Commands

A `Command` is an object, not a callback:

```text
Command: id, label, icon, shortcut, enabled (observable), checked (observable),
         semantic role/description, execute()
```

Commands are shared by buttons, menus, shortcuts, automation (through the allow-list) and, indirectly, MCP (via automation):

```text
Button / Menu / Shortcut / Automation (+MCP via Automation)
                  |
               Command
                  |
           Application Action
```

Widgets accept a `command:` property that supplies label, enabled state, shortcut and semantics. Undo/redo integrates at the command/action layer (AD-15, Decided); the detailed history API remains open.

### 22.2 Menus and dialogs

The framework provides abstract menu and dialog models. Platform presentation may be native or framework-rendered, because macOS, Windows, Wayland and X11 do not share one native menu/dialog model.

- **The menu model is always represented in the semantic tree**, regardless of presentation. Automation and preview interact with the model and commands, not with OS menu objects.
- macOS requires a native menu bar. Linux generally uses framework-drawn popups or overlays. Both implement the same menu model.
- File and folder dialogs are asynchronous (portals on Linux) and return results through the event loop (§7.4).

### 22.3 Platform services

Clipboard (multiple formats), drag/drop, cursor, tooltips, notifications, native file/folder dialogs. Services are abstract interfaces implemented per platform. Platform prerequisites (bundle identity for macOS notifications, AUMID registration on Windows, D-Bus and portals on Linux) are documented per platform.

---

## 23. DPI, Resources and Internationalization

### 23.1 DPI

Multiple monitors and dynamic scale/display changes are supported where the platform exposes them. X11 limitations (global DPI, no reliable per-monitor change notification) are explicitly acknowledged and documented as Tier 2 behavior (§4.1).

### 23.2 Resources

- Resources include images, fonts, icons, translations and application assets.
- A resource pipeline exists from Phase 4 because text needs fonts. Resource dependencies participate in incremental builds, and resource changes trigger only the necessary compilation or reload (`UpdateResource`).

### 23.3 Internationalization

- Locale changes, pluralization, locale-aware formatting, translated strings, RTL.
- Translatable strings use a dedicated syntax in `.ui` (an illustrative `@tr("...")`), extracted at compile time into catalogs. Translated strings are locale-dependent bindings, never constants (§13.1).
- The locale and text direction are environment properties. A locale change re-evaluates dependent bindings and triggers relayout.
- i18n core (environment properties, catalogs, direction) is built in Phase 4. Full formatting and pluralization libraries are selected by Phase 11; the *architecture* does not wait for them.

---

## 24. Platform Architecture

```text
Albor Runtime -> Platform Abstraction
                 +-- macOS
                 +-- Windows
                 +-- Linux/Wayland
                 +-- Linux/X11
                 +-- Headless
```

- **macOS:** AppKit/native windows and a Metal-compatible presentation path. Framework timers and posted tasks register in `NSRunLoopCommonModes`, and the display link must keep framework task scheduling alive across window occlusion and application deactivation, because a nested modal session services the modal run-loop mode only (AD-21, validated by Prototype A2 on macOS).
- **Windows:** Win32 and a validated Skia GPU backend (open question 8).
- **Wayland:** explicit surface roles; positioner-based popups; client-side decoration strategy; frame callbacks (and throttling for occluded windows); text-input protocol and IME; portal integration for dialogs and notifications; GPU presentation through the platform's EGL or Vulkan window-system integration, with a shared-memory software fallback. Wayland clients cannot position windows or read global coordinates, so the window API reports "unsupported" for those queries (§8.4).
- **X11:** its own input, clipboard, IME (IBus or XIM), DPI and presentation handling (Tier 2).
- **Headless:** no native window, no GPU requirement (§25).

### 24.1 Platform implementation strategy (AD-16, Decided)

AD-16 settles the ownership boundary: Albor implements its own lightweight Window, EventLoop and Platform abstractions per OS and reuses low-level standard C libraries only for specialized protocol tasks. A general-purpose windowing library is not used as the platform layer. The criteria that produced the decision were:

- ability to express window roles, popups, modality and client-side decorations (Wayland positioner/popup behavior is the key test);
- ability to run custom nested-loop handling and register event sources in all run-loop modes (§7.4);
- IME and text-input integration depth;
- accessibility hooks;
- ability to create surfaces from renderer-owned GPU devices (§9.2);
- licensing, binary size and maintenance burden.

A library that cannot express Wayland popup placement or surface roles is acceptable for the renderer-only prototype A1 but disqualifying as the final platform layer; that is why A1 may use a convenience library while the platform layer is written directly.

| Platform | Direct layer | Reused libraries |
| --- | --- | --- |
| macOS | AppKit plus an Objective-C runtime bridge | CAMetalLayer for presentation |
| Windows | Win32 | DirectComposition, DXGI, TSF |
| Linux Wayland | Wayland client | libdecor, xkbcommon, libwayland-egl |
| Linux X11 (Tier 2) | X11 client | xcb, xkbcommon |

---

## 25. Headless, Software Rendering and GPU Lifetime

- A headless platform backend and a software/raster renderer are **required** for CI, screenshot testing, automation, VMs, remote desktops and systems without usable GPU presentation.
- Functional parity between GPU and software renderers is required for all Level 2 primitives and resources (§11.7). The software renderer is the **reference path for screenshot tests**; GPU output is compared against it with per-test tolerances.
- The renderer owns GPU device and context lifetime. Resource lifetime is defined relative to renderer, surface, window and custom node (§11.4).
- Device reset or loss is handled explicitly: renderer-owned resources are recreated; application-owned resources use `on_device_reset`.

---

## 26. Diagnostics and Inspector

Development diagnostics include UI tree and semantic tree inspection, layout bounds, hit-test visualization, dirty regions, frame timing and FPS, GPU resources, memory, and binding/dependency inspection. The inspector's render-tree and dirty-region views are views over `FrameScene` and layer caches (§10.4).

These facilities exist only in Dev and Test builds (§2.2).

---

## 27. Testing

Testing covers runtime, compiler, UI, tooling, accessibility/semantics, automation, rendering, live reload and all supported platform layers. Headless execution is required for substantial CI coverage.

Cross-cutting conformance suites:

| Suite | Purpose |
| --- | --- |
| **Dual-engine conformance** | The same `.ui` corpus runs through compiled output and the dev IR evaluator and must produce the same observable behavior (AD-25). |
| **GPU/software parity** | Level 2 primitives and resources compared across renderers within tolerance. |
| **LSP/compiler agreement** | Diagnostics from both over the same corpus. |
| **Manifest consistency** | Generated glue, descriptor tables and preview stubs agree with the manifest. |
| **Semantic tree tests** | Roles, names, states and actions of every widget (a widget's definition of done includes its semantic tests). |
| **Automation compatibility** | The reference test (Appendix B) running against a sample application. |
| **Build-scope tests** | A scripted check that a `.ui` body edit rebuilds only the expected translation units (§14.3). |

---

## 28. Measurable Performance Targets and Benchmark Workloads

Performance requirements are numerical. Initial **measurements**:

### 28.1 Build

- clean build time;
- incremental `.ui` compilation time;
- incremental C++ build time;
- generated header and source size;
- number of translation units rebuilt after a local `.ui` body change, after an interface change, and after an unrelated application change;
- the same set measured on a **baseline**: an equivalent UI built in the current workflow that motivated this project (§30, Prototype B).

### 28.2 Runtime

- startup time;
- memory footprint;
- UI node and property counts, and memory per property and per binding;
- frame time distribution;
- input-to-frame latency;
- idle CPU usage.

### 28.3 Rendering workloads

Workloads are fixed so that prototypes have a failure condition:

| Workload | Scene | Interaction |
| --- | --- | --- |
| **W1 — Waveform** | Multiple polylines totaling ~10⁶ segments; antialiased cosmetic (1 device pixel) strokes | (a) continuous pan/zoom of a static scene; (b) data appended at a fixed rate with resource rebuild on a worker |
| **W2 — CAD-like** | ~10⁵ symbols of roughly ten segments each (~10⁶ segments total) plus several thousand text labels | continuous pan/zoom plus hover hit testing against the full scene |
| **W3 — Multi-window pacing** | Two windows, each running W1(a); presentation of one artificially throttled | input handling and frame pacing in the unthrottled window must be unaffected |

Metrics recorded per workload: frame-time percentiles (p50/p95/p99), input-to-frame latency, CPU and GPU memory, resource upload time, idle CPU after the interaction stops.

### 28.4 Pass/fail criteria

Acceptance thresholds are **set and recorded before a prototype runs**, based on the intended application class and reference hardware, and written into the prototype plan. They are not derived from the measured results. A natural default for interactive workloads is sustaining the display refresh rate during interaction on the declared reference hardware; the numbers themselves are chosen when the prototype is scheduled.

---

## 29. Architectural Decision Register

### Core model and build

**AD-01 — Production execution model.** *Decided.* `.ui` compiles to native C++. No compiler frontend, interpreter, registry or reflection in Release.

**AD-02 — Dev runtime extension.** *Proposed.* A separate `albor-devrt` (registry, IR loader, restricted expression VM, reload adapter) is linked only in Dev. The compiler stays out of process. The layout-invariance rule (§2.3) keeps the runtime binary identical across configurations. Validated by Prototype E.

**AD-03 — Generated public headers.** *Decided.* Concrete, non-virtual facade classes; interface-only; implementation private; write-if-changed generation.

**AD-04a — Invalidation index.** *Proposed.* Compact CSR (Compressed Sparse Row) index over declared dependencies with eager push evaluation and no per-binding allocation. Supported by Prototype D at 56 B per property and 39.3 B per binding. **Not covered:** the prototype's index is sealed, while §18 and §20 require runtime mutation — virtualized delegates, `if`/`for` constructs, popups, live-reload `ReplaceBinding`. Per-component or segmented indices are the likely answer and none was tested. `PropertyId` assignment is also unspecified: ids must be component-local with a runtime base offset, or any interface change cascades through every generated file and breaks §14.2/§14.3.

**AD-04b — Storage layout.** *Open.* Prototype D measured a single uniform value slot. Typed per-component storage and the realistic value types (strings, translated strings, snapshots) were not measured, so the layout is not settled.

**AD-04c — Evaluation model.** *Open.* Eager push is implemented and meets its budget, but the prototype also measured one pass per chain layer (depth 8 → 8 passes) and flagged it as a Phase 2 risk; a pull or hybrid model would remove the per-layer passes and is unmeasured. Kahn ordering is a Dev/Test aid only, so tests exercise a different evaluation order from Release, which conflicts with §2.4 unless cycle detection becomes a compile-time error on the static graph. The prototype's 64-pass budget is a guard that should stay in Release, which this entry does not otherwise provide.

**AD-04d — Animation shadowing.** *Proposed.* 64 B per animation slot, matching the measured budget. Its interaction with AD-24 (imperative writes breaking bindings) is unspecified, and whether ending an animation re-evaluates the shadowed binding rather than restoring a stale value was not verified.

**AD-04 prototype caveats.** The workload gave every category a 16 ms p99 budget — a whole frame, shared with layout and scene build — so no category could fail. It contained no reconvergent (diamond) dependencies, since redundant visits were zero, while real UIs have many. It was one run on one machine and there is no `plan.md`, so the thresholds cannot be shown to have been pre-recorded and the reference hardware was declared only afterwards (§28.4). Follow-up is Prototype D2, not a re-reading of this run.

**AD-17 — Native component manifest.** *Proposed.* The manifest is the source of truth; glue is generated; the compiler never parses C++ (§15).

**AD-24 — Binding write precedence.** *Proposed.* Imperative assignment breaks bindings; `rebind` restores; reload does not override unchanged-binding overrides (§13.4).

**AD-25 — Dual-engine semantic equivalence.** *Decided.* Compiled and dev paths share one semantic definition and a conformance corpus (§2.4).

**AD-23 — Compiler implementation language.** *Proposed.* C++ (§16).

**AD-27 — Units.** *Proposed.* `px` = logical pixels; `phx` = physical pixels.

### Rendering

**AD-05 — Renderer architecture.** *Decided.* Renderer separated from windowing/platform APIs; Skia is the initial implementation.

**AD-06 — Custom rendering API.** *Proposed.* Albor `RenderContext` with batched immediate primitives and retained immutable resources; Level 3 deferred; no native GPU commands into renderer state (§11). Validated by Prototype A1.

**AD-07 — Render scene model.** *Proposed.* Retained UI tree; UI-thread scene build produces an immutable `FrameScene`; renderer-internal caching and layers.

**AD-18a — Render execution and stage division.** *Decided.* Stage 1 (scene build and application callbacks) on the UI thread; stage 2 (render execution and GPU encoding) decoupled from application code; immutable snapshots for worker data (§10.5). Supported by A1: recording a frame on a worker costs 0.46 ms p99 against an 8.33 ms processing deadline, and recording on the UI thread is equivalent within noise, so the division is close to free.

**AD-18b — Multi-window presentation isolation.** *Open.* Two windows presented synchronously on the UI thread stall each other: window 2's interval during window 1's 100 ms hold is 100.86 ms p50 (A1-Dawn W3), i.e. the hold itself rather than GPU contention. **That result is close to a tautology** — any design that presents on the UI thread fails it by construction, so repeating the measurement proves nothing. The open question is narrower: can presentation be moved off the UI thread on macOS at all, given AppKit/QuartzCore main-thread affinity, which refuted "the render thread owns the surface" for Ganesh in A1.2a? Two candidate mechanisms are untested: `CAMetalDisplayLink` (macOS 14+, present in this SDK, which delivers the drawable with the display-link callback and so addresses the missing non-blocking availability signal) and `CAMetalLayer.maximumDrawableCount`. Until one is measured, §10.2 and §10.3 still describe a default that A1.1 and A1.2a refuted on macOS and must be read as provisional.

**AD-26 — Software/headless parity.** *Decided.* Required; software renderer is the screenshot reference (§25).

**AD-28 — Candidate primary HAL (Google Dawn / WebGPU C++).** *Proposed (in active evaluation).* **Candidate backend:** Google Dawn (WebGPU C++) is the primary candidate HAL for `albor::Renderer` over Metal on macOS. **Measured on A1-Dawn (macOS/Metal, single run per workload, 2019 Intel laptop):** 1.8 × 10⁶ line segments in a single `Draw(6, 1800000)` instanced call against 220 draw calls under Ganesh; frame recording 0.085 ms p50 against 0.177 ms; present interval p50 16.62 ms against 149.7 ms. **How far that evidence goes, stated narrowly:** the 99.41% hover hit ratio common to both renderers shows only that both received *identical input geometry* — hit testing is CPU-side and the renderer takes no part in it — so it is a fixture check, not a rendering-parity check; pixel-level output parity and GPU timestamp queries are still outstanding. The headline conflates two effects: Ganesh's 10,470 ms stroke figure is CPU tessellation that the pre-tessellated quad path already avoids, so the like-for-like recording gap is about 2×, and the present-interval gap is GPU-bound, so a 60 Hz lock demonstrates no missed vsync but no headroom either. The Skia baseline has a known defect of its own: its pre-tessellated quads fix stroke width in geometry space, so strokes scale under zoom and are not the cosmetic device-pixel lines the workload specifies. **Blocking items:** (1) *Platform parity* — D3D12 and Vulkan unvalidated; only the `metal` feature was enabled. (2) *Text* — Dawn has no glyph stack, and the Skia W2 run drew its 4000 labels while the Dawn run drew none, so the "30× cheaper W2" figure is partly an artefact of unequal work. (3) *2D feature surface* — replacing Skia means rebuilding paths with joins, caps and dashes, clip paths, gradients, opacity layers and effects; this is the largest unlisted cost. (4) *Software and headless parity (AD-26)* — Dawn has no first-party CPU rasterizer, though software adapters (SwiftShader, lavapipe, WARP) are candidates and are slow and non-deterministic. (5) *Build cost* — unevaluated; the 757 s run began with a warm ccache for part of the port and no cold Skia build was measured. (6) *A middle option exists and is untested* — the same vcpkg `skia` port offers a `dawn` feature that pulls in `skia[graphite]`, so Skia Graphite on a Dawn backend could let Skia's text, paths and effects share one device with custom Level 2/3 passes, which would remove blockers 2 and 3. **Skia is not retired** and remains the default renderer until these close. Evidence standard: single runs, and A2 established that tail figures on this hardware vary several-fold between runs, so no p99 here is a decision input on its own.

### Windows and platform

**AD-08 — Window roles and modality.** *Decided.* Role (Normal/Dialog/Tool/Popup) at creation; modality is a separate framework-enforced property; popup carries parent/anchor/placement.

**AD-16 — Platform Implementation Strategy.** *Decided.* 1. **Ownership Boundary:** Albor directly implements its own lightweight Window, EventLoop, and Platform abstractions per OS (macOS AppKit, Windows Win32, Linux Wayland, Linux X11). General-purpose windowing libraries (GLFW, SDL) are rejected as they fail Wayland popup requirements (xdg_positioner), nested run-loop event registration (§7.4), IME text input, and OS accessibility integration. 2. **Targeted Modular Plumbing:** Albor reuses low-level, standard C platform libraries for specialized protocol tasks where appropriate: Linux Wayland: libdecor (client-side decorations), xkbcommon (keyboard mapping/state), libwayland-egl; Linux X11: xcb, xkbcommon; macOS: Direct AppKit / Objective-C runtime bridge + CAMetalLayer; Windows: Direct Win32 / DirectComposition / DXGI / TSF. 3. **Prototype Unblocking:** This unblocks Prototype A2 (platform presentation, surface creation, popups, and nested-loop validation across macOS, Windows, and Wayland). **Evidence status.** *Decided*, but the factual claims in point 1 are **not verified against current library versions**: the claim that SDL fails Wayland popups needs re-checking, since SDL3 added popup and tooltip windows. The supporting evidence is Prototype A2 on macOS only, and A2 left window roles, popups and modality unimplemented, which are exactly the capabilities the rejection rests on. No record yet exists of what would reverse this decision, and the reuse list omits D-Bus (portals, AT-SPI) and IME libraries. Reversal criteria to be recorded: if one library is shown to satisfy Wayland `xdg_positioner` popups, nested-loop source registration, IME and accessibility on both Wayland and Win32, this decision should be reopened.

**AD-21 — Nested native loops and re-entrancy.** *Decided* as a principle (§7.4); platform mechanisms are implementation work.

**AD-22 — X11 tier.** *Proposed.* Tier 2 for the first production milestone.

### Semantics, identity, automation

**AD-09 — Semantic tree.** *Decided.* One tree; custom nodes provide virtualized semantic children (§19.3).

**AD-10 — Automation security.** *Decided.* Excluded from Release at compile/link time; local-only, token-authenticated and allow-listed in Dev/Test.

**AD-11 — Node identity.** *Decided.* Explicit keys are identity; implicit keys are correlation hints only (§18.1).

**AD-20 — MCP routing.** *Decided.* MCP goes only through the automation layer.

### Live reload, models, text, commands

**AD-12 — Live reload.** *Proposed.* External dev service sends serialized IR/patches to the in-app reload adapter in `devrt`.

**AD-13 — Virtualization and model keys.** *Decided* as a principle; the model interface (§20.1) is *Proposed* pending Phase 6 design.

**AD-14 — Text/layout dependency.** *Decided.* Text measurement is a foundational layout contract.

**AD-15 — Commands and undo/redo.** *Decided.* Commands are the shared action abstraction; undo/redo integrates at that layer. The history API remains open.

### Distribution

**AD-19 — ABI, versioning and distribution.** *Proposed.* Pre-1.0: no C++ ABI stability promise; distribute as source/CMake package building static or pinned-toolchain libraries; Skia built as a pinned dependency of Albor; no Skia or platform types in public headers; standard-library types in public APIs limited to those stable within a supported toolchain set. A stable ABI (for example through a C facade) is revisited at 1.0. Validated in Prototype B (build/distribution) and A1 (Skia build cost).

---

## 30. Prototype Plan

Each prototype states its questions, scope and pass/fail criteria recorded in advance (§28.4).

### Status, tests run, and conclusions so far

Every threshold below was committed before its run (§28.4) and every result is in the prototype's `RESULTS.md`, with one
exception recorded as such: **A2's Revision R1 restated the input-to-present budget from 16.7 ms to 25.0 ms after the
first run had already failed against 16.7 ms.** The original miss stands as recorded. Prototype D has no `plan.md` at
all, so its thresholds cannot be shown to have been pre-recorded and its reference hardware was declared only after
the run.

| Prototype | State | Tests and runs performed |
| --- | --- | --- |
| **D — property and binding engine** | complete | 33/33 gtest unit tests (`tests/d/`); benchmark at 100,000 properties and 11,032 bindings |
| **A2 — platform presentation (macOS)** | complete | 200 steady frames, 60-step live resize, 500 ms nested modal session |
| **A2 — Windows and Wayland** | not run | plans and platform scaffolds committed; execution tables `PENDING_CI_MATRIX_RUN` |
| **A1 — renderer (Skia)** | complete | A1.1, A1.2a, A1.3 workloads W1/W2/W3 (401, 401, deadline-limited frames), A1.4 offscreen (400/400) |
| **A1-Dawn — renderer (WebGPU)** | complete, macOS only | workloads W1/W2/W3 (400, 400, 400/400 frames; 3,200 hover queries) |
| **B — generated-code build performance** | not started | none |
| **C — text, IME and accessibility** | not started | none |
| **E — dev runtime slice** | not started | none |

**Conclusions reached**

**D.** The property and binding engine fits its budget, and what it measured is adopted as AD-04a and AD-04d
(*Proposed*), not as one *Decided* entry: compact CSR index with
eager-push evaluation, Kahn ordering kept out of Release and excluded at link time, 64 B per animation slot. Measured
56 B per property and 39.3 B per binding, all recorded budgets passed. Storage layout (AD-04b) and the
evaluation model (AD-04c) stay **Open**, because the prototype tested neither runtime index mutation nor
diamond dependencies nor the per-chain-layer passes it itself flagged. This unblocks the Phase 2 property layout and the
binding representation Prototype B depends on.

**A2 (macOS).** Platform presentation works on AppKit: steady cadence 16.67 ms p50, 60/60 frames presented through a
live resize, 32 event dispatches inside a 500 ms nested modal loop. The frame tail that motivated the slice was attributed
to layer-recycling back-pressure rather than GPU submission (`t_acquire` p99 20.75 ms against 0.12 ms for encode and
submit), which produced A2 Revision R1: the input-to-present budget is 25.0 ms, not 16.7 ms. Run-to-run p99 variance was
17.7 to 45.4 ms across five runs of one binary, so a single tail sample is not a decision input on this machine. Window
roles, popups and modality remain unimplemented.

**A1 — renderer (Skia).** Three findings stand out:

1. *Ganesh strokes lines on the CPU.* A stroked retained batch cost 10,470 ms per frame at 1.8 × 10⁶ segments, all
   inside `skcpu::Draw::drawDevicePoints` → `StrokeTessellateOp`. The same geometry as pre-tessellated filled quads
   costs 0.18 ms, roughly 55,000× less. A §11.2 line batch must be filled quads or the O(1) promise is meaningless.
2. *Ganesh cannot present this workload at 60 Hz.* Present interval p50 was 149.7 ms with the GPU saturated, while the
   scheduling itself was sound: recording p50 0.18 ms, 0.219 ns per segment, O(1) pan/zoom ratio 0.976×, zero frames
   dropped.
3. *The frame window is layer-bound, not thread-bound.* A1.1's render-thread ownership was refuted for Ganesh on macOS
   because `nextDrawable` blocks about one display cycle; A1.2a restored a 16.68 ms p50 cadence by presenting on the UI
   thread and recording on a worker, but left the UI thread blocked a median 16.59 ms, since `CAMetalLayer` exposes no
   non-blocking availability check.

Two pre-registered thresholds failed and stay failed: appending to a retained batch is O(n) (94–120 ms against a 25 ms
budget), and a 10⁶-segment frame costs 220 draw calls because Skia indexes a vertex batch with 16 bits. Workload W3 was
deadline-limited at 130 of 400 frames and is recorded as invalid rather than as a result.

**A1.4 — Graphite, offscreen.** Graphite links once the port's `graphite` feature is enabled; the earlier claim that
milestone 148 lacked it was a configuration mistake, corrected in `1e6aa68`. Offscreen, Graphite's CPU recording is
**1.24× slower than Ganesh at p50 and 3.85× worse at p99 per segment**, while the O(1) pan/zoom claim holds on both. Its
submit is asynchronous, so this slice measured no GPU throughput, and presenting through Graphite is blocked:
`createBackendTexture` needs a `TextureInfo` and `TextureInfo` has no public constructor from backend data.

**A1-Dawn — renderer (WebGPU), same workloads, seeds and thresholds as the Skia runs:**

- *Both Skia structural limits disappear.* 1.8 × 10⁶ segments draw in **one** `Draw(6, 1800000)` call where Skia
  needed 220, and present interval p50 is **16.62 ms** — one 60 Hz interval — where Ganesh managed 149.7 ms. Recording
  is 0.085 ms p50 and 0.114 ns per segment; W2 recording is 30× cheaper at 0.238 ms p99.
- *The comparison is valid.* Hover hit ratio is 99.41% on both renderers to the digit, which can only happen if both
  were fed byte-identical geometry.
- *Multi-window isolation does not exist when presenting on the UI thread, on either renderer.* Window 2's interval
  during window 1's 100 ms hold is 100.86 ms p50 — the hold itself, not GPU contention. This is the evidence behind
  AD-18's open question.
- Text was counted but never drawn: Dawn has no glyph stack.

These results make Dawn *Proposed* as candidate primary HAL (AD-28) with four blocking items: D3D12 and Vulkan
unvalidated, no text pipeline, no software rasterizer against AD-26, and build cost unevaluated. Skia is **not** retired.

**Pixel-parity findings (Prototype A1-Parity, `prototypes/a1_parity/RESULTS.md`).** Both engines render
deterministically (T2: 0 differing bytes across repeats) and Dawn's offscreen readback is exact (T1: 1024000 of 1024000
pixels). With **both engines drawing the same cosmetic filled quads**, the solid stroke interior is identical (the frame
centre reads `255 64 32 255` in both) and zoom-1 disagreement is 15.41%. **An isolated segment narrows it to a half-pixel:** on one clean
horizontal line, Skia lays down the full requested ink (1.000 px at width 1.0, 2.000 px at width 2.0) while Dawn lays down
exactly half of it (0.500 and 1.000). **Dawn does draw** — the earlier "draws nothing" result was a harness artifact, an
upload keyed on buffer existence rather than on the resource set, so a second set drew stale geometry. What remains is a
sub-pixel placement difference between Skia's analytic rasterisation and Dawn's MSAA resolve, not attributed. Averaged
over 4x4 blocks on the batch, 54.12% of blocks differ. Two instruments were reviewed for correctness before their verdicts were relied on. The
±4 per-pixel agreement threshold is sound and its 0.00% is real — delta bands show nothing in 1-4/5-16 and 116,763 pixels
in 129-255, the half-pixel row signature. The stroke-width ratio was rebuilt on the isolated segment and now gives real
verdicts: Skia 1.00× (cosmetic confirmed), Dawn 2.00×, inconclusive but diagnostic — full width once the stroke covers a
pixel centre, so the half-ink at zoom 1 is a sub-pixel placement effect. Zoom-1 disagreement is 15.41% and zoom-8 is
75.72%, the latter dominated by the recorded Skia stroke defect rather than by parity. **A renderer cannot yet be shown to produce Skia's image; the swap question stays open.**

**Platform pacing finding.** Windowed frame pacing on this machine is not reproducible: a standalone program with no albor
code drives a `CADisplayLink` at 58 Hz or at exactly 0 Hz, apparently at random, whenever a `CAMetalLayer` is attached.
A2's design sits in the failing configuration. **All windowed timings in Prototype A1 and Prototype A1-Dawn are therefore
unverified and are not repaired by A1-Parity**, whose own frames are driven deterministically offscreen. The cadence
question stays blocked on the platform.

### Prototype D — Property and binding engine

Scope: property storage, dependency tracking, dirty propagation and batched evaluation; ~10⁵ properties and ~10⁴ bindings; observable external state, environment properties (theme, locale) and animation-driven properties.
Candidates: compact dependency records, dirty bitsets with topological evaluation, shared dependency nodes.
Measures: memory per property and per binding, update latency, cache behavior, invalidation throughput.
Outputs: input to AD-04 (recorded in `prototypes/d/RESULTS.md`), to the property layout in Phase 2, and the binding representation required by Prototype B. Completed: the AD-04 decision and the Phase 2 property-layout question follow from it.

### Prototype A1 — Renderer (single platform)

The A2 platform window (AD-16 decides the windowing layer; this prototype does not) + render surface + Skia GPU renderer + `CustomRenderNode`/`RenderContext` + retained resources + workloads W1, W2 and W3 + pointer interaction + frame scheduling and the two-stage pipeline.
Questions: custom-render API shape (retained geometry, batch primitives), render-thread model, surface/device ownership, synchronization, Skia backend choice (Ganesh versus Graphite — measured offscreen in A1.4, presentation still open), and the software-renderer parity of Level 2 (not run).
A1 may use a convenience windowing library; it must not decide AD-16.

### Prototype A1-Dawn — Renderer candidate (WebGPU)

After AD-06. Dawn (WebGPU C++) through the A2 platform window, with the same retained resources, seeds and workloads
W1, W2 and W3 as Prototype A1, so the two renderers are comparable line for line. macOS only; no Skia dependency at all.
Questions: does instancing remove Skia's draw-call cap, and does a renderer that is not CPU-bound change the
multi-window result? Status: run and recorded; feeds AD-28 (*Proposed*) and AD-18's open presentation question.

### Prototype A1-Parity — Renderer output parity (Skia against Dawn)

After AD-28's candidate comparison. Links both Prototype A1 and Prototype A1-Dawn without modifying either; one generated
`ResourceSet`, one view transform, one clear colour, handed to both renderers so input is identical by construction. Frames
are driven deterministically into offscreen targets — no window, swap chain or display link — because the recorded
platform finding makes windowed pacing unreproducible here.
Questions: do the two renderers produce the same image for the same input, and is the measurement itself repeatable?
Status: run and recorded; both engines render deterministically and Dawn renders offscreen exactly, but output parity is
**not** established — interiors agree while coverage differs by about 15% at zoom 1 and 76% at zoom 8, and the half-ink on an
isolated segment is not yet attributed. Both flagged instruments were checked and both now give real verdicts (the ±4
per-pixel threshold is sound; the stroke-width ratio was rebuilt on the isolated segment). The GPU-vs-GPU column is
deferred.

### Prototype A2 — Platform presentation

After AD-16, which is Decided. Per-platform surface creation, presentation, resize behavior (including nested resize loops), frame callbacks, DPI changes, window roles, popups and modality on macOS, Windows and Wayland (X11 for completeness).
Questions: GPU backend viability per platform, Wayland roles/popups, and nested-loop behavior.

### Prototype B — Generated-code build performance

Stub runtime + compiler + approximately 500 generated components, with the binding representation from Prototype D, minimal headers, generated implementation files and write-if-changed generation.
Measures: clean build, local `.ui` change, interface change, unrelated application rebuild, generated file sizes, and translation units rebuilt.
Requires a **baseline** (an equivalent UI in the current workflow) so "significantly faster" is checkable. Also assesses distribution cost (Skia and runtime build time, AD-19).

### Prototype C — Text, IME and accessibility

Minimal text control with keyboard editing, IME, selection, cursor, DPI changes and accessibility, on macOS, Windows and Linux Wayland. Includes the platform text-accessibility model and a text field inside a virtualized row (focus and IME pinning).

### Prototype E — Dev runtime slice

Reload adapter + IR loader + restricted expression VM + descriptor tables on the prototype runtime.
Questions: does the layout-invariance rule hold (identical runtime binary, no Release cost beyond the null-checked hooks), what is the dual-engine conformance gap on a starter corpus, can nodes be created and patched by key, and what is the descriptor-table size.

These prototypes address the highest-risk assumptions before full-scale implementation.

---

## 31. Implementation Phases

**Phase 0:** AD-16 (*Decided*, with its evaluation and reversal criteria still to be written); Prototypes D, A1,
A1-Dawn, B, E and C (parallelizable); then A2. D is complete; A1 and A1-Dawn are complete on macOS; A2 is complete on
macOS with Windows and Wayland `PENDING_CI_MATRIX_RUN`; B, E and C have not started.

**Phase 1:** Application, EventLoop, tasks/timers, Window and `WindowHandle`, platform abstraction (including the **headless backend**), renderer, software renderer, frame scheduler and two-stage pipeline.

**Phase 2:** UI tree, property storage (per Prototype D), events, **semantic tree with explicit/implicit keys**, focus, input routing, and the **in-process automation core with a test harness**. Automated UI testing begins here.

**Phase 3:** Reactive binding system, observable state, environment properties, and the **animation core** (property animation, scheduler integration).

**Phase 4:** **Resources** (fonts, images), text, layout, DPI, RTL, i18n core (locale and direction environment properties, catalogs), basic controls, and the first platform text-accessibility slice.

**Phase 5:** Parser, semantic analysis, typed IR, C++ code generation, manifest and glue generator, incremental compilation, write-if-changed output, and the Python automation client.

**Phase 6:** Widgets, models and virtualization (with item keys), styling and themes, commands, and basic platform accessibility adapters. Every widget's definition of done includes semantic role/actions/states and semantic tests.

**Phase 7:** Clipboard, dialogs, menus, drag/drop, notifications.

**Phase 8:** LSP, preview, inspector, diagnostics.

**Phase 9:** Dev runtime: out-of-process live reload, serialized IR, patching, stable identity, state preservation.

**Phase 10:** Accessibility and automation platform hardening, and the MCP adapter.

**Phase 11:** Full i18n formatting/pluralization, reduced motion, X11 Tier 2 completion and platform polish.

---

## 32. Repository Layout

```text
albor/
├── core/ application/ ui/ layout/ input/ focus/ commands/ model/
├── text/ style/ animation/ window/
├── platform/
│   ├── macos/
│   ├── windows/
│   ├── linux/
│   │   ├── wayland/
│   │   └── x11/
│   └── headless/
├── render/ render-skia/ render-software/ widgets/ semantics/ accessibility/
├── services/ resources/ i18n/
├── compiler/ manifest/ language-server/ preview/ dev-service/
├── devrt/ automation/ inspector/
├── tools/ examples/ tests/
```

---

## 33. Example Application

```cpp
#include <albor/application.hpp>

int main() {
    albor::Application app;
    auto window = app.create_window({
        .role = albor::WindowRole::Normal
    });
    window.get()->set_content<MainWindow>();
    window.get()->show();
    return app.run();
}
```

The application does not need to understand compiler internals, generated tree representation, Skia resource management or native event translation.

---

## 34. Example `.ui` and Manifest

Illustrative only; the grammar is still open.

```text
// manifest: simulation.ui-manifest
native object Simulation {
    in property<bool> ready;
    in property<Snapshot<WaveformSet>> results;
    command run;
    preview { ready: true; results: sample("tran-simple-01"); }
}

native component ChartView {
    in property<Snapshot<WaveformSet>> data;
    semantic role: "chart";
    semantic children: virtualized;      // provided by the application's SemanticProvider
    preview { placeholder: "chart"; }
}

native global Commands {
    command run_simulation: Simulation.run;
}
```

```text
// MainWindow.ui
component MainWindow {
    property<string> title: @tr("Xyce Studio");

    Column {
        spacing: 8px;

        Button {
            id: run_button;
            command: Commands.run_simulation;     // label, enabled state, shortcut, semantics
        }

        ChartView {
            id: results_chart;
            data: Simulation.results;
        }
    }
}
```

The `Column` has no explicit `id` and therefore only an implicit key (§18.1). `run_button` and `results_chart` have explicit identity usable in automation selectors.

---

## 35. Remaining Open Questions

1. Exact `.ui` grammar, property and binding syntax (including expression purity and any two-way binding).
2. Typed per-component property storage to replace the type-erased slot of AD-04, including the memory target that Phase 2 must reach.
4. Generated C++ splitting strategy (Prototype B).
5. Manifest syntax details and the preview-plugin ABI.
6. ABI and versioning beyond 1.0 (AD-19 covers pre-1.0).
7. Skia backend strategy, including Ganesh versus Graphite and its threading consequences (Prototype A1).
8. Windows GPU backend (Prototype A2).
9. Wayland and X11 presentation implementation (Prototype A2).
10. Headless and software renderer implementation.
11. Final `RenderContext` API (Prototype A1).
12. Level 3 native GPU interop.
13. Modal/popup implementation details per platform.
15. Text implementation and library.
16. Layout algorithm.
17. Animation API details.
18. Command history API.
19. Automation transport and MCP schema.
20. Live-reload wire protocol and state migration.
21. Resource build pipeline.
22. Platform accessibility details.
23. Initial widget set.

Resolved in v3 (no longer open): window role/modality model, single source for scale factor, custom-canvas semantics, MCP routing, identity policy, automation build exclusion, compiler language (Proposed), X11 tier (Proposed), manifest authoring model (Proposed), property representation and binding invalidation algorithm (AD-04), platform implementation strategy (AD-16).

---

## 36. Success Criteria

### Production

- Native C++ runtime.
- No interpreter, registry, compiler frontend, or automation channel in Release.
- Stable, small generated headers; write-if-changed generation.
- Build-scope tests show a `.ui` body change rebuilds only the expected translation units.
- Measured build-time results against the baseline.

### Rendering

- GPU accelerated; no OpenGL requirement.
- Custom rendering through the framework rendering architecture using retained resources and batched primitives.
- Workloads W1, W2 and W3 meet thresholds recorded before the run.
- Software renderer parity for all Level 2 primitives.
- No requirement to convert custom content into bitmap/image widgets.

### Platform

- Tier 1: macOS, Windows, Linux Wayland.
- Tier 2: Linux X11.
- Correct behavior during nested native loops.

### UI

- Declarative composition, reactive properties, layout.
- Focus and navigation, text and IME, DPI and multi-monitor.
- Menus and dialogs, models and virtualization.

### Tooling

- LSP and standalone preview.
- Development live reload via the dev runtime.
- Inspector and diagnostics.
- Dual-engine conformance passing.

### Accessibility and automation

- Shared semantic model including custom canvases.
- Platform accessibility adapters.
- Stable explicit automation identities.
- Python automation; MCP adapter via automation.
- The reference test (Appendix B) passes against a sample application.

### Engineering

- Measured frame and input latency.
- Headless CI path.
- Clear production/development dependency boundary enforced at link time.

---

## 37. Architectural Summary

Albor's architecture rests on explicit boundaries:

- the compiler is not the runtime;
- development tooling is not the production application;
- the dev runtime extension is a separate library, not a variant of the runtime;
- windowing is not rendering;
- Skia is not the public UI architecture;
- scene building is not render execution;
- application data reaches custom rendering as immutable snapshots;
- identity is explicit, correlation hints are not identity;
- automation is not accessibility, and does not ship in production;
- application state is not UI implementation state;
- generated implementation is not the public C++ interface;
- the manifest, not C++ source, defines native component interfaces.

The next implementation work validates these boundaries through Prototypes D, A1, B, E and C, then A2 after the platform decision, before expanding the framework surface.

---

## Appendix A — Review Traceability

| Finding (source) | Addressed in |
| --- | --- |
| Live reload still needs an in-app interpreter/registry; "no reflection" only reworded (v2 review 1; second review disagreed with "resolved") | §2, §17.2, AD-02, Prototype E |
| No module owns the IR evaluator / reload adapter; headless module missing | §6, §32 |
| Preview of native components has no implementation source | §15.3, §17.3 |
| Dev/prod runtime ABI divergence | §2.3 |
| Compiled vs. dev semantic divergence | §2.4, AD-25, §27 |
| Level 2 vs. Level 3 incoherent ("GPU resources") | §11.1, §11.3 |
| No retained geometry; per-segment cost | §11.2, §11.3 |
| Render threading and worker-data ownership undefined | §10.2, §10.5, AD-18 |
| Multi-window presentation stalls (second review) | §10.3, workload W3 |
| Primary renderer HAL undecided; Dawn is a candidate, Skia retained | §11, AD-05, AD-06, AD-28 |
| Dawn has no text or glyph pipeline; no first-party software rasterizer | §21.2, AD-26, AD-28 |
| Replacing Skia means rebuilding the 2D feature surface: joins, caps, dashes, clips, gradients, layers, effects | §11.4, AD-06, AD-28 |
| Skia Graphite on a Dawn backend as a middle option, untested | AD-28 |
| Pixel parity Skia vs Dawn: Dawn lays down half the requested ink on an isolated line; sub-pixel placement not attributed | AD-28 |
| Windowed frame pacing not reproducible on this platform; windowed timings unverified | AD-18, AD-28 |
| Benchmark workload underspecified; no failure condition | §28.3, §28.4 |
| Software/GPU parity | §11.7, §25, §27 |
| Custom-canvas semantic children/automation regressed | §19.3 |
| No phase builds semantic tree/automation; accessibility late | §31 (Phases 2, 4, 6, 10) |
| Reference test lost | Appendix B, §27, §36 |
| MCP routing inconsistent | §19.7, AD-20 |
| "Explicitly enabled" ambiguous | §2.2, §19.6, AD-10 |
| Identity of unnamed nodes; position-derived keys | §18.1, AD-11 |
| Model row keys; instance identity | §18.2, §20.1 |
| `Application::post`/timers; lifetime safety | §7 |
| `Window&` lifetime; close veto | §8.4 |
| Role vs. modality; incomplete `WindowOptions`; Tool on Wayland | §8.1, §8.2, AD-08 |
| `RenderSurface` lost present/resize; scale factor location | §9 |
| Nested native loops absent | §7.4, AD-21 |
| `.ui` example vs. command model; manifest lacks objects/globals | §22.1, §15.2, §34 |
| `px` naming | §9.3, AD-27 |
| Binding precedence hedged; reload interaction | §13.4, AD-24 |
| No binding-engine prototype | Prototype D |
| Prototype B lacks baseline; depends on binding representation | Prototype B |
| Prototype A depends on AD-16; platform/renderer concerns entangled | Prototype A1/A2, §24.1 |
| Animation, i18n, resources too late | §31 (Phases 3, 4) |
| X11 tier undefined | §4.1, AD-22 |
| ABI/Skia distribution unaddressed | AD-19 |
| Compiler language unstated | §16, AD-23 |
| Manifest ergonomics; scanners vs. no-C++-parsing (second review) | §15.1, AD-17 |
| Generated-header "abstract interface" wording (second review) | §14.1, AD-03 |
| Event coalescing; hit-test tolerance | §12, §11.2 |
| Layout algorithm and contract | §21.1 |
| Second review: "freeze v2" recommendation | Not adopted: v3 revises v2 instead |

## Appendix B — Reference Automation Test (Compatibility Target)

The following test captures the desired ergonomics of the automation layer. It should remain a compatibility target for the conceptual API; Albor does not require this exact test implementation. It demonstrates semantic selectors, actions, synchronization, stable IDs and collection queries.

```python
def test_run_simulation_shows_the_charts_panel(self) -> None:
    xyce = shutil.which("Xyce")
    netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"

    if xyce is None:
        self.skipTest("Xyce executable not found")

    with TestSession(
        launch(args=["--netlist", str(netlist), "--xyce", xyce]),
        self.id()
    ) as app:
        app.get_by_type("ToolbarButton").nth(5).click()
        app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
        self.assertEqual(app.get_by_type("ChartView").count(), 1)
```

Note: the test uses `.nth(5)` on a type selector. Under §19.4 and §18.1, a stable-ID equivalent (`get_by_id("MainWindow::run_button")`) is the preferred form. The positional version is retained here as a compatibility target for intentionally repeated elements, not as a model of identity.
