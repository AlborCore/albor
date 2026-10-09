# Defect: the Metal backend traps while destroying its command queue, so no GPU frame can be tested

Context: the CPU reference backend and the headless platform landed in `render/render-software/` and
`platform/headless/`. This record is what the first attempt to use them revealed.

**Status: deferred.** This is tracked in `BACKLOG.md` and is revisited once the missing modules are
implemented. The investigation is complete up to the point where the next step stopped being an
ownership change; the summary below is what a later reader needs and nothing else.

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

**Not yet established.** The first hypothesis, that a struct with strong ARC members inside a
`std::unique_ptr` has two owners of its destruction, is **wrong**. Four variants were built and tested,
and each still traps with the identical crash signature:

| Variant | Result |
| :--- | :--- |
| as committed: `std::unique_ptr` + `abandonContext()` | traps |
| ARC members emptied in a user declared destructor | traps |
| state owned by a plain pointer, deleted explicitly | traps |
| as above, and `abandonContext()` removed | traps |

Leaking the state so that nothing is ever released does **not** trap, which is the only reason the
release path is still implicated rather than the driver refusing some operation.

## What is ruled out

- **The smart pointer is not the second owner.** Removing it changes nothing.
- **`abandonContext()` is not the trigger.** Removing it changes nothing, even though it was the one
  material difference between the framework code and the standalone probe that exits cleanly.
- **Metal and Skia setup is not at fault.** A standalone program performing the same
  `MTLCreateSystemDefaultDevice`, `newCommandQueue` and `GrDirectContexts::MakeMetal` calls, with the
  same queue/context destruction order, exits with status 0.
- **It is not construction.** Every step reports success; the trap is in deallocation.

## One correction the header produced

`GrMtlBackendContext` carries no `fOwner` and no release callback. Its `sk_cfp` members are the whole
contract, and `sk_cfp`'s **copy assignment calls `SkCFSafeRetain`**, which CFRetains the object.

The code carried a comment saying the opposite:

```cpp
// skia borrows the device and the queue through bridged handles, so it does not retain them itself
```

That comment was **false**. Ganesh is a second owner of both objects, not a borrower. The comment has
been corrected, because a false statement about ownership is worse than a missing one: it is what a
later reader trusts.

## What remains

**Two owners is not automatically a double free.** The counts balance on paper: `newCommandQueue`
gives one reference, `sk_cfp` assignment gives Ganesh another, and Ganesh releases its own when the
context dies. A fifth variant tested exactly that by making Ganesh the *sole* owner — the state
holding unretained borrows and the strong locals released as soon as the context came up — and it
still trapped. So the arithmetic being unbalanced is **not** the explanation either.

Every ownership hypothesis has now been tested and eliminated:

| Hypothesis | Test | Result |
| :--- | :--- | :--- |
| smart pointer is a second owner | plain pointer, explicit delete | still traps |
| `abandonContext()` triggers it | removed | still traps |
| framework and Ganesh both own | Ganesh made sole owner | still traps |
| an over release of the queue | leaked the state | does **not** trap |
| Metal and Skia setup | standalone probe, same calls, same order | exits 0 |

The remaining suspect is no longer ownership arithmetic at all. The trap is inside
`MTLIOAccelCommandQueue dealloc` → `dispatch_release` → `_dispatch_semaphore_dispose`, which is driver
code that disposes a dispatch semaphore it believes it owns. The most plausible explanation left is
that **the queue is released while Ganesh still has work or a semaphore in flight on it**, and the
driver traps rather than tolerating it; that would make this a shutdown-ordering problem rather than a
counting problem.

The next step is therefore to stop changing ownership and instead find out what the driver requires at
teardown, by testing whether a **GPU idle wait** or an explicit `GrDirectContext` teardown *before*
the queue is released avoids the trap. That is a different class of experiment, and it is the last one
worth trying before escalating this to a platform issue against the AMD driver, which is where a trap
inside a vendor dealloc ultimately belongs.

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