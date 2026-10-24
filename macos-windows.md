# Windows vs. macOS parity analysis

**Scope:** comparison of the Windows platform and rendering work with the macOS reference, including test coverage.
**Date:** 2026-10-24, updated after the macOS parity tests and the latest Windows CI run.

## Verdict

Windows is broadly at parity with macOS for windowing, input, Blend2D, and the resolver layer of text handling. The macOS renderer, Blend2D and platform-font test gaps identified in the first pass have been closed; both renderer ledgers now report 72 tests.

It is **not fully at parity**. The Windows accessibility bridge is still a stub, and the latest Windows CI run fails the shaped-text parity tests with no reference ink plus a debug allocation test. The macOS release and coverage jobs are green. The macOS gaps addressed here are test-coverage gaps, not production-code gaps.

## 1. Platform adapter

Both adapters implement the same `PlatformWindow` and `PlatformEventLoop` interfaces.

| Area | macOS | Windows | Assessment |
| --- | --- | --- | --- |
| Window creation, show/hide, close veto | AppKit, 832-line adapter | Win32, 767-line adapter | Parity |
| Popup role | `NSPanel`, non-activating, non-opaque layer | Win32 popup, topmost, non-activating | Parity |
| Outside-click and deactivation dismissal | Present and tested | Present and tested | Parity |
| Resize, DPI/scale changes | AppKit notifications | `WM_SIZE`, `WM_DPICHANGED` | Windows implementation present |
| Keyboard/mouse/wheel | Present | Present | Parity |
| IME/text composition | `NSTextInputClient` | `WM_IME_*`, candidate anchor/selection | Parity |
| Clipboard | Pasteboard implementation | Win32 clipboard implementation | Parity in code |
| Event loop pacing | CVDisplayLink | Waitable timer thread | Different mechanism, same contract |
| System appearance | AppKit appearance | Registry-based appearance | Parity |
| Accessibility | 346-line NSAccessibility bridge | 14-line stub returning `false` | **Windows behind** |

The popup test sets are close: **10 macOS tests vs. 9 Windows tests**. The missing macOS test is `ThePopupLayerIsNonopaque`, which is AppKit-specific and has no direct Win32 equivalent.

Neither platform has direct integration tests for clipboard, IME, DPI changes, or the event loop. That is a shared gap, not a Windows-specific one.

### Shared platform gap

`WindowOptions` in `window/window_options.h` does not contain the `parent` field that design §5.1 shows. Modality is validated in `window_options.cpp`, but no platform or application code enforcing `WindowModal` or `ApplicationModal` was found. This affects both macOS and Windows equally.

## 2. Text and Blend2D

| Area | macOS | Windows | Assessment |
| --- | --- | --- | --- |
| HarfBuzz text engine | Same shared engine | Same shared engine | Parity |
| Platform font resolver | CoreText | DirectWrite | Both implemented |
| Platform font tests | 8 CoreText tests | 8 DirectWrite tests | Parity |
| Blend2D windowed surface | 212 lines, 3 tests | 377 lines, 3 tests | Parity |
| Embedded font registration | Shared | Shared | Parity |

The macOS platform-font suite exercises the resolver factory, refusal of unknown families, the system face,
sfnt reconstruction and its reproducibility, null handling, collection-member extraction, and shaping through
the platform engine. The resolver layers are covered on both platforms.

That unit-level parity does not make end-to-end text parity true on Windows: the two shaped-run parity checks
currently fail in Windows CI because the software reference produces no ink (see §5). The macOS shaped-run
checks pass locally.

## 3. `render-albor` backend

This is the strongest part of the Windows port.

- Both backends expose the same 41-test parity corpus:
  - aligned and feathered geometry
  - clips and transforms
  - soft shadows, hard shadows, sheared casters
  - all composition modes
  - layers, transparency, nested layers
  - paths, holed/concave/even-odd fills
  - lines, pictures, images, fractional scaling, texture reuse
  - shaped, rotated, sheared, and embedded text runs
- Metal test file: **72 tests**
- Direct3D test file: **72 tests**
- Both have 41 parity tests.
- Both backends now cover:
  - canonical RGBA readback
  - reading the same finished frame twice
  - refusing a read while a frame is in flight
  - applying a resize that arrived mid-frame
  - empty offscreen frame behavior
  - real window or layer presentation and submit counting
- Direct3D additionally covers two surfaces binding one window and re-rooting. The macOS suite now covers two
  surfaces sharing one `CAMetalLayer` and each retaining its own target and cache.
- Metal-specific checks have D3D equivalents:
  - Metal layer presentation vs. offscreen window presentation
  - Metal library magic vs. D3D container/section validation
  - background offscreen draw vs. clear plus opaque-rectangle tests
  - different-scene scale refusal vs. doubled-geometry scale assertion
- The D3D hard-shadow bound is **8**, stricter than Metal's bound of **32**.
- Both two-scale tests now verify the doubled extent and the pixel placement of the scaled geometry.

The renderer test ledgers are now at the same count and cover the same lifecycle classes. The macOS additions
are `ReadsBackInCanonicalRgbaOrder`, `ReadsTheSameFinishedFrameTwice`, `RefusesAReadWhileAFrameIsInFlight`,
`ASecondSurfaceCanShareTheLayer`, and the strengthened `DefersAResizeThatArrivesMidFrame`, which now verifies
the applied extent on the next frame. The two-scale test now verifies doubled extent and geometry too.

Equal test counts do not mean equal passing behavior. The latest Windows CI run fails
`AlborParityChecks.AShapedRunMatchesTheReference` and `AlborParityChecks.ARotatedShapedRunMatchesTheReference`
with `expected_ink=0`, so the Windows shaped-text path is behind macOS in observed behavior.

**Caveat:** D3D tests use `GTEST_SKIP` when no Direct3D device or compositor is available, while the Metal tests generally assert readiness. If a Windows CI runner cannot create a composited window or D3D device, those tests can pass as skipped, so a green result may cover less than it appears to. The current run is not green, so skipped-test coverage is still an open question.

## 4. Accessibility — the real gap

This is the largest remaining functional area where Windows is clearly behind:

- macOS has `accessibility/accessibility_bridge.macos.mm`, a 346-line NSAccessibility bridge, with **10 role/accessibility tests**.
- Windows has `accessibility/accessibility_platform.win32.c++`, which simply reports that no bridge exists:
  - `available()` returns `false`
  - `build()` returns `nullptr`
  - no UI Automation implementation
  - no Windows accessibility tests
- Design §10.2 requires UI Automation on Windows.
- `README.md` currently says "accessibility: macOS bridge, Win32 adapter layer ... done," which is inaccurate.
- `docs/status.md` says platform accessibility adapters are planned, which is accurate.

Because the public `albor` umbrella includes `albor-accessibility`, a Windows application built against `albor` receives an accessibility module that is inert.

## 5. CI and verification

Current state:

- macOS: `macos-release` and `macos-coverage`
- Windows: `windows-release` and `windows-debug`
- Linux: disabled
- Windows has no code-coverage job comparable to macOS coverage.
- Previous Windows branch runs failed on the D3D shader stamp. The cause was the `.hlsl` file being checked out with CRLF on Windows; this was fixed by marking `.hlsl` as LF and `.dxbc` as binary.
- The latest completed run (`40034355612`) has the Windows shader checks passing, and both macOS jobs passing.
- The Windows release and debug jobs compile but fail:
  - `AlborParityChecks.AShapedRunMatchesTheReference`
  - `AlborParityChecks.ARotatedShapedRunMatchesTheReference`
  - both report `expected_ink=0` for the software reference
  - Windows debug additionally fails `AllocationProbeChecks.ARepeatedMeasureOfALongRunCostsNoAllocation`
- So the Windows shader-stamp problem is fixed, but Windows text rendering and one debug allocation check remain failing.
- An earlier macOS release run had a flaky failure in `AStreamOfPresentedFramesKeepsTheFootprintFlat`; the latest macOS release and coverage runs passed.

## 6. Test-case summary

| Suite | macOS | Windows |
| --- | ---: | ---: |
| `render-albor` backend/parity | 72 | 72 |
| Window popup | 10 | 9 |
| Blend2D windowed | 3 | 3 |
| Platform font resolver | 8 dedicated | 8 |
| Accessibility roles | 10 | 0 |
| Skia backend | 30 macOS-only | replaced by D3D |
| Metal teardown | 2 macOS-only | not applicable |

The renderer, Blend2D and platform-font suites are now at parity in test count and cover the same lifecycle classes, but the Windows shaped-run tests fail in CI. The other meaningful remaining difference is **accessibility test coverage**: macOS has 10 tests, Windows has none.

## 7. Documentation inconsistencies

- `README.md` says the Win32 accessibility adapter layer is done; it is a stub.
- `docs/status.md` correctly says accessibility adapters are planned.
- Design §10.2 states Windows bridges to UI Automation; implementation does not.
- `WindowOptions` lacks the `parent` field shown in design §5.1, and modality enforcement is missing on both platforms.
- A deleted `render-albor-windows.md` plan should not be used as a current status source; the code and tests are the authority.

## 8. Changes made in the macOS parity pass

- Added four Metal lifecycle tests: canonical RGBA readback, repeated read of one finished frame, refusal of a
  read during an in-flight frame, and two surfaces sharing one `CAMetalLayer`.
- Strengthened the mid-frame resize test to verify the applied extent on the next frame, and the two-scale
  test to verify the doubled extent and geometry.
- Added eight CoreText platform-font tests covering resolver refusal, system-family resolution, default face,
  sfnt reconstruction and reproducibility, null handling, collection-member extraction, and platform-engine
  shaping.
- Added a macOS Blend2D test for a real `NSWindow`-backed layer: surface creation, metrics, frame readback,
  resize and empty-extent refusal.
- Updated the CMake test wiring and added the AppKit link the real-window test needs.

## 9. Remaining priorities

1. **Fix the Windows shaped-text parity failure**: determine why the software reference produces no ink for
   `AShapedRunMatchesTheReference` and `ARotatedShapedRunMatchesTheReference`, then rerun both Windows jobs.
2. **Investigate the Windows debug allocation failure** in
   `AllocationProbeChecks.ARepeatedMeasureOfALongRunCostsNoAllocation`; it is not reproduced by the macOS debug suite.
3. **Implement the Windows UI Automation bridge** and add tests comparable to the 10 macOS accessibility tests.
4. **Audit Windows CI skips** for D3D/compositor tests; ensure the windowed and parity paths actually run on the hosted runner.
5. **Add platform-level tests** for clipboard, IME, DPI changes, and event-loop tick delivery; these are missing on both platforms.
6. **Resolve the shared parent/modality gap** in `WindowOptions` and the window manager.
7. **Add Windows coverage** if the Windows port should hold the same verification standard as macOS.

Until shaped text and the debug allocation test pass, and accessibility is implemented, Windows is **near parity
for windowing and most rendering, but not at full parity overall**. The macOS test-coverage gaps identified in the
first pass are closed.
