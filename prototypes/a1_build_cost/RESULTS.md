# Prototype A1-Build-Cost — Results

Plan committed in [`plan.md`](plan.md) (`7d8c6af`) before the measurement ran (§28.4).

**Hardware / toolchain:** Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM, macOS 26.6.2, Apple Clang,
ccache 4.14, vcpkg 2026-07-13 (`bf04c909169fdbb30821c02c6eb01f1cd1295d05`), triplet `x64-osx`. Feature sets exactly as
`vcpkg.json` pins them: `skia[graphite,metal]`, `dawn[metal]`.

**Headline: a cold Skia build is 34.3 minutes; a cold Dawn build is 16.6 minutes.**

## The numbers

| # | workload | wall clock | packages in the closure | compiler-cache hits |
| --- | --- | --- | --- | --- |
| **W1** | cold `skia[graphite,metal]` | **2 058.8 s (34.3 min)** | **45** | **0** |
| W2 | "warm" rerun of the same | 3.0 s — **invalid, see below** | 45 | 0 |
| **W3** | cold `dawn[metal]` | **996.6 s (16.6 min)** | **6** | **0** |

**W1 ÷ W3 = 2.07×.** Dawn's dependency closure is 6 packages; Skia's is 45.

## Reading against the bands committed before the run

| band | condition | result |
| --- | --- | --- |
| Skia's build cost is **material** | W1 > 4 × W3, **or** W1 > 30 min | **TRIGGERED, on the 30-minute clause.** W1 = 34.3 min > 30 min. The ratio clause did **not** trigger: 2.07× is well inside the 4× bound |
| Skia's build cost is **not material** | W1 ≤ 4 × W3 **and** W1 ≤ 30 min | not reached |
| ccache cannot rescue it | W2 / W1 > 0.5 | **not measured** — W2 as written was invalid |
| ccache substantially rescues it | W2 / W1 ≤ 0.25 | **not measured** |

So the pre-registered reading is: **Skia's build cost is a material input to AD-28** — but on the absolute-time clause,
not on the relative one. Skia is **2×** Dawn, not the order of magnitude the "huge dependency" framing implies.

## Two defects in this measurement, recorded rather than buried

**1. The first W2 was invalid.** It reused W1's install root, so vcpkg found all 45 packages already installed and
exited in 3.0 s. That measures vcpkg's no-op path, not a rebuild. It is reported as invalid, not as a 3-second warm
build — a 686× speedup from a `rm -rf` would have been a spectacular and completely false claim. A corrected W2 is
running separately (`w2_corrected.sh`, log `raw/w2_corrected.log`).

**2. CPU seconds were not measured.** The driver timed the build with the *parent* process's CPU clock, which cannot
see child processes, and every workload reported `cpu_s=0.0` or a negative value. **Wall clock is valid; CPU seconds
are absent.** The plan's promise of CPU seconds and peak compiler RSS is not delivered by this run.

## The finding that matters more than the timings

**The repository's ccache does not apply to vcpkg builds at all.** `CMakePresets.json` sets
`CMAKE_C_COMPILER_LAUNCHER: ccache` — which configures *our* CMake invocations. vcpkg drives its own CMake and Ninja
invocations and never reads our preset. The dedicated `CCACHE_DIR` passed to the build stayed at **0.0 GiB with zero
hits and zero misses**: ccache was not invoked once.

This cuts two ways, and both matter:

- **The cold measurement is stronger than claimed, not weaker.** W1 and W3 were cold in the strongest available sense —
  not merely "a cold cache was served no hits", but *no compiler cache existed during the build*. `ccache_hits=0` is
  therefore not evidence of anything vcpkg did; it is evidence ccache was never in the path.
- **A fresh dependency tree is always a full cold build.** Anyone restoring `~/.cache/vcpkg/archives` (205 archives on
  this machine) avoids the build entirely — which is why the isolation in `measure.sh` clears the binary cache. But a CI
  job that rebuilds rather than restores pays the full 34 minutes, because the one cache that would have helped is not
  wired into the build that needs it.

The corrected W2 wires the launcher through vcpkg's own `VCPKG_C/CXX_COMPILER_LAUNCHER` variables to measure what a CI
cache would actually recover.

## What this means for AD-28

**It supports the build-cost argument, with a number attached.** AD-28's blocker (5) read *"Build cost — unevaluated;
no cold Skia build was measured."* It is now measured: **34.3 minutes cold, 45 packages, versus Dawn's 16.6 minutes and
6 packages**, on this hardware with no compiler cache in play.

**But it does not make the case for dropping Skia on its own**, for three reasons recorded here rather than argued
elsewhere:

1. **The ratio is 2×, not 10×.** A 2× build-cost difference is a real cost of a clean CI build and a poor reason to
   reject a dependency that carries glyph rasterization, shaping, bidi, clipping, gradients and joins.
2. **The cost is a first-build cost, and the corrected W2 will say by how much.** If a CI ccache recovers most of the
   34 minutes, the objection is close to a non-issue for everyone but the first build.
3. **Dawn's own cold build is 16.6 minutes, not the 757 s the design doc recorded.** That earlier figure was explicitly
   *"warm ccache for part of the port"*, so it understated the honest cost of the Dawn-only path by a factor of ~1.3.
   Comparing a cold Skia against it would have overstated the gap; comparing cold against cold gives 2.07×.

**The honest summary:** Skia's build cost is material in absolute terms and modest in relative ones. On its own it does
not decide AD-28. It is one input alongside the runtime evidence and the 2D/text surface question.

## Reproducing

```sh
bash prototypes/a1_build_cost/measure.sh        # W1, W3 (and the invalid W2)
bash prototypes/a1_build_cost/w2_corrected.sh   # the ccache-recovery measurement
```

Raw logs: `raw/environment.log`, `raw/w1_skia_cold.log`, `raw/w2_skia_warm.log`, `raw/w3_dawn_cold.log`,
`raw/w2_corrected.log`.
