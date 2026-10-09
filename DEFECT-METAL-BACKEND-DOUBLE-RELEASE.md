# Defect: the Metal backend double releases its command queue, so no GPU frame can be tested

Context: the CPU reference backend and the headless platform landed in `render/render-software/` and
`platform/headless/`. This record is what the first attempt to use them revealed.

## Summary

`SkiaRendererBackend` cannot be destroyed. Constructing one and letting it go out of scope traps with
`EXC_BAD_INSTRUCTION` inside the Metal driver. Because the backend had never been constructed by any
test, this was invisible until the GPU against CPU parity suite tried to use it.

**The parity suite of AD-26 cannot run until this is fixed.** It is the first thing that tried to
create and destroy a GPU surface, and the trap takes the whole test binary down with it, which would
hide every other failure in the suite.

## Defect

1. **`SkiaRendererBackendState` holds `id<MTLDevice>` and `id<MTLCommandQueue>` as strong members
   and is owned by a `std::unique_ptr`.** Under ARC (`-fobjc-arc`, `render/render-skia/CMakeLists.txt`)
   the struct has an ARC generated destructor that releases its Objective-C members. The
   `std::unique_ptr` invokes that path when the backend dies, and the members are released more than
   once. The second release lands in `_dispatch_semaphore_dispose` on a freed object, and the AMD
   driver traps rather than tolerating it.

2. **The crash is in destruction, not construction.** `MTLCreateSystemDefaultDevice()`,
   `[device newCommandQueue]` and `GrDirectContexts::MakeMetal` all succeed. The trap happens when the
   queue is deallocated.

## Cause

A C++ struct with strong ARC members, held by a smart pointer, has two owners of its destruction:
the compiler's ARC cleanup and the container's deleter. The struct was written as though it had one.

## Reproduce

The crash signature, from `~/Library/Logs/DiagnosticReports`:

```text
EXC_BAD_INSTRUCTION / SIGILL
libdispatch.dylib  _dispatch_semaphore_dispose
libdispatch.dylib  dispatch_release
Metal              -[_MTLCommandQueue dealloc]
Metal              -[MTLIOAccelCommandQueue dealloc]
AMDRadeonX6000MTLDriver  -[GFXAAMD_MtlCmdQueue dealloc]
albor              SkiaRendererBackendState::~SkiaRendererBackendState()
```

Three probes establish the diagnosis rather than leaving it to inference:

- a standalone program that creates the device, the queue and a `GrDirectContext` through the same
  Skia API exits cleanly, so the Metal and Skia setup is not at fault;
- emptying the ARC members in a user declared destructor does **not** fix it, so the double release
  is not the simple one;
- leaking the state, so that nothing is ever released, **does** avoid the trap, which places the
  fault in the release path rather than in the Metal or Skia calls.

## Plan

1. **Choose one owner for the Metal objects.** The two workable shapes are to hold the device and
   queue as `__unsafe_unretained` pointers and manage them with an explicit retain in the builder and
   release in the backend destructor, or to keep them as ARC members and give the struct no smart
   pointer, destroying it through a hand written owner. Either is fine; having both is not.
2. **Prove it with a lifecycle test first**, before the parity suite returns: construct the backend,
   destroy it, and assert nothing traps. A test that only crashes is not a test.
3. **Then restore the parity suite**, which should compare the CPU reference against a GPU frame
   within a tolerance and should assert that both reads are non-empty, because an empty read passes
   any tolerance trivially (§6.3, AD-26).

## Verification

- The backend can be constructed and destroyed in a test without trapping.
- A GPU surface can be created, drawn into and read back, which needs `RenderSurface::read_pixels()`
  (`render/render_context.h`) and is what the parity comparison consumes.
- The reference and the measured frame agree within a stated per channel tolerance.
- No frame is empty, so a refused read cannot pass as agreement.

## The rules `read_pixels` obeys

`RenderSurface::read_pixels()` is an optional capability, defaulting to a refusal, and it has three
rules that the parity suite depends on. They live here rather than in the header because
`STYLE-GUIDE.md` forbids consecutive comment lines, so a multi-line contract cannot be written where a
caller would read it:

1. **A read is of the last _finished_ frame.** A surface that has not presented one yet, or is part way
   through one, refuses. Both backends obey this: the Metal surface keeps a snapshot taken at
   `end_frame`, and the software surface counts finished frames. A parity comparison that compared a
   finished frame against a mid frame would report a difference that is not one.
2. **The bytes are premultiplied RGBA, four per pixel, unpadded.** One canonical order, chosen so two
   rasterizers produce bytes a byte comparison can read. The Metal surface draws BGRA and the software
   surface N32; without a common destination format a parity check would compare formats, not pictures.
3. **An empty screenshot is a refusal and never a black frame.** `Screenshot::empty()` distinguishes
   them, because a caller that confused the two would accept a surface that drew nothing — and a parity
   check that reads nothing from one side passes any tolerance trivially.

## Note on what is already in place

`RenderSurface::read_pixels()` exists and both backends implement it in one canonical format
(premultiplied RGBA, tight packing), and `render/screenshot.h` carries the capture and the comparison.
Those parts are correct and are what the parity suite consumes; only the GPU's ability to *be
destroyed* is missing. The reference path is fully tested today
(`tests/headless.test.cpp`, `HeadlessPipelineChecks`).