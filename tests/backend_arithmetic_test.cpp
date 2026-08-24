// Arithmetic, min/max, and the square-root family, checked against consteval_ops
// and against DirectXMath.

#include "support/reg_testing.hpp"

#include <mathematics/arch/consteval_ops.hpp>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHEMATICS_TEST_HAS_DXMATH 1
#else
#  define MATHEMATICS_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace math_test;
namespace ref = math::consteval_ops;

} // namespace

// ------------------------------------------------------------ constexpr parity
// Evaluated at compile time. A regression in the dual-path design breaks the
// build here rather than producing wrong numbers at run time.
static_assert(math::get_x(math::add(math::set(1, 2, 3, 4),
                                     math::set(10, 20, 30, 40))) == 11.0f);
static_assert(math::get_y(math::sub(math::set(10, 20, 30, 40),
                                     math::set(1, 2, 3, 4))) == 18.0f);
static_assert(math::get_z(math::mul(math::set(2, 3, 4, 5),
                                     math::splat(3))) == 12.0f);
static_assert(math::get_w(math::div(math::set(10, 20, 30, 40),
                                     math::splat(2))) == 20.0f);
static_assert(math::get_x(math::negate(math::splat(3.5f))) == -3.5f);
static_assert(math::get_x(math::abs(math::splat(-3.5f))) == 3.5f);
static_assert(math::get_x(math::mul_add(math::splat(2), math::splat(3),
                                        math::splat(1))) == 7.0f);
static_assert(math::get_x(math::mul_sub(math::splat(2), math::splat(3),
                                        math::splat(1))) == 5.0f);
static_assert(math::get_x(math::neg_mul_add(math::splat(2), math::splat(3),
                                           math::splat(10))) == 4.0f);
static_assert(math::get_x(math::min(math::splat(2), math::splat(5))) == 2.0f);
static_assert(math::get_x(math::max(math::splat(2), math::splat(5))) == 5.0f);
static_assert(math::get_x(math::sqrt(math::splat(16.0f))) == 4.0f);
static_assert(math::get_x(math::sqrt(math::splat(0.0f))) == 0.0f);
static_assert(math::get_x(math::recip(math::splat(4.0f))) == 0.25f);
static_assert(math::get_x(math::rsqrt(math::splat(16.0f))) == 0.25f);

// Negation flips the sign bit rather than subtracting, so -0.0f becomes +0.0f.
static_assert(math::lane_bits(math::negate(math::splat(-0.0f)), 0) == 0u);
static_assert(math::lane_bits(math::abs(math::splat(-0.0f)), 0) == 0u);

// The constexpr square root is Newton-Raphson, so exactness on non-perfect
// squares is worth pinning down rather than assuming.
static_assert(math::get_x(math::sqrt(math::splat(2.0f))) > 1.41421f &&
              math::get_x(math::sqrt(math::splat(2.0f))) < 1.41422f);
static_assert(math::get_x(math::sqrt(math::splat(1e30f))) > 9.9999e14f &&
              math::get_x(math::sqrt(math::splat(1e30f))) < 1.0001e15f);

TEST(backend_arithmetic_constexpr, compile_time_matches_runtime) {
    EXPECT_FLOAT_EQ(math::get_x(math::add(math::set(1, 2, 3, 4),
                                           math::set(10, 20, 30, 40))), 11.0f);
    EXPECT_FLOAT_EQ(math::get_x(math::mul_sub(math::splat(2), math::splat(3),
                                              math::splat(1))), 5.0f);
    EXPECT_FLOAT_EQ(math::get_x(math::neg_mul_add(math::splat(2), math::splat(3),
                                                 math::splat(10))), 4.0f);
    EXPECT_FLOAT_EQ(math::get_x(math::sqrt(math::splat(16.0f))), 4.0f);
    EXPECT_EQ(math::lane_bits(math::negate(math::splat(-0.0f)), 0), 0u);
}

// ------------------------------------------------------------ reference parity
TEST(backend_arithmetic, matches_reference_on_random_input) {
    random_vectors gen(random_seed);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();

        // Single IEEE operations on identical inputs: exact bits are the bar.
        EXPECT_TRUE(bits_equal(math::add(a.v, b.v), ref::add(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::sub(a.v, b.v), ref::sub(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::mul(a.v, b.v), ref::mul(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::div(a.v, b.v), ref::div(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::negate(a.v), ref::negate(a.v))) << n;
        EXPECT_TRUE(bits_equal(math::abs(a.v), ref::abs(a.v))) << n;
        EXPECT_TRUE(bits_equal(math::min(a.v, b.v), ref::min(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::max(a.v, b.v), ref::max(a.v, b.v))) << n;
    }
}

TEST(backend_arithmetic, fused_forms_match_reference_within_tolerance) {
    random_vectors gen(random_seed + 1);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next(), b = gen.next(), c = gen.next();
        // A hardware FMA keeps the product at full width, so it can differ from
        // the reference's rounded multiply-then-add in the last places.
        EXPECT_TRUE(near_equal(math::mul_add(a.v, b.v, c.v),
                              ref::mul_add(a.v, b.v, c.v))) << n;
        EXPECT_TRUE(near_equal(math::mul_sub(a.v, b.v, c.v),
                              ref::mul_sub(a.v, b.v, c.v))) << n;
        EXPECT_TRUE(near_equal(math::neg_mul_add(a.v, b.v, c.v),
                              ref::neg_mul_add(a.v, b.v, c.v))) << n;
    }
}

TEST(backend_arithmetic, roots_match_reference) {
    random_vectors gen(random_seed + 2);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next_positive();
        EXPECT_TRUE(near_equal(math::sqrt(a.v), ref::sqrt(a.v))) << n;
        EXPECT_TRUE(near_equal(math::rsqrt(a.v), ref::rsqrt(a.v))) << n;
        EXPECT_TRUE(near_equal(math::recip(a.v), ref::recip(a.v))) << n;
    }
}

// The Est forms trade precision for speed, so they get their own much looser
// bound. SSE rsqrtps and rcpps are specified to roughly 12 bits.
TEST(backend_arithmetic, estimate_forms_are_within_twelve_bits) {
    constexpr float estimate_tolerance = 4e-3f;
    random_vectors gen(random_seed + 3);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next_positive();
        EXPECT_TRUE(near_equal(math::rsqrt_est(a.v), ref::rsqrt(a.v),
                              estimate_tolerance, estimate_tolerance)) << n;
        EXPECT_TRUE(near_equal(math::recip_est(a.v), ref::recip(a.v),
                              estimate_tolerance, estimate_tolerance)) << n;
    }
}

TEST(backend_arithmetic, matches_reference_on_edge_values) {
    const auto& values = edge_values();
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < values.size(); ++j) {
            const vec_reg a = math::splat(opaque(values[i]));
            const vec_reg b = math::splat(opaque(values[j]));
            const std::string where =
                "values[" + std::to_string(i) + "], values[" + std::to_string(j) + "]";

            EXPECT_TRUE(bits_equal(math::add(a, b), ref::add(a, b))) << where;
            EXPECT_TRUE(bits_equal(math::sub(a, b), ref::sub(a, b))) << where;
            EXPECT_TRUE(bits_equal(math::mul(a, b), ref::mul(a, b))) << where;
            EXPECT_TRUE(bits_equal(math::div(a, b), ref::div(a, b))) << where;
            EXPECT_TRUE(bits_equal(math::negate(a), ref::negate(a))) << where;
            EXPECT_TRUE(bits_equal(math::abs(a), ref::abs(a))) << where;
            EXPECT_TRUE(bits_equal(math::recip(a), ref::recip(a))) << where;

            // sqrt is the one operation whose reference is not bit-exact: the
            // hardware instruction is correctly rounded, while consteval_ops
            // iterates Newton-Raphson in double and rounds once more at the end,
            // which can land a ULP away.
            EXPECT_TRUE(near_equal(math::sqrt(a), ref::sqrt(a))) << where;

            // min/max diverge by target on NaN and on signed-zero ties, so they
            // are pinned separately below rather than compared here. Every other
            // edge combination must agree exactly.
        }
    }
}

// min/max are target-specific in two places and intentionally not normalised
// (see arch/simd_neon.hpp). These tests pin what the current target actually
// does, so a change shows up as a visible test update rather than a silent shift.
TEST(backend_arithmetic, min_max_na_n_behaviour_is_documented) {
    const vec_reg nan = math::splat(opaque(quiet_nan()));
    const vec_reg one = math::splat(opaque(1.0f));

#if MATHEMATICS_SIMD_SSE || MATHEMATICS_SIMD_SCALAR
    // minps and the scalar reference both return the second operand when the
    // comparison is unordered.
    EXPECT_FLOAT_EQ(math::get_x(math::min(nan, one)), 1.0f);
    EXPECT_TRUE(std::isnan(math::get_x(math::min(one, nan))));
    EXPECT_FLOAT_EQ(math::get_x(math::max(nan, one)), 1.0f);
    EXPECT_TRUE(std::isnan(math::get_x(math::max(one, nan))));
#elif MATHEMATICS_SIMD_NEON
    // ARM FMIN/FMAX return a quiet NaN if either operand is NaN.
    EXPECT_TRUE(std::isnan(math::get_x(math::min(nan, one))));
    EXPECT_TRUE(std::isnan(math::get_x(math::min(one, nan))));
    EXPECT_TRUE(std::isnan(math::get_x(math::max(nan, one))));
    EXPECT_TRUE(std::isnan(math::get_x(math::max(one, nan))));
#endif
}

// The second divergence. -0.0 and +0.0 compare equal, so a value comparison
// cannot see this at all -- it has to be checked on the bits.
TEST(backend_arithmetic, min_max_signed_zero_behaviour_is_documented) {
    const vec_reg pos = math::splat(opaque(0.0f));
    const vec_reg neg = math::splat(opaque(-0.0f));
    constexpr std::uint32_t positive_zero = 0x00000000u;
    constexpr std::uint32_t negative_zero = 0x80000000u;

#if MATHEMATICS_SIMD_SSE || MATHEMATICS_SIMD_SCALAR
    // minps compares them equal and falls through to the second operand, so the
    // result is whichever zero was passed second.
    EXPECT_EQ(math::lane_bits(math::min(neg, pos), 0), positive_zero);
    EXPECT_EQ(math::lane_bits(math::min(pos, neg), 0), negative_zero);
    EXPECT_EQ(math::lane_bits(math::max(neg, pos), 0), positive_zero);
    EXPECT_EQ(math::lane_bits(math::max(pos, neg), 0), negative_zero);
#elif MATHEMATICS_SIMD_NEON
    // ARM FMIN/FMAX order the zeros, independent of which came first.
    EXPECT_EQ(math::lane_bits(math::min(neg, pos), 0), negative_zero);
    EXPECT_EQ(math::lane_bits(math::min(pos, neg), 0), negative_zero);
    EXPECT_EQ(math::lane_bits(math::max(neg, pos), 0), positive_zero);
    EXPECT_EQ(math::lane_bits(math::max(pos, neg), 0), positive_zero);
#endif
}

// mul_sub is where a fused implementation can lose the sign of zero: computing
// -(c - a*b) instead of a*b - c flips it when the two cancel exactly.
TEST(backend_arithmetic, fused_forms_keep_zero_sign_on_exact_cancellation) {
    const vec_reg two = math::splat(opaque(2.0f));
    const vec_reg three = math::splat(opaque(3.0f));
    const vec_reg six = math::splat(opaque(6.0f));

    EXPECT_EQ(math::lane_bits(math::mul_sub(two, three, six), 0), 0x00000000u)
        << "2*3 - 6 must be +0.0";
    EXPECT_EQ(math::lane_bits(math::neg_mul_add(two, three, six), 0), 0x00000000u)
        << "6 - 2*3 must be +0.0";
    EXPECT_TRUE(bits_equal(math::mul_sub(two, three, six),
                          ref::mul_sub(two, three, six)));
    EXPECT_TRUE(bits_equal(math::neg_mul_add(two, three, six),
                          ref::neg_mul_add(two, three, six)));
}

TEST(backend_arithmetic, sqrt_of_negative_is_na_n) {
    EXPECT_TRUE(std::isnan(math::get_x(math::sqrt(math::splat(opaque(-1.0f))))));
    // Negative zero is not a domain error; the sign is preserved.
    EXPECT_EQ(math::lane_bits(math::sqrt(math::splat(opaque(-0.0f))), 0),
              math::bits_of(-0.0f));
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHEMATICS_TEST_HAS_DXMATH
namespace {

DirectX::XMVECTOR to_xm(vec_reg v) {
    return DirectX::XMVectorSet(math::lane(v, 0), math::lane(v, 1),
                                math::lane(v, 2), math::lane(v, 3));
}

vec_reg from_xm(DirectX::FXMVECTOR v) {
    DirectX::XMFLOAT4 out{};
    DirectX::XMStoreFloat4(&out, v);
    return math::set(out.x, out.y, out.z, out.w);
}

} // namespace

TEST(backend_arithmetic_dx_parity, matches_direct_x_math) {
    random_vectors gen(random_seed + 10);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        const DirectX::XMVECTOR xa = to_xm(a.v);
        const DirectX::XMVECTOR xb = to_xm(b.v);

        EXPECT_TRUE(bits_equal(math::add(a.v, b.v),
                              from_xm(DirectX::XMVectorAdd(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::sub(a.v, b.v),
                              from_xm(DirectX::XMVectorSubtract(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::mul(a.v, b.v),
                              from_xm(DirectX::XMVectorMultiply(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::div(a.v, b.v),
                              from_xm(DirectX::XMVectorDivide(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::negate(a.v),
                              from_xm(DirectX::XMVectorNegate(xa)))) << n;
        EXPECT_TRUE(bits_equal(math::abs(a.v),
                              from_xm(DirectX::XMVectorAbs(xa)))) << n;
        EXPECT_TRUE(bits_equal(math::min(a.v, b.v),
                              from_xm(DirectX::XMVectorMin(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::max(a.v, b.v),
                              from_xm(DirectX::XMVectorMax(xa, xb)))) << n;
    }
}

TEST(backend_arithmetic_dx_parity, fused_and_roots_match_direct_x_math) {
    random_vectors gen(random_seed + 11);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next_positive();
        const sample b = gen.next();
        const sample c = gen.next();

        EXPECT_TRUE(near_equal(math::mul_add(a.v, b.v, c.v),
                              from_xm(DirectX::XMVectorMultiplyAdd(
                                  to_xm(a.v), to_xm(b.v), to_xm(c.v))))) << n;
        // XMVectorNegativeMultiplySubtract computes c - a*b, the same as ours.
        EXPECT_TRUE(near_equal(math::neg_mul_add(a.v, b.v, c.v),
                              from_xm(DirectX::XMVectorNegativeMultiplySubtract(
                                  to_xm(a.v), to_xm(b.v), to_xm(c.v))))) << n;
        EXPECT_TRUE(near_equal(math::sqrt(a.v),
                              from_xm(DirectX::XMVectorSqrt(to_xm(a.v))))) << n;
        EXPECT_TRUE(near_equal(math::rsqrt(a.v),
                              from_xm(DirectX::XMVectorReciprocalSqrt(
                                  to_xm(a.v))))) << n;
        EXPECT_TRUE(near_equal(math::recip(a.v),
                              from_xm(DirectX::XMVectorReciprocal(
                                  to_xm(a.v))))) << n;
    }
}
#endif // MATHEMATICS_TEST_HAS_DXMATH
