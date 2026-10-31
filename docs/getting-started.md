# Getting started

## Requirements

- **C++23** toolchain
- **CMake** ≥ 3.28 and **Ninja** — the presets pin Ninja as the generator, so a build directory
  configured with another one has to be configured fresh
- **[vcpkg](https://vcpkg.io)** in manifest mode — dependencies are pinned in `vcpkg.json`
  and are fetched on the first configure. The build declares the floor to the install itself,
  so the ports are stamped with the same 13.3 the framework is tested at; a port built for a
  newer floor links with a warning and may call an API the minimum system lacks
- **macOS 13.3 or later** on Intel or Apple silicon for the macOS targets, or a supported
  Windows 10/11 MSVC toolchain and SDK for the Windows targets. See
  [Status and platforms](status.md)
- **No shader toolchain is needed to build** — the Metal and Direct3D shaders ship as
  compiled artifacts. Only a change to a shader source needs the matching compiler; see
  [Tooling](tooling.md#the-shaders)

## Build the repository

```sh
cmake --preset debug          # configure (fetches vcpkg dependencies)
cmake --build --preset debug  # build
```

Other presets: `release`, `test`, `coverage`. Unit tests are a plain executable — run it
directly rather than through `ctest`:

```sh
./.build-debug/tests/albor-tests
```

## The target you link

An application names a window and the component it shows. It does not name a renderer, a
platform adapter or a text engine; the framework target carries the recommended selection.
All of those implementations install themselves at load, so **linking is still the
selection**, but a normal application makes that choice once.

| target | what it carries |
| --- | --- |
| `albor` | the platform adapter, HarfBuzz, widgets, layout, accessibility, i18n and the recommended GPU renderer |
| `albor-blend2d` | the same runtime with the portable Blend2D renderer |
| `albor-devrt-auto` | dev runtime: hot swap of `.ui` structure (optional, see [Tooling](tooling.md)) |

`albor` is available on the desktop platforms with an adapter (macOS and Windows today).
On a platform without a desktop adapter, use the headless or granular targets; see
[Consuming Albor](consuming.md#advanced-composition).

## Embedding fonts

A `.ui` file names a family and a weight; the build carries the bytes. Declare the faces
once per target with `albor_build_font_assets` before `albor_build_ui_component`, then name
the generated default family before the first window is created. The full shape, the weight
rules and the diagnostics are on [Fonts](ui-language/fonts.md).

## Use it from another project

The vcpkg package is the published route: one dependency, and the installed package answers
for the platform, the text engine, the renderer and the codegen tools.

```json
{
  "dependencies": [{ "name": "albor" }]
}
```

```cmake
find_package(albor CONFIG REQUIRED)
add_executable(my_app main.cpp)
albor_build_ui_component(my_app "${CMAKE_CURRENT_SOURCE_DIR}/greeting_panel.ui")
target_link_libraries(my_app PRIVATE albor)
```

Until a registry carries the framework, the port ships in the repository and the consumer
wires it as an overlay:

```json
{
  "overlay-ports": ["../albor/ports/albor"],
  "overlay-triplets": ["../albor/.triplets"]
}
```

The default install carries the recommended GPU runtime, and `albor[blend2d]` adds the
portable CPU renderer; the transitional skia reference is not a package surface — it stays
a source-tree backend for the conformance work and retires with issue 111. The detailed
consumer map, advanced composition and troubleshooting are on [Consuming
Albor](consuming.md).

The source route names the framework as a subtree: `FetchContent` — or `add_subdirectory` of
a vendored copy — and every framework target publishes the framework's own include root, so
`#include "application/application.h"` resolves from the consumer's sources.

```cmake
include(FetchContent)
FetchContent_Declare(albor
    GIT_REPOSITORY https://github.com/AlborCore/albor.git
    GIT_TAG        <tag-or-commit>)   # never a moving branch
FetchContent_MakeAvailable(albor)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE albor)
```

The default target carries the platform, text engine, widgets, layout, accessibility, i18n
and the recommended GPU renderer. Link `albor-blend2d` instead for the portable CPU
renderer; both backends are CMake options there (`ALBOR_RENDERER_SKIA`,
`ALBOR_RENDERER_BLEND2D`), whose defaults keep a source build complete.

With the source route, the dependencies come from vcpkg: a consumer in manifest mode lists
the same packages in its own `vcpkg.json`, or points `VCPKG_MANIFEST_DIR` at the fetched
sources so the pinned versions of the framework's manifest are the ones built — that needs
the sources to exist at the first configure, so a vendored copy rather than a fetch that has
not run yet. With the sources present, `VCPKG_OVERLAY_TRIPLETS` pointed at the framework's
`.triplets` adds the framework's own triplets: the ports are stamped through the triplet
itself, and a missing floor is a configure error rather than a link-time surprise.

The floor is the consumer's to declare, and it has to be in place before the first
configure, because the install runs inside `project()` and inherits it from the environment:
export `MACOSX_DEPLOYMENT_TARGET` with the application's floor — 13.3 at the lowest, higher
when the application requires a newer system — and give `CMAKE_OSX_DEPLOYMENT_TARGET` the
same value when naming it explicitly. A floor below 13.3 is rejected while the framework
configures: the shipped shaders and the test suites do not cover it. In the package route
the same refusal runs inside `albor-config.cmake`, so an installed consumer is refused as
early as a source one.

## A component from a `.ui` file

Write `greeting_panel.ui` (the full language is covered in
[The .ui language](ui-language/index.md)):

```text
component GreetingPanel {
    in property<string> user_name: "World";
    inout property<int> click_count: 0;

    signal run_clicked();

    Column {
        spacing: 12px;
        padding: 16px;

        Label {
            text: @tr("Hello, ") + user_name + "!";
        }

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
```

Generate and build it from CMake. The shared rule runs the codegen driver as a build step
and wires the generated units into your target:

```cmake
add_executable(my-app main.cpp)

# runs albor-codegen at build time; output lands in build/generated/my-app/
albor_build_ui_component(my-app "${CMAKE_CURRENT_SOURCE_DIR}/greeting_panel.ui")

target_link_libraries(my-app PRIVATE albor)
```

!!! note
    The helper `albor_build_ui_component` comes from `build/AlborComponent.cmake`, which the
    repository's top-level build includes. Editing the `.ui` file rebuilds only that
    component's generated translation unit — not the whole tree.

When a `.ui` file imports another one, declare the imported file first and name it in
`IMPORTS`:

```cmake
albor_build_ui_component(my-app "${CMAKE_CURRENT_SOURCE_DIR}/side_panel.ui")
albor_build_ui_component(my-app "${CMAKE_CURRENT_SOURCE_DIR}/main.ui" IMPORTS "${CMAKE_CURRENT_SOURCE_DIR}/side_panel.ui")
```

A relative `IMPORTS` path resolves beside the importing `.ui` file, which is where the
language resolves it. The helper compiles both files into the same target and keeps the
imported header ahead of the unit that includes it. `greeting_panel.h` and `greeting_panel.cpp` appear under the build
directory. The header is a **concrete facade**: property accessors, signal slots and
construction — no abstract base, no virtual dispatch.

## Open the first window

```cpp
#include <cstdio>

#include "application/application.h"
#include "greeting_panel.h"

int main() {
    // the application owns the native loop, the window registry and the task dispatcher
    albor::Application& application = albor::Application::instance();

    albor::WindowOptions options;
    options.title = "Albor — Greeting";
    options.size = albor::Size{800.0f, 600.0f};

    // the application builds the component over its engines and hands back the facade
    albor::GreetingPanel* panel = nullptr;
    const albor::WindowHandle handle = application.create_window(options, panel);
    if (!handle.valid() || panel == nullptr) {
        std::fprintf(stderr, "my-app: the window could not be created\n");
        return 1;
    }

    // the generated signal slot is how a host reacts to the component's signals
    panel->set_on_run_clicked([] { std::printf("the run_clicked signal fired\n"); });

    // run() returns when the last main window closes
    return application.run();
}
```

Because `click_count` is bound to the `Label`, incrementing it from anywhere — the handler
above, a timer, application code through `panel->set_click_count(...)` — updates the text on
the next frame. That is the [binding model](concepts/bindings.md) in one sentence.

## The examples

Complete, buildable applications live in the repository's `examples/` directory:

| example | what it demonstrates |
| --- | --- |
| `hello-window` | an empty window with no generated component |
| `hello-world` | the same window through the default GPU target and through `albor-blend2d` |
| `greeting` | a compiled `.ui` component with signals, labels and a timer |
| `form` | every shipped widget, plus the alignment patterns of `align_panel.ui` |
| `reactive-counter` | hand-written C++ components on the reactive property engine |
| `chart` | a manifest-declared native component with custom rendering |
| `commands` | one shared `Command` bound across button, menu row and shortcut |

After `cmake --build --preset debug`, each example binary mirrors its source path under the
build directory, for example:

```sh
./.build-debug/examples/greeting/albor-greeting
```

## Where to go next

- [Consuming Albor](consuming.md) — the public targets and advanced composition.
- [The .ui language](ui-language/index.md) — the language, form by form.
- [Properties and bindings](concepts/bindings.md) — how state moves.
- [Components](components/index.md) — the element catalogue.
- [Tooling](tooling.md) — codegen, hot swap, automation.
