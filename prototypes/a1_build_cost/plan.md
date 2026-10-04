# Prototype A1-Build-Cost — the cold dependency build

**workload, hardware and decision bands are committed before this measurement runs (§28.4)**

Measurement code. Not a runtime module; nothing links it (AGENTS.md, §32).

## Why this exists

Prototype A1-Graphite-Dawn probed AD-28's middle option and found it absent from the pinned build
(`prototypes/a1_graphite_dawn/RESULTS.md`). The discussion that followed raised a different and simpler question,
which the design doc admits it never answered:

> AD-28's blocker (5) reads *"Build cost — unevaluated; the 757 s run began with a warm ccache for part of the port
> and no cold Skia build was measured."*

A renderer decision argued from performance while **one of its two candidate dependencies has never had its build cost
measured**. This prototype measures it, on the axis the decision actually turns on.

This is a **characterisation**, not a pass/fail gate: no threshold is asserted about a build time, because a build time
is not a requirement. What is committed in advance is the **workload** (so the number is reproducible) and the **bands**
the number will be read against (so the reading cannot be chosen after the fact).

## Hardware and toolchain, recorded before the run

Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM, macOS 26.6.2. Apple Clang, ccache 4.14, vcpkg
2026-07-13 (`bf04c909169fdbb30821c02c6eb01f1cd1295d05`), triplet `x64-osx`. Skia `148` with features
`graphite,metal`; Dawn with feature `metal` — **the feature sets exactly as `vcpkg.json` pins them, unchanged.**

## What "cold" means here, and how it is enforced

A build is only cold if every cache that could answer a compile is defeated. All four are defeated **by isolation
rather than by deletion**, so the measurement cannot destroy state the repository depends on:

| cache | how it is defeated |
| --- | --- |
| ccache | a **dedicated empty `CCACHE_DIR`**, not the user's `~/.cache/ccache` (which holds 26 031 hits and is left untouched) |
| vcpkg binary cache | `VCPKG_BINARY_SOURCES=clear`, so no package is restored from `~/.cache/vcpkg/archives` (205 archives, skia among them) |
| vcpkg build trees | `--x-buildtrees-root` pointed at an empty directory, so no `.o` survives from a previous build |
| vcpkg install root | `--x-install-root` pointed at an empty directory, so nothing is reused in place |

Coldness is then **verified, not assumed**: `ccache -s` is captured before and after, and the run fails if the
dedicated directory served any hit.

## Workloads

| # | workload | what it answers |
| --- | --- | --- |
| **W1** | cold build of `skia[graphite,metal]` and its dependency closure | what Skia costs from nothing |
| **W2** | warm rebuild of the same, from W1's state | how much of W1 a CI cache could actually recover |
| **W3** | cold build of `dawn[metal]` and its dependency closure | **the honest comparator.** Dawn's recorded 757 s was partly warm-ccache, so comparing a cold Skia against it would be apples to oranges |

Each workload reports wall-clock seconds, CPU seconds, peak resident set of the build's compiler processes, and the
dependency count vcpkg installs. Reported per package where vcpkg exposes it.

## Bands, committed before the run

The number is read against these, and the reading is fixed in advance:

| reading | condition | consequence for AD-28 |
| --- | --- | --- |
| **Skia's build cost is material** | W1 > 4 × W3, **or** W1 > 30 min | Skia's build cost becomes a first-class input to AD-28, alongside the runtime evidence |
| **Skia's build cost is not material** | W1 ≤ 4 × W3 **and** W1 ≤ 30 min | the compile-time argument for dropping Skia is **not** supported by measurement, and AD-28 should be decided on runtime evidence and the 2D/text surface |
| **ccache cannot rescue it** | W2 / W1 > 0.5 | a clean CI build cannot be made cheap by caching, so the cost is paid on every cold build |
| **ccache substantially rescues it** | W2 / W1 ≤ 0.25 | the cost is a first-build cost only, which CI caching absorbs |

**These bands are deliberately not a threshold on correctness.** A Skia build that takes 40 minutes is not a defect; it
is a fact that AD-28 has to account for. The purpose of committing them here is that the *interpretation* cannot be
selected after seeing the number.

## Explicitly not measured

- **No incremental application build against either dependency.** That is a separate question (what a real component
  header and a real link cost), and B2's numbers were taken on a 4 272 B binary with no third-party dependency.
- **No cross-machine comparison.** `[SECOND MACHINE]` is still unfilled, so every figure is single-machine and the
  bands above are calibrated to this hardware only.
- **No change to `vcpkg.json`.** This prototype measures the pinned feature sets as they are.

## Commands

```sh
bash prototypes/a1_build_cost/measure.sh          # W1, W2, W3
```

Results and the reading against the bands go in `RESULTS.md`.
