// Guards the configuration layer: exactly one SIMD backend must be selected, and
// the macros the rest of the library branches on must be internally consistent.
// A wrong answer here silently degrades every operation in the library.

#include <mathf/config.hpp>

// The umbrella header, included here so that "does mathf.hpp compile at all"
// is a build failure rather than something a consumer discovers first.
#include <mathf/mathf.hpp>

#include <gtest/gtest.h>

TEST(Config, ExactlyOneBackendIsSelected) {
    constexpr int selected = MATHF_SIMD_SSE + MATHF_SIMD_NEON + MATHF_SIMD_SCALAR;
    EXPECT_EQ(selected, 1) << "backend macros must be mutually exclusive";
}

TEST(Config, StandardIsAtLeastCpp20) {
    EXPECT_GE(MATHF_CPLUSPLUS, 202002L);
}

TEST(Config, ArchitectureIsRecognized) {
#if defined(MATHF_FORCE_SCALAR)
    GTEST_SKIP() << "scalar build: architecture detection intentionally bypassed";
#else
    EXPECT_EQ(MATHF_ARCH_X86 + MATHF_ARCH_ARM64, 1)
        << "unrecognized target architecture";
#endif
}

TEST(Config, SimdBackendMatchesArchitecture) {
#if defined(MATHF_FORCE_SCALAR)
    EXPECT_TRUE(MATHF_SIMD_SCALAR);
#elif MATHF_ARCH_X86
    EXPECT_TRUE(MATHF_SIMD_SSE) << "x86 build fell back to scalar";
#elif MATHF_ARCH_ARM64
    EXPECT_TRUE(MATHF_SIMD_NEON) << "ARM64 build fell back to scalar";
#endif
}

TEST(Config, IsaFeatureFlagsAreCoherent) {
    // AVX implies SSE4.1; claiming otherwise would misroute the dpps path.
    if (MATHF_HAS_AVX) EXPECT_TRUE(MATHF_HAS_SSE4);
    // Feature flags are meaningless without a SIMD backend.
    if (MATHF_SIMD_SCALAR) {
        EXPECT_FALSE(MATHF_HAS_SSE4);
        EXPECT_FALSE(MATHF_HAS_AVX);
    }
}

TEST(Config, IntrinsicUnionFlagMatchesCompiler) {
    // clang-cl defines _MSC_VER but uses Clang's native vector __m128, so the
    // flag must be false there. Getting this backwards fails to compile in
    // vec_reg.hpp -- this test documents the intent.
#if defined(__clang__)
    EXPECT_FALSE(MATHF_MSVC_INTRINSIC_UNION);
#elif defined(_MSC_VER)
    EXPECT_TRUE(MATHF_MSVC_INTRINSIC_UNION);
#else
    EXPECT_FALSE(MATHF_MSVC_INTRINSIC_UNION);
#endif
}
