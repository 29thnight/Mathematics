// Guards the configuration layer: exactly one SIMD backend must be selected, and
// the macros the rest of the library branches on must be internally consistent.
// A wrong answer here silently degrades every operation in the library.

#include <mathematics/config.hpp>

// The umbrella header, included here so that "does mathematics.hpp compile at all"
// is a build failure rather than something a consumer discovers first.
#include <mathematics/mathematics.hpp>

#include <gtest/gtest.h>

TEST(config, exactly_one_backend_is_selected) {
    constexpr int selected = MATHEMATICS_SIMD_SSE + MATHEMATICS_SIMD_NEON + MATHEMATICS_SIMD_SCALAR;
    EXPECT_EQ(selected, 1) << "backend macros must be mutually exclusive";
}

TEST(config, standard_is_at_least_cpp20) {
    EXPECT_GE(MATHEMATICS_CPLUSPLUS, 202002L);
}

TEST(config, architecture_is_recognized) {
#if defined(MATHEMATICS_FORCE_SCALAR)
    GTEST_SKIP() << "scalar build: architecture detection intentionally bypassed";
#else
    EXPECT_EQ(MATHEMATICS_ARCH_X86 + MATHEMATICS_ARCH_ARM64, 1)
        << "unrecognized target architecture";
#endif
}

TEST(config, simd_backend_matches_architecture) {
#if defined(MATHEMATICS_FORCE_SCALAR)
    EXPECT_TRUE(MATHEMATICS_SIMD_SCALAR);
#elif MATHEMATICS_ARCH_X86
    EXPECT_TRUE(MATHEMATICS_SIMD_SSE) << "x86 build fell back to scalar";
#elif MATHEMATICS_ARCH_ARM64
    EXPECT_TRUE(MATHEMATICS_SIMD_NEON) << "ARM64 build fell back to scalar";
#endif
}

TEST(config, isa_feature_flags_are_coherent) {
    // AVX implies SSE4.1; claiming otherwise would misroute the dpps path.
    if (MATHEMATICS_HAS_AVX) EXPECT_TRUE(MATHEMATICS_HAS_SSE4);
    // Feature flags are meaningless without a SIMD backend.
    if (MATHEMATICS_SIMD_SCALAR) {
        EXPECT_FALSE(MATHEMATICS_HAS_SSE4);
        EXPECT_FALSE(MATHEMATICS_HAS_AVX);
    }
}

TEST(config, intrinsic_union_flag_matches_compiler) {
    // clang-cl defines _MSC_VER but uses Clang's native vector __m128, so the
    // flag must be false there. Getting this backwards fails to compile in
    // vec_reg.hpp -- this test documents the intent.
#if defined(__clang__)
    EXPECT_FALSE(MATHEMATICS_MSVC_INTRINSIC_UNION);
#elif defined(_MSC_VER)
    EXPECT_TRUE(MATHEMATICS_MSVC_INTRINSIC_UNION);
#else
    EXPECT_FALSE(MATHEMATICS_MSVC_INTRINSIC_UNION);
#endif
}
