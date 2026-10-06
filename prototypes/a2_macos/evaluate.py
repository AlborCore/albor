#!/usr/bin/env python3
"""Evaluate the pacing triage thresholds from the raw run logs.

The thresholds were committed in plan.md before the runs. This script only reads
them; it does not define or adjust them. Every verdict names the evidence it used.

Usage: python3 prototypes/a2_macos/evaluate.py [raw-dir]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RAW = Path(sys.argv[1] if len(sys.argv) > 1 else "prototypes/a2_macos/raw")

# the launches measure.sh performs; the C1+C3+C4 case is the load-bearing one (plan.md T3.1)
LOAD_BEARING = ("bundled_caffeinated_attended", "C4_attended")
MECHANISMS = ("CADisplayLink", "CVDisplayLink", "CAMetalDisplayLink")
CONDITIONS = ("C0_as_found", "C4_attended", "C5_unattended")

# the full mechanism x condition table: four case x condition launches (plan.md T3.5)
TABLE = [
    ("asfound_raw", "C0_as_found"),
    ("bundled_caffeinated_attended", "C4_attended"),
    ("bundled_caffeinated_unattended", "C5_unattended"),
    ("bundled_no_caffeinate_attended", "C4_attended"),
]
CASES = sorted({case for case, _ in TABLE})

# printf pads numeric fields with spaces, so the value may start with them (§28.4)
KEY_VALUE_RE = re.compile(r"(\w+)=\s*([^ ]*)")
CELL_RE = re.compile(
    r"^CELL mechanism=(?P<mech>\S+) condition=(?P<cond>\S+) runs=(?P<runs>\d+) "
    r"hz_median=(?P<median>[\d.]+) hz_min=(?P<min>[\d.]+) hz_max=(?P<max>[\d.]+) "
    r"ivl_p50_median=(?P<ivl>[\d.]+)$"
)


def parse_run_line(line):
    # every field is key=value, so token splitting survives format additions (§28.4)
    fields = {key: value.strip() for key, value in KEY_VALUE_RE.findall(line)}
    return fields if "mech" in fields and "hz" in fields else None


def read_case(name):
    path = RAW / f"{name}.log"
    if not path.exists():
        return None, [], [], []
    runs, cells, driver, env = [], [], {}, {}
    for line in path.read_text().splitlines():
        if line.startswith("DRIVER "):
            for token in line.split()[1:]:
                key, _, value = token.partition("=")
                driver[key] = value
        elif line.startswith("bundle=") or line.startswith("display_id="):
            for token in line.split():
                key, _, value = token.partition("=")
                env[key] = value
        else:
            run = parse_run_line(line)
            if run:
                runs.append(run)
                continue
            cell = CELL_RE.match(line)
            if cell:
                cells.append(cell.groupdict())
    return path, runs, cells, driver


def verdict(name, ok, evidence):
    state = "PASS" if ok is True else "FAIL" if ok is False else "NOT VERIFIABLE"
    print(f"{name}: {state}\n    {evidence}")
    return ok


def main():
    cases = CASES
    loaded = {case: read_case(case) for case in cases}
    for case, (path, _, _, _) in loaded.items():
        if path is None:
            print(f"missing log for {case}; run measure.sh first")
            return 2

    all_runs = [(case, r) for case in cases for r in loaded[case][1]]
    all_cells = [(case, c) for case in cases for c in loaded[case][2]]
    _, _, _, driver_all = loaded[LOAD_BEARING[0]]

    print("=== environment as the program saw it ===")
    for case in cases:
        path, _, _, driver = loaded[case]
        head = [line for line in path.read_text().splitlines() if line.startswith(("bundle=", "display_id=", "macos_version="))]
        print(f"{case}: {' | '.join(head)}")

    print("\n=== cell medians ===")
    for case in cases:
        for cell in loaded[case][2]:
            print(
                f"{case:24s} {cell['mech']:18s} {cell['cond']:14s} runs={cell['runs']:>2s} "
                f"hz_median={cell['median']:>6s} hz_min={cell['min']:>6s} hz_max={cell['max']:>6s} "
                f"ivl_p50_median={cell['ivl']:>6s}"
            )

    # T3.1: every mechanism at or above 55 callbacks/s under C1+C3+C4
    attended = {
        c["mech"]: float(c["median"])
        for case, c in all_cells
        if (case, c["cond"]) == LOAD_BEARING
    }
    missing = [m for m in MECHANISMS if m not in attended]
    if missing:
        verdict("T3.1", None, f"no C4_attended cell for {missing} in the C1+C3+C4 case")
    else:
        low = {m: hz for m, hz in attended.items() if hz < 55.0}
        verdict(
            "T3.1",
            not low,
            "C4_attended medians under caffeinate+bundle: "
            + ", ".join(f"{m}={attended[m]:.1f} Hz" for m in MECHANISMS)
            + (f"; below 55 Hz: {low}" if low else "; all at or above 55 Hz"),
        )

    # T3.2: no run anywhere reports exactly 0 callbacks
    zero = [
        f"{case}/{r['mech']}/{r['cond']}"
        for case, r in all_runs
        if int(r["cb"]) == 0
    ]
    total_runs = len(all_runs)
    verdict(
        "T3.2",
        not zero,
        f"{len(zero)} of {total_runs} runs reported exactly 0 callbacks" + (f": {zero[:8]}" if zero else ""),
    )

    # T3.3: the mechanisms agree within 10 % of each other under C1+C3+C4
    if missing:
        verdict("T3.3", None, "T3.1's cells are incomplete, so agreement cannot be computed")
    else:
        low, high = min(attended.values()), max(attended.values())
        spread = (high - low) / high if high else float("inf")
        verdict(
            "T3.3",
            spread <= 0.10,
            f"spread={spread * 100:.2f} % (min {low:.1f} Hz, max {high:.1f} Hz); the threshold allows 10 %",
        )

    # T3.4: the T3.8 diagnostic fields are populated in every run
    gpu_count = lambda r: int(r["gpu_p50"].split("/")[1])
    drawable_field = lambda r: int(r["drawables"])
    presentation_field = lambda r: int(r["presented_p50"].split("/")[1])

    missing_gpu = [f"{case}/{r['mech']}/{r['cond']}" for case, r in all_runs if gpu_count(r) == 0]
    no_drawable = [f"{case}/{r['mech']}/{r['cond']}" for case, r in all_runs if drawable_field(r) == 0]
    no_presentation = sorted({f"{r['mech']}/{r['cond']}" for case, r in all_runs if presentation_field(r) == 0})
    verdict(
        "T3.4",
        not missing_gpu and not no_drawable,
        f"GPU timestamps absent in {len(missing_gpu)} of {len(all_runs)} runs; runs with 0 drawables: {len(no_drawable)}; "
        f"presentation-target coverage missing for {no_presentation or 'none'} "
        "(CVDisplayLink exposes no presentation target, so those cells cannot report one)",
    )

    # T3.5: the mechanism x condition table is complete
    have = {(case, c["mech"], c["cond"]) for case, c in all_cells}
    gaps = [(case, m, cond) for case, cond in TABLE for m in MECHANISMS if (case, m, cond) not in have]
    verdict(
        "T3.5",
        not gaps,
        f"{len(have)} of {len(TABLE) * len(MECHANISMS)} cells present" + (f"; gaps: {gaps}" if gaps else ""),
    )

    # T3.6: at least five runs per cell
    thin = [f"{case}/{c['mech']}/{c['cond']}={c['runs']}" for case, c in all_cells if int(c["runs"]) < 5]
    minimum = min(int(c["runs"]) for _, c in all_cells) if all_cells else 0
    verdict(
        "T3.6",
        not thin,
        f"minimum runs per cell = {minimum}; cells below 5: {thin or 'none'}",
    )

    # T3.7: caffeinate presence and exit status recorded for every case
    missing_caff = [case for case in cases if "caffeinate" not in loaded[case][3]]
    missing_status = [case for case in cases if "caffeinate_status" not in loaded[case][3]]
    verdict(
        "T3.7",
        not missing_caff and not missing_status,
        "; ".join(f"{case}: caffeinate={loaded[case][3].get('caffeinate')} status={loaded[case][3].get('caffeinate_status')}" for case in cases),
    )

    # T3.8: GPU cost is reported, and is reported apart from the callback rate
    gpu_values = [float(r["gpu_p50"].split("/")[0]) for _, r in all_runs if gpu_count(r) > 0]
    worst = max(gpu_values) if gpu_values else 0.0
    verdict(
        "T3.8",
        bool(gpu_values),
        f"GPU p50 reported in {len(gpu_values)} of {len(all_runs)} runs, worst cell p50 = {worst:.3f} ms; "
        "the callback rate is reported independently of this figure",
    )

    print("\n=== what the cases say about the recorded finding ===")
    for case in cases:
        runs_here = loaded[case][1]
        zero_here = [r for r in runs_here if int(r["cb"]) == 0]
        slow_here = [r for r in runs_here if 0 < float(r["hz"]) < 55.0]
        rates = [float(r["hz"]) for r in runs_here]
        summary = f"{case:32s} runs={len(rates):3d} 0 Hz runs={len(zero_here):2d} below-55 Hz runs={len(slow_here):2d}"
        if rates:
            summary += f" hz min={min(rates):.1f} median={sorted(rates)[len(rates) // 2]:.1f} max={max(rates):.1f}"
        print(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
