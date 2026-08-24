// Arithmetic, min/max, and the square-root family, checked against consteval_ops
// and against DirectXMath.

#include "support/reg_testing.hpp"

#include <mathf/arch/consteval_ops.hpp>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHF_TEST_HAS_DXMATH 1
#else
#  define MATHF_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace mathf_test;
namespace ref = mathf::consteval_ops;

} // namespace

// ------------------------------------------------------------ constexpr parity
// Evaluated at compile time. A regression in the dual-path design breaks the
// build here rather than producing wrong numbers at run time.
static_assert(mathf::GetX(mathf::Add(mathf::Set(1, 2, 3, 4),
                                     mathf::Set(10, 20, 30, 40))) == 11.0f);
static_assert(mathf::GetY(mathf::Sub(mathf::Set(10, 20, 30, 40),
                                     mathf::Set(1, 2, 3, 4))) == 18.0f);
static_assert(mathf::GetZ(mathf::Mul(mathf::Set(2, 3, 4, 5),
                                     mathf::Splat(3))) == 12.0f);
static_assert(mathf::GetW(mathf::Div(mathf::Set(10, 20, 30, 40),
                                     mathf::Splat(2))) == 20.0f);
static_assert(mathf::GetX(mathf::Negate(mathf::Splat(3.5f))) == -3.5f);
static_assert(mathf::GetX(mathf::Abs(mathf::Splat(-3.5f))) == 3.5f);
static_assert(mathf::GetX(mathf::MulAdd(mathf::Splat(2), mathf::Splat(3),
                                        mathf::Splat(1))) == 7.0f);
static_assert(mathf::GetX(mathf::MulSub(mathf::Splat(2), mathf::Splat(3),
                                        mathf::Splat(1))) == 5.0f);
static_assert(mathf::GetX(mathf::NegMulAdd(mathf::Splat(2), mathf::Splat(3),
                                           mathf::Splat(10))) == 4.0f);
static_assert(mathf::GetX(mathf::Min(mathf::Splat(2), mathf::Splat(5))) == 2.0f);
static_assert(mathf::GetX(mathf::Max(mathf::Splat(2), mathf::Splat(5))) == 5.0f);
static_assert(mathf::GetX(mathf::Sqrt(mathf::Splat(16.0f))) == 4.0f);
static_assert(mathf::GetX(mathf::Sqrt(mathf::Splat(0.0f))) == 0.0f);
static_assert(mathf::GetX(mathf::Recip(mathf::Splat(4.0f))) == 0.25f);
static_assert(mathf::GetX(mathf::RSqrt(mathf::Splat(16.0f))) == 0.25f);

// Negation flips the sign bit rather than subtracting, so -0.0f becomes +0.0f.
static_assert(mathf::LaneBits(mathf::Negate(mathf::Splat(-0.0f)), 0) == 0u);
static_assert(mathf::LaneBits(mathf::Abs(mathf::Splat(-0.0f)), 0) == 0u);

// The constexpr square root is Newton-Raphson, so exactness on non-perfect
// squares is worth pinning down rather than assuming.
static_assert(mathf::GetX(mathf::Sqrt(mathf::Splat(2.0f))) > 1.41421f &&
              mathf::GetX(mathf::Sqrt(mathf::Splat(2.0f))) < 1.41422f);
static_assert(mathf::GetX(mathf::Sqrt(mathf::Splat(1e30f))) > 9.9999e14f &&
              mathf::GetX(mathf::Sqrt(mathf::Splat(1e30f))) < 1.0001e15f);

TEST(BackendArithmeticConstexpr, CompileTimeMatchesRuntime) {
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Add(mathf::Set(1, 2, 3, 4),
                                           mathf::Set(10, 20, 30, 40))), 11.0f);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::MulSub(mathf::Splat(2), mathf::Splat(3),
                                              mathf::Splat(1))), 5.0f);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::NegMulAdd(mathf::Splat(2), mathf::Splat(3),
                                                 mathf::Splat(10))), 4.0f);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Sqrt(mathf::Splat(16.0f))), 4.0f);
    EXPECT_EQ(mathf::LaneBits(mathf::Negate(mathf::Splat(-0.0f)), 0), 0u);
}

// ------------------------------------------------------------ reference parity
TEST(BackendArithmetic, MatchesReferenceOnRandomInput) {
    RandomVectors gen(kSeed);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();

        // Single IEEE operations on identical inputs: exact bits are the bar.
        EXPECT_TRUE(BitsEqual(mathf::Add(a.v, b.v), ref::Add(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Sub(a.v, b.v), ref::Sub(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Mul(a.v, b.v), ref::Mul(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Div(a.v, b.v), ref::Div(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Negate(a.v), ref::Negate(a.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Abs(a.v), ref::Abs(a.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Min(a.v, b.v), ref::Min(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Max(a.v, b.v), ref::Max(a.v, b.v))) << n;
    }
}

TEST(BackendArithmetic, FusedFormsMatchReferenceWithinTolerance) {
    RandomVectors gen(kSeed + 1);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next(), b = gen.Next(), c = gen.Next();
        // A hardware FMA keeps the product at full width, so it can differ from
        // the reference's rounded multiply-then-add in the last places.
        EXPECT_TRUE(NearEqual(mathf::MulAdd(a.v, b.v, c.v),
                              ref::MulAdd(a.v, b.v, c.v))) << n;
        EXPECT_TRUE(NearEqual(mathf::MulSub(a.v, b.v, c.v),
                              ref::MulSub(a.v, b.v, c.v))) << n;
        EXPECT_TRUE(NearEqual(mathf::NegMulAdd(a.v, b.v, c.v),
                              ref::NegMulAdd(a.v, b.v, c.v))) << n;
    }
}

TEST(BackendArithmetic, RootsMatchReference) {
    RandomVectors gen(kSeed + 2);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.NextPositive();
        EXPECT_TRUE(NearEqual(mathf::Sqrt(a.v), ref::Sqrt(a.v))) << n;
        EXPECT_TRUE(NearEqual(mathf::RSqrt(a.v), ref::RSqrt(a.v))) << n;
        EXPECT_TRUE(NearEqual(mathf::Recip(a.v), ref::Recip(a.v))) << n;
    }
}

// The Est forms trade precision for speed, so they get their own much looser
// bound. SSE rsqrtps and rcpps are specified to roughly 12 bits.
TEST(BackendArithmetic, EstimateFormsAreWithinTwelveBits) {
    constexpr float kEstTolerance = 4e-3f;
    RandomVectors gen(kSeed + 3);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.NextPositive();
        EXPECT_TRUE(NearEqual(mathf::RSqrtEst(a.v), ref::RSqrt(a.v),
                              kEstTolerance, kEstTolerance)) << n;
        EXPECT_TRUE(NearEqual(mathf::RecipEst(a.v), ref::Recip(a.v),
                              kEstTolerance, kEstTolerance)) << n;
    }
}

TEST(BackendArithmetic, MatchesReferenceOnEdgeValues) {
    const auto& values = EdgeValues();
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < values.size(); ++j) {
            const VecReg a = mathf::Splat(Opaque(values[i]));
            const VecReg b = mathf::Splat(Opaque(values[j]));
            const std::string where =
                "values[" + std::to_string(i) + "], values[" + std::to_string(j) + "]";

            EXPECT_TRUE(BitsEqual(mathf::Add(a, b), ref::Add(a, b))) << where;
            EXPECT_TRUE(BitsEqual(mathf::Sub(a, b), ref::Sub(a, b))) << where;
            EXPECT_TRUE(BitsEqual(mathf::Mul(a, b), ref::Mul(a, b))) << where;
            EXPECT_TRUE(BitsEqual(mathf::Div(a, b), ref::Div(a, b))) << where;
            EXPECT_TRUE(BitsEqual(mathf::Negate(a), ref::Negate(a))) << where;
            EXPECT_TRUE(BitsEqual(mathf::Abs(a), ref::Abs(a))) << where;
            EXPECT_TRUE(BitsEqual(mathf::Recip(a), ref::Recip(a))) << where;

            // Sqrt is the one operation whose reference is not bit-exact: the
            // hardware instruction is correctly rounded, while consteval_ops
            // iterates Newton-Raphson in double and rounds once more at the end,
            // which can land a ULP away.
            EXPECT_TRUE(NearEqual(mathf::Sqrt(a), ref::Sqrt(a))) << where;

            // Min/Max diverge by target on NaN and on signed-zero ties, so they
            // are pinned separately below rather than compared here. Every other
            // edge combination must agree exactly.
        }
    }
}

// Min/Max are target-specific in two places and intentionally not normalised
// (see arch/simd_neon.hpp). These tests pin what the current target actually
// does, so a change shows up as a visible test update rather than a silent shift.
TEST(BackendArithmetic, MinMaxNaNBehaviourIsDocumented) {
    const VecReg nan = mathf::Splat(Opaque(QuietNaN()));
    const VecReg one = mathf::Splat(Opaque(1.0f));

#if MATHF_SIMD_SSE || MATHF_SIMD_SCALAR
    // minps and the scalar reference both return the second operand when the
    // comparison is unordered.
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Min(nan, one)), 1.0f);
    EXPECT_TRUE(std::isnan(mathf::GetX(mathf::Min(one, nan))));
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Max(nan, one)), 1.0f);
    EXPECT_TRUE(std::isnan(mathf::GetX(mathf::Max(one, nan))));
#elif MATHF_SIMD_NEON
    // ARM FMIN/FMAX return a quiet NaN if either operand is NaN.
    EXPECT_TRUE(std::isnan(mathf::GetX(mathf::Min(nan, one))));
    EXPECT_TRUE(std::isnan(mathf::GetX(mathf::Min(one, nan))));
    EXPECT_TRUE(std::isnan(mathf::GetX(mathf::Max(nan, one))));
    EXPECT_TRUE(std::isnan(mathf::GetX(mathf::Max(one, nan))));
#endif
}

// The second divergence. -0.0 and +0.0 compare equal, so a value comparison
// cannot see this at all -- it has to be checked on the bits.
TEST(BackendArithmetic, MinMaxSignedZeroBehaviourIsDocumented) {
    const VecReg pos = mathf::Splat(Opaque(0.0f));
    const VecReg neg = mathf::Splat(Opaque(-0.0f));
    constexpr std::uint32_t kPosZero = 0x00000000u;
    constexpr std::uint32_t kNegZero = 0x80000000u;

#if MATHF_SIMD_SSE || MATHF_SIMD_SCALAR
    // minps compares them equal and falls through to the second operand, so the
    // result is whichever zero was passed second.
    EXPECT_EQ(mathf::LaneBits(mathf::Min(neg, pos), 0), kPosZero);
    EXPECT_EQ(mathf::LaneBits(mathf::Min(pos, neg), 0), kNegZero);
    EXPECT_EQ(mathf::LaneBits(mathf::Max(neg, pos), 0), kPosZero);
    EXPECT_EQ(mathf::LaneBits(mathf::Max(pos, neg), 0), kNegZero);
#elif MATHF_SIMD_NEON
    // ARM FMIN/FMAX order the zeros, independent of which came first.
    EXPECT_EQ(mathf::LaneBits(mathf::Min(neg, pos), 0), kNegZero);
    EXPECT_EQ(mathf::LaneBits(mathf::Min(pos, neg), 0), kNegZero);
    EXPECT_EQ(mathf::LaneBits(mathf::Max(neg, pos), 0), kPosZero);
    EXPECT_EQ(mathf::LaneBits(mathf::Max(pos, neg), 0), kPosZero);
#endif
}

// MulSub is where a fused implementation can lose the sign of zero: computing
// -(c - a*b) instead of a*b - c flips it when the two cancel exactly.
TEST(BackendArithmetic, FusedFormsKeepZeroSignOnExactCancellation) {
    const VecReg two = mathf::Splat(Opaque(2.0f));
    const VecReg three = mathf::Splat(Opaque(3.0f));
    const VecReg six = mathf::Splat(Opaque(6.0f));

    EXPECT_EQ(mathf::LaneBits(mathf::MulSub(two, three, six), 0), 0x00000000u)
        << "2*3 - 6 must be +0.0";
    EXPECT_EQ(mathf::LaneBits(mathf::NegMulAdd(two, three, six), 0), 0x00000000u)
        << "6 - 2*3 must be +0.0";
    EXPECT_TRUE(BitsEqual(mathf::MulSub(two, three, six),
                          ref::MulSub(two, three, six)));
    EXPECT_TRUE(BitsEqual(mathf::NegMulAdd(two, three, six),
                          ref::NegMulAdd(two, three, six)));
}

TEST(BackendArithmetic, SqrtOfNegativeIsNaN) {
    EXPECT_TRUE(std::isnan(mathf::GetX(mathf::Sqrt(mathf::Splat(Opaque(-1.0f))))));
    // Negative zero is not a domain error; the sign is preserved.
    EXPECT_EQ(mathf::LaneBits(mathf::Sqrt(mathf::Splat(Opaque(-0.0f))), 0),
              mathf::BitsOf(-0.0f));
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHF_TEST_HAS_DXMATH
namespace {

DirectX::XMVECTOR ToXm(VecReg v) {
    return DirectX::XMVectorSet(mathf::Lane(v, 0), mathf::Lane(v, 1),
                                mathf::Lane(v, 2), mathf::Lane(v, 3));
}

VecReg FromXm(DirectX::FXMVECTOR v) {
    DirectX::XMFLOAT4 out{};
    DirectX::XMStoreFloat4(&out, v);
    return mathf::Set(out.x, out.y, out.z, out.w);
}

} // namespace

TEST(BackendArithmeticDxParity, MatchesDirectXMath) {
    RandomVectors gen(kSeed + 10);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        const DirectX::XMVECTOR xa = ToXm(a.v);
        const DirectX::XMVECTOR xb = ToXm(b.v);

        EXPECT_TRUE(BitsEqual(mathf::Add(a.v, b.v),
                              FromXm(DirectX::XMVectorAdd(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Sub(a.v, b.v),
                              FromXm(DirectX::XMVectorSubtract(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Mul(a.v, b.v),
                              FromXm(DirectX::XMVectorMultiply(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Div(a.v, b.v),
                              FromXm(DirectX::XMVectorDivide(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Negate(a.v),
                              FromXm(DirectX::XMVectorNegate(xa)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Abs(a.v),
                              FromXm(DirectX::XMVectorAbs(xa)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Min(a.v, b.v),
                              FromXm(DirectX::XMVectorMin(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Max(a.v, b.v),
                              FromXm(DirectX::XMVectorMax(xa, xb)))) << n;
    }
}

TEST(BackendArithmeticDxParity, FusedAndRootsMatchDirectXMath) {
    RandomVectors gen(kSeed + 11);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.NextPositive();
        const Sample b = gen.Next();
        const Sample c = gen.Next();

        EXPECT_TRUE(NearEqual(mathf::MulAdd(a.v, b.v, c.v),
                              FromXm(DirectX::XMVectorMultiplyAdd(
                                  ToXm(a.v), ToXm(b.v), ToXm(c.v))))) << n;
        // XMVectorNegativeMultiplySubtract computes c - a*b, the same as ours.
        EXPECT_TRUE(NearEqual(mathf::NegMulAdd(a.v, b.v, c.v),
                              FromXm(DirectX::XMVectorNegativeMultiplySubtract(
                                  ToXm(a.v), ToXm(b.v), ToXm(c.v))))) << n;
        EXPECT_TRUE(NearEqual(mathf::Sqrt(a.v),
                              FromXm(DirectX::XMVectorSqrt(ToXm(a.v))))) << n;
        EXPECT_TRUE(NearEqual(mathf::RSqrt(a.v),
                              FromXm(DirectX::XMVectorReciprocalSqrt(
                                  ToXm(a.v))))) << n;
        EXPECT_TRUE(NearEqual(mathf::Recip(a.v),
                              FromXm(DirectX::XMVectorReciprocal(
                                  ToXm(a.v))))) << n;
    }
}
#endif // MATHF_TEST_HAS_DXMATH
