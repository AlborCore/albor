# Prototype B — Results

Generated-code build performance (§14, §30, AD-03, AD-04a, AD-19). Thresholds were recorded in
`plan.md` before any run and are **not** weakened here. Reproduce with:

```sh
bash prototypes/b/measure_t1.sh      # clean build time, T1
bash prototypes/b/measure_scope.sh   # rebuild scope and generator behaviour, T2 through T7
```

## Machine

Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM, macOS 26.6.2 (Darwin 25.6.0),
Apple Clang 21.0.0, C++23, libc++, Make, `-j12`. 500 components on each side. ≥ 5 runs per
configuration.

## Summary

| # | Threshold | Criterion | Measured | Verdict |
| --- | --- | --- | --- | --- |
| T1 | Clean build ratio, generated / baseline | ≤ 0.50 | **89.6×** (CPU) / **12.0×** (wall) | **FAIL** |
| T2 | Body edit: TUs rebuilt | exactly 1 | 1 | PASS |
| T3 | Body edit: wall time | ≤ 1.5 s | 0.97 s | PASS |
| T4 | Interface edit: TUs rebuilt | header + direct includers | 2 (the component and the driver) | PASS |
| T5 | Write-if-changed: files restamped | 0 | 0 | PASS |
| T6 | PropertyId isolation: component 499 rebuilt | not rebuilt | not rebuilt | PASS |
| T7 | Unrelated edit: TUs rebuilt | 1 | 1 | PASS |

**6 of 7 pass. T1 fails, and not marginally.**

## T1 — clean build time

Five clean builds per configuration, every object file deleted before each run (the scripts report the
object count so a failed clean cannot be mistaken for a fast build).

| Configuration | Wall (median) | Wall range | CPU user+sys (median) | CPU range |
| --- | --- | --- | --- | --- |
| generated | 4.45 s | 4.26 – 4.56 | **34.90 s** | 34.31 – 35.95 |
| baseline | 0.37 s | 0.36 – 0.38 | **0.39 s** | 0.38 – 0.40 |

Ratio, generated / baseline:

- **CPU: 34.90 / 0.39 = 89.6×** — the threshold requires ≤ 0.50, so this fails by a factor of ~179.
- **Wall clock: 4.45 / 0.37 = 12.0×** — also fails, and this is the *favourable* reading, because wall clock
  flatters the generated side for a reason that has nothing to do with code style (below).

Both readings are reported because they measure different things and the plan named neither.

### Why wall clock is the wrong metric here, and CPU is the right one

The two configurations do not have the same shape. The generated suite is **500 separate `.cpp` files**,
so it compiles 500 translation units in parallel across 12 cores. The baseline is a **single translation
unit** — one `.cpp` that includes 500 headers and instantiates 500 class templates — so it compiles on
one core while 11 sit idle.

Wall clock therefore compares 12 cores of work against 1 core of work. That is why the generated side
spends 34.9 s of CPU but only 4.45 s of wall time. A wall-clock ratio of 12.0× mostly measures core
count. CPU time removes the parallelism from both sides and is the metric that answers the question the
plan actually asks: *how much compiling does this architecture cost?* On that metric the answer is
~90×, and no amount of parallelism hides it.

### Why the generated side is more expensive, and what that does and does not mean

The plan's premise is that a concrete facade over an opaque implementation builds faster than a
template-heavy header, because an includer sees a declaration instead of a body. **For incremental work
that premise holds and is confirmed below** (T2, T3, T4, T6, T7). For *clean* builds it does not, and the
reason is structural rather than incidental:

- The baseline pays for its templates **once**, in one TU: 500 headers parsed once, 0.39 s of CPU.
- The generated suite pays a **fixed per-translation-unit cost 500 times** — a compiler process, a
  preamble, and a full set of built-in initialisation per `.cpp`. The component bodies are trivial; the
  overhead is in *having 500 of them*.

That is the cost of the code-generation architecture itself, not an artefact of how this prototype was
written. The obvious mitigations all trade against the architecture's own claims, so none were applied:

- **Fewer, larger `.cpp` files** would cut the per-TU overhead, but §14.3 requires that a body edit rebuild
  exactly one component's `.cpp`. Merging components destroys the incremental property that T2 confirms.
- **Precompiled headers or a shared preamble** would amortise the fixed cost. That is a build-system
  mitigation, not an architecture claim, and it would need measuring on its own before being credited.
- **`-flto` / unity builds** would collapse the TU count and invalidate T2's measurement.

So T1 is recorded as a genuine failure of the clean-build claim, not as a defect in the generated-code
design. **AD-03's clean-build performance claim is not established.** Its incremental claims are, and
they are the ones §14.3 actually depends on.

### What T1 does not say

T1 does not show the generated code is worse. It shows a different distribution of cost: much more
compiler work in total, spread across cores, and far less work per edit. For the workflow §14.3 is
written around — edit one component, rebuild a little — the generated side wins by 1.0 s per edit against
a 34.5 s clean-build penalty. Whether that trade favours Albor depends on edit frequency and clean-build
frequency in real use, which this prototype does not measure and `plan.md` does not claim.

## T2 / T3 — local `.ui` body edit

A comment appended to `Component250.cpp` (a body change), then a rebuild:

- **TUs rebuilt: 1** — exactly `Component250.cpp.o`. Threshold is exactly 1. **PASS.**
- **Wall time: 0.97 s** against a ≤ 1.5 s threshold. **PASS.**

This is the §14.3 invariant, and it holds: a body edit does not escape the component.

## T4 — interface change scope

A comment appended to `Component250.h` (an interface change), then a rebuild:

- **TUs rebuilt: 2** — `Component250.cpp.o` and the driver TU that includes the header.
- Wall time 0.98 s.

The component itself plus its direct includer, with no cascade into the other 498 components.
**PASS.**

This is a direct consequence of the fix recorded in `6a36369`. While each header included its
predecessor's header, the 500 components formed one 500-deep include chain, so *any* header edit
invalidated every component after it and T4 was unreachable by construction. With the child held as a
forward-declared pointer, the include graph is 500 independent nodes and the scope is bounded.

## T5 — write-if-changed

The generator is re-run twice with no `.ui` source change, from a settled state (one throwaway run first,
so the measurement does not inherit whatever the previous build left behind).

- Generator reports `files written: 0` for both suites.
- A digest of every generated file's mtime is identical before and after.

**PASS.** An unchanged generation cannot restamp a file and trigger a rebuild that should not happen
(§14.2).

One condition is worth recording, because it would otherwise have made this threshold fail for an
unrelated reason: **the generator emits the project's own format.** An earlier version generated code
that then had to be reformatted afterwards. Because T5 compares *content*, a generator whose output is
post-processed would restamp all 1,500 files on every run and fail this threshold for a reason having
nothing to do with the behaviour under test. `generate.py` now emits `.clang-format`-clean output
directly, and a generated tree passes `bash build/check-format.sh`.

## T6 — component-local PropertyId isolation

`kPropColor` in `Component000.h` changed from `0` to `9`; nothing else touched.

- Rebuilt: `Component000.cpp.o` and the driver TU only.
- **Component 499 was not rebuilt.** Threshold met. **PASS.**

The generated components number their properties as `uint16_t` local offsets (`kPropColor = 0` …
`kPropHeight = 3`) over a `m_base_offset` supplied at registration; the baseline numbers them globally
and monomorphically (`kProp499_Color = 499 * 4 + 0`). Editing one component's declaration cannot
disturb another's, which is what AD-04a claims and what T6 confirms.

## T7 — unrelated edit

A comment appended to the driver TU only:

- **TUs rebuilt: 1** — `bench_generated.cpp.o` alone. No component rebuilt. **PASS.**

Confirms no transitive header pollution: editing the application entry point does not drag in any of the
500 components.

## Defects found and fixed while measuring

These were blocking the prototype and are recorded because each would otherwise have produced a
misleading number rather than no number.

1. **500-deep include chain** (`6a36369`). Each header included its predecessor's, so Clang refused the
   build outright with `#include nested too deeply`. Beyond not compiling, the chain is the opposite of
   what the prototype measures: AD-03 is an opaque implementation behind a concrete facade, and a chain
   that makes every header depend on every earlier one would make T4 unreachable even if it compiled.
   Both suites now hold the child as a forward-declared pointer. The comparison is unchanged — the
   baseline remains template-heavy with the whole body visible at the include site and global ids.

2. **The baseline was not being compiled.** The original driver included 2 of the 500 headers, so the
   "baseline" build was compiling almost nothing and T1 compared a real suite against a stub. The driver
   now instantiates all 500.

3. **Both suites were being optimised away.** Instantiating a component and discarding the result lets
   the optimiser elide it: the baseline object file was **752 bytes** for 2,510 lines of instantiations.
   Each driver now accumulates a value read back from every component, so the work is observable and
   cannot be discarded. The baseline object is now 65,896 bytes.

4. **The measurement scripts reverted uncommitted work.** Restoring an edit with `git checkout` discarded
   the rewritten drivers, which produced a *failing* build that a careless reading would have scored as a
   very fast clean build. The scripts now restore from the generator (the source of truth for generated
   files) and from a saved copy for the hand-written driver.

## What this leaves open

- **AD-03's clean-build claim is not established** and is now contradicted at this workload size. It
  should move from *Proposed* support toward an explicit trade statement, or T1's premise should be
  revisited: §14.3 is about edit scope, and the clean-build number is a different claim.
- A shared-preamble or precompiled-header mitigation would plausibly close much of the 34.5 s gap. It is
  a build-system change, not an architecture change, and it is **not** measured here.
- No measurement covers memory consumption or binary size, both listed in `plan.md`'s measurement table.
  Compiler peak RSS was not recorded, so neither is claimed.
- Single machine, single toolchain, 500 components at one component shape (4 properties, 2 bindings,
  1 child, 3 callbacks). Whether the crossover between clean-build cost and incremental cost moves with
  component count is not measured.