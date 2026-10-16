# Tooling

The language ships with a compiler, a generator, a hot-swap runtime, a language server and
an automation layer. This page is the map; each tool names the module it lives in.

## The codegen driver

`albor-codegen` is the standalone compiler driver — the same frontend the CMake helpers run:

```text
albor-codegen --input greeting.ui --output build/generated/greeting
```

It parses the `.ui`, type-checks it, and emits the concrete facade — a header and a
translation unit per component. The generator writes a file **only when its content
changes**, so a body edit rebuilds just that component's `.cpp`, while a declared interface
change rebuilds the header and its includers
([Build scope](concepts/native-components.md)).

## Manifest glue

`manifest-glue` is the driver behind `albor_build_native_component` and
`albor_build_native_record`. It reads a `.ui-manifest` and generates the facade units the
C++ class implements — descriptor tables, property and signal facades, the preview stub.
The manifest is the single source of truth; the glue is generated from it
([Native components](concepts/native-components.md)).

## The CMake helpers

```cmake
albor_build_ui_component(my-app "${CMAKE_CURRENT_SOURCE_DIR}/greeting.ui")
albor_build_native_record(my-app "${CMAKE_CURRENT_SOURCE_DIR}/task.ui-manifest")
albor_build_native_component(my-app "${CMAKE_CURRENT_SOURCE_DIR}/chart.ui-manifest" WaveformView)
```

All three run the appropriate driver as a build step, write under the build directory, and
join the target's sources and include path — an application never names a generated file.

## Hot reload

`devrt/` — the dev runtime extension — loads a compiled IR at startup and swaps it when the
`.ui` changes, without rebuilding the host. It is linked in Dev and Test configurations only,
through a registration hook that exists in every build, and is excluded from Release at
link time, not by a runtime flag (AD-04, AD-10). A Release reference to a devrt symbol fails
the link.

## Language server

`language-server/` implements the Language Server Protocol over stdio for `.ui` files:
diagnostics (parse, type, layout, style, translation), hover, and go-to-definition. The
editor experience — completion, inline diagnostics — is served by the same frontend, so a
diagnostic in the editor is the diagnostic the build produces.

## Preview

`preview/` hosts a `.ui` component in a window and reloads it as the file changes — the
hosted counterpart of the hot-swap runtime. A component can be previewed without an
application: the preview host constructs the facade and drives the same frame pipeline a
shipped application does.

## Automation and MCP

`automation/` — Dev and Test configurations only — addresses the running application by
semantic identity: find a node, read a property, invoke a command, take a screenshot. It
never reaches internals directly; every command goes through the same registry an
application's own handlers use ([Commands](concepts/commands.md)).

`inspector/` is the MCP server: an adapter over the automation layer, nothing more (AD-20).
A client sees a set of tools — query, act, inspect — whose semantics are the automation
API's; there is no second path to the runtime.

## What the test suite covers

Beyond per-file unit tests, the architecture defines cross-cutting suites
([Testing](status.md#testing)):

- **Dual-engine conformance** — one `.ui` corpus through compiled output and the dev IR
  evaluator, same observable behaviour (AD-25).
- **GPU/software parity** — every resource kind and primitive behaves the same in both
  renderers within tolerance (AD-26).
- **LSP/compiler agreement** — same corpus, same diagnostics.
- **Manifest consistency** — generated glue, descriptor tables and preview stubs agree with
  the manifest.
- **Build-scope** — a scripted check that a `.ui` body edit rebuilds only the expected
  translation units.
