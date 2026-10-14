# the widget form example, which is the test bed and the showcase every widget the framework ships (§14)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-form: skipped, the example needs the macos platform adapter")
    return()
endif()

# gather cpp sources (all platforms), and leave the drivability choice to the conditional below (§2.3, AD-04, AD-10)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")
list(FILTER CPP_SOURCES EXCLUDE REGEX "drivability(_stub)?\\.cpp$")

# create the executable from the gathered cpp sources
add_executable(albor-form ${CPP_SOURCES})

# the backends install themselves at load, and the host owns the wiring, so the example only builds and runs its form (§5.3, §6.3, §9.1, AD-16)
target_link_libraries(albor-form PRIVATE albor-platform-macos-auto albor-render-skia-auto albor-text-skia-auto albor-widgets albor-layout albor-application albor-accessibility albor-i18n)

# compile the ui asset into a generated facade, which is what the application instantiates (§11.3, AD-03)
include("${CMAKE_SOURCE_DIR}/build/AlborComponent.cmake")
albor_build_ui_component(albor-form "${CMAKE_CURRENT_SOURCE_DIR}/widget_form.ui")

# the alignment panel compiles alongside it, so the two axis properties are proved by the build rather than by a test (§8.2)
albor_build_ui_component(albor-form "${CMAKE_CURRENT_SOURCE_DIR}/align_panel.ui")

# the drivability of an example is a link time choice, because the automation core only exists where it is enabled
# and AD-04 forbids reaching it behind a flag: the example source is byte identical either way (§2.3, AD-04, AD-10)
if(ALBOR_AUTOMATION)
    target_sources(albor-form PRIVATE "./drivability.cpp")
    target_link_libraries(albor-form PRIVATE albor-automation)
else()
    target_sources(albor-form PRIVATE "./drivability_stub.cpp")
endif()
