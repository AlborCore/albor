# the render-albor window example, which is hello-window linked against the in-repo backend instead (§6.3, AD-26)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-hello-render: skipped, the example needs the macos platform adapter")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the executable from the gathered cpp sources
add_executable(albor-hello-render ${CPP_SOURCES})

# the example is the composition root, so it wires the platform and the renderer backend together (§6.3)
target_include_directories(albor-hello-render PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-hello-render PRIVATE cxx_std_23)

# the framework modules arrive transitively through the backends, and the application links a text engine even
# without one text draw, so the harfbuzz engine installs itself at load beside the gpu backend (§6.2)
target_link_libraries(albor-hello-render PRIVATE albor-platform-macos-auto albor-render-albor-auto albor-text-harfbuzz-auto)
