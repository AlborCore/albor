# the frame scheduler integrated animation core of §7.3, which drives properties through their presence channel

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-animation STATIC ${CPP_SOURCES})

# the animation types are header visible, because the frame pipeline holds an engine (§3.5)
target_include_directories(albor-animation PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-animation PUBLIC cxx_std_23)

# the animation module needs nothing but the standard library, because it only advances time (§3.2, AD-19)