# Prototype D — Property and Binding Engine

Recorded result for the AD-04 input (§30 Prototype D, §28.4 pass/fail, §29 AD-04).
This is measurement code. It is not a runtime module and must not be linked by one (AGENTS.md).

## Recorded before the run

**Thresholds** (declared in `benchmark.cpp` before the measurements, §28.4):

| Category | Threshold |
| --- | --- |
| Single input update, p99 | ≤ 16.0 ms |
| Wide environment update (theme), p99 | ≤ 16.0 ms |
| Worker snapshot publish + settle, p99 | ≤ 16.0 ms |
| Animation frame (1024 animated writes + settle), p99 | ≤ 16.0 ms |
| Memory per property | reported, no threshold (feeds the property layout decision) |
| Memory per binding | reported, no threshold (feeds the Prototype B binding representation) |

**Hardware / build:** Intel Core i7-9750H @ 2.60 GHz, 16 GB, macOS 26.6.2, `cmake --preset release` (`-O3`, C++23, libc++).

**Workload** (§30): 100 000 properties, 11 032 compiled bindings, chain depth 8 per node, 64 environment properties (theme and locale, §13.1), 8 immutable snapshot properties (§10.5), 1024 animation-driven properties (§13.5).

**Update categories:** (a) write one node input and settle; (b) write the shared theme property, which invalidates every binding in the scene; (c) publish an immutable snapshot from a worker thread and settle (§13.2); (d) write one animation frame of 1024 animated properties and settle (§13.5).

**Candidates:** compact dependency records, dirty bitset with Kahn topological ordering, shared dependency nodes.

## Measured

| Metric | compact_records | dirty_bitset_topo | shared_dep_nodes |
| --- | --- | --- | --- |
| Bytes per property | 56.00 | 56.00 | 56.00 |
| Bytes per binding (record + declared deps) | 39.30 | 39.30 | 39.30 |
| Index bytes per binding | 141.67 | 98.08 | 343.40 |
| Index bytes total | 1 416 740 | 980 764 | 3 433 952 |
| Animation side-table bytes (1024 animated) | 65 536 | 65 536 | 65 536 |
| Input update p50 / p95 / p99 (ms) | 0.0012 / 0.0016 / 0.0021 | 0.0113 / 0.0120 / 0.0129 | 0.0012 / 0.0017 / 0.0019 |
| Wide update p50 / p99 (ms) | 0.8835 / 1.1492 | 1.2111 / 2.3496 | 0.9705 / 1.3827 |
| Snapshot update p50 / p99 (ms) | 0.0294 / 0.0541 | 0.0325 / 0.0611 | 0.0335 / 0.0681 |
| Animation frame p50 / p99 (ms) | 0.0297 / 0.0450 | 0.0292 / 0.0438 | 0.0293 / 0.0473 |
| Invalidations per animation frame | 1024 | 1024 | 1024 |
| Redundant candidate visits per input update | 0 | 0 | 0 |
| Bindings planned per input update | 10 | 10 | 10 |
| Input update bindings / passes | 10 / 8 | 10 / 8 | 10 / 8 |
| Wide update bindings / passes | 11 024 / 1 | 11 024 / 1 | 11 024 / 1 |

All three candidates meet the recorded thresholds on this hardware.

## Findings

1. **Storage dominates, not the index.** A property slot is 56 bytes (40-byte type-erased variant + 8-byte generation + write source + padding) against an index of 98 to 343 bytes per binding. The per-property layout is the cost AD-04 must decide.
2. **The binding record is already cheap.** 39 bytes per binding = 24-byte record + 3 x 4-byte declared `PropertyId`s, with no per-binding `std::function` and no per-binding heap block, so §13.3 is satisfied by the shape itself.
3. **Invalidation throughput is not the bottleneck.** 1024 invalidations per animation frame cost 0.030 ms p50, about 29 ns per invalidated property. A 60 Hz frame budget of 16 ms leaves room for roughly 500 000 invalidations per frame in this engine.
4. **The cache has nothing to hide.** Redundant candidate visits are 0 in all three candidates: every dependent of a changed property is a distinct planned binding, so deduplication never rejects work. A caching layer would therefore only pay off if bindings were allowed to read dynamically, which is the devrt path.
5. **Topological ordering costs latency and buys nothing.** `dirty_bitset_topo` is ~10x slower on a narrow input update p50 (0.0113 ms vs 0.0012 ms) because a narrow invalidation scans the dirty words, and it did not reduce the pass count. A chain of depth 8 needs 8 passes in every strategy, because a downstream binding is only discovered dirty while the pass that writes its dependency runs. **A deep binding chain therefore costs one pass per layer.**
6. **Shared dependency nodes cost 3.6x the memory and win only on the wide update.** The per-property node vector is the cost. It pays off only when dependency sets repeat heavily, which this workload does not.
7. **Wide invalidation is linear in the number of dependent bindings.** A theme change costs 11 024 binding evaluations in one pass at roughly 0.10 microseconds each. The flat index is not the bottleneck; the per-binding evaluation cost is, so a lazy pull model is the candidate worth measuring next.
8. **The animation layer is cheap and correctly layered.** An animation frame that writes 1024 animated properties and settles costs 0.030 ms p50. The layer shadows each animated binding value in a side table of 64 bytes per animated property; bindings do not overwrite an animated value, an explicit write cancels the animation, and ending an animation restores the shadowed binding value (§13.5).
9. **Snapshot publishing costs one binding.** A worker `post_set` of immutable data reaches bindings in a single pass; the settle path is dominated by allocating the new payload, not by the invalidation machinery.
10. **Cycle protection is bounded.** A non-converging cycle is stopped by the 64 pass budget, so a bad expression cannot hang the UI thread.

## Candidate for AD-04

`compact_records`: a flat dependent list per property (CSR offsets plus binding ids), an epoch-stamped deduplication set and a pending property set. It is the simplest index and the fastest on narrow updates, which is the common case. If the wide-update cost becomes a problem, the next thing to measure is lazy evaluation, not a more elaborate index.

Keep Kahn ordering as a debug and validation aid, since it detects cycles and emits bindings in a deterministic order, but not as the shipping path.

The animation layer is measured and fits the budget; it only needs the scheduler integration that Phase 3 owns.

## Still open before AD-04 can be closed

- The property slot layout (56 bytes) is dominated by the type-erased variant; a per-component typed storage strategy is unmeasured.
- Evaluation is eager to a fixpoint and pays one pass per chain layer; a pull/lazy model is unmeasured and would change both the wide-update and the deep-chain cost.
- The wide-update path is linear in dependent bindings; a lazy pull model or per-property memoization is the unmeasured candidate.
- Bindings declare dependencies statically (§13.3). The devrt expression VM path needs the dynamic alternative measured before AD-25 conformance can be designed, and finding 4 says that path is where redundancy would first appear.
- The workload is synthetic; a `.ui` corpus would be needed to confirm the fan-out it assumes.