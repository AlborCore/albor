# the widget form example, which is the test bed and the showcase every widget the framework ships (§14)

# the example drives a desktop platform directly, so it only builds where a renderer umbrella exists (AD-16)
# the umbrella is the public target the top level creates for one platform and renderer pair (§3.3, AD-19)
if(NOT TARGET albor AND NOT TARGET albor-blend2d)
    message(STATUS "form: skipped, the example needs a desktop platform and a renderer")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the executable from the gathered cpp sources
add_executable(form-albor ${CPP_SOURCES})
add_executable(form-blend2d ${CPP_SOURCES})

# include directory
target_include_directories(form-albor PRIVATE ${ALBOR_SOURCE_DIR})
target_include_directories(form-blend2d PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(form-albor PRIVATE cxx_std_20)
target_compile_features(form-blend2d PRIVATE cxx_std_20)

# the public target carries the platform, text engine, widgets, layout, accessibility and i18n (§3.3, AD-19)
target_link_libraries(form-albor PRIVATE albor)
target_link_libraries(form-blend2d PRIVATE albor-blend2d)

# compile the ui asset into a generated facade, which is what the application instantiates (§11.3, AD-03)
include("${ALBOR_SOURCE_DIR}/build/AlborComponent.cmake")

# the todo record type for the form
albor_build_native_record(form-albor "${CMAKE_CURRENT_SOURCE_DIR}/todo.manifest")
albor_build_native_record(form-blend2d "${CMAKE_CURRENT_SOURCE_DIR}/todo.manifest")
target_include_directories(form-albor PRIVATE "${CMAKE_BINARY_DIR}/generated/form-albor")
target_include_directories(form-blend2d PRIVATE "${CMAKE_BINARY_DIR}/generated/form-blend2d")

# the form ui facade
albor_build_ui_component(form-albor "${CMAKE_CURRENT_SOURCE_DIR}/widget_form.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/todo.manifest")
albor_build_ui_component(form-blend2d "${CMAKE_CURRENT_SOURCE_DIR}/widget_form.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/todo.manifest")

# when automation is enabled, link the whole-archive umbrella which installs the host at load (§10.3, AD-10)
if(ALBOR_AUTOMATION)
    target_link_libraries(form-albor PRIVATE albor-automation-auto)
    target_link_libraries(form-blend2d PRIVATE albor-automation-auto)
endif()
