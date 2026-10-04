# Prototype A1-Graphite-Dawn — Results (AD-28)

Plan committed in [`plan.md`](plan.md) (`a19c9da`) before this program existed (§28.4).

**Hardware / build:** Intel Core i7-9750H @ 2.60 GHz, AMD Radeon Pro 5300M, macOS 26.6.2,
`cmake --preset release`, Skia 148 and Dawn from the pinned vcpkg manifest.

**Verdict: T5.1 FAILS. The spike cannot run, and the reason is a missing build feature rather than an
architectural obstacle.**

## What happened

The program compiles against Skia's Dawn backend headers and links every object except one: `ContextFactory::MakeDawn`.
That symbol is absent from the built library, so the spike stops at bring-up and never records a frame.

## The cause, measured rather than inferred

`nm` over the installed static library gives Graphite's complete factory set:

```
graphite14ContextFactory10MakeVulkanERKNS_20VulkanBackendContextERKNS0_14ContextOptionsE
graphite14ContextFactory9MakeMetalERKNS0_17MtlBackendContextERKNS0_14ContextOptionsE
```

**Two factories. No Dawn.** Graphite itself is present and working — `MakeMetal` is what Prototype A1.4 drives — so
this is not a missing-Graphite problem. It is a missing-**Dawn-backend** problem.

The headers are misleading here, and that is worth recording: `skia/gpu/graphite/dawn/DawnBackendContext.h` ships in
the installed include tree and declares `ContextFactory::MakeDawn`, so the code compiles cleanly and fails only at
link time. **A header that exists is not evidence that the backend was compiled in**, because Skia's public headers are
not feature-gated.

The cause is the manifest. [`vcpkg.json`](../../vcpkg.json) requests:

```json
{ "name": "skia", "platform": "osx", "features": ["graphite", "metal"] }
```

The vcpkg Skia port **does** ship a `dawn` feature (alongside `avif`, `direct3d`, `dng`, `gl`, `graphite`, `harfbuzz`,
`icu`, `metal`, `pdf`, `vulkan`, …). It was never requested, so `skia_use_dawn` was off for the whole build.

## What this says about AD-28 — and what it does not

**It does not refute Graphite-on-Dawn.** Nothing here tested whether a Graphite context can accept an *externally
created* `wgpu::Device`, or what the combined geometry-and-text cost would be. Those questions are T5.3–T5.5 and they
remain entirely open. The spike stopped one step before the question it was built to ask.

**It does settle a question the design doc had open.** Prototype A1.4 recorded that milestone 148's public API "cannot
point graphite at a drawable", and left the implication ambiguous: whether Graphite could reach Dawn at all. The
answer at this milestone is that **the capability exists in the source and is simply not built** — `DawnBackendContext`
carries a `wgpu::Instance`, `wgpu::Device` and `wgpu::Queue`, which is exactly the shared-device design the spike
wanted to test, so the API shape the spike needed is present.

**It adds a cost nobody had priced.** The answer is one line in `vcpkg.json` and a full Skia rebuild, which on this
machine is a source build of a very large project. Every consumer of the HAL pays that once, and the build is no longer
reproducible from the current pinned manifest without changing it.

## The decision this needs

Adding `"dawn"` to the Skia feature list in `vcpkg.json` is the entire fix, and it is **not applied here**, for two
reasons:

1. It changes a **pinned dependency's feature set**, which is the human gate's territory (§29), not a prototype's.
2. It triggers a **Skia rebuild**, whose duration is unknown on this machine and whose result would change every
   existing A1 figure's binary (though not their semantics — `MakeMetal` is unaffected).

If the gate approves, the spike's remaining work is mechanical: add the feature, rebuild, re-run, and T5.1–T5.6 answer
themselves. The program is written, compiles, and stops honestly at the missing symbol.

## Verdicts

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T5.1 | Graphite context on an externally created Dawn device | `MakeDawn` absent from the library; spike stops at bring-up | **FAIL** |
| T5.2 | hardware adapter | not reached | **NOT REACHED** |
| T5.3 | geometry and text in one recorded frame | not reached | **NOT REACHED** |
| T5.4 | combined record ≤ 2× geometry-only | not reached | **NOT REACHED** |
| T5.5 | combined ≥ 55 fps | not reached | **NOT REACHED** |
| T5.6 | GPU work observed and drained | not reached | **NOT REACHED** |

T5.1's failure stands. T5.2–T5.6 are recorded as **not reached** rather than as failures, because the spike never
recorded a frame; a threshold cannot fail on a measurement that was never taken (§28.4).

## Reproducing

```sh
cmake --preset release
cmake --build --preset release --target albor-prototype-a1-graphite-dawn   # links: undefined MakeDawn
nm -gU .build-release/vcpkg_installed/x64-osx/lib/libskia.a | grep -o "graphite.*ContextFactory[^ ]*"
```
