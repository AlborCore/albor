# the internationalization catalogue and locale, which translated bindings resolve against (§9.2)

# the portable half names no platform type, so it builds wherever the framework builds (§9.2, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-i18n STATIC ${CPP_SOURCES})

# the module reaches the project root, which is where the includes point (§3.2)
target_include_directories(albor-i18n PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-i18n PUBLIC cxx_std_23)
