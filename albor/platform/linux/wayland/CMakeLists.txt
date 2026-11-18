# the wayland xdg adapter with its timer pacing (§3.2, AD-16, AD-22)

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

# libdecor draws the client side chrome where the compositor draws none, and the adapter falls back to xdg-decoration when absent (§5.3)
pkg_check_modules(LIBDECOR_PC IMPORTED_TARGET libdecor-0)
if(NOT LIBDECOR_PC_FOUND)
    message(STATUS "albor-platform-wayland: libdecor is not present, the xdg-decoration fallback is used")
endif()

# the scanner generates the protocol bindings from the xml definitions, which the adapter builds against (§5.3)
pkg_get_variable(WAYLAND_SCANNER_BIN wayland-scanner wayland_scanner)
if(NOT WAYLAND_SCANNER_BIN)
    message(STATUS "albor-platform-wayland: skipped, the wayland-scanner tool is not present")
    return()
endif()

# the protocol xml definitions ship in the wayland-protocols package, which carries xdg-shell, xdg-decoration, text-input and xdg-activation (§5.3)
pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML "")
foreach(CANDIDATE_DIR "${WAYLAND_PROTOCOLS_DATA_DIR}" "/usr/share/wayland-protocols" "/usr/share/qt6/wayland/protocols")
    if(XDG_SHELL_XML STREQUAL "" AND EXISTS "${CANDIDATE_DIR}/stable/xdg-shell/xdg-shell.xml")
        set(XDG_SHELL_XML "${CANDIDATE_DIR}/stable/xdg-shell/xdg-shell.xml")
        set(XDG_DECORATION_XML "${CANDIDATE_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
        set(TEXT_INPUT_XML "${CANDIDATE_DIR}/unstable/text-input/text-input-unstable-v3.xml")
        # xdg-activation is stable upstream, while older wayland-protocols ships it under staging
        if(EXISTS "${CANDIDATE_DIR}/stable/xdg-activation/xdg-activation-v1.xml")
            set(XDG_ACTIVATION_XML "${CANDIDATE_DIR}/stable/xdg-activation/xdg-activation-v1.xml")
        else()
            set(XDG_ACTIVATION_XML "${CANDIDATE_DIR}/staging/xdg-activation/xdg-activation-v1.xml")
        endif()
    endif()
endforeach()
if(NOT XDG_SHELL_XML OR NOT EXISTS "${XDG_DECORATION_XML}" OR NOT EXISTS "${TEXT_INPUT_XML}" OR NOT EXISTS "${XDG_ACTIVATION_XML}")
    message(STATUS "albor-platform-wayland: skipped, the wayland protocol definitions are not present")
    return()
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

# create the static library from the gathered sources and the generated bindings
add_library(albor-platform-wayland STATIC ${LINUX_SOURCES} ${CPP_SOURCES} "${XDG_SHELL_CODE}" "${XDG_DECORATION_CODE}" "${TEXT_INPUT_CODE}" "${XDG_ACTIVATION_CODE}")

# the adapter implements the framework's platform interfaces, which stay free of wayland types (§3.5, AD-05)
target_include_directories(albor-platform-wayland PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<BUILD_INTERFACE:${PROTOCOL_OUTPUT_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-platform-wayland PUBLIC cxx_std_23)

# the adapter needs the application loop it implements, and reaches wayland and xkbcommon directly (AD-16)
# albor-window arrives transitively through albor-application, so naming it again would duplicate the archive on the link line
target_link_libraries(albor-platform-wayland PUBLIC albor-application PkgConfig::WAYLAND_CLIENT_PC PkgConfig::XKBCOMMON_PC)
if(LIBDECOR_PC_FOUND)
    # the client side decoration library is optional, and its presence selects the primary decorated path (§5.3)
    target_compile_definitions(albor-platform-wayland PRIVATE ALBOR_WAYLAND_LIBDECOR=1)
    target_link_libraries(albor-platform-wayland PUBLIC PkgConfig::LIBDECOR_PC)
endif()

# the machine generated protocol bindings carry no fixable warnings, which the suppression records (§14.2)
set_source_files_properties("${XDG_SHELL_CODE}" "${XDG_DECORATION_CODE}" "${TEXT_INPUT_CODE}" "${XDG_ACTIVATION_CODE}" PROPERTIES COMPILE_OPTIONS "-w")

# the adapter build is published, so the conditional suite and the real runners see it (§5.3, AD-22)
set(ALBOR_WAYLAND_FOUND ON CACHE INTERNAL "the wayland adapter was configured")
set(ALBOR_WAYLAND_PROTOCOL_HEADER_DIR "${PROTOCOL_OUTPUT_DIR}" CACHE INTERNAL "the directory the generated protocol headers live in")

# the auto target links the archive whole, so the adapter installs itself at load without a call (§5.3, AD-16)
add_library(albor-platform-wayland-auto INTERFACE)
target_link_libraries(albor-platform-wayland-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-platform-wayland>")
