# Prototype D — Property and Binding Engine

**retroactive — written after the run; thresholds cannot be shown to have been pre-recorded**

Plan for Prototype D (§30, §13, AD-04). Prototype code: not linked by any runtime module (AGENTS.md).

## Questions

| # | Question | Spec |
| --- | --- | --- |
| Q1 | What is the memory cost per property slot and per binding record? | §13.3, AD-04 |
| Q2 | What index structure best handles dependency propagation and batching (flat CSR vs. Kahn bitset vs. shared nodes)? | §13.3, AD-04 |
| Q3 | Can 1024 animated property writes settle within a 60 Hz frame budget (16 ms)? | §13.5, AD-04d |
| Q4 | Does worker snapshot publishing settle without blocking the UI thread? | §10.5, §13.2 |
| Q5 | Does Kahn topological ordering improve evaluation pass counts on deep binding chains? | §13.3 |

## Hardware and Environment

- **Machine:** Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM
- **OS:** macOS 26.6.2 (Darwin 25.6.0)
- **Toolchain / Build:** Apple Clang (Xcode / SDK 15+), C++23, libc++, `cmake --preset release` (`-O3`)

## Workload Parameters

- **Properties:** 100,000 properties total
- **Bindings:** 11,032 compiled static bindings
- **Binding topologies:** Chain depth 8 per node
- **Environment properties:** 64 environment properties (theme, locale)
- **Snapshot properties:** 8 immutable snapshot properties (`std::shared_ptr<const T>`)
- **Animation properties:** 1,024 animation-driven properties shadowed in an animation side table
- **Cycle guard:** 64 passes maximum before cycle abort

## Evaluation Categories

1. **Single input update:** write one node input and settle.
2. **Wide environment update:** write the shared theme property, invalidating every binding in the scene.
3. **Snapshot publish:** publish an immutable snapshot from a worker thread (`post_set`) and settle on the UI thread.
4. **Animation frame:** write one animation frame of 1,024 animated properties and settle.

## Candidates Under Test

1. `compact_records`: flat CSR offset list per property + epoch deduplication + pending set.
2. `dirty_bitset_topo`: dirty bitset scanned in topological order via Kahn ordering.
3. `shared_dep_nodes`: shared dependency nodes representing repeat dependency sets.

## Thresholds (Declared in `benchmark.cpp`)

| Category | Threshold | Rationale |
| --- | --- | --- |
| Single input update, p99 | ≤ 16.0 ms | Must not drop a 60 Hz frame |
| Wide environment update (theme), p99 | ≤ 16.0 ms | Theme switch must complete within one frame |
| Worker snapshot publish + settle, p99 | ≤ 16.0 ms | Worker data handoff must complete within one frame |
| Animation frame (1024 writes + settle), p99 | ≤ 16.0 ms | 60 Hz animation frame budget |
| Memory per property | reported | Informs Phase 2 property layout (AD-04) |
| Memory per binding | reported | Informs Prototype B binding representation |
