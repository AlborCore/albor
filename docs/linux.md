# Linux

The Linux platform adapter is `albor/platform/linux/wayland/`. It targets **Wayland** first
(§5.3, AD-22); X11 remains the Tier 2 fallback and is not implemented yet.

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
- A `NativeWindowHandle` carrying `wl_display*` and `wl_surface*` for a future Vulkan WSI.
- The portable **Blend2D** renderer, which presents each finished frame through a `wl_shm`
  buffer. There is no GPU backend on Linux yet; that is #110.
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
| `libpango1.0-dev`, `libcairo2-dev` | the libdecor Cairo frame |
| `libdbus-1-dev` | the desktop portal file chooser and colour scheme |
| `pkg-config` | resolve the modules above |

`wayland-protocols` is not needed: the XMLs are vendored. `libdecor-0-dev` and its plugin
package are not needed either, because the frame is built from the adapter's private
`vendor/libdecor/` copy.

On Debian/Ubuntu:

```sh
sudo apt-get install -y \
    pkg-config cmake ninja-build ccache \
    libwayland-dev libxkbcommon-dev \
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

## Headless Wayland tests

The Linux platform suite is a separate binary, because the Wayland adapter carries its own
`albor_platform_marker` and `albor-tests` carries the headless one. Run it against a
headless Weston compositor:

```sh
export XDG_RUNTIME_DIR=/tmp/albor-xdg
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
weston --backend=headless-backend.so --socket=wayland-0 --idle-time=0 &
weston_pid=$!
export WAYLAND_DISPLAY=wayland-0
./.build-debug/tests/albor-linux-tests
kill "$weston_pid"
```

The suite has 35 tests: window creation, show/hide/resize, metrics, a parented popup,
xkb key and modifier mapping, frame pacing, the clipboard surface, the decoration strategy
and client-side frame tests (native first, libdecor fallback, and a popup under a decorated
parent), the fractional scale mapping and its integer fallback, a real Blend2D frame
presented through `wl_shm`, including a present from a render thread while the UI thread
resizes the window, and the XDG portal suite: an open and a save panel, a cancel, an empty
result, an escaped path, the exported parent window, a host without a portal, a bus whose
service is missing, a portal that names its own request handle, a bus that goes away while a
panel runs, the colour scheme read through both settings methods, its setting change, and a
change that arrives while a panel is open.
The suite can run twice, once at the default scale and once under a Weston authoritative
output scale of 2, so the integer scale path is exercised. Weston does not advertise
`wp_fractional_scale_v1`, so the fractional case runs under a compositor that does, for
example sway 1.12 with its headless backend and `output HEADLESS-1 scale 1.5`; the suite
then asserts the reported factor, and `ALBOR_EXPECT_SCALE=1.5` makes the assertion exact
rather than a capability skip. The Linux CI jobs are
temporarily disabled; the container workflow above is the local route until a Linux host is
available. A headless compositor has no real input devices or decorations, so a
compositor-driven popup dismissal and the decorated desktop path still need a real Wayland
session; the suite does not claim desktop parity.

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
- **Vulkan** with Wayland/X11 WSI is the GPU path. #110.
- **AT-SPI** accessibility is not implemented. #42.
- **X11** is the Tier 2 adapter and is a separate module.
- A headless Weston has no seat, so clipboard readiness, real input devices and a
  compositor-driven popup dismissal need a desktop session; the suite covers activation's
  no-op fallback, and the popup dismissal rebuild is judged by code plus the framework's
  hide/show popup test.

## CI

The Linux debug and release jobs are **temporarily disabled** in `.github/workflows/build.yml`
until a Linux host can run the full vcpkg build and the platform suite. The `linux-floor`
job runs the same work on `almalinux:9` and is disabled with them. Re-enable the two matrix
entries and remove the `linux-floor` job's `if: false` when the host is ready. The jobs
install the Wayland, Pango, Cairo and D-Bus development packages, run `albor-linux-tests`
after `albor-tests`, and add a second run under a scale-2 Weston. The AlmaLinux job also
ends with `build/check-linux-floor.sh`, which rejects a newer `GLIBC`, `GLIBCXX` or
`CXXABI`.
