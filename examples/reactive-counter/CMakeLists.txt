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

# the public target carries the platform and the recommended renderer (§3.3, AD-19)
target_include_directories(albor-reactive-counter PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(albor-reactive-counter PRIVATE cxx_std_23)

# the public target carries the platform, text engine and recommended renderer (§3.3, AD-19)
target_link_libraries(albor-reactive-counter PRIVATE albor)