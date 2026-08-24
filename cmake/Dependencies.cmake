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

# Fetched targets land in the generated Visual Studio solution alongside ours.
# Filing them under one folder keeps Solution Explorer readable; on generators
# without folders the property is simply ignored.
function(mathf_group_third_party)
    foreach(target IN LISTS ARGN)
        if(TARGET ${target})
            set_target_properties(${target} PROPERTIES FOLDER "ThirdParty")
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
    mathf_group_third_party(gtest gtest_main gmock gmock_main)
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
    mathf_group_third_party(benchmark benchmark_main)
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
    mathf_group_third_party(glm)
endfunction()

# ------------------------------------------------------------- Sony Vectormath
# Sony's original release is unmaintained; this is the modernised fork in common
# use. Header-only with no build system of its own, so the target is declared
# here. Secondary comparison target, like GLM: a failed fetch skips its
# benchmarks rather than breaking the build.
function(mathf_fetch_vectormath)
    # Pinned to a commit rather than a branch: this fork publishes no tags, and
    # a moving branch would silently change what the benchmarks measure.
    FetchContent_Declare(vectormath
        GIT_REPOSITORY https://github.com/glampert/vectormath.git
        GIT_TAG        7105ef341303fe83b3dacd6883d9333989126069)
    FetchContent_MakeAvailable(vectormath)

    if(NOT TARGET vectormath::vectormath)
        add_library(vectormath_headers INTERFACE)
        add_library(vectormath::vectormath ALIAS vectormath_headers)
        # SYSTEM so the library's own warnings do not fail our strict build.
        target_include_directories(vectormath_headers SYSTEM INTERFACE
            "${vectormath_SOURCE_DIR}")
    endif()
endfunction()
