# Prototype A1-Graphite-Dawn: one Dawn device for custom geometry and Skia text (AD-28, §30)
# this target is standalone: it reads a1's resource factory but modifies neither a1 nor a1-dawn

if(NOT APPLE)
    message(STATUS "prototype a1-graphite-dawn: skipped, the spike only builds on the macOS platform layer")
    return()
endif()

# find_package(skia) is deprecated by the port itself and raises an author warning, which would
# break the zero warning rule, so use the config package the port documents instead
find_package(unofficial-skia CONFIG REQUIRED)

# dawn's own config package, which is what the port documents
find_package(Dawn CONFIG REQUIRED)

# skia headers self include as "include/core/...", so the port needs its include directory as a
# second root next to the include/skia directory the target already exports
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ALBOR_SPIKE_SKIA_INCLUDE_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
else()
    get_target_property(ALBOR_SPIKE_SKIA_TARGET_INCLUDES unofficial::skia::skia INTERFACE_INCLUDE_DIRECTORIES)
    list(GET ALBOR_SPIKE_SKIA_TARGET_INCLUDES 0 ALBOR_SPIKE_SKIA_INCLUDE_ROOT)
    string(REGEX REPLACE "/skia$" "" ALBOR_SPIKE_SKIA_INCLUDE_ROOT "${ALBOR_SPIKE_SKIA_INCLUDE_ROOT}")
endif()

# the feasibility spike, which keeps its pre-registered T5.1-T5.6 thresholds. it was excluded from the default
# build while the manifest lacked skia's dawn feature and ContextFactory::MakeDawn did not link; the feature is
# enabled now (vcpkg.json), so both drivers build normally
add_executable(albor-prototype-a1-graphite-dawn graphite_dawn_main.cpp)

target_include_directories(albor-prototype-a1-graphite-dawn PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(albor-prototype-a1-graphite-dawn SYSTEM PRIVATE ${ALBOR_SPIKE_SKIA_INCLUDE_ROOT} ${ALBOR_SPIKE_SKIA_INCLUDE_ROOT}/skia)
target_compile_features(albor-prototype-a1-graphite-dawn PRIVATE cxx_std_23)

# the spike reads a1's resource factory for its geometry and typeface, and drives dawn's webgpu directly:
# the feasibility claim is that one device serves both, so the spike links no a1 renderer of its own (AD-06)
target_link_libraries(albor-prototype-a1-graphite-dawn PRIVATE albor-prototype-a1 dawn::webgpu_dawn)

# phase two: the measurement driver. it prices a1's w1 and w2 workloads on the shared device, reads the pixels
# back, and keeps recording, enqueue, gpu fence and readback on separate clocks (§13)
add_executable(albor-prototype-a1-graphite-dawn-bench graphite_dawn_bench.cpp)

target_include_directories(albor-prototype-a1-graphite-dawn-bench PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(albor-prototype-a1-graphite-dawn-bench SYSTEM PRIVATE ${ALBOR_SPIKE_SKIA_INCLUDE_ROOT} ${ALBOR_SPIKE_SKIA_INCLUDE_ROOT}/skia)
target_compile_features(albor-prototype-a1-graphite-dawn-bench PRIVATE cxx_std_23)

target_link_libraries(albor-prototype-a1-graphite-dawn-bench PRIVATE albor-prototype-a1 dawn::webgpu_dawn)

# the font probe: this build's only font manager is SkFontMgr_New_CoreText, and a1's route returns nothing in a
# command-line process. the probe isolates that question from the workloads, which take minutes to build (§21.2)
# it is objective c++ because route B asks whether [NSApplication sharedApplication] is what coretext needs, and
# the a1 and a2 slices enable objcxx the same way
enable_language(OBJCXX)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

add_executable(albor-prototype-a1-graphite-dawn-font-probe font_probe.mm)

target_include_directories(albor-prototype-a1-graphite-dawn-font-probe SYSTEM PRIVATE ${ALBOR_SPIKE_SKIA_INCLUDE_ROOT} ${ALBOR_SPIKE_SKIA_INCLUDE_ROOT}/skia)
target_compile_features(albor-prototype-a1-graphite-dawn-font-probe PRIVATE cxx_std_23)

target_link_libraries(albor-prototype-a1-graphite-dawn-font-probe PRIVATE unofficial::skia::skia "-framework CoreFoundation" "-framework CoreText" "-framework AppKit")
