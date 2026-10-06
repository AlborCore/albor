# Prototype D2: binding engine follow-up measurement code (§30)
# this target is standalone: no runtime module links it

add_library(albor-prototype-d2 STATIC
    src/cycles.h
    src/graph.cpp
    src/graph.h
    src/index.cpp
    src/index.h
    src/schema.cpp
    src/schema.h
    src/storage.cpp
    src/storage.h
    src/value.cpp
    src/value.h
    src/workloads.h
)

target_include_directories(albor-prototype-d2 PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-prototype-d2 PUBLIC cxx_std_23)

# the measurement tool, which carries the thresholds committed in plan.md before the run (§28.4)
add_executable(albor-prototype-d2-benchmark benchmark.cpp)
target_link_libraries(albor-prototype-d2-benchmark PRIVATE albor-prototype-d2)