// Bitwise operations, comparisons, Select, and the mask predicates.
//
// These are where a backend is most likely to diverge quietly: results are bit
// patterns rather than numbers, so a wrong operand order or an inverted mask
// still produces plausible-looking floats. Everything here compares bits.

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

// A mask with a known pattern: lanes 0 and 2 set.
VecReg PatternMask() {
    return mathf::MakeMaskReg(mathf::kLaneTrue, mathf::kLaneFalse,
                              mathf::kLaneTrue, mathf::kLaneFalse);
}

} // namespace

// ------------------------------------------------------------ constexpr parity
static_assert(mathf::LaneBits(mathf::CmpLt(mathf::Set(1, 5, 3, 7),
                                           mathf::Set(2, 2, 4, 4)), 0)
              == mathf::kLaneTrue);
static_assert(mathf::LaneBits(mathf::CmpLt(mathf::Set(1, 5, 3, 7),
                                           mathf::Set(2, 2, 4, 4)), 1)
              == mathf::kLaneFalse);
static_assert(mathf::GetX(mathf::Select(mathf::CmpLt(mathf::Splat(1),
                                                     mathf::Splat(2)),
                                        mathf::Splat(10),
                                        mathf::Splat(20))) == 10.0f);
static_assert(mathf::GetX(mathf::Select(mathf::CmpGt(mathf::Splat(1),
                                                     mathf::Splat(2)),
                                        mathf::Splat(10),
                                        mathf::Splat(20))) == 20.0f);
static_assert(mathf::MoveMask(mathf::CmpLt(mathf::Set(1, 5, 1, 5),
                                           mathf::Set(2, 2, 2, 2))) == 0b0101);
static_assert(mathf::AllTrue(mathf::CmpEq(mathf::Splat(3), mathf::Splat(3))));
static_assert(!mathf::AnyTrue(mathf::CmpEq(mathf::Splat(3), mathf::Splat(4))));

// NaN compares unequal to everything, including itself.
static_assert(mathf::AllTrue(mathf::CmpNe(
    mathf::Splat(mathf::consteval_ops::kQuietNaN),
    mathf::Splat(mathf::consteval_ops::kQuietNaN))));

TEST(BackendLogicConstexpr, CompileTimeMatchesRuntime) {
    EXPECT_EQ(mathf::MoveMask(mathf::CmpLt(mathf::Set(1, 5, 1, 5),
                                           mathf::Set(2, 2, 2, 2))), 0b0101);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Select(
                        mathf::CmpLt(mathf::Splat(1), mathf::Splat(2)),
                        mathf::Splat(10), mathf::Splat(20))), 10.0f);
    EXPECT_TRUE(mathf::AllTrue(mathf::CmpEq(mathf::Splat(3), mathf::Splat(3))));
}

// ------------------------------------------------------------------- bitwise
TEST(BackendLogic, BitwiseMatchesReference) {
    RandomVectors gen(kSeed + 20);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        EXPECT_TRUE(BitsEqual(mathf::And(a.v, b.v), ref::And(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Or(a.v, b.v), ref::Or(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Xor(a.v, b.v), ref::Xor(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::AndNot(a.v, b.v), ref::AndNot(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Not(a.v), ref::Not(a.v))) << n;
    }
}

// AndNot's operand order is the one thing about it worth remembering, and the
// two SIMD backends spell it with opposite argument orders internally.
TEST(BackendLogic, AndNotInvertsItsFirstOperand) {
    const VecReg ones = mathf::Splat(mathf::FromBits(mathf::kLaneTrue));
    const VecReg zeros = mathf::Splat(mathf::FromBits(mathf::kLaneFalse));

    // ~0 & 1 == 1
    EXPECT_EQ(mathf::LaneBits(mathf::AndNot(zeros, ones), 0), mathf::kLaneTrue);
    // ~1 & 0 == 0, and ~1 & 1 == 0
    EXPECT_EQ(mathf::LaneBits(mathf::AndNot(ones, zeros), 0), mathf::kLaneFalse);
    EXPECT_EQ(mathf::LaneBits(mathf::AndNot(ones, ones), 0), mathf::kLaneFalse);
}

// ------------------------------------------------------------------ comparison
TEST(BackendLogic, ComparisonsMatchReference) {
    RandomVectors gen(kSeed + 21);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        EXPECT_TRUE(BitsEqual(mathf::CmpEq(a.v, b.v), ref::CmpEq(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpNe(a.v, b.v), ref::CmpNe(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpLt(a.v, b.v), ref::CmpLt(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpLe(a.v, b.v), ref::CmpLe(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpGt(a.v, b.v), ref::CmpGt(a.v, b.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpGe(a.v, b.v), ref::CmpGe(a.v, b.v))) << n;
    }
}

// Random floats almost never compare equal, so the interesting cases have to be
// constructed: equal values, and lanes that straddle the comparison.
TEST(BackendLogic, ComparisonsMatchReferenceWhenOperandsCoincide) {
    RandomVectors gen(kSeed + 22);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        // Half the lanes identical to a, half strictly greater.
        const VecReg b = mathf::Set(a.f[0], a.f[1] + 1.0f, a.f[2], a.f[3] + 1.0f);

        EXPECT_TRUE(BitsEqual(mathf::CmpEq(a.v, b), ref::CmpEq(a.v, b))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpLe(a.v, b), ref::CmpLe(a.v, b))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpGe(a.v, b), ref::CmpGe(a.v, b))) << n;
        EXPECT_EQ(mathf::MoveMask(mathf::CmpEq(a.v, b)), 0b0101) << n;
    }
}

TEST(BackendLogic, ComparisonsOnEdgeValues) {
    const auto& values = EdgeValues();
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < values.size(); ++j) {
            const VecReg a = mathf::Splat(Opaque(values[i]));
            const VecReg b = mathf::Splat(Opaque(values[j]));
            const std::string where =
                "values[" + std::to_string(i) + "], values[" + std::to_string(j) + "]";
            EXPECT_TRUE(BitsEqual(mathf::CmpEq(a, b), ref::CmpEq(a, b))) << where;
            EXPECT_TRUE(BitsEqual(mathf::CmpLt(a, b), ref::CmpLt(a, b))) << where;
            EXPECT_TRUE(BitsEqual(mathf::CmpGe(a, b), ref::CmpGe(a, b))) << where;
        }
    }
}

TEST(BackendLogic, NaNComparesUnorderedAgainstEverything) {
    const VecReg nan = mathf::Splat(Opaque(QuietNaN()));
    const VecReg one = mathf::Splat(Opaque(1.0f));

    // Every ordered comparison is false, and only not-equal is true.
    EXPECT_FALSE(mathf::AnyTrue(mathf::CmpEq(nan, one)));
    EXPECT_FALSE(mathf::AnyTrue(mathf::CmpLt(nan, one)));
    EXPECT_FALSE(mathf::AnyTrue(mathf::CmpLe(nan, one)));
    EXPECT_FALSE(mathf::AnyTrue(mathf::CmpGt(nan, one)));
    EXPECT_FALSE(mathf::AnyTrue(mathf::CmpGe(nan, one)));
    EXPECT_TRUE(mathf::AllTrue(mathf::CmpNe(nan, one)));
    EXPECT_TRUE(mathf::AllTrue(mathf::CmpNe(nan, nan)));
}

// Signed zeros compare equal but have different bits, which is exactly the case
// a value-based comparison would wave through.
TEST(BackendLogic, SignedZerosCompareEqual) {
    const VecReg pos = mathf::Splat(Opaque(0.0f));
    const VecReg neg = mathf::Splat(Opaque(-0.0f));
    EXPECT_TRUE(mathf::AllTrue(mathf::CmpEq(pos, neg)));
    EXPECT_NE(mathf::LaneBits(pos, 0), mathf::LaneBits(neg, 0));
}

// ---------------------------------------------------------------------- select
TEST(BackendLogic, SelectMatchesReference) {
    RandomVectors gen(kSeed + 23);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        const Sample c = gen.Next();
        const mathf::MaskReg mask = mathf::CmpLt(a.v, b.v);

        EXPECT_TRUE(BitsEqual(mathf::Select(mask, b.v, c.v),
                              ref::Select(mask, b.v, c.v))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Select(PatternMask(), b.v, c.v),
                              ref::Select(PatternMask(), b.v, c.v))) << n;
    }
}

TEST(BackendLogic, SelectTakesSetLanesFromIfTrue) {
    const VecReg t = mathf::Set(1, 2, 3, 4);
    const VecReg f = mathf::Set(10, 20, 30, 40);
    const VecReg r = mathf::Select(PatternMask(), t, f);

    EXPECT_FLOAT_EQ(mathf::GetX(r), 1.0f);    // mask lane 0 set   -> ifTrue
    EXPECT_FLOAT_EQ(mathf::GetY(r), 20.0f);   // mask lane 1 clear -> ifFalse
    EXPECT_FLOAT_EQ(mathf::GetZ(r), 3.0f);
    EXPECT_FLOAT_EQ(mathf::GetW(r), 40.0f);
}

// ------------------------------------------------------------------ predicates
TEST(BackendLogic, MoveMaskMatchesReference) {
    RandomVectors gen(kSeed + 24);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        const mathf::MaskReg mask = mathf::CmpLt(a.v, b.v);
        EXPECT_EQ(mathf::MoveMask(mask), ref::MoveMask(mask)) << n;
        EXPECT_EQ(mathf::AllTrue(mask), ref::AllTrue(mask)) << n;
        EXPECT_EQ(mathf::AnyTrue(mask), ref::AnyTrue(mask)) << n;
    }
}

TEST(BackendLogic, MoveMaskReadsLanesLowToHigh) {
    // Lane 0 is bit 0. Getting this backwards is a classic transposition bug and
    // would still pass a symmetric test.
    EXPECT_EQ(mathf::MoveMask(mathf::MakeMaskReg(mathf::kLaneTrue, 0, 0, 0)), 0b0001);
    EXPECT_EQ(mathf::MoveMask(mathf::MakeMaskReg(0, mathf::kLaneTrue, 0, 0)), 0b0010);
    EXPECT_EQ(mathf::MoveMask(mathf::MakeMaskReg(0, 0, mathf::kLaneTrue, 0)), 0b0100);
    EXPECT_EQ(mathf::MoveMask(mathf::MakeMaskReg(0, 0, 0, mathf::kLaneTrue)), 0b1000);
    EXPECT_EQ(mathf::MoveMask(PatternMask()), 0b0101);
}

// MoveMask reads the sign bit, so a negative float registers even though it is
// not a mask. That is the movmskps definition and callers rely on it.
TEST(BackendLogic, MoveMaskReadsSignBitOfOrdinaryFloats) {
    EXPECT_EQ(mathf::MoveMask(mathf::Set(-1.0f, 1.0f, -1.0f, 1.0f)), 0b0101);
    EXPECT_EQ(mathf::MoveMask(mathf::Splat(-0.0f)), 0b1111);
    EXPECT_EQ(mathf::MoveMask(mathf::Splat(0.0f)), 0b0000);
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

TEST(BackendLogicDxParity, ComparisonsAndBitwiseMatchDirectXMath) {
    RandomVectors gen(kSeed + 30);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        const DirectX::XMVECTOR xa = ToXm(a.v);
        const DirectX::XMVECTOR xb = ToXm(b.v);

        EXPECT_TRUE(BitsEqual(mathf::CmpEq(a.v, b.v),
                              FromXm(DirectX::XMVectorEqual(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpLt(a.v, b.v),
                              FromXm(DirectX::XMVectorLess(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpLe(a.v, b.v),
                              FromXm(DirectX::XMVectorLessOrEqual(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpGt(a.v, b.v),
                              FromXm(DirectX::XMVectorGreater(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::CmpGe(a.v, b.v),
                              FromXm(DirectX::XMVectorGreaterOrEqual(xa, xb)))) << n;

        EXPECT_TRUE(BitsEqual(mathf::And(a.v, b.v),
                              FromXm(DirectX::XMVectorAndInt(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Or(a.v, b.v),
                              FromXm(DirectX::XMVectorOrInt(xa, xb)))) << n;
        EXPECT_TRUE(BitsEqual(mathf::Xor(a.v, b.v),
                              FromXm(DirectX::XMVectorXorInt(xa, xb)))) << n;
        // DirectXMath offers NOR rather than NOT; NOR(a, 0) is ~a.
        EXPECT_TRUE(BitsEqual(mathf::Not(a.v),
                              FromXm(DirectX::XMVectorNorInt(
                                  xa, DirectX::XMVectorZero())))) << n;
        // XMVectorAndCInt(V1, V2) is V1 & ~V2 -- the inverted operand is the
        // second, where ours is the first, so the arguments swap.
        EXPECT_TRUE(BitsEqual(mathf::AndNot(a.v, b.v),
                              FromXm(DirectX::XMVectorAndCInt(xb, xa)))) << n;
    }
}

TEST(BackendLogicDxParity, SelectMatchesDirectXMath) {
    RandomVectors gen(kSeed + 31);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        const Sample c = gen.Next();
        const mathf::MaskReg mask = mathf::CmpLt(a.v, b.v);

        // XMVectorSelect(V1, V2, Control) takes V1 where the control bit is
        // clear and V2 where it is set, so ifFalse comes first.
        EXPECT_TRUE(BitsEqual(
            mathf::Select(mask, b.v, c.v),
            FromXm(DirectX::XMVectorSelect(ToXm(c.v), ToXm(b.v), ToXm(mask)))))
            << n;
    }
}
#endif // MATHF_TEST_HAS_DXMATH
