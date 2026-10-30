# Albor

**A cross-platform declarative UI framework for C++.**

Albor compiles a compact `.ui` declarative language directly to native C++ — no
interpreter, no reflection, no runtime UI evaluator in production builds. It pairs
conventional desktop controls with first-class support for high-throughput custom
rendering (schematics, CAD views, waveforms, plots), real multi-window behavior with
modality, and a two-stage render pipeline that never blocks the UI thread on drawing.

> **Status: under heavy development.** macOS (AppKit + Metal) and Windows
> (Win32 + Direct3D) are implemented and built in CI; the Linux (Wayland/X11/Vulkan)
> port follows. `render-albor`, the in-repo GPU 2D renderer, draws shapes, retained
> resources, outlined text, shadows, composited layers and all Porter-Duff composition
> modes, and is gated against a software reference by a parity suite. Skia remains the
> transitional/reference backend. The architecture is settled and specified in
> [`albor-design-v4.md`](albor-design-v4.md).

## Why Albor exists

Albor was built out of concrete limitations hit while building a desktop application on
an existing declarative framework:

1. **A foreign-language runtime in the middle.** A Rust core under a C++ API means
   slow builds, extra binary size, and a real correctness hazard: on Apple Silicon, a
   C++ exception traversing the Rust boundary loses its RTTI, so the receiving C++
   code cannot catch it by type — which forces the runtime to be dynamically linked.
   Albor is pure C++ end to end: one language, one toolchain, static linking, one
   exception model.

2. **Custom rendering stopped at the abstraction.** Applications with domain-specific
   views — a circuit schematic editor is the motivating case — need direct access to
   the native surface, or a way to issue retained drawing commands that the renderer
   replays without going through a universal drawing API. Albor's render contract
   (`CustomRenderNode`, retained `LineBatch` / `PathResource` / `PictureResource`
   resources) is designed for exactly this workload, and the surface level is a
   supported, documented extension point rather than a workaround.

3. **No dialogs.** Without window roles and modality, every feature of an application
   collapses into one large single-window UI. Albor treats window roles, modality and
   close-request handling as core window behavior (§5), so dialogs are dialogs.

## Design pillars

These are non-negotiable invariants (AD-NN in the design spec); violating any of them
is a defect, not a style choice:

- **No interpreter in Release (AD-01).** Production builds contain no `.ui` parser, no
  IR evaluator, no type registry, no property-by-name reflection. `.ui` compiles to
  native C++.
- **Two-stage frame (AD-18).** The UI thread runs layout and records an immutable
  `FrameScene`; a render thread executes it. Render execution never calls application
  code.
- **Immutable snapshots cross threads (§10.5).** Application data reaches custom nodes
  and bindings as `std::shared_ptr<const T>` in an observable `Snapshot<T>`; workers
  never mutate live UI objects.
- **No platform or renderer types in public headers (AD-05).** Rendering, windowing,
  text shaping and property storage sit behind stable interfaces, so any subsystem can
  be swapped without breaking application code.
- **The manifest is the single source of truth (AD-17).** Native C++ components exposed
  to `.ui` are declared in native manifests; the C++ glue is generated, and the
  compiler never parses application headers.
- **Explicit identity (AD-11).** A node's author-assigned `id:` is its authoritative
  identity; generated keys are correlation hints only.
- **Concrete, non-virtual facades (AD-03).** Generated public headers expose a stable
  interface over a private implementation — never an abstract base with virtual
  dispatch.
- **Dev/Test features are excluded at link time, never with `#ifdef` (AD-04).** Class
  layout is byte-identical across Release, Dev and Test.

## The language

```
component GreetingPanel {
    in property<string> user_name: "World";
    inout property<int> click_count: 0;

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
                id: increment_button;
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

The compiler generates a concrete facade class per component — properties, signals and
children as a stable, non-virtual interface — and rebuilds only the translation units
that actually changed when a `.ui` body is edited (§14.3).

## Starting an application

An application names a window and the component it shows. It does not name a renderer, a
platform adapter or a text engine: each backend installs itself at load, so **linking is the
selection** (§5.3, §6.3, §9.1). A normal consumer links the framework's default target:

```cmake
target_link_libraries(my_app PRIVATE albor)
```

`albor` carries the platform adapter, text engine, widgets, accessibility, i18n and the
recommended GPU renderer. Link `albor-blend2d` instead for the portable CPU renderer. The
target map and the advanced composition escape hatch are on
[Consuming Albor](docs/consuming.md).

```cpp
#include "application/application.h"
#include "greeting_panel.h"

int main() {
    albor::Application& application = albor::Application::instance();

    albor::WindowOptions options;
    options.title = "Albor — Greeting";
    options.size = albor::Size{800.0f, 600.0f};

    // the application builds the component over the engines it owns and hands the facade back
    albor::GreetingPanel* panel = nullptr;
    const albor::WindowHandle handle = application.create_window(options, panel);
    if (!handle.valid()) {
        std::fprintf(stderr, "greeting: the window could not be created\n");
        return 1;
    }

    panel->set_on_run_clicked([] { /* react to the signal */ });
    return application.run();
}
```

`run()` returns when the last main window closes (§4.1). A window may also be added while
the loop is running, from a task or a timer; the run still ends with the last one.

`albor::MainWindowConfig` carries the per-window choices: the clear `background`, an
`always_render` escape hatch for a tree that draws non-reactive state, and a `scene_source` for
an application that owns its `FrameScene` and brings no tree at all. A retained tree renders on
invalidation by default; `always_render` is the exception, not the setup step. `application.set_frame_hook`
installs work that runs between a window's build and its submit; `application.set_input_handler`
observes the event **after** the tree has answered it, `event.handled` included (§8.3).

`application.root(handle)` hands out the tree a window drives, for a host that installs a
theme over it or reads its identity. The raw APIs stay public and documented as the escape
hatch: `application.stage(handle)` reaches the wiring itself, and
`create_window(options, RootBuilder)` builds a tree imperatively.

## Linking the dev runtime

`albor-devrt-auto` is the whole switch. An application that links it gets hot swap of
`.ui` structure and property values; an application that does not is unaffected, and a
reference to the extension from a Release target **fails to link** rather than compiling away
(§12.1, AD-01, AD-04). No application code changes between the two, and the framework itself
knows nothing about the extension beyond four null-checked hook calls.

Complete, buildable versions live in [`examples/`](examples/) — a hand-built reactive
counter, a manifest-driven chart with custom line-batch rendering, the compiled
`GreetingPanel` above, and the same window linked against each in-repo renderer
(`hello-world-albor` for the GPU backend, `hello-world-blend2d` for the raster one).

## What is implemented

| area | state |
| --- | --- |
| application, event loop, tasks, timers | done (§4) |
| startup facade: one call wires window, surface, pipeline, executor and input | done (§4.1, §6.1) |
| dev runtime extension: self-installing, patches a live tree in place | done (§12.1) |
| macOS platform: AppKit window, display-link pacing, input | done (§5) |
| `render-albor` GPU backend: fills, strokes, outlined text, retained resources, images, shadows, layers, fill rules, 11 composition modes | done on macOS and Windows (§6.2–§6.4, AD-26) |
| Skia renderer backend (Metal/Ganesh) | transitional/reference, not the default consumer backend (§6, #102) |
| software reference (Blend2D + HarfBuzz glyph atlas) and Blend2D windowed backend | done (§6.3, AD-26) |
| GPU/software parity suite, headless CI | done (AD-26, §16) |
| reactive properties, eager-push binding engine, snapshots | done (§7) |
| ui tree, components, event routing, focus | done (§8) |
| measure/arrange layout, row/column/stack/grid/padding | done (§8.2) |
| widgets: Button, Label, CheckBox, RadioButton, RadioGroup, Slider, NumericStepper, Canvas, Image, TextField, ComboBox, Panel, Card, ListBox, ScrollView, ProgressBar, TabView, Tab, TabTitle | done (§8) |
| semantic tree, explicit keys, semantic provider | done (§10.1) |
| `.ui` compiler, manifest, glue generator, codegen | done (phase 4, §11) |
| text shaping (HarfBuzz), embedded font registration and weights, i18n catalog | done (§9.1, AD-34) |
| animation engine, theme system and widget states | done (§7.3) |
| inspector, LSP, preview host, automation core, MCP adapter | done (Dev/Test only, phase 5) |
| accessibility: macOS NSAccessibility and Windows UI Automation bridges | done; AT-SPI adapter planned (§10.2) |
| Win32 platform: window, clipboard, event loop, text encoding, named-pipe automation | implemented and built in CI; Direct3D backend ships (§5.3) |
| Wayland / X11 / Vulkan platform and renderer | planned (phase 1 follow-up) |
| out-of-process dev service that delivers definitions to the extension | planned (§12.1, AD-12) |

## Building

Requirements: C++23, CMake ≥ 3.28 with Ninja, [vcpkg](https://vcpkg.io) (manifest mode;
dependencies are pinned in [`vcpkg.json`](vcpkg.json)).

```sh
cmake --preset debug          # configure (fetches vcpkg dependencies)
cmake --build --preset debug  # build
```

Unit tests are built into `./.build-debug/tests/albor-tests` — run that executable
directly.

## Consuming as a package

Third-party projects install the framework through vcpkg. The port ships inside this
repository (`ports/albor/`) and a consumer wires it as an overlay until a registry carries
it; the default install carries the recommended GPU runtime with no third-party renderer
backends, and the `blend2d` / `skia-reference` features opt into them:

```json
{ "dependencies": [{ "name": "albor" }] }
```

```cmake
find_package(albor CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE albor)
```

The consumer's manual, from a first window to renderer features, is
[Consuming Albor](docs/consuming.md).

## Releases

The [`release`](.github/workflows/release.yml) workflow makes a release one dispatch that a
human only reviews; every mechanical step is machine work. Merges to `main` are squash
commits and the branch may not be pushed to directly, so the flow leans on what a squash
repository can trust: **a main commit is the pinned source, and a tag on it is permanent
history**.

1. **Dispatch the workflow** with the version, from `main`, and nothing else:

   ```sh
   gh workflow run release.yml -f version=0.2.0
   ```

2. **The version PR** — automation patches the three version sites together (root
   `vcpkg.json`, `project(albor VERSION)`, and the port's `version-semver`), opens the PR
   with the release token, and hands it to GitHub auto-merge. The PR runs the full CI
   matrix like every other PR, and one review of the diff is the release decision.

3. **The tag** — once the version PR merges, automation tags that merge commit `v0.2.0`.
   Tag creation touches refs only, which the tag ruleset grants through the token; commits
   to `main` are never pushed by hand.

4. **The pin PR** — automation patches the portfile's one-line `REF` to the merge commit
   and opens it with auto-merge as well. That single line makes the package fetch **exactly
   the released tree**, whose port version is the one being released.

What makes the flow sound without manual steps:

- The package fetches its sources with `vcpkg_from_git` at the pinned commit. Git objects
  are immutable, so integrity needs **no hash of a tree that does not exist yet** — the
  classic reason release stamping needed a human hand. The tag decorates the same commit
  for humans; the pin is the commit.
- The pinned tree is the released tree: the pin PR lands after the version PR, so a package
  claiming 0.2.0 fetches sources whose `project(VERSION)` says 0.2.0. The `REF` never names
  the tree it lives in, so there is no circularity. Until the first release lands, the
  no-override consumption of the port is exercised through the release itself; the test bed
  keeps using the local-checkout override.
- [`build/check-port.sh`](build/check-port.sh) enforces the contract on every run: the
  port's version equals the project's, the dependency matrices agree (project / port /
  fixture, per feature), and the pinned `REF` names a commit that exists on GitHub.

One-time repository settings the flow needs:

- the **`RELEASE_PAT` secret** — a token with `contents: write` and `pull-requests:
  read/write` on this repository. The automation drives its PRs and merges with it, because
  events raised by the default `GITHUB_TOKEN` do not trigger the repository's CI.
- *Allow auto-merge* enabled,
- a tag rules entry that permits the release token to create `v*` tags.

## Documentation

- [`docs/`](docs/index.md) — the user guide, including
  [Consuming Albor](docs/consuming.md) for third-party applications.
- [`albor-design-v4.md`](albor-design-v4.md) — the architecture specification; the
  single source of truth for module layout and decisions (`AD-NN`, `§N` are cited
  throughout the code and reviews).
- [`gap-analysis-xyce-studio.md`](gap-analysis-xyce-studio.md) — the application-readiness
  analysis against the Xyce Studio reference application, with the ordered P0/P1 work list.
- [`STYLE-GUIDE.md`](STYLE-GUIDE.md) — code style rules enforced by
  `build/check-format.sh`.
- [`AGENTS.md`](AGENTS.md) — the contract for AI agents working in this repository.

## License

[Apache License 2.0](LICENSE)
