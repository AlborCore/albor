# the manifest parser of §11.2, which is the single source of truth for native component types (AD-17)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-manifest STATIC ${CPP_SOURCES})

# the parser is included by its own tools and tests, because nothing inside the framework includes it (§2.1)
target_include_directories(albor-manifest PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-manifest PUBLIC cxx_std_23)

# the manifest reuses the frontend scanner and the shared diagnostics rather than scanning twice (§11.2, AD-17)
target_link_libraries(albor-manifest PUBLIC albor-compiler)
