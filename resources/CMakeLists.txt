# the embedded resource registry of §3.2, which carries a build's fonts into its binary (§11.3, AD-34)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-resources STATIC ${CPP_SOURCES})

# the registry is header visible, because generated code registers into it (§3.5, §11.3)
target_include_directories(albor-resources PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-resources PUBLIC cxx_std_23)
