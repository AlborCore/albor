# the collection and record fixtures, whose generated units prove that generated code compiles (§8.1, §11.2, §11.3)

# the static library is declared first, because the generation rules add sources to it (§11.3)
add_library(albor-vector-fixture STATIC vector_fixture.cpp)

# the facade units are generated through the shared rule, with the manifest that declares the records it repeats (§18.1, AD-13)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/task_list.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/task.manifest")

# the fixture includes through the project root, which is where the module includes point (§3.5, §11.3)
target_include_directories(albor-vector-fixture PRIVATE ${CMAKE_SOURCE_DIR})
# the generated directory is on the include path from the start, because the fixture includes its facades (§11.3, §14.2)
target_include_directories(albor-vector-fixture PRIVATE "${CMAKE_BINARY_DIR}/generated/albor-vector-fixture")
target_compile_features(albor-vector-fixture PRIVATE cxx_std_23)

# the record struct is generated from the manifest, which is the single source of truth for its shape (§11.2, AD-17)
albor_build_native_record(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/task.manifest")

# the facade units are generated through the shared rule, which wires them into the fixture (§11.3, §14.2)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/vector_panel.ui")

# the facade units are generated through the shared rule, with the manifest that declares the records it names (§11.2, AD-17)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/record_panel.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/task.manifest")

# the virtualized facade is generated with the manifest, which declares the records it repeats (§8.2, AD-13)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/scroll_log.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/task.manifest")

# the collection facade is generated, which proves a handler mutates a vector property in place (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/collection_panel.ui")

# the struct facade is generated, which proves a ui-declared struct compiles into a facade (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/frame_panel.ui")


# the generated header reaches the test binary, because the suite includes the facade it exercises (§11.3)
target_include_directories(albor-tests PRIVATE "${CMAKE_BINARY_DIR}/generated/albor-vector-fixture")
