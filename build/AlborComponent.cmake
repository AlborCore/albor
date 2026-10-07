# the shared build rules for generated components, which the examples and the fixtures use (§14.2, §14.3)
include_guard(GLOBAL)

# the snake case unit stem of a component class, which is how the generated files are named (§11.3)
function(albor_component_stem out_var class_name)
    # an upper case letter after text starts a word, which the underscore marks (§11.3)
    string(REGEX REPLACE "([a-z0-9])([A-Z])" "\\1_\\2" stem "${class_name}")
    # the letters lower, because the file base is lower case (§11.3)
    string(TOLOWER "${stem}" stem)
    set(${out_var} "${stem}" PARENT_SCOPE)
endfunction()

# generate the facade units of one ui component for a target, which wires them into its sources (§11.3, AD-03a, §14.2)
function(albor_build_ui_component target ui_source)
    # the generated units land in a per target directory, which must not sit in the source tree (§14.2)
    set(generated_dir "${CMAKE_BINARY_DIR}/generated/${target}")
    # the unit stem is the source base, which is the file name the component compiles to (§11.3)
    get_filename_component(stem "${ui_source}" NAME_WE)
    # the facade header and its implementation unit are the outputs, which the build orders on (§14.2)
    set(units "${generated_dir}/${stem}.h" "${generated_dir}/${stem}.cpp")
    # the driver compiles the ui file into the units, which write only when their content changed (§14.2, AD-03a)
    add_custom_command(
        OUTPUT ${units}
        COMMAND $<TARGET_FILE:albor-codegen> --input "${ui_source}" --output "${generated_dir}"
        DEPENDS "${ui_source}" albor-codegen
        COMMENT "${target}: generating ${stem} from ${ui_source}"
        VERBATIM
    )
    # the generated units join the target, which is what compiles them with the rest (§14.1)
    target_sources(${target} PRIVATE ${units})
    # the generated directory joins the include path, which is where the facade header lives (§3.5, §11.3)
    target_include_directories(${target} PRIVATE "${generated_dir}")
endfunction()

# generate the facade units of one native component for a target, which wires them into its sources (§11.2, AD-17, §14.2)
function(albor_build_native_component target manifest_source class_name)
    # the generated units land in a per target directory, which must not sit in the source tree (§14.2)
    set(generated_dir "${CMAKE_BINARY_DIR}/generated/${target}")
    # the module of the unit is the manifest base, which is what the driver reads the module from (§11.2)
    get_filename_component(module "${manifest_source}" NAME_WE)
    # the unit stem is the snake case class, which is how the facade files are named (§11.3)
    albor_component_stem(stem "${class_name}")
    # the facade header and its implementation unit are the outputs, which the build orders on (§14.2)
    set(units "${generated_dir}/${module}/${stem}.h" "${generated_dir}/${module}/${stem}.cpp")
    # the glue driver compiles the manifest into the units, which write only when their content changed (§14.2, AD-17)
    add_custom_command(
        OUTPUT ${units}
        COMMAND $<TARGET_FILE:albor-manifest-glue> --input "${manifest_source}" --output "${generated_dir}"
        DEPENDS "${manifest_source}" albor-manifest-glue
        COMMENT "${target}: generating ${class_name} from ${manifest_source}"
        VERBATIM
    )
    # the generated units join the target, which is what compiles them with the rest (§14.1)
    target_sources(${target} PRIVATE ${units})
    # the generated directory joins the include path, which is where the facade header lives (§3.5, §11.2)
    target_include_directories(${target} PRIVATE "${generated_dir}")
endfunction()