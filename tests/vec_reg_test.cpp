// Establishes the three-way verification pattern every later phase reuses:
//   1. constexpr parity  -- the compile-time path agrees with the runtime path
//   2. reference parity  -- the SIMD path agrees with a plain scalar reference
//   3. DirectXMath parity -- results match the baseline library bit-for-bit
// Adding an operation without all three is how a SIMD library grows silent bugs.

#include <mathf/vec_reg.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <random>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHF_TEST_HAS_DXMATH 1
#else
#  define MATHF_TEST_HAS_DXMATH 0
#endif

namespace {

using mathf::VecReg;

std::array<float, 4> ToArray(VecReg v) {
    return {mathf::Lane(v, 0), mathf::Lane(v, 1),
            mathf::Lane(v, 2), mathf::Lane(v, 3)};
}

// Forces a value to be a materialized, rounded float the optimizer cannot see
// through. Without this the reference oracle is not trustworthy: under /fp:fast
// the compiler may re-derive an input from the expression that produced it,
// using FMA contraction and skipping an intermediate rounding, so the "scalar
// reference" ends up computed from different bits than the SIMD path consumed.
// Measured on MSVC /O2 /fp:fast /arch:AVX2 -- divergence up to ~8 ULP.
float Opaque(float x) {
    volatile float v = x;
    return v;
}

// A random input kept in both forms, guaranteed to hold identical bits: `f` for
// the scalar reference and `v` for the SIMD path under test.
struct Sample {
    std::array<float, 4> f;
    VecReg v;
};

// Deterministic generator: a failure reported by CI must be reproducible locally.
class RandomVectors {
public:
    explicit RandomVectors(unsigned seed) : rng_(seed), dist_(-100.0f, 100.0f) {}

    Sample Next() {
        // Filled in a loop, not as call arguments: argument evaluation order is
        // unspecified, so building the vector inline would let two compilers
        // consume the generator in different orders and see different sequences.
        std::array<float, 4> f{};
        for (auto& x : f) x = Opaque(dist_(rng_));
        return Sample{f, mathf::Set(f[0], f[1], f[2], f[3])};
    }

private:
    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_;
};

constexpr unsigned kSeed = 0x4D617468u;  // 'Math'
constexpr int kSamples = 512;

} // namespace

// ------------------------------------------------------------ constexpr parity
// These run at compile time. If the dual-path design ever regresses, the build
// breaks here rather than producing wrong numbers at runtime.
static_assert(mathf::GetX(mathf::Set(1.0f, 2.0f, 3.0f, 4.0f)) == 1.0f);
static_assert(mathf::Lane(mathf::Set(1.0f, 2.0f, 3.0f, 4.0f), 3) == 4.0f);
static_assert(mathf::Lane(mathf::Splat(7.0f), 2) == 7.0f);
static_assert(mathf::GetX(mathf::Add(mathf::Set(1, 2, 3, 4),
                                     mathf::Set(10, 20, 30, 40))) == 11.0f);
static_assert(mathf::Lane(mathf::Sub(mathf::Set(10, 20, 30, 40),
                                     mathf::Set(1, 2, 3, 4)), 1) == 18.0f);
static_assert(mathf::Lane(mathf::Mul(mathf::Set(2, 3, 4, 5),
                                     mathf::Set(3, 3, 3, 3)), 2) == 12.0f);
static_assert(mathf::Lane(mathf::Div(mathf::Set(10, 20, 30, 40),
                                     mathf::Set(2, 2, 2, 2)), 3) == 20.0f);
static_assert(mathf::GetX(mathf::MulAdd(mathf::Set(2, 0, 0, 0),
                                        mathf::Set(3, 0, 0, 0),
                                        mathf::Set(1, 0, 0, 0))) == 7.0f);
static_assert(mathf::GetX(mathf::Dot4(mathf::Set(1, 2, 3, 4),
                                      mathf::Set(1, 1, 1, 1))) == 10.0f);
static_assert(mathf::GetX(mathf::Zero()) == 0.0f);

TEST(VecRegConstexpr, CompileTimeMatchesRuntime) {
    // The same expressions above, evaluated at runtime through the intrinsic
    // path. Equal results prove both paths agree.
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Add(mathf::Set(1, 2, 3, 4),
                                           mathf::Set(10, 20, 30, 40))), 11.0f);
    EXPECT_FLOAT_EQ(mathf::Lane(mathf::Mul(mathf::Set(2, 3, 4, 5),
                                           mathf::Set(3, 3, 3, 3)), 2), 12.0f);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::MulAdd(mathf::Set(2, 0, 0, 0),
                                              mathf::Set(3, 0, 0, 0),
                                              mathf::Set(1, 0, 0, 0))), 7.0f);
    EXPECT_FLOAT_EQ(mathf::GetX(mathf::Dot4(mathf::Set(1, 2, 3, 4),
                                            mathf::Set(1, 1, 1, 1))), 10.0f);
}

// ------------------------------------------------------------ reference parity
TEST(VecRegReference, SetRoundTripsExactly) {
    // Everything below assumes Set/Lane preserve bits exactly. Check it first,
    // so a failure here does not masquerade as an arithmetic bug.
    RandomVectors gen(kSeed);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const auto got = ToArray(a.v);
        for (int i = 0; i < 4; ++i)
            EXPECT_EQ(got[i], a.f[i]) << "lane " << i << " sample " << n;
    }
}

TEST(VecRegReference, ArithmeticMatchesScalarReference) {
    RandomVectors gen(kSeed);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();

        const auto add = ToArray(mathf::Add(a.v, b.v));
        const auto sub = ToArray(mathf::Sub(a.v, b.v));
        const auto mul = ToArray(mathf::Mul(a.v, b.v));
        const auto div = ToArray(mathf::Div(a.v, b.v));

        for (int i = 0; i < 4; ++i) {
            // Single IEEE operations on identical inputs: exact equality is the
            // correct bar. Anything looser would hide a real lane-ordering bug.
            EXPECT_EQ(add[i], a.f[i] + b.f[i]) << "lane " << i << " sample " << n;
            EXPECT_EQ(sub[i], a.f[i] - b.f[i]) << "lane " << i << " sample " << n;
            EXPECT_EQ(mul[i], a.f[i] * b.f[i]) << "lane " << i << " sample " << n;
            EXPECT_EQ(div[i], a.f[i] / b.f[i]) << "lane " << i << " sample " << n;
        }
    }
}

TEST(VecRegReference, Dot4MatchesScalarReference) {
    RandomVectors gen(kSeed + 1);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();

        const float expected = a.f[0] * b.f[0] + a.f[1] * b.f[1]
                             + a.f[2] * b.f[2] + a.f[3] * b.f[3];
        const auto got = ToArray(mathf::Dot4(a.v, b.v));

        // dpps sums in a different order than a left-to-right scalar reduction,
        // so this is a tolerance comparison by nature, not by convenience.
        const float tol = std::max(1e-3f, std::abs(expected) * 1e-5f);
        for (int i = 0; i < 4; ++i) {
            EXPECT_NEAR(got[i], expected, tol)
                << "dot must be splatted to every lane; lane " << i
                << " sample " << n;
        }
    }
}

TEST(VecRegReference, MulAddMatchesScalarReference) {
    RandomVectors gen(kSeed + 2);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next(), b = gen.Next(), c = gen.Next();
        const auto got = ToArray(mathf::MulAdd(a.v, b.v, c.v));

        for (int i = 0; i < 4; ++i) {
            // A true FMA keeps the intermediate product at full width, so it can
            // legitimately differ from a rounded mul-then-add in the last places.
            const float expected = std::fma(a.f[i], b.f[i], c.f[i]);
            const float tol = std::max(1e-3f, std::abs(expected) * 1e-5f);
            EXPECT_NEAR(got[i], expected, tol) << "lane " << i << " sample " << n;
        }
    }
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHF_TEST_HAS_DXMATH
namespace {

std::array<float, 4> ToArray(DirectX::FXMVECTOR v) {
    DirectX::XMFLOAT4 out{};
    DirectX::XMStoreFloat4(&out, v);
    return {out.x, out.y, out.z, out.w};
}

DirectX::XMVECTOR ToXm(VecReg v) {
    return DirectX::XMVectorSet(mathf::Lane(v, 0), mathf::Lane(v, 1),
                                mathf::Lane(v, 2), mathf::Lane(v, 3));
}

} // namespace

TEST(VecRegDxParity, ArithmeticMatchesDirectXMath) {
    RandomVectors gen(kSeed + 10);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        const DirectX::XMVECTOR xa = ToXm(a.v);
        const DirectX::XMVECTOR xb = ToXm(b.v);

        // Both libraries issue the same single instruction here, so the results
        // must be bit-identical, not merely close.
        const auto mine = ToArray(mathf::Add(a.v, b.v));
        const auto theirs = ToArray(DirectX::XMVectorAdd(xa, xb));
        for (int i = 0; i < 4; ++i)
            EXPECT_EQ(mine[i], theirs[i]) << "add lane " << i << " sample " << n;

        const auto mineMul = ToArray(mathf::Mul(a.v, b.v));
        const auto theirsMul = ToArray(DirectX::XMVectorMultiply(xa, xb));
        for (int i = 0; i < 4; ++i)
            EXPECT_EQ(mineMul[i], theirsMul[i]) << "mul lane " << i << " sample " << n;
    }
}

TEST(VecRegDxParity, Dot4MatchesDirectXMath) {
    RandomVectors gen(kSeed + 11);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();

        const auto mine = ToArray(mathf::Dot4(a.v, b.v));
        const auto theirs = ToArray(DirectX::XMVector4Dot(ToXm(a.v), ToXm(b.v)));

        // Tolerance rather than equality: Mathf uses dpps only when SSE4.1 is
        // enabled, and DirectXMath's own path varies with its build settings.
        const float tol = std::max(1e-3f, std::abs(theirs[0]) * 1e-5f);
        for (int i = 0; i < 4; ++i)
            EXPECT_NEAR(mine[i], theirs[i], tol) << "lane " << i << " sample " << n;
    }
}

TEST(VecRegDxParity, MulAddMatchesDirectXMath) {
    RandomVectors gen(kSeed + 12);
    for (int n = 0; n < kSamples; ++n) {
        const Sample a = gen.Next(), b = gen.Next(), c = gen.Next();
        const auto mine = ToArray(mathf::MulAdd(a.v, b.v, c.v));
        const auto theirs = ToArray(
            DirectX::XMVectorMultiplyAdd(ToXm(a.v), ToXm(b.v), ToXm(c.v)));

        for (int i = 0; i < 4; ++i) {
            const float tol = std::max(1e-3f, std::abs(theirs[i]) * 1e-5f);
            EXPECT_NEAR(mine[i], theirs[i], tol) << "lane " << i << " sample " << n;
        }
    }
}
#endif // MATHF_TEST_HAS_DXMATH
