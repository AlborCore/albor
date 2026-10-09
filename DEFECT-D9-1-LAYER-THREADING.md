# Defect D9.1: The metal layer is resized from the render thread

Context and the sibling defects live in `RENDERING-DEFECTS.md`. **Fixed**: resize runs on the ui thread and the executor drops stale metric frames.

## Summary

The `CAMetalLayer` is created on the main thread when the platform window attaches its
content view (`platform/macos/platform_window.macos.mm:287`), and the platform window
never touches it again. From then on, every layer write comes from the render thread:
`RendererBackend::execute_frame` runs on the `RenderExecutor` thread and, whenever the
scene metrics differ from the surface metrics, calls `resize_surface`, which writes
`layer.frame`, `layer.contentsScale` and `layer.drawableSize`
(`render/render-skia/skia_render_surface.macos.mm:43-51`, reached from
`skia_renderer_backend.macos.mm:99-105`).

Core Animation layer properties are main-thread-affine: they participate in Core
Animation transaction state and are not synchronized against other threads. Writing
them from the render thread races the compositor and any main-thread access, which is
undefined behaviour in the layer hosting model. During a live window resize this is a
continuous cross-thread write storm on the layer.

## Defect

1. **Cross-thread property writes.** The layer is created on the main thread and then
   mutated exclusively by the render thread, with no synchronization and no ownership
   transfer.

2. **The resize arrives from the wrong side.** The scene is built at metrics the ui
   thread read (`window/window.cpp:37-40`), the executor notices the mismatch at draw
   time (`skia_renderer_backend.macos.mm:103-105`) and repairs the surface itself.
   The ui thread, which already received the authoritative metrics through
   `Window::on_platform_resize` (`window/window.cpp:140-146`), never tells the surface.

3. **Off-main reads are only safe under exclusive ownership.** `begin_frame` reads the
   layer and acquires a drawable off-main (`skia_render_surface.macos.mm:101-116`),
   which is workable for a render thread, but only if no other thread touches the
   layer. The current split does not establish that contract: the layer was created on
   the main thread, and AppKit may touch the hosting view on the main thread at any
   time.

Reproduce: continuously resize a window while frames present, and run the process
under the thread sanitizer; observe unsynchronized writes to layer properties from the
render thread.

## Cause

The executor was given enough authority to repair the surface, because at the time the
resize path was written the executor was the only component that knew both the surface
and the incoming scene metrics. The ui thread's authoritative metrics path was never
connected to the surface.

## Plan

1. **The render thread stops resizing.** `execute_frame` refuses a frame whose scene
   metrics do not match the surface metrics: the frame is dropped and counted, and no
   layer property is written from the render thread. A dropped frame is correct here,
   because the next frame the ui thread submits carries the new metrics anyway.

2. **The ui thread owns resize.** The resize path that already exists is connected:
   `Window::on_platform_resize` reaches the application resize handler
   (`window/window.cpp:140-146`), and the composition root calls
   `RendererBackend::resize_surface` on the main thread before the next submit. The
   layer property writes then happen on the thread Core Animation expects.

3. **Document the thread contract of the layer.** The platform module states it
   explicitly: the layer is created on the main thread, property writes happen only on
   the main thread, and the render thread exclusively drives drawable acquisition and
   presentation. This is the contract the surface already half-follows; it becomes
   explicit and enforced.

## Verification

- The thread sanitizer is clean on a resize stress run.
- No layer property write occurs outside the main thread, asserted by review and by
  the sanitizer.
- During a live resize no frame is presented at stale metrics: the mismatched frame is
  dropped and the first frame after the ui thread resize carries the new metrics.
- The existing render suites keep passing, and `bash build/check-format.sh` passes on
  every changed file.
