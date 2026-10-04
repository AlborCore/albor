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
