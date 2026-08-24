cmake_minimum_required(VERSION 3.24)

project(Mathf
    VERSION 0.1.0
    DESCRIPTION "Modern C++20/23 game math library with DirectXMath-class performance"
    LANGUAGES CXX)

# C++20 is the language floor, but our own targets build as C++23 wherever the
# compiler offers it, because `if consteval` is worth real performance on MSVC.
# Under C++20 MSVC falls back to `if (std::is_constant_evaluated())`, and the
# dead compile-time branch -- which reads __m128's float[4] union member -- makes
# it represent the whole object as four scalars, rebuilding and tearing apart the
# vector around every operation. Measured at 1.9x slower than DirectXMath on a
# MulAdd chain; `if consteval` removes it entirely. Clang is unaffected either
# way. See docs/SPIKE-RESULTS.md.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(cxx_std_23 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()

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
# Without this every build enables AVX2, so the SSE2 paths -- the dpps-free dot
# products, the blendv-free Select, the non-FMA multiply-add -- are never
# compiled and never tested.
option(MATHF_BASELINE_SSE2 "Build against SSE2 only, without AVX2 or SSE4.1" OFF)

# Group targets into folders in IDEs that have them (Visual Studio, Xcode).
# Ignored by every other generator.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

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
    # x64 has SSE2 unconditionally, so the baseline build simply omits /arch.
    if(NOT MATHF_FORCE_SCALAR AND NOT MATHF_BASELINE_SSE2 AND MATHF_TARGET_IS_X86)
        target_compile_options(mathf_build_settings INTERFACE /arch:AVX2)
    endif()
    target_compile_options(mathf_build_settings INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:/O2>)
else()
    target_compile_options(mathf_build_settings INTERFACE
        -Wall -Wextra -Wpedantic)
    if(NOT MATHF_FORCE_SCALAR AND NOT MATHF_BASELINE_SSE2 AND MATHF_TARGET_IS_X86)
        target_compile_options(mathf_build_settings INTERFACE -mavx2 -mfma)
    elseif(MATHF_BASELINE_SSE2 AND MATHF_TARGET_IS_X86)
        target_compile_options(mathf_build_settings INTERFACE -msse2 -mno-sse4.1)
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

# ------------------------------------------------------------------ IDE layout
# None of this affects the build. It exists so the generated Visual Studio
# solution is actually usable: a header-only library contributes no project of
# its own, and F5 in a freshly generated solution runs nothing unless a startup
# project is named. Kept at the bottom because it inspects targets the
# subdirectories above create.
if(MATHF_IS_TOP_LEVEL)
    file(GLOB_RECURSE MATHF_PUBLIC_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/mathf/*.hpp")

    # A utility target, not a build target: it compiles nothing and only carries
    # the headers so they are browsable and searchable in Solution Explorer.
    add_custom_target(mathf_headers SOURCES ${MATHF_PUBLIC_HEADERS})
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PREFIX "include" FILES ${MATHF_PUBLIC_HEADERS})

    foreach(mathf_target IN ITEMS
            mathf_headers mathf_config_report mathf_tests mathf_bench)
        if(TARGET ${mathf_target})
            set_target_properties(${mathf_target} PROPERTIES FOLDER "Mathf")
        endif()
    endforeach()

    # Startup project: the tests are what "does it work?" means here.
    if(TARGET mathf_tests)
        set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT mathf_tests)
    elseif(TARGET mathf_config_report)
        set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT mathf_config_report)
    endif()
endif()

message(STATUS "Mathf ${PROJECT_VERSION}")
message(STATUS "  tests=${MATHF_BUILD_TESTS} bench=${MATHF_BUILD_BENCH} tools=${MATHF_BUILD_TOOLS}")
message(STATUS "  force-scalar=${MATHF_FORCE_SCALAR} DirectXMath=${MATHF_HAVE_DIRECTXMATH}")
