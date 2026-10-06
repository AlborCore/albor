# Prototype D: property and binding engine measurement code (§30)
# this target is standalone: no runtime module links it

# the shipping candidates, linked in every configuration (AD-04)
add_library(albor-prototype-d STATIC
    binding.h
    engine.cpp
    engine.h
    property.h
    property_id.h
    store.cpp
    store.h
    strategies.cpp
    strategies.h
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

# the kahn ordering is a dev and test aid only, so it lives in its own linked target and is
# excluded from release at link time rather than with an #ifdef (AD-04, §2.3)
add_library(albor-prototype-d-topo STATIC
    strategy_dirty_bitset.cpp
    strategy_dirty_bitset.h
)

target_link_libraries(albor-prototype-d-topo PUBLIC albor-prototype-d)
target_compile_features(albor-prototype-d-topo PUBLIC cxx_std_23)

# the measurement tool needs every candidate to compare them, which is why it links the dev unit
add_executable(albor-prototype-d-benchmark benchmark.cpp)
target_link_libraries(albor-prototype-d-benchmark PRIVATE albor-prototype-d albor-prototype-d-topo)

# the release link must not reach the dev only unit, and the symbols have to prove it (§2.3)
add_custom_target(albor-link-boundary ALL
    COMMAND ${CMAKE_COMMAND} -E env sh ${CMAKE_SOURCE_DIR}/build/check-link-boundary.sh $<TARGET_FILE:albor-prototype-d>
    COMMENT "check that the dev only ordering is absent from the shipping library"
    VERBATIM
)
