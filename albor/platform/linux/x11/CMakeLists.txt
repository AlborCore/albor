# the x11 adapter, the tier two linux window system beside wayland (§3.2, AD-16, AD-22)

# the build fact starts off, so a reconfigure without the development packages turns the umbrella off rather than keeping a stale success (§5.3)
set(ALBOR_X11_FOUND OFF CACHE INTERNAL "the x11 adapter was configured" FORCE)

# the adapter is written directly against xcb and xkbcommon, so it only builds on linux (AD-16)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "albor-platform-x11: skipped, the x11 adapter only builds on linux")
    return()
endif()

# the adapter reaches xcb and the shared keymap, which the host's development packages provide (AD-16)
find_package(PkgConfig)
if(NOT PKG_CONFIG_FOUND)
    message(STATUS "albor-platform-x11: skipped, pkg-config is not present")
    return()
endif()

pkg_check_modules(XCB_PC IMPORTED_TARGET xcb)
pkg_check_modules(XKBCOMMON_PC IMPORTED_TARGET xkbcommon)
if(NOT XCB_PC_FOUND OR NOT XKBCOMMON_PC_FOUND)
    message(STATUS "albor-platform-x11: skipped, the xcb or xkbcommon development libraries are not present")
    return()
endif()

# gather the x11 sources, which the .linux infix marks as the ones excluded on every other platform
file(GLOB_RECURSE LINUX_SOURCES "./*.linux.c++")

# gather the portable sources, which the adapter carries for the self install that linking selects (§5.3, AD-16)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-platform-x11 STATIC ${LINUX_SOURCES} ${CPP_SOURCES})

# the adapter implements the framework's platform interfaces, which stay free of xcb types (§3.5, AD-05)
target_include_directories(albor-platform-x11 PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-platform-x11 PUBLIC cxx_std_20)

# the adapter needs the application loop it implements and the shared linux input normalization (§5.3, AD-16)
# albor-window arrives transitively through albor-application, so naming it again would duplicate the archive on the link line
target_link_libraries(albor-platform-x11 PUBLIC albor-application albor-platform-linux-common PkgConfig::XCB_PC PkgConfig::XKBCOMMON_PC)

# the adapter build is published, so the conditional suite and the real runners see it (§5.3, AD-22)
set(ALBOR_X11_FOUND ON CACHE INTERNAL "the x11 adapter was configured" FORCE)

# the auto target links the archive whole, so the adapter installs itself at load without a call, and carries the shared linux mark (§5.3, AD-16)
add_library(albor-platform-x11-auto INTERFACE)
target_link_libraries(albor-platform-x11-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-platform-x11>" albor-platform-linux-common-auto)
