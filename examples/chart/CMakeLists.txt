# the manifest glue example, which generates a native facade from a manifest at build time (§11.2, AD-17)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-chart: skipped, the example needs the macos platform adapter")
    return()
endif()

# the directory the generated facade lands in, which must not sit in the source tree (§14.2)
set(CHART_GENERATED "${CMAKE_BINARY_DIR}/generated/examples/chart")

# the glue driver reads the manifest and writes the facade, which is the compile step of a native type (§11.2, AD-17)
add_custom_command(
    OUTPUT "${CHART_GENERATED}/chart/waveform_view.h" "${CHART_GENERATED}/chart/waveform_view.cpp"
    COMMAND $<TARGET_FILE:albor-manifest-glue> --input "${CMAKE_CURRENT_SOURCE_DIR}/chart.ui-manifest" --output "${CHART_GENERATED}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/chart.ui-manifest"
    COMMENT "albor-chart: generating WaveformView from chart.ui-manifest"
    VERBATIM
)

# the example compiles the generated source and the application implementation, which proves the facade builds (AD-17)
add_executable(albor-chart main.cpp waveform_widget.cpp "${CHART_GENERATED}/chart/waveform_view.cpp")

# the glue driver must run before the generated source compiles, which is what orders the build (§11.2)
add_dependencies(albor-chart albor-manifest-glue)

# the example includes through the project root and the generated directory, which is where the facade lands (§3.5, §11.2)
target_include_directories(albor-chart PRIVATE ${CMAKE_SOURCE_DIR} "${CHART_GENERATED}")
target_compile_features(albor-chart PRIVATE cxx_std_23)

# the example reaches the platform, the renderers, the ui tree, the reactive engine and the widgets, so they link explicitly (§3.2)
target_link_libraries(albor-chart PRIVATE albor-platform-macos albor-render-skia albor-text-skia albor-widgets albor-layout albor-ui albor-runtime albor-application)