# Prototype A1: renderer and custom rendering (§30)
# the skia workloads need the macOS platform slice and objective c++, and the vcpkg skia target

if(NOT APPLE)
    message(STATUS "prototype a1: skipped, the skia workloads build on the macOS platform slice")
    return()
endif()

enable_language(OBJCXX)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# find_package(skia) is deprecated by the port itself and raises an author warning, which would
# break the zero warning rule, so use the config package the port documents instead
find_package(unofficial-skia CONFIG REQUIRED)

# skia headers self include as "include/core/...", so the port needs its include directory as a
# second root next to the include/skia directory the target already exports
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ALBOR_SKIA_INCLUDE_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
else()
    get_target_property(ALBOR_SKIA_TARGET_INCLUDES unofficial::skia::skia INTERFACE_INCLUDE_DIRECTORIES)
    list(GET ALBOR_SKIA_TARGET_INCLUDES 0 ALBOR_SKIA_INCLUDE_ROOT)
    string(REGEX REPLACE "/skia$" "" ALBOR_SKIA_INCLUDE_ROOT "${ALBOR_SKIA_INCLUDE_ROOT}")
endif()

add_library(albor-prototype-a1 STATIC
    custom_render_node.cpp
    custom_render_node.h
    gpu_context.h
    gpu_context.macos.mm
    graphite_context.h
    graphite_context.macos.mm
    render_executor.h
    render_executor.macos.mm
    resource_factory.h
    resource_factory.macos.mm
    resource_set.h
    snapshot.h
)

target_include_directories(albor-prototype-a1 PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(albor-prototype-a1 SYSTEM PUBLIC ${ALBOR_SKIA_INCLUDE_ROOT} ${ALBOR_SKIA_INCLUDE_ROOT}/skia)
target_compile_features(albor-prototype-a1 PUBLIC cxx_std_23)
target_link_libraries(albor-prototype-a1 PUBLIC albor-prototype-a2 unofficial::skia::skia "-framework CoreFoundation" "-framework CoreText")

# the driver that runs the recorded phase 1 workload
add_executable(albor-prototype-a1-bench bench.cpp)
target_link_libraries(albor-prototype-a1-bench PRIVATE albor-prototype-a1)

# the driver that runs the recorded workloads w1 to w3
add_executable(albor-prototype-a1-workloads workloads.cpp)
target_link_libraries(albor-prototype-a1-workloads PRIVATE albor-prototype-a1)

# the driver that runs the recorded offscreen graphite slice, which needs no window and no window server
add_executable(albor-prototype-a1-graphite-bench graphite_bench.cpp)
target_link_libraries(albor-prototype-a1-graphite-bench PRIVATE albor-prototype-a1)
