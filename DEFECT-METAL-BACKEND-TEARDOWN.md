# Defect: the Metal driver traps while destroying a Ganesh command queue

**Status: worked around, and owned by the driver rather than by the framework.** The framework now constructs and
destroys `SkiaRendererBackend` cleanly, and the GPU parity suite of AD-26 can run. The upstream defect is still live:
it traps in twenty lines of code that contain no framework code at all.

## The finding that settled it

`tools/metal_teardown_probe` is the repro, and it is kept in the tree so the upstream report stays reproducible after
a macOS or Skia update. Run it in each mode; the exit code is the entire answer:

```text
$ ./.build-debug/tools/metal_teardown_probe/albor-metal-teardown-probe base   # exit 132, SIGILL
$ ./.build-debug/tools/metal_teardown_probe/albor-metal-teardown-probe hold-queue   # exit 0
```

| Mode | What it does | Result |
| :--- | :--- | :--- |
| `base` | scoped release, the ordinary case | **SIGILL** |
| `abandon` | `abandonContext()` before release | **SIGILL** |
| `release` | `releaseResourcesAndAbandonContext()` | **SIGILL** |
| `finish` | `flushAndSubmit(kYes)` to drain the GPU | **SIGILL** |
| `finish-then-delete` | drain, then `reset()` while the device is alive | **SIGILL** |
| `hold-queue` | one reference the process never drops | exit 0 |
| `leak-context` | the context is never destroyed | exit 0 |

Two facts follow, and together they are the whole diagnosis.

**One of the two objects must survive.** Destroying the context and letting the queue deallocate traps; keeping either
alive exits clean. Metal alone, with no Skia at all, releases a device and a queue and exits 0, so Ganesh is required
to reach the bug.

**No teardown order avoids it.** Draining the GPU, abandoning correctly, releasing resources properly, and deleting the
context while the device is still strongly held were all tried. All four trap. This is not an ordering bug that a
correct shutdown sequence can work around.

## Why the earlier record was wrong

The previous version of this file concluded "Metal and Skia setup is not at fault" on the strength of a standalone probe
that exited 0, and then spent five variants eliminating hypotheses about the framework's ownership arithmetic. Every one
of those five variants was answering the wrong question. The probe that exited 0 was built **without ARC**, so its
locals were never released and nothing deallocated at all.

This is worth stating plainly because it nearly cost the real answer: a repro that exits clean is only evidence when
you have shown that it released something. `tools/metal_teardown_probe` sets `-fobjc-arc` explicitly and says why in a
comment, because a future reader must not make the same mistake and conclude the driver is healthy.

The trap signature is unchanged from the original report, in `_MTLIOAccelCommandQueue dealloc` →
`_dispatch_semaphore_dispose`: the driver disposes a dispatch semaphore it believes it owns, on a queue that Ganesh
has used.

## Scope

Both GPUs of this host are affected, so it is not one vendor's driver:

```text
$ ./albor-metal-teardown-probe base "AMD Radeon Pro 5300M"     # exit 132
$ ./albor-metal-teardown-probe base "Intel(R) UHD Graphics 630" # exit 132
```

The trap is in libdispatch, which both vendors share, which points at the interaction between Ganesh's semaphore use
and this macOS release rather than at a driver-specific defect.

## The workaround in the framework

`hold_for_process_lifetime()` in `render/render-skia/skia_renderer_backend.macos.mm` keeps exactly one
`MTLCommandQueue` alive for the life of the process, which is the one sequence the driver tolerates.

This is a deliberate, bounded leak of a single process-lifetime GPU object, taken to work around a vendor defect, and
it is justified here rather than hidden: the repro above proves the fault is upstream, proves that no correct teardown
avoids it, and can be re-run to detect the day it is fixed. When the driver is fixed, the helper and the two lifecycle
tests become removable.

## What is still owed upstream

A report against Ganesh-on-Metal, carrying the repro, the seven-mode exit-code table, and the statement that two GPUs
and five teardown sequences are all affected. Until that lands, AD-26 GPU parity is verified on a host whose driver has
a known teardown defect, which is worth recording against any parity result obtained here.
