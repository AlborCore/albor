# the collection fixture, whose generated units prove that generated collection code compiles (§8.1, §11.3)

# the static library consumes the generated facade, which is what the test binary links against (§11.3)
add_library(albor-vector-fixture STATIC vector_fixture.cpp)

# the facade units are generated through the shared rule, which wires them into the fixture (§11.3, §14.2)
albor_build_ui_component(albor-vector-fixture "${CMAKE_CURRENT_SOURCE_DIR}/vector_panel.ui")

# the fixture includes through the project root, which is where the module includes point (§3.5, §11.3)
target_include_directories(albor-vector-fixture PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-vector-fixture PRIVATE cxx_std_23)

# the generated header reaches the test binary, because the suite includes the facade it exercises (§11.3)
target_include_directories(albor-tests PRIVATE "${CMAKE_BINARY_DIR}/generated/albor-vector-fixture")
