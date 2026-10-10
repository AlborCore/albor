# the allocation probe, which counts the heap traffic of a test binary (§2.2)

# the probe replaces the global operators, so it is a library the test binary links and a shipping build never does (§2.3, AD-04)
add_library(albor-alloc-probe STATIC alloc_probe.cpp)

# the tests reach the probe through the project root, which is where the module includes point (§3.2)
target_include_directories(albor-alloc-probe PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-alloc-probe PUBLIC cxx_std_23)
