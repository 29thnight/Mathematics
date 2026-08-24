// Every code sample in docs/GUIDE.md, compiled and checked.
//
// Documentation that does not compile is worse than none: it is confidently
// wrong, and the reader trusts it. So the guide's examples live here as real
// tests, and the guide's claims about conventions and degenerate inputs are
// asserted rather than asserted-in-prose.
//
// When the guide changes, this file changes with it.

#include "support/reg_testing.hpp"

#include <mathf/mathf.hpp>

#include <cmath>
#include <limits>

using namespace mathf;

namespace {
constexpr float kEps = 1e-4f;
} // namespace

// -------------------------------------------------- guide section 1: conventions
// "합성 순서: 왼쪽에서 오른쪽, 적용 순서 그대로"
TEST(Guide, CompositionReadsLeftToRight) {
    const Matrix4x4 scale2 = ScalingMatrix(2.0f);
    const Matrix4x4 move10 = TranslationMatrix(Vector3{10, 0, 0});

    // Scale first, then translate: 1 -> 2 -> 12.
    EXPECT_TRUE(NearEqual(TransformPoint(Vector3{1, 0, 0}, scale2 * move10),
                          Vector3{12, 0, 0}, kEps));
    // The other order scales the translation too: 1 -> 11 -> 22.
    EXPECT_TRUE(NearEqual(TransformPoint(Vector3{1, 0, 0}, move10 * scale2),
                          Vector3{22, 0, 0}, kEps));
}

// "쿼터니언 곱 순서가 반대인 이유" -- the three identities the guide prints.
TEST(Guide, TheThreeCompositionIdentities) {
    const Quaternion a = QuaternionFromAxisAngle(Vector3{0, 0, 1}, 0.7f);
    const Quaternion b = QuaternionFromAxisAngle(Vector3{1, 0, 0}, 0.4f);
    const Vector3 v{1, 2, 3};

    EXPECT_TRUE(NearEqual(Rotate(v, a * b), Rotate(Rotate(v, a), b), kEps));
    EXPECT_TRUE(NearEqual(RotationMatrix(a * b),
                          RotationMatrix(a) * RotationMatrix(b), kEps));

    const Matrix4x4 m1 = RotationMatrix(a);
    const Matrix4x4 m2 = TranslationMatrix(Vector3{5, 0, 0});
    const Vector4 v4{1, 2, 3, 1};
    EXPECT_TRUE(NearEqual(v4 * (m1 * m2), (v4 * m1) * m2, 1e-3f));
}

// "이동 성분: 3행" -- the row/column question, settled by a value.
TEST(Guide, TranslationLivesInRowThree) {
    const Matrix4x4 t = TranslationMatrix(Vector3{10, 20, 30});
    EXPECT_FLOAT_EQ(t.m[3][0], 10.0f);
    EXPECT_FLOAT_EQ(t.m[0][3], 0.0f) << "not column 3";
    EXPECT_TRUE(NearEqual(t.Translation(), Vector3{10, 20, 30}, kEps));
}

// ---------------------------------------------------- guide section 2: the example
// The five-minute example, run end to end. If the guide's pipeline were
// composed in the wrong order this would put the point somewhere else.
TEST(Guide, TheFiveMinuteExampleRuns) {
    const Quaternion spin =
        QuaternionFromAxisAngle(Vector3{0, 1, 0}, Radians(30.0f));
    const Matrix4x4 world =
        Compose(Vector3{2, 2, 2}, spin, Vector3{10, 0, 5});

    const Matrix4x4 view = LookAtLH(Vector3{0, 5, -10}, Vector3{0, 0, 0},
                                    Vector3{0, 1, 0});
    const Matrix4x4 proj =
        PerspectiveFovLH(Radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

    const Matrix4x4 mvp = world * view * proj;
    const Vector4 clip = Vector4{1, 0, 0, 1} * mvp;
    const Vector3 ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};

    // The object sits in front of the camera, so it must land inside the clip
    // cube with a positive w.
    EXPECT_GT(clip.w, 0.0f) << "behind the camera means the pipeline is wrong";
    EXPECT_GE(ndc.z, 0.0f);
    EXPECT_LE(ndc.z, 1.0f);

    // Composing by hand must agree with the chained product.
    EXPECT_TRUE(NearEqual(Vector4{1, 0, 0, 1} * world * view * proj, clip,
                          1e-3f));
}

TEST(Guide, PointAndDirectionDifferByTranslation) {
    const Matrix4x4 world = Compose(
        Vector3{2, 2, 2},
        QuaternionFromAxisAngle(Vector3{0, 1, 0}, Radians(30.0f)),
        Vector3{10, 0, 5});

    const Vector3 localPos{1, 0, 0};
    EXPECT_FALSE(NearEqual(TransformPoint(localPos, world),
                           TransformDirection(localPos, world), 1e-2f))
        << "if these agreed the translation would not be applied";

    // The guide's note on non-uniform scale: the inverse-transpose is what a
    // normal needs, and it differs from the plain matrix when scale is uneven.
    const Matrix4x4 squashed =
        Compose(Vector3{2, 1, 1}, Quaternion::Identity(), Vector3{0, 0, 0});
    const Vector3 normal = Normalize(Vector3{1, 1, 0});
    const Vector3 wrong = Normalize(TransformDirection(normal, squashed));
    const Vector3 right =
        Normalize(TransformDirection(normal, Transpose(Inverse(squashed))));
    EXPECT_FALSE(NearEqual(wrong, right, 1e-2f))
        << "the guide claims these differ under non-uniform scale";
}

// ------------------------------------------- guide section 2: compile time
constexpr Matrix4x4 kGuideProj = PerspectiveFovLH(kHalfPi, 1.0f, 1.0f, 100.0f);
constexpr Quaternion kGuideTurn =
    QuaternionFromAxisAngle(Vector3{0, 0, 1}, kHalfPi);
constexpr float kGuideSin = Sin(0.5f);
static_assert(Inverse(Matrix4x4::Identity()) == Matrix4x4::Identity());
static_assert(kGuideSin > 0.47f && kGuideSin < 0.48f);
static_assert(kGuideProj.m[2][3] == 1.0f);
static_assert(kGuideTurn.z > 0.7f);

// ------------------------------------------------------ guide section 3: types
TEST(Guide, TheSizeTableIsCorrect) {
    EXPECT_EQ(sizeof(Vector2), 8u);
    EXPECT_EQ(sizeof(Vector3), 12u);
    EXPECT_EQ(sizeof(Vector4), 16u);
    EXPECT_EQ(sizeof(Quaternion), 16u);
    EXPECT_EQ(sizeof(Matrix3x3), 36u);
    EXPECT_EQ(sizeof(Matrix4x4), 64u);
    EXPECT_EQ(sizeof(Plane), 16u);
    EXPECT_EQ(sizeof(Sphere), 16u);
    EXPECT_EQ(sizeof(AABB), 24u);
    EXPECT_EQ(sizeof(Ray), 24u);
    EXPECT_EQ(sizeof(VecReg), 16u);
}

// "AABB의 함정" -- the guide claims these two are DIFFERENT boxes.
TEST(Guide, TheAabbTrapIsReal) {
    const AABB a{Vector3{0, 0, 0}, Vector3{1, 1, 1}};
    const AABB b = AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{1, 1, 1});
    EXPECT_FALSE(NearEqual(a, b, 1e-3f)) << "the guide's warning must be true";

    EXPECT_TRUE(NearEqual(a.Min(), Vector3{-1, -1, -1}, kEps));
    EXPECT_TRUE(NearEqual(b.Min(), Vector3{0, 0, 0}, kEps));
}

// ------------------------------------------- guide section 4: degenerate inputs
// Every row of the guide's table, asserted.
TEST(Guide, TheDegenerateInputTable) {
    const float inf = std::numeric_limits<float>::infinity();

    // Extra parentheses: the preprocessor does not protect commas inside
    // BRACES, so a brace-init at the top level of a macro argument reads as
    // several arguments.
    EXPECT_TRUE((Normalize(Vector3{0, 0, 0}) == Vector3{0, 0, 0}));
    EXPECT_TRUE(std::isnan(Normalize(Vector3{inf, 0, 0}).x));

    const Matrix4x4 singular{1, 2, 3, 4, 2, 4, 6, 8, 9, 10, 11, 13, 14, 15, 17, 19};
    EXPECT_TRUE(Inverse(singular) == Matrix4x4::Identity());
    Matrix4x4 withInf = Matrix4x4::Identity();
    withInf.m[0][0] = inf;
    EXPECT_TRUE(Inverse(withInf) == Matrix4x4::Identity());

    EXPECT_TRUE(Normalize(Quaternion{0, 0, 0, 0}) == Quaternion::Identity());
    EXPECT_TRUE(QuaternionFromAxisAngle(Vector3{0, 0, 0}, 1.0f) ==
                Quaternion::Identity());

    Vector3 scale, translation;
    Quaternion rotation;
    EXPECT_FALSE(Decompose(ScalingMatrix(Vector3{1, 0, 1}), scale, rotation,
                           translation));

    EXPECT_TRUE(LookAtLH(Vector3{0, 0, 0}, Vector3{0, 5, 0}, Vector3{0, 1, 0}) ==
                Matrix4x4::Identity());

    EXPECT_TRUE(PlaneFromPointNormal(Vector3{1, 2, 3}, Vector3{0, 0, 0}) ==
                Plane{});

    EXPECT_TRUE(std::isnan(Sin(9.0e5f)));
    EXPECT_FALSE(std::isnan(Sin(8.0e5f)));
}

// ------------------------------------------------- guide section 5: geometry
// "Contains는 비대칭이다"
TEST(Guide, ContainsIsAsymmetricAsDocumented) {
    const AABB box = AABB::FromMinMax(Vector3{-1, -1, -1}, Vector3{1, 1, 1});
    const Sphere huge{Vector3{0, 0, 0}, 3.0f};
    EXPECT_EQ(Contains(box, huge), Containment::Intersects);
    EXPECT_EQ(Contains(huge, box), Containment::Contains);
}

// "접촉은 교차로 센다"
TEST(Guide, TouchingCountsAsIntersecting) {
    EXPECT_TRUE(Intersects(Sphere{Vector3{0, 0, 0}, 1.0f},
                           Sphere{Vector3{2, 0, 0}, 1.0f}));
}

// "레이는 반직선이다"
TEST(Guide, RayIsAHalfLine) {
    float distance = -1.0f;
    EXPECT_FALSE(Raycast(Ray{Vector3{0, 0, 5}, Vector3{0, 0, 1}},
                         Sphere{Vector3{0, 0, 0}, 1.0f}, distance));
    ASSERT_TRUE(Raycast(Ray{Vector3{0, 0, 0}, Vector3{0, 0, 1}},
                        Sphere{Vector3{0, 0, 0}, 1.0f}, distance));
    EXPECT_NEAR(distance, 0.0f, kEps) << "inside means zero";
}

// ------------------------------------- guide section 7: the migration table
// Spot checks on the rows a reader is most likely to trust blindly, each one a
// place where DirectXMath and Mathf could plausibly have disagreed.
TEST(Guide, MigrationTableRowsAreAccurate) {
    // "XMMatrixMultiply(a, b) -> a * b (순서 동일)"
    const Matrix4x4 a = RotationZ(0.3f);
    const Matrix4x4 b = TranslationMatrix(Vector3{1, 2, 3});
    EXPECT_TRUE(NearEqual(TransformPoint(Vector3{1, 0, 0}, a * b),
                          TransformPoint(TransformPoint(Vector3{1, 0, 0}, a), b),
                          kEps));

    // "XMPlaneDotCoord -> SignedDistance"
    const Plane p = PlaneFromPointNormal(Vector3{0, 0, 5}, Vector3{0, 0, 1});
    EXPECT_NEAR(SignedDistance(p, Vector3{0, 0, 9}), 4.0f, kEps);

    // "XMVector3TransformCoord -> TransformPoint",
    // "XMVector3TransformNormal -> TransformDirection"
    const Matrix4x4 t = TranslationMatrix(Vector3{10, 0, 0});
    EXPECT_TRUE(NearEqual(TransformPoint(Vector3{1, 0, 0}, t), Vector3{11, 0, 0},
                          kEps));
    EXPECT_TRUE(NearEqual(TransformDirection(Vector3{1, 0, 0}, t),
                          Vector3{1, 0, 0}, kEps));

    // "XMMatrixAffineTransformation -> Compose(scale, rot, translation)"
    EXPECT_TRUE(NearEqual(
        Compose(Vector3{2, 2, 2}, Quaternion::Identity(), Vector3{1, 0, 0}),
        ScalingMatrix(2.0f) * TranslationMatrix(Vector3{1, 0, 0}), kEps));

    // "PlaneIntersectionType -> PlaneSide (INTERSECTING -> Straddling)"
    EXPECT_EQ(Classify(Sphere{Vector3{0, 0, 0}, 1.0f}, Plane{0, 0, 1, 0}),
              PlaneSide::Straddling);
}

// "Est 이외의 근사 없음" -- the exact form and the estimate must actually
// differ, or the guide is promising a trade that does not exist.
TEST(Guide, EstimateFormsTradeAccuracyForSpeed) {
    const Vector3 v{3, 4, 12};
    const Vector3 exact = Normalize(v);
    const Vector3 estimate = NormalizeEst(v);
    EXPECT_TRUE(NearEqual(exact, estimate, 1e-2f)) << "still close";
    EXPECT_NEAR(Length(exact), 1.0f, 1e-6f);
    EXPECT_NEAR(Length(estimate), 1.0f, 1e-2f);
}
