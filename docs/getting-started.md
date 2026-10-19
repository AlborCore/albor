# Getting started

## Requirements

- **C++23** toolchain
- **CMake** ≥ 3.28
- **[vcpkg](https://vcpkg.io)** in manifest mode — dependencies are pinned in `vcpkg.json`
  and are fetched on the first configure. Export `MACOSX_DEPLOYMENT_TARGET=13.3` so the ports
  build for the same floor, which CMake reads and vcpkg forwards to them; a port built for a
  newer floor links with a warning and may call an API the minimum system lacks
- **macOS 13.3 or later** on Intel or Apple silicon to build and run the macOS targets (the
  example applications drive the AppKit platform adapter directly; see
  [Status and platforms](status.md))
- **No Metal toolchain is needed to build** — the shader ships as a compiled artifact. Only a
  change to the shader itself needs `xcodebuild -downloadComponent MetalToolchain`; see
  [Tooling](tooling.md#the-metal-shader)

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

## The pieces you link

An application names a window and the component it shows. It does not name a renderer, a
platform adapter or a text engine: **linking is the selection**. Each backend installs itself
at load, so the libraries a target links decide what ships inside it.

| target | what it carries |
| --- | --- |
| `albor-application` | event loop, tasks, timers, window registry |
| `albor-platform-macos-auto` | the AppKit window and input adapter |
| `albor-render-skia-auto` | the Skia renderer backend (Metal) |
| `albor-text-harfbuzz-auto` | HarfBuzz text shaping |
| `albor-widgets`, `albor-layout` | the widget and container set |
| `albor-accessibility`, `albor-i18n` | semantic/accessibility adapters, translation catalogs |
| `albor-devrt-auto` | dev runtime: hot swap of `.ui` structure (optional, see [Tooling](tooling.md)) |

## Use it from another project

`FetchContent` — or `add_subdirectory` of a vendored copy — brings the framework in as a
subtree. Its tests and examples stay off, because they belong to developing the framework,
and every `albor-*` target publishes the framework's own include root, so `#include
"application/application.h"` resolves from the consumer's sources.

```cmake
include(FetchContent)
FetchContent_Declare(albor
    GIT_REPOSITORY https://github.com/AlborCore/albor.git
    GIT_TAG        main)          # a tag or a commit, never a moving branch
FetchContent_MakeAvailable(albor)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE
    albor-application albor-platform-macos-auto
    albor-render-albor-auto albor-text-harfbuzz-auto)
```

The dependencies come from vcpkg. A consumer in manifest mode lists the same packages in
its own `vcpkg.json`, or points `VCPKG_MANIFEST_DIR` at the fetched sources so the pinned
versions of the framework's manifest are the ones built. Export
`MACOSX_DEPLOYMENT_TARGET=13.3` so the ports build for the framework's floor.

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

target_include_directories(my-app PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(my-app PRIVATE cxx_std_23)

target_link_libraries(my-app PRIVATE
    albor-platform-macos-auto albor-render-skia-auto albor-text-harfbuzz-auto
    albor-widgets albor-layout albor-accessibility albor-i18n)
```

!!! note
    The helper `albor_build_ui_component` comes from `build/AlborComponent.cmake`, which the
    repository's top-level build includes. Editing the `.ui` file rebuilds only that
    component's generated translation unit — not the whole tree.

`greeting_panel.h` and `greeting_panel.cpp` appear under the build directory. The header is a
**concrete facade**: property accessors, signal slots and construction — no abstract base, no
virtual dispatch.

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

- [The .ui language](ui-language/index.md) — the language, form by form.
- [Properties and bindings](concepts/bindings.md) — how state moves.
- [Components](components/index.md) — the element catalogue.
- [Tooling](tooling.md) — codegen, hot swap, automation.
