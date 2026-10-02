# Prototype A2: platform presentation, macOS slice (§30)
# this target is standalone: no runtime module links it

# the slice needs objective c++ because the platform layer is written directly against appkit (AD-16)
if(NOT APPLE)
    message(STATUS "prototype a2: skipped, the macOS slice only builds on apple platforms")
    return()
endif()

enable_language(OBJCXX)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

add_library(albor-prototype-a2 STATIC
    platform.h
    platform_mac.mm
)

target_include_directories(albor-prototype-a2 PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-prototype-a2 PUBLIC cxx_std_23)
target_link_libraries(albor-prototype-a2 PUBLIC "-framework AppKit" "-framework QuartzCore" "-framework Metal" "-framework CoreVideo")

# the driver that runs the recorded workload against the platform window
add_executable(albor-prototype-a2-bench bench.cpp)
target_link_libraries(albor-prototype-a2-bench PRIVATE albor-prototype-a2)
