// plane, ray, sphere, aabb and the queries between them.
//
// The conventions here are the kind that produce plausible wrong answers: a
// flipped plane sign puts everything on the far side, a min/max box read as
// centre/extents is a different box entirely, and a raycast that accepts hits
// behind the origin makes objects visible through the camera's back. Each is
// pinned by a hand-computed case, and where DirectXCollision has an equivalent
// the result is compared against it too.

#include "support/reg_testing.hpp"

#include <mathematics/geometry.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <ranges>
#include <span>

#if __has_include(<DirectXCollision.h>)
#  include <DirectXCollision.h>
#  include <DirectXMath.h>
#  define MATHEMATICS_TEST_HAS_DXCOLLISION 1
#else
#  define MATHEMATICS_TEST_HAS_DXCOLLISION 0
#endif

namespace {

using namespace math_test;
using math::aabb;
using math::containment;
using math::plane;
using math::plane_side;
using math::ray;
using math::sphere;
using math::vector3;

constexpr float epsilon = 1e-5f;

} // namespace

// ------------------------------------------------------------------- layout
static_assert(sizeof(plane) == 16);
static_assert(sizeof(sphere) == 16);
static_assert(sizeof(aabb) == 24);
static_assert(sizeof(ray) == 24);

// A default primitive must be a usable one, not a degenerate that poisons
// every query -- the same reason quaternion defaults to the identity.
static_assert(plane{}.c == 1.0f);
static_assert(ray{}.direction.z == 1.0f);

// --------------------------------------------------------------------- plane
// Hand-computed: the plane through (0,0,5) facing +Z is z - 5 = 0, so d = -5.
TEST(plane, stores_minus_dot_normal_point) {
    const plane p = math::plane_from_point_normal(vector3{0, 0, 5}, vector3{0, 0, 1});
    EXPECT_NEAR(p.a, 0.0f, epsilon);
    EXPECT_NEAR(p.b, 0.0f, epsilon);
    EXPECT_NEAR(p.c, 1.0f, epsilon);
    EXPECT_NEAR(p.d, -5.0f, epsilon) << "d is MINUS dot(normal, point)";
}

// The sign convention: positive means the side the normal points to.
TEST(plane, positive_distance_is_the_normal_side) {
    const plane p = math::plane_from_point_normal(vector3{0, 0, 5}, vector3{0, 0, 1});
    EXPECT_NEAR(math::signed_distance(p, vector3{0, 0, 9}), 4.0f, epsilon);
    EXPECT_NEAR(math::signed_distance(p, vector3{0, 0, 5}), 0.0f, epsilon);
    EXPECT_NEAR(math::signed_distance(p, vector3{0, 0, 0}), -5.0f, epsilon);

    EXPECT_EQ(math::classify_point(p, vector3{0, 0, 9}), plane_side::front);
    EXPECT_EQ(math::classify_point(p, vector3{0, 0, 0}), plane_side::back);
    EXPECT_EQ(math::classify_point(p, vector3{0, 0, 5}), plane_side::straddling);
}

// right-handed winding: cross(p1 - p0, p2 - p0).
TEST(plane, from_points_uses_the_right_hand_rule) {
    const plane p = math::plane_from_points(vector3{0, 0, 0}, vector3{1, 0, 0},
                                           vector3{0, 1, 0});
    EXPECT_TRUE(math::near_equal(p.normal(), vector3{0, 0, 1}, epsilon));
    EXPECT_NEAR(p.d, 0.0f, epsilon);

    // Reversing two vertices reverses the normal, which is the whole point of
    // a winding convention.
    const plane flipped = math::plane_from_points(vector3{0, 0, 0}, vector3{0, 1, 0},
                                                 vector3{1, 0, 0});
    EXPECT_TRUE(math::near_equal(flipped.normal(), vector3{0, 0, -1}, epsilon));
}

TEST(plane, normalize_makes_the_distance_true) {
    // The same plane with a normal of length 3. Distances are threefold until
    // it is normalized -- the trap the header warns about.
    const plane scaled{0, 0, 3, -15};
    EXPECT_NEAR(math::signed_distance(scaled, vector3{0, 0, 9}), 12.0f, epsilon);

    const plane unit = math::normalize(scaled);
    EXPECT_NEAR(math::signed_distance(unit, vector3{0, 0, 9}), 4.0f, epsilon);
    EXPECT_NEAR(math::length(unit.normal()), 1.0f, epsilon);
}

TEST(plane, projection_and_reflection) {
    const plane p = math::plane_from_point_normal(vector3{0, 0, 5}, vector3{0, 0, 1});
    EXPECT_TRUE(math::near_equal(
        math::closest_point_on_plane(p, vector3{3, 4, 9}), vector3{3, 4, 5}, epsilon));
    // Mirrored through the plane: 9 is 4 in front, so it lands 4 behind.
    EXPECT_TRUE(math::near_equal(
        math::reflect_point(p, vector3{3, 4, 9}), vector3{3, 4, 1}, epsilon));
    // A point on the plane is its own reflection.
    EXPECT_TRUE(math::near_equal(
        math::reflect_point(p, vector3{3, 4, 5}), vector3{3, 4, 5}, epsilon));

    // Raw planes are deliberately allowed to be unnormalized. Projection and
    // reflection must therefore divide by |normal|^2, not assume a unit normal.
    const plane scaled{0, 0, 2, -10};
    EXPECT_TRUE(math::near_equal(
        math::closest_point_on_plane(scaled, vector3{3, 4, 9}),
        vector3{3, 4, 5}, epsilon));
    EXPECT_TRUE(math::near_equal(
        math::reflect_point(scaled, vector3{3, 4, 9}),
        vector3{3, 4, 1}, epsilon));

    // A malformed raw plane has no unique answer; preserving the point keeps
    // the library's degenerate-input policy and avoids manufacturing NaN.
    const plane degenerate{0, 0, 0, 5};
    EXPECT_EQ(math::closest_point_on_plane(degenerate, vector3{3, 4, 9}),
              vector3(3, 4, 9));
    EXPECT_EQ(math::reflect_point(degenerate, vector3{3, 4, 9}),
              vector3(3, 4, 9));
}

TEST(plane, flip_reverses_every_side) {
    const plane p = math::plane_from_point_normal(vector3{0, 0, 5}, vector3{0, 0, 1});
    const plane f = math::flip(p);
    EXPECT_NEAR(math::signed_distance(f, vector3{0, 0, 9}), -4.0f, epsilon);
    // Flipping does not move the plane: points on it stay on it.
    EXPECT_NEAR(math::signed_distance(f, vector3{0, 0, 5}), 0.0f, epsilon);
}

TEST(plane, degenerate_normal_gives_the_default_not_na_n) {
    EXPECT_TRUE(math::plane_from_point_normal(vector3{1, 2, 3}, vector3{0, 0, 0}) ==
                plane{});
    // Three collinear points span no plane.
    EXPECT_TRUE(math::plane_from_points(vector3{0, 0, 0}, vector3{1, 1, 1},
                                       vector3{2, 2, 2}) == plane{});
    EXPECT_TRUE(math::normalize(plane{0, 0, 0, 5}) == plane{});
    EXPECT_TRUE(math::normalize(plane{quiet_nan(), 0, 1, 0}) == plane{});
}

TEST(plane, near_equal_rejects_na_n) {
    plane with_nan{0, 0, 1, 0};
    with_nan.d = quiet_nan();
    EXPECT_FALSE(math::near_equal(with_nan, plane{}));
    EXPECT_FALSE(math::near_equal(with_nan, with_nan));
}

// ----------------------------------------------------------------------- aabb
// The trap: two Vector3s can mean centre/extents or min/max, and the wrong
// reading compiles.
TEST(aabb, stores_center_and_half_widths) {
    const aabb box{vector3{0, 0, 0}, vector3{1, 2, 3}};
    EXPECT_TRUE(math::near_equal(box.min(), vector3{-1, -2, -3}, epsilon));
    EXPECT_TRUE(math::near_equal(box.max(), vector3{1, 2, 3}, epsilon));

    // from_min_max reads the same two vectors the other way, deliberately.
    const aabb same = aabb::from_min_max(vector3{-1, -2, -3}, vector3{1, 2, 3});
    EXPECT_TRUE(math::near_equal(same, box, epsilon));

    const aabb offset = aabb::from_min_max(vector3{0, 0, 0}, vector3{2, 4, 6});
    EXPECT_TRUE(math::near_equal(offset.center, vector3{1, 2, 3}, epsilon));
    EXPECT_TRUE(math::near_equal(offset.extents, vector3{1, 2, 3}, epsilon));
}

// Bit 0 is X, bit 1 is Y, bit 2 is Z; set means the maximum side.
TEST(aabb, corners_are_indexed_by_bit) {
    const aabb box = aabb::from_min_max(vector3{0, 0, 0}, vector3{1, 2, 4});
    EXPECT_TRUE(math::near_equal(box.corner(0), vector3{0, 0, 0}, epsilon));
    EXPECT_TRUE(math::near_equal(box.corner(1), vector3{1, 0, 0}, epsilon));
    EXPECT_TRUE(math::near_equal(box.corner(2), vector3{0, 2, 0}, epsilon));
    EXPECT_TRUE(math::near_equal(box.corner(4), vector3{0, 0, 4}, epsilon));
    EXPECT_TRUE(math::near_equal(box.corner(7), vector3{1, 2, 4}, epsilon));

    // Every corner must lie in the box, and all eight must be distinct.
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(math::intersects(box, box.corner(i))) << i;
        for (int j = i + 1; j < 8; ++j) {
            EXPECT_FALSE(math::near_equal(box.corner(i), box.corner(j), epsilon))
                << i << " vs " << j;
        }
    }
}

TEST(aabb, merge_and_expand) {
    const aabb a = aabb::from_min_max(vector3{0, 0, 0}, vector3{1, 1, 1});
    EXPECT_TRUE(math::near_equal(
        math::merge(a, vector3{3, 0, 0}),
        aabb::from_min_max(vector3{0, 0, 0}, vector3{3, 1, 1}), epsilon));
    // A point already inside changes nothing.
    EXPECT_TRUE(math::near_equal(math::merge(a, vector3{0.5f, 0.5f, 0.5f}), a,
                                 epsilon));

    const aabb b = aabb::from_min_max(vector3{-2, 0, 0}, vector3{0, 5, 1});
    EXPECT_TRUE(math::near_equal(
        math::merge(a, b),
        aabb::from_min_max(vector3{-2, 0, 0}, vector3{1, 5, 1}), epsilon));
    // merge is symmetric.
    EXPECT_TRUE(math::near_equal(math::merge(a, b), math::merge(b, a), epsilon));

    EXPECT_TRUE(math::near_equal(
        math::expand(a, 1.0f),
        aabb::from_min_max(vector3{-1, -1, -1}, vector3{2, 2, 2}), epsilon));

    // The default really is empty: it must not silently add the origin to a
    // bound accumulated from points or boxes.
    const aabb empty;
    const vector3 point{5, 6, 7};
    EXPECT_TRUE(empty.is_empty());
    EXPECT_FALSE(math::intersects(empty, vector3{}));
    EXPECT_EQ(math::merge(empty, point), aabb(point, vector3{}));
    EXPECT_EQ(math::merge(empty, a), a);
    EXPECT_EQ(math::merge(a, empty), a);
    EXPECT_TRUE(math::expand(empty, 100.0f).is_empty());
}

TEST(aabb, from_points_is_the_tightest_box) {
    const vector3 points[] = {{1, 5, -2}, {-3, 0, 4}, {0, 2, 1}};
    const aabb box = math::aabb_from_points(points, 3);
    const aabb span_box = math::aabb_from_points(std::span<const vector3>{points});
    EXPECT_TRUE(math::near_equal(box.min(), vector3{-3, 0, -2}, epsilon));
    EXPECT_TRUE(math::near_equal(box.max(), vector3{1, 5, 4}, epsilon));
    EXPECT_EQ(span_box, box) << "span and pointer overloads must agree";
    for (const vector3& p : points) EXPECT_TRUE(math::intersects(box, p));

    // Empty and null ranges give the empty box rather than reading memory.
    EXPECT_TRUE(math::aabb_from_points(points, 0) == aabb{});
    EXPECT_TRUE(math::aabb_from_points(nullptr, 3) == aabb{});
    // one point makes a degenerate box at that point.
    EXPECT_TRUE(math::near_equal(math::aabb_from_points(points, 1),
                                 aabb{vector3{1, 5, -2}, vector3{0, 0, 0}}, epsilon));
}

TEST(aabb, from_points_accepts_lazy_non_contiguous_ranges) {
    struct sample {
        vector3 position;
        bool visible;
    };

    const std::array samples{
        sample{vector3{10, 20, 30}, false},
        sample{vector3{1, 5, -2}, true},
        sample{vector3{-3, 0, 4}, true},
        sample{vector3{100, 200, 300}, false}};

    auto visible_positions =
        samples |
        std::views::filter([](const sample& value) { return value.visible; }) |
        std::views::transform([](const sample& value) { return value.position; });
    const aabb box = math::aabb_from_points(visible_positions);

    EXPECT_EQ(box.min(), vector3(-3, 0, -2));
    EXPECT_EQ(box.max(), vector3(1, 5, 4));

    auto empty_positions =
        samples |
        std::views::filter([](const sample&) { return false; }) |
        std::views::transform([](const sample& value) { return value.position; });
    EXPECT_TRUE(math::aabb_from_points(empty_positions).is_empty());
}

TEST(aabb, midpoint_avoids_extreme_endpoint_overflow) {
    const float largest = std::numeric_limits<float>::max();

    const aabb full = aabb::from_min_max(vector3{-largest, -largest, -largest},
                                         vector3{largest, largest, largest});
    EXPECT_EQ(full.center, vector3{}) << "(-max + max) / 2 is exactly zero";
    EXPECT_EQ(full.extents, vector3(largest));
    EXPECT_TRUE(std::isfinite(full.center.x));
    EXPECT_TRUE(std::isfinite(full.extents.x));

    const aabb point = aabb::from_min_max(vector3(largest), vector3(largest));
    EXPECT_EQ(point.center, vector3(largest));
    EXPECT_EQ(point.extents, vector3{});
    EXPECT_TRUE(std::isfinite(point.center.x))
        << "the former (maximum + maximum) path overflowed here";
}

TEST(aabb, closest_point_clamps_per_axis) {
    const aabb box = aabb::from_min_max(vector3{0, 0, 0}, vector3{2, 2, 2});
    EXPECT_TRUE(math::near_equal(math::closest_point(box, vector3{5, 1, -3}),
                                 vector3{2, 1, 0}, epsilon));
    // A point inside is its own closest point.
    EXPECT_TRUE(math::near_equal(math::closest_point(box, vector3{1, 1, 1}),
                                 vector3{1, 1, 1}, epsilon));
}

// --------------------------------------------------------------------- sphere
TEST(sphere, growing_to_swallow_a_point) {
    const sphere s{vector3{0, 0, 0}, 1.0f};
    // Inside: unchanged.
    EXPECT_TRUE(math::near_equal(math::merge(s, vector3{0.5f, 0, 0}), s, epsilon));

    // A point 3 away: the result must reach both it and the far side of the
    // original, so radius 2 centred at (1,0,0).
    const sphere grown = math::merge(s, vector3{3, 0, 0});
    EXPECT_NEAR(grown.radius, 2.0f, epsilon);
    EXPECT_TRUE(math::near_equal(grown.center, vector3{1, 0, 0}, epsilon));
    EXPECT_TRUE(math::intersects(grown, vector3{3, 0, 0}));
    EXPECT_TRUE(math::intersects(grown, vector3{-1, 0, 0}))
        << "the far side of the original must survive the growth";
}

TEST(sphere, closest_point_projects_onto_the_surface) {
    const sphere s{vector3{0, 0, 0}, 2.0f};
    EXPECT_TRUE(math::near_equal(math::closest_point(s, vector3{10, 0, 0}),
                                 vector3{2, 0, 0}, epsilon));
    EXPECT_TRUE(math::near_equal(math::closest_point(s, vector3{1, 0, 0}),
                                 vector3{1, 0, 0}, epsilon));
    // The centre has no nearest surface point; the centre itself is the only
    // answer that does not invent a direction.
    EXPECT_TRUE(math::near_equal(math::closest_point(s, vector3{0, 0, 0}),
                                 vector3{0, 0, 0}, epsilon));
}

TEST(sphere, bounding_conversions_round_trip_outward) {
    const aabb box = aabb::from_min_max(vector3{-1, -1, -1}, vector3{1, 1, 1});
    const sphere around = math::bounding_sphere(box);
    EXPECT_NEAR(around.radius, std::sqrt(3.0f), 1e-4f);
    // Every corner must be inside the sphere that claims to bound the box.
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(math::intersects(around, box.corner(i))) << i;
    }

    const aabb back = math::bounding_box(around);
    EXPECT_EQ(math::contains(back, box), containment::contains);
}

// --------------------------------------------------------------- intersection
TEST(intersect, touching_counts_as_intersecting) {
    // Two unit spheres exactly two apart touch at one point.
    EXPECT_TRUE(math::intersects(sphere{vector3{0, 0, 0}, 1.0f},
                                  sphere{vector3{2, 0, 0}, 1.0f}));
    EXPECT_FALSE(math::intersects(sphere{vector3{0, 0, 0}, 1.0f},
                                   sphere{vector3{2.001f, 0, 0}, 1.0f}));

    // Two boxes sharing a face.
    const aabb a = aabb::from_min_max(vector3{0, 0, 0}, vector3{1, 1, 1});
    const aabb b = aabb::from_min_max(vector3{1, 0, 0}, vector3{2, 1, 1});
    EXPECT_TRUE(math::intersects(a, b));

    // A point exactly on the surface.
    EXPECT_TRUE(math::intersects(sphere{vector3{0, 0, 0}, 1.0f}, vector3{1, 0, 0}));
    EXPECT_TRUE(math::intersects(a, vector3{1, 1, 1}));
}

TEST(intersect, boxes_overlap_per_axis) {
    const aabb a = aabb::from_min_max(vector3{0, 0, 0}, vector3{2, 2, 2});
    // Overlapping on two axes but not the third is a miss -- the case a test
    // written with || instead of && would pass.
    EXPECT_FALSE(math::intersects(
        a, aabb::from_min_max(vector3{0, 0, 5}, vector3{2, 2, 7})));
    EXPECT_TRUE(math::intersects(
        a, aabb::from_min_max(vector3{1, 1, 1}, vector3{3, 3, 3})));
}

TEST(intersect, sphere_against_box_uses_the_nearest_point) {
    const aabb box = aabb::from_min_max(vector3{0, 0, 0}, vector3{1, 1, 1});
    // Diagonally out from a corner: the centre distance is larger than any
    // single axis gap, which a naive per-axis test would get wrong.
    EXPECT_FALSE(math::intersects(box, sphere{vector3{2, 2, 2}, 1.0f}));
    EXPECT_TRUE(math::intersects(box, sphere{vector3{2, 2, 2}, 1.8f}));
    // Straight out from a face at the same distance IS a hit.
    EXPECT_TRUE(math::intersects(box, sphere{vector3{2, 0.5f, 0.5f}, 1.0f}));
    // The argument order must not matter.
    EXPECT_EQ(math::intersects(box, sphere{vector3{2, 2, 2}, 1.8f}),
              math::intersects(sphere{vector3{2, 2, 2}, 1.8f}, box));
}

// The asymmetry people get backwards: a big volume swallowing a small one is
// contains only when asked in the right order.
TEST(intersect, contains_is_asymmetric) {
    const aabb box = aabb::from_min_max(vector3{-1, -1, -1}, vector3{1, 1, 1});
    const sphere small{vector3{0, 0, 0}, 0.5f};
    const sphere huge{vector3{0, 0, 0}, 3.0f};

    EXPECT_EQ(math::contains(box, small), containment::contains);
    EXPECT_EQ(math::contains(box, huge), containment::intersects)
        << "a sphere swallowing the box is not the box containing the sphere";
    EXPECT_EQ(math::contains(huge, box), containment::contains);

    const sphere apart{vector3{10, 0, 0}, 1.0f};
    EXPECT_EQ(math::contains(box, apart), containment::disjoint);
    EXPECT_EQ(math::contains(apart, box), containment::disjoint);
}

TEST(intersect, contains_for_like_volumes) {
    const sphere outer{vector3{0, 0, 0}, 5.0f};
    EXPECT_EQ(math::contains(outer, sphere{vector3{0, 0, 0}, 1.0f}),
              containment::contains);
    // Touching the inside of the wall still counts as contained.
    EXPECT_EQ(math::contains(outer, sphere{vector3{4, 0, 0}, 1.0f}),
              containment::contains);
    EXPECT_EQ(math::contains(outer, sphere{vector3{4.5f, 0, 0}, 1.0f}),
              containment::intersects);
    EXPECT_EQ(math::contains(outer, sphere{vector3{20, 0, 0}, 1.0f}),
              containment::disjoint);

    const aabb big = aabb::from_min_max(vector3{0, 0, 0}, vector3{10, 10, 10});
    EXPECT_EQ(math::contains(big, aabb::from_min_max(vector3{1, 1, 1},
                                                    vector3{2, 2, 2})),
              containment::contains);
    EXPECT_EQ(math::contains(big, aabb::from_min_max(vector3{9, 1, 1},
                                                    vector3{11, 2, 2})),
              containment::intersects);
}

// ------------------------------------------------------------- versus plane
TEST(intersect, volumes_classify_against_a_plane) {
    // The XY plane facing +Z.
    const plane p = math::plane_from_point_normal(vector3{0, 0, 0}, vector3{0, 0, 1});

    EXPECT_EQ(math::classify(sphere{vector3{0, 0, 5}, 1.0f}, p), plane_side::front);
    EXPECT_EQ(math::classify(sphere{vector3{0, 0, -5}, 1.0f}, p), plane_side::back);
    EXPECT_EQ(math::classify(sphere{vector3{0, 0, 0}, 1.0f}, p),
              plane_side::straddling);
    // Exactly touching counts as straddling, not as clearing the plane.
    EXPECT_EQ(math::classify(sphere{vector3{0, 0, 1}, 1.0f}, p),
              plane_side::straddling);

    EXPECT_EQ(math::classify(aabb{vector3{0, 0, 5}, vector3{1, 1, 1}}, p),
              plane_side::front);
    EXPECT_EQ(math::classify(aabb{vector3{0, 0, -5}, vector3{1, 1, 1}}, p),
              plane_side::back);
    EXPECT_EQ(math::classify(aabb{vector3{0, 0, 0}, vector3{1, 1, 1}}, p),
              plane_side::straddling);
}

// A box against a diagonal plane: the projected reach is what decides it, and
// a test that used a single extent or a corner would get this wrong.
TEST(intersect, box_against_a_diagonal_plane) {
    const plane p = math::plane_from_point_normal(vector3{0, 0, 0},
                                                vector3{1, 1, 1});
    const aabb unit{vector3{0, 0, 0}, vector3{1, 1, 1}};
    // Reach along the normal is (1+1+1)/sqrt(3) = sqrt(3) ~ 1.732.
    EXPECT_EQ(math::classify(unit, p), plane_side::straddling);

    const float clear = 1.74f;
    EXPECT_EQ(math::classify(
                  aabb{vector3{clear, clear, clear}, vector3{1, 1, 1}}, p),
              plane_side::front);
    EXPECT_EQ(math::classify(
                  aabb{vector3{-clear, -clear, -clear}, vector3{1, 1, 1}}, p),
              plane_side::back);
}

// ------------------------------------------------------------------ raycast
TEST(raycast, sphere_from_outside) {
    const sphere s{vector3{0, 0, 0}, 1.0f};
    float distance = -1.0f;

    ASSERT_TRUE(math::raycast(ray{vector3{0, 0, -5}, vector3{0, 0, 1}}, s,
                               distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f) << "the NEAR surface, not the far one";
}

TEST(raycast, optional_overloads_match_the_out_parameter_apis) {
    const ray input_ray{vector3{0, 0, -5}, vector3{0, 0, 1}};
    const sphere input_sphere{vector3{}, 1.0f};
    const aabb box{vector3{}, vector3{1, 1, 1}};
    const plane input_plane = math::plane_from_point_normal(vector3{}, vector3{0, 0, 1});
    const vector3 v0{-1, -1, 0}, v1{1, -1, 0}, v2{0, 1, 0};

    float legacy_distance = -1.0f;
    ASSERT_TRUE(math::raycast(input_ray, input_sphere, legacy_distance));
    ASSERT_TRUE(math::raycast(input_ray, input_sphere).has_value());
    EXPECT_FLOAT_EQ(*math::raycast(input_ray, input_sphere), legacy_distance);

    ASSERT_TRUE(math::raycast(input_ray, box, legacy_distance));
    EXPECT_FLOAT_EQ(math::raycast(input_ray, box).value(), legacy_distance);

    ASSERT_TRUE(math::raycast(input_ray, input_plane, legacy_distance));
    EXPECT_FLOAT_EQ(math::raycast(input_ray, input_plane).value(), legacy_distance);

    ASSERT_TRUE(math::raycast_triangle(input_ray, v0, v1, v2, legacy_distance));
    EXPECT_FLOAT_EQ(math::raycast_triangle(input_ray, v0, v1, v2).value(),
                    legacy_distance);

    const ray away{vector3{0, 0, 5}, vector3{0, 0, 1}};
    EXPECT_FALSE(math::raycast(away, input_sphere).has_value());
    EXPECT_FALSE(math::raycast(away, box).has_value());
    EXPECT_FALSE(math::raycast(away, input_plane).has_value());
    EXPECT_FALSE(math::raycast_triangle(away, v0, v1, v2).has_value());
}

// The convention that matters: a ray is a half-line.
TEST(raycast, pointing_away_is_a_miss) {
    const sphere s{vector3{0, 0, 0}, 1.0f};
    float distance = -1.0f;
    EXPECT_FALSE(math::raycast(ray{vector3{0, 0, 5}, vector3{0, 0, 1}}, s,
                                distance))
        << "a hit behind the origin is not a hit";

    const aabb box{vector3{0, 0, 0}, vector3{1, 1, 1}};
    EXPECT_FALSE(math::raycast(ray{vector3{0, 0, 5}, vector3{0, 0, 1}}, box,
                                distance));

    const plane p = math::plane_from_point_normal(vector3{0, 0, 0},
                                                vector3{0, 0, 1});
    EXPECT_FALSE(math::raycast(ray{vector3{0, 0, 5}, vector3{0, 0, 1}}, p,
                                distance));
}

// zero, for every primitive and every position inside. This is the one
// documented divergence from DirectXMath, and it exists because DirectXMath
// disagrees with itself: from (0,0,0.5) along +Z its bounding_sphere reports
// +0.5 (the exit) and its bounding_box reports -1.5 (the entry, behind the
// origin). Neither can be matched without breaking the other, so the answer
// here is the one that is true for both and never points backwards.
TEST(raycast, starting_inside_hits_at_zero_for_every_primitive) {
    const sphere input_sphere{vector3{0, 0, 0}, 1.0f};
    const aabb box{vector3{0, 0, 0}, vector3{1, 1, 1}};
    const vector3 inside[] = {{0, 0, 0}, {0, 0, 0.5f}, {0, 0, -0.5f},
                              {0.5f, 0.5f, 0}, {0, 0, 0.99f}};

    for (const vector3& origin : inside) {
        ASSERT_TRUE(math::intersects(input_sphere, origin));   // really is inside
        ASSERT_TRUE(math::intersects(box, origin));

        float distance = -1.0f;
        ASSERT_TRUE(math::raycast(ray{origin, vector3{0, 0, 1}}, input_sphere,
                                   distance));
        EXPECT_NEAR(distance, 0.0f, epsilon) << origin.z;
        EXPECT_GE(distance, 0.0f) << "never behind the origin";

        distance = -1.0f;
        ASSERT_TRUE(math::raycast(ray{origin, vector3{0, 0, 1}}, box,
                                   distance));
        EXPECT_NEAR(distance, 0.0f, epsilon) << origin.z;
        EXPECT_GE(distance, 0.0f);
    }
}

TEST(raycast, box_slabs_and_the_parallel_case) {
    const aabb box = aabb::from_min_max(vector3{-1, -1, -1}, vector3{1, 1, 1});
    float distance = -1.0f;

    ASSERT_TRUE(math::raycast(ray{vector3{-5, 0, 0}, vector3{1, 0, 0}}, box,
                               distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f);

    // Parallel to two slabs and inside both: hits.
    ASSERT_TRUE(math::raycast(ray{vector3{-5, 0.5f, 0.5f}, vector3{1, 0, 0}},
                               box, distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f);

    // Parallel and OUTSIDE one of them: never hits, however far it travels.
    EXPECT_FALSE(math::raycast(ray{vector3{-5, 2, 0}, vector3{1, 0, 0}}, box,
                                distance));

    // A diagonal that misses the corner.
    EXPECT_FALSE(math::raycast(ray{vector3{-5, 5, 0}, vector3{1, 0, 0}}, box,
                                distance));
}

// A ray grazing exactly along a face is the case where the slab method divides
// zero by zero if it is written without the parallel branch.
TEST(raycast, grazing_a_face_does_not_produce_na_n) {
    const aabb box = aabb::from_min_max(vector3{-1, -1, -1}, vector3{1, 1, 1});
    float distance = -1.0f;
    ASSERT_TRUE(math::raycast(ray{vector3{-5, 1, 0}, vector3{1, 0, 0}}, box,
                               distance));
    EXPECT_FALSE(std::isnan(distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f);
}

TEST(raycast, plane_is_two_sided_but_not_parallel) {
    const plane p = math::plane_from_point_normal(vector3{0, 0, 5},
                                                vector3{0, 0, 1});
    float distance = -1.0f;

    // From in front, travelling toward it.
    ASSERT_TRUE(math::raycast(ray{vector3{0, 0, 0}, vector3{0, 0, 1}}, p,
                               distance));
    EXPECT_NEAR(distance, 5.0f, 1e-4f);
    // From behind, travelling toward it -- still a hit.
    ASSERT_TRUE(math::raycast(ray{vector3{0, 0, 9}, vector3{0, 0, -1}}, p,
                               distance));
    EXPECT_NEAR(distance, 4.0f, 1e-4f);
    // Parallel: no single distance to report.
    EXPECT_FALSE(math::raycast(ray{vector3{0, 0, 0}, vector3{1, 0, 0}}, p,
                                distance));
}

TEST(raycast, triangle_barycentric_bounds) {
    const vector3 v0{0, 0, 0}, v1{1, 0, 0}, v2{0, 1, 0};
    float distance = -1.0f;

    // Straight down through the middle.
    ASSERT_TRUE(math::raycast_triangle(
        ray{vector3{0.25f, 0.25f, 5}, vector3{0, 0, -1}}, v0, v1, v2, distance));
    EXPECT_NEAR(distance, 5.0f, 1e-4f);

    // Outside the hypotenuse -- inside the plane, outside the triangle.
    EXPECT_FALSE(math::raycast_triangle(
        ray{vector3{0.8f, 0.8f, 5}, vector3{0, 0, -1}}, v0, v1, v2, distance));
    // Past a vertex.
    EXPECT_FALSE(math::raycast_triangle(
        ray{vector3{-0.1f, 0.5f, 5}, vector3{0, 0, -1}}, v0, v1, v2, distance));

    // From the other face: still a hit, since this is not single-sided.
    ASSERT_TRUE(math::raycast_triangle(
        ray{vector3{0.25f, 0.25f, -5}, vector3{0, 0, 1}}, v0, v1, v2, distance));
    EXPECT_NEAR(distance, 5.0f, 1e-4f);

    // Parallel to the plane, and a degenerate triangle.
    EXPECT_FALSE(math::raycast_triangle(
        ray{vector3{0.25f, 0.25f, 5}, vector3{1, 0, 0}}, v0, v1, v2, distance));
    EXPECT_FALSE(math::raycast_triangle(
        ray{vector3{0, 0, 5}, vector3{0, 0, -1}}, v0, v1, v1, distance));
}

// The hit point a raycast reports must actually be on the surface, which is a
// stronger statement than the distance being some plausible number.
TEST(raycast, reported_distance_lands_on_the_surface) {
    random_vectors gen(random_seed + 400);
    const sphere s{vector3{0, 0, 0}, 2.0f};
    int hits = 0;

    for (int n = 0; n < 256; ++n) {
        const sample input_sample = gen.next();
        const vector3 origin{input_sample.f[0] * 0.1f, input_sample.f[1] * 0.1f,
                             input_sample.f[2] * 0.1f};
        const vector3 to_center = s.center - origin;
        if (math::length_sq(to_center) < 9.0f) continue;   // start outside

        const ray input_ray{origin, math::normalize(to_center)};
        float distance = -1.0f;
        ASSERT_TRUE(math::raycast(input_ray, s, distance)) << n;
        ++hits;
        // Aimed at the centre, so the hit is exactly one radius short of it.
        EXPECT_NEAR(math::length(input_ray.point_at(distance) - s.center), s.radius,
                    1e-3f) << n;
    }
    EXPECT_GT(hits, 100) << "the sweep has to actually exercise the path";
}

TEST(raycast, degenerate_ray_direction) {
    float distance = -1.0f;
    // A zero direction describes no ray; it must not divide by zero.
    EXPECT_FALSE(math::raycast(ray{vector3{0, 0, -5}, vector3{0, 0, 0}},
                                sphere{vector3{0, 0, 0}, 1.0f}, distance));
    EXPECT_FALSE(math::raycast(ray{vector3{0, 0, 0}, vector3{0, 0, 0}},
                                aabb{vector3{0, 0, 0}, vector3{1, 1, 1}},
                                distance))
        << "a zero direction is not a ray even when its origin is inside";

    const ray fixed = math::normalize_direction(
        ray{vector3{1, 2, 3}, vector3{0, 0, 0}});
    EXPECT_TRUE(math::near_equal(fixed.direction, vector3{0, 0, 1}, epsilon));
    EXPECT_NEAR(math::length(
                    math::normalize_direction(ray{vector3{}, vector3{3, 4, 0}})
                        .direction),
                1.0f, epsilon);
}

// -------------------------------------------------------------------- constexpr
// The whole geometry layer usable in a constant expression, which is the line
// DirectXCollision cannot cross.
static_assert(math::plane_from_point_normal(vector3{0, 0, 5},
                                          vector3{0, 0, 2}).d == -5.0f);
static_assert(math::signed_distance(plane{0, 0, 1, -5}, vector3{0, 0, 9}) == 4.0f);
static_assert(math::classify_point(plane{0, 0, 1, -5}, vector3{0, 0, 9}) ==
              plane_side::front);
static_assert(aabb::from_min_max(vector3{0, 0, 0}, vector3{2, 2, 2}).center.x == 1.0f);
static_assert(aabb{vector3{0, 0, 0}, vector3{1, 1, 1}}.corner(7).z == 1.0f);
static_assert(aabb{}.is_empty());
static_assert(math::merge(aabb{}, vector3{4, 5, 6}) ==
              aabb{vector3{4, 5, 6}, vector3{0, 0, 0}});
static_assert(math::intersects(sphere{vector3{0, 0, 0}, 1.0f},
                                vector3{0.5f, 0, 0}));
static_assert(math::contains(aabb{vector3{0, 0, 0}, vector3{5, 5, 5}},
                              sphere{vector3{0, 0, 0}, 1.0f}) ==
              containment::contains);
static_assert(math::classify(sphere{vector3{0, 0, 5}, 1.0f},
                              plane{0, 0, 1, 0}) == plane_side::front);
static_assert(math::merge(aabb::from_min_max(vector3{0, 0, 0}, vector3{1, 1, 1}),
                           vector3{3, 0, 0}).max().x == 3.0f);
constexpr std::array compile_time_points{
    vector3{1, 5, -2}, vector3{-3, 0, 4}, vector3{0, 2, 1}};
constexpr aabb compile_time_points_box =
    math::aabb_from_points(std::span<const vector3>{compile_time_points});
static_assert(compile_time_points_box.min() == vector3{-3, 0, -2});
static_assert(compile_time_points_box.max() == vector3{1, 5, 4});
constexpr aabb compile_time_range_points_box =
    math::aabb_from_points(compile_time_points);
static_assert(compile_time_range_points_box == compile_time_points_box);
constexpr float compile_time_largest = std::numeric_limits<float>::max();
static_assert(aabb::from_min_max(vector3{-compile_time_largest},
                                 vector3{compile_time_largest}).center == vector3{});
static_assert(aabb::from_min_max(vector3{compile_time_largest},
                                 vector3{compile_time_largest}).center ==
              vector3{compile_time_largest});

namespace {
// raycast takes an out-parameter, so it needs a wrapper to be asserted.
constexpr float compile_time_raycast() {
    float distance = -1.0f;
    const bool hit = math::raycast(ray{vector3{0, 0, -5}, vector3{0, 0, 1}},
                                    sphere{vector3{0, 0, 0}, 1.0f}, distance);
    return hit ? distance : -1.0f;
}
constexpr float compile_time_box_raycast() {
    float distance = -1.0f;
    const bool hit = math::raycast(ray{vector3{-5, 0, 0}, vector3{1, 0, 0}},
                                    aabb{vector3{0, 0, 0}, vector3{1, 1, 1}},
                                    distance);
    return hit ? distance : -1.0f;
}
} // namespace
static_assert(compile_time_raycast() > 3.99f && compile_time_raycast() < 4.01f);
static_assert(compile_time_box_raycast() > 3.99f && compile_time_box_raycast() < 4.01f);
constexpr auto compile_time_optional_hit =
    math::raycast(ray{vector3{0, 0, -5}, vector3{0, 0, 1}},
                  sphere{vector3{}, 1.0f});
static_assert(compile_time_optional_hit.has_value());
static_assert(*compile_time_optional_hit > 3.99f && *compile_time_optional_hit < 4.01f);

// ------------------------------------------------------ DirectXCollision parity
#if MATHEMATICS_TEST_HAS_DXCOLLISION
namespace {

DirectX::XMVECTOR to_xm(const vector3& v) {
    return DirectX::XMVectorSet(v.x, v.y, v.z, 1.0f);
}

DirectX::BoundingBox to_xm(const aabb& box) {
    return DirectX::BoundingBox(
        DirectX::XMFLOAT3(box.center.x, box.center.y, box.center.z),
        DirectX::XMFLOAT3(box.extents.x, box.extents.y, box.extents.z));
}

DirectX::BoundingSphere to_xm(const sphere& s) {
    return DirectX::BoundingSphere(
        DirectX::XMFLOAT3(s.center.x, s.center.y, s.center.z), s.radius);
}

containment from_xm(DirectX::ContainmentType c) {
    return c == DirectX::DISJOINT    ? containment::disjoint
           : c == DirectX::CONTAINS  ? containment::contains
                                     : containment::intersects;
}

} // namespace

// The layout check first: if bounding_box did not mean centre-and-extents the
// same way aabb does, every comparison below would still "pass" while
// describing different boxes.
TEST(geometry_dx_parity, box_layout_matches_bounding_box) {
    const aabb box{vector3{1, 2, 3}, vector3{4, 5, 6}};
    const DirectX::BoundingBox theirs = to_xm(box);
    DirectX::XMFLOAT3 corners[8];
    theirs.GetCorners(corners);

    // Their corner set and ours must be the same eight points.
    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (int j = 0; j < 8 && !found; ++j) {
            found = math::near_equal(
                box.corner(j),
                vector3{corners[i].x, corners[i].y, corners[i].z}, 1e-4f);
        }
        EXPECT_TRUE(found) << "corner " << i << " has no match";
    }
}

TEST(geometry_dx_parity, containment_matches_direct_x_collision) {
    random_vectors gen(random_seed + 401);
    for (int n = 0; n < 128; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        const aabb box{vector3{a.f[0] * 0.1f, a.f[1] * 0.1f, a.f[2] * 0.1f},
                       vector3{std::abs(a.f[3]) * 0.05f + 0.5f,
                               std::abs(b.f[0]) * 0.05f + 0.5f,
                               std::abs(b.f[1]) * 0.05f + 0.5f}};
        const sphere input_sphere{
            vector3{b.f[2] * 0.1f, b.f[3] * 0.1f, a.f[0] * 0.1f},
            std::abs(b.f[1]) * 0.05f + 0.3f};

        EXPECT_EQ(math::contains(box, input_sphere),
                  from_xm(to_xm(box).Contains(to_xm(input_sphere)))) << n;
        EXPECT_EQ(math::contains(input_sphere, box),
                  from_xm(to_xm(input_sphere).Contains(to_xm(box)))) << n;
        EXPECT_EQ(math::intersects(box, input_sphere),
                  to_xm(box).Intersects(to_xm(input_sphere))) << n;
    }
}

TEST(geometry_dx_parity, plane_classification_matches_direct_x_collision) {
    random_vectors gen(random_seed + 402);
    for (int n = 0; n < 128; ++n) {
        const sample a = gen.next();
        const sphere input_sphere{vector3{a.f[0] * 0.05f, a.f[1] * 0.05f,
                                          a.f[2] * 0.05f},
                                  std::abs(a.f[3]) * 0.02f + 0.5f};
        const sample b = gen.next();
        const vector3 normal{b.f[0], b.f[1], b.f[2]};
        if (math::length_sq(normal) < 1e-3f) continue;

        const plane p = math::plane_from_point_normal(
            vector3{b.f[3] * 0.05f, 0, 0}, normal);
        const DirectX::XMVECTOR their_plane =
            DirectX::XMVectorSet(p.a, p.b, p.c, p.d);

        const DirectX::PlaneIntersectionType theirs =
            to_xm(input_sphere).Intersects(their_plane);
        const plane_side mine = math::classify(input_sphere, p);

        const plane_side converted =
            theirs == DirectX::FRONT   ? plane_side::front
            : theirs == DirectX::BACK  ? plane_side::back
                                       : plane_side::straddling;
        EXPECT_EQ(mine, converted) << n;
    }
}

TEST(geometry_dx_parity, sphere_raycast_matches_direct_x_collision) {
    random_vectors gen(random_seed + 403);
    int compared = 0;
    for (int n = 0; n < 128; ++n) {
        const sample a = gen.next();
        const sphere input_sphere{vector3{0, 0, 0}, std::abs(a.f[0]) * 0.02f + 1.0f};
        const vector3 origin{a.f[1] * 0.1f, a.f[2] * 0.1f, a.f[3] * 0.1f};
        // Aimed at a point near the sphere rather than in a uniformly random
        // direction: from ten units out, a random direction misses a unit
        // sphere almost every time, and a sweep that never hits compares
        // nothing. The jitter is wide enough that plenty still miss.
        const sample b = gen.next();
        const vector3 target{b.f[0] * 0.015f, b.f[1] * 0.015f, b.f[2] * 0.015f};
        const vector3 raw = target - origin;
        if (math::length_sq(raw) < 1e-3f) continue;
        const vector3 direction = math::normalize(raw);

        // Origins inside the sphere are skipped on purpose: there the two
        // libraries answer different questions (see the note on
        // StartingInsideHitsAtZeroForEveryPrimitive), and comparing them would
        // be asserting that a documented divergence does not exist.
        if (math::intersects(input_sphere, origin)) continue;

        float mine_distance = -1.0f;
        const bool mine_hit =
            math::raycast(ray{origin, direction}, input_sphere, mine_distance);

        float their_distance = -1.0f;
        const bool their_hit = to_xm(input_sphere).Intersects(to_xm(origin),
                                                      DirectX::XMVectorSet(
                                                          direction.x,
                                                          direction.y,
                                                          direction.z, 0.0f),
                                                      their_distance);
        ASSERT_EQ(mine_hit, their_hit) << n;
        if (mine_hit) {
            EXPECT_NEAR(mine_distance, their_distance, 1e-3f) << n;
            ++compared;
        }
    }
    EXPECT_GT(compared, 20) << "the skip must not have emptied the sweep";
}
#endif // MATHEMATICS_TEST_HAS_DXCOLLISION
