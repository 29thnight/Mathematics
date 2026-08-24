include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)
mathf_fetch_benchmark()

add_executable(mathf_bench baseline_bench.cpp)
target_link_libraries(mathf_bench
    PRIVATE mathf mathf_build_settings mathf_fp_fast benchmark::benchmark)

if(MATHF_HAVE_DIRECTXMATH)
    target_compile_definitions(mathf_bench PRIVATE MATHF_BENCH_HAS_DXMATH=1)
else()
    message(WARNING "DirectXMath not found: the primary performance baseline "
                    "will be missing from benchmark results.")
endif()

# GLM and Sony Vectormath are secondary references -- context for where Mathf
# and DirectXMath sit, not part of the release gate. Fetching them is
# best-effort so an offline build still produces the DirectXMath comparison that
# does gate releases.
option(MATHF_BENCH_GLM "Include GLM in benchmark comparisons" ON)
if(MATHF_BENCH_GLM)
    mathf_fetch_glm()
    if(TARGET glm::glm)
        target_link_libraries(mathf_bench PRIVATE glm::glm)
        target_compile_definitions(mathf_bench PRIVATE
            MATHF_BENCH_HAS_GLM=1
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
option(MATHF_BENCH_VECTORMATH "Include Sony Vectormath in benchmark comparisons"
       ${MATHF_TARGET_IS_X86})
if(MATHF_BENCH_VECTORMATH AND NOT MATHF_TARGET_IS_X86)
    message(STATUS "Sony Vectormath requested but the target is not x86; skipping.")
    set(MATHF_BENCH_VECTORMATH OFF)
endif()

if(MATHF_BENCH_VECTORMATH)
    mathf_fetch_vectormath()
    if(TARGET vectormath::vectormath)
        target_link_libraries(mathf_bench PRIVATE vectormath::vectormath)
        target_compile_definitions(mathf_bench PRIVATE MATHF_BENCH_HAS_VECTORMATH=1)
    else()
        message(STATUS "Sony Vectormath unavailable; skipping its benchmarks.")
    endif()
endif()
