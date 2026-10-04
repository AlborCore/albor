# Prototype A1-Graphite-Dawn — Graphite on a Dawn device (AD-28)

**thresholds below are committed before the spike exists (§28.4)**

Measurement code. Not a runtime module; nothing under `core/`, `ui/`, `window/` or `render/` may link it
(AGENTS.md, §32).

## The question

AD-28 asks which HAL the framework ships. The Dawn slice established that Dawn alone can draw Albor's Level 2
geometry fast (**1.8 × 10^6 line draws, single pass, O(1) per draw**) but **lacks text glyph atlases and 2D vector
primitives**, which is why it cannot be the whole answer. Skia's Graphite is the candidate that supplies those, and
**A1.4 already measured Graphite working — on the Metal backend.**

So the open question is narrower and it is the one this spike answers:

> **Can one Graphite recorder draw both Albor's custom instanced geometry and Skia text through a single Dawn-backed
> `wgpu::Device`, and what does that combination cost to record?**

Two sub-questions hide inside it, and both have to be answered for the spike to mean anything:

1. **Feasibility.** Does milestone 148's Graphite Dawn backend accept an *externally created* device — the same
   `wgpu::Device` A1-Dawn already owns — rather than creating its own?
2. **Value.** If it works, does the combination close the feature gap that disqualified Dawn alone, or does it
   reintroduce the dependency that disqualified Skia?

## What the spike builds

One program, `albor-prototype-a1-graphite-dawn`, in two parts.

### Part A — bring-up and the shared-device claim

Create `wgpu::Instance` → `RequestAdapter` → `RequestDevice` exactly as A1-Dawn does, then hand **that** device to
`skgpu::graphite::ContextFactory::MakeDawn` through `DawnBackendContext{instance, device, queue}`. Record, as
measured facts:

| # | fact | why it matters |
| --- | --- | --- |
| A1 | a Graphite context was created on the externally supplied device | feasibility of the shared device is the whole spike |
| A2 | the adapter's backend type and the device's limits | a software adapter would make every later figure meaningless; **a software adapter is labelled as such and excluded from the timing verdicts** |
| A3 | a frame recorded and submitted, with `hasUnfinishedGpuWork()` observed | a context that silently drops GPU work would report fast and mean nothing |
| A4 | whether text and the custom geometry draw **in the same recorded pass on the same device** | the feature-gap question; two passes would still be a shared device, but not one pass |

### Part B — the combined workload

One recording per frame containing, in a single recorded frame:

- **Level 2 custom geometry**: Albor's `CustomRenderNode` instanced quads, the same shape A1-Dawn drove at 1.8 × 10^6
  draws, issued through the resource set A1 defines (§11).
- **Skia text**: `SkCanvas::drawSimpleText` labels, with the glyph-atlas cost the Dawn slice lacked.

Workloads, each with a warm-up and at least five timed repetitions (§28.2):

| workload | what it isolates |
| --- | --- |
| geometry only | the cost A1-Dawn already measured, on this device |
| text only | the cost that disqualified Dawn |
| geometry + text, one pass | the combined cost, which is the spike's actual number |

Reported per workload: CPU recording time (p50/p99), GPU-submission cost via `timestamp-query` if the device
supports `TimestampQuery`, presented frames per second over a fixed window, and peak device memory if obtainable.

## Thresholds (committed before the spike exists)

| # | Threshold | Rationale |
| --- | --- | --- |
| T5.1 | a Graphite context is created on an **externally created** Dawn device | the feasibility claim; failing it means Graphite-on-Dawn cannot share A1-Dawn's device at milestone 148 and AD-28's gate needs a different answer |
| T5.2 | the adapter reports a **hardware** backend, not `wgpu::BackendType::Null` or a software adapter | every timing figure below is meaningless on a software adapter; a software adapter is recorded as **NOT VERIFIABLE** for T5.3–T5.5 rather than passed or failed |
| T5.3 | custom geometry **and** Skia text both appear in one recorded frame on one device | the feature gap is only closed if the same device serves both; two passes recorded is a partial pass, recorded as such |
| T5.4 | combined CPU recording time ≤ 2 × geometry-only recording time | text is the addition; if adding glyph work more than doubles recording cost, the combination is not viable as a shipping default and the gate must say so |
| T5.5 | the combined workload sustains ≥ 55 presented frames/s in a 5 s window | the frame-pacing band A2 revision 2 measured on this machine (`prototypes/a2_macos/PACING.md`); below it, the combination cannot carry a window |
| T5.6 | `hasUnfinishedGpuWork()` is observed and reaches false after the final submit | a context that drops work would satisfy every other threshold while drawing nothing |

## Pass/fail policy

Failures stay failures, and a **T5.2 software adapter converts T5.3–T5.5 into NOT VERIFIABLE** rather than into passes.
Feasibility (T5.1, T5.3, T5.6) and cost (T5.4, T5.5) are reported separately, because "it works" and "it is
affordable" are different questions and the human gate decides them separately.

## Commands

```sh
cmake --preset release
cmake --build --preset release --target albor-prototype-a1-graphite-dawn
./.build-release/prototypes/a1_graphite_dawn/albor-prototype-a1-graphite-dawn
```

## Out of scope

- No change to A1's Metal Graphite context or to A1-Dawn's renderer; the spike reads both, it does not modify them.
- No text shaping or IME work — that is Prototype C.
- No decision. AD-28's status changes at the human gate, never by measurement alone (§29).
