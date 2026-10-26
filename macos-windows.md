# Windows vs. macOS parity analysis

**Scope:** comparison of the Windows platform and rendering work with the macOS reference, including test coverage.
**Date:** 2026-10-25, after the Windows parity, UI Automation and automation-transport pass.

## Verdict

Windows is now at functional parity with macOS for windowing, input, clipboard, IME, DPI, event-loop pacing,
Blend2D, shaped text, the `render-albor` D3D backend, the named-pipe automation transport, and the accessibility
bridge. The Windows accessibility bridge is a real UI Automation provider, not a stub; both accessibility role
suites carry **10 tests**, and Windows adds a real UI Automation client test.

The per-abstraction support table is maintained in [`docs/status.md`](docs/status.md#windows-support).

The remaining differences are shared or process gaps rather than Windows implementation gaps:

- `WindowOptions` still has no `parent` field, and modality is validated but not enforced on either platform.
- macOS has no equivalent of the Windows clipboard/IME/DPI/event-loop integration suite yet.
- Windows has no code-coverage job comparable to `macos-coverage`.
- `IValueProvider::SetValue` is read-only because a `SemanticAction` carries no value argument yet.

## 1. Platform adapter

Both adapters implement the same `PlatformWindow` and `PlatformEventLoop` interfaces.

| Area | macOS | Windows | Assessment |
| --- | --- | --- | --- |
| Window creation, show/hide, close veto | AppKit, 832-line adapter | Win32, 767-line adapter | Parity |
| Popup role | `NSPanel`, non-activating, non-opaque layer | Win32 popup, topmost, non-activating | Parity |
| Outside-click and deactivation dismissal | Present and tested | Present and tested | Parity |
| Resize, DPI/scale changes | AppKit notifications | `WM_SIZE`, `WM_DPICHANGED`, tested | Parity |
| Keyboard/mouse/wheel | Present | Present | Parity |
| IME/text composition | `NSTextInputClient` | `WM_IME_*`, candidate anchor/selection, tested | Parity |
| Clipboard | Pasteboard implementation | Win32 clipboard implementation, tested | Parity |
| Event loop pacing | CVDisplayLink | Waitable timer thread, tested | Different mechanism, same contract |
| System appearance | AppKit appearance | Registry-based appearance | Parity |
| Automation local transport | Unix domain socket | Named pipe, tested | Parity |
| Accessibility | 348-line NSAccessibility bridge, 10 tests | 1,199-line UI Automation bridge, 10 role tests + 1 client test | Parity |

The popup test sets remain asymmetric by design: **10 macOS tests vs. 9 Windows tests**. The missing macOS test
is `ThePopupLayerIsNonopaque`, which is AppKit-specific and has no direct Win32 equivalent.

Windows now has dedicated integration tests for clipboard round-tripping, IME composition lifecycle, DPI metrics
and the `WM_DPICHANGED` resize path, and frame-tick delivery. macOS still lacks the equivalent clipboard, IME,
DPI and event-loop integration tests; that is a macOS test-coverage gap, not a Windows one.

### Shared platform gap

`WindowOptions` in `window/window_options.h` does not contain the `parent` field that design §5.1 shows.
Modality is validated in `window_options.cpp`, but no platform or application code enforces `WindowModal` or
`ApplicationModal` on either platform. This is documented here rather than resolved because adding a parent
handle changes the shared creation contract, not one backend.

## 2. Text and shaped-run parity

| Area | macOS | Windows | Assessment |
| --- | --- | --- | --- |
| HarfBuzz text engine | Same shared engine | Same shared engine | Parity |
| Platform font resolver | CoreText | DirectWrite | Both implemented |
| Platform font tests | 8 CoreText tests | 8 DirectWrite tests | Parity |
| Shaped-run parity | 4 tests | 4 tests | Parity |
| Repeated measure allocation | 0 allocations | 0 allocations | Parity |

The Windows shaped failures were test geometry, not a reference-renderer defect: at 28 px the Windows default
face reports an ascent of `30.214844` and a descent of `7.027344`, so the fixed origin placed the baseline outside
the 40 by 40 surface and the reference produced no ink. The Windows tests now center the run with
`layout.ascent` and `layout.descent` and rotate about the run baseline. The macOS placement is unchanged; both
platforms now assert that the shaped and rotated runs left ink, so a zero-ink run cannot pass.

The same pass found a real D3D backend defect: D3D11 `SV_VertexID` excludes `BaseVertexLocation`, while Metal's
`[[vertex_id]]` includes `baseVertex`. The shader now receives `baseVertex` in its uniform block and indexes
`vertices[vertexId + baseVertex]`; the committed DXBC artifact and stamp were regenerated with
`bash build/shaders.sh --d3d --write`.

The long-run allocation probe is fixed by storing the default `FontSpec` in the engine state instead of
constructing a temporary on every measure. The embedded engine constructor also carries its weight into that
default selection, which preserves bold-only family selection. The long-run count is now **0 allocations**.

## 3. `render-albor` backend

This remains the strongest part of the Windows port.

- Both backends expose the same **73-test** corpus:
  - aligned and feathered geometry
  - clips and transforms
  - soft shadows, hard shadows, sheared casters
  - all composition modes
  - layers, transparency, nested layers
  - paths, holed/concave/even-odd fills
  - lines, pictures, images, fractional scaling, texture reuse
  - shaped, rotated, sheared, and embedded text runs
- Metal test file: **73 tests**
- Direct3D test file: **73 tests**
- Both have 41 parity tests.
- Both backends cover:
  - canonical RGBA readback
  - reading the same finished frame twice
  - refusing a read while a frame is in flight
  - applying a resize that arrived mid-frame
  - empty offscreen frame behavior
  - real window or layer presentation and submit counting
- Direct3D additionally covers two surfaces binding one window and re-rooting. The macOS suite covers two
  surfaces sharing one `CAMetalLayer` and each retaining its own target and cache.
- Metal-specific checks have D3D equivalents:
  - Metal layer presentation vs. offscreen window presentation
  - Metal library magic vs. D3D container/section validation
  - background offscreen draw vs. clear plus opaque-rectangle tests
  - different-scene scale refusal vs. doubled-geometry scale assertion
- The D3D hard-shadow bound is **8**, stricter than Metal's bound of **32**.
- Both two-scale tests verify the doubled extent and the pixel placement of the scaled geometry.

The batch-buffer reuse test was mutation-proven: after 16 warmup allocations, forcing a buffer recreation on every
steady frame produced **256 allocations after 120 frames** (240 extra, 2 per frame) and failed
`AlborRendererBackendChecks.ReusesTheBatchBuffersAcrossSteadyFrames`. The mutation was reverted, and the test
passes with the retained buffers.

D3D tests no longer skip when the device, window, or composited surface is missing. Those paths are required
coverage, so a host that cannot provide them fails the test rather than reporting a green skip.

## 4. Accessibility — now implemented

Windows has `accessibility/accessibility_platform.win32.c++`, a UI Automation provider that publishes the semantic
tree:

- The window is subclassed with `SetWindowSubclass`; `WM_GETOBJECT` with `UiaRootObjectId` is answered through
  `UiaReturnRawElementProvider`, and the provider map is released on `WM_DESTROY`.
- The fragment root implements `IRawElementProviderSimple`, `IRawElementProviderFragment` and
  `IRawElementProviderFragmentRoot`; every node implements the simple and fragment interfaces plus the patterns
  its role supports.
- Role mapping covers every `SemanticRole`: Button, CheckBox, Text, Slider, ProgressBar, Pane (Canvas and
  ScrollView), Edit, Image, ListItem, List, Group, Window and Custom.
- Properties include name, automation id, control type, enabled, keyboard focus, focusability, control/content
  element flags, offscreen, process id, framework id, bounding rectangle, value and read-only, toggle state, and
  selection-item state.
- Patterns include Invoke for buttons, Toggle for checkboxes, Value for text fields, and SelectionItem for list
  rows. Invoke, toggle, select and deselect run the semantic actions the application registered.
- The provider reads an immutable publication snapshot, so a rebuild cannot invalidate an element a reader holds.
  Structure changes are raised with `UiaRaiseStructureChangedEvent`.
- `IValueProvider::SetValue` returns `UIA_E_ELEMENTNOTAVAILABLE`; the bridge has no channel to carry the new text
  into the application, because `SemanticAction` takes no arguments. Reading the value is supported.

The **10 Windows role tests** mirror the macOS role suite: root role, child hierarchy, empty publication, detach,
text-field value, checkbox toggle state, list role, selected row, unselected row, and a plain label with no value.
The suite also sends `WM_GETOBJECT` and asserts the window returns a provider key. An eleventh test drives a real
`IUIAutomation` client through `ElementFromHandle` and `FindFirst`, which proves UI Automation reaches the published
root rather than only the direct provider interfaces. The providers are reached through the platform-only
`accessibility/accessibility_platform.win32.h` accessor, which is the Windows analogue of reading the AppKit
hierarchy directly.

`README.md` and `docs/status.md` now describe both bridges accurately.

## 5. Windows platform services

`tests/conditional/platform_services.test.win32.c++` adds **8 tests**:

- clipboard Unicode round-trip and empty-copy clearing
- IME composition start/end lifecycle and the no-IME anchor/selection no-op
- DPI scale reporting from `GetDpiForWindow` and the `WM_DPICHANGED` resize report
- a stopped loop ignoring a tick and a live pacing thread pumping on its UI thread

These close the Windows side of the previous clipboard/IME/DPI/event-loop gap. macOS still needs its own
integration tests for the same services.

Windows now also has a named-pipe automation transport matching the macOS Unix domain socket. It is covered by
10 transport tests plus a host test that starts the embedded host over `host.json` and completes an MCP
`initialize` and `tools/list` exchange. `automation.md` documents the pipe endpoint, and `form-albor.exe` starts
the host with `albor automation: serving <descriptor>` instead of the previous no-transport refusal.

## 6. CI and verification

Local Windows results from this working tree:

| configuration | suite | result |
| --- | --- | ---: |
| debug | `albor-tests.exe` | 3,335 tests: 3,333 passed, 2 skipped, 0 failed |
| release | `albor-tests.exe` | 3,166 tests: 3,164 passed, 2 skipped, 0 failed |
| debug | `albor-generated-font-checks.exe` | 4 passed |
| release | `albor-generated-font-checks.exe` | 4 passed |
| debug | `albor-render-albor-auto-checks.exe` | 1 passed |
| release | `albor-render-albor-auto-checks.exe` | 1 passed |
| debug | `albor-render-blend2d-auto-checks.exe` | 4 passed |
| release | `albor-render-blend2d-auto-checks.exe` | 4 passed |

`bash build/check-format.sh` passes, and `bash build/shaders.sh --d3d` reports that the committed DXBC artifact
matches the six HLSL entry points. The macOS suite was not run here because no macOS host is available; the only
macOS source change is the two added `ASSERT_GT(actual_ink, 0u)` assertions.

The complete Windows suite has exactly **2 skips**:

- `DepfileChecks.EscapesBackslashRunsAroundMakeSyntax` — a Windows backslash is a path separator, so the
  backslash-as-file-character case is genuinely platform-conditional.
- `GlyphOutlineChecks.ACubicOutlineIsTessellated` — the test opens the macOS CFF face
  `/System/Library/Fonts/Supplemental/STIXGeneral.otf`.

Neither skip hides required Windows coverage. The previous D3D device, window, composited-surface, DirectWrite,
collection-font and accessibility skips are gone: those paths now assert.

The macOS configuration still excludes `metal_teardown.test.cpp` from Release at link time (issue 29) and the
Metal footprint test skips when coverage instrumentation is active. Those are macOS-specific, intentional, and
unrelated to Windows.

The local results above were measured before the commit that carries this document. CI runs on the pushed commit,
and the workflow run is the authoritative result.

A Windows coverage job was evaluated and not added. It would need a Windows-native instrumentation toolchain
(`clang-cl` plus `llvm-cov`, or OpenCppCoverage), and the footprint/allocation probes are sensitive to
instrumentation. The existing `windows-debug` and `windows-release` jobs cover both configurations; a coverage
job remains a tracked enhancement rather than a parity requirement.

## 7. Test-case summary

| Suite | macOS | Windows |
| --- | ---: | ---: |
| `render-albor` backend/parity | 73 | 73 |
| Window popup | 10 | 9 |
| Blend2D windowed | 3 | 3 |
| Platform font resolver | 8 dedicated | 8 |
| Accessibility roles | 10 | 11 (10 parity + 1 UI Automation client) |
| Windows platform services | not present | 8 |
| Skia backend | 30 macOS-only | replaced by D3D |
| Metal teardown | 2 macOS-only | not applicable |

## 8. Changes in the Windows parity pass

- Fixed the Windows shaped and rotated shaped tests by centering on the face ascent/descent and rotating about
  the run baseline; added zero-ink assertions to the Windows and macOS shaped tests.
- Fixed D3D base-vertex handling by adding `baseVertex` to the uniform block and indexing the vertex buffer with
  `vertexId + baseVertex`; regenerated the committed DXBC artifact and stamp.
- Fixed the long-run measure allocation by storing the default `FontSpec` in the engine state and carrying the
  embedded engine's weight into that selection.
- Proved and retained batch-buffer reuse with a mutation test.
- Implemented the Windows UI Automation bridge and its 10 role parity tests plus a real UI Automation client test;
  the portable accessibility suite now fails rather than skips on Windows and macOS when the bridge is missing.
- Removed required Windows `GTEST_SKIP` paths from D3D, DirectWrite, collection-font and accessibility coverage.
- Added the Windows clipboard, IME, DPI and event-loop integration suite.
- Added the Windows named-pipe automation transport, 10 transport tests and a host MCP exchange test.
- Enabled the `form` example on Windows, which builds and runs against the Win32/Direct3D adapter.
- Added the per-abstraction Windows support table to `docs/status.md`.
- Updated `README.md` and `docs/status.md` to describe both accessibility bridges.

## 9. Remaining priorities

1. **Resolve the shared parent/modality gap** in `WindowOptions` and the window manager; it affects both platforms.
2. **Add macOS equivalents** of the Windows clipboard, IME, DPI and event-loop integration tests.
3. **Carry text edits through the UI Automation value pattern** once a semantic action can receive the new value.
4. **Add a Windows coverage job** once a Windows instrumentation toolchain and exclusion policy are chosen.
5. **Validate the UIA bridge with a third-party assistive-technology client** on an interactive Windows desktop; the
   role and UI Automation client tests cover the hierarchy and patterns, but not a screen reader's navigation.
