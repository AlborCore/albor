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

# this target is excluded from the default build on purpose: skia's dawn backend is not compiled into the
# pinned manifest's skia, so ContextFactory::MakeDawn is absent and the link fails. Excluding it keeps the
# default build green and makes the spike's missing capability an explicit, named link failure on request
# rather than a broken tree (prototypes/a1_graphite_dawn/RESULTS.md, T5.1)
message(STATUS "prototype a1-graphite-dawn: excluded from the default build; skia's dawn backend is not in the pinned manifest")
add_executable(albor-prototype-a1-graphite-dawn EXCLUDE_FROM_ALL graphite_dawn_main.cpp)

target_include_directories(albor-prototype-a1-graphite-dawn PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(albor-prototype-a1-graphite-dawn SYSTEM PRIVATE ${ALBOR_SPIKE_SKIA_INCLUDE_ROOT} ${ALBOR_SPIKE_SKIA_INCLUDE_ROOT}/skia)
target_compile_features(albor-prototype-a1-graphite-dawn PRIVATE cxx_std_23)

# the spike reads a1's resource factory for its geometry and typeface, and drives dawn's webgpu directly:
# the feasibility claim is that one device serves both, so the spike links no a1 renderer of its own (AD-06)
target_link_libraries(albor-prototype-a1-graphite-dawn PRIVATE albor-prototype-a1 dawn::webgpu_dawn)
