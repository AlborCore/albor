# Prototype A1-Dawn — Dawn (WebGPU C++) as a renderer candidate

Recorded plan for Prototype A1-Dawn (§30), evaluating Google Dawn as a lightweight, fast-compiling alternative to Skia
for the Level 2 custom rendering path (§11.2, §11.4, AD-06). Prototype code: not linked by any runtime module (AGENTS.md).

## Why this prototype exists

Prototype A1.3 measured Skia's Ganesh and Graphite backends against the same retained batches and found two limits that
a different renderer may not share:

1. **Skia strokes lines on the CPU.** A retained batch drawn with a stroke paint cost 10,470 ms per frame at 1.8 × 10⁶
   segments, entirely inside `skcpu::Draw::drawDevicePoints` → `StrokeTessellateOp`. Pre-tessellated filled quads cost
   0.18 ms, so a retained line batch must be quads.
2. **Skia caps one draw call at 8192 line quads**, because it indexes a vertex batch with 16 bits. A 10⁶-segment waveform
   cost **220 draw calls per frame**, not one.

WebGPU has neither limit: a render pipeline draws instanced geometry with a 32-bit instance index, so one
`RenderPassEncoder::Draw` call can carry 10⁶ instances of a two-triangle quad. Whether that translates into a frame
this Mac can actually present is the question.

## Dependency

`vcpkg.json` pins the `dawn` port with the `metal` feature on macOS. The port exports `dawn::webgpu_dawn` via
`find_package(Dawn CONFIG REQUIRED)`. Dawn is a **production candidate**, not a stub: no Dawn or WebGPU header may reach
a public `albor` interface header (AD-05, AD-06), so every Dawn type stays inside `*.macos.mm` files and the public
headers speak only in the prototype's own opaque handles.

## Reference machine

Intel Core i7-9750H @ 2.60 GHz, 16 GB, macOS 26.6.2, `release` preset, display at 60 Hz and 2.00 backing scale (so the
16.7 ms interval threshold applies as recorded). Build time is measured with `ccache` **cold** — the cache is cleared or
the port is rebuilt from a changed manifest, so the figure is a clean build.

## Thresholds, recorded before the run

### Build cost

| Category | Threshold |
| --- | --- |
| Clean build time of the Dawn port | ≤ 3.0 minutes |

The Skia baseline is recorded in `RESULTS.md` from a measured build, not from documentation.

### W1 — 10⁶ line segments as instanced quads

| Category | Threshold |
| --- | --- |
| Frame recording, p99 | ≤ 1.0 ms |
| Present interval, p99 | ≤ 16.7 ms at the 60 Hz reference display |
| Draw calls per frame | exactly 1, using instanced quads over a vertex buffer |
| Recording cost per segment, p99 | ≤ 8 ns (the A1.3 figure, so the two renderers are comparable) |
| Recording p95 while the view moves / while it is still | ≤ 1.25× (the O(1) claim of §11.2, retested) |
| Frames dropped | ≤ 1% of the frames the display offered |

### W2 — CAD canvas, 10⁵ symbols, hover hit testing

| Category | Threshold |
| --- | --- |
| Frame recording, p99 | ≤ 8.33 ms (the R1 processing deadline) |
| Hover hit query on the UI thread, p99 | ≤ 1.0 ms |
| Queries that hit the expected symbol | ≥ 99% (a miss is a grid or transform bug, not a workload fact) |
| Present interval, p99 | ≤ 16.7 ms |

### W3 — multi-window swapchain isolation

A1.3's W3 could not answer its own question: at 10⁶ segments both windows were GPU-bound, so a 100 ms hold on window 1
was invisible in window 2's cadence. This slice therefore records the isolation test **at a scale the GPU sustains**:

| Category | Threshold |
| --- | --- |
| Scale sweep: largest instance count reaching ≤ 16.7 ms p99 single-window | reported, and the isolation test runs at that scale |
| Window 2 present interval during a 100 ms hold on window 1, p99 | ≤ 16.7 ms |
| Window 2 frames delivered during the hold | ≥ 95% of the frames the display offered |
| Window 2 hover queries answered during the hold | ≥ 30 |

## Recorded limitations before the run

- Only the macOS slice is executable here. `*.win32.c++` and `*.linux.c++` files are scaffolds, linted but not compiled,
  exactly as in Prototype A2.
- Dawn's own swapchain on macOS is created from a `CAMetalLayer`, so this slice reuses the Prototype A2 platform window
  rather than writing a second platform layer. That is deliberate: it isolates the renderer variable.
- The 3.0-minute build threshold is a *port* build, not a framework build. It says nothing about link time or runtime
  performance, and it is recorded separately from every other threshold so it cannot be confused with them.