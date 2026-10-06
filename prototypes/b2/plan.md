# Prototype B2 — Build Performance With a Meaningful Frontier

**thresholds below are committed before any measurement in this prototype (§28.4)**

Prototype measurement code. Not a runtime module; nothing under `core/`, `ui/`, `window/` or `render/` may link it
(AGENTS.md, §32). B2 generates its own suites and does not modify `prototypes/b/`, whose results are historical.

## The baseline problem this revision exists to address

Prototype B's verdict is currently weaker than the design doc states, and the reason is the baseline rather than the
generated code.

| Claim in B | Value | The weakness |
| --- | --- | --- |
| clean build fails by ~90× CPU | 34.90 s vs 0.39 s | the baseline is **one translation unit on one core**; a 500-file project is being compared with a single file |
| incremental thresholds pass | body edit 0.97 s ≤ 1.5 s | the baseline's **full rebuild is 0.37 s**, so a one-component edit on the generated side costs 2.6× a complete rebuild of the baseline |
| generated side costs ~70 ms CPU per TU | 34.90 s ÷ 500 | **not decomposed**. It is unknown how much is compiler startup, header parsing and body code generation |

So "AD-03's clean-build claim is contradicted" holds against a synthetic baseline, and 70 ms per TU being "typical
header-parse overhead" is an assumption this revision tests rather than repeats.

**`[BASELINE]` has not been named.** Per the brief, M1–M4 and M6–M8 are baseline-independent and run now; **M5 and the
incremental comparison against a reference UI are deferred** and this file says so rather than substituting silently.

## Method

500 components per suite, as in Prototype B. Every variant is generated from one template so the suites differ only in
the axis under test. Every timed configuration runs **at least 5 times**; the reported figure is the **median** with the
**min and max** alongside, because a single clean-build sample is not a decision input.

Every figure states its basis: **wall at `-j12`**, **wall at `-j1`**, or **CPU**, and peak compiler RSS is reported
separately. Build commands use `cmake --build` with an explicit `-j`, and the object directory is deleted before each
clean sample.

### The variants

| # | Variant | What it isolates |
| --- | --- | --- |
| **M1** | 500 TUs, **includes only** — each TU includes its component header and defines nothing | the **fixed per-TU cost**: compiler startup, `<cstdint>` parse, and the generated declarations. This is the floor every other variant is measured against |
| **M2** | M1 plus a **precompiled header** of the runtime headers | whether header parsing is a material share of the floor |
| **M3** | M1 with headers that **forward-declare** instead of including `<cstdint>` | whether the standard-library include is material. Prototype B's generated headers include `<cstdint>` and little else, so this may show almost nothing — which is itself the answer |
| **M4** | **Unity-style chunking**, N components per TU, N ∈ {1, 5, 10, 25, 50} | the clean-versus-incremental frontier: clean CPU and wall per N, plus the **body-edit rebuild time** and the **number of components recompiled** |
| **M6** | a one-TU body edit, **split into compile and link** | how much of an incremental edit is linking rather than compiling, which decides whether chunking can be made cheap |
| **M7** | **peak compiler RSS** and **binary size** | both were named in Prototype B's `plan.md` and never measured |
| **M8** | a component with **65 536 properties** | the `uint16_t` local-id limit. The generator must **fail with a clear error**, not silently wrap |

M5 (body-size scaling against a reference UI) is **deferred pending `[BASELINE]`**.

## Hardware and build

Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM, macOS 26.6.2, Apple Clang, C++23, libc++,
`cmake --preset release` (`-O3`). Second machine: **none available**; every figure is single-machine and says so.

## Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T2.1 | M2 (PCH) reduces per-TU CPU by ≥ 20 % against M1 | if a precompiled header of the runtime headers buys less than this, header parsing is not the cost and the whole "header-parse overhead" explanation is wrong |
| T2.2 | M3 (forward-declared headers) reduces per-TU CPU by ≥ 10 % against M1 | B's generated headers already include almost nothing, so a small gain is the expected result and a large one would mean something else is being pulled in |
| T2.3 | M4 at N=50 has clean CPU ≤ 25 % of N=1 | chunking's whole purpose. 25 % allows for the per-TU floor amortising 50-to-1 while the bodies do not shrink |
| T2.4 | M4 at N=50 has a body-edit rebuild ≤ 3× the N=1 body-edit rebuild | the price of chunking. An edit must re-parse N headers, and 3× is the point at which chunking stops being free |
| T2.5 | link time ≤ 25 % of a one-TU body edit's total | decides whether the frontier is a compile problem or a link problem |
| T2.6 | the generator **refuses** 65 536 properties with a message naming the limit and the count | `uint16_t` local ids cannot address 65 536 properties. Silent wrap would corrupt every property id in the component |
| T2.7 | the recommended chunk size is reported with its clean and incremental cost | **recorded, no threshold** — the frontier is the finding, and thresholding a recommended N would only restate the expected answer |
| T2.8 | M1, M2 and M3 per-TU figures are reported with ≥ 5 samples and their spread | a consistency check on the harness itself: a variant whose spread exceeds its difference from M1 cannot be distinguished from noise |

## Commands

```sh
cmake --preset release
./.build-release/prototypes/b2/albor-prototype-b2-generate --out /tmp/b2/m1
./.build-release/prototypes/b2/albor-prototype-b2-bench --variant m1 --repeats 5
./.build-release/prototypes/b2/albor-prototype-b2-bench --variant m4 --chunk 10 --repeats 5
./.build-release/tests/albor-tests --gtest_filter='B2*'
```

## Pass/fail policy

Failures stay failures. T2.8 is the check that keeps the rest honest: if a variant's spread overlaps its difference from
the floor, the difference is reported as **not distinguishable from noise** rather than as a gain.

**No AD status is changed here.** The deliverable is a proposed **split of AD-03** into AD-03a (facade headers,
interface-only, write-if-changed — already validated) and AD-03b (translation-unit granularity — *Open*, with this
frontier), for the human to accept or reject.
