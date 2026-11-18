/* the configuration of the vendored libdecor 0.2.2 private build (§5.3, AD-16) */
#ifndef ALBOR_LIBDECOR_CONFIG_H
#define ALBOR_LIBDECOR_CONFIG_H

/* the version the private source was taken from */
#define VERSION "0.2.2"

/* the plugin API version the cairo plugin is compiled against */
#define LIBDECOR_PLUGIN_API_VERSION 1

/* the private build has no runtime plugin directory, because the cairo plugin is linked in */
#define LIBDECOR_PLUGIN_DIR ""

/* the cursor settings can follow the desktop through the session bus */
#define HAS_DBUS 1

/* the Linux libc provides the file helpers libdecor uses */
#define HAVE_MKOSTEMP 1
#define HAVE_POSIX_FALLOCATE 1
#define HAVE_MEMFD_CREATE 1

/* the vendored xdg-shell defines version six */
#define HAVE_XDG_SHELL_V6 1

#endif /* ALBOR_LIBDECOR_CONFIG_H */
