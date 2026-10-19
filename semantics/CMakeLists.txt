# the unified semantic tree that accessibility and automation both consume (§10.1)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-semantics STATIC ${CPP_SOURCES})

# the semantic model is header visible, because every custom node implements its provider (§3.5)
target_include_directories(albor-semantics PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-semantics PUBLIC cxx_std_23)

# the semantic module needs the shared value types for its bounds and nothing else (§3.2)
target_link_libraries(albor-semantics PUBLIC albor-core)