// Plane, Ray, Sphere, AABB and the queries between them.
//
// The conventions here are the kind that produce plausible wrong answers: a
// flipped plane sign puts everything on the far side, a min/max box read as
// centre/extents is a different box entirely, and a raycast that accepts hits
// behind the origin makes objects visible through the camera's back. Each is
// pinned by a hand-computed case, and where DirectXCollision has an equivalent
// the result is compared against it too.

#include "support/reg_testing.hpp"

#include <mathf/geometry.hpp>

#include <cmath>
#include <limits>

#if __has_include(<DirectXCollision.h>)
#  include <DirectXCollision.h>
#  include <DirectXMath.h>
#  define MATHF_TEST_HAS_DXCOLLISION 1
#else
#  define MATHF_TEST_HAS_DXCOLLISION 0
#endif

namespace {

using namespace mathf_test;
using mathf::AABB;
using mathf::Containment;
using mathf::Plane;
using mathf::PlaneSide;
using mathf::Ray;
using mathf::Sphere;
using mathf::Vector3;

constexpr float kEps = 1e-5f;

} // namespace

// ------------------------------------------------------------------- layout
static_assert(sizeof(Plane) == 16);
static_assert(sizeof(Sphere) == 16);
static_assert(sizeof(AABB) == 24);
static_assert(sizeof(Ray) == 24);

// A default primitive must be a usable one, not a degenerate that poisons
// every query -- the same reason Quaternion defaults to the identity.
static_assert(Plane{}.c == 1.0f);
static_assert(Ray{}.direction.z == 1.0f);

// --------------------------------------------------------------------- plane
// Hand-computed: the plane through (0,0,5) facing +Z is z - 5 = 0, so d = -5.
TEST(Plane, StoresMinusDotNormalPoint) {
    const Plane p = mathf::PlaneFromPointNormal(Vector3{0, 0, 5}, Vector3{0, 0, 1});
    EXPECT_NEAR(p.a, 0.0f, kEps);
    EXPECT_NEAR(p.b, 0.0f, kEps);
    EXPECT_NEAR(p.c, 1.0f, kEps);
    EXPECT_NEAR(p.d, -5.0f, kEps) << "d is MINUS dot(normal, point)";
}

// The sign convention: positive means the side the normal points to.
TEST(Plane, PositiveDistanceIsTheNormalSide) {
    const Plane p = mathf::PlaneFromPointNormal(Vector3{0, 0, 5}, Vector3{0, 0, 1});
    EXPECT_NEAR(mathf::SignedDistance(p, Vector3{0, 0, 9}), 4.0f, kEps);
    EXPECT_NEAR(mathf::SignedDistance(p, Vector3{0, 0, 5}), 0.0f, kEps);
    EXPECT_NEAR(mathf::SignedDistance(p, Vector3{0, 0, 0}), -5.0f, kEps);

    EXPECT_EQ(mathf::ClassifyPoint(p, Vector3{0, 0, 9}), PlaneSide::Front);
    EXPECT_EQ(mathf::ClassifyPoint(p, Vector3{0, 0, 0}), PlaneSide::Back);
    EXPECT_EQ(mathf::ClassifyPoint(p, Vector3{0, 0, 5}), PlaneSide::Straddling);
}

// Right-handed winding: Cross(p1 - p0, p2 - p0).
TEST(Plane, FromPointsUsesTheRightHandRule) {
    const Plane p = mathf::PlaneFromPoints(Vector3{0, 0, 0}, Vector3{1, 0, 0},
                                           Vector3{0, 1, 0});
    EXPECT_TRUE(mathf::NearEqual(p.Normal(), Vector3{0, 0, 1}, kEps));
    EXPECT_NEAR(p.d, 0.0f, kEps);

    // Reversing two vertices reverses the normal, which is the whole point of
    // a winding convention.
    const Plane flipped = mathf::PlaneFromPoints(Vector3{0, 0, 0}, Vector3{0, 1, 0},
                                                 Vector3{1, 0, 0});
    EXPECT_TRUE(mathf::NearEqual(flipped.Normal(), Vector3{0, 0, -1}, kEps));
}

TEST(Plane, NormalizeMakesTheDistanceTrue) {
    // The same plane with a normal of length 3. Distances are threefold until
    // it is normalized -- the trap the header warns about.
    const Plane scaled{0, 0, 3, -15};
    EXPECT_NEAR(mathf::SignedDistance(scaled, Vector3{0, 0, 9}), 12.0f, kEps);

    const Plane unit = mathf::Normalize(scaled);
    EXPECT_NEAR(mathf::SignedDistance(unit, Vector3{0, 0, 9}), 4.0f, kEps);
    EXPECT_NEAR(mathf::Length(unit.Normal()), 1.0f, kEps);
}

TEST(Plane, ProjectionAndReflection) {
    const Plane p = mathf::PlaneFromPointNormal(Vector3{0, 0, 5}, Vector3{0, 0, 1});
    EXPECT_TRUE(mathf::NearEqual(
        mathf::ClosestPointOnPlane(p, Vector3{3, 4, 9}), Vector3{3, 4, 5}, kEps));
    // Mirrored through the plane: 9 is 4 in front, so it lands 4 behind.
    EXPECT_TRUE(mathf::NearEqual(
        mathf::ReflectPoint(p, Vector3{3, 4, 9}), Vector3{3, 4, 1}, kEps));
    // A point on the plane is its own reflection.
    EXPECT_TRUE(mathf::NearEqual(
        mathf::ReflectPoint(p, Vector3{3, 4, 5}), Vector3{3, 4, 5}, kEps));
}

TEST(Plane, FlipReversesEverySide) {
    const Plane p = mathf::PlaneFromPointNormal(Vector3{0, 0, 5}, Vector3{0, 0, 1});
    const Plane f = mathf::Flip(p);
    EXPECT_NEAR(mathf::SignedDistance(f, Vector3{0, 0, 9}), -4.0f, kEps);
    // Flipping does not move the plane: points on it stay on it.
    EXPECT_NEAR(mathf::SignedDistance(f, Vector3{0, 0, 5}), 0.0f, kEps);
}

TEST(Plane, DegenerateNormalGivesTheDefaultNotNaN) {
    EXPECT_TRUE(mathf::PlaneFromPointNormal(Vector3{1, 2, 3}, Vector3{0, 0, 0}) ==
                Plane{});
    // Three collinear points span no plane.
    EXPECT_TRUE(mathf::PlaneFromPoints(Vector3{0, 0, 0}, Vector3{1, 1, 1},
                                       Vector3{2, 2, 2}) == Plane{});
    EXPECT_TRUE(mathf::Normalize(Plane{0, 0, 0, 5}) == Plane{});
    EXPECT_TRUE(mathf::Normalize(Plane{QuietNaN(), 0, 1, 0}) == Plane{});
}

TEST(Plane, NearEqualRejectsNaN) {
    Plane withNan{0, 0, 1, 0};
    withNan.d = QuietNaN();
    EXPECT_FALSE(mathf::NearEqual(withNan, Plane{}));
    EXPECT_FALSE(mathf::NearEqual(withNan, withNan));
}

// ----------------------------------------------------------------------- AABB
// The trap: two Vector3s can mean centre/extents or min/max, and the wrong
// reading compiles.
TEST(AABB, StoresCenterAndHalfWidths) {
    const AABB box{Vector3{0, 0, 0}, Vector3{1, 2, 3}};
    EXPECT_TRUE(mathf::NearEqual(box.Min(), Vector3{-1, -2, -3}, kEps));
    EXPECT_TRUE(mathf::NearEqual(box.Max(), Vector3{1, 2, 3}, kEps));

    // FromMinMax reads the same two vectors the other way, deliberately.
    const AABB same = AABB::FromMinMax(Vector3{-1, -2, -3}, Vector3{1, 2, 3});
    EXPECT_TRUE(mathf::NearEqual(same, box, kEps));

    const AABB offset = AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{2, 4, 6});
    EXPECT_TRUE(mathf::NearEqual(offset.center, Vector3{1, 2, 3}, kEps));
    EXPECT_TRUE(mathf::NearEqual(offset.extents, Vector3{1, 2, 3}, kEps));
}

// Bit 0 is X, bit 1 is Y, bit 2 is Z; set means the maximum side.
TEST(AABB, CornersAreIndexedByBit) {
    const AABB box = AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{1, 2, 4});
    EXPECT_TRUE(mathf::NearEqual(box.Corner(0), Vector3{0, 0, 0}, kEps));
    EXPECT_TRUE(mathf::NearEqual(box.Corner(1), Vector3{1, 0, 0}, kEps));
    EXPECT_TRUE(mathf::NearEqual(box.Corner(2), Vector3{0, 2, 0}, kEps));
    EXPECT_TRUE(mathf::NearEqual(box.Corner(4), Vector3{0, 0, 4}, kEps));
    EXPECT_TRUE(mathf::NearEqual(box.Corner(7), Vector3{1, 2, 4}, kEps));

    // Every corner must lie in the box, and all eight must be distinct.
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(mathf::Intersects(box, box.Corner(i))) << i;
        for (int j = i + 1; j < 8; ++j) {
            EXPECT_FALSE(mathf::NearEqual(box.Corner(i), box.Corner(j), kEps))
                << i << " vs " << j;
        }
    }
}

TEST(AABB, MergeAndExpand) {
    const AABB a = AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{1, 1, 1});
    EXPECT_TRUE(mathf::NearEqual(
        mathf::Merge(a, Vector3{3, 0, 0}),
        AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{3, 1, 1}), kEps));
    // A point already inside changes nothing.
    EXPECT_TRUE(mathf::NearEqual(mathf::Merge(a, Vector3{0.5f, 0.5f, 0.5f}), a,
                                 kEps));

    const AABB b = AABB::FromMinMax(Vector3{-2, 0, 0}, Vector3{0, 5, 1});
    EXPECT_TRUE(mathf::NearEqual(
        mathf::Merge(a, b),
        AABB::FromMinMax(Vector3{-2, 0, 0}, Vector3{1, 5, 1}), kEps));
    // Merge is symmetric.
    EXPECT_TRUE(mathf::NearEqual(mathf::Merge(a, b), mathf::Merge(b, a), kEps));

    EXPECT_TRUE(mathf::NearEqual(
        mathf::Expand(a, 1.0f),
        AABB::FromMinMax(Vector3{-1, -1, -1}, Vector3{2, 2, 2}), kEps));
}

TEST(AABB, FromPointsIsTheTightestBox) {
    const Vector3 points[] = {{1, 5, -2}, {-3, 0, 4}, {0, 2, 1}};
    const AABB box = mathf::AABBFromPoints(points, 3);
    EXPECT_TRUE(mathf::NearEqual(box.Min(), Vector3{-3, 0, -2}, kEps));
    EXPECT_TRUE(mathf::NearEqual(box.Max(), Vector3{1, 5, 4}, kEps));
    for (const Vector3& p : points) EXPECT_TRUE(mathf::Intersects(box, p));

    // Empty and null ranges give the empty box rather than reading memory.
    EXPECT_TRUE(mathf::AABBFromPoints(points, 0) == AABB{});
    EXPECT_TRUE(mathf::AABBFromPoints(nullptr, 3) == AABB{});
    // One point makes a degenerate box at that point.
    EXPECT_TRUE(mathf::NearEqual(mathf::AABBFromPoints(points, 1),
                                 AABB{Vector3{1, 5, -2}, Vector3{0, 0, 0}}, kEps));
}

TEST(AABB, ClosestPointClampsPerAxis) {
    const AABB box = AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{2, 2, 2});
    EXPECT_TRUE(mathf::NearEqual(mathf::ClosestPoint(box, Vector3{5, 1, -3}),
                                 Vector3{2, 1, 0}, kEps));
    // A point inside is its own closest point.
    EXPECT_TRUE(mathf::NearEqual(mathf::ClosestPoint(box, Vector3{1, 1, 1}),
                                 Vector3{1, 1, 1}, kEps));
}

// --------------------------------------------------------------------- sphere
TEST(Sphere, GrowingToSwallowAPoint) {
    const Sphere s{Vector3{0, 0, 0}, 1.0f};
    // Inside: unchanged.
    EXPECT_TRUE(mathf::NearEqual(mathf::Merge(s, Vector3{0.5f, 0, 0}), s, kEps));

    // A point 3 away: the result must reach both it and the far side of the
    // original, so radius 2 centred at (1,0,0).
    const Sphere grown = mathf::Merge(s, Vector3{3, 0, 0});
    EXPECT_NEAR(grown.radius, 2.0f, kEps);
    EXPECT_TRUE(mathf::NearEqual(grown.center, Vector3{1, 0, 0}, kEps));
    EXPECT_TRUE(mathf::Intersects(grown, Vector3{3, 0, 0}));
    EXPECT_TRUE(mathf::Intersects(grown, Vector3{-1, 0, 0}))
        << "the far side of the original must survive the growth";
}

TEST(Sphere, ClosestPointProjectsOntoTheSurface) {
    const Sphere s{Vector3{0, 0, 0}, 2.0f};
    EXPECT_TRUE(mathf::NearEqual(mathf::ClosestPoint(s, Vector3{10, 0, 0}),
                                 Vector3{2, 0, 0}, kEps));
    EXPECT_TRUE(mathf::NearEqual(mathf::ClosestPoint(s, Vector3{1, 0, 0}),
                                 Vector3{1, 0, 0}, kEps));
    // The centre has no nearest surface point; the centre itself is the only
    // answer that does not invent a direction.
    EXPECT_TRUE(mathf::NearEqual(mathf::ClosestPoint(s, Vector3{0, 0, 0}),
                                 Vector3{0, 0, 0}, kEps));
}

TEST(Sphere, BoundingConversionsRoundTripOutward) {
    const AABB box = AABB::FromMinMax(Vector3{-1, -1, -1}, Vector3{1, 1, 1});
    const Sphere around = mathf::BoundingSphere(box);
    EXPECT_NEAR(around.radius, std::sqrt(3.0f), 1e-4f);
    // Every corner must be inside the sphere that claims to bound the box.
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(mathf::Intersects(around, box.Corner(i))) << i;
    }

    const AABB back = mathf::BoundingBox(around);
    EXPECT_EQ(mathf::Contains(back, box), Containment::Contains);
}

// --------------------------------------------------------------- intersection
TEST(Intersect, TouchingCountsAsIntersecting) {
    // Two unit spheres exactly two apart touch at one point.
    EXPECT_TRUE(mathf::Intersects(Sphere{Vector3{0, 0, 0}, 1.0f},
                                  Sphere{Vector3{2, 0, 0}, 1.0f}));
    EXPECT_FALSE(mathf::Intersects(Sphere{Vector3{0, 0, 0}, 1.0f},
                                   Sphere{Vector3{2.001f, 0, 0}, 1.0f}));

    // Two boxes sharing a face.
    const AABB a = AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{1, 1, 1});
    const AABB b = AABB::FromMinMax(Vector3{1, 0, 0}, Vector3{2, 1, 1});
    EXPECT_TRUE(mathf::Intersects(a, b));

    // A point exactly on the surface.
    EXPECT_TRUE(mathf::Intersects(Sphere{Vector3{0, 0, 0}, 1.0f}, Vector3{1, 0, 0}));
    EXPECT_TRUE(mathf::Intersects(a, Vector3{1, 1, 1}));
}

TEST(Intersect, BoxesOverlapPerAxis) {
    const AABB a = AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{2, 2, 2});
    // Overlapping on two axes but not the third is a miss -- the case a test
    // written with || instead of && would pass.
    EXPECT_FALSE(mathf::Intersects(
        a, AABB::FromMinMax(Vector3{0, 0, 5}, Vector3{2, 2, 7})));
    EXPECT_TRUE(mathf::Intersects(
        a, AABB::FromMinMax(Vector3{1, 1, 1}, Vector3{3, 3, 3})));
}

TEST(Intersect, SphereAgainstBoxUsesTheNearestPoint) {
    const AABB box = AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{1, 1, 1});
    // Diagonally out from a corner: the centre distance is larger than any
    // single axis gap, which a naive per-axis test would get wrong.
    EXPECT_FALSE(mathf::Intersects(box, Sphere{Vector3{2, 2, 2}, 1.0f}));
    EXPECT_TRUE(mathf::Intersects(box, Sphere{Vector3{2, 2, 2}, 1.8f}));
    // Straight out from a face at the same distance IS a hit.
    EXPECT_TRUE(mathf::Intersects(box, Sphere{Vector3{2, 0.5f, 0.5f}, 1.0f}));
    // The argument order must not matter.
    EXPECT_EQ(mathf::Intersects(box, Sphere{Vector3{2, 2, 2}, 1.8f}),
              mathf::Intersects(Sphere{Vector3{2, 2, 2}, 1.8f}, box));
}

// The asymmetry people get backwards: a big volume swallowing a small one is
// Contains only when asked in the right order.
TEST(Intersect, ContainsIsAsymmetric) {
    const AABB box = AABB::FromMinMax(Vector3{-1, -1, -1}, Vector3{1, 1, 1});
    const Sphere small{Vector3{0, 0, 0}, 0.5f};
    const Sphere huge{Vector3{0, 0, 0}, 3.0f};

    EXPECT_EQ(mathf::Contains(box, small), Containment::Contains);
    EXPECT_EQ(mathf::Contains(box, huge), Containment::Intersects)
        << "a sphere swallowing the box is not the box containing the sphere";
    EXPECT_EQ(mathf::Contains(huge, box), Containment::Contains);

    const Sphere apart{Vector3{10, 0, 0}, 1.0f};
    EXPECT_EQ(mathf::Contains(box, apart), Containment::Disjoint);
    EXPECT_EQ(mathf::Contains(apart, box), Containment::Disjoint);
}

TEST(Intersect, ContainsForLikeVolumes) {
    const Sphere outer{Vector3{0, 0, 0}, 5.0f};
    EXPECT_EQ(mathf::Contains(outer, Sphere{Vector3{0, 0, 0}, 1.0f}),
              Containment::Contains);
    // Touching the inside of the wall still counts as contained.
    EXPECT_EQ(mathf::Contains(outer, Sphere{Vector3{4, 0, 0}, 1.0f}),
              Containment::Contains);
    EXPECT_EQ(mathf::Contains(outer, Sphere{Vector3{4.5f, 0, 0}, 1.0f}),
              Containment::Intersects);
    EXPECT_EQ(mathf::Contains(outer, Sphere{Vector3{20, 0, 0}, 1.0f}),
              Containment::Disjoint);

    const AABB big = AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{10, 10, 10});
    EXPECT_EQ(mathf::Contains(big, AABB::FromMinMax(Vector3{1, 1, 1},
                                                    Vector3{2, 2, 2})),
              Containment::Contains);
    EXPECT_EQ(mathf::Contains(big, AABB::FromMinMax(Vector3{9, 1, 1},
                                                    Vector3{11, 2, 2})),
              Containment::Intersects);
}

// ------------------------------------------------------------- versus plane
TEST(Intersect, VolumesClassifyAgainstAPlane) {
    // The XY plane facing +Z.
    const Plane p = mathf::PlaneFromPointNormal(Vector3{0, 0, 0}, Vector3{0, 0, 1});

    EXPECT_EQ(mathf::Classify(Sphere{Vector3{0, 0, 5}, 1.0f}, p), PlaneSide::Front);
    EXPECT_EQ(mathf::Classify(Sphere{Vector3{0, 0, -5}, 1.0f}, p), PlaneSide::Back);
    EXPECT_EQ(mathf::Classify(Sphere{Vector3{0, 0, 0}, 1.0f}, p),
              PlaneSide::Straddling);
    // Exactly touching counts as straddling, not as clearing the plane.
    EXPECT_EQ(mathf::Classify(Sphere{Vector3{0, 0, 1}, 1.0f}, p),
              PlaneSide::Straddling);

    EXPECT_EQ(mathf::Classify(AABB{Vector3{0, 0, 5}, Vector3{1, 1, 1}}, p),
              PlaneSide::Front);
    EXPECT_EQ(mathf::Classify(AABB{Vector3{0, 0, -5}, Vector3{1, 1, 1}}, p),
              PlaneSide::Back);
    EXPECT_EQ(mathf::Classify(AABB{Vector3{0, 0, 0}, Vector3{1, 1, 1}}, p),
              PlaneSide::Straddling);
}

// A box against a diagonal plane: the projected reach is what decides it, and
// a test that used a single extent or a corner would get this wrong.
TEST(Intersect, BoxAgainstADiagonalPlane) {
    const Plane p = mathf::PlaneFromPointNormal(Vector3{0, 0, 0},
                                                Vector3{1, 1, 1});
    const AABB unit{Vector3{0, 0, 0}, Vector3{1, 1, 1}};
    // Reach along the normal is (1+1+1)/sqrt(3) = sqrt(3) ~ 1.732.
    EXPECT_EQ(mathf::Classify(unit, p), PlaneSide::Straddling);

    const float clear = 1.74f;
    EXPECT_EQ(mathf::Classify(
                  AABB{Vector3{clear, clear, clear}, Vector3{1, 1, 1}}, p),
              PlaneSide::Front);
    EXPECT_EQ(mathf::Classify(
                  AABB{Vector3{-clear, -clear, -clear}, Vector3{1, 1, 1}}, p),
              PlaneSide::Back);
}

// ------------------------------------------------------------------ raycast
TEST(Raycast, SphereFromOutside) {
    const Sphere s{Vector3{0, 0, 0}, 1.0f};
    float distance = -1.0f;

    ASSERT_TRUE(mathf::Raycast(Ray{Vector3{0, 0, -5}, Vector3{0, 0, 1}}, s,
                               distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f) << "the NEAR surface, not the far one";
}

// The convention that matters: a ray is a half-line.
TEST(Raycast, PointingAwayIsAMiss) {
    const Sphere s{Vector3{0, 0, 0}, 1.0f};
    float distance = -1.0f;
    EXPECT_FALSE(mathf::Raycast(Ray{Vector3{0, 0, 5}, Vector3{0, 0, 1}}, s,
                                distance))
        << "a hit behind the origin is not a hit";

    const AABB box{Vector3{0, 0, 0}, Vector3{1, 1, 1}};
    EXPECT_FALSE(mathf::Raycast(Ray{Vector3{0, 0, 5}, Vector3{0, 0, 1}}, box,
                                distance));

    const Plane p = mathf::PlaneFromPointNormal(Vector3{0, 0, 0},
                                                Vector3{0, 0, 1});
    EXPECT_FALSE(mathf::Raycast(Ray{Vector3{0, 0, 5}, Vector3{0, 0, 1}}, p,
                                distance));
}

// Zero, for every primitive and every position inside. This is the one
// documented divergence from DirectXMath, and it exists because DirectXMath
// disagrees with itself: from (0,0,0.5) along +Z its BoundingSphere reports
// +0.5 (the exit) and its BoundingBox reports -1.5 (the entry, behind the
// origin). Neither can be matched without breaking the other, so the answer
// here is the one that is true for both and never points backwards.
TEST(Raycast, StartingInsideHitsAtZeroForEveryPrimitive) {
    const Sphere sphere{Vector3{0, 0, 0}, 1.0f};
    const AABB box{Vector3{0, 0, 0}, Vector3{1, 1, 1}};
    const Vector3 inside[] = {{0, 0, 0}, {0, 0, 0.5f}, {0, 0, -0.5f},
                              {0.5f, 0.5f, 0}, {0, 0, 0.99f}};

    for (const Vector3& origin : inside) {
        ASSERT_TRUE(mathf::Intersects(sphere, origin));   // really is inside
        ASSERT_TRUE(mathf::Intersects(box, origin));

        float distance = -1.0f;
        ASSERT_TRUE(mathf::Raycast(Ray{origin, Vector3{0, 0, 1}}, sphere,
                                   distance));
        EXPECT_NEAR(distance, 0.0f, kEps) << origin.z;
        EXPECT_GE(distance, 0.0f) << "never behind the origin";

        distance = -1.0f;
        ASSERT_TRUE(mathf::Raycast(Ray{origin, Vector3{0, 0, 1}}, box,
                                   distance));
        EXPECT_NEAR(distance, 0.0f, kEps) << origin.z;
        EXPECT_GE(distance, 0.0f);
    }
}

TEST(Raycast, BoxSlabsAndTheParallelCase) {
    const AABB box = AABB::FromMinMax(Vector3{-1, -1, -1}, Vector3{1, 1, 1});
    float distance = -1.0f;

    ASSERT_TRUE(mathf::Raycast(Ray{Vector3{-5, 0, 0}, Vector3{1, 0, 0}}, box,
                               distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f);

    // Parallel to two slabs and inside both: hits.
    ASSERT_TRUE(mathf::Raycast(Ray{Vector3{-5, 0.5f, 0.5f}, Vector3{1, 0, 0}},
                               box, distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f);

    // Parallel and OUTSIDE one of them: never hits, however far it travels.
    EXPECT_FALSE(mathf::Raycast(Ray{Vector3{-5, 2, 0}, Vector3{1, 0, 0}}, box,
                                distance));

    // A diagonal that misses the corner.
    EXPECT_FALSE(mathf::Raycast(Ray{Vector3{-5, 5, 0}, Vector3{1, 0, 0}}, box,
                                distance));
}

// A ray grazing exactly along a face is the case where the slab method divides
// zero by zero if it is written without the parallel branch.
TEST(Raycast, GrazingAFaceDoesNotProduceNaN) {
    const AABB box = AABB::FromMinMax(Vector3{-1, -1, -1}, Vector3{1, 1, 1});
    float distance = -1.0f;
    ASSERT_TRUE(mathf::Raycast(Ray{Vector3{-5, 1, 0}, Vector3{1, 0, 0}}, box,
                               distance));
    EXPECT_FALSE(std::isnan(distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f);
}

TEST(Raycast, PlaneIsTwoSidedButNotParallel) {
    const Plane p = mathf::PlaneFromPointNormal(Vector3{0, 0, 5},
                                                Vector3{0, 0, 1});
    float distance = -1.0f;

    // From in front, travelling toward it.
    ASSERT_TRUE(mathf::Raycast(Ray{Vector3{0, 0, 0}, Vector3{0, 0, 1}}, p,
                               distance));
    EXPECT_NEAR(distance, 5.0f, 1e-4f);
    // From behind, travelling toward it -- still a hit.
    ASSERT_TRUE(mathf::Raycast(Ray{Vector3{0, 0, 9}, Vector3{0, 0, -1}}, p,
                               distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f);
    // Parallel: no single distance to report.
    EXPECT_FALSE(mathf::Raycast(Ray{Vector3{0, 0, 0}, Vector3{1, 0, 0}}, p,
                                distance));
}

TEST(Raycast, TriangleBarycentricBounds) {
    const Vector3 v0{0, 0, 0}, v1{1, 0, 0}, v2{0, 1, 0};
    float distance = -1.0f;

    // Straight down through the middle.
    ASSERT_TRUE(mathf::RaycastTriangle(
        Ray{Vector3{0.25f, 0.25f, 5}, Vector3{0, 0, -1}}, v0, v1, v2, distance));
    EXPECT_NEAR(distance, 5.0f, 1e-4f);

    // Outside the hypotenuse -- inside the plane, outside the triangle.
    EXPECT_FALSE(mathf::RaycastTriangle(
        Ray{Vector3{0.8f, 0.8f, 5}, Vector3{0, 0, -1}}, v0, v1, v2, distance));
    // Past a vertex.
    EXPECT_FALSE(mathf::RaycastTriangle(
        Ray{Vector3{-0.1f, 0.5f, 5}, Vector3{0, 0, -1}}, v0, v1, v2, distance));

    // From the other face: still a hit, since this is not single-sided.
    ASSERT_TRUE(mathf::RaycastTriangle(
        Ray{Vector3{0.25f, 0.25f, -5}, Vector3{0, 0, 1}}, v0, v1, v2, distance));
    EXPECT_NEAR(distance, 5.0f, 1e-4f);

    // Parallel to the plane, and a degenerate triangle.
    EXPECT_FALSE(mathf::RaycastTriangle(
        Ray{Vector3{0.25f, 0.25f, 5}, Vector3{1, 0, 0}}, v0, v1, v2, distance));
    EXPECT_FALSE(mathf::RaycastTriangle(
        Ray{Vector3{0, 0, 5}, Vector3{0, 0, -1}}, v0, v1, v1, distance));
}

// The hit point a raycast reports must actually be on the surface, which is a
// stronger statement than the distance being some plausible number.
TEST(Raycast, ReportedDistanceLandsOnTheSurface) {
    RandomVectors gen(kSeed + 400);
    const Sphere s{Vector3{0, 0, 0}, 2.0f};
    int hits = 0;

    for (int n = 0; n < 256; ++n) {
        const Sample sample = gen.Next();
        const Vector3 origin{sample.f[0] * 0.1f, sample.f[1] * 0.1f,
                             sample.f[2] * 0.1f};
        const Vector3 toCenter = s.center - origin;
        if (mathf::LengthSq(toCenter) < 9.0f) continue;   // start outside

        const Ray ray{origin, mathf::Normalize(toCenter)};
        float distance = -1.0f;
        ASSERT_TRUE(mathf::Raycast(ray, s, distance)) << n;
        ++hits;
        // Aimed at the centre, so the hit is exactly one radius short of it.
        EXPECT_NEAR(mathf::Length(ray.PointAt(distance) - s.center), s.radius,
                    1e-3f) << n;
    }
    EXPECT_GT(hits, 100) << "the sweep has to actually exercise the path";
}

TEST(Raycast, DegenerateRayDirection) {
    float distance = -1.0f;
    // A zero direction describes no ray; it must not divide by zero.
    EXPECT_FALSE(mathf::Raycast(Ray{Vector3{0, 0, -5}, Vector3{0, 0, 0}},
                                Sphere{Vector3{0, 0, 0}, 1.0f}, distance));

    const Ray fixed = mathf::NormalizeDirection(
        Ray{Vector3{1, 2, 3}, Vector3{0, 0, 0}});
    EXPECT_TRUE(mathf::NearEqual(fixed.direction, Vector3{0, 0, 1}, kEps));
    EXPECT_NEAR(mathf::Length(
                    mathf::NormalizeDirection(Ray{Vector3{}, Vector3{3, 4, 0}})
                        .direction),
                1.0f, kEps);
}

// -------------------------------------------------------------------- constexpr
// The whole geometry layer usable in a constant expression, which is the line
// DirectXCollision cannot cross.
static_assert(mathf::PlaneFromPointNormal(Vector3{0, 0, 5},
                                          Vector3{0, 0, 2}).d == -5.0f);
static_assert(mathf::SignedDistance(Plane{0, 0, 1, -5}, Vector3{0, 0, 9}) == 4.0f);
static_assert(mathf::ClassifyPoint(Plane{0, 0, 1, -5}, Vector3{0, 0, 9}) ==
              PlaneSide::Front);
static_assert(AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{2, 2, 2}).center.x == 1.0f);
static_assert(AABB{Vector3{0, 0, 0}, Vector3{1, 1, 1}}.Corner(7).z == 1.0f);
static_assert(mathf::Intersects(Sphere{Vector3{0, 0, 0}, 1.0f},
                                Vector3{0.5f, 0, 0}));
static_assert(mathf::Contains(AABB{Vector3{0, 0, 0}, Vector3{5, 5, 5}},
                              Sphere{Vector3{0, 0, 0}, 1.0f}) ==
              Containment::Contains);
static_assert(mathf::Classify(Sphere{Vector3{0, 0, 5}, 1.0f},
                              Plane{0, 0, 1, 0}) == PlaneSide::Front);
static_assert(mathf::Merge(AABB::FromMinMax(Vector3{0, 0, 0}, Vector3{1, 1, 1}),
                           Vector3{3, 0, 0}).Max().x == 3.0f);

namespace {
// Raycast takes an out-parameter, so it needs a wrapper to be asserted.
constexpr float CompileTimeRaycast() {
    float distance = -1.0f;
    const bool hit = mathf::Raycast(Ray{Vector3{0, 0, -5}, Vector3{0, 0, 1}},
                                    Sphere{Vector3{0, 0, 0}, 1.0f}, distance);
    return hit ? distance : -1.0f;
}
constexpr float CompileTimeBoxRaycast() {
    float distance = -1.0f;
    const bool hit = mathf::Raycast(Ray{Vector3{-5, 0, 0}, Vector3{1, 0, 0}},
                                    AABB{Vector3{0, 0, 0}, Vector3{1, 1, 1}},
                                    distance);
    return hit ? distance : -1.0f;
}
} // namespace
static_assert(CompileTimeRaycast() > 3.99f && CompileTimeRaycast() < 4.01f);
static_assert(CompileTimeBoxRaycast() > 3.99f && CompileTimeBoxRaycast() < 4.01f);

// ------------------------------------------------------ DirectXCollision parity
#if MATHF_TEST_HAS_DXCOLLISION
namespace {

DirectX::XMVECTOR ToXm(const Vector3& v) {
    return DirectX::XMVectorSet(v.x, v.y, v.z, 1.0f);
}

DirectX::BoundingBox ToXm(const AABB& box) {
    return DirectX::BoundingBox(
        DirectX::XMFLOAT3(box.center.x, box.center.y, box.center.z),
        DirectX::XMFLOAT3(box.extents.x, box.extents.y, box.extents.z));
}

DirectX::BoundingSphere ToXm(const Sphere& s) {
    return DirectX::BoundingSphere(
        DirectX::XMFLOAT3(s.center.x, s.center.y, s.center.z), s.radius);
}

Containment FromXm(DirectX::ContainmentType c) {
    return c == DirectX::DISJOINT    ? Containment::Disjoint
           : c == DirectX::CONTAINS  ? Containment::Contains
                                     : Containment::Intersects;
}

} // namespace

// The layout check first: if BoundingBox did not mean centre-and-extents the
// same way AABB does, every comparison below would still "pass" while
// describing different boxes.
TEST(GeometryDxParity, BoxLayoutMatchesBoundingBox) {
    const AABB box{Vector3{1, 2, 3}, Vector3{4, 5, 6}};
    const DirectX::BoundingBox theirs = ToXm(box);
    DirectX::XMFLOAT3 corners[8];
    theirs.GetCorners(corners);

    // Their corner set and ours must be the same eight points.
    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (int j = 0; j < 8 && !found; ++j) {
            found = mathf::NearEqual(
                box.Corner(j),
                Vector3{corners[i].x, corners[i].y, corners[i].z}, 1e-4f);
        }
        EXPECT_TRUE(found) << "corner " << i << " has no match";
    }
}

TEST(GeometryDxParity, ContainmentMatchesDirectXCollision) {
    RandomVectors gen(kSeed + 401);
    for (int n = 0; n < 128; ++n) {
        const Sample a = gen.Next();
        const Sample b = gen.Next();
        const AABB box{Vector3{a.f[0] * 0.1f, a.f[1] * 0.1f, a.f[2] * 0.1f},
                       Vector3{std::abs(a.f[3]) * 0.05f + 0.5f,
                               std::abs(b.f[0]) * 0.05f + 0.5f,
                               std::abs(b.f[1]) * 0.05f + 0.5f}};
        const Sphere sphere{
            Vector3{b.f[2] * 0.1f, b.f[3] * 0.1f, a.f[0] * 0.1f},
            std::abs(b.f[1]) * 0.05f + 0.3f};

        EXPECT_EQ(mathf::Contains(box, sphere),
                  FromXm(ToXm(box).Contains(ToXm(sphere)))) << n;
        EXPECT_EQ(mathf::Contains(sphere, box),
                  FromXm(ToXm(sphere).Contains(ToXm(box)))) << n;
        EXPECT_EQ(mathf::Intersects(box, sphere),
                  ToXm(box).Intersects(ToXm(sphere))) << n;
    }
}

TEST(GeometryDxParity, PlaneClassificationMatchesDirectXCollision) {
    RandomVectors gen(kSeed + 402);
    for (int n = 0; n < 128; ++n) {
        const Sample a = gen.Next();
        const Sphere sphere{Vector3{a.f[0] * 0.05f, a.f[1] * 0.05f,
                                    a.f[2] * 0.05f},
                            std::abs(a.f[3]) * 0.02f + 0.5f};
        const Sample b = gen.Next();
        const Vector3 normal{b.f[0], b.f[1], b.f[2]};
        if (mathf::LengthSq(normal) < 1e-3f) continue;

        const Plane p = mathf::PlaneFromPointNormal(
            Vector3{b.f[3] * 0.05f, 0, 0}, normal);
        const DirectX::XMVECTOR theirPlane =
            DirectX::XMVectorSet(p.a, p.b, p.c, p.d);

        const DirectX::PlaneIntersectionType theirs =
            ToXm(sphere).Intersects(theirPlane);
        const PlaneSide mine = mathf::Classify(sphere, p);

        const PlaneSide converted =
            theirs == DirectX::FRONT   ? PlaneSide::Front
            : theirs == DirectX::BACK  ? PlaneSide::Back
                                       : PlaneSide::Straddling;
        EXPECT_EQ(mine, converted) << n;
    }
}

TEST(GeometryDxParity, SphereRaycastMatchesDirectXCollision) {
    RandomVectors gen(kSeed + 403);
    int compared = 0;
    for (int n = 0; n < 128; ++n) {
        const Sample a = gen.Next();
        const Sphere sphere{Vector3{0, 0, 0}, std::abs(a.f[0]) * 0.02f + 1.0f};
        const Vector3 origin{a.f[1] * 0.1f, a.f[2] * 0.1f, a.f[3] * 0.1f};
        // Aimed at a point near the sphere rather than in a uniformly random
        // direction: from ten units out, a random direction misses a unit
        // sphere almost every time, and a sweep that never hits compares
        // nothing. The jitter is wide enough that plenty still miss.
        const Sample b = gen.Next();
        const Vector3 target{b.f[0] * 0.015f, b.f[1] * 0.015f, b.f[2] * 0.015f};
        const Vector3 raw = target - origin;
        if (mathf::LengthSq(raw) < 1e-3f) continue;
        const Vector3 direction = mathf::Normalize(raw);

        // Origins inside the sphere are skipped on purpose: there the two
        // libraries answer different questions (see the note on
        // StartingInsideHitsAtZeroForEveryPrimitive), and comparing them would
        // be asserting that a documented divergence does not exist.
        if (mathf::Intersects(sphere, origin)) continue;

        float mineDistance = -1.0f;
        const bool mineHit =
            mathf::Raycast(Ray{origin, direction}, sphere, mineDistance);

        float theirDistance = -1.0f;
        const bool theirHit = ToXm(sphere).Intersects(ToXm(origin),
                                                      DirectX::XMVectorSet(
                                                          direction.x,
                                                          direction.y,
                                                          direction.z, 0.0f),
                                                      theirDistance);
        ASSERT_EQ(mineHit, theirHit) << n;
        if (mineHit) {
            EXPECT_NEAR(mineDistance, theirDistance, 1e-3f) << n;
            ++compared;
        }
    }
    EXPECT_GT(compared, 20) << "the skip must not have emptied the sweep";
}
#endif // MATHF_TEST_HAS_DXCOLLISION
