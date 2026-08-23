# Test/benchmark dependencies. Pinned to tags so a CI run months from now
# measures the same thing this one did.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Third-party targets are compiled with their own warning policy -- GoogleTest,
# for one, builds with warnings-as-errors. A newer compiler than the release was
# tested against then fails the build on warnings in code we do not own
# (observed: clang-cl rejecting gtest-printers.h for -Wcharacter-conversion).
# Our own targets keep their strict settings; only fetched ones are relaxed.
# Branch on driver style, not compiler id: clang reports CXX_COMPILER_ID "Clang"
# for both clang-cl and clang++, but only the MSVC-style driver accepts /WX-.
# The MSVC variable is true exactly when the driver takes MSVC-style flags.
function(mathf_relax_warnings)
    foreach(target IN LISTS ARGN)
        if(TARGET ${target})
            if(MSVC)
                target_compile_options(${target} PRIVATE /WX-)
            else()
                target_compile_options(${target} PRIVATE -Wno-error)
            endif()
        endif()
    endforeach()
endfunction()

# ------------------------------------------------------------------ GoogleTest
function(mathf_fetch_googletest)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE)
    # Match the CRT the rest of the build uses; mismatches surface as link errors.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    mathf_relax_warnings(gtest gtest_main gmock gmock_main)
endfunction()

# ------------------------------------------------------------- Google Benchmark
function(mathf_fetch_benchmark)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.0
        GIT_SHALLOW    TRUE)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    mathf_relax_warnings(benchmark benchmark_main)
endfunction()

# ------------------------------------------------------------------------- GLM
# Secondary comparison target only. Optional: if the fetch fails the GLM
# benchmarks are skipped rather than breaking the build.
function(mathf_fetch_glm)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glm)
endfunction()
