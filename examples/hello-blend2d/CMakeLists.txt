# the blend2d window example, which is hello-window linked against the cpu backend instead (§6.3, AD-26)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-hello-blend2d: skipped, the example needs the macos platform adapter")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the executable from the gathered cpp sources
add_executable(albor-hello-blend2d ${CPP_SOURCES})

# the example is the composition root, so it wires the platform and the renderer backend together (§6.3)
target_include_directories(albor-hello-blend2d PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-hello-blend2d PRIVATE cxx_std_23)

# the framework modules arrive transitively through the backends, and the application needs a text engine and a
# renderer even without one text draw, so the skia engine installs itself at load beside the cpu backend (§6.2)
target_link_libraries(albor-hello-blend2d PRIVATE albor-platform-macos-auto albor-render-blend2d-auto albor-text-skia-auto)
