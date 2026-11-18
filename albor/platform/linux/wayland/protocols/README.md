# Vendored Wayland protocol definitions

These XML files are the client protocol definitions the Linux adapter generates its bindings from. They are
vendored so the build does not inherit the wayland-protocols package version of the host distribution: the
generated listener structs and request tables stay identical between the RHEL/Alma 9 baseline (wayland-protocols
1.37) and the Ubuntu 24.04 current configuration (1.45).

- Upstream: https://gitlab.freedesktop.org/wayland/wayland-protocols
- Version: 1.37
- License: MIT (the copyright notices are in each file)

| file | upstream path |
| --- | --- |
| `xdg-shell.xml` | `stable/xdg-shell/xdg-shell.xml` |
| `xdg-decoration-unstable-v1.xml` | `unstable/xdg-decoration/xdg-decoration-unstable-v1.xml` |
| `text-input-unstable-v3.xml` | `unstable/text-input/text-input-unstable-v3.xml` |
| `xdg-activation-v1.xml` | `staging/xdg-activation/xdg-activation-v1.xml` |

The bindings are generated at build time by `wayland-scanner`, which still comes from the host's `libwayland`
development package.
