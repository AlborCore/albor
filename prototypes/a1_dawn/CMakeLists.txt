# Prototype A1-Dawn: dawn (webgpu c++) as a renderer candidate (§30)
# this target deliberately does not link prototype a1: the point of the slice is that dawn needs no skia at all,
# so the only prototype dependency is the a2 platform window it presents through (AD-06)

if(NOT APPLE)
    message(STATUS "prototype a1-dawn: skipped, the dawn slice only builds on the macOS platform layer")
    return()
endif()

enable_language(OBJCXX)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# dawn's own config package, which is what the port documents; find_package(dawn) is not the name it exports
find_package(Dawn CONFIG REQUIRED)

add_library(albor-prototype-a1-dawn STATIC
    dawn_renderer.h
    dawn_renderer.macos.mm
    resources.cpp
    resources.h
)

target_include_directories(albor-prototype-a1-dawn PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-prototype-a1-dawn PUBLIC cxx_std_23)

# the renderer needs dawn and the platform window, and nothing else: no skia, and no prototype a1 library
target_link_libraries(albor-prototype-a1-dawn PUBLIC albor-prototype-a2 dawn::webgpu_dawn "-framework QuartzCore" "-framework Metal")

# the driver that runs the same workloads prototype a1 ran, so the two renderers are comparable
add_executable(albor-prototype-a1-dawn-bench dawn_bench.cpp)
target_link_libraries(albor-prototype-a1-dawn-bench PRIVATE albor-prototype-a1-dawn)