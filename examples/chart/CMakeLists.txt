# the manifest glue example, which generates a native facade from a manifest at build time (§11.2, AD-17)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-chart: skipped, the example needs the macos platform adapter")
    return()
endif()

# the example compiles the application implementation and the generated facade, which the shared rule wired in (§11.2, AD-17, §14.2)
add_executable(albor-chart main.cpp waveform_widget.cpp)

# the facade units are generated through the shared rule, which wires them into the example (§11.2, AD-17, §14.2)
albor_build_native_component(albor-chart "${CMAKE_CURRENT_SOURCE_DIR}/chart.ui-manifest" WaveformView)

# the example includes through the project root, which is where its own headers live (§3.5, §11.2)
target_include_directories(albor-chart PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-chart PRIVATE cxx_std_23)

# the example reaches the platform, the renderers, the ui tree, the reactive engine and the widgets, so they link explicitly (§3.2)
target_link_libraries(albor-chart PRIVATE albor-platform-macos albor-render-skia albor-text-skia albor-widgets albor-layout albor-ui albor-runtime albor-application)