# the shared value types every other module includes (§3.2)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-core STATIC ${CPP_SOURCES})

# the value types are header visible, because they appear in every public interface (§3.5)
target_include_directories(albor-core PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-core PUBLIC cxx_std_23)