# the codegen driver, which turns a ui file into concrete facade units at build time (§11.3, AD-03a)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the driver executable from the gathered cpp sources
add_executable(albor-codegen ${CPP_SOURCES})

# the driver includes through the project root, which is where the module includes point (§3.5)
target_include_directories(albor-codegen PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-codegen PRIVATE cxx_std_23)

# the driver is a compile time tool, so it links the frontend through the manifest library (§11.2, §11.3)
target_link_libraries(albor-codegen PRIVATE albor-manifest)