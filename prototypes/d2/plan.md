# Prototype D2 — Binding Engine Follow-up (typed storage, diamond topologies, runtime mutation)

**thresholds below are committed before any benchmark in this prototype is run (§28.4)**

Plan for Prototype D2 (§30, §13, AD-04b, AD-04c, AD-24). Prototype measurement code: not a runtime module, not linked by
one (AGENTS.md). Prototype D is historical evidence and is neither modified nor re-run here.

## Relationship to Prototype D

Prototype D left five questions open. Its own `RESULTS.md` lists them, and AD-04b/AD-04c stay *Open* because of them:

| # | Question left open by D | Experiment here |
| --- | --- | --- |
| 1 | "The property slot layout (56 bytes) is dominated by the type-erased variant; a per-component typed storage strategy is unmeasured." | §1 |
| 2 | "a pull/lazy model is unmeasured and would change both the wide-update and the deep-chain cost" | §2, §3 |
| 3 | "The workload is synthetic; a `.ui` corpus would be needed to confirm the fan-out it assumes." | §3, reconvergent topologies |
| 4 | "the devrt expression VM path needs the dynamic alternative measured before AD-25 conformance can be designed" | §4, §6 |
| 5 | AD-04d: "whether ending an animation re-evaluates the shadowed binding rather than restoring a stale value was not verified" | §8 |

Prototype D's recorded numbers are reused as the comparison baseline and are **not** restated as new measurements:
56 B/property, 39.3 B/binding, depth 8 → 8 passes, 0 redundant candidate visits, 64 B animation slot, 64-pass cycle
guard. Where D2 reproduces a D workload it says so explicitly and reports the D number alongside.

## Hardware and environment

- **Machine:** Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM
- **OS:** macOS 26.6.2 (Darwin 25.6.0)
- **Toolchain:** Apple Clang (Xcode SDK 15+), C++23, libc++
- **Build:** `cmake --preset release` (`-O3`), the only configuration measured
- **Machine notes:** the recorded platform pacing finding (windowed `CADisplayLink` at 0 Hz in ~half of runs) does not
  apply here; D2 measures no windowed presentation.

## Commands

```sh
cmake --preset release
cmake --build --preset release
./.build-release/tests/albor-tests --gtest_filter='D2*'      # correctness
./.build-release/prototypes/d2/albor-prototype-d2-benchmark  # measurements
./.build-release/prototypes/d2/albor-prototype-d2-benchmark --section storage
./.build-release/prototypes/d2/albor-prototype-d2-benchmark --section chains
./.build-release/prototypes/d2/albor-prototype-d2-benchmark --section diamonds
./.build-release/prototypes/d2/albor-prototype-d2-benchmark --section mutation
./.build-release/prototypes/d2/albor-prototype-d2-benchmark --section ids
./.build-release/prototypes/d2/albor-prototype-d2-benchmark --section replace
./.build-release/prototypes/d2/albor-prototype-d2-benchmark --section cycles
./.build-release/prototypes/d2/albor-prototype-d2-benchmark --section animation
```

Every timed section discards 200 warm-up updates and records at least 2,000 measured repetitions, or at least 200 for
sections whose unit is a rebuild or a cycle detection. Sections report p50/p95/p99 and the raw counters (passes,
bindings evaluated, redundant evaluations, invalidations) rather than only timings.

## Experiment 1 — Typed property storage (AD-04b)

Two candidates over one identical property mix:

- **Candidate A, `typed_per_component`.** A component *type* declares its property types in a schema; storage is inline
  bytes at precomputed per-property offsets inside a chunked slab. The type tag is known from the schema, so no slot
  carries a discriminant. Slot addresses must stay stable, so the slab is chunked rather than one contiguous vector that
  reallocates. Non-trivial values (`String`, `TranslatedString`, `Snapshot`) are constructed and destroyed in place.
- **Candidate B, `type_erased`.** The D representation, one fixed-size variant per slot, extended with `TranslatedString`.

Realistic mix, mirroring §13.1 categories 1–9: `Bool`, `Int`, `Double`, `String`, `TranslatedString`, `Snapshot`.

Measured: bytes/property, allocations, construct and destroy cost for non-trivial values, single-write update cost,
effect on evaluation, and the memory penalty of stable slot addressing.

| Threshold | Value | Rationale |
| --- | --- | --- |
| T1.1 typed bytes/property, all-scalar component (8 × `Int`/`Double`/`Bool`) | ≤ 16 B | The layout question itself: D's variant slot was 56 B. Inline typed storage must beat it by a wide margin for the common case. |
| T1.2 typed bytes/property, realistic mixed component | ≤ 48 B | Real components mix strings and snapshots; a scheme that only wins for scalars is not an answer. |
| T1.3 typed bytes/property ≤ erased bytes/property, same mix | must hold | Otherwise typed storage trades memory for a type-safety story, which is not the trade being made. |
| T1.4 allocations to create one instance of an 8-property component | ≤ 1 | No per-property and no per-value allocation; one slab chunk at most. |
| T1.5 construct + destroy cost per non-trivial property | ≤ 200 ns | String and snapshot destruction is a refcount release plus a payload free. |
| T1.6 single `Int` write + settle, typed, p99 | ≤ 250 ns | A scalar write is a store plus a dependent scan. Anything near a frame budget would hide a per-write allocation. |
| T1.7 typed memory overhead for stable slot addressing | reported, no threshold | Chunking buys stable addresses at some slack cost; the size of that cost is an input to AD-04b, not a pass/fail. |

## Experiment 2 — Deep dependency chains (AD-04c)

Chains of depth 1, 2, 4, 8, 16, plus D's 25,000-node depth-8 shape for comparability.

Three evaluation models:

- **`eager_push`** — D's model: invalidate, plan dependents, evaluate, repeat until the plan is empty.
- **`pull_topo`** — Kahn topological order computed once at seal; a settle is one pass in that order.
- **`hybrid_push_pull`** — push marks the dirty binding set, then a memoised recursive pull computes each dirty binding
  exactly once, pulling its dependencies first.

| Threshold | Value | Rationale |
| --- | --- | --- |
| T2.1 `eager_push` settle p99 at depth 16 | ≤ 50 µs | 16 bindings is trivial work; the point is the pass count, not the latency ceiling. |
| T2.2 `eager_push` passes at depth N | recorded as `N` | D measured one pass per chain layer. D2 must state it, not hide it. |
| T2.3 `pull_topo` and `hybrid_push_pull` passes at depth 16 | == 1 | This is the architectural claim of both alternatives: a pre-ordered or recursive traversal must not iterate to a fixpoint. |
| T2.4 redundant evaluations per settle, all models, at depth 16 | == 0 | A chain has no reconvergence, so re-evaluating any binding is waste. |
| T2.5 D's depth-8 scene, `eager_push` passes | == 8 | Must reproduce D's recorded 8 passes; a disagreement would mean D2's model is not D's. |

## Experiment 3 — Diamond and reconvergent topologies (AD-04c)

- **Simple diamond** `A → {B, C} → D`, width 2.
- **Wide diamond** `A → {B₁…B_w} → D`.
- **Layered reconvergent** `A → {B₁…B_w} → … → D` where `D` is reachable at *different depths* through different paths.
  This is the case D never ran: its workload reported 0 redundant visits, and this topology is what produces them.

| Threshold | Value | Rationale |
| --- | --- | --- |
| T3.1 redundant evaluations, simple diamond, all models | == 0 | `B` and `C` change in the same pass, so dedup must evaluate `D` once. Any non-zero value is a dedup defect. |
| T3.2 redundant evaluations, layered reconvergent (w=8, d=8), `hybrid_push_pull` | ≤ 1 | The memoisation guarantee is the reason to prefer the hybrid; one evaluation per dirty binding per settle. |
| T3.3 redundant evaluations, layered reconvergent, `eager_push` | recorded, no threshold | The measurement *is* the finding; a threshold here would only state the expected answer in advance. |
| T3.4 settle p99, wide diamond w=32 and layered reconvergent w=32 d=8, all models | ≤ 1 ms | A thousand-binding settle at 32-wide fan-in must stay far inside a frame. |
| T3.5 invalidations, layered reconvergent | recorded | Distinguishes "re-evaluated" from "re-invalidated": dedup at plan time does not stop re-invalidation. |

## Experiment 4 — Runtime graph mutation (AD-04c)

Operations on a live engine: add property, remove property, add binding, remove binding, add a dependency to an existing
binding, remove a dependency, replace a binding's whole dependency set. Two index designs:

- **`sealed_csr`** — D's flat CSR index. Mutating it requires a full rebuild; the rebuild cost is the measurement.
- **`segmented`** — per-property segments with amortised growth and tombstones on removal. Add and remove are local.

| Threshold | Value | Rationale |
| --- | --- | --- |
| T4.1 `segmented` add-binding, p99 | ≤ 5 µs | Appending one entry to one segment. A rebuild-scale cost here would mean the segment is not actually local. |
| T4.2 `segmented` remove-binding, p99 | ≤ 5 µs | Tombstone and count. |
| T4.3 `segmented` replace-binding (live reload), p99 | ≤ 50 µs | One add plus one remove plus a re-evaluation of the target. |
| T4.4 `sealed_csr` mutation | must require a full rebuild; cost reported | If it did not, D's index would not be sealed and the recorded D limitation would be wrong. |
| T4.5 `segmented` add-binding cost ÷ `sealed_csr` full rebuild cost | ≤ 1 % | The quantitative form of "mutation does not rebuild the graph". |
| T4.6 settle p99 after 1,000 mutations | ≤ 1 ms | Tombstones and slack must not degrade steady-state evaluation. |
| T4.7 a removed binding receives no further evaluations | must hold | Correctness, not timing. |
| T4.8 ids of untouched properties stay valid and addressable after mutation elsewhere | must hold | Correctness; §5 makes this load-bearing. |

## Experiment 5 — Component-local `PropertyId` with runtime base offsets (AD-04b, AD-04a)

Component-local `uint16_t` ids resolved through a per-instance base offset, as Prototype B established for generated code,
extended here to mutable graphs: multiple instances of one component type, delegate instances, popup instances, and
region reuse after destruction.

| Threshold | Value | Rationale |
| --- | --- | --- |
| T5.1 typed property read via `base + local` ÷ read via a direct global id | ≤ 1.5× | The base-offset indirection is the price of component locality; above 50 % overhead it would argue against AD-04a's direction. |
| T5.2 component instance create/destroy, p99 | ≤ 2 µs | Region allocation from a free list plus schema-relative construction. |
| T5.3 ids remain valid across create/destroy of *other* instances | must hold | Correctness. |
| T5.4 a destroyed instance's ids are detectable as stale, not silently aliased to a reused region | must hold | Correctness; a stamp is the mechanism under test. |
| T5.5 region reuse across component types with **different** property counts | must be rejected or explicitly reported as invalidating | The dense id space shifts when a region's size changes. This is the structural finding; the test must pin the behaviour down. |
| T5.6 id indirection table cost | reported | The alternative to rejecting reuse is an id → location table; its size is an AD-04b input. |

## Experiment 6 — Live-reload `ReplaceBinding` (§13.4, AD-24)

`ReplaceBinding` as a real binding replacement: a different binding function and a different dependency set on an
existing property. It must **not** be implemented as `SetProperty`.

Sequence: property with binding A → replace with binding B → change B's source dependencies → verify invalidation
follows B → verify A no longer participates → verify the value is correct → verify dependents see the update → replace
again → remove the binding.

| Threshold | Value | Rationale |
| --- | --- | --- |
| T6.1 the eight-step sequence in the prompt | all steps must hold | Correctness; this is the behaviour Prototype E did not validate. |
| T6.2 binding A must not invalidate or evaluate after replacement | must hold | The stale-binding elimination that E could not test. |
| T6.3 `replace_binding` p99 | ≤ 50 µs | Same as T4.3; it is the same operation. |
| T6.4 an imperatively overridden property whose declared binding did **not** change keeps its override | must hold | §13.4 states this. |
| T6.5 an imperatively overridden property whose declared binding **did** change | **recorded as unresolved** | §13.4 specifies only the first case. D2 must not invent a permanent rule; the behaviour is observed and the decision is referred to AD-24. |
| T6.6 remove-binding leaves the property holding its last computed value and stops participating in evaluation | must hold | §13.4 gives no removal semantics; this is the least surprising reading and is recorded as a proposal, not as spec. |

## Experiment 7 — Cycle detection (§2.4, AD-04c)

Cycles introduced statically (`A → B → A`), by layered mutation, and by runtime dependency insertion. Also a *converging*
cycle (`A = min(B, 1)`, `B = max(A, 0)`), which is the interesting case: a pass budget stops a non-converging cycle but a
converging one merely wastes passes.

| Threshold | Value | Rationale |
| --- | --- | --- |
| T7.1 a static cycle is detected at seal | must hold | Kahn cannot order a cyclic graph, so detection falls out of the ordering D already ships as a Dev/Test aid. |
| T7.2 detection cost, 10,000-binding graph, p99 | ≤ 5 ms | Kahn over one edge per dependency. |
| T7.3 a cycle introduced by runtime mutation is detected before evaluation | must hold | Dynamic structures exist in Release, so a runtime guard cannot be a Dev-only concern. |
| T7.4 a non-converging cycle is stopped within the 64-pass budget | must hold | D's guard; must still hold once mutation can introduce the cycle. |
| T7.5 a converging cycle's wasted passes | reported | A converging cycle terminates but may still cost every pass; the count decides whether a Release guard is adequate. |
| T7.6 can an invalid cycle reach Release | reported | The answer decides whether cycle detection stays a Dev/Test aid or becomes a shipping guard. |

## Experiment 8 — Animation interaction (AD-04d, AD-24)

Uses D's model: a binding value, a shadowed value, and an animation writing an effective value. Sequence: bound property →
animation starts → animation changes the effective value → animation ends → original binding resumes → source dependency
changes during the animation → imperative write during the animation → `rebind` after the animation.

The decisive case is a dependency changing **while the animation owns the property**, because D's
`PropertyStore::end_animation` restores the shadowed snapshot (`prototypes/d/store.cpp:95`) and `BindingContext::write`
drops writes while an animation owns the target, so the shadow can only hold the value from animation start.

| Threshold | Value | Rationale |
| --- | --- | --- |
| T8.1 ending an animation after its dependency changed during the animation yields the **new** binding value | must hold | This is AD-04d's unverified stale-value question. If restoring the shadow passes, the model has the defect D's entry warns about. |
| T8.2 the same sequence under `hybrid_push_pull` | must hold | Restoration must not depend on the evaluation model. |
| T8.3 an imperative write during an animation cancels the animation and wins | must hold | AD-24 precedence, and D's recorded behaviour. |
| T8.4 `rebind` after an animation re-evaluates from current dependencies | must hold | The recovery path after an override. |
| T8.5 1,024 animated properties written and settled, p99 | ≤ 16 ms | Comparable to D's recorded 0.030 ms p50, so D2 must not regress it. |
| T8.6 animation side-table bytes per animated property | reported | D recorded 64 B; D2 reports its own scheme. |

## Pass/fail policy

A section passes only if every threshold in it passes. A threshold that is a `must hold` correctness item passes on the
test, not on a timing. Where a run fails because of an implementation limit, `RESULTS.md` records it as a **failure** with
the reason; the benchmark is not changed to make it pass. A threshold marked `recorded, no threshold` is never used to
claim a pass.

## Out of scope

No widgets, no `.ui` parser, no code generator, no LSP, no preview, no MCP, no platform window, no Skia, no Dawn, no
accessibility adapter, no production framework. No `.ui` corpus is built; §3 substitutes explicit graph generators and
records that substitution as a limitation, so the "confirm the fan-out on a real corpus" item from D stays open.