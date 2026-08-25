// Every code sample in docs/GUIDE.md, compiled and checked.
//
// Documentation that does not compile is worse than none: it is confidently
// wrong, and the reader trusts it. So the guide's examples live here as real
// tests, and the guide's claims about conventions and degenerate inputs are
// asserted rather than asserted-in-prose.
//
// When the guide changes, this file changes with it.

#include "support/reg_testing.hpp"

#include <mathematics/mathematics.hpp>

#include <cmath>
#include <limits>

using namespace math;

namespace {
constexpr float epsilon = 1e-4f;
} // namespace

// -------------------------------------------------- guide section 1: conventions
// "합성 순서: 왼쪽에서 오른쪽, 적용 순서 그대로"
TEST(guide, composition_reads_left_to_right) {
    const matrix4x4 scale2 = scaling_matrix(2.0f);
    const matrix4x4 move10 = translation_matrix(vector3{10, 0, 0});

    // Scale first, then translate: 1 -> 2 -> 12.
    EXPECT_TRUE(near_equal(transform_point(vector3{1, 0, 0}, scale2 * move10),
                          vector3{12, 0, 0}, epsilon));
    // The other order scales the translation too: 1 -> 11 -> 22.
    EXPECT_TRUE(near_equal(transform_point(vector3{1, 0, 0}, move10 * scale2),
                          vector3{22, 0, 0}, epsilon));
}

// "쿼터니언 곱 순서가 반대인 이유" -- the three identities the guide prints.
TEST(guide, the_three_composition_identities) {
    const quaternion a = quaternion_from_axis_angle(vector3{0, 0, 1}, 0.7f);
    const quaternion b = quaternion_from_axis_angle(vector3{1, 0, 0}, 0.4f);
    const vector3 v{1, 2, 3};

    EXPECT_TRUE(near_equal(rotate(v, a * b), rotate(rotate(v, a), b), epsilon));
    EXPECT_TRUE(near_equal(rotation_matrix(a * b),
                          rotation_matrix(a) * rotation_matrix(b), epsilon));

    const matrix4x4 m1 = rotation_matrix(a);
    const matrix4x4 m2 = translation_matrix(vector3{5, 0, 0});
    const vector4 v4{1, 2, 3, 1};
    EXPECT_TRUE(near_equal(v4 * (m1 * m2), (v4 * m1) * m2, 1e-3f));
}

// "이동 성분: 3행" -- the row/column question, settled by a value.
TEST(guide, translation_lives_in_row_three) {
    const matrix4x4 t = translation_matrix(vector3{10, 20, 30});
    EXPECT_FLOAT_EQ(t.m[3][0], 10.0f);
    EXPECT_FLOAT_EQ(t.m[0][3], 0.0f) << "not column 3";
    EXPECT_TRUE(near_equal(t.translation(), vector3{10, 20, 30}, epsilon));
}

// ---------------------------------------------------- guide section 2: the example
// The five-minute example, run end to end. If the guide's pipeline were
// composed in the wrong order this would put the point somewhere else.
TEST(guide, the_five_minute_example_runs) {
    const quaternion spin =
        quaternion_from_axis_angle(vector3{0, 1, 0}, radians(30.0f));
    const matrix4x4 world =
        compose(vector3{2, 2, 2}, spin, vector3{10, 0, 5});

    const matrix4x4 view = look_at_lh(vector3{0, 5, -10}, vector3{0, 0, 0},
                                    vector3{0, 1, 0});
    const matrix4x4 proj =
        perspective_fov_lh(radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

    const matrix4x4 mvp = world * view * proj;
    const vector4 clip = vector4{1, 0, 0, 1} * mvp;
    const vector3 ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};

    // The object sits in front of the camera, so it must land inside the clip
    // cube with a positive w.
    EXPECT_GT(clip.w, 0.0f) << "behind the camera means the pipeline is wrong";
    EXPECT_GE(ndc.z, 0.0f);
    EXPECT_LE(ndc.z, 1.0f);

    // Composing by hand must agree with the chained product.
    EXPECT_TRUE(near_equal(vector4{1, 0, 0, 1} * world * view * proj, clip,
                          1e-3f));
}

TEST(guide, point_and_direction_differ_by_translation) {
    const matrix4x4 world = compose(
        vector3{2, 2, 2},
        quaternion_from_axis_angle(vector3{0, 1, 0}, radians(30.0f)),
        vector3{10, 0, 5});

    const vector3 local_pos{1, 0, 0};
    EXPECT_FALSE(near_equal(transform_point(local_pos, world),
                           transform_direction(local_pos, world), 1e-2f))
        << "if these agreed the translation would not be applied";

    // The guide's note on non-uniform scale: the inverse-transpose is what a
    // normal needs, and it differs from the plain matrix when scale is uneven.
    const matrix4x4 squashed =
        compose(vector3{2, 1, 1}, quaternion::identity(), vector3{0, 0, 0});
    const vector3 normal = normalize(vector3{1, 1, 0});
    const vector3 wrong = normalize(transform_direction(normal, squashed));
    const vector3 right =
        normalize(transform_direction(normal, transpose(inverse(squashed))));
    EXPECT_FALSE(near_equal(wrong, right, 1e-2f))
        << "the guide claims these differ under non-uniform scale";
}

// ------------------------------------------- guide section 2: compile time
constexpr matrix4x4 guide_projection = perspective_fov_lh(half_pi, 1.0f, 1.0f, 100.0f);
constexpr quaternion guide_turn =
    quaternion_from_axis_angle(vector3{0, 0, 1}, half_pi);
constexpr float guide_sine = math::sin(0.5f);
static_assert(inverse(matrix4x4::identity()) == matrix4x4::identity());
static_assert(guide_sine > 0.47f && guide_sine < 0.48f);
static_assert(guide_projection.m[2][3] == 1.0f);
static_assert(guide_turn.z > 0.7f);

// ------------------------------------------------------ guide section 3: types
TEST(guide, the_size_table_is_correct) {
    EXPECT_EQ(sizeof(vector2), 8u);
    EXPECT_EQ(sizeof(vector3), 12u);
    EXPECT_EQ(sizeof(vector4), 16u);
    EXPECT_EQ(sizeof(quaternion), 16u);
    EXPECT_EQ(sizeof(matrix3x3), 36u);
    EXPECT_EQ(sizeof(matrix4x4), 64u);
    EXPECT_EQ(sizeof(color), 16u);
    EXPECT_EQ(sizeof(rect), 16u);
    EXPECT_EQ(sizeof(plane), 16u);
    EXPECT_EQ(sizeof(sphere), 16u);
    EXPECT_EQ(sizeof(aabb), 24u);
    EXPECT_EQ(sizeof(ray), 24u);
    EXPECT_EQ(sizeof(bounding_frustum), 52u);
    EXPECT_EQ(sizeof(vec_reg), 16u);
}

TEST(guide, color_rect_and_frustum_examples_are_accurate) {
    const color source{0.8f, 0.4f, 0.2f, 0.5f};
    EXPECT_EQ(premultiply(source), color(0.4f, 0.2f, 0.1f, 0.5f));

    const rect ui_bounds{10.0f, 20.0f, 100.0f, 50.0f};
    EXPECT_TRUE(contains(ui_bounds, vector2{10.0f, 20.0f}));
    EXPECT_FALSE(contains(ui_bounds, vector2{110.0f, 70.0f}));

    const matrix4x4 projection =
        perspective_fov_lh(radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    const bounding_frustum frustum =
        bounding_frustum_from_projection_lh(projection);
    const vector3 camera_position{0.0f, 0.0f, 1.0f};
    const aabb world_bounds{{0.0f, 0.0f, 5.0f}, {1.0f, 1.0f, 1.0f}};
    EXPECT_EQ(contains(frustum, camera_position), containment::contains);
    EXPECT_TRUE(intersects(frustum, world_bounds));

    const auto corners = frustum.corners();
    const auto planes = frustum_planes(frustum);
    EXPECT_EQ(corners.size(), bounding_frustum::corner_count);
    EXPECT_EQ(planes.size(), bounding_frustum::plane_count);
}

// "AABB의 함정" -- the guide claims these two are DIFFERENT boxes.
TEST(guide, the_aabb_trap_is_real) {
    const aabb a{vector3{0, 0, 0}, vector3{1, 1, 1}};
    const aabb b = aabb::from_min_max(vector3{0, 0, 0}, vector3{1, 1, 1});
    EXPECT_FALSE(near_equal(a, b, 1e-3f)) << "the guide's warning must be true";

    EXPECT_TRUE(near_equal(a.min(), vector3{-1, -1, -1}, epsilon));
    EXPECT_TRUE(near_equal(b.min(), vector3{0, 0, 0}, epsilon));
}

// ------------------------------------------- guide section 4: degenerate inputs
// Every row of the guide's table, asserted.
TEST(guide, the_degenerate_input_table) {
    const float inf = std::numeric_limits<float>::infinity();

    // Extra parentheses: the preprocessor does not protect commas inside
    // BRACES, so a brace-init at the top level of a macro argument reads as
    // several arguments.
    EXPECT_TRUE((normalize(vector3{0, 0, 0}) == vector3{0, 0, 0}));
    EXPECT_TRUE(std::isnan(normalize(vector3{inf, 0, 0}).x));

    const matrix4x4 singular{1, 2, 3, 4, 2, 4, 6, 8, 9, 10, 11, 13, 14, 15, 17, 19};
    EXPECT_TRUE(inverse(singular) == matrix4x4::identity());
    matrix4x4 with_inf = matrix4x4::identity();
    with_inf.m[0][0] = inf;
    EXPECT_TRUE(inverse(with_inf) == matrix4x4::identity());

    EXPECT_TRUE(normalize(quaternion{0, 0, 0, 0}) == quaternion::identity());
    EXPECT_TRUE(quaternion_from_axis_angle(vector3{0, 0, 0}, 1.0f) ==
                quaternion::identity());

    vector3 scale, translation;
    quaternion rotation;
    EXPECT_FALSE(decompose(scaling_matrix(vector3{1, 0, 1}), scale, rotation,
                           translation));

    EXPECT_TRUE(look_at_lh(vector3{0, 0, 0}, vector3{0, 5, 0}, vector3{0, 1, 0}) ==
                matrix4x4::identity());

    EXPECT_TRUE(plane_from_point_normal(vector3{1, 2, 3}, vector3{0, 0, 0}) ==
                plane{});

    const aabb empty;
    EXPECT_TRUE(empty.is_empty());
    EXPECT_FALSE(intersects(empty, vector3{}));
    EXPECT_EQ(merge(empty, vector3{1, 2, 3}),
              aabb(vector3{1, 2, 3}, vector3{}));

    EXPECT_TRUE(std::isnan(math::sin(9.0e5f)));
    EXPECT_FALSE(std::isnan(math::sin(8.0e5f)));
}

// ------------------------------------------------- guide section 5: geometry
// "Contains는 비대칭이다"
TEST(guide, contains_is_asymmetric_as_documented) {
    const aabb box = aabb::from_min_max(vector3{-1, -1, -1}, vector3{1, 1, 1});
    const sphere huge{vector3{0, 0, 0}, 3.0f};
    EXPECT_EQ(contains(box, huge), containment::intersects);
    EXPECT_EQ(contains(huge, box), containment::contains);
}

// "접촉은 교차로 센다"
TEST(guide, touching_counts_as_intersecting) {
    EXPECT_TRUE(intersects(sphere{vector3{0, 0, 0}, 1.0f},
                           sphere{vector3{2, 0, 0}, 1.0f}));
}

// "레이는 반직선이다"
TEST(guide, ray_is_a_half_line) {
    float distance = -1.0f;
    EXPECT_FALSE(raycast(ray{vector3{0, 0, 5}, vector3{0, 0, 1}},
                         sphere{vector3{0, 0, 0}, 1.0f}, distance));
    ASSERT_TRUE(raycast(ray{vector3{0, 0, 0}, vector3{0, 0, 1}},
                        sphere{vector3{0, 0, 0}, 1.0f}, distance));
    EXPECT_NEAR(distance, 0.0f, epsilon) << "inside means zero";
}

// ------------------------------------- guide section 7: the migration table
// Spot checks on the rows a reader is most likely to trust blindly, each one a
// place where DirectXMath and Mathematics could plausibly have disagreed.
TEST(guide, migration_table_rows_are_accurate) {
    // "XMMatrixMultiply(a, b) -> a * b (순서 동일)"
    const matrix4x4 a = rotation_z(0.3f);
    const matrix4x4 b = translation_matrix(vector3{1, 2, 3});
    EXPECT_TRUE(near_equal(transform_point(vector3{1, 0, 0}, a * b),
                          transform_point(transform_point(vector3{1, 0, 0}, a), b),
                          epsilon));

    // "XMPlaneDotCoord -> signed_distance"
    const plane p = plane_from_point_normal(vector3{0, 0, 5}, vector3{0, 0, 1});
    EXPECT_NEAR(signed_distance(p, vector3{0, 0, 9}), 4.0f, epsilon);

    // "XMVector3TransformCoord -> transform_point",
    // "XMVector3TransformNormal -> transform_direction"
    const matrix4x4 t = translation_matrix(vector3{10, 0, 0});
    EXPECT_TRUE(near_equal(transform_point(vector3{1, 0, 0}, t), vector3{11, 0, 0},
                          epsilon));
    EXPECT_TRUE(near_equal(transform_direction(vector3{1, 0, 0}, t),
                          vector3{1, 0, 0}, epsilon));

    // "XMMatrixAffineTransformation -> compose(scale, rot, translation)"
    EXPECT_TRUE(near_equal(
        compose(vector3{2, 2, 2}, quaternion::identity(), vector3{1, 0, 0}),
        scaling_matrix(2.0f) * translation_matrix(vector3{1, 0, 0}), epsilon));

    // "PlaneIntersectionType -> plane_side (INTERSECTING -> straddling)"
    EXPECT_EQ(classify(sphere{vector3{0, 0, 0}, 1.0f}, plane{0, 0, 1, 0}),
              plane_side::straddling);
}

// "Est 이외의 근사 없음" -- the exact form and the estimate must actually
// differ, or the guide is promising a trade that does not exist.
TEST(guide, estimate_forms_trade_accuracy_for_speed) {
    const vector3 v{3, 4, 12};
    const vector3 exact = normalize(v);
    const vector3 estimate = normalize_est(v);
    EXPECT_TRUE(near_equal(exact, estimate, 1e-2f)) << "still close";
    EXPECT_NEAR(length(exact), 1.0f, 1e-6f);
    EXPECT_NEAR(length(estimate), 1.0f, 1e-2f);
}

// -------------------------------------- guide section: C++20 range view
// "두 view는 튜플 프로토콜도 만족하므로 루프 없이 이름으로 받을 수 있다."
// "바인딩은 원본을 참조하므로 쓰기가 그대로 관통하고, 상수평가에서도 동작한다."
TEST(guide, structured_bindings_name_components_and_rows) {
    vector4 color{1, 0.5f, 0.25f, 1};
    auto&& [x, y, z, w] = math::components(color);
    EXPECT_FLOAT_EQ(x, 1.0f);
    EXPECT_FLOAT_EQ(y, 0.5f);

    z = 0.75f;
    EXPECT_FLOAT_EQ(color.z, 0.75f);
    EXPECT_FLOAT_EQ(w, 1.0f);

    matrix4x4 world = matrix4x4::identity();
    auto&& [row0, row1, row2, row3] = math::rows(world);
    static_assert(decltype(row0)::extent == 4);
    row3[0] = 5.0f;
    EXPECT_FLOAT_EQ(world.m[3][0], 5.0f);
    EXPECT_FLOAT_EQ(row1[1], 1.0f);
    EXPECT_FLOAT_EQ(row2[2], 1.0f);
}

// "일반 코드에서는 math::ranges::get<I>(view)를 쓴다."
TEST(guide, free_get_is_the_generic_spelling) {
    vector3 value{1, 2, 3};
    auto view = math::components(value);
    EXPECT_FLOAT_EQ(math::ranges::get<1>(view), 2.0f);

    math::ranges::get<0>(view) = -1.0f;
    EXPECT_EQ(value, vector3(-1, 2, 3));
}
