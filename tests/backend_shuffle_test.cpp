// Shuffles, dot-product reductions, memory access, and lane readout.
//
// Lane-ordering mistakes are the failure mode here: _mm_shuffle_ps takes its
// selectors reversed, and its two-source form draws from different operands per
// lane. Tests use asymmetric inputs throughout so a transposition cannot pass.

#include "support/reg_testing.hpp"

#include <mathf/arch/consteval_ops.hpp>

#include <vector>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHF_TEST_HAS_DXMATH 1
#else
#  define MATHF_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace mathf_test;
namespace ref = mathf::consteval_ops;

// Distinct lanes, so every permutation produces a distinguishable result.
constexpr float kA = 1.0f, kB = 2.0f, kC = 3.0f, kD = 4.0f;

} // namespace

// ------------------------------------------------------------ constexpr parity
static_assert(mathf::GetX(mathf::Shuffle<3, 2, 1, 0>(
                  mathf::Set(kA, kB, kC, kD))) == kD);
static_assert(mathf::GetW(mathf::Shuffle<3, 2, 1, 0>(
                  mathf::Set(kA, kB, kC, kD))) == kA);
static_assert(mathf::GetX(mathf::SplatW(mathf::Set(kA, kB, kC, kD))) == kD);
static_assert(mathf::GetX(mathf::Dot4(mathf::Set(1, 2, 3, 4),
                                      mathf::Splat(1))) == 10.0f);
static_assert(mathf::GetX(mathf::Dot3(mathf::Set(1, 2, 3, 999),
                                      mathf::Splat(1))) == 6.0f);
static_assert(mathf::GetX(mathf::Dot2(mathf::Set(1, 2, 999, 999),
                                      mathf::Splat(1))) == 3.0f);
static_assert(mathf::GetY(mathf::Set(kA, kB, kC, kD)) == kB);
static_assert(mathf::GetZ(mathf::Set(kA, kB, kC, kD)) == kC);

TEST(BackendShuffleConstexpr, CompileTimeMatchesRuntime) {
    const VecReg v = mathf::Set(kA, kB, kC, kD);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Shuffle<3, 2, 1, 0>(v)), kD);
    EXPECT_FLOAT_EQ(mathf::GetW(mathf::Shuffle<3, 2, 1, 0>(v)), kA);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::SplatW(v)), kD);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Dot3(mathf::Set(1, 2, 3, 999),
                                            mathf::Splat(1))), 6.0f);
}

// ---------------------------------------------------------------------- shuffle
TEST(BackendShuffle, SingleSourceLanesComeFromTheNamedIndices) {
    const VecReg v = mathf::Set(kA, kB, kC, kD);

    const VecReg identity = mathf::Shuffle<0, 1, 2, 3>(v);
    EXPECT_TRUE(BitsEqual(identity, v));

    const VecReg reversed = mathf::Shuffle<3, 2, 1, 0>(v);
    EXPECT_FLOAT_EQ(mathf::GetX(reversed), kD);
    EXPECT_FLOAT_EQ(mathf::GetY(reversed), kC);
    EXPECT_FLOAT_EQ(mathf::GetZ(reversed), kB);
    EXPECT_FLOAT_EQ(mathf::GetW(reversed), kA);

    const VecReg mixed = mathf::Shuffle<2, 0, 3, 1>(v);
    EXPECT_FLOAT_EQ(mathf::GetX(mixed), kC);
    EXPECT_FLOAT_EQ(mathf::GetY(mixed), kA);
    EXPECT_FLOAT_EQ(mathf::GetZ(mixed), kD);
    EXPECT_FLOAT_EQ(mathf::GetW(mixed), kB);
}

// The low two lanes come from the first operand and the high two from the
// second, matching _mm_shuffle_ps. Both operands are distinct here so a
// backend that read all four from one of them would fail.
TEST(BackendShuffle, TwoSourceTakesLowLanesFromFirstOperand) {
    const VecReg a = mathf::Set(1, 2, 3, 4);
    const VecReg b = mathf::Set(10, 20, 30, 40);

    const VecReg r = mathf::Shuffle<0, 1, 0, 1>(a, b);
    EXPECT_FLOAT_EQ(mathf::GetX(r), 1.0f);
    EXPECT_FLOAT_EQ(mathf::GetY(r), 2.0f);
    EXPECT_FLOAT_EQ(mathf::GetZ(r), 10.0f);
    EXPECT_FLOAT_EQ(mathf::GetW(r), 20.0f);

    const VecReg s = mathf::Shuffle<3, 2, 3, 2>(a, b);
    EXPECT_FLOAT_EQ(mathf::GetX(s), 4.0f);
    EXPECT_FLOAT_EQ(mathf::GetY(s), 3.0f);
    EXPECT_FLOAT_EQ(mathf::GetZ(s), 40.0f);
    EXPECT_FLOAT_EQ(mathf::GetW(s), 30.0f);
}

TEST(BackendShuffle, SplatsBroadcastTheNamedLane) {
    const VecReg v = mathf::Set(kA, kB, kC, kD);
    EXPECT_TRUE(BitsEqual(mathf::SplatX(v), mathf::Splat(kA)));
    EXPECT_TRUE(BitsEqual(mathf::SplatY(v), mathf::Splat(kB)));
    EXPECT_TRUE(BitsEqual(mathf::SplatZ(v), mathf::Splat(kC)));
    EXPECT_TRUE(BitsEqual(mathf::SplatW(v), mathf::Splat(kD)));
}

namespace {

// Shuffle indices must be template arguments, so an exhaustive sweep has to be
// unrolled at compile time. One small function per index position, each fanning
// out to the next -- verbose, but readable and portable.
template <int X, int Y, int Z, int W>
void CheckShuffle(VecReg a) {
    EXPECT_TRUE(BitsEqual(mathf::Shuffle<X, Y, Z, W>(a),
                          ref::Shuffle<X, Y, Z, W>(a)))
        << "Shuffle<" << X << "," << Y << "," << Z << "," << W << ">";
}

template <int X, int Y, int Z>
void SweepW(VecReg a) {
    CheckShuffle<X, Y, Z, 0>(a);
    CheckShuffle<X, Y, Z, 1>(a);
    CheckShuffle<X, Y, Z, 2>(a);
    CheckShuffle<X, Y, Z, 3>(a);
}

template <int X, int Y>
void SweepZ(VecReg a) {
    SweepW<X, Y, 0>(a);
    SweepW<X, Y, 1>(a);
    SweepW<X, Y, 2>(a);
    SweepW<X, Y, 3>(a);
}

template <int X>
void SweepY(VecReg a) {
    SweepZ<X, 0>(a);
    SweepZ<X, 1>(a);
    SweepZ<X, 2>(a);
    SweepZ<X, 3>(a);
}

} // namespace

// Exhaustive over all 256 single-source permutations: cheap, and it leaves no
// index combination untested.
TEST(BackendShuffle, AllSingleSourcePermutationsMatchReference) {
    RandomVectors gen(kSeed + 40);
    const Sample a = gen.Next();
    SweepY<0>(a.v);
    SweepY<1>(a.v);
    SweepY<2>(a.v);
    SweepY<3>(a.v);
}

TEST(BackendShuffle, TwoSourcePermutationsMatchReference) {
    RandomVectors gen(kSeed + 41);
    const Sample a = gen.Next();
    const Sample b = gen.Next();

    EXPECT_TRUE(BitsEqual((mathf::Shuffle<0, 1, 2, 3>(a.v, b.v)),
                          (ref::Shuffle<0, 1, 2, 3>(a.v, b.v))));
    EXPECT_TRUE(BitsEqual((mathf::Shuffle<3, 2, 1, 0>(a.v, b.v)),
                          (ref::Shuffle<3, 2, 1, 0>(a.v, b.v))));
    EXPECT_TRUE(BitsEqual((mathf::Shuffle<0, 0, 3, 3>(a.v, b.v)),
                          (ref::Shuffle<0, 0, 3, 3>(a.v, b.v))));
    EXPECT_TRUE(BitsEqual((mathf::Shuffle<2, 1, 1, 2>(a.v, b.v)),
                          (ref::Shuffle<2, 1, 1, 2>(a.v, b.v))));
}

// ------------------------------------------------------------------- reductions
TEST(BackendShuffle, DotProductsMatchReference) {
    RandomVectors gen(kSeed + 42);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        // dpps and a left-to-right scalar sum associate differently, so this is
        // a tolerance comparison by nature -- and the tolerance has to scale with
        // the terms rather than the result, since the two can nearly cancel.
        EXPECT_TRUE(NearEqual(mathf::Dot2(a.v, b.v), ref::Dot2(a.v, b.v),
                              0.0f, DotTolerance(a.f, b.f, 2))) << n;
        EXPECT_TRUE(NearEqual(mathf::Dot3(a.v, b.v), ref::Dot3(a.v, b.v),
                              0.0f, DotTolerance(a.f, b.f, 3))) << n;
        EXPECT_TRUE(NearEqual(mathf::Dot4(a.v, b.v), ref::Dot4(a.v, b.v),
                              0.0f, DotTolerance(a.f, b.f, 4))) << n;
    }
}

TEST(BackendShuffle, DotProductsIgnoreLanesOutsideTheirWidth) {
    // A huge value in the unused lanes would swamp the result if it leaked in.
    const VecReg a = mathf::Set(1, 2, 1e20f, 1e20f);
    const VecReg b = mathf::Set(3, 4, 1e20f, 1e20f);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Dot2(a, b)), 11.0f);

    const VecReg c = mathf::Set(1, 2, 3, 1e20f);
    const VecReg d = mathf::Set(4, 5, 6, 1e20f);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Dot3(c, d)), 32.0f);
}

TEST(BackendShuffle, DotProductsBroadcastToEveryLane) {
    const VecReg a = mathf::Set(1, 2, 3, 4);
    const VecReg b = mathf::Set(5, 6, 7, 8);
    for (const VecReg r : {mathf::Dot2(a, b), mathf::Dot3(a, b), mathf::Dot4(a, b)}) {
        EXPECT_FLOAT_EQ(mathf::GetX(r), mathf::GetY(r));
        EXPECT_FLOAT_EQ(mathf::GetX(r), mathf::GetZ(r));
        EXPECT_FLOAT_EQ(mathf::GetX(r), mathf::GetW(r));
    }
}

// ---------------------------------------------------------------- memory access
TEST(BackendShuffle, LoadAndStoreRoundTripExactly) {
    RandomVectors gen(kSeed + 43);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        alignas(16) std::array<float, 4> buffer{};

        mathf::StoreAligned(buffer.data(), a.v);
        EXPECT_TRUE(BitsEqual(mathf::LoadAligned(buffer.data()), a.v)) << n;

        mathf::Store(buffer.data(), a.v);
        EXPECT_TRUE(BitsEqual(mathf::Load(buffer.data()), a.v)) << n;
    }
}

// Every other test in the suite assumes Set and Lane preserve bits exactly, so
// this is checked on its own -- a failure here would otherwise surface as an
// arithmetic bug somewhere unrelated.
TEST(BackendShuffle, SetAndLaneRoundTripExactly) {
    RandomVectors gen(kSeed + 44);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const auto got = ToArray(a.v);
        for (int i = 0; i < 4; ++i) {
            EXPECT_EQ(got[i], a.f[i]) << "lane " << i << " sample " << n;
        }
    }
}

TEST(BackendShuffle, LoadPreservesLaneOrder) {
    alignas(16) const std::array<float, 4> buffer = {kA, kB, kC, kD};
    const VecReg v = mathf::LoadAligned(buffer.data());
    EXPECT_FLOAT_EQ(mathf::GetX(v), kA);
    EXPECT_FLOAT_EQ(mathf::GetY(v), kB);
    EXPECT_FLOAT_EQ(mathf::GetZ(v), kC);
    EXPECT_FLOAT_EQ(mathf::GetW(v), kD);
}

// The unaligned form must work at every byte offset; using an aligned load on an
// unaligned address faults on x86 rather than degrading.
TEST(BackendShuffle, UnalignedLoadWorksAtEveryOffset) {
    std::vector<float> storage(16);
    for (std::size_t i = 0; i < storage.size(); ++i) {
        storage[i] = static_cast<float>(i);
    }
    for (std::size_t offset = 0; offset < 4; ++offset) {
        const VecReg v = mathf::Load(storage.data() + offset);
        EXPECT_FLOAT_EQ(mathf::GetX(v), static_cast<float>(offset));
        EXPECT_FLOAT_EQ(mathf::GetW(v), static_cast<float>(offset + 3));
    }
}

TEST(BackendShuffle, StoreWritesExactlyFourFloats) {
    std::array<float, 6> buffer{};
    constexpr float kGuard = -999.0f;
    buffer.fill(kGuard);

    mathf::Store(buffer.data() + 1, mathf::Set(kA, kB, kC, kD));

    EXPECT_FLOAT_EQ(buffer[0], kGuard) << "wrote before the destination";
    EXPECT_FLOAT_EQ(buffer[1], kA);
    EXPECT_FLOAT_EQ(buffer[4], kD);
    EXPECT_FLOAT_EQ(buffer[5], kGuard) << "wrote past the destination";
}

// ------------------------------------------------------------------ lane readout
TEST(BackendShuffle, GettersReadTheirOwnLane) {
    const VecReg v = mathf::Set(kA, kB, kC, kD);
    EXPECT_FLOAT_EQ(mathf::GetX(v), kA);
    EXPECT_FLOAT_EQ(mathf::GetY(v), kB);
    EXPECT_FLOAT_EQ(mathf::GetZ(v), kC);
    EXPECT_FLOAT_EQ(mathf::GetW(v), kD);

    EXPECT_FLOAT_EQ(mathf::Lane(v, 0), kA);
    EXPECT_FLOAT_EQ(mathf::Lane(v, 1), kB);
    EXPECT_FLOAT_EQ(mathf::Lane(v, 2), kC);
    EXPECT_FLOAT_EQ(mathf::Lane(v, 3), kD);
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

TEST(BackendShuffleDxParity, DotProductsMatchDirectXMath) {
    RandomVectors gen(kSeed + 50);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        const DirectX::XMVECTOR xa = ToXm(a.v);
        const DirectX::XMVECTOR xb = ToXm(b.v);

        EXPECT_TRUE(NearEqual(mathf::Dot2(a.v, b.v),
                              FromXm(DirectX::XMVector2Dot(xa, xb)),
                              0.0f, DotTolerance(a.f, b.f, 2))) << n;
        EXPECT_TRUE(NearEqual(mathf::Dot3(a.v, b.v),
                              FromXm(DirectX::XMVector3Dot(xa, xb)),
                              0.0f, DotTolerance(a.f, b.f, 3))) << n;
        EXPECT_TRUE(NearEqual(mathf::Dot4(a.v, b.v),
                              FromXm(DirectX::XMVector4Dot(xa, xb)),
                              0.0f, DotTolerance(a.f, b.f, 4))) << n;
    }
}

TEST(BackendShuffleDxParity, LaneGettersMatchDirectXMath) {
    RandomVectors gen(kSeed + 51);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const DirectX::XMVECTOR xa = ToXm(a.v);
        EXPECT_FLOAT_EQ(mathf::GetX(a.v), DirectX::XMVectorGetX(xa)) << n;
        EXPECT_FLOAT_EQ(mathf::GetY(a.v), DirectX::XMVectorGetY(xa)) << n;
        EXPECT_FLOAT_EQ(mathf::GetZ(a.v), DirectX::XMVectorGetZ(xa)) << n;
        EXPECT_FLOAT_EQ(mathf::GetW(a.v), DirectX::XMVectorGetW(xa)) << n;
    }
}
#endif // MATHF_TEST_HAS_DXMATH
