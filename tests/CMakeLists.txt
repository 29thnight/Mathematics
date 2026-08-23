include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)
mathf_fetch_googletest()

include(GoogleTest)

add_executable(mathf_tests
    config_test.cpp
    vec_reg_test.cpp)

target_link_libraries(mathf_tests
    PRIVATE mathf mathf_build_settings mathf_fp_precise GTest::gtest_main)

gtest_discover_tests(mathf_tests)
