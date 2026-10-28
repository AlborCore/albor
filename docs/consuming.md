# Consuming Albor

This page is for an application that uses Albor as a library. The framework owns the
composition of its platform, text and rendering implementations; an application chooses a
widget tree and, when it needs to, a renderer.

## The target you link

Normal applications link one target:

```cmake
target_link_libraries(my_app PRIVATE albor)
```

`albor` is the recommended runtime. It carries the platform adapter, HarfBuzz text shaping,
the widget and layout set, accessibility, internationalization, resources, and the
recommended GPU renderer. The platform adapter, text engine and renderer are selected at
configure time; there is one platform per build and no runtime backend choice.

The portable CPU choice keeps everything except the renderer the same:

```cmake
target_link_libraries(my_app PRIVATE albor-blend2d)
```

| target | renderer | availability |
| --- | --- | --- |
| `albor` | `render-albor` GPU (Metal on macOS, Direct3D on Windows) | macOS and Windows |
| `albor-blend2d` | Blend2D CPU rasterizer | macOS and Windows |
| advanced `-auto` targets | explicit choice, see below | where the module is built |

The umbrella is offered only where a desktop platform adapter exists. Linux currently has
the headless platform only, so `albor` and `albor-blend2d` are not defined there until the
Wayland/X11 adapter lands; use the granular targets below for a headless or test
application.

## A minimal consumer project

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app LANGUAGES CXX)

# the fetched copy builds the framework and nothing else; set these before MakeAvailable
set(ALBOR_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALBOR_TESTS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(albor
    GIT_REPOSITORY https://github.com/AlborCore/albor.git
    GIT_TAG        <tag-or-commit>)   # never a moving branch
FetchContent_MakeAvailable(albor)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE albor)
```

`ALBOR_EXAMPLES` and `ALBOR_TESTS` already default to off when Albor is not the top-level
project, so the explicit settings above are optional; they document the choice.

The framework targets publish the framework include root, so a consumer does not add
`target_include_directories` for `application/application.h` or a generated component
header.

## Choosing the renderer

This is the only renderer decision a normal application makes. Both targets carry the same
runtime, so switching renderers does not change application code.

| target | when to use it |
| --- | --- |
| `albor` | the default: GPU rendering through the in-repo backend |
| `albor-blend2d` | a portable CPU path, or a platform where the GPU backend is not the recommended default |

Do not link both, and do not add a second renderer target to an umbrella target. Backends
install themselves at load through whole-archive registration; two renderers in one binary
compete for the same registration and produce duplicate or ambiguous output.

## What the default carries

The non-render half is shared by `albor` and `albor-blend2d`:

| module | why it is in the default |
| --- | --- |
| `albor-platform-auto` | the platform window, input and event loop adapter |
| `albor-text-auto` | HarfBuzz shaping and font resolution |
| `albor-layout`, `albor-widgets` | containers and the standard widget set |
| `albor-accessibility` | the platform semantic/accessibility bridge |
| `albor-i18n` | translation catalogs and RTL support |
| `albor-resources` | image and font resource registries |
| `albor-application`, `albor-ui`, `albor-runtime`, `albor-render` | the runtime closure those modules need |

The runtime modules arrive transitively. An application should not name them.

## Advanced composition

An experienced consumer can replace any recommended implementation by linking the
underlying targets directly. The framework's public building blocks are:

| target | carries |
| --- | --- |
| `albor-platform-auto` | the selected desktop platform adapter (`-macos-auto` or `-windows-auto`) |
| `albor-platform-macos-auto` | AppKit window, input and display-link pacing |
| `albor-platform-windows-auto` | Win32 window, input and event loop |
| `albor-platform-headless-auto` | the headless platform for tests and CI |
| `albor-text-auto` | the recommended text engine |
| `albor-text-harfbuzz-auto` | HarfBuzz shaping, line breaking and font resolution |
| `albor-layout` | the measure/arrange contract and containers |
| `albor-widgets` | Button, Label, TextField, Slider, CheckBox, RadioButton, RadioGroup, NumericStepper, Canvas, Image, ListBox, ScrollView, ProgressBar, TabView and the containers |
| `albor-accessibility` | the semantic/accessibility bridge |
| `albor-i18n` | translation catalogs and RTL helpers |
| `albor-resources` | image decoding, the image registry and the font registry |
| `albor-application` | application lifecycle, tasks, timers and window wiring |
| `albor-ui` | the UI tree, components, event routing and scene build |
| `albor-render` | the portable rendering HAL, frame scene and pipeline |
| `albor-render-albor-auto` | the in-repo GPU backend (Metal or Direct3D) |
| `albor-render-blend2d-auto` | the Blend2D CPU backend |
| `albor-render-skia-auto` | the transitional Skia reference backend |
| `albor-render-software-auto` | the Test-configuration software reference |
| `albor-devrt-auto` | the Dev hot-swap extension |
| `albor-automation-auto` | the Dev/Test automation host |
| `albor-preview` | the preview host and language-facing preview objects |
| `albor-language-server` | the LSP implementation |

`albor-compiler`, `albor-manifest`, `albor-language-server`, `albor-preview` and
`albor-inspector` are development tools, not runtime dependencies of a shipped application.
The `albor-lsp` executable is the stdio transport around the language server; it is also a
development tool. The alloc probe is test-only and is not part of the consumer surface.

A composition that keeps the default platform and text engine but chooses another
renderer is:

```cmake
target_link_libraries(my_app PRIVATE
    albor-platform-auto
    albor-text-auto
    albor-layout
    albor-widgets
    albor-accessibility
    albor-i18n
    albor-render-skia-auto)
```

The same rules apply to every composition:

- Exactly one platform adapter, one text engine and one renderer may be linked.
- The `-auto` targets are whole-archive targets, because their translation units register
  themselves at load. Link the target, not the raw archive.
- Do not combine `albor` or `albor-blend2d` with another platform, text, or renderer target.
  The umbrella already names them, and the second copy duplicates the registration.
- The debug/test build configurations use different registrations; keep the matching
  configuration for every target in one application.

## Using `.ui` files and fonts

The shared CMake helpers are available after `FetchContent_MakeAvailable(albor)`:

```cmake
add_executable(my_app main.cpp)

albor_build_font_assets(my_app
    DEFAULT Inter
    FONTS
        Inter regular "assets/Inter-Regular.ttf"
        Inter bold "assets/Inter-Bold.ttf")

albor_build_ui_component(my_app "${CMAKE_CURRENT_SOURCE_DIR}/side_panel.ui")
albor_build_ui_component(my_app "${CMAKE_CURRENT_SOURCE_DIR}/main.ui" IMPORTS "${CMAKE_CURRENT_SOURCE_DIR}/side_panel.ui")

target_link_libraries(my_app PRIVATE albor)
```

`albor_build_font_assets` must come before `albor_build_ui_component`, because the generated
facade is checked against the same faces. The `albor` target already carries the text engine,
so a consumer does not link `albor-text-harfbuzz-auto` separately. See
[Fonts](ui-language/fonts.md) for the family and weight rules.

Each imported `.ui` file gets its own call before the importer, and the importer names its
direct imports in `IMPORTS`; a relative path resolves beside the importing file. The helper
compiles every file into the same target and orders the generated headers before the unit
that includes them. The generated header is included by
the consumer; no generated file has to be named in CMake. The helper adds the generated
directory to the target's include path and writes only changed units.

## Build configuration and automation

Albor's configuration is a build choice, not a consumer link choice:

| option | effect |
| --- | --- |
| `ALBOR_DEV` | builds the dev runtime extension; the application still opts into `albor-devrt-auto` |
| `ALBOR_TEST` | builds the headless platform, software renderer and, with automation, the test host |
| `ALBOR_AUTOMATION` | builds the automation core and host; invalid in a Release-only build |

`albor` deliberately does **not** include `albor-devrt-auto` or `albor-automation-auto`.
Live reload and automation are development features with their own lifetime and transport
rules, so an application links them explicitly where it wants them:

```cmake
if(ALBOR_DEV)
    target_link_libraries(my_app PRIVATE albor-devrt-auto)
endif()

if(ALBOR_AUTOMATION)
    target_link_libraries(my_app PRIVATE albor-automation-auto)
endif()
```

A Release link that references `albor-devrt` or `albor-automation` fails at link time, by
design (AD-04, AD-10). See [Tooling](tooling.md#hot-reload) and
[Automation and MCP](tooling.md#automation-and-mcp).

## Requirements and deployment

- C++23 toolchain.
- CMake 3.28 or newer, with Ninja.
- vcpkg in manifest mode, with the dependencies pinned by the framework's `vcpkg.json`, or
  by the consumer's own manifest naming the same packages.
- macOS 13.3 or newer for the macOS targets; a supported Windows 10/11 toolchain and SDK for
  the Windows targets.
- **No shader toolchain is needed to build.** The Metal `.metallib` and the Direct3D `.dxbc`
  are committed artifacts. Only a change to a shader source needs the matching compiler; see
  [Tooling](tooling.md#the-shaders).
- A consumer that names its own deployment floor must use 13.3 or newer on macOS.

The current distribution is source/CMake through `FetchContent` or a vendored
`add_subdirectory`. There is no installed `find_package(albor CONFIG)` package yet; use one
of those two forms. A tag or commit is the supported pin, never a moving branch.

## One target, one artifact

`albor` and `albor-blend2d` are CMake interface targets. They give an application one link
name while CMake places the selected framework archives on the linker line. This is the
stable consumer contract.

A physical single archive or shared library can be added behind the same target names later
without changing consumer CMake. When that lands, the consumer still links `albor` or
`albor-blend2d`; the artifact choice is an implementation detail of the framework.

## Troubleshooting

- **`albor` is not defined.** The host has no desktop platform adapter yet. Use the
  granular targets, or `albor-platform-headless-auto` for a test/CI surface; see
  [Status and platforms](status.md).
- **No window appears.** The target has a platform adapter but no window was created, or a
  headless platform was linked. Link `albor` for a desktop build.
- **Duplicate symbols or duplicate backend registration.** The umbrella was linked together
  with another platform, text, or renderer target. Link exactly one.
- **The generated component header is missing.** `albor_build_ui_component` was not called,
  or the `.ui` path was not absolute; the helper resolves a relative path against the
  caller's source directory.
- **A font is not resolved.** Declare the face with `albor_build_font_assets` before the UI
  component and set the generated default family before the first window.
- **A shader stamp check fails.** A shader source and its committed artifact disagree; run
  `bash build/shaders.sh --write` for Metal or `bash build/shaders.sh --d3d --write` for
  Direct3D in the framework checkout.
