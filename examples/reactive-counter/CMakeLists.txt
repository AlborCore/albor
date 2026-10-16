# the phase 2 reactive counter example (§14, phase 2 deliverable 4)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-reactive-counter: skipped, the example needs the macos platform adapter")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the executable from the gathered cpp sources
add_executable(albor-reactive-counter ${CPP_SOURCES})

# the example is the composition root, so it wires the platform and the renderer backend together (§6.3)
target_include_directories(albor-reactive-counter PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-reactive-counter PRIVATE cxx_std_23)

# the platform carries the host, the input path and the ui tree, so naming the platform and the two backends is the whole link (§3.2)
target_link_libraries(albor-reactive-counter PRIVATE albor-platform-macos-auto albor-render-skia-auto albor-text-skia-auto)