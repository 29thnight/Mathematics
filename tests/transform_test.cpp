// Transform, view and projection matrices.
//
// These are the functions where being subtly wrong is invisible: a mirrored
// scene, a depth buffer running backwards, a projection that is fine in the
// middle and wrong at the corners. So the tests here check the properties a
// caller depends on -- where the near plane lands, which way the camera faces,
// what a corner of the frustum maps to -- rather than just comparing sixteen
// floats against sixteen other floats.

#include "support/reg_testing.hpp"

#include <mathf/transform.hpp>

#include <cmath>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHF_TEST_HAS_DXMATH 1
#else
#  define MATHF_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace mathf_test;
using mathf::Matrix4x4;
using mathf::Quaternion;
using mathf::Vector3;
using mathf::Vector4;

constexpr float kEps = 1e-5f;

// A point through a projection, with the perspective divide applied.
Vector3 Project(const Vector3& viewSpace, const Matrix4x4& proj) {
    const Vector4 clip = Vector4{viewSpace.x, viewSpace.y, viewSpace.z, 1.0f} * proj;
    return Vector3{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

} // namespace

// ------------------------------------------------------------ basic transforms
TEST(TransformBasics, ScaleAndTranslate) {
    const Matrix4x4 s = mathf::ScalingMatrix(Vector3{2, 3, 4});
    EXPECT_TRUE(mathf::NearEqual(mathf::TransformPoint(Vector3{1, 1, 1}, s),
                                 Vector3{2, 3, 4}, kEps));

    const Matrix4x4 t = mathf::TranslationMatrix(Vector3{10, 20, 30});
    EXPECT_TRUE(mathf::NearEqual(mathf::TransformPoint(Vector3{1, 2, 3}, t),
                                 Vector3{11, 22, 33}, kEps));
    // A direction ignores translation; that is the whole point of the split.
    EXPECT_TRUE(mathf::NearEqual(mathf::TransformDirection(Vector3{1, 2, 3}, t),
                                 Vector3{1, 2, 3}, kEps));
}

// Right-handed about each axis, matching the quaternion of the same angle.
TEST(TransformBasics, AxisRotationsAreRightHandedAndMatchQuaternions) {
    const float a = 0.7f;
    const struct { Matrix4x4 m; Vector3 axis; const char* name; } cases[] = {
        {mathf::RotationX(a), Vector3{1, 0, 0}, "X"},
        {mathf::RotationY(a), Vector3{0, 1, 0}, "Y"},
        {mathf::RotationZ(a), Vector3{0, 0, 1}, "Z"},
    };
    for (const auto& c : cases) {
        EXPECT_TRUE(mathf::NearEqual(
            c.m, mathf::RotationMatrix(mathf::QuaternionFromAxisAngle(c.axis, a)),
            1e-5f)) << c.name;
    }

    // The concrete right-handed check: about +Z, +X turns toward +Y.
    EXPECT_TRUE(mathf::NearEqual(
        mathf::TransformDirection(Vector3{1, 0, 0},
                                  mathf::RotationZ(mathf::kHalfPi)),
        Vector3{0, 1, 0}, kEps));
}

// ------------------------------------------------------------------ TRS
// Scale first, rotate second, translate last -- so the translation is neither
// scaled nor rotated.
TEST(TransformCompose, AppliesScaleThenRotationThenTranslation) {
    const Vector3 scale{2, 2, 2};
    const Quaternion rot =
        mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1}, mathf::kHalfPi);
    const Vector3 translation{10, 0, 0};

    const Matrix4x4 m = mathf::Compose(scale, rot, translation);

    // +X scales to (2,0,0), rotates to (0,2,0), translates to (10,2,0).
    EXPECT_TRUE(mathf::NearEqual(mathf::TransformPoint(Vector3{1, 0, 0}, m),
                                 Vector3{10, 2, 0}, kEps));
    // The translation must come through unscaled and unrotated.
    EXPECT_TRUE(mathf::NearEqual(m.Translation(), translation, kEps));
}

TEST(TransformCompose, MatchesTheEquivalentMatrixProduct) {
    RandomVectors gen(kSeed + 300);
    for (int n = 0; n < 64; ++n) {
        const Sample s = gen.Next();
        const Vector3 scale{std::abs(s.f[0]) * 0.05f + 0.5f,
                            std::abs(s.f[1]) * 0.05f + 0.5f,
                            std::abs(s.f[2]) * 0.05f + 0.5f};
        const Vector3 translation{s.f[3], s.f[0] * 0.1f, s.f[1] * 0.1f};
        const Quaternion rot = mathf::QuaternionFromAxisAngle(
            Vector3{s.f[0], s.f[1], s.f[2] + 1.0f}, s.f[3] * 0.02f);

        const Matrix4x4 built = mathf::Compose(scale, rot, translation);
        const Matrix4x4 product = mathf::ScalingMatrix(scale) *
                                  mathf::RotationMatrix(rot) *
                                  mathf::TranslationMatrix(translation);
        EXPECT_TRUE(mathf::NearEqual(built, product, 1e-4f)) << n;
    }
}

TEST(TransformDecompose, RoundTripsWhatComposeBuilt) {
    RandomVectors gen(kSeed + 301);
    for (int n = 0; n < 128; ++n) {
        const Sample s = gen.Next();
        const Vector3 scale{std::abs(s.f[0]) * 0.05f + 0.5f,
                            std::abs(s.f[1]) * 0.05f + 0.5f,
                            std::abs(s.f[2]) * 0.05f + 0.5f};
        const Vector3 translation{s.f[3], s.f[0] * 0.1f, s.f[1] * 0.1f};
        const Quaternion rot = mathf::Normalize(mathf::QuaternionFromAxisAngle(
            Vector3{s.f[0], s.f[1], s.f[2] + 1.0f}, s.f[3] * 0.02f));

        const Matrix4x4 m = mathf::Compose(scale, rot, translation);

        Vector3 outScale, outTranslation;
        Quaternion outRot;
        ASSERT_TRUE(mathf::Decompose(m, outScale, outRot, outTranslation)) << n;

        EXPECT_TRUE(mathf::NearEqual(outScale, scale, 1e-3f)) << n;
        EXPECT_TRUE(mathf::NearEqual(outTranslation, translation, 1e-4f)) << n;
        EXPECT_TRUE(mathf::SameRotation(outRot, rot, 1e-3f)) << n;

        // The one that has to hold even where the parts are ambiguous.
        EXPECT_TRUE(mathf::NearEqual(
            mathf::Compose(outScale, outRot, outTranslation), m, 1e-3f)) << n;
    }
}

// A rotation matrix has determinant +1, so a mirror cannot be one. The
// convention is to fold the reflection into a negative X scale; what must hold
// is that recomposing reproduces the original matrix.
TEST(TransformDecompose, FoldsAReflectionIntoNegativeScale) {
    for (const Vector3 mirror : {Vector3{-1, 1, 1}, Vector3{1, -1, 1},
                                 Vector3{1, 1, -1}}) {
        const Matrix4x4 m = mathf::ScalingMatrix(mirror);
        Vector3 scale, translation;
        Quaternion rot;
        ASSERT_TRUE(mathf::Decompose(m, scale, rot, translation));

        EXPECT_LT(scale.x * scale.y * scale.z, 0.0f)
            << "the reflection has to survive somewhere in the scale";
        EXPECT_TRUE(mathf::NearEqual(
            mathf::Compose(scale, rot, translation), m, 1e-5f));
    }
}

// A zero scale destroys the direction of a basis vector; no rotation can be
// recovered from it, so the function says so instead of guessing.
TEST(TransformDecompose, RejectsDegenerateMatrices) {
    Vector3 scale, translation;
    Quaternion rot;

    EXPECT_FALSE(mathf::Decompose(mathf::ScalingMatrix(Vector3{1, 0, 1}), scale,
                                  rot, translation));
    EXPECT_FALSE(mathf::Decompose(mathf::ScalingMatrix(Vector3{0, 0, 0}), scale,
                                  rot, translation));
    EXPECT_FALSE(mathf::Decompose(Matrix4x4{}, scale, rot, translation))
        << "an all-zero matrix has no decomposition";

    Matrix4x4 withNan = Matrix4x4::Identity();
    withNan.m[1][1] = QuietNaN();
    EXPECT_FALSE(mathf::Decompose(withNan, scale, rot, translation));
}

// ------------------------------------------------------------------- view
// The defining property: the camera's own position lands at the origin, and the
// direction it faces lands on +Z (left-handed) or -Z (right-handed).
TEST(TransformView, PlacesTheEyeAtTheOrigin) {
    const Vector3 eye{3, 4, -5};
    const Vector3 target{1, 0, 2};
    const Vector3 up{0, 1, 0};

    for (const Matrix4x4& view : {mathf::LookAtLH(eye, target, up),
                                  mathf::LookAtRH(eye, target, up)}) {
        EXPECT_TRUE(mathf::NearEqual(mathf::TransformPoint(eye, view),
                                     Vector3{0, 0, 0}, 1e-4f));
    }
}

TEST(TransformView, HandednessDecidesWhichWayTheCameraLooks) {
    const Vector3 eye{0, 0, -5};
    const Vector3 target{0, 0, 0};
    const Vector3 up{0, 1, 0};

    // The target sits five units in front of the camera either way; the sign of
    // the view-space z is what the handedness decides.
    const Vector3 lh = mathf::TransformPoint(target, mathf::LookAtLH(eye, target, up));
    const Vector3 rh = mathf::TransformPoint(target, mathf::LookAtRH(eye, target, up));

    EXPECT_NEAR(lh.z, 5.0f, 1e-4f) << "left-handed looks down +Z";
    EXPECT_NEAR(rh.z, -5.0f, 1e-4f) << "right-handed looks down -Z";
}

TEST(TransformView, LookAtAndLookToAgree) {
    const Vector3 eye{2, -1, 4};
    const Vector3 target{-3, 5, 0};
    const Vector3 up{0, 1, 0};
    const Vector3 direction = target - eye;

    EXPECT_TRUE(mathf::NearEqual(mathf::LookAtLH(eye, target, up),
                                 mathf::LookToLH(eye, direction, up), 1e-5f));
    EXPECT_TRUE(mathf::NearEqual(mathf::LookAtRH(eye, target, up),
                                 mathf::LookToRH(eye, direction, up), 1e-5f));
}

// A view matrix is a rigid motion, so it must not stretch anything.
TEST(TransformView, PreservesDistances) {
    const Matrix4x4 view =
        mathf::LookAtLH(Vector3{5, 2, -3}, Vector3{0, 1, 1}, Vector3{0, 1, 0});
    RandomVectors gen(kSeed + 302);
    for (int n = 0; n < 32; ++n) {
        const Sample s = gen.Next();
        const Vector3 a{s.f[0], s.f[1], s.f[2]};
        const Sample s2 = gen.Next();
        const Vector3 b{s2.f[0], s2.f[1], s2.f[2]};

        EXPECT_NEAR(mathf::Distance(mathf::TransformPoint(a, view),
                                    mathf::TransformPoint(b, view)),
                    mathf::Distance(a, b), 1e-2f) << n;
    }
}

// ------------------------------------------------------------- projection
// Direct3D depth: the near plane is 0 and the far plane is 1, not -1 and 1.
TEST(TransformProjection, DepthRunsFromZeroAtNearToOneAtFar) {
    const float nearZ = 0.5f, farZ = 250.0f;

    const Matrix4x4 lh = mathf::PerspectiveFovLH(mathf::kHalfPi, 1.6f, nearZ, farZ);
    EXPECT_NEAR(Project(Vector3{0, 0, nearZ}, lh).z, 0.0f, 1e-4f);
    EXPECT_NEAR(Project(Vector3{0, 0, farZ}, lh).z, 1.0f, 1e-4f);

    const Matrix4x4 rh = mathf::PerspectiveFovRH(mathf::kHalfPi, 1.6f, nearZ, farZ);
    EXPECT_NEAR(Project(Vector3{0, 0, -nearZ}, rh).z, 0.0f, 1e-4f);
    EXPECT_NEAR(Project(Vector3{0, 0, -farZ}, rh).z, 1.0f, 1e-4f);

    const Matrix4x4 ol = mathf::OrthographicLH(4, 4, nearZ, farZ);
    EXPECT_NEAR(Project(Vector3{0, 0, nearZ}, ol).z, 0.0f, 1e-4f);
    EXPECT_NEAR(Project(Vector3{0, 0, farZ}, ol).z, 1.0f, 1e-4f);

    const Matrix4x4 orr = mathf::OrthographicRH(4, 4, nearZ, farZ);
    EXPECT_NEAR(Project(Vector3{0, 0, -nearZ}, orr).z, 0.0f, 1e-4f);
    EXPECT_NEAR(Project(Vector3{0, 0, -farZ}, orr).z, 1.0f, 1e-4f);
}

// Depth must increase monotonically with distance, or the depth buffer sorts
// backwards -- the classic symptom of a swapped near and far.
TEST(TransformProjection, DepthIncreasesWithDistance) {
    const Matrix4x4 p = mathf::PerspectiveFovLH(1.0f, 1.777f, 0.1f, 1000.0f);
    float previous = -1.0f;
    for (float z = 0.1f; z < 1000.0f; z *= 1.7f) {
        const float depth = Project(Vector3{0, 0, z}, p).z;
        EXPECT_GT(depth, previous) << "at z = " << z;
        EXPECT_GE(depth, -1e-5f);
        EXPECT_LE(depth, 1.0f + 1e-5f);
        previous = depth;
    }
}

// The field of view has to be the angle it claims: at the near plane, a point on
// the edge of the vertical field lands exactly on the top of the clip cube.
TEST(TransformProjection, FieldOfViewIsTheAngleItClaims) {
    const float fov = 1.2f;
    const float aspect = 1.5f;
    const float nearZ = 2.0f;
    const Matrix4x4 p = mathf::PerspectiveFovLH(fov, aspect, nearZ, 100.0f);

    // Half the vertical field, at the near plane.
    const float halfHeight = nearZ * mathf::Tan(fov * 0.5f);
    EXPECT_NEAR(Project(Vector3{0, halfHeight, nearZ}, p).y, 1.0f, 1e-4f);
    EXPECT_NEAR(Project(Vector3{0, -halfHeight, nearZ}, p).y, -1.0f, 1e-4f);

    // The horizontal field is the vertical one widened by the aspect ratio.
    const float halfWidth = halfHeight * aspect;
    EXPECT_NEAR(Project(Vector3{halfWidth, 0, nearZ}, p).x, 1.0f, 1e-4f);
}

TEST(TransformProjection, OrthographicMapsTheBoxToTheClipCube) {
    const Matrix4x4 p = mathf::OrthographicLH(8.0f, 6.0f, 1.0f, 51.0f);
    EXPECT_TRUE(mathf::NearEqual(Project(Vector3{4, 3, 1}, p), Vector3{1, 1, 0},
                                 1e-4f));
    EXPECT_TRUE(mathf::NearEqual(Project(Vector3{-4, -3, 51}, p),
                                 Vector3{-1, -1, 1}, 1e-4f));
    // Unlike a perspective projection, parallel lines stay parallel: doubling x
    // doubles the projected x regardless of depth.
    EXPECT_NEAR(Project(Vector3{2, 0, 30}, p).x,
                Project(Vector3{2, 0, 5}, p).x, 1e-5f);
}

TEST(TransformProjection, OffCenterMatchesCenteredWhenSymmetric) {
    const Matrix4x4 centered = mathf::OrthographicLH(8.0f, 6.0f, 1.0f, 51.0f);
    const Matrix4x4 offCenter =
        mathf::OrthographicOffCenterLH(-4.0f, 4.0f, -3.0f, 3.0f, 1.0f, 51.0f);
    EXPECT_TRUE(mathf::NearEqual(centered, offCenter, 1e-5f));

    const Matrix4x4 centeredRh = mathf::OrthographicRH(8.0f, 6.0f, 1.0f, 51.0f);
    const Matrix4x4 offCenterRh =
        mathf::OrthographicOffCenterRH(-4.0f, 4.0f, -3.0f, 3.0f, 1.0f, 51.0f);
    EXPECT_TRUE(mathf::NearEqual(centeredRh, offCenterRh, 1e-5f));
}

// An asymmetric frustum is the point of the off-center form; a symmetric test
// alone would not notice the offset terms being dropped.
TEST(TransformProjection, OffCenterHandlesAnAsymmetricBox) {
    const Matrix4x4 p =
        mathf::OrthographicOffCenterLH(-1.0f, 7.0f, 2.0f, 8.0f, 1.0f, 11.0f);
    EXPECT_TRUE(mathf::NearEqual(Project(Vector3{-1, 2, 1}, p),
                                 Vector3{-1, -1, 0}, 1e-4f));
    EXPECT_TRUE(mathf::NearEqual(Project(Vector3{7, 8, 11}, p),
                                 Vector3{1, 1, 1}, 1e-4f));
    EXPECT_TRUE(mathf::NearEqual(Project(Vector3{3, 5, 1}, p),
                                 Vector3{0, 0, 0}, 1e-4f))
        << "the centre of the box maps to the centre of the near face";
}

// The two ways to spell the same perspective projection.
TEST(TransformProjection, FovAndSizeFormsAgree) {
    const float fov = 0.9f, nearZ = 0.75f, farZ = 300.0f, aspect = 16.0f / 9.0f;
    const float height = 2.0f * nearZ * mathf::Tan(fov * 0.5f);
    const float width = height * aspect;

    EXPECT_TRUE(mathf::NearEqual(mathf::PerspectiveFovLH(fov, aspect, nearZ, farZ),
                                 mathf::PerspectiveLH(width, height, nearZ, farZ),
                                 1e-4f));
    EXPECT_TRUE(mathf::NearEqual(mathf::PerspectiveFovRH(fov, aspect, nearZ, farZ),
                                 mathf::PerspectiveRH(width, height, nearZ, farZ),
                                 1e-4f));
}

// The handed pairs must actually differ, or one of them is quietly wrong.
TEST(TransformProjection, HandednessPairsAreNotTheSameMatrix) {
    EXPECT_FALSE(mathf::NearEqual(mathf::PerspectiveFovLH(1.0f, 1.5f, 1, 100),
                                  mathf::PerspectiveFovRH(1.0f, 1.5f, 1, 100),
                                  1e-3f));
    EXPECT_FALSE(mathf::NearEqual(mathf::OrthographicLH(4, 4, 1, 100),
                                  mathf::OrthographicRH(4, 4, 1, 100), 1e-3f));
    EXPECT_FALSE(mathf::NearEqual(
        mathf::LookAtLH(Vector3{0, 0, -5}, Vector3{}, Vector3{0, 1, 0}),
        mathf::LookAtRH(Vector3{0, 0, -5}, Vector3{}, Vector3{0, 1, 0}), 1e-3f));
}

// ------------------------------------------------------------------- constexpr
static_assert(mathf::ScalingMatrix(Vector3{2, 3, 4})(1, 1) == 3.0f);
static_assert(mathf::TranslationMatrix(Vector3{5, 6, 7})(3, 1) == 6.0f);
static_assert(mathf::RotationZ(0.0f) == Matrix4x4::Identity());

constexpr Matrix4x4 kCompileTimeTrs =
    mathf::Compose(Vector3{2, 2, 2},
                   mathf::QuaternionFromAxisAngle(Vector3{0, 0, 1},
                                                  mathf::kHalfPi),
                   Vector3{10, 0, 0});
static_assert(kCompileTimeTrs.m[3][0] == 10.0f);
static_assert(kCompileTimeTrs.m[0][1] > 1.999f);   // scale 2, quarter turn

constexpr Matrix4x4 kCompileTimeProj =
    mathf::PerspectiveFovLH(mathf::kHalfPi, 1.0f, 1.0f, 100.0f);
static_assert(kCompileTimeProj.m[2][3] == 1.0f);
static_assert(kCompileTimeProj.m[3][3] == 0.0f);

constexpr Matrix4x4 kCompileTimeView =
    mathf::LookAtLH(Vector3{0, 0, -5}, Vector3{0, 0, 0}, Vector3{0, 1, 0});
static_assert(kCompileTimeView.m[3][2] > 4.999f);

// In ULP, for the reason given on the quaternion version: Clang may fuse a
// multiply-add at run time that constant evaluation computed unfused.
TEST(TransformConstexpr, CompileTimeMatchesRuntimeToWithinAFewUlp) {
    auto Compare = [](const Matrix4x4& a, const Matrix4x4& b, const char* what) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                EXPECT_TRUE(SameToWithin(a.m[i][j], b.m[i][j])) << what << " at " << i << "," << j;
            }
        }
    };

    Compare(mathf::Compose(
                Vector3{Opaque(2.0f), Opaque(2.0f), Opaque(2.0f)},
                mathf::QuaternionFromAxisAngle(
                    Vector3{Opaque(0.0f), Opaque(0.0f), Opaque(1.0f)},
                    Opaque(mathf::kHalfPi)),
                Vector3{Opaque(10.0f), Opaque(0.0f), Opaque(0.0f)}),
            kCompileTimeTrs, "Compose");

    Compare(mathf::PerspectiveFovLH(Opaque(mathf::kHalfPi), Opaque(1.0f),
                                    Opaque(1.0f), Opaque(100.0f)),
            kCompileTimeProj, "PerspectiveFovLH");

    Compare(mathf::LookAtLH(Vector3{Opaque(0.0f), Opaque(0.0f), Opaque(-5.0f)},
                            Vector3{Opaque(0.0f), Opaque(0.0f), Opaque(0.0f)},
                            Vector3{Opaque(0.0f), Opaque(1.0f), Opaque(0.0f)}),
            kCompileTimeView, "LookAtLH");
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHF_TEST_HAS_DXMATH
namespace {

bool MatchesXm(const Matrix4x4& mine, DirectX::FXMMATRIX theirs, float eps) {
    DirectX::XMFLOAT4X4 f{};
    DirectX::XMStoreFloat4x4(&f, theirs);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const float d = mine.m[i][j] - f.m[i][j];
            if (!(d <= eps && d >= -eps)) return false;
        }
    }
    return true;
}

} // namespace

TEST(TransformDxParity, ProjectionsMatchDirectXMath) {
    const float fov = 1.1f, aspect = 1.7778f, nearZ = 0.3f, farZ = 500.0f;

    EXPECT_TRUE(MatchesXm(mathf::PerspectiveFovLH(fov, aspect, nearZ, farZ),
                          DirectX::XMMatrixPerspectiveFovLH(fov, aspect, nearZ,
                                                            farZ), 1e-5f));
    EXPECT_TRUE(MatchesXm(mathf::PerspectiveFovRH(fov, aspect, nearZ, farZ),
                          DirectX::XMMatrixPerspectiveFovRH(fov, aspect, nearZ,
                                                            farZ), 1e-5f));
    EXPECT_TRUE(MatchesXm(mathf::PerspectiveLH(4, 3, nearZ, farZ),
                          DirectX::XMMatrixPerspectiveLH(4, 3, nearZ, farZ),
                          1e-5f));
    EXPECT_TRUE(MatchesXm(mathf::PerspectiveRH(4, 3, nearZ, farZ),
                          DirectX::XMMatrixPerspectiveRH(4, 3, nearZ, farZ),
                          1e-5f));
    EXPECT_TRUE(MatchesXm(mathf::OrthographicLH(8, 6, nearZ, farZ),
                          DirectX::XMMatrixOrthographicLH(8, 6, nearZ, farZ),
                          1e-5f));
    EXPECT_TRUE(MatchesXm(mathf::OrthographicRH(8, 6, nearZ, farZ),
                          DirectX::XMMatrixOrthographicRH(8, 6, nearZ, farZ),
                          1e-5f));
    EXPECT_TRUE(MatchesXm(
        mathf::OrthographicOffCenterLH(-1, 7, 2, 8, nearZ, farZ),
        DirectX::XMMatrixOrthographicOffCenterLH(-1, 7, 2, 8, nearZ, farZ),
        1e-5f));
    EXPECT_TRUE(MatchesXm(
        mathf::OrthographicOffCenterRH(-1, 7, 2, 8, nearZ, farZ),
        DirectX::XMMatrixOrthographicOffCenterRH(-1, 7, 2, 8, nearZ, farZ),
        1e-5f));
}

TEST(TransformDxParity, ViewAndBasicTransformsMatchDirectXMath) {
    RandomVectors gen(kSeed + 320);
    for (int n = 0; n < 32; ++n) {
        const Sample s = gen.Next();
        const Vector3 eye{s.f[0], s.f[1], s.f[2]};
        const Sample s2 = gen.Next();
        const Vector3 target{s2.f[0], s2.f[1], s2.f[2]};
        if (mathf::LengthSq(target - eye) < 1.0f) continue;

        const DirectX::XMVECTOR e =
            DirectX::XMVectorSet(eye.x, eye.y, eye.z, 1.0f);
        const DirectX::XMVECTOR t =
            DirectX::XMVectorSet(target.x, target.y, target.z, 1.0f);
        const DirectX::XMVECTOR u = DirectX::XMVectorSet(0, 1, 0, 0);

        EXPECT_TRUE(MatchesXm(mathf::LookAtLH(eye, target, Vector3{0, 1, 0}),
                              DirectX::XMMatrixLookAtLH(e, t, u), 1e-4f)) << n;
        EXPECT_TRUE(MatchesXm(mathf::LookAtRH(eye, target, Vector3{0, 1, 0}),
                              DirectX::XMMatrixLookAtRH(e, t, u), 1e-4f)) << n;

        const float a = s.f[3] * 0.03f;
        EXPECT_TRUE(MatchesXm(mathf::RotationX(a), DirectX::XMMatrixRotationX(a),
                              1e-5f)) << n;
        EXPECT_TRUE(MatchesXm(mathf::RotationY(a), DirectX::XMMatrixRotationY(a),
                              1e-5f)) << n;
        EXPECT_TRUE(MatchesXm(mathf::RotationZ(a), DirectX::XMMatrixRotationZ(a),
                              1e-5f)) << n;
    }
}

// XMMatrixAffineTransformation is DirectXMath's TRS, and it has to agree about
// the order the three parts apply in.
TEST(TransformDxParity, ComposeMatchesAffineTransformation) {
    RandomVectors gen(kSeed + 321);
    for (int n = 0; n < 32; ++n) {
        const Sample s = gen.Next();
        const Vector3 scale{std::abs(s.f[0]) * 0.05f + 0.5f,
                            std::abs(s.f[1]) * 0.05f + 0.5f,
                            std::abs(s.f[2]) * 0.05f + 0.5f};
        const Vector3 translation{s.f[3], s.f[0] * 0.1f, s.f[1] * 0.1f};
        const Quaternion rot = mathf::QuaternionFromPitchYawRoll(
            s.f[0] * 0.02f, s.f[1] * 0.02f, s.f[2] * 0.02f);

        EXPECT_TRUE(MatchesXm(
            mathf::Compose(scale, rot, translation),
            DirectX::XMMatrixAffineTransformation(
                DirectX::XMVectorSet(scale.x, scale.y, scale.z, 0.0f),
                DirectX::XMVectorZero(),
                DirectX::XMVectorSet(rot.x, rot.y, rot.z, rot.w),
                DirectX::XMVectorSet(translation.x, translation.y,
                                     translation.z, 0.0f)),
            1e-4f)) << n;
    }
}
#endif // MATHF_TEST_HAS_DXMATH
