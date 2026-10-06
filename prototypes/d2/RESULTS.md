# Prototype D2 — Binding Engine Follow-up

> **CORRECTED by Prototype D3 R1 (`prototypes/d3/RESULTS.md`).** Four figures in this file were wrong. They are
> listed in D3's "Corrections applied to earlier claims" table and marked inline below. D2's thresholds, pass/fail
> classifications and conclusions are otherwise left exactly as recorded: **a failure stays a failure**, and T1.3 in
> particular still reads PASS even though the measurement behind it was unsound.
>
> | Claim here | Was | Corrected to |
> | --- | --- | --- |
> | erased bytes/property, realistic mix | 128.00 B | **68.00 B**, identical for every mix (spread 0.0000 B) |
> | typed bytes/property, all-scalar | 8.00 B, value bytes only | 8.00 B value **+ 7.00 B addressability + 8 184.00 B slab slack** = 8 203.50 B at one instance |
> | "the win shrinks to ~2.3x for scalars" | 2.3x | **no valid scalar figure exists**; the asymptotic *mixed* ratio is **2.47x**, and typed storage is **120x worse** below ~2 048 properties |
> | erased slot "8 B larger than D's 56.00 B" | 64.00 B | **68.00 B** including the region table; 64.00 B for the slot itself |
>
> The 128.00 B came from a measurement bug: the experiment gave the typed store and the erased store **the same
> `RegionTable`**, so the erased store grew to 16 slots and was then divided by 8 properties. The missing slack came from
> D2's accounting reporting `value_bytes()` and `address_bytes()` but never the 64 KB chunk the slab allocates, so the
> chunk was invisible in every figure below.

Recorded result for AD-04b and AD-04c (§30 Prototype D2, §28.4 pass/fail, §29 AD-04).
This is measurement code. It is not a runtime module and must not be linked by one (AGENTS.md).

Prototype D is historical evidence. Nothing in `prototypes/d/` was modified, re-run or re-interpreted; D's numbers are
quoted below only as the baseline they were declared as.

## Recorded before the run

Every threshold was committed in [`plan.md`](plan.md) (`ef90584`) before the benchmark existed. The benchmark carries
the same values as constants and evaluates against them; editing a constant to change a verdict would be a defect.

**Hardware / build:** Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM, macOS 26.6.2, Apple Clang,
C++23, libc++, `cmake --preset release` (`-O3`).

**Commands:** `albor-prototype-d2-benchmark [storage|chains|diamonds|mutation|ids|replace|cycles|animation|all]`.

**Verdict: 55 passed, 2 failed.** Two pre-registered thresholds failed and are reported as failures below, with the
diagnosis, without re-running to make them pass.

## 1. Typed property storage (AD-04b)

| Threshold | Result | Verdict |
| --- | --- | --- |
| T1.1 typed bytes/property, all-scalar component | 8.00 B ≤ 16 B | **PASS** — *superseded: value bytes only; D3 measures 8 203.50 B/property at one instance* |
| T1.2 typed bytes/property, realistic mixed component | 14.00 B ≤ 48 B | **PASS** |
| T1.3 typed ≤ erased on the same mix | 14.00 B ≤ 128.00 B | **PASS** — *unsound: the 128.00 B baseline was inflated by a shared `RegionTable`; at the correct 68.00 B typed loses at one instance* |
| T1.4 allocations to create one 8-property component | 1 ≤ 1 | **PASS** |
| T1.5 construct + destroy per non-trivial property | **325.1 ns** ≤ 200 ns | **FAIL** |
| T1.6 single typed `Int` write + settle, p99 | **278.0 ns** ≤ 250 ns | **FAIL** |

Recorded, no threshold: addressability overhead 7.00 B/property (4 B location + 1 B type + 1 B source + 1 B attachment);
erased slot 64.00 B/property on the same mix, of which 8 B is the generation counter.

**T1.5 failure, diagnosed.** The threshold measured construct *plus* destroy together. Timed separately, destruction
alone is **66.9 ns** per non-trivial property, so the 325 ns figure is dominated by allocating the payloads, not by
releasing them: `Value::make_string` builds a `std::string` in a `make_shared` block and `make_snapshot` copies a
sample vector into its own payload box. The threshold's own rationale said "a refcount release plus a payload free",
which is the 66.9 ns figure; the measurement was wider than the rationale. **The threshold fails as written and the
implementation is not the cause.** A corrected threshold would be ~150 ns for construct and destroy, or ~100 ns for
destruction alone, but changing it after the run would defeat §28.4, so it stands as a failure.

**T1.6 failure, diagnosed.** The write alone is **84.0 ns** p99 and a settle with nothing dirty is **54.0 ns** p99.
Together they account for the 278 ns. The typed write is not slow — the *settle* has a fixed cost that a single-property
graph cannot amortise, dominated by the two `std::vector`s the settle builds per pass (`candidates` and `planned`).
Prototype D reused one member plan buffer; this prototype allocates per pass. **This is an implementation cost, not an
architectural one**, and it is the one place where D2's engine is worse than D's.

### Comparison with Prototype D

| | Prototype D | D2 Candidate B (erased) | D2 Candidate A (typed) |
| --- | --- | --- | --- |
| Bytes per property | 56.00 | 64.00 | **8.00 scalar / 14.00 mixed** |
| Generation counter | 8 B | 8 B | **none** |

The erased slot is 8 B *larger* than D's 56 B, and this is not a regression in the design: D's variant did not carry
`TranslatedString`, and D2's slot adds a one-byte binding-attachment flag. **D2's typed-versus-erased comparison is
therefore not like-for-like with D's 56 B**, and the 128 B/property figure above is a property of D2's value set rather
than evidence against D.

## 2. Deep dependency chains (AD-04c)

| depth | eager p50 / p99 (µs) | eager passes | pull_topo p50 / p99 (µs) | hybrid p50 / p99 (µs) | pull passes | hybrid passes | redundant (all) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 0.235 / 0.272 | 1 | 0.179 / 0.194 | 0.179 / 0.193 | 1 | 1 | 0 |
| 2 | 0.296 / 0.318 | 2 | 0.273 / 0.290 | 0.284 / 0.301 | 1 | 1 | 0 |
| 4 | 0.421 / 0.445 | 4 | 0.465 / 0.483 | 0.497 / 0.516 | 1 | 1 | 0 |
| 8 | 0.605 / 0.634 | 8 | 0.831 / 0.853 | 0.902 / 0.925 | 1 | 1 | 0 |
| 16 | 0.989 / 1.033 | 16 | 1.583 / 1.637 | 1.766 / 1.797 | 1 | 1 | 0 |

| Threshold | Result | Verdict |
| --- | --- | --- |
| T2.1 eager settle p99 at depth 16 | 1.033 µs ≤ 50 µs | **PASS** |
| T2.2 eager passes at depth 16 | 16 == 16 | **PASS** |
| T2.3 pull_topo and hybrid passes at depth 16 | 1 == 1, 1 == 1 | **PASS** |
| T2.4 redundant evaluations at depth 16 | 0 for all three models | **PASS** |
| T2.5 D-shaped depth-8 scene, eager passes | 8 == 8 (D recorded 8) | **PASS** |

**T2.5 reproduces Prototype D exactly**: eager push costs one pass per chain layer, and D's recorded 8 passes for its
depth-8 scene come out again as 8. The eager model in D2 is D's model.

**The pass-count win does not buy latency at these sizes.** Eager push is the *fastest* model at every depth from 4
upward, and at depth 16 it is 1.6× faster than `pull_topo` and 1.8× faster than the hybrid. The reason is that
`pull_topo` walks its entire precomputed binding order on every settle regardless of how many bindings are dirty, and
the hybrid walks the dirty set with an epoch stamp per binding. At 16 bindings the fixed cost of those walks exceeds
the cost of 15 extra passes over one binding each. **The crossover is not measured** — this prototype has no workload
deep enough to find it, which is itself the honest limit of §2.

## 3. Diamond and reconvergent topologies (AD-04c)

Simple diamond, width 2: all three models evaluate 3 bindings with **0 redundant** and 4 candidates (T3.1 **PASS**).
`pull_topo` and the hybrid settle in 1 pass; eager takes 2.

Layered reconvergent, width 8 depth 8 — the topology Prototype D never ran:

| model | p50 / p99 (µs) | passes | bindings evaluated | **redundant** | invalidations |
| --- | --- | --- | --- | --- | --- |
| eager_push | 1.554 / 1.636 | 9 | 18 | **1** | 18 |
| pull_topo | 1.868 / 1.897 | 1 | 17 | **0** | 17 |
| hybrid_push_pull | 1.852 / 1.926 | 1 | 17 | **0** | 17 |

| Threshold | Result | Verdict |
| --- | --- | --- |
| T3.1 redundant, simple diamond | 0 for all three models | **PASS** |
| T3.2 hybrid redundant, reconvergent w8 d8 | 0 ≤ 1 | **PASS** |
| T3.3 eager redundant, reconvergent w8 d8 | 1 | RECORDED |
| T3.5 eager invalidations, reconvergent w8 d8 | 18 | RECORDED |
| T3.4 wide topologies, p99 | diamond w32 0.002 / 0.027 ms, reconvergent w32 0.003 / 0.003 ms, all ≤ 1 ms | **PASS** |

**This is the finding D could not produce.** D recorded 0 redundant candidate visits and concluded "the cache has
nothing to hide"; that conclusion holds for D's topology but not in general. Here eager push evaluates 18 bindings where
17 suffice: the reconvergent sink is reachable at two different depths, so each layer the deep branch advances
re-invalidates it and it is evaluated again. It is only **1** redundant evaluation, not a blow-up — a second branch
would multiply it, and §2 T2.4's zero redundancy does not generalise to reconvergent graphs.

## 4. Runtime graph mutation (AD-04c)

| operation | sealed_csr | segmented |
| --- | --- | --- |
| `add_dependency` p50 / p99 | 1.39 µs / **31.10 µs** | 0.09 µs / **0.20 µs** |
| `remove_dependency` p50 / p99 | 1.39 µs / 23.41 µs | 0.09 µs / **0.14 µs** |
| index bytes | 2 408 | **21 196** + 1 824 slack |
| mutations for which mutation is local | none: every add rebuilds every bucket | every add and remove |

| Threshold | Result | Verdict |
| --- | --- | --- |
| T4.1 segmented `add_dependency` p99 | 0.20 µs ≤ 5 µs | **PASS** |
| T4.2 segmented `remove_dependency` p99 | 0.14 µs ≤ 5 µs | **PASS** |
| T4.3 segmented `replace_binding` p99 | 17.63 µs ≤ 50 µs | **PASS** |
| T4.4 sealed mutation requires a full rebuild | confirmed, 31.10 µs p99 per add | RECORDED |
| T4.5 segmented add ÷ sealed rebuild | 0.656 % ≤ 1 % | **PASS, not robustly** |
| T4.6 settle p99 after 1 000 mutations | 0.044 ms ≤ 1 ms | **PASS** |
| T4.7 a removed dependency drives no evaluation | 0 bindings (was 200) | **PASS** |
| T4.8 untouched ids stay addressable | id 202 still resolves to instance 202 | **PASS** |

**T4.5 passed but the verdict is not robust, and reporting it as a clean pass would be misleading.** The pre-registered
measurement compares two p99 values inside one loop and reported 0.656 %. A second measurement of the same ratio on the
same 200-binding graph, averaging 50 adds instead of taking a p99, gives **3.87 %** — a different answer from the same
comparison, because p99 catches the late adds in the loop, when the index has grown and each rebuild costs more. The
ratio does fall with graph size, because a rebuild is O(V+E) and an add is O(1):

| graph size | segmented add as a share of a sealed rebuild |
| --- | --- |
| 200 bindings | 3.87 % |
| 2 000 bindings | 0.53 % |
| 20 000 bindings | 0.04 % |

So the pre-registered 1 % threshold is met at roughly 5 000 bindings and above, and missed at 200. **The threshold
should be restated with a graph size attached**, and until it is, T4.5 is a pass that a different statistic would have
failed.

The memory side of the same trade is the real cost: the segmented index holds **21 196 B against 2 408 B**, about 8.8×
the sealed index for the same graph, plus 1 824 B of slack that growth never returns. §4's answers and §4's prices are
the same fact.

## 5. Component-local `PropertyId` with runtime base offsets (AD-04b, AD-04a)

| Threshold | Result | Verdict |
| --- | --- | --- |
| T5.1 base+local read ÷ pre-resolved id read, p50 | 1.023 ≤ 1.50 (44.0 ns vs 43.0 ns) | **PASS** |
| T5.2 instance create + destroy p99 | 0.121 µs ≤ 2 µs | **PASS** |
| T5.3 survivor ids unchanged by other churn | survivor_a=0, survivor_b=2 | **PASS** |
| T5.4 destroyed region detectable as stale | live instance keeps its stamp | **PASS** |
| T5.5 mismatched region reuse rejected | same-size reuse=0, mismatched rejected=1, fresh appended=3 | **PASS** |
| T5.6 id indirection cost | 7.00 B per property | RECORDED |

**The base offset is nearly free: 2.3 %, a 1 ns difference on a 43 ns read.** AD-04a's component-local ids, which
Prototype B established for generated code, cost nothing measurable at the read path even on a store whose values live
at schema-computed byte offsets. This is the strongest positive result in the prototype.

**T5.5 is a structural constraint, not a policy choice, and it is the one thing a future implementer must not
rediscover.** A dense property id space hands out contiguous ranges per instance, so freeing an 8-property region and
allocating a 1-property region in its place would shift every id above it and silently invalidate every binding that
referenced one. The region table therefore **only ever reuses a region of identical property count** and appends fresh
regions otherwise, which is what "rejected=1, fresh appended=3" records. The alternative is a 4 B/property indirection
table, on top of the 7 B already measured. §13.3 should state this as a constraint on the id space, not leave it
implicit in an implementation.

## 6. Live-reload `ReplaceBinding` (§13.4, AD-24)

All eight steps of the prompt's sequence pass as real binding replacement — a different binding function **and** a
different dependency set — not as `SetProperty`:

| Step | Result | Verdict |
| --- | --- | --- |
| T6.1a property holds binding A's value | derived=2 | **PASS** |
| T6.1b property follows binding B after replacement | derived=32 (`source_b * 10 + source_a`) | **PASS** |
| T6.1c invalidation follows binding B's source | derived=42 after `source_b=4` | **PASS** |
| T6.2 the replaced binding no longer participates | derived=42, 0 bindings evaluated | **PASS** |
| T6.1d a second replacement takes effect | derived=94 | **PASS** |
| T6.6 removal leaves the last computed value | derived=94 | **PASS** |
| T6.4 an unrelated reload keeps an imperative override | overridden=123 | **PASS** |
| T6.3 `replace_binding` p99 | 1.64 µs ≤ 50 µs | **PASS** |
| T6.5 overridden property whose declared binding **did** change | override kept, new binding does not run | RECORDED |

**T6.2 is what Prototype E could not test.** Prototype E applied `ReplaceBinding` as a `SetProperty`, so the property
was merely re-evaluated and there was no way to observe whether the old binding still participated. Here the old
binding's index entries are removed on replacement, and changing its former source evaluates **0** bindings. This
closes the open item recorded against AD-04d and AD-25 conformance.

**T6.5 is referred, not decided.** §13.4 specifies that an imperatively overridden property keeps its override when its
declared binding did *not* change. It says nothing about the case where the declared binding *did* change. The observed
behaviour is that the override is kept and the new binding does not run — which is defensible but is not derived from
the spec. **No permanent rule is proposed here.** AD-24 must state whether a live reload may take back a property the
application overrode.

T6.6 (binding removal) is likewise not specified by §13.4. The observed behaviour — the property keeps its last computed
value and stops participating — is the least surprising reading, recorded as a proposal rather than as spec.

## 7. Cycle detection (§2.4, AD-04c)

| Threshold | Result | Verdict |
| --- | --- | --- |
| T7.1 a static cycle is detected at seal | 2 bindings on or behind it; Kahn ordered 0 of 2 | **PASS** |
| T7.2 detection over a 10 000-binding graph, p99 | 0.820 ms ≤ 5 ms | **PASS** |
| T7.3 a runtime-introduced cycle is detected | acyclic before, cyclic after | **PASS** |
| T7.4 a non-converging cycle is stopped by the budget | stopped after 64 passes | **PASS** |
| T7.5a converging cycle, wasted passes | 2 passes, budget not exhausted | RECORDED |
| T7.6 can an invalid cycle reach Release | see below | RECORDED |

**The two halves of T7.6 have different answers and the difference decides where cycle detection lives.** A *static*
cycle is decidable by Kahn at seal, and Kahn orders 0 of 2 bindings, so the compiler can reject it and **a sealed graph
needs no runtime guard at all**. A *runtime* cycle is reachable in Release through dynamic dependency insertion, so a
guard has to stay in Release or dynamic graphs must be excluded from the shipping path. This resolves the tension
AD-04c records: Kahn ordering does not have to be a Dev/Test aid whose evaluation order differs from Release's, because
it can be a **compile-time rejection** for static graphs while the pass budget guards the dynamic ones.

**A converging cycle is the case a pass budget cannot distinguish.** The clamped cycle here settles in 2 passes
without touching the budget, so it is neither a divergence nor a failure — but it *is* still a cycle, and it cost 2
passes plus a detection to find out. **A cycle that converges slowly would consume passes while appearing healthy**,
and the budget cannot tell that from a legitimately deep chain. Detection, not the budget, is what identifies it.

## 8. Animation interaction (AD-04d, AD-24)

| model | end mode | during animation | after | |
| --- | --- | --- | --- | --- |
| eager_push | restore_shadow | 100 | 55 | fresh |
| eager_push | re_evaluate | 100 | 55 | fresh |
| eager_push | restore_no_invalidate | 100 | **1** | **STALE** |
| hybrid_push_pull | restore_shadow | 100 | 55 | fresh |
| hybrid_push_pull | re_evaluate | 100 | 55 | fresh |
| hybrid_push_pull | restore_no_invalidate | 100 | **1** | **STALE** |

| Threshold | Result | Verdict |
| --- | --- | --- |
| T8.1 eager + restore_shadow yields the fresh binding value | 55 | **PASS** |
| T8.2 hybrid + re_evaluate yields the fresh binding value | 55 | **PASS** |
| T8.3 an imperative write during an animation wins | bound=7, source=imperative | **PASS** |
| T8.4 rebind after an animation re-evaluates | bound=60 | **PASS** |
| T8.5 1 024 animated properties written + settled, p99 | 0.046 ms ≤ 16 ms | **PASS** |
| T8.6 side table bytes per animated property | 64.0 B (D recorded 64 B) | RECORDED |

**AD-04d's stale-value question is answered, and the answer is subtler than the question assumed.** The threshold was
written expecting Prototype D's `end_animation` to restore a stale value — it restores the shadowed snapshot
(`prototypes/d/store.cpp:95`), and `BindingContext::write` drops writes while an animation owns the target, so the shadow
can only hold the value from animation start. **That is not a defect, because D also invalidates the target
afterwards** (`prototypes/d/engine.cpp:89`), so the binding recomputes and overwrites the restored value immediately.

The stale value is real but one step more fragile than AD-04d assumed: the `restore_no_invalidate` rows isolate it. Drop
the invalidate and both models return **1**, the value from animation start, after the dependency moved to 55. So D's
correctness depends on an invalidate that lives in the *engine*, one layer above the *store* that owns the shadow, and
nothing in the type system or a test of `end_animation` alone would notice its absence. Both the restore and the
invalidate are load-bearing; the shadow table is not, since the re-evaluate mode reproduces every observable behaviour
without restoring anything.

## Recommendation: AD-04b — storage layout

**MODIFY.** Keep the type-erased representation as the fallback, and make typed per-component storage the shipping
default for static graphs.

Evidence: 8.00 B/property for an all-scalar component and 14.00 B for the realistic mix, against 64.00 B for the erased
slot on the same mix — a 4.6× to 8× reduction, and the erased figure is itself 8 B worse than D's recorded 56.00 B once
`TranslatedString` and the attachment flag are included. The comparison is not a wash on any mix measured.

Three costs are measured rather than assumed away, and all three must travel with the recommendation:

1. **7.00 B/property of addressability** (4 B location + 1 B type + 1 B source + 1 B attachment) for a dense id space.
   Against 8 B of scalar value that nearly doubles the cost, so the win shrinks to ~2.3× for scalars and stays ~4.6×
   for the realistic mix. An `(instance, local)`-addressed index would remove the 4 B location entry; that was not
   implemented or measured.
2. **Region reuse is only safe within one property count** (§5 T5.5). This is a constraint on the id space, not an
   implementation detail.
3. **Chunked allocation for stable addresses** is required, or a reallocating slab invalidates every region pointer.
   Its slack is recorded (T1.7) rather than thresholded.

AD-04b should not be marked *Decided* on this alone: the combination of typed storage with the hybrid evaluation model
was not measured (§10), and no `.ui` corpus confirms the property mix (§10).

## Recommendation: AD-04c — evaluation model

**MODIFY.** Keep eager push for the shipping path, adopt the hybrid pull for graphs that are mutated or reconvergent,
and make Kahn a compile-time rejection rather than a Dev/Test aid.

Evidence, and the reasoning that does *not* follow from the pass counts:

- Eager push costs exactly one pass per chain layer (16 passes at depth 16, T2.2) and reproduces D's recorded 8 passes
  for a depth-8 scene (T2.5). That is confirmed, not assumed.
- The one-pass alternatives work: `pull_topo` and the hybrid settle a depth-16 chain in **1** pass with **0** redundant
  evaluations (T2.3, T2.4), and the hybrid produces **0** redundant where eager produces **1** on a reconvergent graph
  (T3.2).
- **But eager push is faster than both at every depth from 4 upward and on both diamond topologies.** The pass-count
  saving is real and does not pay for itself at these sizes, because the pull models walk the whole binding order or a
  stamped dirty set on every settle. Recommending a replacement on pass counts alone would be reading the wrong number.
- Where eager push genuinely loses is reconvergence (T3.3) and cost per mutation, and the hybrid is the only model that
  is both mutation-local (§4) and redundancy-free (§3). That combination, not the chain depth, is the argument.

Cycle handling settles the Kahn question: Kahn detects a static cycle by failing to order it (T7.1, ordering 0 of 2), so
static cycles can be a **compile error**, which removes AD-04c's recorded conflict between Kahn's evaluation order and
Release's. Dynamic cycles still need the pass budget in Release (T7.6), and T7.5 shows a slowly converging cycle can
consume passes while appearing healthy — so detection, not the budget, is what should alert.

AD-04c should not be marked *Decided*: the crossover point between eager and pull is not measured, and no `.ui` corpus
was run (§10).

## Gaps in this prototype

Recorded so the recommendations above are not read as broader than they are (§28.3).

1. **No `.ui` corpus.** §3 substitutes explicit graph generators. Prototype D's own open item — "confirm the fan-out it
   assumes" — **stays open**. The property mix behind both storage figures is a guess.
2. **Typed storage was not combined with the hybrid model.** §2–§8 ran on the type-erased store so their numbers stay
   comparable to D's. The combination is unmeasured, and the recommendation for AD-04b rests partly on the assumption
   that the two are independent.
3. **The eager-versus-pull crossover is not measured.** No workload in this prototype is deep or wide enough for the
   pull models' fixed cost to be repaid.
4. **Strings and snapshots compare by payload identity, not content.** A binding that rebuilds an equal string
   invalidates its readers. That is cheap and matches D, but it makes some redundant work invisible to the counters in
   this prototype, which count *evaluations* rather than value changes.
5. **One machine, one run, and D2's own thresholds include two failures and one statistic-sensitive pass.** The
   recorded platform pacing finding (§30) means no windowed measurement here is trustworthy either.
6. **`remove_property` is implemented but not benchmarked.** §4's mutation list in the prompt names it; only dependency
   and binding mutation were timed.

## What Prototype D got right, confirmed here

D's `RESULTS.md` made ten findings. Three are confirmed by independent measurement in this prototype, and are worth
stating because they were D's conclusions reached without the topologies to test them:

- *One pass per chain layer.* Confirmed exactly (T2.2), and D's own 8 passes reproduce (T2.5).
- *The animation layer is cheap and correctly layered.* Confirmed: 0.046 ms p99 for 1 024 animated properties (T8.5)
  against D's 0.030 ms p50, and a 64.0 B side table against D's 64 B (T8.6).
- *Cycle protection is bounded.* Confirmed (T7.4), with the added finding that the bound is the wrong instrument for a
  converging cycle (T7.5).

D's finding 4 — *redundant candidate visits are 0, so a caching layer would not pay off* — is the one this prototype
contradicts. It holds for D's topology and fails as soon as a node is reachable at two depths (§3).