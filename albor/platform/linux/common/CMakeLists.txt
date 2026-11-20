# the input and surface facts every linux adapter shares, which keeps one normalization for wayland and x11 (§5.3, AD-16)

# gather the portable sources, which reach no platform api and therefore build on every host that names the library
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-platform-linux-common STATIC ${CPP_SOURCES})

# the adapter names the framework's event types, which the library publishes (§3.5, AD-05)
target_include_directories(albor-platform-linux-common PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-platform-linux-common PUBLIC cxx_std_20)

# the normalization names the portable event types, which the core module publishes (§3.2)
target_link_libraries(albor-platform-linux-common PUBLIC albor-core)

# the auto target links the archive whole, so the shared install mark is always in the binary either adapter links (§5.3, AD-16)
add_library(albor-platform-linux-common-auto INTERFACE)
target_link_libraries(albor-platform-linux-common-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-platform-linux-common>")
