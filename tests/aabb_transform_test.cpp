#include <mathematics/geometry.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>

#if __has_include(<DirectXCollision.h>)
#  include <DirectXCollision.h>
#  define MATHEMATICS_TEST_HAS_DX_BOUNDING_BOX 1
#else
#  define MATHEMATICS_TEST_HAS_DX_BOUNDING_BOX 0
#endif

namespace {

using math::aabb;
using math::matrix4x4;
using math::quaternion;
using math::vector3;

constexpr aabb compile_time_box{
    vector3{1.0f, 2.0f, -2.0f}, vector3{0.5f, 1.0f, 2.0f}};
constexpr matrix4x4 compile_time_affine{
    2.0f, 0.0f, 0.0f, 0.0f,
    0.0f, -3.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    4.0f, -5.0f, 6.0f, 1.0f};
constexpr aabb compile_time_result =
    math::transform(compile_time_box, compile_time_affine);
static_assert(compile_time_result.center == vector3{6.0f, -11.0f, 5.0f});
static_assert(compile_time_result.extents == vector3{1.0f, 3.0f, 1.0f});
static_assert(math::transform(aabb{}, compile_time_affine).is_empty());

aabb corner_reference(const aabb& box, const matrix4x4& matrix) {
    if (box.is_empty()) return box;
    std::array<vector3, 8> corners{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        corners[i] = math::transform_point(box.corner(static_cast<int>(i)), matrix);
    }
    return math::aabb_from_points(corners);
}

TEST(aabb_transform, affine_matrix_matches_all_eight_transformed_corners) {
    const aabb box{vector3{2.0f, -1.0f, 3.0f},
                   vector3{1.5f, 0.75f, 2.25f}};
    matrix4x4 affine = math::compose(
        vector3{-2.0f, 0.5f, 1.25f},
        math::quaternion_from_axis_angle(
            math::normalize(vector3{1.0f, 2.0f, -0.5f}), 0.73f),
        vector3{8.0f, -4.0f, 2.0f});
    affine.m[0][1] += 0.35f;
    affine.m[2][0] -= 0.2f;

    EXPECT_TRUE(math::near_equal(math::transform(box, affine),
                                 corner_reference(box, affine), 2e-5f));
}

TEST(aabb_transform, rotation_expands_the_axis_aligned_result) {
    const aabb unit{vector3::zero(), vector3{1.0f, 1.0f, 1.0f}};
    const matrix4x4 rotation = math::compose(
        vector3{1.0f, 1.0f, 1.0f},
        math::quaternion_from_axis_angle(vector3{0.0f, 0.0f, 1.0f},
                                         math::quarter_pi),
        vector3::zero());
    const aabb result = math::transform(unit, rotation);

    EXPECT_NEAR(result.extents.x, std::sqrt(2.0f), 1e-5f);
    EXPECT_NEAR(result.extents.y, std::sqrt(2.0f), 1e-5f);
    EXPECT_NEAR(result.extents.z, 1.0f, 1e-6f);
}

TEST(aabb_transform, explicit_trs_matches_the_composed_affine_matrix) {
    const aabb box{vector3{-2.0f, 4.0f, 1.0f},
                   vector3{0.5f, 2.0f, 1.5f}};
    const float scale = -1.75f;
    const quaternion rotation = math::quaternion_from_axis_angle(
        math::normalize(vector3{0.3f, 1.0f, -0.2f}), 1.1f);
    const vector3 translation{7.0f, -3.0f, 5.0f};

    const aabb direct = math::transform(box, scale, rotation, translation);
    const aabb composed = math::transform(
        box, math::compose(vector3{scale, scale, scale}, rotation, translation));
    EXPECT_TRUE(math::near_equal(direct, composed, 2e-5f));
}

TEST(aabb_transform, empty_flat_and_point_boxes_keep_their_contract) {
    const matrix4x4 affine = math::compose(
        vector3{-2.0f, 3.0f, 0.5f}, quaternion::identity(),
        vector3{4.0f, 5.0f, 6.0f});
    EXPECT_TRUE(math::transform(aabb{}, affine).is_empty());

    const aabb point{vector3{1.0f, 2.0f, 3.0f}, vector3::zero()};
    const aabb point_result = math::transform(point, affine);
    EXPECT_TRUE(point_result.extents == vector3::zero());
    EXPECT_TRUE(math::near_equal(point_result.center,
                                 math::transform_point(point.center, affine)));

    const aabb flat{vector3::zero(), vector3{2.0f, 0.0f, 1.0f}};
    EXPECT_TRUE(math::near_equal(math::transform(flat, affine),
                                 corner_reference(flat, affine)));
}

#if MATHEMATICS_TEST_HAS_DX_BOUNDING_BOX

DirectX::XMMATRIX to_xm(const matrix4x4& value) {
    return DirectX::XMMATRIX(
        value.m[0][0], value.m[0][1], value.m[0][2], value.m[0][3],
        value.m[1][0], value.m[1][1], value.m[1][2], value.m[1][3],
        value.m[2][0], value.m[2][1], value.m[2][2], value.m[2][3],
        value.m[3][0], value.m[3][1], value.m[3][2], value.m[3][3]);
}

DirectX::BoundingBox to_xm(const aabb& value) {
    return DirectX::BoundingBox{
        DirectX::XMFLOAT3{value.center.x, value.center.y, value.center.z},
        DirectX::XMFLOAT3{value.extents.x, value.extents.y, value.extents.z}};
}

void expect_near(const aabb& ours, const DirectX::BoundingBox& theirs,
                 float epsilon = 1e-4f) {
    EXPECT_NEAR(ours.center.x, theirs.Center.x, epsilon);
    EXPECT_NEAR(ours.center.y, theirs.Center.y, epsilon);
    EXPECT_NEAR(ours.center.z, theirs.Center.z, epsilon);
    EXPECT_NEAR(ours.extents.x, theirs.Extents.x, epsilon);
    EXPECT_NEAR(ours.extents.y, theirs.Extents.y, epsilon);
    EXPECT_NEAR(ours.extents.z, theirs.Extents.z, epsilon);
}

float sample(std::uint32_t& state, float minimum, float maximum) {
    state = state * 1664525u + 1013904223u;
    const float unit = static_cast<float>(state >> 8) * (1.0f / 16777215.0f);
    return minimum + (maximum - minimum) * unit;
}

TEST(aabb_transform_dx_parity, affine_matrix_matches_bounding_box_transform) {
    std::uint32_t random_state = 0x5eed1234u;
    for (int i = 0; i < 256; ++i) {
        const aabb box{
            vector3{sample(random_state, -50.0f, 50.0f),
                    sample(random_state, -50.0f, 50.0f),
                    sample(random_state, -50.0f, 50.0f)},
            vector3{sample(random_state, 0.0f, 8.0f),
                    sample(random_state, 0.0f, 8.0f),
                    sample(random_state, 0.0f, 8.0f)}};
        const quaternion rotation = math::quaternion_from_axis_angle(
            math::normalize(vector3{sample(random_state, -1.0f, 1.0f),
                                    sample(random_state, -1.0f, 1.0f),
                                    sample(random_state, -1.0f, 1.0f)}),
            sample(random_state, -math::pi, math::pi));
        matrix4x4 affine = math::compose(
            vector3{sample(random_state, -3.0f, 3.0f),
                    sample(random_state, -3.0f, 3.0f),
                    sample(random_state, -3.0f, 3.0f)},
            rotation,
            vector3{sample(random_state, -20.0f, 20.0f),
                    sample(random_state, -20.0f, 20.0f),
                    sample(random_state, -20.0f, 20.0f)});
        affine.m[0][1] += sample(random_state, -0.5f, 0.5f);
        affine.m[2][0] += sample(random_state, -0.5f, 0.5f);

        DirectX::BoundingBox theirs;
        to_xm(box).Transform(theirs, to_xm(affine));
        expect_near(math::transform(box, affine), theirs);
    }
}

TEST(aabb_transform_dx_parity, explicit_trs_matches_bounding_box_transform) {
    const aabb box{vector3{2.0f, -3.0f, 4.0f},
                   vector3{1.0f, 2.0f, 0.5f}};
    const float scale = -2.25f;
    const quaternion rotation = math::quaternion_from_axis_angle(
        math::normalize(vector3{0.5f, -1.0f, 0.25f}), 0.9f);
    const vector3 translation{-8.0f, 3.0f, 11.0f};

    DirectX::BoundingBox theirs;
    const DirectX::XMVECTOR dx_rotation = DirectX::XMVectorSet(
        rotation.x, rotation.y, rotation.z, rotation.w);
    const DirectX::XMVECTOR dx_translation = DirectX::XMVectorSet(
        translation.x, translation.y, translation.z, 0.0f);
    to_xm(box).Transform(theirs, scale, dx_rotation, dx_translation);

    expect_near(math::transform(box, scale, rotation, translation), theirs,
                2e-5f);
}

#endif

} // namespace
