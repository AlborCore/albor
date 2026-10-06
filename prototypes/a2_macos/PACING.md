# Prototype A2 macOS — Pacing Triage Results (WP-R3)

**Workload:** standalone display-link triage, no albor code (plan.md). **Hardware:** AMD Radeon Pro 5300M,
macOS 26.6.2, 60 Hz display, single machine (plan.md records no second machine). **Thresholds:** the eight
committed in `plan.md` before any run. All figures below are medians of 5 runs per cell unless stated.

## Verdict

| # | Threshold | Verdict |
| --- | --- | --- |
| T3.1 | all mechanisms ≥ 55 Hz under C1+C3+C4 | **FAIL** — CADisplayLink 0.0 Hz, CVDisplayLink 59.8 Hz, CAMetalDisplayLink 0.6 Hz |
| T3.2 | no run reports exactly 0 callbacks | **FAIL** — 20 of 60 runs, all CADisplayLink, all conditions |
| T3.3 | mechanisms agree within 10 % | **FAIL** — spread 100 % (0.0 vs 59.8 Hz) |
| T3.4 | diagnostic fields populated in every run | **FAIL** — the 20 stalled runs have no GPU samples and 0 drawables; the fields are *themselves* the diagnosis (no link callbacks → nothing to sample), but the threshold as written fails |
| T3.5 | complete mechanism × condition table | **PASS** — 12 of 12 cells |
| T3.6 | ≥ 5 runs per cell | **PASS** — minimum 5 |
| T3.7 | caffeinate presence and exit recorded per run | **PASS** — every case log carries `DRIVER caffeinate=… status=0` |
| T3.8 | GPU cost reported, reported apart from the callback rate | **PASS** — GPU p50 in the 40 runs that rendered; worst cell p50 = 0.000 ms |

**Failures stay failures.** The committed matrix does not clear the unverified markings on A1, A1-Dawn or A2;
it explains what they are up against (see Consequences).

## The mechanism × condition table (Hz, median of 5 × 5 s runs)

| Mechanism | C0 as found (unbundled, no caffeinate) | C1+C3+C4 caffeinate+bundle+attended | C1+C3+C5 caffeinate+bundle+unattended | C3+C4 bundle+attended, no caffeinate |
| --- | --- | --- | --- | --- |
| CADisplayLink | **0.0** | **0.0** | **0.0** | **0.0** |
| CVDisplayLink | 59.8 | 59.8 | 60.0 | 59.8 |
| CAMetalDisplayLink | **0.6** | **0.6** | **0.6** | **0.6** |

Every cell is the median of five runs, and every cell of CADisplayLink and CAMetalDisplayLink is *unanimously*
stalled (hz_min = hz_max), while CVDisplayLink is unanimously fine. The recorded "58 Hz or 0 Hz, apparently at
random" is now reproduced at will, and it is **not random at the cell level**: it is a per-mechanism, per-process
property.

## What each mechanism does

**CVDisplayLink — the reliable one.** 60 of 60 committed-matrix runs delivered 58.8–60.0 Hz under every
condition: unbundled, bundled, caffeinated, attended, unattended. It ignores window visibility, app activation
state and bundle presence entirely. Its interval p50 is 16.66–16.67 ms with a max of ~17.7 ms (one outlier of
100 ms in 15 runs).

**CAMetalDisplayLink — starves exactly when nothing composites.** In the committed matrix it delivered exactly
3 callbacks per 5 s run (0.6 Hz) in all four cases, with `drawables=3` — the full 3-drawable pool vended within
the first ~90 ms, then silence for the remaining ~4.9 s while the 1 Hz run-loop heartbeat kept ticking (hb=55–75
per run). The pool never returns because the window is never composited: `occl=8192` (not visible), window never
ordered to the front in C0/C5. This is deterministic, not random, and the recorded fields prove the mechanism:
drawables stall at exactly the pool size, heartbeats continue.

**CADisplayLink — stalls to 0 Hz whole-process, on no recorded variable.** In the committed matrix, 20 of 20
runs across all four launches delivered 0 callbacks with `visible=1`, `running=0`, heartbeat ticking. Outside
the matrix, in fresh single-run processes launched seconds apart during the same session, the same binary
alternated 59 Hz and 0 Hz with **no recorded difference**: same occlusion state, same activation, same level,
same bundle state, same caffeinate status. Exploratory probes (not committed conditions, recorded in
`raw/probes.log`): a floating-level window (`--floating`), skipping `finishLaunching` (`--no-finish-launching`),
and the direct single-run path all failed to correlate. The gate is a machine/window-server state outside
everything the process can record, which is exactly why the original finding said "apparently at random".

## Exploratory probes (out-of-band, labeled)

Recorded in `raw/probes.log`; not part of the committed matrix and not used by any threshold verdict.

- Single-run CA attended, 8 fresh processes at one moment: 8 × 0.0 Hz.
- Single-run CA attended + `--floating`, 8 fresh processes: 8 × 0.0 Hz.
- Single-run CAMetal attended standalone, 4 fresh processes: 56.5, 57.0, 56.5, 56.5 Hz — **CAMetal works when
  it is the first (or only) link in a fresh process with a composited window**, then throttles ~5 % under
  drawable back-pressure. In the matrix it ran *third* (after CA and CV) and starved 5/5; earlier in the session
  the same in-process sequencing delivered 46–58 Hz. Both its failure modes are real: pool starvation without
  compositing, and order-dependence within a process.
- CA attended with `--no-finish-launching`, 4 fresh processes: 4 × 0.0 Hz — indistinguishable from the
  with-launching state, so `finishLaunching` is not the gate either.

## Two bring-up defects found in the triage program itself (recorded as warnings)

The triage program initially reproduced the *exact* 0 Hz signature for two mundane reasons, both fixed before
the committed matrix ran:

1. an NSScreen-created `CADisplayLink` is **never called** until `addToRunLoop:forMode:` (screen-created links
   are not auto-registered);
2. `addCompletedHandler` **must precede** `commit` (Metal asserts otherwise and the process aborts).

Any future reproduction of this symptom must prove its wiring first; this triage's own history is the evidence
that trivial wiring mistakes produce the identical 0 Hz signature.

## Consequences

- **The recorded finding is explained up to the CADisplayLink gate.** CVDisplayLink is uniformly reliable;
  CAMetalDisplayLink starves deterministically without compositing (drawables=3, heartbeat continues);
  CADisplayLink stalls whole-process on a state none of the recorded facts capture. What was "apparently at
  random" is now three named behaviours.
- **The unverified markings on A1, A1-Dawn and A2 stand** (plan.md). Any windowed timing driven by
  CADisplayLink or CAMetalDisplayLink on this machine inherits these stall modes. Re-attribution of A2's
  Revision R1 frame tail (layer-recycling back-pressure) is now *refuted as sufficient*: the pacing source
  itself stalls, and the earlier "apparently at random" observations are one of the three behaviours above.
- **Design direction for the framework (§10.3, AD-18b):** macOS pacing must not ride `CADisplayLink` or
  `CAMetalDisplayLink` alone. CVDisplayLink (deprecated in macOS 15 but the only uniformly reliable mechanism
  measured) is the pacing source, with a startup gate that refuses CAMetalDisplayLink pacing for a window that
  is not composited. This is a recommendation for revision, not a settled decision (§29).
- **Second machine: none available.** The CADisplayLink stall frequency and the CAMetal order-dependence need
  confirmation on other hardware before any of this becomes *Decided*.

## Reproduction

```sh
cmake --preset release
cmake --build --preset release --target albor-prototype-a2-pacing
bash prototypes/a2_macos/measure.sh        # raw runs into prototypes/a2_macos/raw/
python3 prototypes/a2_macos/evaluate.py    # threshold verdicts from the raw logs
```

Raw evidence: `raw/asfound_raw.log`, `raw/bundled_caffeinated_attended.log`,
`raw/bundled_caffeinated_unattended.log`, `raw/bundled_no_caffeinate_attended.log`, `raw/probes.log`.
