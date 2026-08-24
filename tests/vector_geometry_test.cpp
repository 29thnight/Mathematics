// Geometric vector operations, checked against hand-computed values and against
// DirectXMath. The degenerate cases matter most here: a normalize that returns
// NaN instead of zero propagates through a whole scene graph before anyone sees
// it.

#include "support/reg_testing.hpp"

#include <mathf/vector.hpp>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHF_TEST_HAS_DXMATH 1
#else
#  define MATHF_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace mathf_test;
using mathf::Vector2;
using mathf::Vector3;
using mathf::Vector4;

Vector3 RandomVector3(RandomVectors& gen) {
    const Sample s = gen.Next();
    return Vector3{s.f[0], s.f[1], s.f[2]};
}

} // namespace

// ---------------------------------------------------------- dot, length
static_assert(mathf::Dot(Vector3{1, 2, 3}, Vector3{1, 1, 1}) == 6.0f);
static_assert(mathf::LengthSq(Vector3{0, 3, 4}) == 25.0f);
static_assert(mathf::Length(Vector3{0, 3, 4}) == 5.0f);
static_assert(mathf::Distance(Vector3{1, 0, 0}, Vector3{4, 4, 0}) == 5.0f);

TEST(VectorGeometry, LengthAndDistance) {
    EXPECT_FLOAT_EQ(mathf::Length(Vector3(3, 4, 0)), 5.0f);
    EXPECT_FLOAT_EQ(mathf::LengthSq(Vector3(3, 4, 0)), 25.0f);
    EXPECT_FLOAT_EQ(mathf::Distance(Vector3(1, 1, 1), Vector3(4, 5, 1)), 5.0f);
    EXPECT_FLOAT_EQ(mathf::DistanceSq(Vector3(1, 1, 1), Vector3(4, 5, 1)), 25.0f);
}

// ------------------------------------------------------------------ normalize
TEST(VectorGeometry, NormalizeProducesUnitLength) {
    RandomVectors gen(kSeed + 70);
    for (int n = 0; n < kSamples; ++n) {
        const Vector3 v = RandomVector3(gen);
        if (mathf::LengthSq(v) < 1e-6f) continue;
        EXPECT_NEAR(mathf::Length(mathf::Normalize(v)), 1.0f, 1e-5f) << n;
    }
}

TEST(VectorGeometry, NormalizePreservesDirection) {
    const Vector3 v{3, 4, 0};
    const Vector3 n = mathf::Normalize(v);
    EXPECT_TRUE(mathf::NearEqual(n, Vector3(0.6f, 0.8f, 0.0f)));
}

// The whole reason Normalize costs two selects rather than a bare divide.
TEST(VectorGeometry, NormalizeOfZeroIsZeroNotNaN) {
    const Vector3 n = mathf::Normalize(Vector3::Zero());
    EXPECT_FALSE(std::isnan(n.x)) << "a zero vector must not normalize to NaN";
    EXPECT_TRUE(n == Vector3::Zero());
}

TEST(VectorGeometry, NormalizeOfInfiniteIsNaN) {
    const float inf = std::numeric_limits<float>::infinity();
    const Vector3 n = mathf::Normalize(Vector3(Opaque(inf), 0.0f, 0.0f));
    EXPECT_TRUE(std::isnan(n.x));
}

// The approximate form trades those guards for speed, which is the point of it.
TEST(VectorGeometry, NormalizeEstIsCloseButNotExact) {
    RandomVectors gen(kSeed + 71);
    for (int n = 0; n < kSamples; ++n) {
        const Vector3 v = RandomVector3(gen);
        if (mathf::LengthSq(v) < 1.0f) continue;
        EXPECT_NEAR(mathf::Length(mathf::NormalizeEst(v)), 1.0f, 4e-3f) << n;
    }
}

// ---------------------------------------------------------------------- cross
static_assert(mathf::Cross(Vector3::UnitX(), Vector3::UnitY()).z == 1.0f);
static_assert(mathf::Cross(Vector2{1, 0}, Vector2{0, 1}) == 1.0f);

// Right-handed, matching DirectXMath. Getting the handedness backwards flips
// every surface normal in a renderer, so all three basis pairs are pinned.
TEST(VectorGeometry, CrossIsRightHanded) {
    EXPECT_TRUE(mathf::Cross(Vector3::UnitX(), Vector3::UnitY()) ==
                Vector3::UnitZ());
    EXPECT_TRUE(mathf::Cross(Vector3::UnitY(), Vector3::UnitZ()) ==
                Vector3::UnitX());
    EXPECT_TRUE(mathf::Cross(Vector3::UnitZ(), Vector3::UnitX()) ==
                Vector3::UnitY());
}

TEST(VectorGeometry, CrossIsAntiCommutativeAndOrthogonal) {
    RandomVectors gen(kSeed + 72);
    for (int n = 0; n < kSamples; ++n) {
        const Vector3 a = RandomVector3(gen);
        const Vector3 b = RandomVector3(gen);
        const Vector3 c = mathf::Cross(a, b);

        EXPECT_TRUE(mathf::NearEqual(mathf::Cross(b, a), -c, 1e-2f)) << n;

        // Orthogonality is checked relative to the magnitudes involved: with
        // components up to 100 the cross reaches 1e4, and a dot of two such
        // values cancels to a residue proportional to that, not to zero.
        const float scale = mathf::Length(a) * mathf::Length(b) *
                            std::max(mathf::Length(a), mathf::Length(b));
        EXPECT_NEAR(mathf::Dot(a, c), 0.0f, scale * 1e-5f) << n;
        EXPECT_NEAR(mathf::Dot(b, c), 0.0f, scale * 1e-5f) << n;
    }
}

TEST(VectorGeometry, Cross2DIsTheSignedArea) {
    EXPECT_FLOAT_EQ(mathf::Cross(Vector2(1, 0), Vector2(0, 1)), 1.0f);
    EXPECT_FLOAT_EQ(mathf::Cross(Vector2(0, 1), Vector2(1, 0)), -1.0f);
    EXPECT_FLOAT_EQ(mathf::Cross(Vector2(2, 0), Vector2(0, 3)), 6.0f);
    EXPECT_FLOAT_EQ(mathf::Cross(Vector2(1, 1), Vector2(2, 2)), 0.0f);
}

TEST(VectorGeometry, PerpendicularTurnsCounterClockwise) {
    EXPECT_TRUE(mathf::Perpendicular(Vector2(1, 0)) == Vector2(0, 1));
    EXPECT_TRUE(mathf::Perpendicular(Vector2(0, 1)) == Vector2(-1, 0));
    EXPECT_FLOAT_EQ(mathf::Dot(Vector2(3, 4), mathf::Perpendicular(Vector2(3, 4))),
                    0.0f);
}

// -------------------------------------------------------------------- reflect
TEST(VectorGeometry, ReflectMirrorsAboutTheSurface) {
    // Straight down onto a floor comes straight back up.
    const Vector3 r = mathf::Reflect(Vector3(0, -1, 0), Vector3::UnitY());
    EXPECT_TRUE(mathf::NearEqual(r, Vector3(0, 1, 0)));

    // A 45-degree incidence leaves at 45 degrees.
    const Vector3 diagonal = mathf::Normalize(Vector3(1, -1, 0));
    const Vector3 bounced = mathf::Reflect(diagonal, Vector3::UnitY());
    EXPECT_TRUE(mathf::NearEqual(bounced, mathf::Normalize(Vector3(1, 1, 0))));
}

TEST(VectorGeometry, ReflectPreservesLength) {
    RandomVectors gen(kSeed + 73);
    for (int n = 0; n < kSamples; ++n) {
        const Vector3 incident = RandomVector3(gen);
        const Vector3 normal = mathf::Normalize(RandomVector3(gen));
        if (std::isnan(normal.x)) continue;
        EXPECT_NEAR(mathf::Length(mathf::Reflect(incident, normal)),
                    mathf::Length(incident),
                    mathf::Length(incident) * 1e-4f) << n;
    }
}

// -------------------------------------------------------------------- refract
TEST(VectorGeometry, RefractBendsTowardTheNormalEnteringDenserMedium) {
    const Vector3 incident = mathf::Normalize(Vector3(1, -1, 0));
    const Vector3 normal = Vector3::UnitY();
    const Vector3 out = mathf::Refract(incident, normal, 1.0f / 1.5f);

    EXPECT_FALSE(out == Vector3::Zero()) << "should not be total internal reflection";
    EXPECT_NEAR(mathf::Length(out), 1.0f, 1e-4f);
    // Bending toward the normal means a steeper descent: |y| grows.
    EXPECT_GT(std::abs(out.y), std::abs(incident.y));
}

TEST(VectorGeometry, RefractReturnsZeroOnTotalInternalReflection) {
    // Leaving glass for air at a shallow angle: past the critical angle.
    const Vector3 incident = mathf::Normalize(Vector3(1, -0.05f, 0));
    const Vector3 out = mathf::Refract(incident, Vector3::UnitY(), 1.5f);
    EXPECT_TRUE(out == Vector3::Zero());
}

// ------------------------------------------- every width, not just Vector3
// Vector2 and Vector3 take the scalar path; Vector4 takes the branchless
// Select-based one. They are separate implementations of the same contract, so
// testing only Vector3 -- as the first version of this file did -- left
// NormalizeWide entirely unexercised.
static_assert(mathf::Normalize(Vector2{3, 4}).x == 0.6f);
static_assert(mathf::Normalize(Vector4{0, 0, 3, 4}).w == 0.8f);
static_assert(mathf::Saturate(Vector4{-1, 5, 0.5f, 2}).y == 1.0f);
static_assert(mathf::Clamp(Vector2{-1, 5}, Vector2::Zero(), Vector2::One()).x == 0.0f);
static_assert(mathf::Reflect(Vector4{0, -1, 0, 0}, Vector4::UnitY()).y == 1.0f);

TEST(VectorAllWidths, NormalizeProducesUnitLength) {
    EXPECT_TRUE(mathf::NearEqual(mathf::Normalize(Vector2(3, 4)),
                                 Vector2(0.6f, 0.8f)));
    EXPECT_NEAR(mathf::Length(mathf::Normalize(Vector4(1, 2, 3, 4))), 1.0f, 1e-5f);
    EXPECT_NEAR(mathf::Length(mathf::Normalize(Vector2(-7, 11))), 1.0f, 1e-5f);
}

// The degenerate contract has to hold on the SIMD path too, not just the scalar
// one -- Vector4 reaches it through completely different code.
TEST(VectorAllWidths, NormalizeDegenerateCasesHoldAtEveryWidth) {
    EXPECT_TRUE(mathf::Normalize(Vector2::Zero()) == Vector2::Zero());
    EXPECT_TRUE(mathf::Normalize(Vector3::Zero()) == Vector3::Zero());
    EXPECT_TRUE(mathf::Normalize(Vector4::Zero()) == Vector4::Zero());

    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(std::isnan(mathf::Normalize(Vector2(Opaque(inf), 0.0f)).x));
    EXPECT_TRUE(std::isnan(mathf::Normalize(Vector4(Opaque(inf), 0, 0, 0)).x));
}

TEST(VectorAllWidths, NormalizeEstAtEveryWidth) {
    EXPECT_NEAR(mathf::Length(mathf::NormalizeEst(Vector2(3, 4))), 1.0f, 4e-3f);
    EXPECT_NEAR(mathf::Length(mathf::NormalizeEst(Vector4(1, 2, 3, 4))), 1.0f, 4e-3f);
}

TEST(VectorAllWidths, LaneWiseMathAtEveryWidth) {
    EXPECT_TRUE(mathf::Abs(Vector2(-1, 2)) == Vector2(1, 2));
    EXPECT_TRUE(mathf::Abs(Vector4(-1, 2, -3, 4)) == Vector4(1, 2, 3, 4));

    EXPECT_TRUE(mathf::Min(Vector2(1, 5), Vector2(4, 2)) == Vector2(1, 2));
    EXPECT_TRUE(mathf::Max(Vector4(1, 5, 3, 7), Vector4(4, 2, 6, 0)) ==
                Vector4(4, 5, 6, 7));

    EXPECT_TRUE(mathf::Saturate(Vector4(-1, 5, 0.5f, 2)) == Vector4(0, 1, 0.5f, 1));
    EXPECT_TRUE(mathf::Clamp(Vector2(-1, 5), Vector2::Zero(), Vector2::One()) ==
                Vector2(0, 1));

    EXPECT_TRUE(mathf::Lerp(Vector2(0, 0), Vector2(10, 20), 0.5f) == Vector2(5, 10));
    EXPECT_TRUE(mathf::Lerp(Vector4(0, 0, 0, 0), Vector4(10, 20, 30, 40), 0.5f) ==
                Vector4(5, 10, 15, 20));
}

TEST(VectorAllWidths, ReflectAndRefractAtEveryWidth) {
    EXPECT_TRUE(mathf::NearEqual(mathf::Reflect(Vector2(0, -1), Vector2::UnitY()),
                                 Vector2(0, 1)));
    EXPECT_TRUE(mathf::NearEqual(
        mathf::Reflect(Vector4(0, -1, 0, 0), Vector4::UnitY()),
        Vector4(0, 1, 0, 0)));

    // Total internal reflection returns zero at every width.
    const Vector2 shallow = mathf::Normalize(Vector2(1, -0.05f));
    EXPECT_TRUE(mathf::Refract(shallow, Vector2::UnitY(), 1.5f) == Vector2::Zero());
}

TEST(VectorAllWidths, NearEqualUsesOnlyItsOwnLanes) {
    EXPECT_TRUE(mathf::NearEqual(Vector2(1, 2), Vector2(1, 2.000001f)));
    EXPECT_FALSE(mathf::NearEqual(Vector2(1, 2), Vector2(1, 2.1f)));
    EXPECT_TRUE(mathf::NearEqual(Vector4(1, 2, 3, 4), Vector4(1, 2, 3, 4.000001f)));
    EXPECT_FALSE(mathf::NearEqual(Vector4(1, 2, 3, 4), Vector4(1, 2, 3, 4.1f)));
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHF_TEST_HAS_DXMATH
namespace {

DirectX::XMVECTOR ToXm(Vector3 v) {
    return DirectX::XMVectorSet(v.x, v.y, v.z, 0.0f);
}

Vector3 FromXm3(DirectX::FXMVECTOR v) {
    DirectX::XMFLOAT3 out{};
    DirectX::XMStoreFloat3(&out, v);
    return Vector3{out.x, out.y, out.z};
}

float FirstLane(DirectX::FXMVECTOR v) { return DirectX::XMVectorGetX(v); }

} // namespace

TEST(VectorDxParity, DotLengthAndCrossMatchDirectXMath) {
    RandomVectors gen(kSeed + 80);
    for (int n = 0; n < kSamples; ++n) {
        const Vector3 a = RandomVector3(gen);
        const Vector3 b = RandomVector3(gen);
        const DirectX::XMVECTOR xa = ToXm(a);
        const DirectX::XMVECTOR xb = ToXm(b);

        const float terms = std::abs(a.x * b.x) + std::abs(a.y * b.y) +
                            std::abs(a.z * b.z);
        EXPECT_NEAR(mathf::Dot(a, b), FirstLane(DirectX::XMVector3Dot(xa, xb)),
                    std::max(1e-6f, terms * 2e-6f)) << n;

        EXPECT_NEAR(mathf::Length(a), FirstLane(DirectX::XMVector3Length(xa)),
                    mathf::Length(a) * 1e-5f) << n;

        EXPECT_TRUE(mathf::NearEqual(mathf::Cross(a, b),
                                     FromXm3(DirectX::XMVector3Cross(xa, xb)),
                                     1e-2f)) << n;
    }
}

TEST(VectorDxParity, NormalizeMatchesDirectXMath) {
    RandomVectors gen(kSeed + 81);
    for (int n = 0; n < kSamples; ++n) {
        const Vector3 a = RandomVector3(gen);
        EXPECT_TRUE(mathf::NearEqual(
            mathf::Normalize(a),
            FromXm3(DirectX::XMVector3Normalize(ToXm(a))), 1e-5f)) << n;
    }
}

// The degenerate cases are the ones worth matching exactly, because they are
// the ones a caller will not have thought about.
TEST(VectorDxParity, NormalizeDegenerateCasesMatchDirectXMath) {
    const Vector3 zeroResult = mathf::Normalize(Vector3::Zero());
    const Vector3 dxZeroResult =
        FromXm3(DirectX::XMVector3Normalize(ToXm(Vector3::Zero())));
    EXPECT_TRUE(zeroResult == dxZeroResult)
        << "zero-length normalize must agree with DirectXMath";

    const float inf = std::numeric_limits<float>::infinity();
    const Vector3 infInput{Opaque(inf), 0.0f, 0.0f};
    const Vector3 infResult = mathf::Normalize(infInput);
    const Vector3 dxInfResult =
        FromXm3(DirectX::XMVector3Normalize(ToXm(infInput)));
    EXPECT_EQ(std::isnan(infResult.x), std::isnan(dxInfResult.x))
        << "infinite-length normalize must agree with DirectXMath";
}

TEST(VectorDxParity, ReflectAndRefractMatchDirectXMath) {
    RandomVectors gen(kSeed + 82);
    for (int n = 0; n < kSamples; ++n) {
        const Vector3 incident = mathf::Normalize(RandomVector3(gen));
        const Vector3 normal = mathf::Normalize(RandomVector3(gen));
        if (std::isnan(incident.x) || std::isnan(normal.x)) continue;

        EXPECT_TRUE(mathf::NearEqual(
            mathf::Reflect(incident, normal),
            FromXm3(DirectX::XMVector3Reflect(ToXm(incident), ToXm(normal))),
            1e-4f)) << n;

        constexpr float kEta = 1.0f / 1.33f;
        EXPECT_TRUE(mathf::NearEqual(
            mathf::Refract(incident, normal, kEta),
            FromXm3(DirectX::XMVector3Refract(ToXm(incident), ToXm(normal),
                                              kEta)),
            1e-4f)) << n;
    }
}

TEST(VectorDxParity, LaneWiseMathMatchesDirectXMath) {
    RandomVectors gen(kSeed + 83);
    for (int n = 0; n < kSamples; ++n) {
        const Vector3 a = RandomVector3(gen);
        const Vector3 b = RandomVector3(gen);

        EXPECT_TRUE(mathf::NearEqual(
            mathf::Lerp(a, b, 0.25f),
            FromXm3(DirectX::XMVectorLerp(ToXm(a), ToXm(b), 0.25f)), 1e-3f)) << n;
        EXPECT_TRUE(mathf::NearEqual(
            mathf::Min(a, b),
            FromXm3(DirectX::XMVectorMin(ToXm(a), ToXm(b))))) << n;
        EXPECT_TRUE(mathf::NearEqual(
            mathf::Max(a, b),
            FromXm3(DirectX::XMVectorMax(ToXm(a), ToXm(b))))) << n;
        EXPECT_TRUE(mathf::NearEqual(
            mathf::Saturate(a),
            FromXm3(DirectX::XMVectorSaturate(ToXm(a))))) << n;
    }
}
#endif // MATHF_TEST_HAS_DXMATH
