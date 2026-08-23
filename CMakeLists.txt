cmake_minimum_required(VERSION 3.24)

project(Mathf
    VERSION 0.1.0
    DESCRIPTION "Modern C++20/23 game math library with DirectXMath-class performance"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Treat Mathf as top-level only when built directly, so add_subdirectory() users
# do not inherit our tests, benchmarks, or warning policy.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MATHF_IS_TOP_LEVEL ON)
else()
    set(MATHF_IS_TOP_LEVEL OFF)
endif()

option(MATHF_BUILD_TESTS  "Build Mathf tests"      ${MATHF_IS_TOP_LEVEL})
option(MATHF_BUILD_BENCH  "Build Mathf benchmarks" ${MATHF_IS_TOP_LEVEL})
option(MATHF_BUILD_TOOLS  "Build Mathf tools"      ${MATHF_IS_TOP_LEVEL})
option(MATHF_FORCE_SCALAR "Force the scalar SIMD fallback (for CI coverage)" OFF)

# ----------------------------------------------------------------- the library
add_library(mathf INTERFACE)
add_library(Mathf::mathf ALIAS mathf)

target_include_directories(mathf INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(mathf INTERFACE cxx_std_20)

if(MATHF_FORCE_SCALAR)
    target_compile_definitions(mathf INTERFACE MATHF_FORCE_SCALAR)
endif()

# ------------------------------------------------------- shared build settings
# Applied to our own targets only, never propagated to consumers of mathf.
add_library(mathf_build_settings INTERFACE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|x64|X64|i[3-6]86")
    set(MATHF_TARGET_IS_X86 ON)
else()
    set(MATHF_TARGET_IS_X86 OFF)
endif()

if(MSVC)
    # /utf-8 is required: without it MSVC reads sources in the system code page
    # and warns (C4819) on any non-ASCII character, including em-dashes in comments.
    target_compile_options(mathf_build_settings INTERFACE
        /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8)
    # AVX2 implies FMA; the codegen spike measured all baselines at this level.
    # Must be gated on the target architecture, not just on MATHF_FORCE_SCALAR:
    # /arch:AVX2 is an x86/x64-only option and cl.exe rejects it outright when
    # targeting ARM64, breaking the build before any NEON code is reached.
    if(NOT MATHF_FORCE_SCALAR AND MATHF_TARGET_IS_X86)
        target_compile_options(mathf_build_settings INTERFACE /arch:AVX2)
    endif()
    target_compile_options(mathf_build_settings INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:/O2>)
else()
    target_compile_options(mathf_build_settings INTERFACE
        -Wall -Wextra -Wpedantic)
    if(NOT MATHF_FORCE_SCALAR AND MATHF_TARGET_IS_X86)
        target_compile_options(mathf_build_settings INTERFACE -mavx2 -mfma)
    endif()
    target_compile_options(mathf_build_settings INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:-O2>)
endif()

# ------------------------------------------------------- floating-point models
# The FP model is chosen per-target, not globally, because tests and benchmarks
# need opposite things.
#
# Benchmarks use fast: it is how games ship and how DirectXMath is normally
# built, so a comparison under anything else would not reflect real use.
#
# Tests use precise: under fast the compiler rewrites the *reference* code the
# tests check against -- contracting a multiply-add into an FMA, or replacing
# division with a reciprocal approximation -- which makes the oracle less
# accurate than the intrinsics under test. Both were observed here: MSVC
# diverging ~8 ULP on add, clang ~2 ULP on div. Mathf's own runtime paths are
# explicit intrinsics and are unaffected by either setting.
add_library(mathf_fp_fast INTERFACE)
add_library(mathf_fp_precise INTERFACE)

if(MSVC)
    target_compile_options(mathf_fp_fast INTERFACE /fp:fast)
    target_compile_options(mathf_fp_precise INTERFACE /fp:precise)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(mathf_fp_fast INTERFACE -ffast-math)
    target_compile_options(mathf_fp_precise INTERFACE -ffp-model=precise)
else()
    # GCC has no -ffp-model=; it rejects the flag outright. The two behaviours
    # the tests actually depend on -- no FMA contraction and no reciprocal
    # approximation for division -- are spelled separately here.
    target_compile_options(mathf_fp_fast INTERFACE -ffast-math)
    target_compile_options(mathf_fp_precise INTERFACE
        -fno-fast-math -ffp-contract=off)
endif()

# ------------------------------------------------------------- DirectXMath dep
# The performance baseline. Present in the Windows SDK, so no fetch is needed;
# absence simply disables the comparison benchmarks rather than failing the build.
include(CheckIncludeFileCXX)
check_include_file_cxx("DirectXMath.h" MATHF_HAVE_DIRECTXMATH)

# ------------------------------------------------------------------ subprojects
if(MATHF_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(MATHF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MATHF_BUILD_BENCH)
    add_subdirectory(bench)
endif()

message(STATUS "Mathf ${PROJECT_VERSION}")
message(STATUS "  tests=${MATHF_BUILD_TESTS} bench=${MATHF_BUILD_BENCH} tools=${MATHF_BUILD_TOOLS}")
message(STATUS "  force-scalar=${MATHF_FORCE_SCALAR} DirectXMath=${MATHF_HAVE_DIRECTXMATH}")
