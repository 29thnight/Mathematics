#include <mathematics/geometry.hpp>
#include <mathematics/transform.hpp>

#include "support/runtime_value.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#if __has_include(<DirectXCollision.h>)
#  include <DirectXCollision.h>
#  define MATHEMATICS_TEST_HAS_DX_FRUSTUM 1
#else
#  define MATHEMATICS_TEST_HAS_DX_FRUSTUM 0
#endif

using math::aabb;
using math::bounding_frustum;
using math::containment;
using math::matrix4x4;
using math::plane;
using math::plane_side;
using math::quaternion;
using math::ray;
using math::sphere;
using math::vector3;

namespace {

constexpr bounding_frustum compile_time_frustum =
    math::bounding_frustum_from_projection_lh(
        math::perspective_fov_lh(math::half_pi, 1.0f, 1.0f, 10.0f));
static_assert(compile_time_frustum.near_plane > 0.999f &&
              compile_time_frustum.near_plane < 1.001f);
static_assert(compile_time_frustum.far_plane > 9.99f &&
              compile_time_frustum.far_plane < 10.01f);
static_assert(math::contains(compile_time_frustum, vector3{0.0f, 0.0f, 5.0f}) ==
              containment::contains);

TEST(frustum_storage, default_matches_direct_x_shape_and_corner_order) {
    const bounding_frustum frustum;
    EXPECT_EQ(frustum.origin, vector3::zero());
    EXPECT_EQ(frustum.orientation, quaternion::identity());
    EXPECT_EQ(frustum.corner(0), vector3::zero());
    EXPECT_EQ(frustum.corner(3), vector3::zero());
    EXPECT_EQ(frustum.corner(4), vector3(-1.0f, 1.0f, 1.0f));
    EXPECT_EQ(frustum.corner(5), vector3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(frustum.corner(6), vector3(1.0f, -1.0f, 1.0f));
    EXPECT_EQ(frustum.corner(7), vector3(-1.0f, -1.0f, 1.0f));
}

TEST(frustum_projection, extracts_lh_and_rh_perspective_geometry) {
    const bounding_frustum lh = math::bounding_frustum_from_projection_lh(
        math::perspective_fov_lh(math::half_pi, 2.0f, 1.0f, 10.0f));
    EXPECT_NEAR(lh.right_slope, 2.0f, 1e-5f);
    EXPECT_NEAR(lh.left_slope, -2.0f, 1e-5f);
    EXPECT_NEAR(lh.top_slope, 1.0f, 1e-5f);
    EXPECT_NEAR(lh.bottom_slope, -1.0f, 1e-5f);
    EXPECT_NEAR(lh.near_plane, 1.0f, 1e-5f);
    EXPECT_NEAR(lh.far_plane, 10.0f, 1e-4f);

    const bounding_frustum rh = math::bounding_frustum_from_projection_rh(
        math::perspective_fov_rh(math::half_pi, 2.0f, 1.0f, 10.0f));
    EXPECT_NEAR(rh.right_slope, -2.0f, 1e-5f);
    EXPECT_NEAR(rh.left_slope, 2.0f, 1e-5f);
    EXPECT_NEAR(rh.near_plane, -10.0f, 1e-4f);
    EXPECT_NEAR(rh.far_plane, -1.0f, 1e-5f);
    EXPECT_EQ(math::contains(rh, vector3{0.0f, 0.0f, -5.0f}),
              containment::contains);
}

TEST(frustum_projection, singular_projection_has_explicit_try_failure) {
    const matrix4x4 singular{};
    EXPECT_FALSE(math::try_bounding_frustum_from_projection_lh(singular));
    EXPECT_EQ(math::bounding_frustum_from_projection_lh(singular),
              bounding_frustum{});
}

TEST(frustum_planes, are_outward_unit_planes_in_world_space) {
    const bounding_frustum local = math::bounding_frustum_from_projection_lh(
        math::perspective_fov_lh(math::half_pi, 1.0f, 1.0f, 10.0f));
    const quaternion rotation =
        math::quaternion_from_axis_angle(vector3::unit_y(), 0.4f);
    const bounding_frustum world =
        math::transform(local, 1.0f, rotation, vector3{3.0f, 2.0f, -4.0f});
    const auto planes = math::frustum_planes(world);
    for (const plane& boundary : planes) {
        EXPECT_NEAR(math::length(boundary.normal()), 1.0f, 1e-5f);
        EXPECT_LE(math::signed_distance(boundary,
                                        math::rotate(vector3{0, 0, 5}, rotation) +
                                            world.origin),
                  1e-5f);
    }
    for (const vector3& corner : world.corners()) {
        bool lies_on_boundary = false;
        for (const plane& boundary : planes) {
            const float distance = math::signed_distance(boundary, corner);
            EXPECT_LE(distance, 2e-4f);
            if (distance > -2e-4f) lies_on_boundary = true;
        }
        EXPECT_TRUE(lies_on_boundary);
    }
}

TEST(frustum_transform, explicit_trs_and_matrix_paths_agree) {
    const bounding_frustum source = math::bounding_frustum_from_projection_lh(
        math::perspective_fov_lh(1.1f, 1.4f, 0.5f, 12.0f));
    const quaternion rotation =
        math::quaternion_from_pitch_yaw_roll(0.2f, -0.4f, 0.1f);
    const vector3 translation{4.0f, -2.0f, 7.0f};
    const bounding_frustum explicit_result =
        math::transform(source, 2.0f, rotation, translation);
    const bounding_frustum matrix_result = math::transform(
        source, math::compose(vector3{2.0f, 2.0f, 2.0f},
                              rotation, translation));
    EXPECT_TRUE(math::near_equal(explicit_result, matrix_result, 2e-4f));

    for (std::size_t i = 0; i < bounding_frustum::corner_count; ++i) {
        const vector3 expected =
            math::rotate(source.corner(i) * 2.0f, rotation) + translation;
        EXPECT_TRUE(math::near_equal(explicit_result.corner(i), expected, 2e-4f));
    }
}

TEST(frustum_queries, point_sphere_and_exact_corner_distance) {
    const bounding_frustum frustum = math::bounding_frustum_from_projection_lh(
        math::perspective_fov_lh(math::half_pi, 1.0f, 1.0f, 10.0f));
    EXPECT_EQ(math::contains(frustum, vector3{0.0f, 0.0f, 5.0f}),
              containment::contains);
    EXPECT_EQ(math::contains(frustum, vector3{6.0f, 0.0f, 5.0f}),
              containment::disjoint);
    EXPECT_EQ(math::contains(frustum, sphere{{0.0f, 0.0f, 5.0f}, 1.0f}),
              containment::contains);
    EXPECT_EQ(math::contains(frustum, sphere{{5.5f, 0.0f, 5.0f}, 1.0f}),
              containment::intersects);

    // It is within one radius of both adjacent infinite planes, but farther
    // than one radius from their shared edge. A plane-only test says hit; the
    // exact face/edge/corner query must reject it.
    EXPECT_FALSE(math::intersects(
        frustum, sphere{{6.3f, 6.3f, 5.0f}, 1.0f}));
}

TEST(frustum_queries, aabb_and_frustum_use_exact_sat) {
    const bounding_frustum outer = math::bounding_frustum_from_projection_lh(
        math::perspective_fov_lh(math::half_pi, 1.0f, 1.0f, 10.0f));
    EXPECT_EQ(math::contains(outer, aabb{{0.0f, 0.0f, 5.0f},
                                         {0.5f, 0.5f, 0.5f}}),
              containment::contains);
    EXPECT_TRUE(math::intersects(outer, aabb{{5.0f, 0.0f, 5.0f},
                                              {0.5f, 0.5f, 0.5f}}));
    EXPECT_FALSE(math::intersects(outer, aabb{{20.0f, 0.0f, 5.0f},
                                               {1.0f, 1.0f, 1.0f}}));

    const bounding_frustum inner{
        vector3{0.0f, 0.0f, 2.0f}, quaternion::identity(),
        0.5f, -0.5f, 0.5f, -0.5f, 1.0f, 3.0f};
    EXPECT_EQ(math::contains(outer, inner), containment::contains);
    EXPECT_TRUE(math::intersects(outer, inner));
    EXPECT_FALSE(math::intersects(
        outer, math::transform(inner, 1.0f, quaternion::identity(),
                               vector3{100.0f, 0.0f, 0.0f})));
}

TEST(frustum_queries, raycast_and_plane_classification) {
    const bounding_frustum frustum = math::bounding_frustum_from_projection_lh(
        math::perspective_fov_lh(math::half_pi, 1.0f, 1.0f, 10.0f));
    float distance = -1.0f;
    ASSERT_TRUE(math::raycast(ray{{0, 0, 0}, {0, 0, 1}}, frustum, distance));
    EXPECT_NEAR(distance, 1.0f, 1e-5f);
    ASSERT_TRUE(math::raycast(ray{{0, 0, 5}, {1, 0, 0}}, frustum, distance));
    EXPECT_FLOAT_EQ(distance, 0.0f);
    EXPECT_FALSE(math::raycast(ray{{20, 0, 5}, {1, 0, 0}}, frustum, distance));

    EXPECT_EQ(math::classify(frustum, plane{0, 0, 1, -20}),
              plane_side::back);
    EXPECT_EQ(math::classify(frustum, plane{0, 0, 1, -5}),
              plane_side::straddling);
}

TEST(frustum_bounds, conservative_bounds_contain_every_corner) {
    const bounding_frustum frustum = math::transform(
        compile_time_frustum, 1.0f,
        math::quaternion_from_pitch_yaw_roll(0.2f, 0.4f, -0.1f),
        vector3{2.0f, 3.0f, 4.0f});
    const aabb box = math::bounding_box(frustum);
    const sphere enclosing_sphere = math::bounding_sphere(frustum);
    for (const vector3& corner : frustum.corners()) {
        EXPECT_TRUE(math::intersects(box, corner));
        EXPECT_TRUE(math::intersects(enclosing_sphere, corner));
    }
}

#if MATHEMATICS_TEST_HAS_DX_FRUSTUM
DirectX::XMMATRIX to_xm(const matrix4x4& value) {
    return DirectX::XMMATRIX(
        DirectX::XMVectorSet(value.m[0][0], value.m[0][1], value.m[0][2], value.m[0][3]),
        DirectX::XMVectorSet(value.m[1][0], value.m[1][1], value.m[1][2], value.m[1][3]),
        DirectX::XMVectorSet(value.m[2][0], value.m[2][1], value.m[2][2], value.m[2][3]),
        DirectX::XMVectorSet(value.m[3][0], value.m[3][1], value.m[3][2], value.m[3][3]));
}

DirectX::XMVECTOR to_xm(const vector3& value, float w = 0.0f) {
    return DirectX::XMVectorSet(value.x, value.y, value.z, w);
}

DirectX::XMVECTOR to_xm(const quaternion& value) {
    return DirectX::XMVectorSet(value.x, value.y, value.z, value.w);
}

DirectX::BoundingFrustum to_xm(const bounding_frustum& value) {
    return DirectX::BoundingFrustum{
        DirectX::XMFLOAT3(value.origin.x, value.origin.y, value.origin.z),
        DirectX::XMFLOAT4(value.orientation.x, value.orientation.y,
                          value.orientation.z, value.orientation.w),
        value.right_slope, value.left_slope,
        value.top_slope, value.bottom_slope,
        value.near_plane, value.far_plane};
}

containment from_xm(DirectX::ContainmentType value) {
    if (value == DirectX::CONTAINS) return containment::contains;
    if (value == DirectX::INTERSECTS) return containment::intersects;
    return containment::disjoint;
}

TEST(frustum_dx_parity, projection_fields_corners_and_planes_match) {
    for (const bool right_handed : {false, true}) {
        const matrix4x4 projection = right_handed
            ? math::perspective_fov_rh(1.1f, 1.7f, 0.3f, 250.0f)
            : math::perspective_fov_lh(1.1f, 1.7f, 0.3f, 250.0f);
        const bounding_frustum mine = right_handed
            ? math::bounding_frustum_from_projection_rh(projection)
            : math::bounding_frustum_from_projection_lh(projection);
        DirectX::BoundingFrustum theirs;
        DirectX::BoundingFrustum::CreateFromMatrix(
            theirs, to_xm(projection), right_handed);

        EXPECT_NEAR(mine.right_slope, theirs.RightSlope, 1e-5f);
        EXPECT_NEAR(mine.left_slope, theirs.LeftSlope, 1e-5f);
        EXPECT_NEAR(mine.top_slope, theirs.TopSlope, 1e-5f);
        EXPECT_NEAR(mine.bottom_slope, theirs.BottomSlope, 1e-5f);
        // Mathematics solves the projection coefficients directly while
        // DirectXCollision inverse-projects probe points. At large far/near
        // ratios the two float paths differ by a few parts in 100,000 because
        // both reconstruct depth through cancellation.
        const float near_tolerance =
            1e-4f + std::abs(theirs.Near) * 2e-5f;
        const float far_tolerance =
            1e-4f + std::abs(theirs.Far) * 2e-5f;
        EXPECT_NEAR(mine.near_plane, theirs.Near, near_tolerance);
        EXPECT_NEAR(mine.far_plane, theirs.Far, far_tolerance);

        DirectX::XMFLOAT3 their_corners[8];
        theirs.GetCorners(their_corners);
        for (std::size_t i = 0; i < 8; ++i) {
            EXPECT_TRUE(math::near_equal(
                mine.corner(i),
                vector3{their_corners[i].x, their_corners[i].y,
                        their_corners[i].z},
                3e-3f)) << i << " rh=" << right_handed;
        }
    }
}

TEST(frustum_dx_parity, transformed_corners_and_volume_queries_match) {
    const bounding_frustum base = math::bounding_frustum_from_projection_lh(
        math::perspective_fov_lh(1.2f, 1.4f, 0.5f, 30.0f));
    const quaternion rotation =
        math::quaternion_from_pitch_yaw_roll(0.15f, -0.35f, 0.08f);
    const vector3 translation{3.0f, -1.0f, 5.0f};
    const bounding_frustum mine =
        math::transform(base, 1.5f, rotation, translation);
    DirectX::BoundingFrustum theirs;
    to_xm(base).Transform(theirs, 1.5f, to_xm(rotation), to_xm(translation));

    DirectX::XMFLOAT3 their_corners[8];
    theirs.GetCorners(their_corners);
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_TRUE(math::near_equal(
            mine.corner(i),
            vector3{their_corners[i].x, their_corners[i].y,
                    their_corners[i].z}, 3e-4f)) << i;
    }

    for (int z = -2; z <= 30; z += 2) {
        for (int x = -12; x <= 16; x += 2) {
            const vector3 point{static_cast<float>(x), 0.5f,
                                static_cast<float>(z)};
            EXPECT_EQ(math::contains(mine, point),
                      from_xm(theirs.Contains(to_xm(point))))
                << x << ',' << z;

            const sphere input_sphere{point, 0.75f + 0.05f * (x & 3)};
            const DirectX::BoundingSphere their_sphere{
                DirectX::XMFLOAT3(point.x, point.y, point.z),
                input_sphere.radius};
            EXPECT_EQ(math::intersects(mine, input_sphere),
                      theirs.Intersects(their_sphere)) << x << ',' << z;
            EXPECT_EQ(math::contains(mine, input_sphere),
                      from_xm(theirs.Contains(their_sphere))) << x << ',' << z;

            const aabb box{point, vector3{0.6f, 1.0f, 0.8f}};
            const DirectX::BoundingBox their_box{
                DirectX::XMFLOAT3(point.x, point.y, point.z),
                DirectX::XMFLOAT3(0.6f, 1.0f, 0.8f)};
            EXPECT_EQ(math::intersects(mine, box), theirs.Intersects(their_box))
                << x << ',' << z;
            EXPECT_EQ(math::contains(mine, box),
                      from_xm(theirs.Contains(their_box))) << x << ',' << z;
        }
    }
}

TEST(frustum_dx_parity, frustum_intersection_and_raycast_match) {
    const bounding_frustum base = math::bounding_frustum_from_projection_lh(
        math::perspective_fov_lh(1.0f, 1.3f, 0.5f, 20.0f));
    const DirectX::BoundingFrustum their_base = to_xm(base);

    for (int i = -5; i <= 5; ++i) {
        const quaternion rotation = math::quaternion_from_axis_angle(
            vector3::unit_y(), static_cast<float>(i) * 0.11f);
        const vector3 translation{static_cast<float>(i) * 2.0f,
                                  0.25f * static_cast<float>(i),
                                  4.0f + static_cast<float>(i)};
        const bounding_frustum other =
            math::transform(base, 0.6f, rotation, translation);
        DirectX::BoundingFrustum their_other;
        their_base.Transform(their_other, 0.6f, to_xm(rotation),
                             to_xm(translation));
        EXPECT_EQ(math::intersects(base, other),
                  their_base.Intersects(their_other)) << i;
        EXPECT_EQ(math::contains(base, other),
                  from_xm(their_base.Contains(their_other))) << i;
    }

    const vector3 origin{10.0f, 0.0f, 5.0f};
    const vector3 direction = math::normalize(vector3{-1.0f, 0.0f, 0.1f});
    float mine_distance = -1.0f;
    float their_distance = -1.0f;
    const bool mine_hit = math::raycast(ray{origin, direction}, base,
                                         mine_distance);
    const bool their_hit = their_base.Intersects(
        to_xm(origin), to_xm(direction), their_distance);
    ASSERT_EQ(mine_hit, their_hit);
    if (mine_hit) EXPECT_NEAR(mine_distance, their_distance, 1e-4f);
}
#endif

} // namespace

// ------------------------------------------------ run-time paths and reverses
// Inputs go through runtime_value: every query here is constexpr, and with
// literal arguments GCC answers it at compile time without running the code.
namespace {

using math_test::runtime_value;

// The default frustum: apex at the origin (near distance zero), looking +Z,
// slopes of one, far plane at one.
bounding_frustum apex_frustum() { return runtime_value(bounding_frustum{}); }

TEST(frustum_projection, rh_extraction_runs_at_run_time_too) {
    const matrix4x4 projection =
        runtime_value(math::perspective_fov_rh(math::half_pi, 2.0f, 1.0f, 10.0f));
    const auto extracted = math::try_bounding_frustum_from_projection_rh(projection);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_NEAR(extracted->near_plane, -10.0f, 1e-4f);
    EXPECT_NEAR(extracted->far_plane, -1.0f, 1e-5f);
    EXPECT_EQ(math::bounding_frustum_from_projection_rh(projection), *extracted);
}

TEST(frustum_projection, non_finite_and_overflowing_projections_are_rejected) {
    matrix4x4 infinite = runtime_value(
        math::perspective_fov_lh(math::half_pi, 1.0f, 1.0f, 10.0f));
    infinite.m[0][0] = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(math::try_bounding_frustum_from_projection_lh(infinite));

    // Every coefficient finite, but a slope (m23 - m20) / m00 that is not:
    // the inputs pass the first screen and the derived frustum fails the second.
    matrix4x4 overflowing = runtime_value(
        math::perspective_fov_lh(math::half_pi, 1.0f, 1.0f, 10.0f));
    overflowing.m[0][0] = 1e-30f;
    overflowing.m[2][3] = 1e30f;
    EXPECT_FALSE(math::try_bounding_frustum_from_projection_lh(overflowing));
    EXPECT_EQ(math::bounding_frustum_from_projection_lh(overflowing),
              bounding_frustum{});
}

TEST(frustum_transform, zero_scale_matrix_returns_the_default_frustum) {
    matrix4x4 collapsed = runtime_value(matrix4x4::identity());
    collapsed.m[1][1] = 0.0f;
    EXPECT_EQ(math::transform(apex_frustum(), collapsed), bounding_frustum{});
}

TEST(frustum_queries, reversed_overloads_agree_with_the_frustum_first_forms) {
    const bounding_frustum frustum = apex_frustum();
    const sphere near_sphere{vector3{0.0f, 0.0f, 0.5f}, 0.1f};
    const sphere far_sphere{vector3{0.0f, 0.0f, 5.0f}, 0.1f};
    const aabb near_box{vector3{0.0f, 0.0f, 0.5f}, vector3{0.1f, 0.1f, 0.1f}};
    const aabb far_box{vector3{0.0f, 0.0f, 5.0f}, vector3{0.1f, 0.1f, 0.1f}};

    EXPECT_TRUE(math::intersects(near_sphere, frustum));
    EXPECT_FALSE(math::intersects(far_sphere, frustum));
    EXPECT_TRUE(math::intersects(near_box, frustum));
    EXPECT_FALSE(math::intersects(far_box, frustum));
}

// The volumes contain the frustum only when all eight corners are inside.
TEST(frustum_queries, sphere_and_box_containing_a_frustum) {
    const bounding_frustum frustum = apex_frustum();

    EXPECT_EQ(math::contains(sphere{vector3{0.0f, 0.0f, 0.5f}, 2.0f}, frustum),
              containment::contains);
    EXPECT_EQ(math::contains(sphere{vector3{0.0f, 0.0f, 0.5f}, 0.2f}, frustum),
              containment::intersects);
    EXPECT_EQ(math::contains(sphere{vector3{0.0f, 0.0f, 9.0f}, 0.2f}, frustum),
              containment::disjoint);

    EXPECT_EQ(math::contains(aabb{vector3{0.0f, 0.0f, 0.5f},
                                  vector3{1.5f, 1.5f, 1.5f}}, frustum),
              containment::contains);
    EXPECT_EQ(math::contains(aabb{vector3{0.0f, 0.0f, 0.5f},
                                  vector3{0.2f, 0.2f, 0.2f}}, frustum),
              containment::intersects);
    EXPECT_EQ(math::contains(aabb{vector3{0.0f, 0.0f, 9.0f},
                                  vector3{0.2f, 0.2f, 0.2f}}, frustum),
              containment::disjoint);
}

// With a near distance of zero the four near corners are one point, and every
// face touching the apex is a degenerate triangle. The distance to those falls
// back to the closest of the triangle's edges.
TEST(frustum_queries, sphere_at_the_apex_measures_to_degenerate_faces) {
    const bounding_frustum frustum = apex_frustum();
    EXPECT_TRUE(math::intersects(frustum, sphere{vector3{0.0f, 0.0f, -0.5f}, 0.6f}));
    EXPECT_FALSE(math::intersects(frustum, sphere{vector3{0.0f, 0.0f, -0.5f}, 0.4f}));
}

TEST(frustum_queries, box_and_frustum_separated_behind_the_apex_and_aside) {
    EXPECT_FALSE(math::intersects(
        apex_frustum(), aabb{vector3{0.0f, 0.0f, -2.0f}, vector3{0.5f, 0.5f, 0.5f}}));
    EXPECT_FALSE(math::intersects(
        apex_frustum(), aabb{vector3{3.0f, 0.0f, 0.5f}, vector3{0.5f, 0.5f, 0.5f}}));
}

TEST(frustum_queries, frustum_pairs_separate_in_either_argument_order) {
    const bounding_frustum x = apex_frustum();
    bounding_frustum beyond = apex_frustum();
    beyond.origin = vector3{0.0f, 0.0f, 5.0f};
    EXPECT_FALSE(math::intersects(x, beyond));
    EXPECT_FALSE(math::intersects(beyond, x));

    bounding_frustum overlapping = apex_frustum();
    overlapping.origin = vector3{0.0f, 0.0f, 0.5f};
    EXPECT_TRUE(math::intersects(x, overlapping));
}

TEST(frustum_queries, raycast_optional_and_the_parallel_miss) {
    const bounding_frustum frustum = apex_frustum();
    const auto hit = math::raycast(
        runtime_value(ray{vector3{0.0f, 0.0f, -1.0f}, vector3{0.0f, 0.0f, 1.0f}}),
        frustum);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(*hit, 1.0f, 1e-5f);

    // Parallel to the near plane and on its outside: no entry is possible.
    EXPECT_FALSE(math::raycast(
        runtime_value(ray{vector3{0.0f, 0.0f, -1.0f}, vector3{1.0f, 0.0f, 0.0f}}),
        frustum));
}

TEST(volume_queries, contains_point_is_contains_or_disjoint) {
    const sphere ball = runtime_value(sphere{vector3{1.0f, 0.0f, 0.0f}, 1.0f});
    EXPECT_EQ(math::contains(ball, vector3{1.5f, 0.0f, 0.0f}), containment::contains);
    EXPECT_EQ(math::contains(ball, vector3{3.0f, 0.0f, 0.0f}), containment::disjoint);

    const aabb box = runtime_value(
        aabb{vector3{0.0f, 0.0f, 0.0f}, vector3{1.0f, 2.0f, 3.0f}});
    EXPECT_EQ(math::contains(box, vector3{0.5f, -1.5f, 2.5f}), containment::contains);
    EXPECT_EQ(math::contains(box, vector3{0.5f, -2.5f, 2.5f}), containment::disjoint);
}

// Each separating-axis stage gets a pair that only it can separate; the
// stages before it all overlap. The inputs were found by searching a grid
// with the library's own per-stage tests, and are kept as plain data here.
TEST(frustum_queries, each_sat_stage_separates_something_on_its_own) {
    // Box axis only: the frustum's face normals all overlap this box.
    EXPECT_FALSE(math::intersects(
        apex_frustum(),
        aabb{vector3{-1.5f, -0.75f, 1.0f}, vector3{0.25f, 0.5f, 0.5f}}));

    // Edge cross product only. An axis-aligned frustum's edge-by-box-axis
    // products coincide with its own face normals, so this needs a turn.
    bounding_frustum turned = apex_frustum();
    turned.orientation =
        math::quaternion_from_axis_angle(vector3{1.0f, 1.0f, 0.0f}, 0.75f);
    EXPECT_FALSE(math::intersects(
        turned, aabb{vector3{-0.5f, 0.75f, 1.0f}, vector3{0.25f, 0.5f, 0.75f}}));

    // The second frustum's planes only.
    bounding_frustum second = apex_frustum();
    second.origin = vector3{1.0f, -1.5f, 1.5f};
    second.orientation =
        math::quaternion_from_axis_angle(vector3{0.0f, 1.0f, 0.0f}, 2.25f);
    EXPECT_FALSE(math::intersects(apex_frustum(), second));

    // Edge-by-edge only.
    bounding_frustum crossing = apex_frustum();
    crossing.origin = vector3{-1.0f, 0.0f, 0.25f};
    crossing.orientation =
        math::quaternion_from_axis_angle(vector3{1.0f, 0.0f, 0.0f}, 2.25f);
    EXPECT_FALSE(math::intersects(apex_frustum(), crossing));
}

// A frustum with equal left and right slopes is flat: its side faces are
// segments, not triangles. The exact sphere distance then falls back to the
// nearest point on the triangles' edges.
TEST(frustum_queries, sphere_beside_a_flat_frustum_measures_to_edges) {
    const bounding_frustum flat = runtime_value(bounding_frustum{
        vector3::zero(), quaternion::identity(),
        0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 2.0f});
    EXPECT_TRUE(math::intersects(flat, sphere{vector3{0.1f, 0.0f, 1.5f}, 0.2f}));
    // Beyond the far edge: 0.18 away in one case, 0.21 in the other.
    EXPECT_TRUE(math::intersects(flat, sphere{vector3{0.1f, 0.0f, 2.15f}, 0.2f}));
    EXPECT_FALSE(math::intersects(flat, sphere{vector3{0.15f, 0.0f, 2.15f}, 0.2f}));
}

TEST(frustum_queries, zero_direction_ray_never_hits) {
    EXPECT_FALSE(math::raycast(
        runtime_value(ray{vector3{0.0f, 0.0f, 0.5f}, vector3{0.0f, 0.0f, 0.0f}}),
        apex_frustum()));
}

TEST(frustum_projection, singular_rh_projection_fails_at_run_time) {
    EXPECT_FALSE(math::try_bounding_frustum_from_projection_rh(
        runtime_value(matrix4x4{})));
}

} // namespace
