# window roles, modality, handles and the window registry (§3.2)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-window STATIC ${CPP_SOURCES})

# the window interfaces are header visible, because application code names them (§3.5)
target_include_directories(albor-window PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-window PUBLIC cxx_std_23)

# the window module needs the shared value types and nothing else (§3.2)
target_link_libraries(albor-window PUBLIC albor-core)