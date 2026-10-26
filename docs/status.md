# Status

Albor is under active, phased implementation (§14). This page reflects what is shipped and
what is not, so a reader knows what a code sample will do today. The authoritative plan is
the architecture specification `albor-design-v4.md`.

## Platforms

| platform | state |
| --- | --- |
| macOS 13.3+ (AppKit, Metal), Intel and Apple silicon | implemented — the primary platform |
| Windows 10/11 (Win32, Direct3D) | implemented and built in CI |
| headless | implemented — the test and CI surface |
| Linux (Wayland, X11) | planned — the HAL interface is fixed |

An application names a window and a component. A normal application links `albor`
(GPU renderer) or `albor-blend2d` (portable CPU renderer); the platform and text engine
are the framework's choice. The target map and advanced composition are on
[Consuming Albor](consuming.md).

## Windows support

Support levels: **Full** means implemented and covered by the Windows debug/release suites;
**Partial** means implemented with a documented limitation; **Dev/Test** means linked only in
those configurations (AD-10); **Not supported** means there is no Windows implementation.

### Runtime and platform

| abstraction | level | notes |
| --- | --- | --- |
| Application, event loop, tasks, timers | Full | Win32 message loop and waitable timer pacing |
| Window roles and close request | Full | Normal, Dialog, Tool, Popup; close veto |
| Window modality and parent windows | Partial | modality is validated but not enforced; `WindowOptions.parent` is still missing (§5.1) |
| Window events | Full | resize, DPI, close, pointer, keyboard, wheel, appearance |
| Clipboard | Full | native Win32 clipboard; 2 integration tests |
| IME and text composition | Full | `WM_IME_*`, candidate anchor and document selection; lifecycle test |
| DPI and scale factor | Full | `WM_DPICHANGED` and authoritative metrics; 2 integration tests |
| Event-loop frame pacing | Full | waitable timer thread; live tick test |
| System appearance | Full | registry-backed light/dark scheme and accent |
| Input normalization and focus | Full | shared input/focus controllers with Win32 event mapping |
| Layout, measure, arrange | Full | shared layout module |
| Animation and scheduling | Full | shared animation core |
| Headless platform | Full | test and CI surface |

### Rendering

| abstraction | level | notes |
| --- | --- | --- |
| Render surface, metrics, scale | Full | D3D and Blend2D windowed surfaces |
| Two-stage frame pipeline | Full | scene build and render execution separated |
| Custom rendering and retained resources | Full | `CustomRenderNode`, `RenderContext`, batch buffers, glyph cache |
| `render-albor` GPU backend | Full | Direct3D 11; 73 tests, 41 parity tests, D3D/Metal parity |
| Blend2D windowed surface | Full | 3 windowed tests |
| Software and headless renderer | Full | screenshot reference for GPU/software parity |
| Skia backend | Not supported | macOS-only transitional backend; Windows uses Direct3D and Blend2D |

### UI, text, semantics and tooling

| abstraction | level | notes |
| --- | --- | --- |
| Reactive properties and bindings | Full | shared eager-push runtime |
| UI tree, components, event routing | Full | shared UI module |
| Semantic tree and semantic provider | Full | shared semantics module |
| Standard widgets | Full | thirteen elements, including TextField, ListBox and ScrollView |
| Text measurement and shaping | Full | HarfBuzz with DirectWrite resolution; 8 font tests and 4 shaped-run parity tests |
| `.ui` compiler, parser and typed IR | Full | platform-neutral compiler, built and tested on Windows |
| Manifest, glue generator and C++ codegen | Full | generated facades build on Windows |
| Dev runtime and hot swap | Dev/Test | excluded from Release at link time |
| LSP, preview host and inspector | Dev/Test | excluded from Release at link time |
| Automation core and MCP server | Dev/Test | named-pipe transport; 10 transport tests and a host MCP exchange test |
| Accessibility | Full with one gap | UI Automation; 10 role tests and 1 client test; value writing is read-only |
| Examples (`hello-world`, `form`) | Full | both build; the `form` example was launched against Win32/Direct3D |

Windows has no code-coverage job comparable to `macos-coverage`; the existing
`windows-debug` and `windows-release` jobs cover both configurations.

## Subsystems

| area | module | state |
| --- | --- | --- |
| Application, event loop, tasks, timers | `application/` | shipped |
| Window roles, modality, close request | `window/` | shipped |
| Platform abstraction (macOS, Windows, headless) | `platform/` | shipped |
| Render surface, metrics, scale factor | `render/` | shipped |
| Frame pipeline, two-stage frame | `render/` | shipped |
| Custom rendering (`CustomRenderNode`) | `render/` | shipped |
| Reactive properties, eager-push bindings | `ui/` | shipped |
| UI tree, components, event routing | `ui/` | shipped |
| Semantic tree, explicit/implicit keys | `semantics/` | shipped |
| Input normalization, coalescing, focus | `input/`, `focus/` | shipped |
| Layout: measure & arrange, containers | `layout/` | shipped |
| Text, measurement, shaping | `text/` | baseline — HarfBuzz, embedded font registration and weight selection |
| Standard widgets | `widgets/` | shipped — thirteen elements |
| Animation core | `animation/` | baseline |
| Compiler frontend, `.ui` parser, typed IR | `compiler/` | shipped |
| Manifest parser, validator, glue generator | `manifest/` | shipped |
| C++ code generator, facade emission | `compiler/` | shipped |
| Dev runtime extension, IR loader, hot swap | `devrt/` | Dev only |
| LSP, preview host, inspector | `language-server/`, `preview/`, `inspector/` | Dev/Test only |
| Automation core, MCP server | `automation/`, `inspector/` | Dev/Test only — unix socket on macOS, named pipe on Windows |
| Platform accessibility adapters | `accessibility/` | shipped on macOS and Windows — NSAccessibility and UI Automation; AT-SPI planned |

## Language coverage

The [`.ui` language](ui-language/index.md) is complete for the thirteen shipped elements:
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
parity, LSP/compiler agreement, manifest consistency, and build-scope checks. The software
renderer is the screenshot reference.
