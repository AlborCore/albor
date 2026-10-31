# the collection and record fixtures, whose generated units prove that generated code compiles (§8.1, §11.2, §11.3)

# the static library is declared first, because the generation rules add sources to it (§11.3)
add_library(albor-vector-fixture STATIC vector_fixture.cpp)

# the facade units are generated through the shared rule, with the manifest that declares the records it repeats (§18.1, AD-13)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/task_list.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/task.manifest")

# the fixture includes through the project root, which is where the module includes point (§3.5, §11.3)
target_include_directories(albor-vector-fixture PRIVATE ${ALBOR_SOURCE_DIR})
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

# the list facade is generated, which proves a repeat under a list windows into its viewport (§8.2, §18.3)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/list_panel.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/task.manifest")

# the conditional facade is generated, which proves a guarded body and a visible property compile (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/conditional_panel.ui")

# the ordered conditional facade is generated, which proves adjacent guarded bodies keep their order (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/ordered_panel.ui")

# the nested conditional facade is generated, which proves a guarded body inside a guarded body follows both (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/nested_panel.ui")

# the directly nested conditional facade is generated, which proves a conditional body that is a conditional follows both (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/nested_direct_panel.ui")

# the guarded repeat facade is generated, which proves a repeat inside a conditional body materializes with it (§11.1, §18.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/guarded_repeat_panel.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/task.manifest")

# the guarded flat repeat facade is generated, which proves a repeat that is the whole conditional body appears with it (§11.1, §18.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/guarded_flat_repeat_panel.ui" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/task.manifest")

# the collection facade is generated, which proves a handler mutates a vector property in place (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/collection_panel.ui")

# the struct facade is generated, which proves a ui-declared struct compiles into a facade (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/frame_panel.ui")

# the enum facade is generated, which proves a ui-declared enum compiles into a facade (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/status_panel.ui")

# the field-write facade is generated, which proves a handler writes one field and a signal carries a record (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/field_panel.ui")

# the function facade is generated, which proves a ui-declared function compiles into a facade (§11.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/fn_panel.ui")

# the command facade is generated, which proves a command property binds to a widget action (§18.1, AD-15)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/command_panel.ui")

# the image facade is generated, which proves an embedded png compiles into the asset pair (§6.4)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/image_panel.ui")

# the dual-engine facade is generated, which is the compiled half of the AD-25 corpus (§16, AD-25)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/dual_engine_panel.ui")

# the control facade broadens the corpus past the panel, so a checkbox and a slider agree too (§16, AD-25)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/dual_engine_controls.ui")

# the tabs facade broadens the corpus to a container that owns a selection, so the page strip agrees too (§16, AD-25)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/dual_engine_tabs.ui")

# the choices facade covers the single-choice group and the bounded stepper, so both new widgets agree (§16, AD-25)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/dual_engine_choices.ui")

# the image facade carries an embedded asset through both engines, so a retained picture agrees too (§16, AD-25)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/dual_engine_image.ui")

# the p0 facade proves the panel, combo box, icon button and styled field compile into one unit (§8, §9.1, §11.3)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/p0_widgets.ui")

# the netlist facade proves the token ranges and multiline field compile into a fixture (§9.1)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/netlist_editor.ui")

# the generated header reaches the test binary, because the suite includes the facade it exercises (§11.3)
target_include_directories(albor-tests PRIVATE "${CMAKE_BINARY_DIR}/generated/albor-vector-fixture")
