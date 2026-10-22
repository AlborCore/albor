# the font embedding driver, which turns declared font faces into generated registration units (§11.3, AD-34)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the driver executable from the gathered cpp sources
add_executable(albor-font-embed ${CPP_SOURCES})

# the driver includes through the project root, which is where the module includes point (§3.5)
target_include_directories(albor-font-embed PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(albor-font-embed PRIVATE cxx_std_23)

# the driver is a compile time tool, so it links the manifest library for its parser and diagnostics (§11.2, §11.3)
target_link_libraries(albor-font-embed PRIVATE albor-manifest)
