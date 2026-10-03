# Prototype A1: renderer and custom rendering (§30)
# phase 1 measures the two stage frame, so it needs the macOS platform slice and objective c++

if(NOT APPLE)
    message(STATUS "prototype a1: skipped, phase 1 builds on the macOS platform slice")
    return()
endif()

enable_language(OBJCXX)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

add_library(albor-prototype-a1 STATIC
    render_executor.h
    render_executor.mm
)

target_include_directories(albor-prototype-a1 PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-prototype-a1 PUBLIC cxx_std_23)
target_link_libraries(albor-prototype-a1 PUBLIC albor-prototype-a2)

# the driver that runs the recorded phase 1 workload
add_executable(albor-prototype-a1-bench bench.cpp)
target_link_libraries(albor-prototype-a1-bench PRIVATE albor-prototype-a1)
