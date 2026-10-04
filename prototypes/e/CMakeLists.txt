# Prototype E: dev runtime and layout invariance (§2.3, §17.2, AD-02, AD-25)
# node.h / node.cpp: the shared Release + Dev node implementation (identical layout, AD-02)

add_library(albor-prototype-e-nodes STATIC
    src/node.cpp
    src/ir_tree.cpp
    src/ui_patch.cpp
)
target_include_directories(albor-prototype-e-nodes PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-prototype-e-nodes PUBLIC cxx_std_23)

# dev-only extension: linked only when ALBOR_DEV is on (AD-02, AD-04)
# the translation unit installs the hook at library load time via a static object constructor
# Release never links this target, so the hook pointer stays null (T7, plan.md)
add_library(albor-prototype-e-devrt STATIC
    src/node_dev.cpp
)
target_link_libraries(albor-prototype-e-devrt PUBLIC albor-prototype-e-nodes)
target_compile_features(albor-prototype-e-devrt PUBLIC cxx_std_23)

# layout_check: built in both Release and Debug; the bench script diffs their outputs (plan.md)
add_executable(albor-prototype-e-layout src/layout_check.cpp)
target_link_libraries(albor-prototype-e-layout PRIVATE albor-prototype-e-nodes)

# link devrt into the Debug build so the hook is non-null, proving the hook slot exists (T7)
if(ALBOR_DEV)
    target_link_libraries(albor-prototype-e-layout PRIVATE albor-prototype-e-devrt)
endif()

# conformance: dual-engine conformance test (AD-25, plan.md T4-T5)
add_executable(albor-prototype-e-conformance src/conformance.cpp)
target_link_libraries(albor-prototype-e-conformance PRIVATE albor-prototype-e-nodes)
