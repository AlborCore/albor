# Prototype E — Dev Runtime and Layout Invariance

Plan for Prototype E (§2.3, §17.2, §30, AD-02, AD-25). Prototype code: not linked by any
release module (AGENTS.md). Thresholds recorded **before** the run.

## Questions

| # | Question | Spec |
| --- | --- | --- |
| Q1 | Is the memory layout (size and field offsets) of key UI node classes identical in Release and Dev builds? | §2.3, AD-02 |
| Q2 | Are virtual table offsets for the primary virtual interfaces identical across Release and Dev? | §2.3, AD-02 |
| Q3 | Can an IR tree serialized from the compiled path be loaded by `devrt` and produce the same observable property values? | §2.4, AD-25 |
| Q4 | Can a `UiPatch` (`SetProperty`, `ReplaceBinding`) be applied to a live node by explicit `id:` key without changing any Release class layout? | §18.1, AD-11, AD-25 |
| Q5 | Do Dev-only features (IR loader, reload hook, side tables) attach exclusively through null-checked extension hooks that exist in Release as no-ops? | §2.3, AD-02, AD-04 |

## Hardware and Environment

- **Machine:** Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM
- **OS:** macOS 26.6.2 (Darwin 25.6.0)
- **Toolchain:** Apple Clang 21.0.0, C++23, libc++
- **Build presets measured:**
  - `release` (defines nothing; no `ALBOR_DEV`): the production binary
  - `debug` (defines `ALBOR_DEV`, links `devrt`): the development binary
- **Runs:** layout checks are deterministic (single run sufficient); IR/patch tests run ≥ 5 times

## Workload Parameters

- **Node classes under test:** a minimal `UiNode` base, a `TextNode`, and a `ContainerNode` — three representative classes that span the single-root hierarchy
- **IR tree:** 16 nodes, 4 levels deep, 4 properties each = 64 property slots; one binding chain of depth 4
- **Patches:** `SetProperty` on 3 leaf nodes by explicit `id:`; `ReplaceBinding` on 1 binding by explicit `id:`
- **Side-table probe:** registration hook invoked in Dev, null-no-op in Release; verified by the link boundary check (`build/check-link-boundary.sh`)

## Layout Invariance Check (Q1, Q2)

The test executable (built under both presets) calls `static_assert` and a runtime table dump:

```
sizeof(UiNode)              Release == Dev  (exact byte equality)
offsetof(UiNode, <field>)   Release == Dev  (for every field)
sizeof(TextNode)            Release == Dev
sizeof(ContainerNode)       Release == Dev
vtable slot 0 of UiNode     Release == Dev  (first virtual destructor)
vtable slot 1               Release == Dev  (first virtual method)
```

The dump is written to `RESULTS.md` verbatim. Any difference is a defect (AD-02 violation).

## Dual-Engine Conformance (Q3, Q4)

1. The compiled C++ path initialises an IR tree and records the value of each property via a thin `read_property` accessor.
2. `devrt` loads the serialised IR, walks the same tree, and reads the same properties.
3. A `UiPatch` is applied; property values are re-read from both engines.
4. **Pass:** all values agree exactly (integer and float types; floats compared bitwise for this prototype since no arithmetic is involved).

## Thresholds

| # | Threshold | Pass criterion | Rationale |
| --- | --- | --- | --- |
| T1 | `sizeof` difference across Release/Dev for any node class | **= 0 bytes** | AD-02: layout invariance is non-negotiable |
| T2 | `offsetof` difference across Release/Dev for any field | **= 0 bytes** | AD-02 |
| T3 | Vtable slot difference across Release/Dev | **= 0** (slot indices and arities identical) | AD-02 |
| T4 | Dual-engine property agreement (compiled vs. IR loader) before patch | **100 % exact match** | AD-25 |
| T5 | Dual-engine property agreement after `UiPatch` application | **100 % exact match** | AD-25 |
| T6 | Dev-only symbols present in Release binary | **0** (link boundary check must pass) | AD-04, AD-10 |
| T7 | Registration hook in Release | **non-null only if linked** — the hook exists as a function pointer initialised to `nullptr`; Release build must link with the pointer present but null | AD-02: hook slot in Release layout, null value confirms no dev code linked |

## Failure Handling

A threshold failure is recorded as a failure in `RESULTS.md`. No threshold may be weakened post-run.
If T1, T2, or T3 fails, the exact byte counts are recorded; AD-02 is not established for this prototype.
If T4 or T5 fails, the differing property IDs and values are recorded; AD-25 is not established.
If T6 fails (dev symbols found in Release), the offending symbols are listed.
