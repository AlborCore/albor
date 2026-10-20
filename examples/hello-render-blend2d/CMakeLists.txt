# the blend2d window example, which is hello-window linked against the cpu backend instead (§6.3, AD-26)

# the example drives the platform adapter of the host, so it only builds where an adapter exists (AD-16)
if(NOT APPLE AND NOT WIN32)
    message(STATUS "hello-render-blend2d: skipped, the example needs a platform adapter")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the executable from the gathered cpp sources
add_executable(hello-render-blend2d ${CPP_SOURCES})

# the example is the composition root, so it wires the platform and the renderer backend together (§6.3)
target_include_directories(hello-render-blend2d PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(hello-render-blend2d PRIVATE cxx_std_23)

# the platform adapter of the host installs itself at load, so the example names the one the build targets (§5.3, AD-16)
if(APPLE)
    target_link_libraries(hello-render-blend2d PRIVATE albor-platform-macos-auto)
elseif(WIN32)
    target_link_libraries(hello-render-blend2d PRIVATE albor-platform-windows-auto)
endif()

# the framework modules arrive transitively through the backends, and the application needs a text engine and a
# renderer even without one text draw, so the text engine installs itself at load beside the cpu backend (§6.2)
target_link_libraries(hello-render-blend2d PRIVATE albor-render-blend2d-auto albor-text-harfbuzz-auto)
