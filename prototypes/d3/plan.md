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

### Pass/fail policy (all three revisions)

Failures stay failures. A threshold that fails because the harness was narrower than its wording is reported as a
failure with the diagnosis, as R1 did. No status of AD-04b, AD-04c or AD-04d changes by measurement alone; the human
gate converts recommendations to decisions.