# the first vertical slice, which assembles two shared pages and the shell controls into one window (§14)

# the example drives a desktop platform directly, so it only builds where an adapter exists (AD-16)
if(NOT APPLE AND NOT WIN32)
    message(STATUS "albor-vertical-slice: skipped, the example needs the macos/windows platform adapter")
    return()
endif()

# the root source is the composition root, which instantiates the generated facade (§11.3)
add_executable(albor-vertical-slice main.cpp)

# the shared rule generates the facade units, which the example and its imports both use (§11.3, §14.2)
include("${ALBOR_SOURCE_DIR}/build/AlborComponent.cmake")

# the imported pages are generated first, because the root includes their headers (§11.1, §14.3)
albor_build_native_record(albor-vertical-slice "${CMAKE_CURRENT_SOURCE_DIR}/slice_row.manifest")
target_include_directories(albor-vertical-slice PRIVATE "${CMAKE_BINARY_DIR}/generated/albor-vertical-slice")
albor_build_ui_component(albor-vertical-slice "${CMAKE_CURRENT_SOURCE_DIR}/slice_form.ui")
albor_build_ui_component(albor-vertical-slice "${CMAKE_CURRENT_SOURCE_DIR}/slice_table.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/slice_row.manifest")

# the root declares both pages as direct imports, which orders the generated units (§11.1, §14.3)
albor_build_ui_component(albor-vertical-slice "${CMAKE_CURRENT_SOURCE_DIR}/vertical_slice.ui" IMPORTS "slice_form.ui" "slice_table.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/slice_row.manifest")

# the example includes through the project root, which is where the module includes point (§3.5, §11.3)
target_include_directories(albor-vertical-slice PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(albor-vertical-slice PRIVATE cxx_std_23)

# the public target carries the platform, text engine, widgets, layout, commands, accessibility and i18n (§3.3, AD-19)
target_link_libraries(albor-vertical-slice PRIVATE albor)

# when automation is enabled, link the whole-archive umbrella which installs the host at load (§10.3, AD-10)
if(ALBOR_AUTOMATION)
    target_link_libraries(albor-vertical-slice PRIVATE albor-automation-auto)
endif()
