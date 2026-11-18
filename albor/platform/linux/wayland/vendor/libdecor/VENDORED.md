# Vendored libdecor

The private client-side decoration library the Wayland adapter falls back to when the
compositor does not advertise `xdg-decoration` (§5.3, AD-16). The copy lives with the
adapter rather than in the repository's top-level `third-party/` reference tree, which is
a local, untracked checkout area.

- Upstream: https://gitlab.freedesktop.org/libdecor/libdecor
- Version: **0.2.2**
- License: MIT, see [`LICENSE`](LICENSE)
- Source: the `src/` tree and `README.md` of the 0.2.2 release

## Local changes

- `src/libdecor.c`: `init_plugins` uses the built-in Cairo plugin first and the fallback
  plugin second. The private build links the frame into the adapter, so it loads no
  runtime plugin directory and a consumer never sets `LIBDECOR_PLUGIN_DIR`.
- The Cairo plugin's `libdecor_plugin_description` symbol is renamed to
  `albor_libdecor_cairo_plugin_description` by a compile definition on that source, so the
  plugin can live in the adapter archive without colliding with a system libdecor plugin.
- `config.h` is written for this build: it declares the version, the plugin API version, an
  empty plugin directory, D-Bus support, the Linux file helpers and the vendored
  xdg-shell v6 API.

The build is `albor/platform/linux/wayland/CMakeLists.txt`: the files compile into
`albor-platform-wayland` when `pangocairo`, `wayland-cursor` and `dbus-1` are present. A
host without them keeps the native `xdg-decoration` path.
