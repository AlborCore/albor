# Prototype A2 macOS — Pacing Triage

**thresholds below are committed before the triage runs (§28.4)**

Measurement code. Not a runtime module; nothing under `core/`, `ui/`, `window/` or `render/` may link it (AGENTS.md).

## The finding this triage exists to explain

The design doc records, as a **Platform pacing finding**:

> Windowed frame pacing on this machine is not reproducible: a standalone program with no albor code drives a
> `CADisplayLink` at 58 Hz or at exactly 0 Hz, apparently at random, whenever a `CAMetalLayer` is attached.

Consequences recorded elsewhere: **all** windowed timings in Prototype A1 and Prototype A1-Dawn are marked
**unverified**, and Prototype A2's own Revision R1 attribution of the frame tail to layer-recycling back-pressure is
provisional.

**The standalone program is not in this repository.** The finding is therefore an assertion with no reproduction, which
is why §30 lists this triage as *not started* rather than as done. This revision builds the reproduction and the triage.

## What is measured

One program, `pacing_triage`, that creates a real `NSWindow` with a `CAMetalLayer` attached and counts display-link
callbacks over a fixed window. It reports, **per run**:

| field | source |
| --- | --- |
| callback count and derived Hz | the display link itself |
| inter-callback interval p50 / max | the display link |
| `NSWindow.occlusionState` | `NSWindow` |
| application activation state | `NSApplication` |
| process activation policy | `NSApplication` |
| whether the process has a bundle identifier | `NSBundle` |
| display awake / asleep | `CGDisplayIsAsleep` / `CGDisplayIsActive` |
| display id and its refresh rate | `CGDisplayCopyDisplayMode` |
| number of attached displays | `CGGetActiveDisplayList` |
| presentation time via the presented handler | `CAMetalLayer` |
| GPU cost via command-buffer timestamps | `MTLCommandBuffer` |

Three display-link mechanisms are compared in the same process, because the mechanism may itself be the variable:
`CVDisplayLink`, `NSScreen.displayLinkWithTarget:selector:` and `CAMetalDisplayLink`.

## Conditions, ruled out one at a time

The human supplied the single-machine elimination list. Each condition is a separate run, and **the conditions the
agent controls are automated**; the ones that need a human are recorded as instructions and the run is marked as
requiring them.

| # | Condition | How it is achieved | Automated |
| --- | --- | --- | --- |
| C0 | as-found, nothing changed | baseline run | yes |
| C1 | display kept awake | `caffeinate -dimsu` wrapping the run | yes |
| C2 | session unlocked | cannot be automated; recorded and checked via `CGDisplayIsAsleep` | **no — human** |
| C3 | activated regular app **with a bundle** | the agent builds a real `.app` bundle with an `Info.plist` and runs the binary inside it | yes |
| C4 | attended (window on screen, app frontmost) | the agent orders the window front and runs | yes |
| C5 | unattended (window off screen, app not frontmost) | the agent moves the window off screen and backgrounds the app | yes |
| C6 | external display disconnected | reported by `CGGetActiveDisplayList`; the agent records what is attached and does **not** assume | partly |
| C7 | GPU pinned | reported via the Metal device name; pinning is not attempted without `sudo powermetrics` | **no — human** |

**Second machine: none available.** Every figure is single-machine, and the triage states which conclusions would need
confirmation on other hardware.

## Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T3.1 | under **C1 + C3 + C4** (awake, bundled, activated, attended) **all three** display-link mechanisms deliver ≥ 55 callbacks/s | the machine's display is 60 Hz. 55 allows for one dropped interval per second without declaring failure. **If this holds, the 0 Hz behaviour is a condition, not a machine property.** |
| T3.2 | under those same conditions, **no run reports exactly 0 callbacks** | "exactly 0 Hz" is the specific symptom recorded in the design doc. Reproducing it is the triage's primary objective; failing to reproduce it is also a result |
| T3.3 | the three mechanisms agree within 10 % of each other under C1 + C3 + C4 | if `CVDisplayLink` is fine and `CAMetalDisplayLink` is not, the mechanism is the variable and the fix is a mechanism choice rather than a workaround |
| T3.4 | **T3.8's diagnostic fields are all populated in every run** | a triage that cannot report *why* a run stalled has not diagnosed anything. This is the threshold that keeps the rest honest |
| T3.5 | the mechanism × condition table is **complete**: every mechanism under every automatable condition | a partial table cannot distinguish "condition C fixes it" from "condition C happened not to stall this time", which is the original sin of the 58 Hz / 0 Hz finding |
| T3.6 | each condition is run **at least 5 times** | the recorded symptom is intermittent, so a single sample per condition proves nothing. This is the threshold that would have caught the original finding being unreliable |
| T3.7 | `caffeinate` presence and its exit status are **recorded per run** | otherwise "display kept awake" is an assumption |
| T3.8 | GPU cost from command-buffer timestamps is reported, and is **not** used to explain the callback rate | a 0 Hz callback rate with a ~0 ms GPU cost means the link is not being called, not that the GPU is busy — the distinction the triage exists to make |

## Commands

```sh
cmake --preset release
cmake --build --preset release --target albor-prototype-a2-pacing

# one condition, one mechanism
./.build-release/prototypes/a2/albor-prototype-a2-pacing --mechanism cadisplaylink --seconds 5

# the whole matrix, five runs per cell
./.build-release/prototypes/a2/albor-prototype-a2-pacing --matrix --runs 5 --seconds 5

# the bundled, activated variant
caffeinate -dimsu ./.build-release/a2_pacing/AlborPacing.app/Contents/MacOS/albor-prototype-a2-pacing --matrix
```

## Pass/fail policy

Failures stay failures. **T3.5 and T3.6 are the load-bearing ones**: the original finding was a small number of
uncontrolled observations, and a triage that repeats it with a complete table and five runs per cell is the minimum
needed before any windowed timing in this repository becomes a decision input.

Until this triage closes, the **unverified** markings on A1, A1-Dawn and A2 windowed timings **stand**. This revision
does not clear them; it either explains them or records that it could not.

## Out of scope

No renderer work, no `CADisplayLink` workaround in the framework, and no change to Prototype A2's Revision R1 attribution
unless the triage produces a cause. AD-18b's off-UI-thread experiment (WP-S) is a **separate** package and depends on
this one.
