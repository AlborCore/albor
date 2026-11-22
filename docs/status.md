# Status

Albor's five implementation phases delivered their runtime surfaces. One Phase 5 tooling
deliverable, the Python automation compatibility harness (GAP-01, #25), is still open, so this
page distinguishes what a pre-release consumer can use from what a stable tag requires. The
authoritative architecture and acceptance contract is [`framework-design.md`](framework-design.md).

## Pre-release scope

The current external target is a **pre-release**. A stable tag is deliberately withheld while
the stable-tag blockers below remain open; tracking is in #263. An accepted item is a documented
limitation of the pre-release, not a defect that may be raised by a change.

| Contract gap | Pre-release | Rationale |
| --- | --- | --- |
| GAP-01 (#25) automation compatibility harness | blocks stable | §13.2 names the suite; Phase 5 deliverable 2 is not delivered |
| GAP-02 (#98, #99) RTL mirroring and bidi | blocks stable | i18n is described as shipped; the limitation must be closed before a stable claim |
| GAP-03 out-of-process dev service | accepted | Dev-only reload path, independent of the shipped application |
| GAP-04 full LSP component-aware completion | accepted | Dev tooling; standard definition, symbols and tokens ship |
| GAP-05 input coalescing, touch/pen, focus scopes | accepted | mouse/keyboard normalization and focus are complete |
| GAP-06 script coverage of non-Latin golden corpus | accepted | pixel-identity budgets are not claimed by the pre-release |
| GAP-07 per-argument handler `invoke()` values | accepted | signals and handlers carry their declared arguments today |
| GAP-08 subtree-level relayout | accepted | damage-region repaint ships; this is a performance follow-up |

| Issue | Pre-release | Rationale |
| --- | --- | --- |
| #95 retained pictures drop embedded text | blocks stable | embedded text in a retained picture is a documented use of custom rendering |
| #7 manifest consistency suite | blocks stable | §13.2 requires the glue/descriptor/preview-stub agreement check |
| #11 physical pixel lengths | accepted | AD-27 defers density to the surface; codegen refuses the unit deliberately |
| #97 devrt/preview font-selection properties | accepted | Dev/Test tooling only, excluded from Release at link time |
| #117 occluded capture can block the UI thread | blocks stable | a hang is not an acceptable pre-release limitation for an application |
| #112 PNG encode stalls a frame | blocks stable | frame pacing is a load-bearing promise of §6.1 |
| #257 Codecov coverage | neither | already delivered in #258; the issue is stale and closed |
| #20, #31–#36, #89, #114, #119–#122, #232 | accepted | enhancements, benchmarks and documentation outside the pre-release contract |

A consumer of the pre-release should read the platform tables below for the current per-platform
levels; "Full" there means implemented and covered by the suites that run today, not that every
gap above is closed.

The pre-release's consumption evidence runs in CI on every `main` push, the nightly and pull
requests from this repository: the `port` workflow builds the installed package against
`tests/consumer` and the `FetchContent` source route against `tests/fetch-consumer`. The `link boundary` step in the macOS and Linux Release jobs runs
`build/check-link-boundary.sh` over the shipping archives; on Windows the same boundary is held by
the Release link itself (a reference to an excluded dev symbol fails to link, BDG-03), with a
native symbol scan tracked in #263.

The debug preset on each platform also exports Google Test's JUnit XML (`GTEST_OUTPUT`) and uploads
it to Codecov Test Analytics with `report_type: test_results` under the platform's flag. The upload
runs even when a test step fails, so a failing or flaky test is still reported (§13.2).

## Platforms

| platform | runtime floor | build baseline | state |
| --- | --- | --- | --- |
| macOS (AppKit, Metal), Intel and Apple silicon | macOS 13.3 | `macos-15`, Xcode 16.4 | implemented — the primary platform |
| Windows (Win32, Direct3D) | Windows 10 1809 (build 17763, LTSC 2019); VC++ 2015-2022 redistributable | `windows-2022`, MSVC 14.44 toolset (VS 2022 17.14 image release), Windows SDK 10.0.22621.0 | implemented and built in CI |
| Linux Wayland (Vulkan GPU, Blend2D CPU) and X11 | RHEL/Alma 9+ (glibc 2.34, GLIBCXX 3.4.29, CXXABI 1.3.13, Wayland 1.21) | `ubuntu-24.04` current; `almalinux:9` baseline | implemented — Wayland is Tier 1 and X11 is Tier 2; the Linux CI jobs are enabled with lavapipe |
| headless | — | — | implemented — the test and CI surface |

The framework and its compiler are **C++20**. The Windows build declares the Windows 10
1809 target (`WINVER`, `_WIN32_WINNT`, `NTDDI_VERSION`) so a newer API cannot slip in, and
the macOS build stamps the 13.3 deployment floor into the shaders and the package. The
Linux artifacts are checked against the glibc and libstdc++ floor with
`build/check-linux-floor.sh`; [Linux](linux.md) carries the baseline. The platform pins are
recorded in [CI build tools](#ci-build-tools).

## CI build tools

The workflow pins the build tools rather than taking them from the runner image. These are the
versions CI actually uses; the floors a consumer needs are in
[Getting started](getting-started.md#requirements) and
[Consuming Albor](consuming.md#requirements-and-deployment).

| tool | macOS | Windows | Linux |
| --- | --- | --- | --- |
| CMake | 4.4.4 | 4.4.4 | 4.4.4 |
| Ninja | 1.13.2 | 1.13.2 | 1.13.2 |
| vcpkg revision | `866e9e258a76437074a5cf02d4c2d71b53bff2f8` | `8c5c88430ac864cf4fe8de4be61f69f003d23ffe` | `866e9e258a76437074a5cf02d4c2d71b53bff2f8` |
| ABI policy | `pinned-toolchain-v2` | `pinned-toolchain-v2` with `--x-abi-tools-use-exact-versions` | `pinned-toolchain-v2` |
| compiler | Xcode 16.4 (Apple Clang) | MSVC 14.44 toolset, SDK 10.0.22621.0 | clang-18 for all Linux presets (debug, release, coverage) |

The Windows job pins the MSVC **toolset** and SDK through `msvc-dev-cmd`, not the Visual Studio
minor version: the 17.14 release named above is the matching release on
the `windows-2022` image, so it follows from the toolset pin rather than being pinned on its own.

Two vcpkg cache classes are kept separate. The **build** cache, shared by the `build-*` workflows
and the `nightly-cache` workflow, is keyed `vcpkg-<os>-<arch>-<dependency-hash>` — for example
`vcpkg-macos-arm64-<hash>`, `vcpkg-macos-x64-<hash>`, and the same form with `windows`/`linux` and
`x64`. Its hash covers `vcpkg.json`, `vcpkg-configuration.json` and `.triplets/`. The **consumer**
cache used by the `port` workflow is keyed `vcpkg-port-<os>-<arch>-<port-hash>`, where the hash
covers `ports/albor/**`; it is never mixed with the build cache. In both classes the toolchain is
already part of vcpkg's own ABI hash, so it is not repeated in the GitHub Actions cache key, and
the architecture is taken from `runner.arch` (`ARM64` → `arm64`, otherwise `x64`).

An application names a window and a component. A normal application links `albor`
(GPU renderer) or `albor-blend2d` (portable CPU renderer); the platform and text engine
are the framework's choice. The target map and advanced composition are on
[Consuming Albor](consuming.md).

## Linux support

| abstraction | level | notes |
| --- | --- | --- |
| Application, event loop, tasks, timers | Full | Wayland loop with timerfd pacing |
| Window roles and close request | Partial | Normal, Dialog and Tool are parented toplevels; Popup uses `xdg_popup` + `xdg_positioner`; close veto is framework-enforced |
| Window modality and parent windows | Full | the framework gate enforces it before dispatch; Wayland has no native dialog role |
| Clipboard | Partial | `wl_data_device` text selection; a headless compositor without a seat has no clipboard |
| IME and text composition | Partial | `text-input-v3`; candidate anchor; surrounding text is not sent yet |
| DPI and scale factor | Full | fractional `wp_fractional_scale_v1` with `wp_viewporter`; integer output fallback, covered at scale 2 by the local container |
| Event-loop frame pacing | Full | timerfd pacing |
| Input normalization and focus | Partial | xkbcommon key and modifier mapping; no scripted real-device suite yet |
| System appearance | Full | XDG portal `org.freedesktop.appearance` colour scheme over the session bus; a change reaches the window listener, and the accent stays the portable default |
| Native file dialogs | Partial | XDG portal `FileChooser` over the session bus: open, save, title, filters, initial folder, proposed name and the exported parent window; a single selection, and a host without a portal refuses the panel |
| Platform font resolution | Full | fontconfig resolves a family and weight and hands over its sfnt; embedded text never reaches it and a host without fontconfig degrades to embedded-only |
| Renderer | Full | `render-albor` on Vulkan through Wayland and XCB WSI, with host-visible readback; Blend2D CPU through `wl_shm` remains the portable fallback |
| Accessibility | Full with gaps | AT-SPI over the D-Bus accessibility bus: application root and published nodes answer role, role name, name, description, interfaces, bounds and state; action, range, text, editable text and radio-group selection are exposed; a publish that changed the roots broadcasts `children-changed` on the root, and an unchanged one stays quiet. A client request is queued and run by the next publish, so application code never runs on the dispatch thread. The launcher is asked for the accessibility bus and a host without one keeps the session bus. Gaps shared with the other two adapters: only structure is announced, so state, name, selection and focus changes raise no event; a list and a tab view do not expose container selection yet; index-based object paths can go stale after a republish. On Linux alone the extents are the published logical bounds rather than screen coordinates (31 tests on a private bus) |

The adapter builds only where the Wayland, xkbcommon and Pango/Cairo/D-Bus development
packages exist. The libdecor frame and the Wayland protocol XMLs are vendored, so no
libdecor package, plugin directory or `wayland-protocols` package is needed. The Linux debug,
release and coverage CI jobs are enabled with lavapipe; only the Alma 9 `linux-floor` job is
disabled. The workflow installs the packages and runs the platform suite under a headless
Weston compositor.
[Linux](linux.md) records the workflow, the Alma 9 baseline and the current limits.

## Windows support

Support levels: **Full** means implemented and covered by the Windows debug/release suites;
**Partial** means implemented with a documented limitation; **Dev/Test** means linked only in
those configurations (BDG-04); **Not supported** means there is no Windows implementation.

### Runtime and platform

| abstraction | level | notes |
| --- | --- | --- |
| Application, event loop, tasks, timers | Full | Win32 message loop and waitable timer pacing |
| Window roles and close request | Full | Normal, Dialog, Tool, Popup; close veto |
| Window modality and parent windows | Full | WindowModal and ApplicationModal are enforced before dispatch; an owned dialog closes with its parent; opening activates it and closing restores the parent control (§5.1) |
| Window events | Full | resize, DPI, close, pointer, keyboard, wheel, appearance |
| Clipboard | Full | native Win32 clipboard; 2 integration tests |
| IME and text composition | Full | `WM_IME_*`, candidate anchor and document selection; lifecycle test |
| DPI and scale factor | Full | `WM_DPICHANGED` and authoritative metrics; 2 integration tests |
| Event-loop frame pacing | Full | waitable timer thread; live tick test |
| System appearance | Full | registry-backed light/dark scheme and accent |
| Input normalization and focus | Full | shared input/focus controllers with Win32 event mapping |
| Native file dialogs | Full | `IFileOpenDialog`/`IFileSaveDialog`; owned by the parent window |
| Layout, measure, arrange | Full | shared layout module |
| Animation and scheduling | Full | shared animation core |
| Headless platform | Full | test and CI surface |

### Rendering

| abstraction | level | notes |
| --- | --- | --- |
| Render surface, metrics, scale | Full | D3D and Blend2D windowed surfaces |
| Two-stage frame pipeline | Full | scene build and render execution separated |
| Custom rendering and retained resources | Full | `CustomRenderNode`, `RenderContext`, batch buffers, glyph cache |
| `render-albor` GPU backend | Full | Direct3D 11 on Windows, Metal on macOS and Vulkan on Linux; the Linux suite carries the offscreen smoke, the parity corpus, the allocation invariant and the Wayland/XCB windowed present, with damage culled stage one and damage-bounded partial frames. The Vulkan backend keeps two frames in flight per surface with no queue drain on a steady frame |
| Blend2D windowed surface | Full | 3 windowed tests |
| Software and headless renderer | Full | screenshot reference for GPU/software parity |

### UI, text, semantics and tooling

| abstraction | level | notes |
| --- | --- | --- |
| Reactive properties and bindings | Full | shared eager-push runtime |
| UI tree, components, event routing | Full | shared UI module |
| Semantic tree and semantic provider | Full | shared semantics module |
| Standard widgets | Full | twenty-nine elements, including Panel, ComboBox, Image, TextField, ListBox, ScrollView, TabView, RadioGroup, NumericStepper, Menu, MenuItem, MenuBar and ToolBar; ComboBox prefers a native `WindowRole::Popup` list and falls back to the in-tree overlay for detached trees |
| Text measurement and shaping | Full | HarfBuzz with DirectWrite resolution; 8 font tests and 4 shaped-run parity tests |
| `.ui` compiler, parser and typed IR | Full | platform-neutral compiler, built and tested on Windows |
| Manifest, glue generator and C++ codegen | Full | generated facades build on Windows |
| Dev runtime and hot swap | Dev/Test | excluded from Release at link time |
| LSP, preview host and inspector | Dev/Test | excluded from Release at link time |
| Automation core and MCP server | Dev/Test | named-pipe transport; 10 transport tests and a host MCP exchange test; layout bounds, pointer/key input and typed value, state and range reads |
| Accessibility | Full with gaps | UI Automation; 25 suite tests including a client walk; range and text writes, expand/collapse and radio-group selection patterns exposed; a publish that changed the roots raises `ChildrenInvalidated` on the window; only structure is announced, so state, name, selection and focus changes raise no event |
| Examples (`hello-world`, `form`, `vertical-slice`) | Full | all build on macOS; the vertical slice is driven end to end through the MCP automation host and captures PNG frames |

Windows has a `windows-coverage` job alongside `macos-coverage`: the instrumented suite runs
under OpenCppCoverage and uploads a Cobertura report with the `windows` Codecov flag. The
`windows-debug` and `windows-release` jobs remain the authority for allocation behaviour.

## Subsystems

| area | module | state |
| --- | --- | --- |
| Application, event loop, tasks, timers | `application/` | shipped |
| Window roles, modality, close request | `window/` | shipped |
| Native file dialogs | `application/`, `platform/` | shipped — appkit panels, shell dialogs, the XDG portal chooser and a scripted headless backend |
| Platform abstraction (macOS, Windows, Linux Wayland and X11, headless) | `platform/` | shipped |
| Render surface, metrics, scale factor | `render/` | shipped |
| Frame pipeline, two-stage frame | `render/` | shipped |
| Custom rendering (`CustomRenderNode`) | `render/` | shipped |
| Reactive properties, eager-push bindings | `ui/` | shipped |
| UI tree, components, event routing | `ui/` | shipped |
| Semantic tree, explicit/implicit keys | `semantics/` | shipped |
| Input normalization, focus | `input/`, `focus/` | shipped; event coalescing is an open gap |
| Layout: measure & arrange, containers | `layout/` | shipped |
| Text, measurement, shaping | `text/` | shipped — HarfBuzz shaping, embedded faces and platform font resolvers on all three platforms |
| Standard widgets | `widgets/` | shipped — twenty-nine elements |
| Animation core | `animation/` | baseline |
| Compiler frontend, `.ui` parser, typed IR | `compiler/` | shipped |
| Manifest parser, validator, glue generator | `manifest/` | shipped |
| C++ code generator, facade emission | `compiler/` | shipped |
| Dev runtime extension, IR loader, hot swap | `devrt/` | Dev only |
| LSP, preview host, inspector | `language-server/`, `preview/`, `inspector/` | Dev/Test only |
| Automation core, MCP server | `automation/` | Dev/Test only — unix socket on macOS, named pipe on Windows; layout bounds, pointer/key input and typed value, state and range reads |
| Platform accessibility adapters | `accessibility/` | shipped on all three platforms — NSAccessibility, UI Automation and AT-SPI; the shared semantic tree is the single source of truth (§10.2, BDG-10) |

## Language coverage

The [`.ui` language](ui-language/index.md) is complete for every shipped element:
components, typed properties, signals and handlers, expressions and bindings, control flow
(`if`, `Repeat`, virtualized `Repeat`), native components through manifests, and
internationalization through `@tr`. Two forms are settled in the grammar and ship today:
component composition and inheritance with `@children` — see
[Components](ui-language/components.md#composition-vs-inheritance) and
[Inheritance](ui-language/components.md#inheritance).

## Build configurations

| configuration | defines | links |
| --- | --- | --- |
| Debug | `ALBOR_DEV`, `ALBOR_AUTOMATION` | the dev runtime extension and the automation host |
| Release | — | nothing Dev/Test — excluded at link time |
| Test | `ALBOR_TEST` | the automation core, the software renderer |

## Testing

The suite is self-contained — Google Test for C++, no external fixtures. Beyond per-file
tests the architecture defines cross-cutting suites
([Tooling](tooling.md#what-the-test-suite-covers)): dual-engine conformance, GPU/software
parity, LSP/compiler agreement, manifest consistency, build-scope checks, and the automation
reference test against the form sample. The software renderer is the screenshot reference.
