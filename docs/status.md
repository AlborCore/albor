# Status

Albor is under active, phased implementation (§14). This page reflects what is shipped and
what is not, so a reader knows what a code sample will do today. The authoritative plan is
the architecture specification `albor-design-v4.md`.

## Platforms

| platform | state |
| --- | --- |
| macOS (AppKit, Metal) | implemented — the primary platform |
| headless | implemented — the test and CI surface |
| Windows (Win32) | planned — the HAL interface is fixed |
| Linux (Wayland, X11) | planned — the HAL interface is fixed |

An application names a window and a component; the platform, the renderer and the text
engine are selected by linking ([Getting started](getting-started.md#the-pieces-you-link)).

## Subsystems

| area | module | state |
| --- | --- | --- |
| Application, event loop, tasks, timers | `application/` | shipped |
| Window roles, modality, close request | `window/` | shipped |
| Platform abstraction (macOS, headless) | `platform/` | shipped |
| Render surface, metrics, scale factor | `render/` | shipped |
| Frame pipeline, two-stage frame | `render/` | shipped |
| Custom rendering (`CustomRenderNode`) | `render/` | shipped |
| Reactive properties, eager-push bindings | `ui/` | shipped |
| UI tree, components, event routing | `ui/` | shipped |
| Semantic tree, explicit/implicit keys | `semantics/` | shipped |
| Input normalization, coalescing, focus | `input/`, `focus/` | shipped |
| Layout: measure & arrange, containers | `layout/` | shipped |
| Text, measurement, shaping | `text/` | baseline — HarfBuzz through Skia |
| Standard widgets | `widgets/` | shipped — twelve elements |
| Animation core | `animation/` | baseline |
| Compiler frontend, `.ui` parser, typed IR | `compiler/` | shipped |
| Manifest parser, validator, glue generator | `manifest/` | shipped |
| C++ code generator, facade emission | `compiler/` | shipped |
| Dev runtime extension, IR loader, hot swap | `devrt/` | Dev/Test only |
| LSP, preview host, inspector | `language-server/`, `preview/`, `inspector/` | Dev/Test only |
| Automation core, MCP server | `automation/`, `inspector/` | Dev/Test only |
| Platform accessibility adapters | `accessibility/` | planned — the semantic tree is shipped |

## Language coverage

The [`.ui` language](ui-language/index.md) is complete for the twelve shipped elements:
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
