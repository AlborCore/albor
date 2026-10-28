# Windows vs. macOS parity analysis

**Scope:** comparison of the Windows platform and rendering work with the macOS reference, including test coverage.
**Date:** 2026-10-28, after the native ComboBox popup pass.

## Verdict

Windows is now at functional parity with macOS for windowing, input, clipboard, IME, DPI, event-loop pacing,
Blend2D, shaped text, the `render-albor` D3D backend, the named-pipe automation transport, and the accessibility
bridge. The Windows accessibility bridge is a real UI Automation provider, not a stub; both accessibility role
suites carry **14 tests**, and Windows adds a real UI Automation client test.

The per-abstraction support table is maintained in [`docs/status.md`](docs/status.md#windows-support).

The remaining differences are shared or process gaps rather than Windows implementation gaps:

- Modality is validated but not enforced on either platform.
- macOS has no equivalent of the Windows clipboard/IME/DPI/event-loop integration suite yet.
- Windows has no code-coverage job comparable to `macos-coverage`.
- `IValueProvider::SetValue` is read-only because a `SemanticAction` carries no value argument yet.
- Windows UI Automation exposes the numeric stepper value read-only and does not yet expose its increment and
  decrement actions; it also maps a radio group to a group without a selection provider. macOS exposes both.
  These are the remaining accessibility capability gaps in this pass.

## 1. Platform adapter

Both adapters implement the same `PlatformWindow` and `PlatformEventLoop` interfaces.

| Area | macOS | Windows | Assessment |
| --- | --- | --- | --- |
| Window creation, show/hide, close veto | AppKit, 927-line adapter | Win32, 952-line adapter | Parity |
| Popup role | `NSPanel`, owned child, anchored placement, non-activating, non-opaque layer | Win32 popup, owned child, anchored placement, topmost, non-activating | Parity |
| Outside-click, trigger-click and deactivation dismissal | Present and tested, with anchor exclusion | Present and tested, with trigger forwarding | Parity |
| Resize, DPI/scale changes | AppKit notifications | `WM_SIZE`, `WM_DPICHANGED`, tested | Parity |
| Keyboard/mouse/wheel | Present | Present | Parity |
| IME/text composition | `NSTextInputClient` | `WM_IME_*`, candidate anchor/selection, tested | Parity |
| Clipboard | Pasteboard implementation | Win32 clipboard implementation, tested | Parity |
| Event loop pacing | CVDisplayLink | Waitable timer thread, tested | Different mechanism, same contract |
| System appearance | AppKit appearance | Registry-based appearance | Parity |
| Automation local transport | Unix domain socket | Named pipe, tested | Parity |
| Accessibility | 382-line NSAccessibility bridge, 14 tests | 1,216-line UI Automation bridge, 14 role tests + 1 client test | Parity |

Both popup suites now carry **14 tests**. They cover ownership, anchor placement, trigger-click forwarding,
owner-move following and a parented dialog. The AppKit-only `ThePopupLayerIsNonopaque` has no direct Win32
equivalent, while Win32 adds the opening-press capture handoff that has no AppKit counterpart. A press that hides
the surface from inside its own dispatch now drops the popup stage's capture, so the next open accepts its first click.

Windows now has dedicated integration tests for clipboard round-tripping, IME composition lifecycle, DPI metrics
and the `WM_DPICHANGED` resize path, and frame-tick delivery. macOS still lacks the equivalent clipboard, IME,
DPI and event-loop integration tests; that is a macOS test-coverage gap, not a Windows one.

### Shared platform gap

`WindowOptions` now carries a portable `WindowHandle parent` and a logical trigger `anchor`; the window registry
resolves the handle to the native owner before the platform creates the surface, and refuses a handle that names no
live window rather than silently making the child top level. Both adapters create an owned
popup, place it below the anchor, flip and clamp it into the monitor work area, keep the owner focused, forward a
click on the trigger back to the owner, reposition the popup when the owner moves, and attach a parented dialog as
an owned child. Two native differences remain recorded rather than claimed away: an AppKit child dialog follows an
owner move while a Win32 owned dialog does not, and a Win32 popup that holds capture offers activation to another
application instead of synthesizing a cross-process click, while the AppKit monitor returns the event directly.

The remaining shared gap is modality: it is validated in `window_options.cpp`, but no platform or application code
enforces `WindowModal` or `ApplicationModal` on either platform.

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
- Role mapping covers every `SemanticRole`: Button, CheckBox, Text, Slider, ProgressBar, TabView, TabItem,
  RadioGroup (Group), RadioButton, NumericStepper (Spinner), Pane (Canvas and ScrollView), Edit, Image, ListItem,
  List, Group, Window and Custom.
- Properties include name, automation id, control type, enabled, keyboard focus, focusability, control/content
  element flags, offscreen, process id, framework id, bounding rectangle, value and read-only, toggle state, and
  selection-item state.
- Patterns include Invoke for buttons, Toggle for checkboxes, Value for text fields and numeric steppers, and
  SelectionItem for list rows, tab items and radio buttons. Invoke, toggle, select and deselect run the semantic
  actions the application registered; selecting a tab item or radio button invokes its `activate` action, and
  both stay selected because their group carries one selection.
- The provider reads an immutable publication snapshot, so a rebuild cannot invalidate an element a reader holds.
  Structure changes are raised with `UiaRaiseStructureChangedEvent`.
- `IValueProvider::SetValue` returns `UIA_E_ELEMENTNOTAVAILABLE`; the bridge has no channel to carry the new text
  into the application, because `SemanticAction` takes no arguments. Reading the value is supported.

The **14 Windows role tests** mirror the macOS role suite: root role, child hierarchy, empty publication, detach,
text-field value, checkbox toggle state, list role, tab group and tab selection, radio group and choice selection,
numeric-stepper value, image role with its name and disabled state, selected row, unselected row, and a plain label
with no value. The suite also sends `WM_GETOBJECT` and asserts the window returns a provider key. A fifteenth test
drives a real `IUIAutomation` client through `ElementFromHandle` and `FindFirst`, which proves UI Automation reaches
the published root rather than only the direct provider interfaces. The providers are reached through the platform-only
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

The latest full Windows run, CI `40691294678` at `a8e019f`, recorded:

| configuration | suite | result |
| --- | --- | ---: |
| debug | `albor-tests.exe` | 3,565 tests: 3,563 passed, 2 skipped, 0 failed |
| release | `albor-tests.exe` | 3,387 tests: 3,385 passed, 2 skipped, 0 failed |
| debug | `albor-generated-font-checks.exe` | 4 passed |
| release | `albor-generated-font-checks.exe` | 4 passed |

`bash build/check-format.sh` passes, and `bash build/shaders.sh --d3d` reports that the committed DXBC artifact
matches the six HLSL entry points. The Windows host cannot build the macOS bridge, so the macOS half of this
pass runs in the macOS CI jobs: the debug suite runs **3,588 tests** with zero failures, including all 14 macOS
popup tests, and the release suite runs 3,408 tests with zero failures. The 14 Win32 popup tests passed in both
Windows configurations, including owner-move following, trigger-click forwarding and capture handoff.

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

The radio-group, radio-button, numeric-stepper, Image and dual-engine suites are included in the macOS count,
but not in the Windows counts above, because this host cannot build Windows. The pushed CI run is the
authoritative result for both configurations.

A Windows coverage job was evaluated and not added. It would need a Windows-native instrumentation toolchain
(`clang-cl` plus `llvm-cov`, or OpenCppCoverage), and the footprint/allocation probes are sensitive to
instrumentation. The existing `windows-debug` and `windows-release` jobs cover both configurations; a coverage
job remains a tracked enhancement rather than a parity requirement.

## 7. Test-case summary

| Suite | macOS | Windows |
| --- | ---: | ---: |
| `render-albor` backend/parity | 73 | 73 |
| Window popup | 14 | 14 |
| Blend2D windowed | 3 | 3 |
| Platform font resolver | 8 dedicated | 8 |
| Accessibility roles | 14 | 15 (14 parity + 1 UI Automation client) |
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
- Added the radio-group, radio-button and numeric-stepper roles to both accessibility bridges, with 2 parity
  tests on each platform.
- Added the `Image` widget with natural or explicit sizing, retained pixel painting, semantic name and bounds,
  preview asset resolution beside the source file and dev-runtime asset-root resolution. Both accessibility
  bridges already mapped `SemanticRole::Image`, so the parity suites carry the role without a bridge change.
- Carried cross-file `.ui` composition through the portable compiler and CMake helpers: import paths resolve with
  `std::filesystem`, diagnostics use `generic_string()`, and `IMPORTS` resolves a relative path beside the importer
  before ordering imported units by absolute source path. `build/check-build-scope.sh` now runs in both debug
  matrix jobs, uses portable `sed -i.bak`, and normalizes the `.obj` suffix so the imported body and interface
  rebuild sets are checked on Windows and macOS. The Windows debug and release builds remain the CI authority for
  separator and depfile behavior.
- Added the `albor-lsp` stdio executable over the existing language-server library. The portable transport reads
  and writes `Content-Length` frames and is covered by framing tests; Windows additionally switches stdin and
  stdout to binary mode so the CRT does not translate frame bytes, while macOS uses the portable no-op shim.
  Windows CI is the build authority for that platform source.
- Added `Panel`/`Card`, `ComboBox`, icon `Button` properties and styled `TextField` decoration ranges.
  `Panel` maps to the existing semantic `Group`, `ComboBox` maps to `NSAccessibilityComboBoxRole` and the UI
  Automation combo-box control type, and both platforms share the portable widget and semantic tests. The
  ComboBox value and expanded state publish on both bridges; the UI Automation expand/collapse pattern remains a follow-up.
- Added native ComboBox popup surfaces. `WindowOptions` carries a portable parent handle and an anchor, which the
  window registry resolves to a native owner; `Application` owns a `PopupHost`, tracks popup owners, destroys owned
  popups with their owner, and tears popups down before the main stages. The macOS panel and Win32 popup are owned
  children placed below the trigger, flip and clamp into the work area, dismiss on an outside press or application
  deactivation, follow an owner move, and forward a trigger click to the owner instead of consuming it. `ComboBox`
  keeps the in-tree overlay only as a fallback when no host or owning stage exists, retains the hidden native surface
  for cheap reopening, rebuilds it after a theme or type change, and commits when the already-selected row is
  pressed. The Escape path now reaches the focused control before the controller consumes it, the Win32 opening
  press hands capture to the popup only after the owner receives the release, and a press that hides its own
  surface drops the popup capture instead of arming it against the next open. Both popup suites carry 14 tests.
- Updated `README.md` and `docs/status.md` to describe both accessibility bridges.

## 9. Remaining priorities

1. **Enforce window modality** in the window manager; it affects both platforms.
2. **Add macOS equivalents** of the Windows clipboard, IME, DPI and event-loop integration tests.
3. **Carry text edits through the UI Automation value pattern** once a semantic action can receive the new value.
4. **Add a Windows coverage job** once a Windows instrumentation toolchain and exclusion policy are chosen.
5. **Validate the UIA bridge with a third-party assistive-technology client** on an interactive Windows desktop; the
   role and UI Automation client tests cover the hierarchy and patterns, but not a screen reader's navigation.
