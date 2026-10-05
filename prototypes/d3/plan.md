# Prototype D3 — Binding Engine Follow-up (closing D2)

**thresholds below are committed before the benchmark in this prototype exists (§28.4)**

Prototype measurement code. Not a runtime module; nothing under `core/`, `ui/`, `window/` or `render/` may link it
(AGENTS.md, §32). D3 **links Prototype D2's storage and graph harness** rather than reimplementing it, which the close-out
brief sanctions and which keeps D3's numbers comparable to D2's by construction.

Prototype D and Prototype D2 are historical evidence. Nothing in `prototypes/d/` is modified except for clearly-named
measurement accessors, which are committed separately.

## Why D3 exists

D2 produced two recommendations and left both decisions Open. Three of its own results turned out to be unsound or
under-specified when the arithmetic was checked:

1. **AD-04b's headline arithmetic contradicts itself.** It states 8.00 B per scalar property and 7.00 B of addressability
   and then says the win "shrinks to about 2.3× for scalars". Those cannot both describe the same number: 8 + 7 = 15 B
   against a 64 B slot is **4.3×**, not 2.3×. The 2.3× figure is real but belongs to a different property mix.
2. **D2's erased comparison figure is wrong.** `RESULTS.md` reports 128.00 B per property for the erased slot on the
   realistic mix. The cause is a measurement bug: the benchmark gave the typed store and the erased store **the same
   `RegionTable`**, so the erased store grew to 16 slots and then divided by 8 properties. The erased slot is a uniform
   variant and is 64.00 B for every mix.
3. **The two storage columns were never separated from the addressability column**, so a reader cannot tell how much of
   the typed figure is value and how much is bookkeeping.

## Sub-experiments

| # | Scope | State |
| --- | --- | --- |
| **F2.1** | Reconcile the typed-storage arithmetic into one table | **this revision (R1)** |
| F2.2 | Combined memory of storage × index at Prototype D's scale | R2, not yet planned |
| F2.3 | Diagnose D2's two failed thresholds, with mitigations as labelled variants | R3 |
| F2.4 | 2×2 factorial: index × evaluation model, with glitch and cost-weighted metrics | R4 |
| F2.5 | Cost of carrying two evaluation engines | R5 |
| F2.6 | Equality semantics for write-compare (NaN, −0.0, tolerance, strings, snapshots) | R6 |
| F2.7 | Animation shadow table: the minimal necessary mechanism | R7 |
| F2.8 | Binding replacement and removal semantics, as variants | R8 |
| F2.9 | Static cycle rejection with conditional dependencies | R9 |
| F2.10 | Read-path cost, interleaved A/B with spread | R10 |
| F2.11 | Proxy property mix from a real UI | R11 |

Only F2.1 has thresholds. Later revisions append their own before they run; nothing here pre-registers a threshold for
work that has not been designed.

---

## R1 — F2.1: the typed-storage arithmetic table

### Question

What is the true per-property cost of typed per-component storage against the type-erased slot, split into **value**,
**addressability**, **slack** and **region overhead**, for three property mixes, on one identical value set?

### Method

Both candidates are measured over the **same declared property types** in each of three component schemas, and each
candidate gets **its own `RegionTable`**, so neither can inflate the other's property count. The denominator is the
property count of the candidate's own region table, never a hard-coded constant.

Per candidate and per schema, report four columns that must sum to the total:

| Column | Typed | Erased |
| --- | --- | --- |
| **value** | the inline bytes the schema's layout occupies | `sizeof(Value)`, the uniform variant |
| **addressability** | 4 B location + 1 B type + 1 B source + 1 B binding attachment | 0 — the slot already carries all four |
| **slack** | slab chunks allocated minus bytes the live regions occupy | slot-array capacity minus slots in use |
| **region overhead** | `sizeof(Region)` per live instance, plus its base-offset entry | same |

The three mixes:

| Schema | Properties | Why |
| --- | --- | --- |
| `scalar` | 8 × `Int` | the common case; what a numeric property costs |
| `mixed` | `Int`, `Double`, `Bool`, `String`, `TranslatedString`, `Snapshot`, `Int`, `Double` | D2's mix, one of every §13.1 category including a translated string and a snapshot |
| `string_heavy` | 5 × `String`, 3 × `TranslatedString` | labels and text, where pointer payloads dominate |

A fourth measurement instance count (1, 64 and 4 096 live instances per schema) reports how value bytes, slack and
region overhead scale, since a per-instance cost is invisible at one instance and dominant at many.

### Hardware and build

Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM, macOS 26.6.2, Apple Clang, C++23, libc++,
`cmake --preset release` (`-O3`). Memory measurement is deterministic, so R1 reports single figures with no spread and
says so; there is no timing in R1.

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.1.1 | typed total ≤ erased total, **every** mix | the storage question itself. If it fails on any mix, typed storage is not the default |
| T3.1.2 | typed `scalar` total ≤ 24 B/property | 8 B of value plus 7 B of bookkeeping plus slack and region overhead. Generous on purpose: the point is that a scalar must not approach a 64 B slot |
| T3.1.3 | typed `mixed` total ≤ 32 B/property | one of every value category; the realistic upper bound for a component that is not text-dominated |
| T3.1.4 | typed `string_heavy` total ≤ 64 B/property | payload-owning types are 16 B and 24 B inline each, so this mix is where typed storage is weakest and the threshold is correspondingly loose |
| T3.1.5 | the four columns sum to the reported total, every cell, within 0.01 B | **arithmetic closure.** This is the check that would have caught D2's 128.00 B: a table whose parts do not add to its total is not a measurement |
| T3.1.6 | erased B/property is the **same** for all three mixes, spread ≤ 0.5 B | the erased slot is a uniform variant, so its cost cannot depend on the mix. A spread means the two candidates are not being measured on the same value set |
| T3.1.7 | typed slab slack ≤ 25 % of allocated slab bytes | chunked allocation buys stable region addresses at some cost; above a quarter the mechanism is not worth its price |
| T3.1.8 | erased-to-typed ratio per mix | **recorded, no threshold** — the ratio is the finding, and thresholding it would only restate the expected answer |

### Commands

```sh
cmake --preset release
cmake --build --preset release
./.build-release/prototypes/d3/albor-prototype-d3-storage      # R1
./.build-release/tests/albor-tests --gtest_filter='D3*'       # correctness
```

### Pass/fail policy

A threshold passes on the measurement. T3.1.5 and T3.1.6 are consistency checks: if either fails, the table is wrong
regardless of what the other columns say, and R1 reports a failure rather than a corrected table. **D2's published
figures are corrected in the design doc in the same change that reports these results**, per the close-out brief's rule
that a result contradicting an earlier claim corrects the doc.

### Not in R1

No timing, so none of D2's two failed thresholds is revisited (F2.3). No index measurement, so the combined memory
question is untouched (F2.2). The erased slot's internal composition is reported as measured sizes rather than a
hand-derived sum, because a hand-derived sum is what produced the original error.

---

## R2 — F2.2: combined memory of storage × index at scale

### Question

How much do the storage and the index **together** cost per property at a real application's scale, across the three
index representations and both storage candidates? R1 measured storage alone; D2 measured index bytes alone; AD-04b's
recommendation needs the sum, because a cheap store with an expensive index can lose to a uniform slot with a cheap one.

### Method

One committed scale, taken from the close-out brief: **100 000 live properties** (12 500 eight-property instances of the
`mixed` schema) and **11 000 bindings with 2 dependencies each** (22 000 dependency entries), distributed uniformly over
the property id space. A second scale of 10 000 properties / 1 100 bindings reports the shape at small scale.

Index variants, all linked from Prototype D2's harness:

| variant | definition |
| --- | --- |
| `sealed_csr` | D2's `SealedCsrIndex`: flat buckets built once at `finish()`, never changed |
| `segmented` | D2's `SegmentedIndex`: one growable segment per property |
| `hybrid` | new in D3: sealed CSR for every dependency declared before `finish()`, a segmented tail for `add_entry` after it; `dependents()` concatenates both halves |

Each index is combined with each storage candidate (`TypedStore`, `ErasedStore`), each on its own region table. The
hybrid's post-seal activity is 1 000 `add_entry` calls, which is what a live reload or a conditional row adds. Memory is
deterministic, so R2 reports single figures with no spread and says so; there is no timing in R2.

Per cell, report: value bytes, addressability bytes, slack bytes, region overhead, index bytes, **combined total**,
combined B/property, and combined B/binding.

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.2.1 | typed + sealed CSR ≤ erased + sealed CSR, combined total, at 100 000 properties | the combined question R1 could not answer; if the index's cost reverses R1's storage answer at scale, AD-04b's condition must say so |
| T3.2.2 | typed + sealed CSR ≤ 36 B/property (`mixed`) | R1's asymptotic typed figure is ~27.5 B/property on this mix; 22 000 CSR entries plus 100 002 offsets add ~4.9 B/property. 36 B leaves headroom without accepting an unexplained jump |
| T3.2.3 | the segmented index adds ≤ 12 B/property over sealed CSR on the same storage | segments buy mutation locality; above 12 B/property they cost more than the storage saving they protect |
| T3.2.4 | the hybrid ≤ sealed CSR + 6 B/property on the same storage | the tail is 1 000 entries; anything above 6 B/property means the tail mechanism is priced like a full second index |
| T3.2.5 | storage bytes + index bytes == reported combined total, 0.00 B error, every cell | arithmetic closure, the check that caught D2's bug |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-index
./.build-release/prototypes/d3/albor-prototype-d3-index       # R2
```

---

## R3 — F2.3: geometric slab growth, and D2's two failed timing thresholds as labelled variants

### Question

Part A: does growing the typed slab geometrically from a small first chunk move the crossover R1 measured at
256 instances / 2 048 properties down to where every small application and test sits above it?

Part B: do the two mitigations D2's own diagnosis named — reusing the settle's plan buffers, and sharing immutable
payloads instead of allocating one per property — meet D2's **original** budgets (T1.5 ≤ 200 ns, T1.6 ≤ 250 ns)?
The original thresholds stay failed; the variants are judged on the same budgets, not on new ones.

### Method — Part A

D2's `TypedStore` gains a clearly-named chunk-growth policy whose **default keeps the fixed 64 KB chunk**, so every
figure D2 and R1 published stays reproducible; the geometric policy starts the first chunk at **1 KB and doubles up to
the 64 KB cap** (the close-out brief's 1 KB → 64 KB policy). The crossover is re-swept over 1, 16, 32, 64, 128, 256,
512, 1 024, 4 096 and 8 192 instances of the `mixed` schema against the erased store. Address stability is checked by
sampling: write a sentinel through a value pointer taken before a growth event, read it back after every later growth,
and report any address whose read disagrees.

### Thresholds — Part A (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.3.1 | typed total ≤ erased total at **1 instance** (`mixed`) | the 120× blowup R1 measured must be gone, not merely smaller |
| T3.3.2 | crossover ≤ 64 instances (512 properties) | 4× better than R1's 256, and inside the "low hundreds of properties" expectation the recommendation needs |
| T3.3.3 | slack ≤ 25 % of allocated slab bytes at 4 096 instances | the large-scale win T3.1.7 measured must survive the policy change |
| T3.3.4 | address stability: 100 % of sampled sentinels read back correctly after every growth event | the mechanism exists to keep addresses stable; a policy that grows by reallocating is disqualified |
| T3.3.5 | erased ÷ typed ratio at 1 instance | recorded, no threshold — the ratio is the finding |

### Method — Part B, two labelled variants

| variant | what changes | what is measured |
| --- | --- | --- |
| `settle-plan-reuse` | D2's `Graph` reuses the `candidates` and `planned` buffers across settles behind a clearly-named option, default off | single typed `Int` write + settle, p99, ≥ 5 runs |
| `shared-immutable-payload` | the benchmark constructs one `String` payload and one `Snapshot` payload **once** and every property aliases them by `shared_ptr` copy — the §13.1 model, where translated strings and snapshots are immutable and shared | construct + destroy per non-trivial property, median and p99, ≥ 5 runs |

### Thresholds — Part B (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.3.6 | `settle-plan-reuse`: single typed `Int` write + settle, p99 ≤ **250 ns** | D2 T1.6's original budget, unmodified |
| T3.3.7 | `shared-immutable-payload`: construct + destroy per non-trivial property ≤ **200 ns** | D2 T1.5's original budget, unmodified |
| T3.3.8 | both variants also record the un-mitigated figure from the same binary in the same run | no threshold — the delta is the finding |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-slab
./.build-release/prototypes/d3/albor-prototype-d3-slab        # R3
./.build-release/tests/albor-tests --gtest_filter='D3*'       # T3.3.4 correctness
```

---

## R7 — F2.7: animation shadow audit (eliminate the shadow side table)

### Question

D2 concluded "the shadow table is not load-bearing, since the re-evaluate mode reproduces every observable behaviour
without restoring anything" — from reading D's code, not from measuring a build that lacks the table. Does an engine
that **never allocates** the animation shadow side table and relies only on post-animation re-evaluation invalidation
pass the animation correctness suite?

### Method

Two modes of the same graph and animation harness, over D2's animation observable checks — writes to an animated
property are dropped while the animation owns it, `end_animation` hands the target back so the binding re-evaluates, a
dependency that moves mid-animation is picked up after the animation ends, and an imperative override still wins:

| mode | shadow side table | end_animation behaviour |
| --- | --- | --- |
| control | allocated, restore + invalidate (D2's `restore_no_invalidate=false` path) | restore, then invalidate |
| `no-shadow` | **never allocated** | invalidate only; the binding re-evaluates and overwrites |

Each observable check must pass in both modes, and the animation bookkeeping bytes at 1 024 animated properties are
reported for both.

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.7.1 | every animation observable check passes in the `no-shadow` mode | the audit itself; a single failure keeps the shadow table |
| T3.7.2 | `no-shadow` animation bookkeeping ≤ 8 B/animated property at 1 024 animated properties | the attachment byte the slot already has; anything more means a table survived |
| T3.7.3 | the `no-shadow` mode performs zero shadow allocations (allocation-count delta = 0) | "never allocated" must be observable, not asserted |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-shadow
./.build-release/prototypes/d3/albor-prototype-d3-shadow      # R7
./.build-release/tests/albor-tests --gtest_filter='D3*'       # correctness
```

---

## R4 — F2.4: the index × evaluation factorial

### Question

AD-04a leaves **which index structure** Open and AD-04c leaves **which evaluation model** Open-but-confirmed. Those are
two decisions, and they have only ever been measured one at a time. Does the index choice interact with the evaluation
model, or are the two axes separable — in which case the index can be chosen on memory and mutation locality alone and
the model on evaluation behaviour alone? If they interact, they are one decision and this factorial, not either
prototype, is what settles AD-04a.

### Method

The 2×2 core is **{`sealed_csr`, `segmented`} × {`eager_push`, `hybrid_push_pull`}**, because those are the two live
candidates on each axis. `pull_topo` is measured in both index cells as a **recorded** fifth and sixth column: it needs a
sealed order, so it is not a candidate for a graph that mutates, and thresholding it would only restate D2's finding.

Four fixed topologies, one cell each, 200 discarded warm-up settles then 200 measured settles per topology per cell:

| topology | shape | why |
| --- | --- | --- |
| `chain16` | one source, 16 derived properties | the deep-chain case; one pass per layer for the eager model |
| `diamond32` | one source, 32 branches, one sink | the fan-in case; every model must be glitch-free at one hop |
| `reconvergent` | 8 short branches plus one branch of depth 8, one sink | the only topology that produces reconvergence, and therefore redundancy |
| `d_shape` | Prototype D's shape: chain of 8 plus 3 siblings, per node | comparability with D's recorded 8 passes |

**Glitch metric.** Every measured binding logs each value it writes. A **glitch** is a write whose value differs from the
value the same binding writes later in the same settle: a value that was published and then superseded inside one settle
is a transient inconsistency a render pass or an observer could have read. Glitches are counted per settle, per cell,
from the binding's own log rather than inferred from the pass count.

**Cost metrics** per cell: passes, bindings evaluated, redundant evaluations, candidates considered, settle p50/p95/p99,
index bytes, and 1 000 post-seal `add_dependency` calls timed individually.

**Cost-weighted metric.** A composite that a shipping engine actually pays: one reload-heavy session of 1 000 settles
interleaved with 1 000 dependency additions, reported as total milliseconds per cell. The individual numbers are reported
beside it, because a composite that hides its parts is not a measurement.

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.4.1 | **0 glitches** in every cell at `diamond32` | A one-hop fan-in has no excuse for a transient value: if any cell publishes one, the render tree can read an inconsistent frame |
| T3.4.2 | every cell reaches **identical final values** on all four topologies | AD-25 requires the engines to be observably equivalent. If cells diverge they are not interchangeable, and choosing between them is a compatibility decision, not a performance one |
| T3.4.3 | the **index axis** moves settle p50 by ≤ **10 %**, averaged over the two models and the two multi-hop topologies | Above 10 % the index choice is not independent of the model, so "choose the index on memory and mutation locality" stops being a valid plan |
| T3.4.4 | the **interaction term** ≤ 10 % of the larger main effect on settle p50 | The interaction term is the whole question. A factorial whose interaction is inside the noise has separable axes; one whose interaction is not has a single decision, not two |
| T3.4.5 | `segmented` `add_dependency` p99 ≤ **1 µs** at 11 000 bindings, and `sealed_csr`'s ≤ 50× that | D2 measured 0.20 µs against 31.10 µs on a couple of hundred bindings. The mutation-locality argument has to survive at the scale AD-04a actually cares about, where an insertion is a much smaller fraction of a rebuild |
| T3.4.6 | **no cell exhausts** the 64-pass settle budget on the acyclic corpus | The pass guard exists for cycles (§28.2). An acyclic topology that trips it is an engine defect, not a budget question |
| T3.4.7 | recorded: glitches per settle at `reconvergent`, redundant evaluations per settle, and the composite session cost per cell | The finding, not the verdict. T3.4.1 is the only glitch threshold because it is the one that can be stated as a defect |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-factorial
./.build-release/prototypes/d3/albor-prototype-d3-factorial      # R4
```

---

## R5 — F2.5: the cost of carrying two evaluation engines

### Question

AD-25 (dual-engine conformance, §2.4) requires a compiled engine and a dev-time evaluator to produce the same observable
behaviour. Does the shipping binary therefore have to carry **two evaluation engines**, and what does the second one cost
in state, in settle time, and in conformance risk? AD-04 and AD-10 already exclude the dev side at link time, so the
honest question is not whether the code is present — it is whether the *state and the dispatch* the second engine needs
are present in the runtime module, and whether the two engines can coexist over one store without sharing mutable state.

### Method

**Conformance.** One binary, two engines: the same graph is built twice over one store per topology — once with
`eager_push`, once with `hybrid_push_pull` — and driven with the same write sequence. The corpus is the four R4
topologies plus **200 pseudo-random DAGs from a fixed seed** (depth ≤ 6, fan-out ≤ 4, uniform dependency choice), because
a hand-picked corpus cannot show a divergence that only appears at an unusual shape. Every property's final value is
compared; pass counts are recorded for the scenes that disagree.

**State cost.** `Graph` allocates its per-binding machinery for every model it was not asked to run, so the measurement
separates three configurations through a clearly-named accessor: the **model's own subset** (dirty stamps, per-pass plan
stamps and run counts for the eager model; the same plus memo stamps and in-progress marks for the hybrid model; the
same plus the Kahn order and rank for the pull models), the **union** the harness actually allocates, and the difference,
which is the price of carrying an engine the graph never runs. Reported per binding and at 11 000 bindings.

**Isolation.** Two `Graph` instances over one store, settles interleaved (`A, B, A, B, …`) against the same values as
sequential settles. If interleaving changes an outcome, the two engines share mutable state and cannot be linked as
independently excluded units.

**Time cost.** One graph, one invalidation set, both engines run in sequence — the double settle — against the single-engine
settle, so the delta is the price of the second engine at run time rather than at build time.

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.5.1 | **100 % final-value agreement** between the eager and the hybrid engine over the 204-scene corpus, 0 divergent scenes | The AD-25 requirement in miniature. If the two engines can diverge on a legitimate DAG, dual-engine conformance is not a test-suite question but an architectural defect, and it must be found here rather than in Phase 9 |
| T3.5.2 | engine-only state ≤ **32 B/binding** at 11 000 bindings for the shipping candidate | Prototype D recorded 39.3 B/binding for the whole binding. A second set of per-binding arrays competes with that budget directly, and 32 B is roughly the difference between one and two stamp arrays over an 11 000-binding graph |
| T3.5.3 | the state allocated for **models the graph does not run** ≤ **16 B/binding** | If carrying the union costs more than this, the access pattern has to be split per model rather than shared, or the memory is being spent on an engine that never runs |
| T3.5.4 | a double settle costs ≤ **1.10×** the single-engine settle p50 | The second engine must not be superlinear; if it is, the settle is not the place to pay for conformance |
| T3.5.5 | **0 divergences** between interleaved and sequential settles over one store | The condition for excluding one engine at link time (AD-04): no shared mutable engine state |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-dual
./.build-release/prototypes/d3/albor-prototype-d3-dual            # R5
```

---

## R6 — F2.6: equality semantics for the write-compare

### Question

§13.3 requires a write that leaves the value equal not to invalidate a dependent, and does not say what *equal* means.
The harness's current comparison is exact for scalars, **payload identity** for `String` and `TranslatedString`, and
identity-then-content for `Snapshot`. Three consequences follow that the design has not decided: a `NaN` write is never
equal to itself, so a binding that produces `NaN` from `NaN` can never converge; an equal-content string with a fresh
payload is reported as a change; and a tolerant comparison would suppress invalidations a developer expects. Which policy
can the runtime adopt, and what does each cost and break?

### Method

Five policies, applied at the write-compare inside the store, selected by a clearly-named option whose default is the
harness's current comparison so every figure D2 and D3 published stays reproducible:

| policy | `Double` | `String` | `TranslatedString` | `Snapshot` |
| --- | --- | --- | --- | --- |
| `exact` (D2's default) | `==`, so `NaN ≠ NaN` and `-0.0 == 0.0` | payload identity | locale + payload identity | identity, then sample-by-sample content |
| `ieee` | `==`, recorded separately to show the naming is not the difference | payload identity | locale + payload identity | identity, then content |
| `bit_pattern` | 8-byte compare, so `NaN == NaN` and `-0.0 ≠ 0.0` | payload identity | locale + payload identity | identity only |
| `tolerant` | `|a-b| ≤ 1e-9 · max(1,|a|)` | payload identity | locale + payload identity | identity, then content |
| `string_content` | `==` | content | locale + content | identity, then content |

Each policy is measured four ways:

1. **Churn** — 1 000 writes of a value equal to the one already stored, counting invalidations and the evaluations they cause. This is the whole point of the write-compare.
2. **Convergence** — a three-binding chain whose middle binding produces `NaN` from `NaN` (`x / 0.0` on a zero dependency), which under a non-converging policy never reaches a fixpoint and exhausts the 64-pass guard. Passes, `cycle_budget_exhausted`, and the final value are recorded.
3. **Cost** — the comparison alone, timed interleaved, on the realistic payloads: a 24-character string and a 1 024-sample snapshot, plus the scalar path.
4. **Observables** — six semantic checks each policy is judged against: (a) an equal write does not invalidate, (b) a changed write invalidates, (c) a string whose content changed invalidates even when the pointer differs, (d) a locale change invalidates a translated string, (e) a snapshot published with a new payload but identical samples does not invalidate, (f) a snapshot whose samples differ invalidates.

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.6.1 | at least one policy **converges** the `NaN` chain without exhausting the 64-pass budget, and the policies that do not are named | A settle that cannot converge is a hung UI thread. The equality rule is the only thing standing between a `NaN`-producing binding and that hang, so this decides the rule rather than describing it |
| T3.6.2 | the converging policy that is also cheapest at the realistic payload passes **all six observables**; if it fails any, that failure is the threshold | Forces the report to name one viable default instead of a table of trade-offs. A policy that converges but drops a real invalidation is not a default |
| T3.6.3 | a 24-character **string content** comparison ≤ **200 ns** p99 | D2 T1.5's original budget for one non-trivial property, unmodified. If content comparison costs more than the budget for the work it replaces, strings must stay identity-compared and the difference has to be visible in generated code |
| T3.6.4 | a 1 024-sample **snapshot content** comparison ≤ **1 µs** p99 | 1 000 snapshots published in one frame must stay inside 1 ms, or the fast path has to be identity-only and the cost of an equal publish has to be paid elsewhere |
| T3.6.5 | under churn, the policy recommended by T3.6.2 produces **≤ 1 invalidation per 1 000 equal writes** for scalars, strings and snapshots; every other policy's count is recorded | The purpose of the write-compare. A policy that invalidates on an equal write has made every poll of every observable a frame's worth of work |
| T3.6.6 | the recommended policy costs ≤ **1 ns** more than `exact` on a scalar write | A scalar compare must not be charged for the string and snapshot paths it does not take |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-equality
./.build-release/prototypes/d3/albor-prototype-d3-equality        # R6
```

---

## R8 — F2.8: binding replacement and removal semantics, as variants

### Question

§13.4 and §18.1 fix what `ReplaceBinding` **means** — an imperatively overridden property whose declared binding did not
change keeps its override — and Prototype D2 implemented it as a removal plus an addition. Nothing fixes the
**mechanism**, and the mechanism is what a live reload pays on every keystroke and what a virtualized list pays on every
recycled row: does the binding table grow per replacement, does the index accumulate, and does a removed binding leave
its readers evaluating against a value nobody maintains?

### Method

Three replacement variants and three removal variants, over a committed 1 000-cycle reload of one property and a
1 000-cycle add/remove of 1 000 bindings, measured through clearly-named options whose defaults are D2's recorded
behaviour:

| variant | mechanism |
| --- | --- |
| `remove_then_add` (D2's default) | drop the record as a tombstone, add a new record with a fresh id |
| `in_place` | keep the id and the record, swap the function and the dependency vector in place |
| `in_place_id_reuse` | in place, and a freed id is reused by the next addition from a free list |

| variant | mechanism |
| --- | --- |
| `tombstone` (D2's default) | the dead record is retained, its index entries are removed |
| `tombstone_id_reuse` | as above, plus the free list |
| `tombstone_id_reuse_release` | as above, and the store releases the removed property's payload so a recycled row returns its bytes |

Per variant: binding-table bytes and index bytes after 1 000 cycles, replace p99 and remove p99 at 11 000 bindings,
allocations per cycle, and six correctness observables that must hold in **every** variant: (1) after a replacement,
changing the old binding's source evaluates 0 bindings; (2) the new binding's changed dependency set is picked up;
(3) after a removal the target keeps its last value and no binding writes it again; (4) after a property is removed its
readers re-evaluate and read generated initialization, never freed payload; (5) an imperatively overridden property keeps
its override across a replacement (§13.4, §18.1); (6) an animated property keeps its animation across a replacement
(§13.5).

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.8.1 | every variant grows the binding table by ≤ **25 %** over 1 000 replacement cycles | A reload that retains a record per keystroke is an unbounded leak wearing a mechanism's clothes. Above 25 % the variant is disqualified as the default and its growth rate is the finding |
| T3.8.2 | in-place replacement p99 ≤ **2 µs** at 11 000 bindings; the remove-then-add figure is recorded | A reload edits a handful of bindings per keystroke, so each must stay far inside a frame even at full application scale |
| T3.8.3 | index bytes after 1 000 post-seal replacement cycles ≤ the sealed-CSR initial bytes **+ 10 %**; the segmented slack is recorded | The sealed index rebuilds wholesale, so it must not accumulate across rebuilds; the segmented index must not accumulate growth slack either |
| T3.8.4 | all six correctness observables pass in **every** variant, 18 checks | A variant that is cheaper and wrong is not a variant. Check (1) is the one D2 added to close Prototype E's unverified item, and check (5) is the §13.4 override rule |
| T3.8.5 | recorded: bytes per replacement cycle, bytes per removal cycle, and allocations per cycle, per variant | The delta between the mechanisms is the decision input; the pass/fail is T3.8.1 and T3.8.4 |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-mutate
./.build-release/prototypes/d3/albor-prototype-d3-mutate          # R8
```

---

## R9 — F2.9: static cycle rejection with conditional dependencies

### Question

§7 and AD-04c propose promoting Kahn cycle detection from a runtime guard to a **compile-time rejection**. Conditional
dependencies break the premise: a dependency added at runtime — a conditional row, a live-reload patch, a
theme-dependent branch — means the runtime graph is not the declared graph, so a static rejection can be unsound. When
is it sound, what does a runtime guard cost after a mutation, and what does it cost to keep the static check sound by
declaring the union of the dependencies a binding might ever have?

### Method

**Declared corpus.** Twelve topologies with an independent oracle (the shape's known-cyclic flag): five cyclic by
construction — a self-loop, a 2-cycle, a 3-cycle, a cycle behind an acyclic fan-in, a cycle inside a reconvergent graph —
and seven acyclic: a chain, a diamond, a wide diamond, the reconvergent shape, Prototype D's shape, a 1 000-leaf tree,
and a graph with an unbound source inside a would-be cycle. Each is judged twice: by `detect_cycles`, and by whether the
sealed order could place every binding.

**Conditional case.** An acyclic declared graph, then one runtime `add_dependency` that closes a cycle — the conditional
row that binds back to its own parent. Measured: whether the sealed order still claims acyclicity after the mutation, what
the settle does (passes until the guard, `cycle_detected`, `cycle_budget_exhausted`, the value left behind), and the cost
of re-running detection after that one edge at 11 000 bindings.

**Over-declaration.** The static check is sound only if the declared set is a superset of every reachable runtime set. The
union declaration is therefore measured directly: a conditional binding declares both branches, the condition is false
throughout the workload, and the cost of the unused declared edges is counted in invalidations, evaluations and index
bytes against the exact declaration.

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.9.1 | **0 false negatives and 0 false positives** over the 12 topologies, by both `detect_cycles` and the sealed order: 5/5 cyclic rejected, 7/7 acyclic accepted | A rejection pass that misses a cycle ships a hang; one that rejects an acyclic graph rejects the application. Both directions have to be exact before the check can move to build time |
| T3.9.2 | in **100 %** of the conditional-cycle scenarios the settle reports `cycle_budget_exhausted` within the 64-pass guard, and the surviving value is the guard-truncated one | The runtime guard is the backstop a compile-time check cannot be. It has to be observable rather than inferred, and it has to fire rather than settle to a plausible wrong value |
| T3.9.3 | re-running detection after one conditional edge ≤ **2 ms** p99 at 11 000 bindings | This is what decides whether a runtime guard is affordable per mutation batch or only per reload. A guard that costs a frame is a guard nobody calls |
| T3.9.4 | **0 false acyclic claims**: after a mutation that closes a cycle, the sealed order must not report that every binding could be ordered | The soundness condition for the whole proposal. A stale order that still claims acyclicity is exactly how a compile-time rejection becomes unsound at run time |
| T3.9.5 | the union declaration causes ≤ **2×** the invalidations of the exact declaration on the conditional workload, and ≤ **25 %** more index bytes | Over-declaration is the price of static soundness. Above 2× the declared-dependency mechanism has stopped buying anything, and the proposal has to change shape rather than grow a superset |
| T3.9.6 | **no acyclic topology** in the corpus exhausts the 64-pass budget | Distinguishes a cycle defect from a budget defect: if an acyclic graph trips the guard, the guard is measuring the wrong thing |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-cycles
./.build-release/prototypes/d3/albor-prototype-d3-cycles          # R9
```

---

## R10 — F2.10: the read path, interleaved A/B with spread

### Question

Every layout pass and every binding evaluation reads properties, and D2 measured the read path once: 43 ns for a global
id, 44 ns through the base offset, a 2.3 % difference. Two things are unmeasured. Whether an A/B on this machine can
decide a 1 % claim at all — no spread was reported — and what the 43 ns is actually made of, because a typed scalar read
should not cost 43 ns and if the number is a 40-byte `Value` copy then D2 measured the variant, not the read path.

### Method

Five paths, measured **interleaved** in blocks (A, B, C, D, E, A, B, …) so that drift, frequency change and cache state
hit every variant equally, which a sequential A-then-B sweep does not:

| # | path | what it isolates |
| --- | --- | --- |
| A | `typed` scalar read, global dense `PropertyId` | the shipping path |
| B | `erased` scalar read, the same id | what typed storage replaced |
| C | `typed` scalar read through `(instance, local)` | the base-offset indirection AD-04a names |
| D | `typed` read returning `Value` by value | the 40 B variant copy |
| E | `typed` string read | a payload-owning read through a `shared_ptr` copy |

5 runs × 20 000 reads per path per run, in blocks of 2 000, after 2 000 discarded warm-up reads. p50/p95/p99 per run,
the spread reported as (max − min) of the run medians over the mean, and an empty loop that reads a precomputed value as
the measurement's own floor. Both the `scalar` and the `mixed` schema are measured, so the read path is not reported for
one property mix only.

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.10.1 | run-to-run spread of the p50 ≤ **10 %** for every path, in both schemas | Without this the A/B decides nothing: D2's claim is 2.3 %, so a harness whose own spread is 20 % cannot support a 1 % conclusion. This threshold gates the rest of the revision |
| T3.10.2 | typed scalar read p99 ≤ **20 ns**, and ≤ 1.5× the erased scalar read p99 | Typed storage's win is memory; if it is paid for in the read path it is not a win at all, and the read path is touched by every layout pass |
| T3.10.3 | the base-offset indirection costs ≤ **5 %** of the typed read p50 | D2's 2.3 %, restated with the spread bound that D2 did not have. The indirection is the price of component-local ids (AD-04a, Prototype B), so it needs a number that can survive scrutiny |
| T3.10.4 | the scalar fast path is ≥ **2×** cheaper than the `Value`-returning read | Stated as a claim to be tested, not as an expectation: if it fails, D2's 43 ns was not the read path this revision set out to decompose, and the recorded decomposition says so |
| T3.10.5 | recorded: every path at p50/p95/p99 per run, the spread, and the loop floor | The decomposition is the deliverable; the thresholds only keep it honest |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-readpath
./.build-release/prototypes/d3/albor-prototype-d3-readpath        # R10
```

---

## R11 — F2.11: the property mix of a real UI

### Question

Every number AD-04b rests on comes from three hand-written eight-property schemas and a synthetic graph of uniform
dependencies: 8/14/20 B per property, and the 32.2 B/property combined figure from R2. **None of them is a real Albor
UI.** Whether a real application holds more or fewer than R3's 128-property crossover, and whether it is mostly scalars
or mostly text and snapshots, decides whether typed storage is the default at all and whether the 36 B budget was set
against the right mix. This is the last outstanding question on AD-04b.

### Method

**The corpus is authored, and that is stated before any measurement.** This repository contains **no `.ui` file** — the
compiler is Phase 5 (§31) — so there is no shipped UI to measure. Six screens are therefore authored under
`prototypes/d3/corpus/`, in a small line-oriented form carrying component types with their declared property types,
bindings with their declared dependency expressions, and a static node tree with multiplicities. They are chosen to cover
the widget surface the design names in §20–§22 and every §13.1 category: a settings window, a file browser (`ListView`,
delegate, selection, model), a text editor (`TextInput`, IME, model), a dashboard (custom render nodes, snapshots
published from a worker), an inspector panel (tree, property editing) and a modal dialog. The prototype parses the
corpus, and **every conclusion below is conditional on it being representative** — which T3.11.2 tests rather than
assumes.

Extracted per component type and per screen: property count and §13.1 category mix; property instances from the node tree
and its multiplicities; bindings; the distribution, mean and maximum of dependencies per binding; the share of bound
versus constant properties; the environment-property bindings (§13.1 categories 8 and 9).

Then R2's accounting is re-run on the corpus mix rather than on `mixed`: the corpus's own component types are
instantiated in the real stores at **1, 10 and 100 screens**, and typed + sealed CSR, typed + segmented and
erased + sealed CSR are reported in B/property, alongside R3's crossover for the corpus's own value bytes.

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.11.1 | the corpus parses with **0 errors** and every declared binding resolves to a declared property | The strongest available check on authored data. A corpus that would not compile measures nothing, and an unresolved dependency would silently understate the dependency fan-in |
| T3.11.2 | the corpus is **representative** on three committed checks: ≥ 40 % scalar properties, ≥ 5 % payload-owning properties, mean dependencies per binding ≤ 3 | Authored data can be made to say anything. These three bounds are what a UI is — mostly geometry and state, some labels and model data, few dependencies per binding — and a corpus that fails them is marked unrepresentative rather than used |
| T3.11.3 | typed + sealed CSR ≤ erased + sealed CSR on the corpus mix, at the 100-screen scale | R1's storage answer has to survive the real mix, not only the mix that was chosen to contain one of every category |
| T3.11.4 | typed + sealed CSR ≤ **36 B/property** on the corpus mix at the 100-screen scale | R2's budget, re-judged on real data. It was set against `mixed`'s 14 B of value; a cheaper corpus passes with headroom and a dearer one means the budget was set on the wrong mix |
| T3.11.5 | the segmented premium over sealed CSR ≤ **12 B/property** on the corpus mix | R2's T3.2.3 budget on the corpus, because the ratio between the two indexes is a function of the binding-to-property ratio and only the corpus knows that ratio |
| T3.11.6 | **one screen's live properties ≥ 128**, R3's measured crossover | Decides whether the 1 KB first chunk's one-instance overshoot (R3's T3.3.1 failure) can ever be reached by a real window. If a screen is below the crossover, that is the finding and it goes to the human gate with the rest |
| T3.11.7 | recorded: the full corpus table, the per-component breakdown, the binding and dependency distribution, and the corpus mix's value bytes against `mixed`'s 14.00 B | The corpus is the deliverable as much as the verdict is. Phase 2 needs the per-component counts to size anything, and the corpus has to be re-read as the widget set grows |

### Commands

```sh
cmake --build --preset release --target albor-prototype-d3-corpus
./.build-release/prototypes/d3/albor-prototype-d3-corpus          # R11
```

---

### Harness changes these revisions make

Every addition to Prototype D2's harness is a clearly-named, **default-off** option or a read-only accessor, so that every
figure D2, D3 R1, R2, R3 and R7 published stays reproducible with the defaults:

| addition | default | revisions |
| --- | --- | --- |
| `Value::equal_under(policy)` and `Store::set_equality_policy` | `exact`, the current comparison | R6 |
| `Graph::set_binding_reuse`, `replace_binding_in_place` | off, remove-then-add as D2 recorded | R8 |
| `Graph::engine_state_bytes` (accessor) | — | R5 |
| `Graph::reset_run_log` / run logs (accessor) | — | R4's glitch metric |

### Pass/fail policy (all revisions)

Failures stay failures. A threshold that fails because the harness was narrower than its wording is reported as a
failure with the diagnosis, as R1 did. No status of AD-04a, AD-04b, AD-04c or AD-04d changes by measurement alone; the
human gate converts recommendations to decisions. `albor-design-v3.md` is **not** edited by these revisions: results land
in this directory's `RESULTS.md` and the human gate applies them.