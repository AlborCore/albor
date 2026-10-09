# the language server that answers an editor over json-rpc (§12.2)

# the sources name the compiler and the json value, and reach no platform type (§12.2, §16)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-language-server STATIC ${CPP_SOURCES})

# the server reaches the project root, which is where the module includes point (§3.2)
target_include_directories(albor-language-server PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-language-server PUBLIC cxx_std_23)

# the server runs the same compiler the build runs, which is what makes its diagnostics the compiler's (§16)
target_link_libraries(albor-language-server PUBLIC albor-compiler albor-core)
