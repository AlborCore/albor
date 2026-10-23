# render-albor Windows buildout plan

The plan to bring `render/render-albor`'s Direct3D half to the same level the macOS
reference (`albor_metal_backend.macos.mm` + the committed `albor_shaders.metallib`) is
already at (§3.2, §6.3).

## Where the Windows half stands today

The portability split is the same one every module of this tree uses: a large portable
core the two platforms share byte for byte, and a thin platform seam.

- the portable core (~120 kb) is done and shared: `albor_render_context.cpp` (the batch
  recorder), `albor_geometry.cpp` (the path and geometry tessellation), `albor_fill.cpp`
  (the coverage generator), `albor_contour`, `albor_batch`, `albor_glyph_cache`,
  `albor_glyph_path`, `albor_renderer_backend.cpp` (the facade) and
  `albor_backend_install.cpp` (the self install, §6.3)
- the seam is the two functions of `albor_backend_support.h`:
  `backend_device_available()` and `backend_create_surface()`. the portable stand-in
  (`albor_backend_support.cpp`) is link filtered out on both apple and windows, so each
  platform supplies the real pair (§5.3, AD-16)
- the macOS reference is `albor_metal_backend.macos.mm` (62 kb) over the committed and
  stamp verified `albor_shaders.metallib` (31 kb, six shader functions)
- the Windows half is `albor_d3d_backend.win32.c++` over the committed and stamp verified
  `albor_shaders.dxbc` (six entry points). It stands at milestones 0 through 3:
  - milestone 0: `fxc` compiles the six HLSL entry points into the container the `WIN32`
    CMake arm verifies and embeds, and `bash build/shaders.sh --d3d` is the only place the
    toolchain runs
  - milestone 1: the device comes up on hardware with the warp fallback, and both the
    offscreen and the composited windowed surface allocate a real target, present under
    the process present mutex, and read back through a staging texture
  - milestone 2: the container parses into the six stages, and readiness builds the eleven
    fixed factor blend states, the three dual source ones, the two shadow passes and the
    rasterizer, all or nothing
  - milestone 3: the encode streams the batch, uploads and ages textures, and composites
    layers and soft shadows through bounded offscreen targets
- milestone 4 is implemented: the back buffer is acquired outside the process lock and the
  present submits inside it with a zero wait, so no refresh is ever covered by the lock and
  the immediate context stays serialized; a resize that arrives mid frame is deferred to the
  end and is all or nothing, so a refused extent keeps the target the window already showed
  and the metrics are published only once the target carries them; the composition target
  re-roots on the newest visual and blanks the tree when the last surface leaves; the
  composition device is created on the first windowed attach, so an offscreen host never
  needs a compositor; a driver removal is reported through `valid()` rather than presenting
  a frozen frame; and the read back stages through a staging texture
- milestone 5 is in place: `tests/conditional/render_albor_d3d.test.win32.c++` carries 71
  checks, the whole portable ledger the apple suite reports (the shader container, the
  device, the offscreen lifecycle, the encode paths, and every parity check against the
  software reference) plus the win32 window checks the apple suite covers with a layer: a
  hidden window presents and counts every submit, two surfaces share one window and re-root,
  a stream of 140 frames holds a footprint bound of two frame targets, and a scene at stale
  metrics is refused. `tests/CMakeLists.txt` wires the suite under `WIN32`, and the whole
  archive install check (`albor_auto`) now runs on both backends

## Milestone 0 — shader artifact conventions (the first-in-the-repo step)

Everything below depends on this decision, so it lands first and alone.

1. artifact format decision: dxbc compiled by `fxc` (every windows sdk ships it, no extra
   install, same discipline the metallib commit follows). the artifact is a committed
   build input with a `.sha256` stamp, verified at build time, per the shader rule of
   the file naming contract (§6.3, AD-16)
2. extend the file naming contract in `AGENTS.md`: the shader section covers the d3d
   artifact and stamp pair beside the metal pair, one convention two toolchains
3. `build/shaders.sh` gains the d3d compilation path, so it stays the only place the
   shader toolchain runs
4. the `WIN32` arm of `render/render-albor/CMakeLists.txt` gains the custom command
   parallel to the metal one: stamp verification plus byte embedding of the artifact
5. `build/check-format.sh`: the d3d shader source lives under the same comment sweep
   rules a `.metal` file carries

gate: the committed d3d artifact verifies with no shader toolchain installed, and a
shader change without regeneration fails the build.

## Milestone 1 — device bring-up and surface shells

Mostly transplanting the patterns `blend2d_window_surface.win32.c++` already proved and
the shape `albor_metal_backend.macos.mm` already fixed.

1. build the real `ensure_device_state()`: hardware `ID3D11CreateDevice` with bgra
   support, warp fallback, one immediate context, process wide, one lazy build guarded
   by the mutex, final on failure (§6.3)
2. the `IDXGIFactory2` from the device's adapter parent
3. the offscreen surface path first: the colour target at the format the tests read
   back, so `backend_create_surface` with an empty handle presents offscreen (§6.2)
4. the windowed path: `CreateSwapChainForComposition` (flip sequential, two buffers,
   bgra) bound through a DirectComposition device, target and visual, committed once,
   under the process present mutex (§5.3)
5. `valid()`, `metrics()`, `resize()` with the keep-last rules, and the deferred
   mid-frame resize the skeleton already stores (§6.2)
6. the white one by one texture and the sampler, recorded into the readiness

gate: `backend_device_available()` answers true on hardware and on warp, and a windowed
surface constructs, draws an empty frame and reads it back.

## Milestone 2 — the HLSL shader set and the pipeline table

1. port the six metal shader functions to HLSL: the batch vertex stage and its coverage
   seeds, the fragment the kind selects between (coverage to alpha, the clip mask the
   inverse matrix maps onto the clip box centre, the paint texture and sampler), the
   exact fragment the dual source blend feeds (SourceIn, Copy, DestinationAtop), the
   shadow vertex, the silhouette shape pass and the separable gaussian blur tap
2. the pipeline table: fixed factor pipelines per composition mode beside the three
   exact dual source pipelines, plus the two shadow passes, built every one of them, and
   readiness names refused slots rather than silently falling back (§6.2)
3. the constant buffer, vertex layout and shadow uniform layout, one authored place,
   never desynchronised under an ifdef (AD-02)

gate: every pipeline slot builds and the committed artifact verifies.

## Milestone 3 — batch execution: draws, textures, offscreen compositing

1. `encode()`: the vertex, index and draw records stream from the portable `FrameBatch`,
   per draw uniforms pushed and per draw pipelines bound, index ranges over one vertex
   buffer
2. the texture upload cache: reuse across frames, aged by the surface frame counter
   (AD-06), the glyph paths walked once per glyph (§6.4)
3. the layer machinery: one offscreen target per group, composited over the parent under
   its opacity in a load pass (§6.2)
4. the shadow machinery: the cast silhouette into bounded offscreen extents, a zero
   sigma shortcut, one horizontal and one vertical blur ping pong, composited under the
   shape before the frame draws over it (§6.2)
5. coverage parity: the portable geometry core is shared already, the shader is where a
   divergence can hide, so the parity suite lands here before anything else does

gate: the single frame parity cases (a rounded clip, a translucent image, a soft shadow,
a filled path) match the software reference within tolerance (AD-26).

## Milestone 4 — presentation lifecycle and footprint discipline

1. `present()`: the swapchain presents frame one per frame under the process mutex, an
   empty frame presents without drawing, a losing back buffer skips one frame (§6.1,
   §6.3)
2. `ResizeBuffers` only once no back buffer reference is outstanding, which the blend2d
   stream test pins as a rule (§6.3)
3. footprint hygiene: no upload or render target leaks per frame, which the stream suite
   measures against the same 8 mib bound the other backends use (§2.2)
4. `read_pixels()` copies back through a staging texture, which is what the parity
   assertions read
5. the begin and end balance, the end without begin, the stale metrics refusal

gate: the create, resize, empty resize, mid frame resize lifecycle suite passes, and the
stream footprint stays flat.

## Milestone 5 — test ledger parity

1. new `tests/conditional/render_albor_d3d.test.win32.c++`, mirroring
   `render_albor_metal.test.macos.mm` with the os particularities:
   - the shader library checks (the committed artifact and its stamp, one case)
   - ~21 backend checks: device bring up, offscreen drawing, the antialiased edge, the
     clip, opacity, layer presentation, the footprint stream, offscreen resize, the
     refused empty extent, the end without begin, the mid frame deferral
   - ~40 parity checks: every primitive against the software reference within tolerance,
     in both the match and the refuse direction, composition modes, layers, shadows,
     glyph runs, pictures and image scaling (AD-26)
   - the glyph path and text checks, plus the shaped run parity cases
   - the footprint stream and the post stream resize behaviour
2. wire the suite into `tests/CMakeLists.txt` under the `WIN32` branch with the same
   plain link versus whole archive discipline the apple branch states (marker collision
   on `albor_renderer_marker`, issue 102)
3. the build scope check: a windows body edit rebuilds only its own translation units
   (§14.3)

gate: the windows run reports the same pass set the apple run reports today.

## Invariants across every milestone

- no ifdef changes a runtime layout or a virtual interface (AD-02)
- no d3d11, dxgi or dcomp type escapes the `.c++` translation unit; the seam crosses as
  `NativeWindowHandle` and an opaque pointer only (§3.5, AD-05, AD-19)
- readiness is all or nothing, and a refused windowed surface stays null rather than
  presenting a blank raster (§6.2)
- a shader change runs the toolchain exactly once, and the committed artifact plus stamp
  keep every consumer build toolchain free
- one backend archive per binary: `albor_renderer_marker` stays single definition
  (issue 102)
