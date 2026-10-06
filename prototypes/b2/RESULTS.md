# Prototype B2 — Build Performance With a Meaningful Frontier

Follow-up to Prototype B, addressing **AD-03** (generated public headers and translation-unit granularity). Plan
committed in [`plan.md`](plan.md) (`ecc2978`) before this measurement ran (§28.4).

Prototype measurement code. Not a runtime module; nothing under `core/`, `ui/`, `window/` or `render/` may link it
(AGENTS.md). `prototypes/b/` is untouched; B's results are historical.

**Hardware / build:** Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM, macOS 26.6.2, Apple Clang,
`clang++ -std=c++23 -O3 -c`, 500 components per suite, **5 samples per timed configuration**, median reported with
min–max spread. **Second machine: none** — every figure is single-machine.

**`[BASELINE]` has not been named.** M5 and the incremental comparison against a reference UI are **deferred**. No
figure below is a comparison against a baseline, and none should be read as one.

**Verdict: 2 pass, 4 fail, 2 recorded.**

## Verdicts

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T2.1 | M2 (precompiled header) cuts per-TU CPU by ≥ 20 % | **3.00 %** | **FAIL** |
| T2.2 | M3 (forward-declared headers) cuts per-TU CPU by ≥ 10 % | **−28.85 %** | **FAIL** |
| T2.3 | N=50 clean CPU ≤ 25 % of N=1 | **9.23 %** | **PASS** |
| T2.4 | N=50 body edit ≤ 3× the N=1 body edit | **3.277×** | **FAIL** |
| T2.5 | link ≤ 25 % of a one-TU edit | **57.26 %** | **FAIL** |
| T2.6 | generator refuses 65 536 properties, naming limit and count | exit 1, message names both | **PASS** |
| T2.7 | frontier and binary size | see below | RECORDED |
| T2.8 | every variant reported with ≥ 5 samples and its spread | M1 spread 3.83 % of median | RECORDED |

## M1/M2/M3 — the fixed per-TU floor

| variant | wall −j12 | wall −j1 | CPU | **CPU per TU** | CPU spread | peak RSS |
| --- | --- | --- | --- | --- | --- | --- |
| **M1** includes only | 2.45 s | 14.77 s | 24.30 s | **48.6 ms** | 23.64–24.57 s | 16.6 MB |
| **M2** precompiled header | 2.27 s | 14.80 s | 23.57 s | **47.1 ms** | 23.03–24.14 s | 16.6 MB |
| **M3** forward-declared | 2.99 s | 18.78 s | 31.31 s | **62.6 ms** | 31.21–31.46 s | 26.8 MB |

### The 70 ms per TU is almost entirely fixed overhead

Prototype B reported about **70 ms CPU per translation unit** and attributed it to "typical header-parse overhead".
Decomposed:

- **48.6 ms** is the floor — 500 translation units that include their component header and **define nothing**.
- **71.5 ms** is the full generated component with its body (from the M4 N=1 row).

So **68 % of the per-TU cost is paid before a single line of generated code is compiled.** The remaining ~23 ms is the
body. B's attribution was not wrong in direction but was far too small in magnitude: header parsing is a minor term,
and the dominant cost is per-translation-unit compiler fixed cost.

### Header parsing is not the cost, so neither mitigation helps

| mitigation | measured | threshold | verdict |
| --- | --- | --- | --- |
| **T2.1 precompiled header** | **3.00 %** CPU reduction | ≥ 20 % | **FAIL** |
| **T2.2 forward-declared headers** | **−28.85 %** (i.e. 29 % *slower*) | ≥ 10 % | **FAIL** |

A precompiled header of the runtime headers buys **3 %**, which is inside the 3.83 % spread of the floor and therefore
**not distinguishable from noise** (T2.8). Forward-declaring instead of including `<cstdint>` made things **29 % worse**
— not because the include is expensive, but because the variant also changed the integer spelling from `uint16_t` to
`unsigned short`, and `<cstdint>` is evidently cheaper to parse than the extra implicit conversions cost. **Neither
result supports the "header-parse overhead" explanation, and together they refute it.**

This is the revision's most consequential result: **the standard advice for slow generated C++ — trim the includes,
precompile the headers — does not apply here.** The generated headers are already minimal (one `<cstdint>`), and the cost
is the compiler being invoked 500 times.

## M4 — the chunking frontier

500 components, N per translation unit. A body edit recompiles the chunk holding the edited component.

| N | TUs | wall −j12 | CPU | CPU per TU | **body edit** | components recompiled | CPU vs N=1 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **1** | 500 | 3.43 s | 35.77 s | 71.5 ms | **0.119 s** | 1 | 100 % |
| 5 | 100 | 1.23 s | 10.84 s | 108.4 ms | 0.143 s | 5 | 30.3 % |
| 10 | 50 | 0.95 s | 8.18 s | 163.6 ms | 0.169 s | 10 | 22.9 % |
| 25 | 20 | 0.81 s | 4.02 s | 201.0 ms | 0.253 s | 25 | 11.2 % |
| **50** | 10 | 1.33 s | 3.30 s | 330.0 ms | **0.390 s** | 50 | **9.2 %** |

**T2.3 PASS** — chunking at N=50 cuts clean CPU to **9.23 %** of one-TU-per-component, a 10.8× reduction.

**T2.4 FAIL** — a body edit costs **3.277×** at N=50 against a 3× threshold. It misses, but only just, and the shape of
the curve is the useful part: the edit cost grows roughly **linearly in N** (0.119 → 0.390 s for 1 → 50) while the clean
cost falls **sub-linearly in the number of translation units**. Extrapolating, chunking is not close to a cliff; it trades
a bounded, predictable edit penalty for an order of magnitude on clean build.

**Note on N=50 wall (1.33 s) exceeding N=25 (0.81 s)** despite lower CPU: at 10 translation units there is not enough
parallelism for 12 cores, so wall time stops tracking CPU. Any chunk size at or above 25 trades parallelism for clean CPU.

## M6/M7 — compile versus link, memory, size

| measurement | value |
| --- | --- |
| one-TU body edit, **compile only** | **0.103 s** |
| **full-suite link** (501 objects) | **0.138 s** |
| link share of a one-TU edit | **57.26 %** (threshold ≤ 25 %, **FAIL**) |
| peak compiler RSS, M1 | **16.6 MB** |
| peak compiler RSS, M3 | **26.8 MB** |
| linked binary size | **4 272 B** |

### The link, not the compile, dominates an incremental edit

**T2.5 fails at 57.26 %.** A one-TU edit costs 0.103 s of compile and 0.138 s of link: **the link is larger than the
compile.** Prototype B recorded the edit as a single 0.97 s figure and the design doc has been reasoning about it as a
compile cost. It is not. This has a direct consequence for chunking — chunking reduces compile work but does nothing for
link time, so **the cheaper a compile becomes, the more the link dominates and the less chunking is worth.** At N=50 the
whole clean build is 3.30 s CPU, of which the 0.138 s link is 4 %; at N=1 the same link is a rounding error against
35.77 s, but against a 0.103 s edit it is the majority.

Binary size is negligible (**4 272 B** for 500 components), and peak RSS is modest (**16.6 MB**), so **memory and size are
not constraints on chunk size** — only time is. Both were named in Prototype B's `plan.md` and never measured.

## M8 — the `uint16_t` local-id limit

The generator **refuses** a component with 65 536 properties, exit status 1:

```
error: component declares 65536 properties but a uint16_t PropertyId addresses at most 65535;
reduce the component to 65535 properties or fewer
```

**T2.6 PASS.** Both the offending count and the limit are named. This is a *correct* refusal rather than a wrap, which
matters because a silent wrap would corrupt every property id in the component rather than fail at generation time.

## Two harness defects found and recorded

1. **The first driver run measured nothing.** It passed `-j` to the compiler driver, which has no such flag, so all 500
   compiles failed instantly and the "clean build" appeared to take 0.08 s. The failure mode is the dangerous one: a broken
   build reports as a very cheap build. The driver now **aborts on a non-zero compile status** rather than reporting a
   timing.
2. **The first generator emitted no headers for chunked suites**, so every chunk failed to compile for a missing include.
   Caught by the same abort-on-failure path.

Both were found because the numbers were implausible, not because a test failed. Recorded here because the second is a
reminder that an incremental build harness must prove it built something.

## What this does not show

1. **No baseline.** `[BASELINE]` is unnamed, so **nothing here compares against the workflow that motivated the
   project.** Prototype B's verdict — that AD-03's clean-build claim is contradicted — still rests on a synthetic
   one-TU, one-core baseline. **B2 does not weaken or strengthen it; it measures the generated side only.**
2. **M5 not run**: no body-size or child-count scaling, because that needs a reference UI.
3. **Single machine**, single run of the whole sweep, and the machine is subject to the recorded pacing finding.
4. **Chunking is measured as generated, not as a build system would do it.** Real unity builds hash the chunk contents
   and skip unchanged chunks, which would make incremental *worse* than measured here only if the hash were unstable, and
   better in wall time than the `-j1` compile-only figures reported.
5. **Edit figures are `-j1` and compile-only** by construction, so they exclude any parallel rebuild benefit.
6. **The components are trivially small** (4 properties, no children), matching Prototype B. §14.3's incremental
   behaviour was confirmed against this shape only.

## Findings that change the design

1. **Per-TU fixed cost, not header parsing, is the dominant term** (48.6 ms of 71.5 ms). Chunking is the only lever that
   touches it; precompiled headers and include trimming, the two standard levers, are worth 3 % and −29 %.
2. **The link dominates an incremental edit** (57.26 %), which caps how much any compile-side optimisation can be worth
   and is invisible in B's single 0.97 s figure.
3. **Chunking buys an order of magnitude on clean build for a bounded, linear edit penalty.** At N=25: clean CPU 11.2 % of
   N=1, body edit 2.1×. At N=50: 9.2 % and 3.3×. N=25 is the better point on this curve, and it still uses 20
   translation units so 12-way parallelism survives.
4. **Memory and binary size do not constrain the choice** (16.6 MB peak RSS, 4 272 B binary).

## Proposed split of AD-03 (for the human; no status changed here)

| entry | content | suggested status |
| --- | --- | --- |
| **AD-03a** | Facade headers, interface-only, implementation private, write-if-changed generation | ***Decided*** — validated by Prototype B and confirmed by B2 (write-if-changed restamps 0 files; a body edit rebuilds one translation unit) |
| **AD-03b** | Translation-unit granularity: one component per TU versus unity chunking | ***Open*** — the frontier above is the evidence; **recommend a documented chunk size of 25 with a PCH escape hatch**, and record that the reason is per-TU fixed cost, not header parsing |

**Conditions on the AD-03b recommendation**, stated here so they travel with it:

- it rests on **no baseline comparison**; a named baseline could change the trade entirely;
- it is a **single-machine** measurement;
- the 25-versus-50 choice is decided by a **2.1× versus 3.3× edit penalty against an 11.2 % versus 9.2 % clean
  difference** — a 2 % clean gain for a 55 % edit penalty, so N=25 unless clean builds dominate the workflow;
- **AD-03's clean-build performance claim remains contradicted** by Prototype B against its synthetic baseline, and B2
  shows what would have to change for it to hold: not smaller headers, but fewer translation units.
