# Prototype A1-Build-Cost — Results

Plan committed in [`plan.md`](plan.md) (`7d8c6af`) before the measurement ran (§28.4).

**Hardware / toolchain:** Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM, macOS 26.6.2, Apple Clang,
ccache 4.14, vcpkg 2026-07-13 (`bf04c909169fdbb30821c02c6eb01f1cd1295d05`), triplet `x64-osx`. Feature sets exactly as
`vcpkg.json` pins them: `skia[graphite,metal]`, `dawn[metal]`.

**Headline: a cold `skia[graphite,metal]` build takes 22.6–34.3 minutes across two runs of the identical workload;
a cold `dawn[metal]` build takes 16.6 minutes. The pre-registered band verdict is NOT ROBUST.**

## The numbers

| # | workload | wall clock | packages | compiler cache |
| --- | --- | --- | --- | --- |
| **W1** | cold `skia[graphite,metal]` | **2 058.8 s (34.3 min)** | **45** | not in the build path |
| **W1-repeat** | the *same* cold workload, repeated | **1 358.0 s (22.6 min)** | 45 | not in the build path |
| W2 | "warm" rerun on the same install root | 3.0 s — **invalid**, see below | 45 | n/a |
| **W3** | cold `dawn[metal]` | **996.6 s (16.6 min)** | **6** | not in the build path |

**Skia's dependency closure is 45 packages; Dawn's is 6.** Skia ÷ Dawn is **1.36–2.07×** by wall clock.

## Reading against the bands committed before the run

| band | condition | W1 | W1-repeat |
| --- | --- | --- | --- |
| material | W1 > 4 × W3 (**3 986 s**) | 2 059 s — **no** | 1 358 s — **no** |
| material | W1 > 30 min (**1 800 s**) | 2 059 s — **YES** | 1 358 s — **no** |
| not material | W1 ≤ 4 × W3 **and** W1 ≤ 30 min | — | **yes** |
| ccache bands | W2 / W1 | **not measured** | **not measured** |

**Verdict: not robust.** The band resolves to *material* on the first sample and *not material* on the second, because
the two samples straddle the 30-minute clause. The ratio clause never triggers on either.

The robust findings are the ones both samples agree on:

- **Skia is 1.4–2.1× Dawn to build cold, not an order of magnitude.** The "huge dependency" framing does not survive
  measurement.
- **A cold Skia build is tens of minutes on 6 cores**, and it is dominated by sheer translation-unit count: 45 packages
  and roughly 2 660 Skia object files against Dawn's 6 packages.

## Three defects in this measurement, recorded rather than buried

**1. The committed bands were decided by a single unreplicated sample.** The first result (`4512d87`) read
*"Skia's build cost is material"* from one 34.3-minute run. The repeat came in at 22.6 minutes and reversed it. A 52%
spread on identical work is itself the finding: **any single-sample build-time number from this machine is not a
decision input**, which is the same conclusion B2 reached about tail latencies on this hardware.

**2. W2 was invalid.** It reused W1's install root, so vcpkg found all 45 packages installed and exited in 3.0 s. That
measures vcpkg's no-op path, not a rebuild. Reporting it as a 3-second warm build would have been a 686× false claim.

**3. CPU seconds and peak compiler RSS were never captured.** The driver timed with the *parent* process's CPU clock,
which cannot see child processes, so every workload reported `cpu_s≈0`. Wall clock is valid; the plan's other two
promises are not delivered.

## The finding that matters most: the repository's ccache never reaches vcpkg

`CMakePresets.json` sets `CMAKE_C_COMPILER_LAUNCHER: ccache`, which configures **our** CMake invocations. vcpkg drives
its own CMake and Ninja and never reads our preset. Evidence, from both runs:

- the dedicated `CCACHE_DIR` passed to the build reported **no hit, miss or cacheable counts at all**, and holds 272
  files with **zero stats entries**;
- the user's own cache is **byte-identical before and after** (26 031 hits / 9 031 misses), which confirms the isolation
  worked and that nothing in these builds consulted it;
- a corrected attempt to wire the launcher through vcpkg's own `VCPKG_C/CXX_COMPILER_LAUNCHER` variables **also failed to
  register any cache statistics**, so phase 2 was stopped rather than spending another ~25 minutes confirming a negative.

**Consequence, and it is the practical one: nobody on this machine pays 34 minutes, because the vcpkg *binary* cache
already prevents the build.** `~/.cache/vcpkg/archives` holds 205 prebuilt archives including Skia, and vcpkg restores a
package in seconds. The cold number only describes a machine — or a CI job — that has neither that archive cache nor a
working compiler cache.

So the honest statement of the build cost is:

- **first-ever build, no caches: 22.6–34.3 minutes for Skia, 16.6 minutes for Dawn;**
- **routine builds on a machine with the binary cache: seconds, for both**, and that is what actually happens here;
- **CI without either cache: the cold figures apply**, and wiring ccache into vcpkg is a real, separate piece of work
  that this prototype attempted and could not make work.

## What this means for AD-28

**Blocker (5) is now answered with a number instead of an admission of ignorance.** Skia's dependency is genuinely more
expensive to build cold than Dawn's — ~45 packages and ~2 660 translation units against 6 — but by **1.4–2.1×**, not by
an order of magnitude, and the difference disappears entirely for anyone with a populated binary cache.

**It does not make the case for dropping Skia.** A 2× cold-build difference, on a cost that caching already eliminates
for routine work, is a poor reason to reject the dependency that supplies glyph rasterization, shaping, bidi, clipping,
gradients and joins. It is one input alongside the runtime evidence and the 2D/text surface question — not a verdict.

**And it revises Dawn's own recorded cost upward.** AD-28 recorded Dawn's build at 757 s, explicitly *"warm ccache for
part of the port"*. Cold, Dawn is **996.6 s**. Any comparison against Skia that used the 757 s figure would have
overstated the gap by roughly 1.3×.

## Reproducing

```sh
bash prototypes/a1_build_cost/measure.sh          # W1, W3, and the invalid W2
```

Raw logs: `raw/environment.log`, `raw/w1_skia_cold.log`, `raw/w2_skia_warm.log`, `raw/w3_dawn_cold.log`,
`raw/w2_corrected.log` (the repeat and the failed ccache wiring).

**If this is repeated, it needs at least three cold samples per workload.** Two samples spread 52%; three would not
settle it either, and the honest conclusion is that build-time comparisons on this machine need medians over many runs
or a quieter machine.
