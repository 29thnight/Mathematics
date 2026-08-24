include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)
mathematics_fetch_googletest()

include(GoogleTest)

add_executable(mathematics_tests
    config_test.cpp
    backend_arithmetic_test.cpp
    backend_logic_test.cpp
    backend_shuffle_test.cpp
    vector_test.cpp
    vector_geometry_test.cpp
    matrix_test.cpp
    scalar_test.cpp
    quaternion_test.cpp
    transform_test.cpp
    geometry_test.cpp
    format_test.cpp
    guide_test.cpp)

# Lets the test sources include "support/reg_testing.hpp" by that path.
target_include_directories(mathematics_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mathematics_tests
    PRIVATE mathematics mathematics_build_settings mathematics_fp_precise GTest::gtest_main)

# Mathematics is header-only, so instrumenting the test translation units records the
# library's inline code as well. Keep this opt-in and test-local: consumers must
# never inherit gcov flags from the INTERFACE library target.
if(MATHEMATICS_ENABLE_COVERAGE)
    # Keep even unused inline definitions in the gcov denominator. Without
    # this, a header-only API could disappear from both numerator and
    # denominator and make the percentage look healthier than it is.
    target_compile_options(mathematics_tests PRIVATE
        --coverage -fprofile-abs-path -fkeep-inline-functions -O0 -g)
    target_link_options(mathematics_tests PRIVATE --coverage)
endif()

gtest_discover_tests(mathematics_tests)
