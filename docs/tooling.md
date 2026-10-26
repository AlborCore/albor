# Tooling

The language ships with a compiler, a generator, a hot-swap runtime, a language server and
an automation layer. This page is the map; each tool names the module it lives in.

## The codegen driver

`albor-codegen` is the standalone compiler driver — the same frontend the CMake helpers run:

```text
albor-codegen --input greeting.ui --output build/generated/greeting \
    --default-font-family Inter \
    --font Inter regular assets/Inter-Regular.ttf \
    --font Inter bold assets/Inter-Bold.ttf
```

It parses the `.ui`, type-checks it, checks the font selection against the carried faces,
and emits the concrete facade — a header and a translation unit per component. The
generator writes a file **only when its content changes**, so a body edit rebuilds just that
component's `.cpp`, while a declared interface change rebuilds the header and its includers
([Build scope](concepts/native-components.md)). The CMake helpers pass the font flags from
the target's `albor_build_font_assets` declaration; see [Fonts](ui-language/fonts.md).

## The shaders

The GPU backend has two platform shader sources and two committed artifacts:

| source | artifact | platform |
| --- | --- | --- |
| `render/render-albor/albor_shaders.metal` | `albor_shaders.metallib` | macOS / Metal |
| `render/render-albor/albor_shaders.hlsl` | `albor_shaders.dxbc` | Windows / Direct3D |

Neither shader is compiled by a consumer's build: the compiled artifacts are committed
beside their sources, and every platform build embeds those bytes. No machine needs the
Metal toolchain or the Direct3D shader compiler to use the framework. The Metal artifact
targets the same floor the build declares (macOS 13.3) and serves Intel and Apple silicon
alike.

Changing a shader is therefore a deliberate act:

```sh
bash build/shaders.sh --write        # recompile the Metal artifact
bash build/shaders.sh --d3d --write  # recompile the Direct3D artifact
bash build/shaders.sh --all          # check both committed artifacts
```

The `.sha256` files record the source and artifact that were built together. Every build
compares the pair and fails when a shader changed without the artifact being regenerated, so
a stale library cannot be shipped by accident. The Metal regeneration needs the toolchain
that Xcode 26 unbundled; `xcodebuild -downloadComponent MetalToolchain` installs it. The
Direct3D regeneration needs `fxc` or the matching Windows SDK toolchain.

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
The target still links one public framework target, `albor` or `albor-blend2d`; see
[Consuming Albor](consuming.md).

## Hot reload

`devrt/` — the dev runtime extension — loads a compiled IR at startup and swaps it when the
`.ui` changes, without rebuilding the host. It is built in the Dev configuration only,
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

The MCP server is the adapter in `automation/host/`: an adapter over the automation layer,
nothing more (AD-20). A client sees a set of tools — query, act, inspect, capture — whose
semantics are the automation API's; there is no second path to the runtime. `inspector/`
consumes the same automation surface for its diagnostics; it is not a transport.

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

## Parity tolerance baseline

`AlborParityChecks` (`tests/conditional/render_albor_metal.test.macos.mm`) compares
`render-albor` against the software reference. The reference is `render-software`, which
rasterizes geometry and images through Blend2D and composites text through the HarfBuzz
glyph-coverage atlas, so it shares no rasterizer with the GPU backend. Each case asserts a
whole-frame `max_channel_difference` bound, and a case whose interiors are flat also asserts
`band_count(..., 2) == 0`, so a uniform tint cannot pass a max-only check. The bound is
the value measured on this host plus margin; values are 8-bit channel levels.

| case class | bound | measured |
| --- | --- | --- |
| aligned geometry, 2x density, snapped scissor, texture reuse | 0 | 0 |
| feathered edges, opacity, retained transforms, rounded clip | 8–40 | 1–35 |
| rounded fills and polylines | 64–80 | 55–73 |
| gaussian shadows, square and sheared casters | 32–64 | 26–61 |
| mipmapped downscale | 72 | 57 |
| text, system and embedded | 80 / 64 | 70 / 57 |
| transformed text, rotated and sheared | 160 | 104–130 |

The table was re-measured when the reference moved from Skia to Blend2D. `render-albor`
snaps an axis-aligned clip to whole device pixels, and the reference applies the same snap,
so a fractional scissor still compares exactly.

A bound is not a budget for drift: a new case records its measured value next to the
assertion, and a case that samples a corner or a centre also compares the whole frame, so
an empty or uniformly wrong read cannot pass.

## Reference determinism

The software reference is byte-deterministic for its geometry corpus, not only reproducible
on one host. At commit `3303e8b` a scratch probe rendered a fixed 64×48 corpus — aligned and
rounded rectangles, a polyline, an even-odd path, a translucent image, a soft shadow and a
composited layer, with no text — and hashed the canonical RGBA readback. macOS (Intel AMD)
and Windows 11 x64 (AMD EPYC) produced the same digests:

| corpus | hash | bytes |
| --- | --- | --- |
| 64×48 at 1× | `547e508416288d67` | 12288 |
| 64×48 at 2× | `0a62d4beb95ce121` | 49152 |

Both used Blend2D 2025-03-08 with the JIT feature. At that commit the Windows validation ran 3006
tests with **0 failures** and 41 platform skips (conformance tests that read macOS font paths, and
accessibility tests that did not yet have a Win32 adapter); the 17 Windows-only tests and the
missing 128 macOS-only tests reconcile exactly with the macOS count of 3117 at that commit.

Text is excluded from the cross-platform hash because platform font files differ between
operating systems; embedded-font runs can be compared separately. A Blend2D upgrade, a JIT
change, or a new primitive that changes the reference output requires re-running the probe
and updating this record rather than treating the digests as permanent.

## Composited layers and blend semantics

`RenderContext::push_layer(opacity)` opens a group the later draws paint into, and
`pop_layer()` composites that group once at the given opacity. A pair is scoped: the push
saves the transform, clip and opacity and the pop restores them, so a layer is not a `save()`
and must not be closed by `restore()`. A mismatched `pop_layer()` is a no-op in every
backend. Layers nest, and an empty layer composites nothing rather than clearing what is
under it.

The portable backends carry layers as `DrawCommand::layer_push` / `layer_pop` markers between
draws, and `render-albor` resolves them into `FrameBatch::layers`, each naming a half-open
draw range. `SceneRecorder` and `PictureRecorder` record markers even under an empty clip,
because dropping one half of a pair would unbalance the replay. The replay applies layer
markers at the base clip and lets each draw open its own clip switch, so a group never
inherits the clip of the draw that preceded it.

`Paint::composition` and `StrokeStyle::composition` name how a draw combines with the target,
and `Paint::fill_rule` selects `NonZero` or `EvenOdd` for a fill. Both follow the femtovg
names the renderer was modelled on. Composition is per draw, not context state. A layer
composites source-over at its opacity; composition modes apply inside the group.

The three modes whose fixed factors cannot express the reference — `SourceIn`, `Copy` and
`DestinationAtop` — bind a destination-aware pipeline instead. It writes the premultiplied
source and a blend control through dual-source blending, so the destination is weighted by
coverage and, for `DestinationAtop`, by `coverage * (1 - source alpha)`. Alpha factors are
set per mode, which makes a translucent source over a translucent destination agree as well.

