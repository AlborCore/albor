# the two pass layout containers of §8.2, which measure and arrange the ui tree

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-layout STATIC ${CPP_SOURCES})

# the layout containers are header visible, because a ui tree is declared in terms of them (§3.5)
target_include_directories(albor-layout PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-layout PUBLIC cxx_std_23)

# the layout module needs the ui tree it arranges and the size constraints it measures against (§3.2)
target_link_libraries(albor-layout PUBLIC albor-ui)