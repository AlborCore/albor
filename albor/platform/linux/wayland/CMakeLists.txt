# the wayland xdg adapter with its timer pacing (§3.2, AD-16, AD-22)

# the build fact starts off, so a reconfigure without the development packages turns the umbrella off rather than keeping a stale success (§5.3)
set(ALBOR_WAYLAND_FOUND OFF CACHE INTERNAL "the wayland adapter was configured" FORCE)

# the adapter is written directly against the wayland client libraries, so it only builds on linux (AD-16)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "albor-platform-wayland: skipped, the wayland adapter only builds on linux")
    return()
endif()

# the adapter reaches the wayland client libraries and xkbcommon, which the host's development packages provide (AD-16)
find_package(PkgConfig)
if(NOT PKG_CONFIG_FOUND)
    message(STATUS "albor-platform-wayland: skipped, pkg-config is not present")
    return()
endif()

pkg_check_modules(WAYLAND_CLIENT_PC IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON_PC IMPORTED_TARGET xkbcommon)
if(NOT WAYLAND_CLIENT_PC_FOUND OR NOT XKBCOMMON_PC_FOUND)
    message(STATUS "albor-platform-wayland: skipped, the wayland or xkbcommon development libraries are not present")
    return()
endif()

# the private libdecor 0.2.2 and its cairo frame draw the client side chrome where the compositor draws none (§5.3, AD-16)
# the frame needs pango/cairo, wayland-cursor and dbus, which a desktop host already has at runtime; a host without them keeps the xdg-decoration fallback
pkg_check_modules(PANGOCAIRO_PC IMPORTED_TARGET pangocairo)
pkg_check_modules(WAYLAND_CURSOR_PC IMPORTED_TARGET wayland-cursor)
pkg_check_modules(DBUS_PC IMPORTED_TARGET dbus-1)
if(PANGOCAIRO_PC_FOUND AND WAYLAND_CURSOR_PC_FOUND AND DBUS_PC_FOUND)
    set(ALBOR_LIBDECOR_AVAILABLE ON)
    set(LIBDECOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libdecor")
    set(LIBDECOR_SOURCES
        "${LIBDECOR_DIR}/src/libdecor.c"
        "${LIBDECOR_DIR}/src/libdecor-fallback.c"
        "${LIBDECOR_DIR}/src/cursor-settings.c"
        "${LIBDECOR_DIR}/src/os-compatibility.c"
        "${LIBDECOR_DIR}/src/plugins/cairo/libdecor-cairo.c"
        "${LIBDECOR_DIR}/src/plugins/common/libdecor-cairo-blur.c")
else()
    set(ALBOR_LIBDECOR_AVAILABLE OFF)
    message(STATUS "albor-platform-wayland: libdecor's cairo dependencies are not present, the xdg-decoration fallback is used")
endif()

# the scanner generates the protocol bindings from the xml definitions, which the adapter builds against (§5.3)
pkg_get_variable(WAYLAND_SCANNER_BIN wayland-scanner wayland_scanner)
if(NOT WAYLAND_SCANNER_BIN)
    message(STATUS "albor-platform-wayland: skipped, the wayland-scanner tool is not present")
    return()
endif()

# the protocol xml definitions are vendored at wayland-protocols 1.37, so the distro package cannot change the generated API (§5.3, AD-16)
set(PROTOCOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/protocols")
set(XDG_SHELL_XML "${PROTOCOLS_DIR}/xdg-shell.xml")
set(XDG_DECORATION_XML "${PROTOCOLS_DIR}/xdg-decoration-unstable-v1.xml")
set(TEXT_INPUT_XML "${PROTOCOLS_DIR}/text-input-unstable-v3.xml")
set(XDG_ACTIVATION_XML "${PROTOCOLS_DIR}/xdg-activation-v1.xml")
if(NOT EXISTS "${XDG_SHELL_XML}" OR NOT EXISTS "${XDG_DECORATION_XML}" OR NOT EXISTS "${TEXT_INPUT_XML}" OR NOT EXISTS "${XDG_ACTIVATION_XML}")
    message(FATAL_ERROR "albor-platform-wayland: the vendored protocol definitions are missing")
endif()

# the protocol bindings are generated once into the build tree, which the sources include through the output directory (§14.2)
set(PROTOCOL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTOCOL_OUTPUT_DIR}")

# the xdg shell bindings, which the window and the role objects ride (§5.3)
set(XDG_SHELL_HEADER "${PROTOCOL_OUTPUT_DIR}/xdg-shell-client-protocol.h")
set(XDG_SHELL_CODE "${PROTOCOL_OUTPUT_DIR}/xdg-shell-protocol.c")
set(XDG_SHELL_SERVER_HEADER "${PROTOCOL_OUTPUT_DIR}/xdg-shell-server-protocol.h")
add_custom_command(OUTPUT "${XDG_SHELL_HEADER}" "${XDG_SHELL_CODE}" "${XDG_SHELL_SERVER_HEADER}" COMMAND "${WAYLAND_SCANNER_BIN}" client-header "${XDG_SHELL_XML}" "${XDG_SHELL_HEADER}" COMMAND "${WAYLAND_SCANNER_BIN}" private-code "${XDG_SHELL_XML}" "${XDG_SHELL_CODE}" COMMAND "${WAYLAND_SCANNER_BIN}" server-header "${XDG_SHELL_XML}" "${XDG_SHELL_SERVER_HEADER}" DEPENDS "${XDG_SHELL_XML}" VERBATIM)

# the xdg decoration bindings, which negotiate the compositor drawn chrome (§5.3)
set(XDG_DECORATION_HEADER "${PROTOCOL_OUTPUT_DIR}/xdg-decoration-client-protocol.h")
set(XDG_DECORATION_CODE "${PROTOCOL_OUTPUT_DIR}/xdg-decoration-protocol.c")
add_custom_command(OUTPUT "${XDG_DECORATION_HEADER}" "${XDG_DECORATION_CODE}" COMMAND "${WAYLAND_SCANNER_BIN}" client-header "${XDG_DECORATION_XML}" "${XDG_DECORATION_HEADER}" COMMAND "${WAYLAND_SCANNER_BIN}" private-code "${XDG_DECORATION_XML}" "${XDG_DECORATION_CODE}" DEPENDS "${XDG_DECORATION_XML}" VERBATIM)

# the text input bindings, which carry the input method session of §8.3
set(TEXT_INPUT_HEADER "${PROTOCOL_OUTPUT_DIR}/text-input-client-protocol.h")
set(TEXT_INPUT_CODE "${PROTOCOL_OUTPUT_DIR}/text-input-protocol.c")
add_custom_command(OUTPUT "${TEXT_INPUT_HEADER}" "${TEXT_INPUT_CODE}" COMMAND "${WAYLAND_SCANNER_BIN}" client-header "${TEXT_INPUT_XML}" "${TEXT_INPUT_HEADER}" COMMAND "${WAYLAND_SCANNER_BIN}" private-code "${TEXT_INPUT_XML}" "${TEXT_INPUT_CODE}" DEPENDS "${TEXT_INPUT_XML}" VERBATIM)

# the activation bindings, which let a compositor raise the window a dialog or a click asked for (§5.1)
set(XDG_ACTIVATION_HEADER "${PROTOCOL_OUTPUT_DIR}/xdg-activation-client-protocol.h")
set(XDG_ACTIVATION_CODE "${PROTOCOL_OUTPUT_DIR}/xdg-activation-protocol.c")
add_custom_command(OUTPUT "${XDG_ACTIVATION_HEADER}" "${XDG_ACTIVATION_CODE}" COMMAND "${WAYLAND_SCANNER_BIN}" client-header "${XDG_ACTIVATION_XML}" "${XDG_ACTIVATION_HEADER}" COMMAND "${WAYLAND_SCANNER_BIN}" private-code "${XDG_ACTIVATION_XML}" "${XDG_ACTIVATION_CODE}" DEPENDS "${XDG_ACTIVATION_XML}" VERBATIM)

# the generated protocol code compiles as c, which the language enables for this adapter
enable_language(C)

# gather the wayland sources, which the .linux infix marks as the ones excluded on every other platform
file(GLOB_RECURSE LINUX_SOURCES "./*.linux.c++")

# gather the portable sources, which the adapter carries for the self install that linking selects (§5.3, AD-16)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources, the private libdecor and the generated bindings
add_library(albor-platform-wayland STATIC ${LINUX_SOURCES} ${CPP_SOURCES} ${LIBDECOR_SOURCES} "${XDG_SHELL_CODE}" "${XDG_DECORATION_CODE}" "${TEXT_INPUT_CODE}" "${XDG_ACTIVATION_CODE}")

# the adapter implements the framework's platform interfaces, which stay free of wayland types (§3.5, AD-05)
target_include_directories(albor-platform-wayland PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<BUILD_INTERFACE:${PROTOCOL_OUTPUT_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-platform-wayland PUBLIC cxx_std_20)
if(ALBOR_LIBDECOR_AVAILABLE)
    # the private libdecor headers stay private to this target, and the cairo plugin's description is renamed (§3.5, AD-05)
    target_include_directories(albor-platform-wayland PRIVATE "${LIBDECOR_DIR}" "${LIBDECOR_DIR}/src" "${LIBDECOR_DIR}/src/plugins")
    target_compile_definitions(albor-platform-wayland PRIVATE _GNU_SOURCE)
    set_source_files_properties("${LIBDECOR_DIR}/src/plugins/cairo/libdecor-cairo.c" PROPERTIES COMPILE_DEFINITIONS "libdecor_plugin_description=albor_libdecor_cairo_plugin_description")
endif()

# the adapter needs the application loop it implements, and reaches wayland and xkbcommon directly (AD-16)
# albor-window arrives transitively through albor-application, so naming it again would duplicate the archive on the link line
target_link_libraries(albor-platform-wayland PUBLIC albor-application PkgConfig::WAYLAND_CLIENT_PC PkgConfig::XKBCOMMON_PC)

# wayland 1.21 added the pointer value120 event, which the 1.20 headers lack (AD-27)
if(WAYLAND_CLIENT_PC_VERSION VERSION_GREATER_EQUAL 1.21)
    target_compile_definitions(albor-platform-wayland PRIVATE ALBOR_WAYLAND_AXIS_VALUE120=1)
endif()

# wayland 1.22 added the surface v6 scale/transform and pointer relative-direction events, which the 1.21 headers lack (AD-27)
if(WAYLAND_CLIENT_PC_VERSION VERSION_GREATER_EQUAL 1.22)
    target_compile_definitions(albor-platform-wayland PRIVATE ALBOR_WAYLAND_SURFACE_V6=1)
endif()
if(ALBOR_LIBDECOR_AVAILABLE)
    # the private libdecor is compiled into the adapter, so the consumer installs no libdecor package or runtime plugin (§5.3)
    target_compile_definitions(albor-platform-wayland PRIVATE ALBOR_WAYLAND_LIBDECOR=1)
    target_link_libraries(albor-platform-wayland PRIVATE PkgConfig::PANGOCAIRO_PC PkgConfig::WAYLAND_CURSOR_PC PkgConfig::DBUS_PC ${CMAKE_DL_LIBS} m)
endif()

# the third party sources and the machine generated bindings carry no fixable warnings, which the suppression records (§14.2)
set_source_files_properties("${XDG_SHELL_CODE}" "${XDG_DECORATION_CODE}" "${TEXT_INPUT_CODE}" "${XDG_ACTIVATION_CODE}" PROPERTIES COMPILE_OPTIONS "-w")
if(ALBOR_LIBDECOR_AVAILABLE)
    set_source_files_properties(${LIBDECOR_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# the adapter build is published, so the conditional suite and the real runners see it (§5.3, AD-22)
set(ALBOR_WAYLAND_FOUND ON CACHE INTERNAL "the wayland adapter was configured" FORCE)
set(ALBOR_WAYLAND_PROTOCOL_HEADER_DIR "${PROTOCOL_OUTPUT_DIR}" CACHE INTERNAL "the directory the generated protocol headers live in")

# the auto target links the archive whole, so the adapter installs itself at load without a call (§5.3, AD-16)
add_library(albor-platform-wayland-auto INTERFACE)
target_link_libraries(albor-platform-wayland-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-platform-wayland>")
