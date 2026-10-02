# Prototype D: property and binding engine measurement code (§30)
# this target is standalone: no runtime module links it

add_library(albor-prototype-d STATIC
    binding.h
    engine.cpp
    engine.h
    property.h
    property_id.h
    strategies.cpp
    strategies.h
    store.cpp
    store.h
    strategy.h
    strategy_common.cpp
    strategy_common.h
    value.cpp
    value.h
    workload.cpp
    workload.h
)

target_include_directories(albor-prototype-d PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-prototype-d PUBLIC cxx_std_23)

add_executable(albor-prototype-d-benchmark benchmark.cpp)
target_link_libraries(albor-prototype-d-benchmark PRIVATE albor-prototype-d)
