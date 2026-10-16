# the phase 1 window example (§14, phase 1 deliverable 4)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-hello-window: skipped, the example needs the macos platform adapter")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the executable from the gathered cpp sources
add_executable(albor-hello-window ${CPP_SOURCES})

# the example is the composition root, so it wires the platform and the renderer backend together (§6.3)
target_include_directories(albor-hello-window PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-hello-window PRIVATE cxx_std_23)

# the framework modules arrive transitively through the backends, and the application needs a text engine and a
# renderer even without one text draw, so the skia engine installs itself at load beside the metal backend (§6.2)
target_link_libraries(albor-hello-window PRIVATE albor-platform-macos-auto albor-render-skia-auto albor-text-skia-auto)