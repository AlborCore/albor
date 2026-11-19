# Status

Albor is under active, phased implementation (§14). This page reflects what is shipped and
what is not, so a reader knows what a code sample will do today. The authoritative plan is
the architecture specification `albor-design-v4.md`.

## Platforms

| platform | runtime floor | build baseline | state |
| --- | --- | --- | --- |
| macOS (AppKit, Metal), Intel and Apple silicon | macOS 13.3 | `macos-15`, Xcode 16.4 | implemented — the primary platform |
| Windows (Win32, Direct3D) | Windows 10 1809 (build 17763, LTSC 2019); VC++ 2015-2022 redistributable | `windows-2022`, VS 2022 17.14 / MSVC 14.44, Windows SDK 10.0.22621.0 | implemented and built in CI |
| Linux Wayland (Blend2D CPU) | RHEL/Alma 9+ (glibc 2.34, GLIBCXX 3.4.29, CXXABI 1.3.13, Wayland 1.21) | `ubuntu-24.04` current; `almalinux:9` baseline | implemented — CI temporarily disabled until a Linux host is available; X11 is planned, Vulkan is #110 |
| headless | — | — | implemented — the test and CI surface |

The framework and its compiler are **C++20**. The Windows build declares the Windows 10
1809 target (`WINVER`, `_WIN32_WINNT`, `NTDDI_VERSION`) so a newer API cannot slip in, and
the macOS build stamps the 13.3 deployment floor into the shaders and the package. The
Linux artifacts are checked against the glibc and libstdc++ floor with
`build/check-linux-floor.sh`; [Linux](linux.md) carries the baseline. The Windows and macOS
pins are recorded in the parity ledger (`macos-windows.md` §6).

An application names a window and a component. A normal application links `albor`
(GPU renderer) or `albor-blend2d` (portable CPU renderer); the platform and text engine
are the framework's choice. The target map and advanced composition are on
[Consuming Albor](consuming.md).

## Linux support

| abstraction | level | notes |
| --- | --- | --- |
| Application, event loop, tasks, timers | Full | Wayland loop with timerfd pacing |
| Window roles and close request | Partial | Normal, Dialog and Tool are parented toplevels; Popup uses `xdg_popup` + `xdg_positioner`; close veto is framework-enforced |
| Window modality and parent windows | Full | the framework gate enforces it before dispatch; Wayland has no native dialog role |
| Clipboard | Partial | `wl_data_device` text selection; a headless compositor without a seat has no clipboard |
| IME and text composition | Partial | `text-input-v3`; candidate anchor; surrounding text is not sent yet |
| DPI and scale factor | Full | fractional `wp_fractional_scale_v1` with `wp_viewporter`; integer output fallback, covered at scale 2 by the local container |
| Event-loop frame pacing | Full | timerfd pacing |
| Input normalization and focus | Partial | xkbcommon key and modifier mapping; no scripted real-device suite yet |
| System appearance | Not yet | XDG portal appearance is #234 |
| Native file dialogs | Not yet | XDG portal file chooser is #234 |
| Platform font resolution | Full | fontconfig resolves a family and hands over its sfnt; embedded text never reaches it and a host without fontconfig degrades to embedded-only |
| Renderer | Partial | Blend2D CPU presenting through `wl_shm`; the Vulkan backend is #110 |
| Accessibility | Not yet | AT-SPI is #42 |

The adapter builds only where the Wayland, xkbcommon and Pango/Cairo/D-Bus development
packages exist. The libdecor frame and the Wayland protocol XMLs are vendored, so no
libdecor package, plugin directory or `wayland-protocols` package is needed. The Linux CI
jobs are temporarily disabled until a Linux host is available; the container workflow
installs the packages and runs the platform suite under a headless Weston compositor.
[Linux](linux.md) records the workflow, the Alma 9 baseline and the current limits.

## Windows support

Support levels: **Full** means implemented and covered by the Windows debug/release suites;
**Partial** means implemented with a documented limitation; **Dev/Test** means linked only in
those configurations (AD-10); **Not supported** means there is no Windows implementation.

### Runtime and platform

| abstraction | level | notes |
| --- | --- | --- |
| Application, event loop, tasks, timers | Full | Win32 message loop and waitable timer pacing |
| Window roles and close request | Full | Normal, Dialog, Tool, Popup; close veto |
| Window modality and parent windows | Full | WindowModal and ApplicationModal are enforced before dispatch; an owned dialog closes with its parent; opening activates it and closing restores the parent control (§5.1) |
| Window events | Full | resize, DPI, close, pointer, keyboard, wheel, appearance |
| Clipboard | Full | native Win32 clipboard; 2 integration tests |
| IME and text composition | Full | `WM_IME_*`, candidate anchor and document selection; lifecycle test |
| DPI and scale factor | Full | `WM_DPICHANGED` and authoritative metrics; 2 integration tests |
| Event-loop frame pacing | Full | waitable timer thread; live tick test |
| System appearance | Full | registry-backed light/dark scheme and accent |
| Input normalization and focus | Full | shared input/focus controllers with Win32 event mapping |
| Native file dialogs | Full | `IFileOpenDialog`/`IFileSaveDialog`; owned by the parent window |
| Layout, measure, arrange | Full | shared layout module |
| Animation and scheduling | Full | shared animation core |
| Headless platform | Full | test and CI surface |

### Rendering

| abstraction | level | notes |
| --- | --- | --- |
| Render surface, metrics, scale | Full | D3D and Blend2D windowed surfaces |
| Two-stage frame pipeline | Full | scene build and render execution separated |
| Custom rendering and retained resources | Full | `CustomRenderNode`, `RenderContext`, batch buffers, glyph cache |
| `render-albor` GPU backend | Full | Direct3D 11; 93 tests, 48 parity tests, D3D/Metal parity, damage culled stage one and damage-bounded partial frames |
| Blend2D windowed surface | Full | 3 windowed tests |
| Software and headless renderer | Full | screenshot reference for GPU/software parity |

### UI, text, semantics and tooling

| abstraction | level | notes |
| --- | --- | --- |
| Reactive properties and bindings | Full | shared eager-push runtime |
| UI tree, components, event routing | Full | shared UI module |
| Semantic tree and semantic provider | Full | shared semantics module |
| Standard widgets | Full | twenty-nine elements, including Panel, ComboBox, Image, TextField, ListBox, ScrollView, TabView, RadioGroup, NumericStepper, Menu, MenuItem, MenuBar and ToolBar; ComboBox prefers a native `WindowRole::Popup` list and falls back to the in-tree overlay for detached trees |
| Text measurement and shaping | Full | HarfBuzz with DirectWrite resolution; 8 font tests and 4 shaped-run parity tests |
| `.ui` compiler, parser and typed IR | Full | platform-neutral compiler, built and tested on Windows |
| Manifest, glue generator and C++ codegen | Full | generated facades build on Windows |
| Dev runtime and hot swap | Dev/Test | excluded from Release at link time |
| LSP, preview host and inspector | Dev/Test | excluded from Release at link time |
| Automation core and MCP server | Dev/Test | named-pipe transport; 10 transport tests and a host MCP exchange test; layout bounds, pointer/key input and typed value, state and range reads |
| Accessibility | Full with gaps | UI Automation; 21 role tests and 1 client test; range and text writes and expand/collapse patterns exposed; radio-group selection is not exposed |
| Examples (`hello-world`, `form`, `vertical-slice`) | Full | all build on macOS; the vertical slice is driven end to end through the MCP automation host and captures PNG frames |

Windows has no code-coverage job comparable to `macos-coverage`; the existing
`windows-debug` and `windows-release` jobs cover both configurations.

## Subsystems

| area | module | state |
| --- | --- | --- |
| Application, event loop, tasks, timers | `application/` | shipped |
| Window roles, modality, close request | `window/` | shipped |
| Native file dialogs | `application/`, `platform/` | shipped — appkit panels, shell dialogs and a scripted headless backend |
| Platform abstraction (macOS, Windows, Linux Wayland, headless) | `platform/` | shipped |
| Render surface, metrics, scale factor | `render/` | shipped |
| Frame pipeline, two-stage frame | `render/` | shipped |
| Custom rendering (`CustomRenderNode`) | `render/` | shipped |
| Reactive properties, eager-push bindings | `ui/` | shipped |
| UI tree, components, event routing | `ui/` | shipped |
| Semantic tree, explicit/implicit keys | `semantics/` | shipped |
| Input normalization, coalescing, focus | `input/`, `focus/` | shipped |
| Layout: measure & arrange, containers | `layout/` | shipped |
| Text, measurement, shaping | `text/` | baseline — HarfBuzz, embedded font registration and weight selection |
| Standard widgets | `widgets/` | shipped — twenty-nine elements |
| Animation core | `animation/` | baseline |
| Compiler frontend, `.ui` parser, typed IR | `compiler/` | shipped |
| Manifest parser, validator, glue generator | `manifest/` | shipped |
| C++ code generator, facade emission | `compiler/` | shipped |
| Dev runtime extension, IR loader, hot swap | `devrt/` | Dev only |
| LSP, preview host, inspector | `language-server/`, `preview/`, `inspector/` | Dev/Test only |
| Automation core, MCP server | `automation/` | Dev/Test only — unix socket on macOS, named pipe on Windows; layout bounds, pointer/key input and typed value, state and range reads |
| Platform accessibility adapters | `accessibility/` | shipped on macOS and Windows — NSAccessibility and UI Automation; AT-SPI planned |

## Language coverage

The [`.ui` language](ui-language/index.md) is complete for every shipped element:
components, typed properties, signals and handlers, expressions and bindings, control flow
(`if`, `Repeat`, virtualized `Repeat`), native components through manifests, and
internationalization through `@tr`. Two forms are settled in the grammar and ship today:
component composition and inheritance with `@children` — see
[Components](ui-language/components.md#composition-vs-inheritance) and
[Inheritance](ui-language/components.md#inheritance).

## Build configurations

| configuration | defines | links |
| --- | --- | --- |
| Debug | `ALBOR_DEV` | the dev runtime extension |
| Release | — | nothing Dev/Test — excluded at link time |
| Test | `ALBOR_TEST` | the automation core, the software renderer |

## Testing

The suite is self-contained — Google Test for C++, no external fixtures. Beyond per-file
tests the architecture defines cross-cutting suites
([Tooling](tooling.md#what-the-test-suite-covers)): dual-engine conformance, GPU/software
parity, LSP/compiler agreement, manifest consistency, build-scope checks, and the automation
reference test against the form sample. The software renderer is the screenshot reference.
