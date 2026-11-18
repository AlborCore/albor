# Linux

The Linux platform adapter is `albor/platform/linux/wayland/`. It targets **Wayland** first
(§5.3, AD-22); X11 remains the Tier 2 fallback and is not implemented yet.

## What ships

- Wayland client with `xdg-shell` toplevels, parented dialogs and tools, and `xdg_popup` +
  `xdg_positioner` popups.
- `libdecor` client-side decorations, with `xdg-decoration` and undecorated fallbacks.
- `xdg-activation-v1` window activation, with a no-op fallback where the compositor does not
  offer it.
- `text-input-v3` IME with the candidate anchor; surrounding text is not sent yet.
- `wl_data_device` text clipboard and xkbcommon key and modifier normalization.
- Integer output scale and timerfd frame pacing.
- A `NativeWindowHandle` carrying `wl_display*` and `wl_surface*` for a future Vulkan WSI.
- The portable **Blend2D** renderer, which presents each finished frame through a `wl_shm`
  buffer. There is no GPU backend on Linux yet; that is #110.
- One display lock shared by the UI thread and the render thread, because both present through
  the same Wayland connection.

The suite runs against a headless Weston compositor. A normal desktop Wayland session is the
real target, and the remote display path below is optional.

## Dependencies

`wayland-client`, `wayland-protocols`, `xkbcommon`, `libdecor-0` and their development
packages. `libdecor` needs one runtime decoration plugin, for example the Cairo or GTK
plugin. The adapter builds without `libdecor` and falls back to `xdg-decoration`, but the
CI installs it so the primary path is built and exercised.

On Debian/Ubuntu:

```sh
sudo apt-get install -y \
    pkg-config cmake ninja-build ccache \
    libwayland-dev wayland-protocols libxkbcommon-dev \
    libdecor-0-dev libdecor-0-plugin-1-cairo \
    weston libgl1-mesa-dri libegl1
```

## Build on Linux

The repository presets use vcpkg. From a clone with `VCPKG_ROOT` set:

```sh
cmake --preset debug
cmake --build --preset debug
./.build-debug/tests/albor-tests
```

The Wayland adapter configures only on Linux and only when the development packages are
present. A configure without them still builds the portable tree and the headless tests.

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

The suite covers window creation, show/hide/resize, metrics, a parented popup, xkb key and
modifier mapping, frame pacing, the clipboard surface, and a real Blend2D frame presented
through `wl_shm`, including a present from a render thread while the UI thread resizes the
window. The suite can run twice, once at the default scale and once under a Weston
authoritative output scale of 2, so the integer scale path is exercised. The Linux CI jobs
are temporarily disabled; the container workflow above is the local route until a Linux
host is available. A headless compositor has no real input devices or decorations, so a
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

- **Fractional scaling** is not implemented; the integer output scale is used. #236.
- **File dialogs and system appearance** go through XDG desktop portals. #234.
- **Platform font resolution** through fontconfig is not implemented; embedded text works. #235.
- **Vulkan** with Wayland/X11 WSI is the GPU path. #110.
- **AT-SPI** accessibility is not implemented. #42.
- **X11** is the Tier 2 adapter and is a separate module.
- A headless Weston has no seat, so clipboard readiness, real input devices and a
  compositor-driven popup dismissal need a desktop session; the suite covers activation's
  no-op fallback, and the popup dismissal rebuild is judged by code plus the framework's
  hide/show popup test.

## CI

The Linux debug and release jobs are **temporarily disabled** in `.github/workflows/build.yml`
until a Linux host can run the full vcpkg build and the platform suite. Re-enable the two
matrix entries when the host is ready. The jobs install the Wayland dependencies, libdecor
and Weston, run `albor-linux-tests` after `albor-tests`, and add a second run under a
scale-2 Weston.
