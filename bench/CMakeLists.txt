include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)
mathematics_fetch_benchmark()

# Placement sampling only; see layout_shift.cpp. Zero builds the ordinary binary.
# When set, layout_shift.cpp must be the first source: contributions to the same
# section keep link order, and the padding has to precede every benchmark.
set(MATHEMATICS_BENCH_TEXT_SHIFT 0 CACHE STRING
    "Bytes to shift the benchmark's .text by, for placement sampling (0 = none)")
set(mathematics_bench_sources baseline_bench.cpp)
if(MSVC AND MATHEMATICS_BENCH_TEXT_SHIFT GREATER 0)
    list(PREPEND mathematics_bench_sources layout_shift.cpp)
endif()

add_executable(mathematics_bench ${mathematics_bench_sources})
target_link_libraries(mathematics_bench
    PRIVATE mathematics mathematics_build_settings mathematics_fp_fast benchmark::benchmark)

add_executable(mathematics_aabb_bench aabb_transform_bench.cpp)
target_link_libraries(mathematics_aabb_bench
    PRIVATE mathematics mathematics_build_settings mathematics_fp_fast benchmark::benchmark)

add_executable(mathematics_easing_tween_bench easing_tween_bench.cpp)
target_link_libraries(mathematics_easing_tween_bench
    PRIVATE mathematics mathematics_build_settings mathematics_fp_fast benchmark::benchmark)

if(MSVC AND MATHEMATICS_BENCH_TEXT_SHIFT GREATER 0)
    target_compile_definitions(mathematics_bench PRIVATE
        MATHEMATICS_BENCH_TEXT_SHIFT=${MATHEMATICS_BENCH_TEXT_SHIFT})
endif()

if(MATHEMATICS_HAVE_DIRECTXMATH)
    target_compile_definitions(mathematics_bench PRIVATE MATHEMATICS_BENCH_HAS_DXMATH=1)
    target_compile_definitions(mathematics_aabb_bench PRIVATE
        MATHEMATICS_BENCH_HAS_DXMATH=1)
    target_compile_definitions(mathematics_easing_tween_bench PRIVATE
        MATHEMATICS_BENCH_HAS_DXMATH=1)
else()
    message(WARNING "DirectXMath not found: the primary performance baseline "
                    "will be missing from benchmark results.")
endif()

# GLM and Sony Vectormath are secondary references -- context for where Mathematics
# and DirectXMath sit, not part of the release gate. Fetching them is
# best-effort so an offline build still produces the DirectXMath comparison that
# does gate releases.
option(MATHEMATICS_BENCH_GLM "Include GLM in benchmark comparisons" ON)
if(MATHEMATICS_BENCH_GLM)
    mathematics_fetch_glm()
    if(TARGET glm::glm)
        target_link_libraries(mathematics_bench PRIVATE glm::glm)
        target_compile_definitions(mathematics_bench PRIVATE
            MATHEMATICS_BENCH_HAS_GLM=1
            GLM_FORCE_INTRINSICS       # GLM is scalar-only unless this is set
            GLM_FORCE_ALIGNED_GENTYPES)
    else()
        message(STATUS "GLM unavailable; skipping its benchmarks.")
    endif()
endif()

# x86 only. Off SSE the library falls back to its own scalar path, which calls
# std::sqrtf, std::sinf and friends -- names that are not in namespace std, so it
# does not compile against libstdc++. That is a defect in Vectormath rather than
# something to work around, and the comparison is only meaningful on x86 anyway,
# since Vectormath is an SSE-oriented library with no NEON backend.
option(MATHEMATICS_BENCH_VECTORMATH "Include Sony Vectormath in benchmark comparisons"
       ${MATHEMATICS_TARGET_IS_X86})
if(MATHEMATICS_BENCH_VECTORMATH AND NOT MATHEMATICS_TARGET_IS_X86)
    message(STATUS "Sony Vectormath requested but the target is not x86; skipping.")
    set(MATHEMATICS_BENCH_VECTORMATH OFF)
endif()

if(MATHEMATICS_BENCH_VECTORMATH)
    mathematics_fetch_vectormath()
    if(TARGET vectormath::vectormath)
        target_link_libraries(mathematics_bench PRIVATE vectormath::vectormath)
        target_compile_definitions(mathematics_bench PRIVATE MATHEMATICS_BENCH_HAS_VECTORMATH=1)
    else()
        message(STATUS "Sony Vectormath unavailable; skipping its benchmarks.")
    endif()
endif()
