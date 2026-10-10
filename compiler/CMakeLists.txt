# the compiler frontend of §11.1, which turns .ui source into a typed ir and then native c++

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-compiler STATIC ${CPP_SOURCES})

# the frontend is included by its own tools and tests, because nothing inside the framework includes it (§2.1)
target_include_directories(albor-compiler PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-compiler PUBLIC cxx_std_23)

# the diagnostics are the only thing every later compiler stage passes around, so the module has no framework dependency (§11.1)
target_link_libraries(albor-compiler PUBLIC albor-core)