# unit tests for the Phase 0 prototypes
# test files mirror the source tree layout and are named <module>.test.cpp (STYLE-GUIDE.md)

add_executable(albor-tests
    d/property_engine.test.cpp
    d/strategies.test.cpp
    d2/binding_engine.test.cpp
    d2/storage.test.cpp
)

find_package(GTest CONFIG REQUIRED)

target_link_libraries(albor-tests PRIVATE albor-prototype-d albor-prototype-d-topo albor-prototype-d2 GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(albor-tests DISCOVERY_TIMEOUT 60)
