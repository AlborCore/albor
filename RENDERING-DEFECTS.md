# Widget Rendering Defects: Scene Representation and Text Pipeline

Draft issue report for the rendering pipeline. Found during a code review of the
implementation branch on 2026-10-07. Every defect names its evidence, so each can be
reproduced without re-analysis.

## Summary

The widget path from a `UiNode::render` call to presented pixels loses information that
the design contract (§6.1, §6.2, §6.4) says must survive the recording stage. Two
structural causes produce nine defects: the frame scene is a bucketed summary instead of
an ordered display list, and the text run type carries a byte string instead of the
shaped result. The plan replaces both with one ordered command list and shaped glyph
runs, which clears six of the defects without touching the two stage threading
(AD-18).

## Defects

### D1. Text is measured shaped and drawn unshaped

The text engine shapes a run through skparagraph on every `measure` call (wrapping,
bidi, line metrics, per §9.1), then the draw path throws the shaped result away and
rasterizes the raw UTF-8 bytes:

- `render/render-skia/skia_render_context.cpp:130` builds the font as
  `SkFont(nullptr, layout.size.height)`, which uses the run's total height as the point
  size. For any wrapped run the total height is the sum of all lines, so the point size
  is wrong by the line count.
- `render/render-skia/skia_render_context.cpp:137` draws with `drawSimpleText` on the
  UTF-8 bytes. Every line of a wrapped run lands on one baseline, the typeface is the
  process default, and no ligature or bidi reordering the measurement produced survives.

Reproduce: measure a two line wrapped label at 16 logical px, draw it, compare the drawn
result against the measured size.

### D2. Clipping is recorded and discarded

`SceneRecorder::clip_rect` and `SceneRecorder::clip_rounded_rect` track the clip in
`m_clip` (`ui/scene_builder.cpp:39-47`), but `SceneRecorder::scene()` never serializes
it and `RendererBackend::execute_frame` applies no clip to any draw. A scrolling
container cannot clip its content, and stage two draws everything the recorder touched,
including content outside every clip.

### D3. Paint order is lost across primitive categories

`FrameScene` stores rects, texts, line batches, paths, pictures and images in separate
parallel vectors (`render/frame_scene.h:154-186`). `execute_frame` draws them in fixed
category order (`render/render-skia/skia_renderer_backend.macos.mm:111-134`): every
rect, then every text, then every batch, path, picture and image. A path recorded
before a rect paints on top of it. Order is preserved within one category only.

Reproduce: record `draw_path` for a stroke, then `draw_rect` that overlaps it. The
rect paints above the stroke.

### D4. Rounded geometry collapses to square

`SceneRecorder::draw_rounded_rect` forwards to `draw_rect` and drops the radius
(`ui/scene_builder.cpp:75-78`), and `clip_rounded_rect` reduces to a plain rect clip
(`ui/scene_builder.cpp:44-47`). Rounded geometry survives only inside a
`PictureResource`, so the widget path cannot express the corner radius a style system
would emit.

### D5. Widgets never render their children

Every widget overrides `render()` without chaining to `Component::render` or
`UiNode::render`, and none recurses into children. A widget that carries `add_child`
subtrees draws only itself. The consequence is that `Component::paint_background` and
`Component::background` (`ui/component.cpp:60-71`) are dead code for every current
widget.

### D6. No partial layout, and every frame re-shapes every run

`UiNode::m_layout_dirty` is set and bubbled (`ui/ui_node.cpp:162-173`) but read
nowhere outside tests. `SceneBuilder::build` re-measures and re-arranges the whole tree
on every build (`ui/scene_builder.cpp:177-180`), and each measure constructs a fresh
skparagraph paragraph with no cache. `FramePipeline::build_if_dirty`
(`ui/frame_pipeline.cpp:35`) gates a build on the render dirty flag, but
`examples/chart/main.cpp:75` calls `build` unconditionally and bypasses the gate.

### D7. No interaction visuals beyond the disabled fade

There is no style module, widget colours are hardcoded literals in each widget header,
and the event set (`input/`) carries no pointer enter or leave, so a hover state cannot
exist. The only interaction driven visual today is the button disabled fade.

### D8. Transparent draws are recorded as real geometry

`SceneRecorder::draw_rect` pushes the paint and geometry unconditionally, without
testing `Paint::fills` (`ui/scene_builder.cpp:66-73`). The component default background
is alpha zero (`ui/component.cpp:70`), so every leaf widget that reaches
`Component::render` records a quad that draws nothing.

### D9. Secondary defects

1. **Layer mutated off the main thread.** `execute_frame` runs on the render thread and
   may call `resize_surface`, which writes `CAMetalLayer.frame`, `contentsScale` and
   `drawableSize` (`render/render-skia/skia_render_surface.macos.mm:43-51`).
2. **Fat command records.** Every `PictureCommand` carries all eleven fields regardless
   of kind (`render/picture_resource.h:41-75`), so a rect command drags an unused
   `TextLayout` string and three unused shared pointers.
3. **Two divergent stage two replay implementations.** `execute_frame` walks the scene
   categories and `SkiaRenderContext::draw_picture` replays picture commands. AD-26
   requires the GPU and software renderers to agree; two replay implementations is
   where they silently diverge.
4. **Canvas callbacks run on the ui thread.** `Canvas::render` invokes the application
   draw callback during recording (`widgets/canvas.cpp:34`), so heavy custom drawing
   blocks the ui thread exactly where the two stage split was meant to protect it.
5. **Per frame allocation churn.** Every build allocates fresh vectors and copies every
   `TextLayout`, each with its own `std::string`. The retained resources (paths,
   pictures, images, batches) already cross as shared pointers; rects and text do not.

## Root causes

**RC1. `FrameScene` is a bucketed summary, not an ordered display list.** Because the
scene has no per command order, clip, or radius, defects D2, D3, D4 and D8 are not bugs
in the recorder, they are features the scene type cannot express. The dual replay
implementations (D9.3) and the fat command records (D9.2) follow from the same split:
the picture command list already is an ordered display list, and the frame scene is a
lossy projection of it.

**RC2. `TextLayout` is a byte string plus scalar metrics, not the shaped result.**
Measurement and drawing are two independent code paths over different data, so D1 is
unfixable inside the current run type, and D6's per frame reshape cost follows because
nothing records the shaped output.

## Plan

The plan clears D1, D2, D3, D4, D8, D9.2 and D9.3, and the allocation churn of D9.5,
without changing the two stage threading (AD-18).

D5, D9.1 and most of D7 are done. D6 is done except for the partial repaint work.
D9.4 has its documented contract and two options still undecided. The status of each
is in the header of its own document, which is where a reader should look rather than
at this list, which is the index of them.

### Phase A: the scene becomes one ordered command list

1. Define one scene command type as a tagged union over the primitive kinds the
   `RenderContext` already declares: rect, rounded rect, text, line batch, path,
   picture, image. A command carries its clip rect, its transform, its paint or stroke
   style, and its geometry payload. Retained resources cross as
   `std::shared_ptr<const>` handles, as they already do (§6.4, AD-06).
2. `FrameScene` becomes the window metrics, the background colour and one
   `std::vector` of commands in paint order. The parallel vectors go away. The same
   command list is what the planned software reference renderer consumes (AD-26), so
   the scene rework and that renderer land on one command vocabulary.
3. The recorder records in paint order, keeps its clip and transform stack, and culls
   at record time: empty geometry and paints that neither fill nor stroke are dropped
   (D8).
4. The backend replay becomes one implementation in `execute_frame`: walk the commands
   in order, apply the clip as a scissor whenever a command's clip differs from the
   previous one, draw each command through the render context (D2, D3).
5. `PictureResource` adopts the same command type, so retained pictures and the frame
   share one command vocabulary and one replay path (D9.2, D9.3). The picture stays a
   retained resource; its commands simply stop being a second scene format.
6. The recorder retains its buffers across frames and reuses capacity, so a steady
   state frame reallocates nothing (D9.5).

### Phase B: text carries shaped glyph runs

7. `TextLayout` becomes a `std::shared_ptr<const>` shaped run: glyph identifiers,
   glyph positions, atlas placement, and one baseline origin per line for wrapped runs.
   The scalar `size`, `ascent` and `descent` stay, the UTF-8 string as the draw payload
   goes away.
8. The text engine returns the shaped result from `measure`, and the draw path replays
   it verbatim. Measurement and drawing consume one representation, so D1 cannot recur.
9. The engine caches shaped runs keyed by text, font, size and maximum width, so a
   steady state frame pays a lookup per run instead of a full shaping pass (D6's text
   cost).
10. The glyph atlas is owned by the text engine. The renderer binds it as a texture and
    draws glyph quads. No text type names a renderer type and no renderer type names a
    text engine type (§3.5, AD-19).

The detailed staging for the text rework, including the vendored stack and the shaper
decision forks, is filed as `TEXT-RENDERING-PLAN.md`.

### Resolved, record closed

These two defects are fixed and their documents are deleted. The cause is kept here,
because it is a design lesson the specification does not hold and a future writer would
otherwise repeat.

- **D5, widgets never rendered their children.** The widgets had been written as
  leaves: each overrode the whole contract and none deferred to the composite parts. The
  hierarchy already permitted composition, since `Widget` extends `Component` which
  extends `UiNode` and `add_child` is inherited, so a leaf-only implementation broke the
  contract silently instead of refusing it. The fix was `paint_content` plus a plate that
  the background hook fills once (§6.1, §6.2, §8.1).
- **D9.1, the metal layer was resized from the render thread.** The executor was given
  enough authority to repair the surface, because when the resize path was written it was
  the only component that knew both the surface and the incoming metrics. The ui thread's
  authoritative metrics path was never connected to the surface. Resize runs on the ui
  thread and the executor drops a frame whose metrics are stale (AD-18, §6.1).

The lesson in both is the same shape: a capability was given to whichever component could
see the inputs, which is how a boundary was crossed without anyone deciding to cross it.

#### Resolved after the spec reconciliation

- **The two stage pipeline had no end to end coverage.** Every caller of
  `RendererBackend::execute_frame` was a test double; the real Skia backend never ran in a test. The
  primitives were proven one at a time and the pipeline was proven in halves. The CPU reference
  backend closes it: `tests/headless.test.cpp` builds a scene from a real tree, draws it through the
  real backend onto a real raster, and reads a pixel back (§6.1, §6.3, AD-26).

## Still open, filed separately

- D6: consumers for the layout dirty flag, so a subtree relayout replaces a full tree
  relayout; and the chart example must go through `build_if_dirty`. The flag is consumed
  and the gate is wired; the partial repaint remains. Filed as
  `DEFECT-D6-PARTIAL-LAYOUT.md`.
- D7: a style module and pointer enter/leave events for hover and pressed states. The
  theme, the transitions, the state machine and the palette builder are done; runtime
  scheme switching is not. Filed as `DEFECT-D7-STYLE-INTERACTION-STATES.md`.
- D9.4: a policy for canvas draw callbacks, which run during recording on the ui thread.
  The contract is documented and two options are undecided. Filed as
  `DEFECT-D9-4-CANVAS-POLICY.md`.

## Verification

- New unit tests: paint order is preserved across categories; a recorded clip reaches
  stage two; a rounded radius survives recording; an alpha zero paint is culled; a
  wrapped run draws with the shaped glyphs at the measured size.
- The existing render, ui and widget suites keep passing.
- `execute_frame` is the only stage two replay implementation.
- `bash build/check-format.sh` passes on every changed file.
