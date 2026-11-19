# Linux

The Linux platform adapter is `albor/platform/linux/wayland/` for **Wayland** (Tier 1, §5.3, AD-22) and
`albor/platform/linux/x11/` for **X11** (Tier 2). The GPU backend is `render-albor` on Vulkan, with Wayland
and XCB WSI through the platform HAL.

## Supported floors

The Linux baseline is **RHEL/AlmaLinux 9+**. The installed artifacts must link on that
distribution, which is why the build and the symbol check use it as the floor:

| input | baseline (RHEL/Alma 9) | current (Ubuntu 24.04) |
| --- | --- | --- |
| glibc | 2.34 | 2.39 |
| libstdc++ | GLIBCXX_3.4.29, CXXABI_1.3.13 | GLIBCXX_3.4.33, CXXABI_1.3.15 |
| compiler | GCC 11.5 | GCC 13 |
| Wayland client | 1.21 | 1.22 |
| wayland-protocols | 1.37, vendored | 1.37, vendored |

`bash build/check-linux-floor.sh <build-tree>` reads every ELF artifact and rejects a
reference above `GLIBC_2.34`, `GLIBCXX_3.4.29` or `CXXABI_1.3.13`. An artifact built on Ubuntu 24.04 or
Ubuntu 22.04 is expected to fail that check; a package for the baseline is built on the
baseline. A newer distribution runs the baseline package, because its glibc and libstdc++
are newer.

The system `libdecor` package is not a dependency or a target. Ubuntu 22.04's libdecor
0.1.0 has no usable frame in the headless test path, and no Albor consumer installs a
libdecor package or sets `LIBDECOR_PLUGIN_DIR`. Ubuntu 22.04 is not a build target either:
its glibc 2.35 and GLIBCXX 3.4.30 are above the baseline, its GitHub runner is unsupported
from 2027-04-17, and a floor package built there would be rejected by the symbol check.

## What ships

- Wayland client with `xdg-shell` toplevels, parented dialogs and tools, and `xdg_popup` +
  `xdg_positioner` popups.
- Native `xdg-decoration` chrome first, with the vendored **libdecor 0.2.2** and its
  built-in Cairo frame as the fallback where the compositor has no `xdg-decoration`.
  The frame is compiled into `albor-platform-wayland`, so it installs with the package.
- The Wayland protocol XMLs (`xdg-shell`, `xdg-decoration`, `text-input-v3`,
  `xdg-activation-v1`, `fractional-scale-v1`, `viewporter`, `xdg-foreign-unstable-v2`) are
  vendored at wayland-protocols **1.37** and generated at build time, so the host's protocol
  package version cannot change the adapter's API.
- Native file dialogs and the system colour scheme through the XDG desktop portals over the
  session bus. The chooser is single selection; the exported `xdg-foreign` toplevel handle
  names the parent window, `color-scheme` reaches the window listener, and a host without a
  portal refuses the panel and keeps the portable dark scheme.
- `xdg-activation-v1` window activation, with a no-op fallback where the compositor does not
  offer it.
- `text-input-v3` IME with the candidate anchor; surrounding text is not sent yet.
- A printable key press commits its character as a `TextInput` event, which is what a text field
  inserts. The text comes from the xkb keymap under the held modifiers, so shift, caps lock and the
  layout all produce the typed glyph; `text-input-v3` only carries input-method text, so a session
  without an input method would otherwise type nothing. Enter, tab and a control/alt/logo chord
  commit no text, because the tree routes them as named keys or accelerators. Both Linux adapters
  share the rule in `albor/platform/linux/common/input_normalization.cpp`.
- The `xdg_wm_base` liveness ping is answered with a pong, so a compositor never marks the
  window unresponsive: KWin desaturates and titles such a window "Not Responding" after its
  timeout, which made a live 60 fps frame look frozen.
- The event loop reads the display with `wl_display_prepare_read`, `wl_display_read_events`
  and `wl_display_dispatch_pending`, and never blocks inside a dispatch. The Vulkan WSI reads
  the same `wl_display` on the render thread, so a blocking `wl_display_dispatch` could wait
  forever for an event on the UI thread's own queue while the render thread consumed the
  socket, freezing the window even though both stage one and stage two were running.
- A surface a renderer presents through is never committed by the platform. The Vulkan WSI
  binds each `wl_surface` to an explicit sync object, so any buffer the client commits on it
  must carry an acquire point; the platform's opaque `wl_shm` placeholder was one such commit,
  and the compositor answered it with `no_acquire_point` and disconnected the client. The
  placeholder is now attached only while the window has no renderer, so the first frame the
  renderer presents maps the surface instead. A window that returns to screen asks the tree
  for a whole frame, because nothing retained the content it had.
- An `xdg_popup` is spent once it is unmapped or dismissed, and a fresh `xdg_surface` cannot
  be created for a `wl_surface` that already carried a role. A popup dismissal therefore
  releases the role and retires the surface: the renderer that still names it releases its
  WSI surface first and destroys the retired `wl_surface` afterwards, and the reopen builds a
  fresh surface and role, re-applying the viewport destination and the buffer scale.
- `wl_data_device` text clipboard and xkbcommon key and modifier normalization. Pointer motion
  reaches the app through `wl_pointer.motion`, and the departure through `wl_pointer.leave`,
  which ends the hover the pointer can no longer follow.
- Fractional scaling through `wp_fractional_scale_v1` and `wp_viewporter`, with the
  preferred scale reported in `WindowMetrics::scale_factor`. The buffer stays at buffer
  scale 1 and the viewport maps it onto the logical extent, so a 1.25 or 1.5 desktop gets
  its exact physical buffer; a compositor without both protocols falls back to the core
  preferred buffer scale, and then to the integer `wl_output` scale. Timerfd frame pacing
  rides the same surface. The Wayland surface v6 scale/transform and pointer
  relative-direction listeners are compiled only on a Wayland 1.22+ client; the pointer
  high-resolution wheel event is compiled on 1.21+.
- A `NativeWindowHandle` carrying the Wayland display and surface, or the XCB connection and window, for the
  Vulkan WSI.
- The portable **Blend2D** renderer, which presents each finished frame through a `wl_shm` buffer.
- **`render-albor` on Vulkan**, the GPU backend: dynamic rendering, the committed SPIR-V container, retained
  targets and host-visible readback. Wayland presents through `VK_KHR_wayland_surface` and X11 through
  `VK_KHR_xcb_surface`. Two frames stay in flight per surface, each with its own command buffer, fence,
  semaphores, descriptor pool and uniform ring, so the UI thread never waits for the display; the queue is
  never drained on a steady frame. Batch buffers, offscreen shadow and layer targets are retained per frame
  slot and reused, and a descriptor set is written once per texture and vertex range rather than per draw.
  Every allocation path is counted, and a steady animated frame raises no device memory allocation. The
  exact composition modes use Vulkan dual-source blending, which the build gives the second fragment output
  `Location 0, Index 1`; glslang's HLSL front end emits `Location 1`, so `build/shaders.sh` rewrites that one
  decoration when it regenerates the container. The backend requires a Vulkan 1.3 device for dynamic
  rendering, while the committed container targets Vulkan 1.2 / SPIR-V 1.5 and was built with glslang
  15.1.0, so it runs on the Linux floor; [Tooling](tooling.md#the-shaders) records the toolchain. The
  offscreen smoke, parity and allocation suites run on every Linux host with a Vulkan loader; the windowed
  suite presents through Weston and Xvfb in CI.
  A window torn down while its surface still stands hands its native object to the shared surface state, so
  the last holder destroys it and an in-flight present never names a dead window or proxy. The finished
  target is copied or blitted into the swapchain image rather than rendered into it directly, because the
  retained target is what a partial frame loads and what a read back returns; Metal and Direct3D 11 present
  through the same retained-target copy.
- One display lock shared by the UI thread and the render thread, because both present through
  the same Wayland connection.

The suite runs against a headless Weston compositor. A normal desktop Wayland session is the
real target, and the remote display path below is optional. The portal suite is self-contained:
it starts its own `dbus-daemon` with a config that declares no service directory, owns
`org.freedesktop.portal.Desktop` on that private bus, and drives the chooser and the settings
interface through it, so neither the session bus nor the host's real portal is ever touched; a
host without `dbus-daemon` skips those tests instead of failing them.

## Dependencies

The adapter needs the Wayland client, its scanner, xkbcommon, D-Bus and the Pango/Cairo
libraries the vendored libdecor frame uses. The D-Bus client is a required dependency of the
adapter, because the desktop portals answer on the session bus:

| package (Debian/Ubuntu) | why |
| --- | --- |
| `libwayland-dev` | Wayland client and `wayland-scanner` |
| `libxkbcommon-dev` | key and modifier mapping |
| `libxcb1-dev` | the Tier 2 X11 window and XCB WSI |
| `libvulkan-dev` | Vulkan headers and the loader |
| `mesa-vulkan-drivers` | the lavapipe device the CI windowed suite runs on |
| `xvfb` | the headless X server the X11 suite runs under |
| `libpango1.0-dev`, `libcairo2-dev` | the libdecor Cairo frame |
| `libdbus-1-dev` | the desktop portal file chooser and colour scheme |
| `pkg-config` | resolve the modules above |
| `autoconf`, `automake`, `libtool`, `autoconf-archive` | autotools vcpkg uses to build the `gperf` host port from source |

`wayland-protocols` is not needed: the XMLs are vendored. `libdecor-0-dev` and its plugin
package are not needed either, because the frame is built from the adapter's private
`vendor/libdecor/` copy. The autotools are build-time only: vcpkg builds `gperf` from source
with `AUTORECONF`, so a host without `aclocal`, `autoreconf` and `libtoolize` fails during
dependency installation with a message naming those programs.

On Debian/Ubuntu:

```sh
sudo apt-get install -y \
    pkg-config cmake ninja-build ccache \
    autoconf autoconf-archive automake libtool \
    libwayland-dev libxkbcommon-dev libxcb1-dev \
    libvulkan-dev mesa-vulkan-drivers xvfb \
    libpango1.0-dev libcairo2-dev libdbus-1-dev \
    weston libgl1-mesa-dri libegl1
```

A host without the frame libraries still builds the adapter; it falls back to native
`xdg-decoration` or an undecorated window. The packaged Linux build expects the frame
libraries, because a desktop distribution already carries them at runtime.

## Build on Linux

The repository presets use vcpkg. From a clone with `VCPKG_ROOT` set:

```sh
cmake --preset debug
cmake --build --preset debug
./.build-debug/tests/albor-tests
```

The Wayland adapter configures only on Linux and only when the development packages are
present. A configure without them still builds the portable tree and the headless tests.

Build a release package for the floor and check it:

```sh
cmake --preset release
cmake --build --preset release
bash build/check-linux-floor.sh .build-release
```

## Build on macOS with a container

Docker Desktop provides a Linux environment for the adapter; the macOS host itself cannot
compile or run Wayland code. Mount the repository and a Linux vcpkg checkout, then use the
same presets inside the container:

```sh
docker run --rm -it \
    -v "$PWD:/src" -w /src \
    -v "$HOME/Projects/vcpkg:/vcpkg" \
    -e VCPKG_ROOT=/vcpkg \
    ubuntu:24.04 bash
```

Inside the container install the dependencies above, then:

```sh
cmake --preset debug
cmake --build --preset debug
./.build-debug/tests/albor-tests
```

The first Linux vcpkg build is the slow part; the container and the CI cache make later
runs cheap. The CI jobs are the authoritative full-build verification.

### The AlmaLinux 9 baseline container

The floor check is only meaningful on the baseline. Enable CRB (for Ninja) and EPEL (for
ccache, Weston and Google Test), install the dependencies, and build:

```sh
docker run --rm -it -v "$PWD:/src" -w /src almalinux:9 bash
dnf install -y 'dnf-command(config-manager)'
dnf config-manager --set-enabled crb
dnf install -y epel-release
dnf install -y gcc-c++ cmake ninja-build pkgconf-pkg-config \
    libpng-devel wayland-devel libxkbcommon-devel \
    pango-devel cairo-devel dbus-devel \
    gtest-devel weston mesa-libEGL mesa-dri-drivers
cmake --preset release
cmake --build --preset release
bash build/check-linux-floor.sh .build-release
```

AlmaLinux 9 carries CMake 3.31, which satisfies the project's 3.28 minimum.

## Headless Wayland and X11 tests

The Linux platform suite is a separate binary. The linux desktop adapters share one platform
mark in `albor-platform-linux-common`, which every `-auto` target carries whole, so linking a
Wayland or X11 adapter beside the headless one still collides at link time while
`albor-platform-auto` may link both and select one from the environment. Run the Wayland half
against a headless Weston compositor:

```sh
export XDG_RUNTIME_DIR=/tmp/albor-xdg
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
weston --backend=headless-backend.so --socket=wayland-0 --idle-time=0 &
weston_pid=$!
export WAYLAND_DISPLAY=wayland-0
./.build-debug/tests/albor-linux-tests --gtest_filter='-X11Platform.*'
kill "$weston_pid"
```

Run the X11 half against Xvfb, which is what the CI job does:

```sh
Xvfb :99 -screen 0 1280x1024x24 &
xvfb_pid=$!
export DISPLAY=:99
unset WAYLAND_DISPLAY
unset XDG_SESSION_TYPE
./.build-debug/tests/albor-linux-tests --gtest_filter='X11Platform.*:AlborVulkanWindowChecks.*'
kill "$xvfb_pid"
```

The suite covers the two adapters together: window creation, show/hide/resize, metrics,
a parented popup, xkb key and modifier mapping, frame pacing, the clipboard surface, the
decoration strategy and client-side frame tests (native first, libdecor fallback, and a popup
under a decorated parent), the fractional scale mapping and its integer fallback, a real
Blend2D frame presented through `wl_shm`, including a present from a render thread while the
UI thread resizes the window, the X11 window and XCB Vulkan present, and the XDG portal suite:
an open and a save panel, a cancel, an empty result, an escaped path, the exported parent
window, a host without a portal, a bus whose service is missing, a portal that names its own
request handle, a bus that goes away while a panel runs, the colour scheme read through both
settings methods, its setting change, and a change that arrives while a panel is open.
The suite can run twice, once at the default scale and once under a Weston authoritative
output scale of 2, so the integer scale path is exercised. Weston does not advertise
`wp_fractional_scale_v1`, so the fractional case runs under a compositor that does, for
example sway 1.12 with its headless backend and `output HEADLESS-1 scale 1.5`; the suite
then asserts the reported factor, and `ALBOR_EXPECT_SCALE=1.5` makes the assertion exact
rather than a capability skip. The Linux CI jobs run the Wayland, X11 and example suites on
lavapipe. A headless compositor has no real input devices or decorations, so a
compositor-driven popup dismissal and the decorated desktop path still need a real Wayland
session; the suite does not claim desktop parity.

The X11 adapter resolves its device scale once per window, in this order: `GDK_SCALE` when it
names a factor between 1 and 4, then the `Xft.dpi` value the session publishes on the root
window, then the screen's own millimetre size, then 1.0. An implausible density falls through
to the next source, and the result is rounded to a quarter step. The server sizes a window in
device pixels, so a logical request is multiplied by that factor when the window is created or
resized, a configure divides it back out for the metrics the layout measures against, and every
pointer position is divided by it before it reaches the tree; without that mapping the scene is
drawn at one extent and shown in another, which shrinks the window and its text.

A transient popup is an override-redirect window, which the window manager neither decorates nor
places: the adapter hangs it under the trigger rectangle of its owner, flips it above that
rectangle when the screen bottom leaves no room, slides it back from the screen edge, marks it
`_NET_WM_WINDOW_TYPE_POPUP_MENU` with its owner as `WM_TRANSIENT_FOR`, takes an active pointer
grab, and dismisses it on the first press outside its bounds, which raises the same closed
notification a Wayland `xdg_popup.done` does. A close request the window manager sends
(`WM_DELETE_WINDOW`) tears the native window down and raises that notification too, which is what
drops the framework stage and lets the last window end the run. X11 remains Tier 2: it has no
IME, no AT-SPI bridge and no fractional scale protocol.

## A display on a remote X11 host

A host with only X11 is not a Wayland host. It can still host a **nested Weston** whose
backend draws into the X11 display:

```sh
weston --backend=x11-backend.so --socket=wayland-0
```

Then run the application with `WAYLAND_DISPLAY=wayland-0` on that host. `XWayland` is not a
substitute: it lets X11 applications run on a Wayland host, which is the opposite direction.

## Limits and follow-ups

- **File dialogs and system appearance** ride the XDG desktop portals over the session bus, which
  needs `dbus-1` at build time and a running `org.freedesktop.portal.Desktop` at run time. The chooser
  is single selection, because a multiple selection changes the public API; the accent colour is not
  mirrored, so only `color-scheme` reaches the window. A host without a portal refuses the panel and
  keeps the portable dark scheme.
- **Platform font resolution** resolves a family and weight through fontconfig and hands over its sfnt bytes; embedded text never reaches it, and a host without fontconfig degrades to embedded-only.
- **Vulkan** with Wayland/X11 WSI is the GPU path, and `render-albor` is the default Linux renderer when the
  Vulkan loader is present. Blend2D stays the portable fallback.
- **AT-SPI** accessibility is not implemented. #42.
- **X11** is the Tier 2 adapter, built from `albor/platform/linux/x11/`.
- A headless Weston has no seat, so clipboard readiness, real input devices and a
  compositor-driven popup dismissal need a desktop session; the suite covers activation's
  no-op fallback, the popup dismissal rebuild is judged by code plus the framework's
  hide/show popup test, and an outside press in the owner window is covered by the widget
  suite through the real input dispatcher.

## CI

The Linux debug and release jobs are **temporarily disabled** in `.github/workflows/build.yml`
until a Linux host can run the full vcpkg build and the platform suite. The `linux-floor`
job runs the same work on `almalinux:9` and is disabled with them. Re-enable the two matrix
entries and remove the `linux-floor` job's `if: false` when the host is ready. The jobs
install the Wayland, Pango, Cairo and D-Bus development packages, run `albor-linux-tests`
after `albor-tests`, and add a second run under a scale-2 Weston. The AlmaLinux job also
ends with `build/check-linux-floor.sh`, which rejects a newer `GLIBC`, `GLIBCXX` or
`CXXABI`.
