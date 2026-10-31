# the headless platform adapter that has no native window behind it (§3.2, AD-16)

# the adapter reaches no platform api at all, which is what makes it the portable one (§5.3, AD-26)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-platform-headless STATIC ${CPP_SOURCES})

# the adapter implements the framework's platform interfaces, which stay free of any platform type (§3.5, AD-05)
target_include_directories(albor-platform-headless PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-platform-headless PUBLIC cxx_std_23)

# the adapter implements the loop and the window interfaces, which arrive through the application (§3.2)
target_link_libraries(albor-platform-headless PUBLIC albor-window albor-application)

# the auto target links the archive whole, so the adapter installs itself at load without a call (§5.3, AD-16)
add_library(albor-platform-headless-auto INTERFACE)
target_link_libraries(albor-platform-headless-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-platform-headless>")
