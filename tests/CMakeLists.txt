include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)
mathf_fetch_googletest()

include(GoogleTest)

add_executable(mathf_tests
    config_test.cpp
    backend_arithmetic_test.cpp
    backend_logic_test.cpp
    backend_shuffle_test.cpp
    vector_test.cpp
    vector_geometry_test.cpp
    matrix_test.cpp)

# Lets the test sources include "support/reg_testing.hpp" by that path.
target_include_directories(mathf_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mathf_tests
    PRIVATE mathf mathf_build_settings mathf_fp_precise GTest::gtest_main)

gtest_discover_tests(mathf_tests)
