# the macos appkit adapter with its display link pacing (§3.2, AD-16)

# the adapter is written directly against appkit, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-platform-macos: skipped, the appkit adapter only builds on apple platforms")
    return()
endif()

# gather macos sources, which the .macos infix marks as the ones excluded on every other platform
file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")

# gather the portable sources, which the adapter carries for the self install that linking selects (§5.3, AD-16)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-platform-macos STATIC ${MACOS_SOURCES} ${CPP_SOURCES})

# the adapter implements the framework's platform interfaces, which stay free of appkit types (§3.5, AD-05)
target_include_directories(albor-platform-macos PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-platform-macos PUBLIC cxx_std_23)

# the adapter owns appkit objects, so arc manages their lifetime rather than hand written retain and release (§5.3)
target_compile_options(albor-platform-macos PRIVATE -fobjc-arc)

# the adapter needs the application loop it implements, and reaches appkit and core video directly (AD-16)
# albor-window arrives transitively through albor-application, so naming it again would duplicate the archive on the link line
target_link_libraries(albor-platform-macos PUBLIC albor-application PRIVATE "-framework AppKit" "-framework QuartzCore" "-framework CoreVideo" "-framework Foundation")

# the auto target links the archive whole, so the adapter installs itself at load without a call (§5.3, AD-16)
add_library(albor-platform-macos-auto INTERFACE)
target_link_libraries(albor-platform-macos-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-platform-macos>")