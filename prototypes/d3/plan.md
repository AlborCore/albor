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