# the widget form example, which is the test bed and the showcase every widget the framework ships (§14)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-form: skipped, the example needs the macos platform adapter")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the executable from the gathered cpp sources
add_executable(albor-form ${CPP_SOURCES})

# the backends install themselves at load, and the host owns the wiring, so the example only builds and runs its form (§5.3, §6.3, §9.1, AD-16)
target_link_libraries(albor-form PRIVATE albor-platform-macos-auto albor-render-skia-auto albor-text-skia-auto albor-widgets albor-layout albor-application)

# compile the ui asset into a generated facade, which is what the application instantiates (§11.3, AD-03)
include("${CMAKE_SOURCE_DIR}/build/AlborComponent.cmake")
albor_build_ui_component(albor-form "${CMAKE_CURRENT_SOURCE_DIR}/widget_form.ui")
