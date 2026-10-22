# the reactive property system, the eager push binding engine and the immutable snapshots (§7)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-runtime STATIC ${CPP_SOURCES})

# the reactive types are header visible, because every component declares properties of them (§3.5)
target_include_directories(albor-runtime PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-runtime PUBLIC cxx_std_23)

# the reactive module needs the shared value types and nothing else, so it sits below application (§3.2)
target_link_libraries(albor-runtime PUBLIC albor-core)