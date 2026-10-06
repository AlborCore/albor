# Prototype D3 — R1: Typed Storage Accounting

Follow-up to Prototype D2, addressing **AD-04b** (property storage layout). Plan committed in
[`plan.md`](plan.md) (`5fcd780`) before this program existed (§28.4).

This is measurement code. It is not a runtime module and must not be linked by one (AGENTS.md).

**Hardware / build:** Intel Core i7-9750H @ 2.60 GHz, macOS 26.6.2, Apple Clang, C++23, libc++,
`cmake --preset release` (`-O3`). Memory is deterministic, so these are single figures with no spread, and there is no
timing in R1.

**Verdict: 11 passed, 6 failed.** The six failures are the most important result in this revision.

## What R1 was asked to reconcile

AD-04b stated three things that could not all describe the same number:

- 8.00 B per scalar property,
- 7.00 B of addressability *per property*,
- and a win that "shrinks to about 2.3× for scalars".

If the 7 B is additional, a scalar is about 15 B against a 64 B slot — **4.3×**, not 2.3×. The 2.3× traces to
`prototypes/d2/RESULTS.md:279` and is close to what a string-heavy mix yields, so the figure was real but attributed to
the wrong mix. Separately, D2's reported **128.00 B** per property for the erased slot was a measurement bug: its
benchmark gave both candidates the **same `RegionTable`**, so the erased store grew to 16 slots and was then divided by 8.

## The table

Bytes per property, one live instance of an eight-property component, each candidate on its own region table:

| Mix | candidate | value | addressability | slack | region overhead | **total** |
| --- | --- | --- | --- | --- | --- | --- |
| `scalar` (8 × `Int`) | typed | 8.00 | 7.00 | **8 184.00** | 4.50 | **8 203.50** |
| | erased | 64.00 | 0.00 | 0.00 | 4.00 | **68.00** |
| `mixed` (one of every §13.1 category) | typed | 14.00 | 7.00 | **8 178.00** | 4.50 | **8 203.50** |
| | erased | 64.00 | 0.00 | 0.00 | 4.00 | **68.00** |
| `string_heavy` (5 `String`, 3 `TranslatedString`) | typed | 20.00 | 7.00 | **8 172.00** | 4.50 | **8 203.50** |
| | erased | 64.00 | 0.00 | 0.00 | 4.00 | **68.00** |

Measured type sizes: `sizeof(Value variant)` **40 B**, `SnapshotBox` **32 B**, `TranslatedString` **24 B**,
`sizeof(Region)` **32 B**, slab chunk **65 536 B**. **80 % of the erased variant is `SnapshotBox` alone** — the erased
slot pays for its largest alternative on every property regardless of what the property holds.

## Verdicts

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.1.1 | typed total ≤ erased total, every mix | 8 203.50 B vs 68.00 B, all three mixes | **FAIL** ×3 |
| T3.1.2 | typed `scalar` ≤ 24 B/property | 8 203.50 B | **FAIL** |
| T3.1.3 | typed `mixed` ≤ 32 B/property | 8 203.50 B | **FAIL** |
| T3.1.4 | typed `string_heavy` ≤ 64 B/property | 8 203.50 B | **FAIL** |
| T3.1.5a | typed value bytes == sum of live `region_bytes` | 0.0000 B error, all three mixes | **PASS** ×3 |
| T3.1.5b | typed addressability == 7 B × properties | 0.0000 B error, all three mixes | **PASS** ×3 |
| T3.1.5c | erased slot bytes == `sizeof(slot)` × properties | 0.0000 B error, all three mixes | **PASS** ×3 |
| T3.1.6 | erased bytes/property identical across all mixes | **0.0000 B spread** (68.00 everywhere) | **PASS** |
| T3.1.7 | typed slab slack ≤ 25 % of allocated, at 4 096 instances | **12.50 %** (65 536 B of 524 288 B) | **PASS** |
| T3.1.8 | erased ÷ typed ratio per mix | 0.01× / 0.01× / 0.01× at one instance | RECORDED |
| T3.1.9 | crossover instance count | **256 instances = 2 048 properties** | RECORDED |

### Two thresholds could not be measured as originally worded

**T3.1.5 was tautological as written.** `plan.md` worded it as "the four columns sum to the reported total", but the
program *defines* total as that sum, so it could not fail. It is measured here against figures predicted independently
from the schema table and the slot size, which can fail — and it does so as three separate checks. **The plan's wording
should be corrected**, and the correction is recorded here rather than applied silently to the committed plan.

**T3.1.6 was pre-registered with a spread bound but the first harness only recorded it per mix.** It is now judged once
across all three mixes. The first run of the harness did not evaluate a threshold it had committed to; that is recorded
rather than quietly fixed.

## Findings

### 1. The 64 KB slab chunk dominates typed storage below ~2 000 properties

The failures are not caused by the value layout. Value bytes are **8.00 / 14.00 / 20.00 B** — small, and confirmed to
0.0000 B against the schema table's own prediction. They are caused by the **slack column**: typed storage allocates a
whole 64 KB chunk to hold 64 bytes of values, so one eight-property instance costs **65 628 B** against the erased
store's **544 B**.

**At one instance, typed storage is 120× worse than the erased slot.** Every absolute threshold in the plan was written
against D2's figures, which excluded slack entirely, so all four failed. They are recorded as failures.

The crossover is measured, not assumed:

| instances | properties | typed total | erased total | erased ÷ typed |
| --- | --- | --- | --- | --- |
| 1 | 8 | 65 628 | 544 | 0.01× |
| 64 | 512 | 71 424 | 34 816 | 0.49× |
| 128 | 1 024 | 77 312 | 69 632 | 0.90× |
| **256** | **2 048** | **89 088** | **139 264** | **1.56×** |
| 512 | 4 096 | 112 640 | 278 528 | 2.47× |
| 4 096 | 32 768 | 901 120 | 2 228 224 | 2.47× |
| 8 192 | 65 536 | 1 736 704 | 4 456 448 | 2.57× |

**Below about 2 000 live properties, typed storage costs more than the erased slot; above it, it is 2.5× cheaper.** The
asymptotic ratio is 2.47× for the mixed schema (68 ÷ 27.5), not the 2.3× AD-04b published, and not the 4.3× the
arithmetic alone implied — the difference is the addressability column.

### 2. The erased slot is one number, and D2's 128.00 B was a bug

**68.00 B per property for every mix, spread 0.0000 B** (T3.1.6). The slot is a uniform variant, so this is the only
possible answer, and D2's 128.00 B came from dividing a 16-slot store by 8 properties. **Prototype D2's published
erased figure is corrected to 68.00 B**, which is 4 B above D's recorded 56.00 B because D2's slot adds
`TranslatedString` and a binding-attachment byte.

### 3. Addressability is the real tax, and it is flat

**7.00 B per property, on every mix** — 4 B location, 1 B type, 1 B write source, 1 B binding attachment. Against 8 B
of scalar value that nearly doubles the cost. This is the quantity that makes the asymptotic win 2.47× rather than the
8× the value column alone suggests, and it is the strongest argument for an `(instance, local)`-addressed index that
would remove the 4 B location entry.

### 4. Chunking for stable addresses is cheap at scale and ruinous at small scale

Slack is **12.50 %** of allocated at 4 096 instances (T3.1.7, **PASS**) — the amortisation works as intended. The
problem is the fixed 64 KB quantum at small instance counts, not the mechanism. **The chunk size is a tunable constant,
not a design constraint**, and geometric growth starting much smaller would move the crossover down by orders of
magnitude. That mitigation is **not** measured here and is named in F2.3.

## What this does not show

1. **No `.ui` corpus.** Whether a real application has more or fewer than 2 000 live properties decides whether the
   crossover matters at all. This is the single question that determines whether AD-04b's recommendation is safe.
2. **No index memory.** The 7 B addressability and the slab costs are storage only; D3 F2.2 adds the index at D's scale.
3. **No timing.** D2's two failed thresholds are untouched (F2.3).
4. **One mix crossover.** Only the `mixed` schema was swept for the crossover; `string_heavy` crosses earlier and
   `scalar` later, because the value column differs and the chunk does not.
5. **Heap payloads excluded.** String and snapshot payloads are not in this table, so a text-heavy application is
   understated on both sides equally.

## Corrections applied to earlier claims

Per the close-out brief's rule that a contradicting result corrects the document in the same change:

| Claim | Where | Correction |
| --- | --- | --- |
| erased slot is 128.00 B/property on the mixed case | `prototypes/d2/RESULTS.md`, AD-04b | **68.00 B**, uniform across mixes; D2's figure came from a shared `RegionTable` |
| typed win "shrinks to about 2.3× for scalars" | AD-04b, `prototypes/d2/RESULTS.md` | **2.3× does not correspond to the scalar mix.** It is close to the asymptotic `mixed` figure of **2.47×**; the scalar figure is **not separately valid** because at one instance slack dominates |
| typed storage is the recommended default | AD-04b | **Downgraded to conditional.** It is 120× worse below ~2 000 live properties and 2.5× better above, so the recommendation is unsafe as an unconditional default |
| D2 T1.3 "typed ≤ erased, same mix" | `prototypes/d2/RESULTS.md` | The threshold **passed in D2 only because D2's erased figure was 128 B, itself inflated by the shared-region-table bug.** At the correct 68.00 B the comparison is 8 203.50 vs 68.00 and typed **loses** at one instance |

## Recommendation (not a decision)

**AD-04b: the recommendation must become conditional on slab policy, and the pre-registered thresholds stand failed.**

Typed per-component storage is the right representation — 8 to 20 B of value against a 64 B uniform slot, with 80 % of
that slot spent on an alternative most properties never hold. But **the shipping default cannot be adopted until the
slab growth policy is fixed**, because as measured it is 120× worse for any application with fewer than about 2 000 live
properties, which includes every small tool and every test.

Recommended next steps, in order:

1. **F2.3**: replace the fixed 64 KB chunk with geometric growth from a small first chunk, and re-measure the crossover.
   Expected to move it from 2 048 properties to the low hundreds. *Measured, not assumed.*
2. **F2.11**: count the live properties of a real application. If real applications sit above the crossover, the slab
   policy is a tuning matter; if below it, the storage recommendation changes shape.
3. **F2.2**: add index memory at D's scale, where the segmented index may consume the entire storage saving.

**AD-04b remains Open.** No status is changed by this revision.

---

# R2 — F2.2: Combined storage × index memory at scale

Plan section appended and committed (`fdd0d39`) before this program existed (§28.4).

**Hardware / build:** same as R1. Memory is deterministic: single figures, no spread, no timing.

**Verdict: 4 passed, 1 failed.**

## The combined table

100 000 live properties (12 500 eight-property `mixed` instances), 11 000 bindings × 2 dependencies (22 000 entries),
plus the committed post-seal activity of 1 000 `add_entry` calls:

| storage | index | storage B | index B | combined B | combined B/property |
| --- | --- | --- | --- | --- | --- |
| typed | sealed_csr | 2 731 616 | 492 004 | 3 223 620 | **32.2** |
| typed | segmented | 2 731 616 | 1 396 608 | 4 128 224 | 41.3 |
| typed | hybrid | 2 731 616 | 1 667 236 | 4 398 852 | 44.0 |
| erased | sealed_csr | 8 912 896 | 492 004 | 9 404 900 | 94.0 |
| erased | segmented | 8 912 896 | 1 396 608 | 10 309 504 | 103.1 |
| erased | hybrid | 8 912 896 | 1 667 236 | 10 580 132 | 105.8 |

## Verdicts

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.2.1 | typed+CSR ≤ erased+CSR combined, at 100k properties | 32.2 vs 94.0 B/property | **PASS** |
| T3.2.2 | typed+CSR ≤ 36 B/property (`mixed`) | 32.2 B/property | **PASS** |
| T3.2.3 | segmented adds ≤ 12 B/property over CSR | 9.0 B/property | **PASS** |
| T3.2.4 | hybrid adds ≤ 6 B/property over CSR | 11.8 B/property | **FAIL** |
| T3.2.5 | CSR index bytes == independent prediction | 0.00 B error (492 004 both) | **PASS** |

## Findings

1. **The index does not invert R1's storage answer — it preserves it.** Typed storage plus the cheapest index costs
   32.2 B/property against the erased store's 94.0 B/property on the same index. The asymptotic storage saving
   survives the index at scale, so AD-04b's condition is now "conditional on slab policy" (R3 answers the policy).
2. **The first hybrid is over-priced, and the diagnosis names why.** 11.8 B/property for 1 000 tail entries: the
   reused `SegmentedIndex` charges a **dense segment table** — `sizeof(Segment)` = 12 B for all 100 000 properties,
   1.2 MB — to serve entries on 1 000 of them, plus upfront 4 096-entry arena blocks. The minimal tail needs a sparse
   record keyed by touched property, not a dense table. The threshold fails as committed; the shape of the fix is
   measured by the two halves' byte accounting, not assumed.
3. **The CSR offsets table is a per-property tax independent of bindings.** (property count + 1) × 4 B regardless of
   how many bindings read a property. At 10 000 properties that is 40 KB against ~88 KB of entries — the offsets
   dominate at small scale. This is the strongest measured argument for an `(instance, local)`-addressed index,
   which would charge the 4 B location entry to the schema, not to a global table.

## What this does not show

No timing (the index's rebuild cost is D2 T4.4's evidence, unchanged), no `.ui` corpus (F2.11), and no second index
shape implemented beyond the three committed.

---

# R3 — F2.3: Geometric slab growth and labelled timing variants

Plan section appended and committed (`fdd0d39`) before this program existed (§28.4).

**Hardware / build:** same as R1. Part A deterministic memory; part B timing, 5 runs of 5 000 samples each.

**Verdict: 4 passed, 1 failed.** The one failure is the committed policy's remaining small-scale overshoot, diagnosed
below; the crossover itself moved by 16×.

## Part A — the crossover sweep, mixed schema

| instances | properties | typed B/property | erased B/property | erased ÷ typed |
| --- | --- | --- | --- | --- |
| 1 | 8 | 139.5 | 68.0 | 0.49× |
| 16 | 128 | 35.5 | 68.0 | 1.92× |
| 32 | 256 | 39.5 | 68.0 | 1.72× |
| 64 | 512 | 41.5 | 68.0 | 1.64× |
| 128 | 1 024 | 26.5 | 68.0 | 2.57× |
| 256 | 2 048 | 27.0 | 68.0 | 2.52× |
| 512 | 4 096 | 27.2 | 68.0 | 2.50× |
| 1 024 | 8 192 | 27.4 | 68.0 | 2.48× |
| 4 096 | 32 768 | 27.5 | 68.0 | 2.48× |
| 8 192 | 65 536 | 26.5 | 68.0 | 2.57× |

## Verdicts — Part A

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.3.1 | typed ≤ erased at 1 instance | 1 116 B vs 544 B (139.5 vs 68.0 B/property) | **FAIL** |
| T3.3.2 | crossover ≤ 64 instances | **16 instances = 128 properties** | **PASS** |
| T3.3.3 | slack ≤ 25 % at 4 096 instances | 12.3 % (64 512 B of 523 264 B) | **PASS** |
| T3.3.4 | address stability, 100 % of sentinels | tested in `D3Engine.GeometricSlabKeepsAddressesStableAcrossGrowth` | **PASS** (see Tests) |
| T3.3.5 | ratio at 1 instance | 0.49× | RECORDED |

**T3.3.1's failure, diagnosed.** The committed policy's first chunk is 1 KB; one eight-property instance occupies
~50 B, so a one-instance store pays ~974 B of slack (≈128 B/property) and loses to the erased store by 2×. **The 120×
blowup R1 measured is gone** — the failure is now bounded by the first-chunk size, not by the store's design. A first
chunk of ~256 B would fit one instance with no overshoot; that is a policy parameter to tune against a real corpus
(F2.11), not a design change, and it is not measured here because the brief fixed the 1 KB start.

## Part B — the labelled timing variants, D2's original budgets

| variant | result | un-mitigated | verdict |
| --- | --- | --- | --- |
| `settle-plan-reuse` | 188.0 ns p99 (median of 5 runs; p50 median 170.0 ns) | 311.0 ns p99 | **PASS** — meets D2 T1.6's original ≤ 250 ns budget |
| `shared-immutable-payload` | 67.0 ns p99 (median of 5 runs; p50 median 64.0 ns) | 315.0 ns p99 | **PASS** — meets D2 T1.5's original ≤ 200 ns budget |

Both mitigations land under the budgets D2 published, and both were diagnosed to the mechanism before measuring: the
settle's fixed cost was the per-settle construction of two vectors; the payload tax was one allocation per property
for an immutable shared object. The original D2 thresholds remain failed; these are labelled variants, recorded with
their deltas.

---

# R7 — F2.7: Animation shadow audit

Plan section appended and committed (`fdd0d39`) before this program existed (§28.4).

**Hardware / build:** same as R1.

**Verdict: 6 passed, 0 failed.**

## The three modes, measured while every animation is active

| mode | record kept | B/animated property (1 024 properties) |
| --- | --- | --- |
| full shadow (D2's path) | property id + binding value + payload | 64.0 |
| active-set (proposed minimal) | property id only | 4.0 |
| none (naive elimination) | nothing | 0.0 |

## Verdicts

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.7.1a | active-set: animation ends at the fresh binding value | final = 16 (source was 15) | **PASS** |
| T3.7.1b | active-set: dependency move mid-animation is picked up | final = 56 (source moved to 55) | **PASS** |
| T3.7.1c | active-set: imperative write during animation wins | final = 1234, binding broken | **PASS** |
| T3.7.1d | active-set: rebind after animation re-evaluates | final = 11 | **PASS** |
| T3.7.3 | none mode: zero shadow records observable | 0 records after all scenarios | **PASS** |
| T3.7.2 | active-set record ≤ 8 B/property | 4.0 B | **PASS** |

## The audit's answer

**Invalidation alone is not sufficient, and the audit names the exact break.** In the none mode, `end_animation`
cannot tell whether an animation ever owned the target: it always rebinds and replans, which **resurrects a binding
that an imperative write had broken** and overwrites the user's override (measured: the imperative override came back
as 11, the bound value, with the attachment restored). The full shadow map currently carries that "animation owns
this" record as a side effect of carrying the value.

**The minimal necessary mechanism is presence without a value**: the active-set mode keeps one sorted record of
animated targets — 4.0 B/animated property against the full map's 64.0 B — and passes every observable check,
including the imperative override the naive elimination clobbers. The post-animation re-evaluation covers the value
semantics (the binding recomputes and overwrites whatever the animation last wrote); only the presence record has no
re-evaluation equivalent.

## What this does not show

The active-set record was measured over dense `Int` targets only; an app that animates sparse or heterogeneous
targets changes the record's bytes but not the mechanism. The suite covers D2's observable checks, not new ones.

---

# Corrections and tests added in R2/R3/R7

- `tests/d3/binding_engine.test.cpp` (3 tests): address stability across growth, uniform-chunk parity of the
  default policy, and the imperative-override split between the naive elimination and the active-set mechanism.
  88 of 88 tests pass in the whole suite.
- Prototype D2's harness gained two clearly-named, default-off options (`set_slab_growth`,
  `set_reuse_settle_buffers`, `set_animation_shadowing`) plus two measurement accessors
  (`storage_address_of`, `shadow_entry_count`); every figure D2 published remains reproducible with the defaults.

---

# R4, R5, R6, R8, R9, R10 and R11 — the seven remaining property questions

Plan sections appended and committed (`507454e`) **before** any of these programs existed (§28.4). `albor-design-v3.md` is
**not** edited by this revision: the findings below are for the human gate, which applies them.

**Hardware / build:** Intel Core i7-9750H @ 2.60 GHz, macOS 26.6.2, Apple Clang, C++23, libc++, `cmake --preset release`
(`-O3`). The machine's load average was **2.0–3.6** throughout, which R10 measures rather than assumes and which is the
reason its spread thresholds fail.

**Verdict: 21 passed, 22 failed.** The failures are the substance here. Six of them are **defects in the harness engine
that no choice of index, model or storage can fix**, and they are load-bearing for Phase 2:

| # | Finding | Where |
| --- | --- | --- |
| 1 | **The hybrid push-pull model settles a reconvergent consumer on a value computed from a dependency that had not yet been recomputed in that settle.** Its `ensure_computed` pulls only dependencies that are *already* dirty, so a consumer reached before a deeper link recomputes keeps the stale value and its memo epoch forbids the correction | R4 T3.4.2, R5 T3.5.1 |
| 2 | **Only 140 of 204 legitimate DAGs reach the same values under both engines** (137 of 200 pseudo-random), so AD-25's dual-engine equivalence does not hold today | R5 T3.5.1 |
| 3 | **`add_binding` after seal never schedules the new binding**, so a recycled row's binding is not evaluated until one of its dependencies changes | R8 T3.8.4 |
| 4 | **`ReplaceBinding` on a property an animation owns stamps `Imperative` over the animation**, because `has_binding` is true only for `WriteSource::Binding`. §13.4 exempts *imperative* overrides; an animation is a different source, and the two are conflated | R8 T3.8.4 |
| 5 | **The declared-graph cycle check skips self-edges**, so a self-referential binding passes a static rejection and only trips the 64-pass guard at run time | R9 T3.9.1 |
| 6 | **The sealed CSR index cannot absorb a property created after `finish()`** — its rebuild indexes by a property count fixed at seal, so a post-seal property id is out of range. A virtualized list that materialises a row after seal cannot use it | R8 (harness finding) |

## What the seven revisions measured

| revision | question | passed | failed | the answer |
| --- | --- | --- | --- | --- |
| **R4** (F2.4) | index × evaluation factorial | 4 | 2 | **The two axes are separable**: the index moves settle p50 by **1.18 %** and the interaction term is **6.96 %**. AD-04a's index and AD-04c's model can be decided separately. The eager model publishes **1 superseded value per settle** on the reconvergent topology; the hybrid publishes none and is **wrong** instead |
| **R5** (F2.5) | cost of two engines | 1 | 4 | **45.3 % of engine state pays for engines that never run** (34.39 B/binding of a 75.85 B/binding union). Two engines coexist over one store with **no shared mutable state**, so link-time exclusion is viable; but they do not agree on 31 % of DAGs |
| **R6** (F2.6) | equality semantics | 3 | 3 | **Identity comparison of a string invalidates on every equal-content write** (1 000 of 1 000). A 1 024-sample snapshot content comparison costs **1 450 ns p99**, over the 1 µs budget. `NaN` does **not** threaten the pass guard in an acyclic chain |
| **R8** (F2.8) | replacement and removal | 1 | 3 | **`remove_then_add` grows the binding table by 100 000 % over 1 000 reload cycles** (41 B and 2 allocations per cycle). In-place replacement is flat, but on the sealed index it costs **143 µs** at 11 000 bindings because every post-seal edit is a full index rebuild |
| **R9** (F2.9) | conditional-cycle rejection | 4 | 2 | A runtime guard is affordable (**0.96 ms p99 at 11 000 bindings**), and clearing the order on mutation prevents a false acyclic claim. **Declaring the union of possible dependencies costs 7.71× the invalidations**, so static soundness cannot be bought that way |
| **R10** (F2.10) | read path | 1 | 7 | **D2's 43 ns read was the 40 B `Value` copy, not the read.** The scalar fast path is **6.59 ns**; the base-offset indirection costs **~3 ns of extra random access**, not 2.3 %. The machine cannot support a 1 % A/B (spread 20–72 %) |
| **R11** (F2.11) | real UI property mix | 7 | 1 | The authored corpus is **71.8 % scalar, 28.2 % payload-owning, 1.19 dependencies per binding**, and **9.75 B of value per property against `mixed`'s 14.00 B**. Typed + sealed CSR is **25.3 B/property**, inside R2's 36 B budget. A modal dialog is **124 properties**, just under R3's 128-property crossover |

## R4 — the index × evaluation factorial (F2.4, AD-04a, AD-04c)

2 indexes × 3 models × 4 topologies, 200 measured settles each, all six cells driven identically.

| index | model | topology | p50 µs | p99 µs | passes/settle | evaluated/settle | redundant/settle | **glitches/settle** | index B |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sealed_csr | eager_push | chain16 | 1.19 | 1.31 | 16.0 | 16.0 | 0.0 | 0.00 | 136 |
| sealed_csr | hybrid_push_pull | chain16 | 1.98 | 31.74 | 1.0 | 16.0 | 0.0 | 0.00 | 136 |
| sealed_csr | pull_topo | chain16 | 1.80 | 1.84 | 1.0 | 16.0 | 0.0 | 0.00 | 136 |
| segmented | eager_push | chain16 | 1.22 | 1.32 | 16.0 | 16.0 | 0.0 | 0.00 | 16 588 |
| sealed_csr | eager_push | diamond32 | 2.86 | 2.94 | 2.0 | 33.0 | 0.0 | 0.00 | 396 |
| sealed_csr | hybrid_push_pull | diamond32 | 3.10 | 3.19 | 1.0 | 33.0 | 0.0 | 0.00 | 396 |
| sealed_csr | eager_push | reconvergent | 1.88 | 1.94 | 9.0 | 18.0 | 1.0 | **1.00** | 176 |
| sealed_csr | hybrid_push_pull | reconvergent | 2.16 | 2.21 | 1.0 | 17.0 | 0.0 | 0.00 | 176 |
| sealed_csr | pull_topo | reconvergent | 2.24 | 2.29 | 1.0 | 17.0 | 0.0 | 0.00 | 176 |
| sealed_csr | eager_push | d_shape | 10.83 | 54.41 | 9.0 | 176.0 | 0.0 | 0.00 | 1 480 |
| sealed_csr | hybrid_push_pull | d_shape | 14.21 | 30.56 | 1.0 | 176.0 | 0.0 | 0.00 | 1 480 |
| sealed_csr | pull_topo | d_shape | 18.77 | 49.08 | 1.0 | 176.0 | 0.0 | 0.00 | 1 480 |

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.4.1 | 0 glitches at `diamond32` | 0 in all six cells over 200 settles | **PASS** |
| T3.4.2 | every cell reaches identical final values | 52/60; `reconvergent` property 17: eager 3609, hybrid 3608 | **FAIL** |
| T3.4.3 | index axis moves settle p50 ≤ 10 % | **1.18 %** (1.95 % eager, 0.42 % hybrid) | **PASS** |
| T3.4.4 | interaction ≤ 10 % of the larger main effect | **6.96 %** (index 1.18 %, model 22.05 %) | **PASS** |
| T3.4.5 | segmented `add_dependency` p99 ≤ 1 µs, sealed ≤ 50× | segmented **0.120 µs**, sealed **89.23 µs**, ratio **744×** | **FAIL** |
| T3.4.6 | no cell exhausts the 64-pass budget | 0 of 24 cells | **PASS** |
| T3.4.7 | recorded | see the table and the mutation row below | RECORDED |

**The axes are separable, and that is the answer AD-04a was waiting for.** The index changes evaluation cost by 1.18 %
and the interaction is inside the noise, so the index can be chosen on **memory and mutation locality alone** — which is
exactly the choice D2's 8.8× figure could not settle because it was measured on one index's memory at one scale.

**T3.4.5 fails in the direction that strengthens the segmented index.** D2 measured a 155× ratio at a couple of hundred
bindings; at 11 000 bindings it is **744×**, because a local add is O(1) and a rebuild is O(entries). The committed 50×
bound assumed the ratio would shrink with scale. It grows. An application that edits a dependency at run time — a
conditional row, a live-reload patch (§18.1) — pays 89 µs per edit on the sealed index.

**The two models fail differently, and that is the finding.** On the reconvergent topology the eager model publishes a
value it later supersedes (**1 glitch per settle**, and the right final value); the hybrid model publishes **no** transient
value and the **wrong** final value, one propagation step behind. The mechanism is in
`Graph::ensure_computed`: it pulls a dependency only when that dependency is *already* dirty, so a consumer reached
before a deeper link has recomputed keeps the stale value, and the memo epoch then forbids the correction.

## R5 — the cost of carrying two evaluation engines (F2.5, AD-25, AD-04)

204 scenes: the four committed topologies plus 200 pseudo-random DAGs (depth ≤ 6, fan-out ≤ 4, fixed seed), each built
twice and driven with the same write sequence.

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.5.1 | 100 % final-value agreement over the corpus | **140/204** (137/200 random); first divergence `reconvergent` node 17: eager 6256, hybrid 4464 | **FAIL** |
| T3.5.2 | eager state ≤ 32 B/binding at 11 000 bindings | **41.45 B/binding** | **FAIL** |
| T3.5.3 | state for a model never run ≤ 16 B/binding | **34.39 B/binding** | **FAIL** |
| T3.5.4 | double settle ≤ 1.10× the single settle | 0.12 µs vs 0.09 µs, **1.307×** | **FAIL** |
| T3.5.5 | 0 divergences between interleaved and sequential settles | 192 properties, 8 rounds, one shared store | **PASS** |

**Engine state at 11 000 bindings and 12 000 properties:**

| configuration | bytes | B/binding |
| --- | --- | --- |
| the union the graph allocates | 834 304 | **75.85** |
| `eager_push`'s own subset | 456 000 | 41.45 |
| `hybrid_push_pull`'s own subset | 555 000 | 50.45 |
| `pull_topo`'s own subset | 588 000 | 53.45 |
| **state for a model never run** | 378 304 | **34.39** |

1. **AD-25's equivalence requirement does not hold today.** 64 of 204 legitimate DAGs settle to different values under
   the two engines. The cause is R4's finding, reproduced here on random shapes: the hybrid model's pull is incomplete,
   so on any graph where a consumer is reachable at two depths it can end a settle one step behind. **A conformance suite
   would catch this; nothing in a single-engine test would.**
2. **45.3 % of engine state pays for engines that never run.** `Graph` allocates the memo epoch, the in-progress marks,
   the Kahn order and its rank whatever model it runs. That is the price of carrying two engines *inside one runtime
   module*, and it is avoidable by splitting the access pattern per model — which AD-04's link-time exclusion permits
   only if the state is split too.
3. **T3.5.4 fails as worded, and the measurement says why.** The second engine adds **0.03 µs** per frame on a 32-wide
   fan-in — 1.31× a single settle of 0.09 µs — because the write-compare suppresses the second engine's writes. The
   committed 1.10× bound was not sized against a measured settle; the incremental cost, not the ratio, is the finding.
4. **T3.5.5 passes, which is the condition AD-04 needs.** Two graphs over one store, driven in lockstep, reach the same
   values as two stores driven in turn. There is no shared mutable engine state, so one engine can be a separately
   linked translation unit and excluded from Release at link time rather than at runtime.

## R6 — equality semantics for the write-compare (F2.6, AD-04b, §13.3)

| policy | scalar ns | string ns | snapshot ns | (a) quiet | (b) loud | (c) string | (d) locale | (e) snapshot | (f) changed |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `exact` (D2's default) | 2.78 | 19.37 | 1 022.23 | pass | pass | pass | pass | pass | pass |
| `ieee` | 2.78 | 19.36 | 1 002.08 | pass | pass | pass | pass | pass | pass |
| `bit_pattern` | 2.91 | 19.36 | **20.79** | pass | pass | pass | pass | **FAIL** | pass |
| `tolerant` | 3.39 | 19.36 | 998.66 | pass | pass | pass | pass | pass | pass |
| `string_content` | 2.90 | **29.87** | 1 022.86 | pass | pass | pass | pass | pass | pass |

**Invalidations from 1 000 writes of a fresh but equal payload:**

| policy | scalar | string | snapshot |
| --- | --- | --- | --- |
| `exact` | 0 | **1 000** | 0 |
| `ieee` | 0 | **1 000** | 0 |
| `bit_pattern` | 0 | **1 000** | **1 000** |
| `tolerant` | 0 | **1 000** | 0 |
| `string_content` | 0 | **0** | 0 |

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.6.1 | at least one policy converges the `NaN` chain | all five converge, 2–3 passes, guard never hit | **PASS** |
| T3.6.2 | the converging policy that is cheapest passes all six observables | the cheapest is `bit_pattern`, which **fails (e)** | **FAIL** |
| T3.6.3 | 24-char string content comparison p99 ≤ 200 ns | **58.54 ns** | **PASS** |
| T3.6.4 | 1 024-sample snapshot content comparison p99 ≤ 1 000 ns | **1 449.76 ns** | **FAIL** |
| T3.6.5 | recommended policy ≤ 1 invalidation per 1 000 equal writes | 0 scalar, **1 000 string, 1 000 snapshot** | **FAIL** |
| T3.6.6 | recommended policy ≤ 1 ns dearer than `exact` on a scalar | +0.129 ns | **PASS** |

1. **The single largest churn source in the engine is string identity.** Every policy except `string_content` reports
   every equal-content write as a change: 1 000 invalidations per 1 000 writes of the same 24-character label. Any
   observable polled once per frame — a locale, a theme, a selection — pays a full dependent re-evaluation every frame.
2. **`bit_pattern` is cheap because it is wrong, and the threshold catches it.** It is the cheapest converging policy at
   the realistic payload (20.79 ns against ~1 000 ns) *because* it compares snapshots by identity only, which fails the
   "an equal snapshot does not invalidate" observable and adds 1 000 snapshot invalidations per 1 000 equal publishes.
3. **A 1 024-sample snapshot cannot be content-compared on every write.** At 1 450 ns p99, a frame that publishes 1 000
   snapshots spends 1.45 ms in the write-compare alone, over the committed 1 µs per compare. Snapshot equality has to
   stay identity-first with an explicit "same payload" fast path, and content comparison must be opt-in.
4. **`NaN` is not the hazard the threshold expected.** All five policies converge an acyclic `NaN` chain in 2–3 passes and
   never trip the 64-pass guard; the eager model's per-settle dedup, not the equality rule, is what bounds the work. The
   only double edge that changes a settle's work is `0.0` against `-0.0`, which `bit_pattern` reports as a change and
   which costs one extra binding evaluation per drive.
5. **The viable default is `string_content`, and the choice among the four survivors is otherwise a coin flip**: their
   snapshot comparison costs differ by 0.06 % (998.66–1 022.86 ns).

## R8 — replacement and removal semantics (F2.8, AD-04c, §18.1)

**1 000 replacement cycles on one bound property:**

| mechanism | index | records after | growth | binding B | index B | allocs | p99 µs |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `remove_then_add` (D2's) | sealed_csr | 1 001 | **100 000 %** | 41 049 | 16 | 2 072 | 2.00 |
| `remove_then_add` | segmented | 1 001 | **100 000 %** | 41 049 | 16 408 | 2 072 | 1.46 |
| `in_place` | sealed_csr | 1 | 0 % | 49 | 16 | 1 012 | 0.25 |
| `in_place` | segmented | 1 | 0 % | 49 | 16 408 | 1 012 | 0.19 |
| `in_place_id_reuse` | sealed_csr | 1 | 0 % | 49 | 16 | 1 012 | 0.27 |
| `in_place_id_reuse` | segmented | 1 | 0 % | 49 | 16 408 | 1 012 | 0.22 |

**1 000 add and remove cycles of 100 bindings** (a pre-seal pool, recycled, because a sealed index cannot bucket a
property created after seal):

| mechanism | index | records after | growth | binding B | index B | allocs | p99 µs/cycle |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `tombstone` (D2's) | sealed_csr | 100 100 | **100 000 %** | 4 105 124 | 1 204 | 200 061 | 76.80 |
| `tombstone` | segmented | 100 100 | **100 000 %** | 4 105 124 | 18 784 | 200 061 | 1.27 |
| `tombstone_id_reuse` | sealed_csr | 100 | 0 % | 5 124 | 1 204 | 200 009 | 50.31 |
| `tombstone_id_reuse` | segmented | 100 | 0 % | 5 124 | 18 784 | 200 009 | 1.23 |
| `tombstone_id_reuse_release` | segmented | 100 | 0 % | 5 124 | 18 784 | 200 009 | 3.27 |

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.8.1 | binding table grows ≤ 25 % over 1 000 cycles | **`remove_then_add` grows 100 000 %** | **FAIL** |
| T3.8.2 | in-place replacement p99 ≤ 2 µs at 11 000 bindings | sealed **143.01 µs**, segmented **0.40 µs** | **FAIL** |
| T3.8.3 | sealed index bytes ≤ initial + 10 % | 0.00 % growth | **PASS** |
| T3.8.4 | all six observables pass in every variant | **12/18** | **FAIL** |
| T3.8.5 | recorded per-cycle cost | 41 B and 2 allocs per replace cycle; 4 100 B per removal cycle under `tombstone` | RECORDED |

1. **D2's recorded mechanism leaks on every reload.** `ReplaceBinding` as remove-then-add retains a tombstone and issues a
   fresh id: **41 binding bytes and 2 allocations per keystroke**, and the table grows 100 000 % over 1 000 cycles. In-place
   replacement is flat at **0 binding bytes and 1 allocation per cycle** — the allocation being the dependency vector's
   own growth, not the record.
2. **On the sealed index, in-place replacement buys nothing at scale.** Every post-seal edit is a full index rebuild, so
   the same replacement costs **143 µs** against the segmented index's **0.40 µs**. R4 measured the same ratio from the
   other direction (89 µs per dependency addition). The mechanism choice and the index choice are the same decision.
3. **Two of the six observables fail in every variant, and both are engine defects rather than mechanism differences** —
   the two diagnoses are in the program output and stand on their own:
   - `add_binding` after seal never schedules the new binding, so a recycled row's binding is not evaluated until one of
     its dependencies changes. Every removal variant inherits it.
   - `has_binding` is true only for `WriteSource::Binding`, so `replace_binding` reads an **animated** property as
     overridden and stamps `Imperative` over the animation, ending it. §13.4's rule is about *imperative* overrides;
     conflating the animation layer with one is a precedence bug that all three mechanisms share.

## R9 — static cycle rejection with conditional dependencies (F2.9, AD-04c, §7)

| topology | cyclic | Kahn finds | sealed order acyclic | guard hit |
| --- | --- | --- | --- | --- |
| self_loop | yes | **none** | **yes** | yes |
| two_cycle | yes | cycle | no | yes |
| three_cycle | yes | cycle | no | yes |
| cycle_behind_fan_in | yes | cycle | no | yes |
| chain16 / diamond32 / reconvergent / d_shape | no | none | yes | no |
| acyclic_tree_1000_leaves | no | none | yes | no |
| unbound_dependency_is_not_an_edge | no | none | yes | no |

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.9.1 | 0 false negatives, 0 false positives | **3/4 cyclic rejected**, 7/7 acyclic accepted | **FAIL** |
| T3.9.2 | the conditional cycle trips the 64-pass guard | 64 passes, guard exhausted, value left at 194 | **PASS** |
| T3.9.3 | detection after one conditional edge p99 ≤ 2 ms | **0.961 ms** at 11 000 bindings | **PASS** |
| T3.9.4 | 0 false acyclic claims after a mutation | order claimed acyclic before, Kahn finds the cycle after | **PASS** |
| T3.9.5 | union declaration ≤ 2× invalidations, ≤ 25 % index bytes | **7.71×** (6 500 exact vs 50 100 union) | **FAIL** |
| T3.9.6 | no acyclic topology exhausts the guard | 0/7 | **PASS** |

1. **The declared-graph check has a self-edge blind spot.** `detect_cycles` skips `producer == consumer`, so a
   self-referential binding is *not* a cycle to Kahn, the sealed order reports the graph acyclic, and the only thing that
   catches it is the runtime pass guard. **A compile-time rejection built on this check would accept a self-referential
   binding.** That is the direct answer to whether Kahn can be promoted: not before this is fixed.
2. **A runtime guard is affordable.** Re-running detection after one conditional edge costs **0.96 ms p99 at 11 000
   bindings** — inside a frame, and therefore usable per mutation batch. T3.9.4 shows the mutation clears the sealed
   order, so a stale "acyclic" claim cannot survive; the order being *empty* is why the pull models cannot evaluate until
   it is rebuilt, which is the cost to state in the design.
3. **Static soundness cannot be bought with a union declaration.** Declaring both branches of a conditional binding, with
   the condition false throughout, costs **7.71× the invalidations** of the exact declaration (50 100 against 6 500 over
   100 source writes), because the unused dependency is a state property that every binding in the subtree also reads.
   Above 2× the declared-dependency mechanism has stopped buying anything, so the proposal has to change shape — the
   compiler must reject the cycle in the **declared** graph and the runtime must guard the **conditional** one, rather
   than making every declaration a superset.

## R10 — the read path, interleaved A/B with spread (F2.10, AD-04a, AD-04b)

Five paths, interleaved in blocks of 2 000 reads, 5 runs of 20 000, shuffled over 100 000 properties, on both the
`scalar` and `mixed` schemas. Machine load average 2.04 at start, 2.23 at end.

| path | scalar p50 ns | mixed p50 ns |
| --- | --- | --- |
| A typed scalar read, global `PropertyId` | **6.59** | **9.20** |
| B erased scalar read, same id | 10.01 | 13.28 |
| C typed scalar read through `(instance, local)` | 9.86 | 11.34 |
| D typed read returning `Value` by value | 13.08 | 25.91 |
| E typed string read (`shared_ptr` copy) | — | ~25 |
| — empty loop over the same shuffled order | 0.43 | 0.50 |

| # | Threshold | scalar | mixed | Verdict |
| --- | --- | --- | --- | --- |
| T3.10.1 | run-to-run spread of the p50 ≤ 10 % | 59.6 % | 72.3 % | **FAIL** ×2 |
| T3.10.2 | typed read p99 ≤ 20 ns and ≤ 1.5× erased | 19.02 ns, erased 12.34 ns | 29.55 ns, erased 28.97 ns | **FAIL** ×2 |
| T3.10.3 | base-offset indirection ≤ 5 % | **49.64 %** | **23.34 %** | **FAIL** ×2 |
| T3.10.4 | scalar fast path ≥ 2× cheaper than a `Value` read | 1.98× | 2.82× | **FAIL**, **PASS** |
| T3.10.5 | decomposition | recorded | recorded | RECORDED |

1. **D2's 43 ns read was the variant copy, not the read.** The scalar fast path costs **6.59 ns** and the
   `Value`-returning read D2 measured costs **13.08 ns** — a factor of two, and both an order of magnitude below what a
   43 ns typed read implies. `BindingContext::read_int` goes through `read()` and builds the 40 B variant, so every
   figure D2 published for the read path is a figure for the *copy*. The generated code path should use the scalar fast
   path, and the fast path is now in the harness for it.
2. **The base-offset indirection is not 2.3 %; it is an extra random access.** Resolving `(instance, local)` reads the
   region table — a 400 KB random-access table at this scale — before the value. Measured at **+3.3 ns (49.6 %)** on the
   scalar schema and **+2.1 ns (23.3 %)** on the mixed one, both far outside D2's 2.3 %. The figure D2 recorded is
   reproducible only when the region table stays in cache, which is a small-graph property.
3. **T3.10.1 fails and it is the finding.** With a load average of 2.0–3.6 the run-to-run spread of the median is
   **20–72 %**, so this machine cannot decide a 1 % A/B. Every sub-percent claim about the read path — D2's included —
   is unmeasurable here. The steady-state diagnostic (not the committed protocol, one full warm-up pass first) reduces
   the typed path's spread to **4.6 %** on the scalar schema, which is the number a future A/B should be sized against.
4. **The p99 is a cache-miss distribution, not a read cost.** At a 19–30 ns p99 against a 6–9 ns median over 100 000
   randomly addressed properties, the tail is the memory hierarchy. The committed 20 ns p99 budget was written without
   that in mind; it fails on the mixed schema by 48 %.

## R11 — the property mix of a real UI (F2.11, AD-04b)

**The corpus is authored, and that is the first result.** This repository contains **no `.ui` file** — the compiler is
Phase 5 (§31) — so six screens and 22 component types were authored under `prototypes/d3/corpus/` and every conclusion
below is conditional on the corpus being representative, which T3.11.2 tests rather than assumes.

| screen | instances | properties | bindings | dependency entries | env bindings |
| --- | --- | --- | --- | --- | --- |
| dashboard | 26 | 382 | 113 | 128 | 61 |
| **dialog** | 7 | **124** | 31 | 39 | 17 |
| file_browser | 58 | 832 | 273 | 329 | 121 |
| inspector | 58 | 804 | 275 | 328 | 117 |
| settings | 37 | 574 | 164 | 188 | 84 |
| text_editor | 25 | 393 | 104 | 128 | 62 |
| **corpus** | **211** | **3 109** | **960** | **1 140** | **462** |

**The mix: 71.8 % scalar, 28.2 % payload-owning, 1.19 dependencies per binding** (85.1 % of bindings read exactly one
property), **9.75 B of value per property against `mixed`'s 14.00 B**, and **40.5 % of all dependency entries are
environment properties** (§13.1 categories 8 and 9).

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T3.11.1 | the corpus parses with 0 errors, every binding resolves | 7 files, 0 errors | **PASS** |
| T3.11.2a | scalar share ≥ 40 % | **71.8 %** | **PASS** |
| T3.11.2b | payload-owning share ≥ 5 % | **28.2 %** | **PASS** |
| T3.11.2c | mean dependencies per binding ≤ 3 | **1.19** | **PASS** |
| T3.11.3 | typed+CSR ≤ erased+CSR on the corpus mix, 100 screens | 25.3 vs 84.3 B/property | **PASS** |
| T3.11.4 | typed+CSR ≤ 36 B/property on the corpus mix | **25.3 B/property** | **PASS** |
| T3.11.5 | segmented premium ≤ 12 B/property | **9.3 B/property** | **PASS** |
| T3.11.6 | every screen's live properties ≥ 128 (R3's crossover) | smallest screen `dialog` = **124** | **FAIL** |

**The combined memory on the real mix, with R2's accounting:**

| screens | instances | properties | typed+CSR | B/property | typed+segmented | B/property | erased+CSR | B/property |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 211 | 3 109 | 112 491 | 36.2 | 149 183 | 48.0 | 229 992 | 74.0 |
| 10 | 2 110 | 34 199 | 958 625 | 28.0 | 1 284 917 | 37.6 | 3 590 272 | 105.0 |
| 100 | 21 100 | 345 099 | 8 725 053 | **25.3** | 11 947 345 | 34.6 | 29 099 376 | 84.3 |

1. **R2's 32.2 B/property was pessimistic, and the reason is the corpus's own fan-in.** The corpus holds **9.75 B of value
   per property** against `mixed`'s 14.00 B, and **1.19 dependencies per binding** against R2's assumed 2, so the sealed
   CSR index costs **5.32 B/property** against R2's ~4.9 B on twice the entries. Typed storage is **19.96 B/property** of
   which 7.00 B is addressability, and the total is **25.3 B/property** — inside the 36 B budget with 30 % headroom. **The
   storage answer survives the real mix**, and AD-04b's conditional recommendation is not made *less* safe by it.
2. **The CSR offsets table is a per-property tax the corpus does not escape.** At one screen the combined figure is
   **36.2 B/property**, above the 36 B budget, because (properties + 1) × 4 B of offsets is charged whether or not a
   property has readers. It falls to 28.0 at ten screens and 25.3 at a hundred. This is the same shape R2 recorded and it
   is the strongest measured argument for an `(instance, local)`-addressed index.
3. **T3.11.6 fails on one screen, and it is R3's remaining overshoot confirmed.** A modal dialog declares **124 live
   properties** against the 128-property crossover, so a dialog-only application pays the geometric slab's first-chunk
   overshoot; every other screen is 2.9× to 6.5× above it. The first chunk's start size remains the tunable constant R3
   named, and a ~256 B start would fit the dialog without changing anything else.
4. **The corpus is authored, and its own numbers are the deliverable Phase 2 needs.** Per-component property counts, the
   binding and dependency distribution and the environment-dependency share are all in the program's output, and the
   corpus has to be re-read as the widget set grows — it is a proxy, and the pass/fail verdicts above are conditional on
   its representativeness, which is why T3.11.2 exists.

## Harness changes these revisions made

Every addition to Prototype D2's harness is a clearly-named, **default-off** option or a read-only accessor, so every
figure D2 and D3 R1–R3, R7 published stays reproducible with the defaults:

| addition | default | revisions |
| --- | --- | --- |
| `Value::equal_under(policy)` + `Store::set_equality_policy` | `exact`, the comparison D2 recorded | R6 |
| `TypedStore`/`ErasedStore::read_int`/`read_double`/`read_bool`/`read_string` | additive; `BindingContext` keeps D2's path | R10 |
| `Graph::replace_binding_in_place`, `set_binding_reuse` | off; remove-then-add as D2 recorded | R8 |
| `Graph::set_rebuild_sealed_after_mutation`, `index_stale` | off | R8 |
| `Graph::engine_state_bytes`, `model_state_bytes`, `binding_record_count`, `live_binding_count` | read-only | R5, R8 |

Two of the additions were **defects the revisions had to fix in the harness rather than measure around**:
`rebuild_sealed_index()` had to become post-seal only (a pre-seal rebuild indexes over a property count that does not
exist yet), and `detect_cycles`' self-edge skip is recorded above as finding 5 rather than patched, because it is a
design question for the human gate.

## What this does not show

1. **No `.ui` file exists**, so R11's corpus is authored. Its representativeness bounds are tested, not established.
2. **Timing on this machine is noisy** (load average 2.0–3.6; spreads up to 72 %). Only R4's and R10's *ratios* between
   variants measured in the same interleaved window should be read; single-digit percentages are not.
3. **No GLI/GPU or windowed work**, and no text: §21.2's shaping cost is untouched, as Prototype C remains unstarted.
4. **The six engine defects are diagnosed, not fixed.** They are in Prototype D2's harness, which §30 treats as
   historical evidence; fixing them is a decision, not a measurement.
5. **R5's conformance corpus is 204 small DAGs** (≤ 24 nodes). It found a 31 % divergence rate; it says nothing about how
   often a real application hits the shape that diverges.
