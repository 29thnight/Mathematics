#include <mathematics/geometry.hpp>
#include <mathematics/transform.hpp>

#include <gtest/gtest.h>

#include <cmath>

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
