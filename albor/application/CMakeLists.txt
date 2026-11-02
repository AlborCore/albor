# application lifecycle, tasks, timers and lifetime tokens (§3.2)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-application STATIC ${CPP_SOURCES})

# the application interfaces are header visible, because every application names them (§3.5)
target_include_directories(albor-application PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-application PUBLIC cxx_std_23)

# the host joins the window to the tree and the engines, so the wiring modules ride along (§2.2, issue 12)
target_link_libraries(albor-application PUBLIC albor-core albor-window albor-render albor-ui albor-input albor-runtime albor-text albor-commands)
