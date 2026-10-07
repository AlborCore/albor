# the compiled ui example, which generates a component from a ui file at build time (§11.3, AD-03a)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-greeting: skipped, the example needs the macos platform adapter")
    return()
endif()

# the directory the generated facade lands in, which must not sit in the source tree (§14.2)
set(GREETING_GENERATED "${CMAKE_BINARY_DIR}/generated/examples/greeting")

# the codegen driver reads the ui file and writes the facade, which is the compile step of a ui (§11.3, AD-03a)
add_custom_command(
    OUTPUT "${GREETING_GENERATED}/greeting_panel.h" "${GREETING_GENERATED}/greeting_panel.cpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GREETING_GENERATED}"
    COMMAND $<TARGET_FILE:albor-codegen> --input "${CMAKE_CURRENT_SOURCE_DIR}/greeting_panel.ui" --output "${GREETING_GENERATED}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/greeting_panel.ui"
    COMMENT "albor-greeting: generating GreetingPanel from greeting_panel.ui"
    VERBATIM
)

# the example compiles the generated source, which proves the emitted facade builds (AD-03a)
add_executable(albor-greeting main.cpp "${GREETING_GENERATED}/greeting_panel.cpp")

# the driver must run before the generated source compiles, which is what orders the build (§11.3)
add_dependencies(albor-greeting albor-codegen)

# the example includes through the project root and the generated directory, which is where the facade lands (§3.5, §11.3)
target_include_directories(albor-greeting PRIVATE ${CMAKE_SOURCE_DIR} "${GREETING_GENERATED}")
target_compile_features(albor-greeting PRIVATE cxx_std_23)

# the example reaches the ui tree, the reactive engine, the widgets and the layout, so they all link explicitly (§3.2)
target_link_libraries(albor-greeting PRIVATE albor-platform-macos albor-render-skia albor-text-skia albor-widgets albor-layout albor-ui albor-runtime albor-application)