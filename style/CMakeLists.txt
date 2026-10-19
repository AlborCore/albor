# the style module of §6.1, which resolves a role to a value instead of each widget carrying a literal

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-style STATIC ${CPP_SOURCES})

# the theme type is header visible, because widgets resolve their paint through it (§3.5, AD-05)
target_include_directories(albor-style PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-style PUBLIC cxx_std_23)

# the theme is built from geometry and paint values, so it needs only the core types and no platform (§3.5)
target_link_libraries(albor-style PUBLIC albor-core)