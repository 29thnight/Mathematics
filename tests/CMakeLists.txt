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
    views_test.cpp
    ranges_test.cpp
    matrix_test.cpp
    mdspan_test.cpp
    scalar_test.cpp
    quaternion_test.cpp
    transform_test.cpp
    geometry_test.cpp
    aabb_transform_test.cpp
    color_rect_test.cpp
    frustum_test.cpp
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
    #
    # -fkeep-inline-functions applies to every inline definition the translation
    # unit sees, libstdc++'s included, and GCC 13's <cmath> has two that do not
    # link. std::nextafter for std::float16_t and std::bfloat16_t open with
    #
    #     if consteval { return __builtin_nextafterf16(__x, __y); }
    #
    # whose own comment says the branch exists because "nothing defines
    # nextafterf16" -- and at -O0 that branch survives into the object file, so
    # the link fails on nextafterf16 and __builtin_nextafterf16b. Nothing here
    # uses either type; both <cmath> blocks are gated on the C++23 predefined
    # macros, so cancelling them removes the broken definitions and leaves the
    # rest of C++23 (mdspan, m[r, c], if consteval) instrumented as before.
    # Fixed in neither GCC 13 nor 14; drop this when the runner's GCC has it.
    target_compile_options(mathematics_tests PRIVATE
        --coverage -fprofile-abs-path -fkeep-inline-functions -O0 -g
        -U__STDCPP_FLOAT16_T__ -U__STDCPP_BFLOAT16_T__)
    # Turns MATHEMATICS_INLINE back into the plain keyword; see config.hpp for
    # why always_inline and -fkeep-inline-functions together make a function's
    # signature line permanently uncoverable.
    target_compile_definitions(mathematics_tests PRIVATE MATHEMATICS_COVERAGE_BUILD)
    target_link_options(mathematics_tests PRIVATE --coverage)
endif()

gtest_discover_tests(mathematics_tests)
