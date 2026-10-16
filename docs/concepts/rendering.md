# Rendering

Albor draws through a **two-stage frame**: the UI thread builds an immutable description of
what to show, and a render thread executes it. Drawing never blocks input, and the render
thread never calls application code.

## The two stages

1. **Scene build (UI thread).** Layout resolves if anything invalidated it; then every node
   records its draw work into a `FrameScene` — an immutable, self-contained description:
   geometry, paints, text runs, custom-node resources.
2. **Scene execution (render thread).** The render thread replays the scene against the
   window's surface. It touches the scene and the GPU, and nothing else — no application
   callback, no property, no widget.

Because the scene is immutable, a frame can be built while the previous one is still being
presented, and no lock protects a drawing structure: ownership simply moves.

## Backends

Rendering sits behind a stable interface; the backend is **selected by linking**:

- **Skia** (`albor-render-skia-auto`) — the shipping backend, Metal on macOS. Link it and it
  installs itself; no application code names it.
- **Software** — a CPU rasterizer built for the Test configuration. It is the reference the
  GPU/software parity suite compares against, so a drawing rule that only holds on one
  backend is a defect rather than a platform quirk.

Application headers never see a backend type. The `RenderContext` a custom node receives is
the portable surface.

## Drawing in `.ui`: Canvas

A `Canvas` hands its surface to a small closed drawing vocabulary:

```text
Canvas {
    width: 380px;          // the intrinsic size the canvas reports to layout
    height: 20px;

    draw: {
        fill_rect(0, 0, canvas_width, canvas_height, #D4D6DE);
        fill_rect(0, 0, canvas_width * level, canvas_height, #0A82FF);
        fill_rounded_rect(2, 2, canvas_width - 4, canvas_height - 4, 4, #0A82FF);
    }
}
```

- `fill_rect(x, y, width, height, color)`
- `fill_rounded_rect(x, y, width, height, radius, color)`
- `canvas_width` and `canvas_height` are the canvas extent, in logical units, read-only
  inside the block.
- Coordinates are relative to the canvas corner; the block re-runs when its bound properties
  change, so the example above is a live progress meter driven by `level`.

The vocabulary is deliberately closed — two primitives, not a universal drawing API. Anything
beyond it belongs in a **custom render node**.

## Custom rendering from C++

For domain-specific views — schematics, CAD, waveforms — a component owns its drawing
directly. A `CustomRenderNode` implements the render contract and records **retained
resources** the renderer keeps alive across frames:

```text
LineBatch        // batches of lines with a shared style
PathResource     // a retained path
PictureResource  // a retained recorded picture
```

The node's draw happens on the render side of the two-stage frame through the `RenderContext`
— the same portable surface the software backend implements, which is what makes the
GPU/software parity suite meaningful. The `chart` example (`examples/chart/`) is the
reference: a manifest-declared `WaveformView` whose C++ implementation batches a waveform
into a `LineBatch` and hands it over.

The data rule that makes this safe: application data reaches a render node as an immutable
`Snapshot<T>` — `std::shared_ptr<const T>` — published from the owning thread. Nothing
mutable crosses, and the render thread never waits.

## What the window owns

The application sets a window's clear colour through the same surface the theme reads:

```cpp
application.set_window_background(handle, controller.theme().surface());
```

The frame pipeline, the surface and the executor are wired by the one
`create_window` call — a host names a window and a component, never a renderer
([Getting started](../getting-started.md#the-pieces-you-link)).
