# Status Audit — Phase 0 Backlog

Produced by **WP-F0** of the Phase 0 close-out brief. Every item in the brief's backlog list is listed with the evidence
found in the repository, not with what the design doc claims. Where the design doc and the repository disagree, the
repository wins and the disagreement is recorded.

**Audited:** 2026-10-04, at `b49462a` (design doc) with the tree clean.
**Auditor's method:** file existence, `grep` over `prototypes/*/RESULTS.md` and `tests/`, and reading each prototype's
own `Status` and `Next proofs` sections. No measurement was re-run for this audit.

**Second pass:** 2026-10-04, at `a81d2ee`. Four packages reported since the first pass — the pacing triage (WP3), the
D3 revisions, Prototype A2 revision 2, and the Graphite-on-Dawn spike (WP5) — so the summary table, the affected item
sections and the blockers table are updated. No measurement was re-run for this pass either; every updated state cites
the `RESULTS.md` that recorded it.

## Summary

| # | Item | State |
| --- | --- | --- |
| WP0.1 | `.clang-format` overwrite and hash guard | **done** |
| WP0.3 | Per-segment figure reconciliation | **done in this change** |
| WP1 | A1-Parity completion (centroid, histogram, AA audit, per-zoom reference, calibration) | **partial** |
| WP2 | Fair Dawn-vs-Skia re-measure with GPU timestamps | **not started** |
| WP3 | Pacing triage | **done** — `prototypes/a2_macos/PACING.md`, and acted on by A2 revision 2 |
| WP4 | Chunked append-only batches | **not started** |
| WP5 | Graphite-on-Dawn spike and the AD-28 gate | **spike run, T5.1 failed on a missing build feature**; the gate is blocked on a manifest decision |
| WP10a | A2 on Windows and Wayland | **partial — blocked on hardware/CI** |
| WP10b | AD-16 evaluation and reversal criteria | **not started** |
| WP11 | AD-18b off-UI-thread presentation experiment | **not started — unblocked, WP3 reported** |

Of the ten, **two were already complete** at the second pass, one was completed by the audit itself, three are partial,
and four have no evidence of any work having started. The second pass moved WP3 to done and turned WP5 into a blocked
gate; it did not start any new work. Nothing in the design doc claimed otherwise, but nothing in it recorded the
backlog either.

## Item detail

### WP0.1 — `.clang-format` overwrite and hash guard: **done**

- **Evidence:** `build/check-format.sh:14` pins `CLANG_FORMAT_SHA256`; lines 18–23 compute the digest through `shasum`,
  `sha256sum` or `openssl`; lines 35–37 fail on a mismatch. Introduced in `b3fabf4`.
- **Verified, not assumed:** the pinned digest equals the digest of the committed `.clang-format`
  (`4f9e53dd…3260`). Appending one comment line to `.clang-format` makes the script exit **1** with
  `error: .clang-format does not match the pinned style file`, printing both digests; restoring the file returns exit
  **0**. `.clang-format` was left unmodified (`git status` clean).
- **Remaining:** none. The brief also asks for "reformat in one formatting-only commit". **No reformat was needed** —
  `build/check-format.sh` exits 0 over the whole repository, so the tree was already conformant. Recorded as a no-op
  rather than an empty commit.

### WP0.3 — Per-segment figure reconciliation: **done in this change**

- **Evidence:** `prototypes/a1/RESULTS.md:119` labels the threshold `Recording p99 per segment`, line 129 states the
  denominator, and line 260 gives the arithmetic (`0.394 ms p99 ÷ 1.8 × 10⁶ segments = 0.219 ns`).
  `prototypes/a1_dawn/RESULTS.md:107` and `:112` do the same for Dawn's `0.114 ns`. The two sides therefore agree on
  statistic and denominator, and the `3.85×` ratio at `a1/RESULTS.md:257` is a p99-to-p99 comparison.
- **One real defect remained, and this change fixes it.** `albor-design-v3.md:1226` read
  "recording p50 0.18 ms, 0.219 ns per segment" — pairing a **p50** latency with an **unlabelled** per-segment figure
  that is in fact p99-derived, in the one place a reader is most likely to quote it as the headline recording cost. It
  now states p50 0.18 ms and p99 0.394 ms separately and names the 1.8 × 10⁶ denominator.
- **Remaining:** none for WP0.3. Note that no per-segment figure anywhere is stated as p50; where a *mean* per-segment
  cost would be the more honest basis for a throughput claim, none is used.

### WP1 — A1-Parity completion: **partial**

- **Done:** the Dawn half-ink defect is fixed and the fix is committed (`cbc4b20`), with the WGSL side selector corrected
  for corner 5. Isolated-segment ink now matches exactly: `prototypes/a1_parity/RESULTS.md:87` (T7b) and `:99` (T7c).
  Batch disagreement is quantified at three levels — pixels differing (**13.9549 %** at zoom 1, **63.6837 %** at zoom 8),
  4×4 block means (**53.18 %**), and ink integral. MSAA is referenced twice in the parity results.
- **Remaining — and the prototype says so itself.** `prototypes/a1_parity/RESULTS.md:119-123` lists its own next proofs,
  which are most of this work package: replace the T4 instrument with a **coverage comparison**; **define an edge
  antialiasing tolerance** between analytic rasterisation and 4× MSAA; and run **T5**, the Ganesh-against-Dawn GPU
  column. Absent entirely: signed **centroid offset** in device pixels, the **per-pixel delta histogram** with bands
  0 / 1–4 / 5–16 / 17–64 / 65–128 / 129–255, the **AA audit** (Dawn MSAA 1 vs 4 against Skia's resolve and raster path),
  a **per-zoom Skia reference**, and **metric calibration** (Skia CPU vs GPU, and each engine against itself under a
  known sub-pixel shift).
- **The zoom-8 contradiction is unresolved.** The design doc attributes zoom-8 disagreement largely to Skia's
  non-cosmetic quads, while T7 reports both engines as cosmetic and the harness feeds identical quads. Nothing in the
  repository establishes whether the harness is cosmetic at every zoom, so the attribution is currently unsupported in
  one direction or the other.

### WP2 — Fair Dawn-vs-Skia re-measure: **not started**

- **Evidence:** no occurrence of "timestamp" in `prototypes/a1_dawn/` or `prototypes/a1_parity/`. No GPU-timestamp
  instrumentation exists in either renderer.
- **Consequence:** every Dawn-vs-Skia cost figure in the design doc rests on CPU-side recording time, and the design doc
  already records that the W2 comparison is "partly an artefact of unequal work" because Skia drew 4 000 text labels
  through `drawSimpleText` while Dawn drew no glyphs. **That comparison cannot be repaired by re-reading; it needs the
  re-measure.**
- **Remaining:** GPU timestamps; text excluded from both sides with W2-with-text reported separately; the stale-geometry
  upload assertion (log instance count and buffer content hash per workload per frame); and the measured cost of
  cosmetic-stroke re-tessellation on Skia under zoom.

### WP3 — Pacing triage: **done**

- **Evidence:** `prototypes/a2_macos/plan.md` (thresholds committed at `f7afb51`), `PACING.md`, `measure.sh`,
  `evaluate.py` and four raw logs, recorded at `0205862` and `48f9b75`.
- **Outcome:** the finding is reproduced and named. A committed mechanism × condition table (12 cells, 5 runs each)
  shows `CVDisplayLink` delivering 58.8–60.0 Hz in **60 of 60** runs under every condition, `CAMetalDisplayLink`
  starving to 0.6 Hz in all 20 matrix runs — exactly 3 drawables vended, the pool never returned, the run-loop
  heartbeat still ticking — and `CADisplayLink` stalling to exactly 0 Hz whole-process with **no recorded correlate**
  (occlusion state, activation, bundle, caffeinate, window level and `finishLaunching` all constant across dead and
  live batches).
- **Thresholds:** T3.1, T3.2, T3.3 and T3.4 **fail** as pre-registered; T3.5–T3.8 pass.
- **Two of the triage's own defects are recorded as warnings**, because both reproduce the exact 0 Hz signature from
  trivial wiring mistakes: an `NSScreen`-created `CADisplayLink` is never called before `addToRunLoop:forMode:`, and
  `addCompletedHandler` must precede `commit`.
- **Consequence for the backlog:** the "unverified" markings on the *earlier* windowed figures still stand, because
  those runs rode the stalling mechanism — but the question is now answered rather than open, and **A2 revision 2 acted
  on it**: pacing now rides `CVDisplayLink` and holds 59.98–60.01 Hz with a maximum interval of 18.76 ms and zero
  stalls over five runs (`prototypes/a2/RESULTS.md`, `e640006`).

### WP4 — Chunked append-only batches: **not started**

- **Evidence:** no `LineBatch` or chunked-composite work in `prototypes/a1/` or `prototypes/a1_dawn/`. The design doc
  records the problem — appending to a retained batch is O(n) at **94–120 ms against a 25 ms budget** — and the two
  `chunk` occurrences in the design doc belong to AD-04b's typed-storage slab, not to batches.
- **Remaining:** the whole package, including the §11.3 amendment as a patch.

### WP5 — Graphite-on-Dawn spike and the AD-28 gate: **spike run, T5.1 failed**

- **Evidence:** `prototypes/a1_graphite_dawn/plan.md` (thresholds committed at `a19c9da`), `RESULTS.md`, and the program
  itself, recorded at `a81d2ee`.
- **Outcome:** the spike stops before its question. `nm` over the installed Skia shows Graphite exporting exactly two
  factories, `MakeMetal` and `MakeVulkan`, and no `MakeDawn`, because `vcpkg.json` requests skia with
  `["graphite", "metal"]` and never the port's `dawn` feature. Skia's headers are not feature-gated, so
  `DawnBackendContext.h` ships and the code compiles; the gap surfaces only at link time.
- **T5.1 fails and stands failed.** T5.2–T5.6 are recorded as **not reached**, because no frame was ever recorded.
- **Remaining:** one manifest line plus a full Skia rebuild. **Not applied here** — it changes a pinned dependency's
  feature set and is a human-gate decision (§29). AD-28 cannot be gated until it is answered.

### WP10a — A2 on Windows and Wayland: **partial, blocked**

- **Evidence:** scaffolds and plans exist for both targets. `prototypes/a2/win32/RESULTS.md:5` reads
  `PENDING_CI_MATRIX_RUN (Requires Windows Host)`; `prototypes/a2/linux/RESULTS.md:5` reads
  `PENDING_CI_MATRIX_RUN (Requires Wayland Host)`. Both are honestly labelled, not silently empty.
- **Remaining:** execution. **Blocked** on `[CI ACCESS]` or the hardware itself. Per the brief's stop-and-ask rule this
  is raised rather than worked around.

### WP10b — AD-16 evaluation and reversal criteria: **not started**

- **Evidence:** `docs/AD16-EVALUATION.md` does not exist. The design doc is explicit that the criteria are missing:
  line 1382 reads "AD-16 (*Decided*, with its evaluation and reversal criteria **still to be written*)". That is
  self-consistent — AD-16 is *Decided* on the ownership boundary, with the evaluation deferred — but it is an open item,
  not a finished one.
- **Remaining:** the SDL3 evaluation against AD-16's criteria, the reversal criteria, and the missing-reused-libraries
  list (D-Bus for portals and AT-SPI, IME libraries).

### WP11 — AD-18b off-UI-thread presentation experiment: **not started, no longer blocked**

- **Evidence:** AD-18b is *Open*. The only empirical input is still A1-Dawn's multi-window result, which the doc notes is
  "true by construction for any design presenting on the UI thread" and is therefore evidence about the question rather
  than an answer to it. W3 as currently written cannot fail, which is the defect this package exists to remove.
- **Unblocked** since WP3 reported: A2 revision 2 presents its frames from CoreVideo's own thread, so the arrangement
  AD-18b wants to measure **already exists and is measurable**. What is missing is the isolation measurement itself — no
  two-window hold experiment has been run against it.
- **Two platform-contract defects surfaced on the way**, both recorded because Phase 1 inherits them: a pacing callback
  must never reach AppKit, and the frame hand-off must not run inside a dispatch block (a modal session entered from one
  never returns).

## The brief's own deliverables

Correcting the first pass, which recorded these as non-existent: **D3 and B2 have since reported**, and E2 and
Prototype C have not started.

| Package | State | Evidence |
| --- | --- | --- |
| D3 | **done** — R1, R2, R3, R7 | `prototypes/d3/RESULTS.md`; thresholds committed at `fdd0d39`, results at `f7bf8fe` |
| B2 | **done, with `[BASELINE]` still unfilled** | `prototypes/b2/RESULTS.md`; M5 and the reference-UI comparison are deferred and labelled as such |
| E2 | **not started** | no `prototypes/e2/` |
| Prototype C | **not started** | no `prototypes/c/`; the only Phase 0 prototype never begun |

## Blockers requiring the human

Three placeholders in the brief are unfilled, and each gates a package:

| Placeholder | Gates | Consequence if still unfilled |
| --- | --- | --- |
| `[BASELINE]` | WP-F3 (Prototype B2) | B2 must run against the synthetic baseline and say so in `RESULTS.md`. The brief forbids silent substitution, and the existing B verdict — "AD-03's clean-build claim is contradicted" — only holds against a one-TU, one-core baseline. |
| `[SECOND MACHINE]` | confirmation of the pacing triage's conclusions | **No longer blocking.** The triage reported single-machine and named three mechanisms; A2 revision 2 then measured the consequence on this machine. Confirmation on other hardware is still desirable and is recorded as such in `PACING.md`. |
| `[CI ACCESS]` | WP-P (A2 on Windows and Wayland) | Both tables stay `PENDING_CI_MATRIX_RUN` and the AD-16 evaluation is desk research only. |
