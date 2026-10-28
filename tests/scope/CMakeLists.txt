# the build-scope fixture, which the §14.3 check builds to observe the recompile set (§14.3)

# the static library consumes the generated facade, which the shared rule wired into it (§11.3, §14.3)
add_library(albor-scope-fixture STATIC scope_fixture.cpp)

# the imported facade is generated first, because the importer compiles against its header (§11.1, §14.3)
albor_build_ui_component(albor-scope-fixture "${CMAKE_CURRENT_SOURCE_DIR}/scope_panel.ui")

# the facade units are generated through the shared rule, which wires them into the fixture (§11.3, §14.3)
albor_build_ui_component(albor-scope-fixture "${CMAKE_CURRENT_SOURCE_DIR}/scope_widget.ui" IMPORTS "scope_panel.ui")

# the fixture includes through the project root, which is where the module includes point (§3.5, §11.3)
target_include_directories(albor-scope-fixture PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(albor-scope-fixture PRIVATE cxx_std_23)