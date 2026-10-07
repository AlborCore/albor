# the compiled ui example, which generates a component from a ui file at build time (§11.3, AD-03a)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-greeting: skipped, the example needs the macos platform adapter")
    return()
endif()

# the example compiles the rebuilt facade source, which the shared rule generated and wired in (§11.3, AD-03a, §14.2)
add_executable(albor-greeting main.cpp)

# the facade units are generated through the shared rule, which wires them into the example (§11.3, AD-03a, §14.2)
albor_build_ui_component(albor-greeting "${CMAKE_CURRENT_SOURCE_DIR}/greeting_panel.ui")

# the example includes through the project root, which is where the module includes point (§3.5, §11.3)
target_include_directories(albor-greeting PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-greeting PRIVATE cxx_std_23)

# the example reaches the ui tree, the reactive engine, the widgets, the layout and the input path, so they all link explicitly (§3.2)
target_link_libraries(albor-greeting PRIVATE albor-platform-macos albor-render-skia albor-text-skia albor-widgets albor-layout albor-input albor-ui albor-runtime albor-application)