# the text engine contract of §9.1, which every shaping backend implements

# gather cpp sources (all platforms), dropping every backend directory because each owns its own target
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# a text backend is a directory below text/, and its sources belong to its own target rather than this one
list(FILTER CPP_SOURCES EXCLUDE REGEX "/text-harfbuzz/")

# create the static library from the gathered cpp sources
add_library(albor-text STATIC ${CPP_SOURCES})

# the contract is header visible, because the standard widgets measure through it (§3.5)
target_include_directories(albor-text PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-text PUBLIC cxx_std_23)

# the text module needs the run type it measures into and nothing else, so it sits below widgets (§3.2)
target_link_libraries(albor-text PUBLIC albor-render)
