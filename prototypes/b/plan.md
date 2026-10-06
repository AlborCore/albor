# Prototype B — Generated-Code Build Performance

Plan for Prototype B (§14, §30, AD-03, AD-04a, AD-19). Prototype code: not linked by any
runtime module (AGENTS.md). Thresholds recorded **before** the run.

## Questions

| # | Question | Spec |
| --- | --- | --- |
| Q1 | Is a clean build of 500 generated facade components ≤ 50 % of the equivalent baseline build time? | §14, AD-03 |
| Q2 | After a local `.ui` body edit, does exactly 1 translation unit rebuild and complete in ≤ 1.5 s? | §14.2, §14.3 |
| Q3 | After a component interface change, does the rebuild scope stay within that component's header and its direct includers? | §14.3 |
| Q4 | Does write-if-changed generation prevent unchanged files from acquiring a new timestamp and triggering spurious rebuilds? | §14.2 |
| Q5 | Does component-local `PropertyId` allocation (local offset + runtime base offset) prevent a header change in Component A from invalidating Component B? | §14.2, AD-04a |

## Hardware and Environment

- **Machine:** Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM
- **OS:** macOS 26.6.2 (Darwin 25.6.0)
- **Toolchain:** Apple Clang 21.0.0, C++23, libc++, Ninja 1.13.2
- **Build preset:** `release` (`cmake --preset release`, `-O2`); clean-build times also recorded under `debug`
- **Parallelism:** `-j12` (all logical cores)
- **Runs:** ≥ 5 clean builds per configuration; median and spread (min/max) reported

## Workload Parameters

- **Component count:** 500 components in both the generated suite and the baseline
- **Component shape (both sides):** each component declares 4 properties, 2 bindings, 1 child component reference, and exposes 3 public callbacks — a representative widget-scale component
- **Generator:** a Python script that writes one `.h` / `.cpp` pair per component (generated suite) or one template-instantiation header per component (baseline); runs write-if-changed (compares content before writing)
- **Binding representation:** flat CSR records matching the Prototype D result (AD-04a, AD-04d)
- **`PropertyId` scheme:** component-local offset (16-bit) + runtime base offset registered at component startup; no global monomorphic IDs

## Configurations Under Test

| Label | Description |
| --- | --- |
| `generated` | 500 Albor-style generated facade components: concrete non-virtual `.h` + opaque-impl `.cpp`, component-local `PropertyId`s, write-if-changed generator (AD-03) |
| `baseline` | 500 equivalent components in the pre-Albor workflow: class template defined entirely in `.h`, all implementation visible at every include site, global monotonic `int` property IDs |

## Measurements

| Measurement | Description |
| --- | --- |
| Clean build time | Wall time for `cmake --build` from zero object files, both configurations |
| Local body edit rebuild scope | Touch the `.cpp` of one component (body change), measure TUs rebuilt and wall time |
| Interface edit rebuild scope | Touch the `.h` of one component (interface change), measure TUs rebuilt and wall time |
| Unrelated edit rebuild scope | Touch only `main.cpp` (application entry point), measure TUs rebuilt |
| Write-if-changed check | Re-run the generator without modifying any `.ui` source; assert zero file timestamps change |
| `PropertyId` isolation check | Change a property declaration in component 0 only; verify component 499's object file is not rebuilt |

## Thresholds

All thresholds apply to the reference machine (see Hardware above) with `-j12` and `cmake --preset release`.

| # | Threshold | Pass criterion | Rationale |
| --- | --- | --- | --- |
| T1 | Clean build time ratio (`generated / baseline`) | **≤ 0.50** (generated builds in ≤ 50 % of baseline time) | AD-03 claim: concrete facades build faster than template-heavy headers |
| T2 | Local body edit: TUs rebuilt | **= 1** (exactly one `.cpp`) | §14.3: a body edit must not escape the component |
| T3 | Local body edit: wall time | **≤ 1.5 s** | §14.3 / user-specified budget |
| T4 | Interface edit: TUs rebuilt | **≤ direct includer count + 1** (header + includers only, not whole tree) | §14.3: interface change scope is bounded |
| T5 | Write-if-changed: files touched by re-running generator unchanged | **= 0** (no new timestamps) | §14.2 |
| T6 | `PropertyId` isolation: object files rebuilt when only component 0 property changes | **component 499's `.cpp.o` not rebuilt** | §14.2, AD-04a |
| T7 | Unrelated edit (main.cpp only): TUs rebuilt | **= 1** (only `main.cpp`) for generated; reported for baseline | Confirms no transitive header pollution |

## Failure Handling

A threshold failure is recorded as a failure in `RESULTS.md`. No threshold may be weakened post-run.
If T1 fails, the ratio and absolute times are recorded and the AD-03 decision remains *Proposed*.
If T2 or T3 fails, the exact TU list and timings are recorded; the §14.3 invariant is not established.
