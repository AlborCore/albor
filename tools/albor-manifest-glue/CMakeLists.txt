# the manifest glue driver, which turns a manifest into facade units the application subclasses at build time (§11.2, AD-17)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the driver executable from the gathered cpp sources
add_executable(albor-manifest-glue ${CPP_SOURCES})

# the driver includes through the project root, which is where the module includes point (§3.5)
target_include_directories(albor-manifest-glue PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(albor-manifest-glue PRIVATE cxx_std_23)

# the driver is a compile time tool, so it links the manifest front end through the manifest library (§11.2, AD-17)
target_link_libraries(albor-manifest-glue PRIVATE albor-manifest)