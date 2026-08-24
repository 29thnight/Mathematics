// Quaternion.
//
// As with the matrices, the conventions matter more than the arithmetic. A
// quaternion library with the multiplication order reversed still composes,
// still normalizes, still round-trips through a matrix, and puts every rotation
// in a scene the wrong way round. So the order is pinned three ways: against a
// hand-computed rotation, against the matrix layer it has to agree with, and
// against DirectXMath.

#include "support/reg_testing.hpp"

#include <mathf/quaternion.hpp>

#include <cmath>
#include <limits>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHF_TEST_HAS_DXMATH 1
#else
#  define MATHF_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace mathf_test;
using mathf::Matrix3x3;
using mathf::Matrix4x4;
using mathf::Quaternion;
using mathf::Vector3;

constexpr float kEps = 1e-5f;

Quaternion RandomRotation(RandomVectors& gen) {
    const Sample s = gen.Next();
    // Any non-degenerate axis will do; the magnitude is normalized away.
    const Vector3 axis{s.f[0], s.f[1], s.f[2]};
    if (mathf::LengthSq(axis) < 1e-6f) return Quaternion::Identity();
    return mathf::QuaternionFromAxisAngle(axis, s.f[3] * 0.03f);
}

bool NearVector(const Vector3& a, const Vector3& b, float eps = kEps) {
    return mathf::NearEqual(a, b, eps);
}

} // namespace

// ---------------------------------------------------------------------- layout
static_assert(sizeof(Quaternion) == 16);
static_assert(std::is_standard_layout_v<Quaternion>);

// A default-constructed rotation must be usable as a rotation. Zero is not one.
static_assert(Quaternion{}.w == 1.0f);
static_assert(Quaternion{} == Quaternion::Identity());

TEST(QuaternionLayout, ScalarIsLast) {
    const Quaternion q{1, 2, 3, 4};
    const float* raw = &q.x;
    EXPECT_FLOAT_EQ(raw[0], 1.0f);
    EXPECT_FLOAT_EQ(raw[3], 4.0f) << "w must be the fourth float, not the first";
}

// ---------------------------------------------------------------- construction
// Hand-computed: a quarter turn about +Z is (0, 0, sin45, cos45).
TEST(QuaternionConstruction, AxisAngleMatchesTheHalfAngleForm) {
    const Quaternion q =
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, mathf::kHalfPi);
    const float r = 0.70710678f;
    EXPECT_NEAR(q.x, 0.0f, kEps);
    EXPECT_NEAR(q.y, 0.0f, kEps);
    EXPECT_NEAR(q.z, r, kEps);
    EXPECT_NEAR(q.w, r, kEps);
}

// Right-handed: about +Z, the +X axis turns toward +Y.
TEST(QuaternionConstruction, RotationIsRightHanded) {
    const Quaternion qz =
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, mathf::kHalfPi);
    EXPECT_TRUE(NearVector(mathf::Rotate(Vector3{1, 0, 0}, qz), Vector3{0, 1, 0}));

    const Quaternion qx =
        mathf::QuaternionFromAxisAngle(Vector3{1, 0, 0}, mathf::kHalfPi);
    EXPECT_TRUE(NearVector(mathf::Rotate(Vector3{0, 1, 0}, qx), Vector3{0, 0, 1}));

    const Quaternion qy =
        mathf::QuaternionFromAxisAngle(Vector3{0, 1, 0}, mathf::kHalfPi);
    EXPECT_TRUE(NearVector(mathf::Rotate(Vector3{0, 0, 1}, qy), Vector3{1, 0, 0}));
}

TEST(QuaternionConstruction, AxisIsNormalizedForYou) {
    const Quaternion a =
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, 0.9f);
    const Quaternion b =
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 17.5f}, 0.9f);
    EXPECT_TRUE(mathf::NearEqual(a, b, kEps));

    // A zero axis has no rotation to describe; identity beats NaN.
    EXPECT_TRUE(mathf::QuaternionFromAxisAngle(Vector3{0, 0, 0}, 1.0f) ==
                Quaternion::Identity());
}

// Each Euler angle turns about the axis its name claims.
TEST(QuaternionConstruction, EulerAnglesUseTheNamedAxes) {
    const float t = 0.6f;
    EXPECT_TRUE(mathf::SameRotation(
        mathf::QuaternionFromPitchYawRoll(t, 0, 0),
        mathf::QuaternionFromAxisAngle(Vector3{1, 0, 0}, t), kEps))
        << "pitch turns about X";
    EXPECT_TRUE(mathf::SameRotation(
        mathf::QuaternionFromPitchYawRoll(0, t, 0),
        mathf::QuaternionFromAxisAngle(Vector3{0, 1, 0}, t), kEps))
        << "yaw turns about Y";
    EXPECT_TRUE(mathf::SameRotation(
        mathf::QuaternionFromPitchYawRoll(0, 0, t),
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, t), kEps))
        << "roll turns about Z";
}

// ------------------------------------------------------------ multiply order
// THE test of this file. `a * b` is "a, then b" -- the reverse of the textbook
// Hamilton product. Rotating +X by a quarter turn about Z lands on +Y; rotating
// that by a quarter turn about X lands on +Z. If the order were flipped the
// answer would be +Y, which is a perfectly plausible vector.
TEST(QuaternionMultiply, AppliesLeftToRight) {
    const Quaternion qz =
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, mathf::kHalfPi);
    const Quaternion qx =
        mathf::QuaternionFromAxisAngle(Vector3{1, 0, 0}, mathf::kHalfPi);

    const Vector3 v{1, 0, 0};
    EXPECT_TRUE(NearVector(mathf::Rotate(v, qz * qx), Vector3{0, 0, 1}))
        << "qz * qx must mean qz first";
    EXPECT_TRUE(NearVector(mathf::Rotate(v, qx * qz), Vector3{0, 1, 0}))
        << "and the other order must differ";
}

TEST(QuaternionMultiply, MatchesRotatingTwice) {
    RandomVectors gen(kSeed + 200);
    for (int n = 0; n < 128; ++n) {
        const Quaternion a = RandomRotation(gen);
        const Quaternion b = RandomRotation(gen);
        const Sample s = gen.Next();
        const Vector3 v{s.f[0], s.f[1], s.f[2]};

        EXPECT_TRUE(NearVector(mathf::Rotate(v, a * b),
                               mathf::Rotate(mathf::Rotate(v, a), b), 1e-2f))
            << n;
    }
}

TEST(QuaternionMultiply, IsAssociativeAndHasAnIdentity) {
    RandomVectors gen(kSeed + 201);
    for (int n = 0; n < 64; ++n) {
        const Quaternion a = RandomRotation(gen);
        const Quaternion b = RandomRotation(gen);
        const Quaternion c = RandomRotation(gen);
        EXPECT_TRUE(mathf::NearEqual((a * b) * c, a * (b * c), 1e-4f)) << n;
        EXPECT_TRUE(mathf::NearEqual(a * Quaternion::Identity(), a, kEps)) << n;
        EXPECT_TRUE(mathf::NearEqual(Quaternion::Identity() * a, a, kEps)) << n;
    }
}

TEST(QuaternionMultiply, DoesNotCommute) {
    const Quaternion a =
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, 0.7f);
    const Quaternion b =
        mathf::QuaternionFromAxisAngle(Vector3{1, 0, 0}, 0.4f);
    EXPECT_FALSE(mathf::NearEqual(a * b, b * a, 1e-3f));
}

// --------------------------------------------------------- matrix agreement
// The reason the product is ordered the way it is: with this convention no
// layer has to reverse anything.
TEST(QuaternionMatrix, CompositionAgreesWithMatrixComposition) {
    RandomVectors gen(kSeed + 202);
    for (int n = 0; n < 64; ++n) {
        const Quaternion a = RandomRotation(gen);
        const Quaternion b = RandomRotation(gen);
        EXPECT_TRUE(mathf::NearEqual(mathf::RotationMatrix(a * b),
                                     mathf::RotationMatrix(a) *
                                         mathf::RotationMatrix(b),
                                     1e-4f)) << n;
    }
}

TEST(QuaternionMatrix, RotatingByHandAgreesWithTheMatrix) {
    RandomVectors gen(kSeed + 203);
    for (int n = 0; n < 64; ++n) {
        const Quaternion q = RandomRotation(gen);
        const Sample s = gen.Next();
        const Vector3 v{s.f[0], s.f[1], s.f[2]};
        EXPECT_TRUE(NearVector(
            mathf::Rotate(v, q),
            mathf::TransformDirection(v, mathf::RotationMatrix(q)), 1e-2f)) << n;
    }
}

TEST(QuaternionMatrix, RotationMatrixIsOrthonormal) {
    RandomVectors gen(kSeed + 204);
    for (int n = 0; n < 64; ++n) {
        const Matrix4x4 m = mathf::RotationMatrix(RandomRotation(gen));
        EXPECT_TRUE(mathf::NearEqual(m * Transpose(m), Matrix4x4::Identity(),
                                     1e-4f)) << n;
        EXPECT_NEAR(Determinant(m), 1.0f, 1e-4f) << n;
    }
}

// QuaternionFromRotationMatrix picks one of four branches by which diagonal
// term is largest. Three of them only run for rotations near 180 degrees, which
// random small rotations never reach -- so they are named explicitly here. The
// naive single-branch formula fails exactly on these.
TEST(QuaternionMatrix, RoundTripCoversEveryShepperdBranch) {
    const struct { Vector3 axis; float angle; const char* branch; } cases[] = {
        {{0, 0, 1}, 0.3f,          "trace > 0"},
        {{1, 0, 0}, mathf::kPi,    "m00 largest (180 about X)"},
        {{0, 1, 0}, mathf::kPi,    "m11 largest (180 about Y)"},
        {{0, 0, 1}, mathf::kPi,    "m22 largest (180 about Z)"},
        {{1, 1, 0}, mathf::kPi,    "180 about a diagonal"},
        {{1, 2, 3}, 3.0f,          "near 180, off axis"},
    };

    for (const auto& c : cases) {
        const Quaternion q = mathf::QuaternionFromAxisAngle(c.axis, c.angle);
        const Quaternion back =
            mathf::QuaternionFromRotationMatrix(mathf::RotationMatrix(q));
        EXPECT_TRUE(mathf::SameRotation(q, back, 1e-4f)) << c.branch;

        // The 3x3 overload must agree with the 4x4 one.
        const Quaternion back3 =
            mathf::QuaternionFromRotationMatrix(mathf::RotationMatrix3x3(q));
        EXPECT_TRUE(mathf::SameRotation(q, back3, 1e-4f)) << c.branch << " (3x3)";
    }
}

TEST(QuaternionMatrix, RoundTripOverRandomRotations) {
    RandomVectors gen(kSeed + 205);
    for (int n = 0; n < 256; ++n) {
        const Sample s = gen.Next();
        const Vector3 axis{s.f[0], s.f[1], s.f[2]};
        if (mathf::LengthSq(axis) < 1e-6f) continue;
        // Full angular range, so the 180-degree branches get exercised too.
        const Quaternion q = mathf::QuaternionFromAxisAngle(axis, s.f[3] * 0.03f);
        const Quaternion back =
            mathf::QuaternionFromRotationMatrix(mathf::RotationMatrix(q));
        EXPECT_TRUE(mathf::SameRotation(q, back, 1e-3f)) << n;
    }
}

// ---------------------------------------------------------------- inverses
TEST(QuaternionInverse, UndoesTheRotation) {
    RandomVectors gen(kSeed + 206);
    for (int n = 0; n < 64; ++n) {
        const Quaternion q = RandomRotation(gen);
        const Sample s = gen.Next();
        const Vector3 v{s.f[0], s.f[1], s.f[2]};

        EXPECT_TRUE(NearVector(mathf::Rotate(mathf::Rotate(v, q),
                                             mathf::Inverse(q)), v, 1e-2f)) << n;
        EXPECT_TRUE(NearVector(mathf::InverseRotate(mathf::Rotate(v, q), q), v,
                               1e-2f)) << n;
        EXPECT_TRUE(mathf::NearEqual(q * mathf::Inverse(q),
                                     Quaternion::Identity(), 1e-4f)) << n;
    }
}

// For a unit quaternion the two agree; for one that has drifted they must not,
// and Inverse is the one that stays right.
TEST(QuaternionInverse, ConjugateAndInverseDifferOffTheUnitSphere) {
    const Quaternion unit =
        mathf::QuaternionFromAxisAngle(Vector3{0, 1, 0}, 0.8f);
    EXPECT_TRUE(mathf::NearEqual(mathf::Conjugate(unit), mathf::Inverse(unit),
                                 kEps));

    const Quaternion scaled = unit * 3.0f;
    EXPECT_FALSE(mathf::NearEqual(mathf::Conjugate(scaled),
                                  mathf::Inverse(scaled), 1e-3f));
    EXPECT_TRUE(mathf::NearEqual(scaled * mathf::Inverse(scaled),
                                 Quaternion::Identity(), 1e-4f))
        << "Inverse must still invert a non-unit quaternion";
}

TEST(QuaternionNormalize, DegenerateInputGivesIdentityNotNaN) {
    EXPECT_TRUE(mathf::Normalize(Quaternion{0, 0, 0, 0}) ==
                Quaternion::Identity());
    EXPECT_TRUE(mathf::Inverse(Quaternion{0, 0, 0, 0}) ==
                Quaternion::Identity());

    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(mathf::Normalize(Quaternion{inf, 0, 0, 0}) ==
                Quaternion::Identity());
    EXPECT_TRUE(mathf::Normalize(Quaternion{QuietNaN(), 0, 0, 1}) ==
                Quaternion::Identity());

    EXPECT_NEAR(mathf::Length(mathf::Normalize(Quaternion{1, 2, 3, 4})), 1.0f,
                kEps);
}

// ------------------------------------------------------------ interpolation
TEST(QuaternionSlerp, HitsBothEndpoints) {
    RandomVectors gen(kSeed + 207);
    for (int n = 0; n < 32; ++n) {
        const Quaternion a = RandomRotation(gen);
        const Quaternion b = RandomRotation(gen);
        EXPECT_TRUE(mathf::SameRotation(mathf::Slerp(a, b, 0.0f), a, 1e-4f)) << n;
        EXPECT_TRUE(mathf::SameRotation(mathf::Slerp(a, b, 1.0f), b, 1e-4f)) << n;
        EXPECT_TRUE(mathf::SameRotation(mathf::Nlerp(a, b, 0.0f), a, 1e-4f)) << n;
        EXPECT_TRUE(mathf::SameRotation(mathf::Nlerp(a, b, 1.0f), b, 1e-4f)) << n;
    }
}

// The property that separates slerp from nlerp: equal steps in t are equal
// steps in angle.
TEST(QuaternionSlerp, MovesAtConstantAngularSpeed) {
    const Quaternion a = Quaternion::Identity();
    const Quaternion b =
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, 2.0f);

    float previous = 0.0f;
    for (int i = 1; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        const Quaternion q = mathf::Slerp(a, b, t);
        Vector3 axis; float angle = 0.0f;
        mathf::ToAxisAngle(q, axis, angle);
        EXPECT_NEAR(angle, 2.0f * t, 1e-3f) << "t = " << t;
        EXPECT_GT(angle, previous);
        previous = angle;
    }
}

// q and -q are the same rotation, so interpolating toward the far
// representation must not take the long way round.
TEST(QuaternionSlerp, TakesTheShortArc) {
    const Quaternion a = Quaternion::Identity();
    const Quaternion b =
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, mathf::kHalfPi);

    EXPECT_TRUE(mathf::SameRotation(mathf::Slerp(a, b, 0.5f),
                                    mathf::Slerp(a, -b, 0.5f), 1e-4f));
    EXPECT_TRUE(mathf::SameRotation(mathf::Nlerp(a, b, 0.5f),
                                    mathf::Nlerp(a, -b, 0.5f), 1e-4f));

    // Halfway between identity and a quarter turn is an eighth turn either way.
    Vector3 axis; float angle = 0.0f;
    mathf::ToAxisAngle(mathf::Slerp(a, -b, 0.5f), axis, angle);
    EXPECT_NEAR(angle, mathf::kQuarterPi, 1e-3f);
}

TEST(QuaternionSlerp, StaysNormalized) {
    RandomVectors gen(kSeed + 208);
    for (int n = 0; n < 64; ++n) {
        const Quaternion a = RandomRotation(gen);
        const Quaternion b = RandomRotation(gen);
        for (float t = 0.0f; t <= 1.0f; t += 0.125f) {
            EXPECT_NEAR(mathf::Length(mathf::Slerp(a, b, t)), 1.0f, 1e-4f) << n;
            EXPECT_NEAR(mathf::Length(mathf::Nlerp(a, b, t)), 1.0f, 1e-4f) << n;
        }
    }
}

// Where the sine of the half-angle divides to zero. The Nlerp fallback has to
// hand over without a visible seam.
TEST(QuaternionSlerp, HandlesNearlyIdenticalEndpoints) {
    const Quaternion a =
        mathf::QuaternionFromAxisAngle(Vector3{0, 1, 0}, 0.5f);
    for (float delta : {0.0f, 1e-7f, 1e-5f, 1e-3f}) {
        const Quaternion b =
            mathf::QuaternionFromAxisAngle(Vector3{0, 1, 0}, 0.5f + delta);
        const Quaternion mid = mathf::Slerp(a, b, 0.5f);
        EXPECT_FALSE(std::isnan(mid.w)) << delta;
        EXPECT_NEAR(mathf::Length(mid), 1.0f, 1e-4f) << delta;
        EXPECT_TRUE(mathf::SameRotation(mid, a, 1e-2f)) << delta;
    }
    // Identical endpoints must come back unchanged, not as a division by zero.
    EXPECT_TRUE(mathf::SameRotation(mathf::Slerp(a, a, 0.5f), a, 1e-5f));
}

// ------------------------------------------------------------ decomposition
TEST(QuaternionDecompose, AxisAngleRoundTrips) {
    RandomVectors gen(kSeed + 209);
    for (int n = 0; n < 128; ++n) {
        const Sample s = gen.Next();
        const Vector3 axis{s.f[0], s.f[1], s.f[2]};
        if (mathf::LengthSq(axis) < 1e-4f) continue;
        const Quaternion q = mathf::QuaternionFromAxisAngle(axis, s.f[3] * 0.03f);

        Vector3 outAxis; float outAngle = 0.0f;
        mathf::ToAxisAngle(q, outAxis, outAngle);
        EXPECT_TRUE(mathf::SameRotation(
            q, mathf::QuaternionFromAxisAngle(outAxis, outAngle), 1e-3f)) << n;
        EXPECT_NEAR(mathf::Length(outAxis), 1.0f, 1e-4f) << n;
    }
}

// A tiny but non-zero angle: vLength is ~5e-7, so 1/vLength is enormous and any
// sloppiness in the normalize-and-scale shows up as a non-unit axis. The random
// sweeps essentially never land here.
TEST(QuaternionDecompose, TinyAngleKeepsAUnitAxis) {
    for (float angle : {1e-6f, 1e-5f, 1e-4f}) {
        const Quaternion q =
            mathf::QuaternionFromAxisAngle(Vector3{0, 1, 0}, angle);
        Vector3 axis; float outAngle = 0.0f;
        mathf::ToAxisAngle(q, axis, outAngle);
        EXPECT_NEAR(mathf::Length(axis), 1.0f, 1e-4f) << angle;
        // Relative, not absolute: the angle itself is the small quantity here.
        EXPECT_NEAR(outAngle, angle, angle * 1e-2f + 1e-9f) << angle;
        EXPECT_TRUE(NearVector(axis, Vector3{0, 1, 0}, 1e-3f)) << angle;
    }
}

// The identity has no axis. Reporting +X with a zero angle beats dividing by
// the zero-length vector part.
TEST(QuaternionDecompose, IdentityHasADefinedAxis) {
    Vector3 axis; float angle = 1.0f;
    mathf::ToAxisAngle(Quaternion::Identity(), axis, angle);
    EXPECT_TRUE(NearVector(axis, Vector3{1, 0, 0}));
    EXPECT_NEAR(angle, 0.0f, 1e-6f);
}

TEST(QuaternionDecompose, EulerRoundTrips) {
    RandomVectors gen(kSeed + 210);
    for (int n = 0; n < 256; ++n) {
        const Sample s = gen.Next();
        const Vector3 axis{s.f[0], s.f[1], s.f[2]};
        if (mathf::LengthSq(axis) < 1e-4f) continue;
        const Quaternion q = mathf::QuaternionFromAxisAngle(axis, s.f[3] * 0.03f);

        const Vector3 e = mathf::ToEuler(q);
        EXPECT_TRUE(mathf::SameRotation(
            q, mathf::QuaternionFromEuler(e), 1e-3f)) << n;
    }
}

// Pitch at +/-90 degrees. Yaw and roll then act on the same axis and cannot be
// told apart -- that is a property of Euler angles, not a defect. What must
// still hold is that the decomposition names the SAME rotation.
TEST(QuaternionDecompose, GimbalLockStillRoundTripsTheRotation) {
    for (float pitch : {mathf::kHalfPi, -mathf::kHalfPi}) {
        for (float yaw : {0.0f, 0.6f, -1.3f, 2.9f}) {
            for (float roll : {0.0f, 0.4f, -0.9f}) {
                const Quaternion q =
                    mathf::QuaternionFromPitchYawRoll(pitch, yaw, roll);
                const Vector3 e = mathf::ToEuler(q);

                EXPECT_NEAR(std::abs(e.x), mathf::kHalfPi, 1e-3f)
                    << "pitch must come back at the pole";
                EXPECT_NEAR(e.z, 0.0f, 1e-4f)
                    << "the convention puts everything in yaw, roll at zero";
                EXPECT_TRUE(mathf::SameRotation(
                    q, mathf::QuaternionFromEuler(e), 1e-3f))
                    << "pitch " << pitch << " yaw " << yaw << " roll " << roll;
            }
        }
    }
}

// Just off the pole, where the general branch runs but is poorly conditioned.
TEST(QuaternionDecompose, NearGimbalLockRoundTrips) {
    for (float delta : {1e-3f, 1e-4f, 1e-5f}) {
        for (float sign : {1.0f, -1.0f}) {
            const float pitch = sign * (mathf::kHalfPi - delta);
            const Quaternion q =
                mathf::QuaternionFromPitchYawRoll(pitch, 0.7f, -0.5f);
            const Vector3 e = mathf::ToEuler(q);
            EXPECT_TRUE(mathf::SameRotation(
                q, mathf::QuaternionFromEuler(e), 1e-2f)) << delta << " " << sign;
        }
    }
}

// ------------------------------------------------------------------ comparison
TEST(QuaternionCompare, NearEqualRejectsNaN) {
    Quaternion withNan = Quaternion::Identity();
    withNan.y = QuietNaN();
    EXPECT_FALSE(mathf::NearEqual(withNan, Quaternion::Identity()));
    EXPECT_FALSE(mathf::NearEqual(withNan, withNan))
        << "a NaN is near nothing, not even itself";
}

// q and -q are the same rotation but different quaternions, and the two
// questions get different functions.
TEST(QuaternionCompare, SameRotationSeesThroughNegation) {
    const Quaternion q =
        mathf::QuaternionFromAxisAngle(Vector3{1, 2, 3}, 1.1f);
    EXPECT_FALSE(mathf::NearEqual(q, -q, 1e-3f));
    EXPECT_TRUE(mathf::SameRotation(q, -q));
    EXPECT_TRUE(mathf::SameRotation(q, q));

    // And they really do rotate identically.
    const Vector3 v{0.3f, -0.7f, 1.1f};
    EXPECT_TRUE(NearVector(mathf::Rotate(v, q), mathf::Rotate(v, -q)));
}

// ------------------------------------------------------------------- constexpr
// The differentiator against DirectXMath: none of this needs a runtime.
constexpr Quaternion kCompileTimeZ =
    mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, mathf::kHalfPi);
static_assert(kCompileTimeZ.z > 0.7070f && kCompileTimeZ.z < 0.7072f);
static_assert(kCompileTimeZ.w > 0.7070f && kCompileTimeZ.w < 0.7072f);

constexpr Quaternion kCompileTimeProduct = kCompileTimeZ * kCompileTimeZ;
static_assert(kCompileTimeProduct.z > 0.9999f);   // a half turn about Z

static_assert(mathf::Conjugate(kCompileTimeZ).z < 0.0f);
static_assert(mathf::Dot(Quaternion::Identity(), Quaternion::Identity()) == 1.0f);
static_assert(mathf::LengthSq(Quaternion::Identity()) == 1.0f);

constexpr Matrix4x4 kCompileTimeMatrix = mathf::RotationMatrix(kCompileTimeZ);
static_assert(kCompileTimeMatrix.m[0][1] > 0.999f);   // +X row maps to +Y

constexpr Vector3 kCompileTimeRotated =
    mathf::Rotate(Vector3{1, 0, 0}, kCompileTimeZ);
static_assert(kCompileTimeRotated.y > 0.999f);

// Slerp, Nlerp and the decompositions carry constexpr; nothing proved they
// survive constant evaluation until a user tried. Out-parameter functions get
// constexpr wrapper functions, since a static_assert cannot bind an lvalue.
constexpr Quaternion kCompileTimeSlerp =
    mathf::Slerp(Quaternion::Identity(), kCompileTimeZ, 0.5f);
static_assert(kCompileTimeSlerp.w > 0.9f);   // an eighth turn about Z
static_assert(kCompileTimeSlerp.z > 0.38f && kCompileTimeSlerp.z < 0.39f);

constexpr Quaternion kCompileTimeNlerp =
    mathf::Nlerp(Quaternion::Identity(), kCompileTimeZ, 0.25f);
static_assert(kCompileTimeNlerp.w > 0.9f);

constexpr Vector3 kCompileTimeEuler = mathf::ToEuler(kCompileTimeZ);
static_assert(kCompileTimeEuler.z > 1.57f && kCompileTimeEuler.z < 1.58f);
static_assert(kCompileTimeEuler.x < 1e-5f && kCompileTimeEuler.x > -1e-5f);

namespace {
constexpr float CompileTimeAxisAngle() {
    Vector3 axis; float angle = 0.0f;
    mathf::ToAxisAngle(kCompileTimeZ, axis, angle);
    return angle + axis.z;   // both halves observable in one value
}
} // namespace
static_assert(CompileTimeAxisAngle() > 2.57f);   // pi/2 + 1.0

// The whole pipeline in one constant expression.
constexpr Quaternion kRoundTripped =
    mathf::QuaternionFromRotationMatrix(mathf::RotationMatrix(kCompileTimeZ));
static_assert(kRoundTripped.z > 0.7070f && kRoundTripped.z < 0.7072f);

// Compile time and run time run the same code, so they agree to within the one
// rounding a compiler is allowed to move: Clang fuses the multiply-adds inside
// the trigonometric polynomials at run time and constant evaluation never
// fuses. Measured in ULP rather than with an epsilon, because a few ULP is the
// real bound and an epsilon would hide a genuine divergence at small magnitudes.
TEST(QuaternionConstexpr, CompileTimeMatchesRuntimeToWithinAFewUlp) {
    const Quaternion runtime = mathf::QuaternionFromAxisAngle(
        Vector3{Opaque(0.0f), Opaque(0.0f), Opaque(1.0f)},
        Opaque(mathf::kHalfPi));
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(SameToWithin(runtime[i], kCompileTimeZ[i])) << "lane " << i;
    }

    const Quaternion runtimeProduct = runtime * runtime;
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(SameToWithin(runtimeProduct[i], kCompileTimeProduct[i])) << "lane " << i;
    }

    const Matrix4x4 runtimeMatrix = mathf::RotationMatrix(runtime);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            EXPECT_TRUE(SameToWithin(runtimeMatrix.m[i][j], kCompileTimeMatrix.m[i][j])) << i << "," << j;
        }
    }
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHF_TEST_HAS_DXMATH
namespace {

DirectX::XMVECTOR ToXm(const Quaternion& q) {
    return DirectX::XMVectorSet(q.x, q.y, q.z, q.w);
}

bool MatchesXm(const Quaternion& mine, DirectX::FXMVECTOR theirs, float eps) {
    DirectX::XMFLOAT4 f{};
    DirectX::XMStoreFloat4(&f, theirs);
    return mathf::NearEqual(mine, Quaternion{f.x, f.y, f.z, f.w}, eps);
}

} // namespace

TEST(QuaternionDxParity, ConstructionMatchesDirectXMath) {
    RandomVectors gen(kSeed + 220);
    for (int n = 0; n < 128; ++n) {
        const Sample s = gen.Next();
        const Vector3 axis{s.f[0], s.f[1], s.f[2]};
        if (mathf::LengthSq(axis) < 1e-4f) continue;
        const float angle = s.f[3] * 0.03f;

        EXPECT_TRUE(MatchesXm(
            mathf::QuaternionFromAxisAngle(axis, angle),
            DirectX::XMQuaternionRotationAxis(
                DirectX::XMVectorSet(axis.x, axis.y, axis.z, 0.0f), angle),
            1e-5f)) << n;

        const float p = s.f[0] * 0.03f, y = s.f[1] * 0.03f, r = s.f[2] * 0.03f;
        EXPECT_TRUE(MatchesXm(
            mathf::QuaternionFromPitchYawRoll(p, y, r),
            DirectX::XMQuaternionRotationRollPitchYaw(p, y, r), 1e-5f)) << n;
    }
}

// The convention check that matters most. XMQuaternionMultiply(a, b) is
// documented as "a followed by b", and Mathf has to mean the same thing.
TEST(QuaternionDxParity, MultiplyOrderMatchesDirectXMath) {
    RandomVectors gen(kSeed + 221);
    for (int n = 0; n < 128; ++n) {
        const Quaternion a = RandomRotation(gen);
        const Quaternion b = RandomRotation(gen);
        EXPECT_TRUE(MatchesXm(
            a * b, DirectX::XMQuaternionMultiply(ToXm(a), ToXm(b)), 1e-5f)) << n;
    }
}

TEST(QuaternionDxParity, MatrixAndSlerpMatchDirectXMath) {
    RandomVectors gen(kSeed + 222);
    for (int n = 0; n < 64; ++n) {
        const Quaternion a = RandomRotation(gen);
        const Quaternion b = RandomRotation(gen);

        DirectX::XMFLOAT4X4 theirs{};
        DirectX::XMStoreFloat4x4(
            &theirs, DirectX::XMMatrixRotationQuaternion(ToXm(a)));
        const Matrix4x4 mine = mathf::RotationMatrix(a);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                EXPECT_NEAR(mine.m[i][j], theirs.m[i][j], 1e-5f) << n;
            }
        }

        const float t = 0.25f + 0.5f * static_cast<float>(n % 3);
        EXPECT_TRUE(MatchesXm(
            mathf::Slerp(a, b, t),
            DirectX::XMQuaternionSlerp(ToXm(a), ToXm(b), t), 1e-4f)) << n;

        EXPECT_TRUE(MatchesXm(
            mathf::Inverse(a), DirectX::XMQuaternionInverse(ToXm(a)), 1e-5f)) << n;
    }
}
#endif // MATHF_TEST_HAS_DXMATH
