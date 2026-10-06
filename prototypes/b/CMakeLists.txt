# Prototype B: generated-code build performance (§14, §30, AD-03, AD-04a)
# measures clean build time, rebuild scope, write-if-changed, and PropertyId isolation
# this target is standalone: no runtime module links it

# run the generator at configure time so source files exist before the build phase
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_CURRENT_SOURCE_DIR}/generate.py --style generated --out-dir ${CMAKE_CURRENT_SOURCE_DIR}/generated_suite
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE ALBOR_B_GEN_RESULT
    OUTPUT_VARIABLE ALBOR_B_GEN_OUTPUT
    ERROR_VARIABLE ALBOR_B_GEN_ERROR
)

if(NOT ALBOR_B_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "prototype b: generated suite generator failed: ${ALBOR_B_GEN_ERROR}")
endif()

message(STATUS "prototype b: generated suite: ${ALBOR_B_GEN_OUTPUT}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_CURRENT_SOURCE_DIR}/generate.py --style baseline --out-dir ${CMAKE_CURRENT_SOURCE_DIR}/baseline
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE ALBOR_B_BASE_RESULT
    OUTPUT_VARIABLE ALBOR_B_BASE_OUTPUT
    ERROR_VARIABLE ALBOR_B_BASE_ERROR
)

if(NOT ALBOR_B_BASE_RESULT EQUAL 0)
    message(FATAL_ERROR "prototype b: baseline generator failed: ${ALBOR_B_BASE_ERROR}")
endif()

message(STATUS "prototype b: baseline suite: ${ALBOR_B_BASE_OUTPUT}")

# collect all generated .cpp files for the facade library
file(GLOB ALBOR_B_GENERATED_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/generated_suite/*.cpp")

# static library of 500 opaque-impl facade components (the generated style)
add_library(albor-prototype-b-generated STATIC ${ALBOR_B_GENERATED_SRCS})
target_include_directories(albor-prototype-b-generated PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/generated_suite)
target_compile_features(albor-prototype-b-generated PUBLIC cxx_std_23)

# interface (header-only) library for the baseline template-heavy suite
add_library(albor-prototype-b-baseline INTERFACE)
target_include_directories(albor-prototype-b-baseline INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/baseline)

# the generated and baseline headers define the same struct names, so each configuration gets its own driver
# target. they are separate targets rather than one executable because T1 is a comparison of the two clean build
# times, and a single target that links both could only ever be timed as a whole
add_executable(albor-prototype-b-bench-generated bench_generated.cpp)
target_link_libraries(albor-prototype-b-bench-generated PRIVATE albor-prototype-b-generated)

add_executable(albor-prototype-b-bench-baseline bench_baseline.cpp)
target_link_libraries(albor-prototype-b-bench-baseline PRIVATE albor-prototype-b-baseline)

# there is deliberately no combined target linking both drivers: each driver is its own executable with its
# own main, because T1 has to time the two configurations independently. Linking them together could only ever
# be timed as a whole, which is the measurement problem recorded in RESULTS.md. bench.cpp is retained as the
# original combined driver for reference and is not built.
# the root include_directories already covers the source root, which lets the bench TUs
# use "prototypes/b/generated_suite/ComponentNNN.h" and "prototypes/b/baseline/ComponentNNN.h"
