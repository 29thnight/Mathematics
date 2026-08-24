// Transform, view and projection matrices.
//
// These are the functions where being subtly wrong is invisible: a mirrored
// scene, a depth buffer running backwards, a projection that is fine in the
// middle and wrong at the corners. So the tests here check the properties a
// caller depends on -- where the near plane lands, which way the camera faces,
// what a corner of the frustum maps to -- rather than just comparing sixteen
// floats against sixteen other floats.

#include "support/reg_testing.hpp"

#include <mathematics/transform.hpp>

#include <cmath>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHEMATICS_TEST_HAS_DXMATH 1
#else
#  define MATHEMATICS_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace math_test;
using math::matrix4x4;
using math::quaternion;
using math::vector3;
using math::vector4;

constexpr float epsilon = 1e-5f;

// A point through a projection, with the perspective divide applied.
vector3 project(const vector3& view_space, const matrix4x4& proj) {
    const vector4 clip = vector4{view_space.x, view_space.y, view_space.z, 1.0f} * proj;
    return vector3{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

} // namespace

// ------------------------------------------------------------ basic transforms
TEST(transform_basics, scale_and_translate) {
    const matrix4x4 s = math::scaling_matrix(vector3{2, 3, 4});
    EXPECT_TRUE(math::near_equal(math::transform_point(vector3{1, 1, 1}, s),
                                 vector3{2, 3, 4}, epsilon));

    const matrix4x4 t = math::translation_matrix(vector3{10, 20, 30});
    EXPECT_TRUE(math::near_equal(math::transform_point(vector3{1, 2, 3}, t),
                                 vector3{11, 22, 33}, epsilon));
    // A direction ignores translation; that is the whole point of the split.
    EXPECT_TRUE(math::near_equal(math::transform_direction(vector3{1, 2, 3}, t),
                                 vector3{1, 2, 3}, epsilon));
}

// right-handed about each axis, matching the quaternion of the same angle.
TEST(transform_basics, axis_rotations_are_right_handed_and_match_quaternions) {
    const float a = 0.7f;
    const struct { matrix4x4 m; vector3 axis; const char* name; } cases[] = {
        {math::rotation_x(a), vector3{1, 0, 0}, "X"},
        {math::rotation_y(a), vector3{0, 1, 0}, "Y"},
        {math::rotation_z(a), vector3{0, 0, 1}, "Z"},
    };
    for (const auto& c : cases) {
        EXPECT_TRUE(math::near_equal(
            c.m, math::rotation_matrix(math::quaternion_from_axis_angle(c.axis, a)),
            1e-5f)) << c.name;
    }

    // The concrete right-handed check: about +Z, +X turns toward +Y.
    EXPECT_TRUE(math::near_equal(
        math::transform_direction(vector3{1, 0, 0},
                                  math::rotation_z(math::half_pi)),
        vector3{0, 1, 0}, epsilon));
}

// ------------------------------------------------------------------ TRS
// Scale first, rotate second, translate last -- so the translation is neither
// scaled nor rotated.
TEST(transform_compose, applies_scale_then_rotation_then_translation) {
    const vector3 scale{2, 2, 2};
    const quaternion rot =
        math::quaternion_from_axis_angle(vector3{0, 0, 1}, math::half_pi);
    const vector3 translation{10, 0, 0};

    const matrix4x4 m = math::compose(scale, rot, translation);

    // +X scales to (2,0,0), rotates to (0,2,0), translates to (10,2,0).
    EXPECT_TRUE(math::near_equal(math::transform_point(vector3{1, 0, 0}, m),
                                 vector3{10, 2, 0}, epsilon));
    // The translation must come through unscaled and unrotated.
    EXPECT_TRUE(math::near_equal(m.translation(), translation, epsilon));
}

TEST(transform_compose, matches_the_equivalent_matrix_product) {
    random_vectors gen(random_seed + 300);
    for (int n = 0; n < 64; ++n) {
        const sample s = gen.next();
        const vector3 scale{std::abs(s.f[0]) * 0.05f + 0.5f,
                            std::abs(s.f[1]) * 0.05f + 0.5f,
                            std::abs(s.f[2]) * 0.05f + 0.5f};
        const vector3 translation{s.f[3], s.f[0] * 0.1f, s.f[1] * 0.1f};
        const quaternion rot = math::quaternion_from_axis_angle(
            vector3{s.f[0], s.f[1], s.f[2] + 1.0f}, s.f[3] * 0.02f);

        const matrix4x4 built = math::compose(scale, rot, translation);
        const matrix4x4 product = math::scaling_matrix(scale) *
                                  math::rotation_matrix(rot) *
                                  math::translation_matrix(translation);
        EXPECT_TRUE(math::near_equal(built, product, 1e-4f)) << n;
    }
}

TEST(transform_decompose, round_trips_what_compose_built) {
    random_vectors gen(random_seed + 301);
    for (int n = 0; n < 128; ++n) {
        const sample s = gen.next();
        const vector3 scale{std::abs(s.f[0]) * 0.05f + 0.5f,
                            std::abs(s.f[1]) * 0.05f + 0.5f,
                            std::abs(s.f[2]) * 0.05f + 0.5f};
        const vector3 translation{s.f[3], s.f[0] * 0.1f, s.f[1] * 0.1f};
        const quaternion rot = math::normalize(math::quaternion_from_axis_angle(
            vector3{s.f[0], s.f[1], s.f[2] + 1.0f}, s.f[3] * 0.02f));

        const matrix4x4 m = math::compose(scale, rot, translation);

        vector3 out_scale, out_translation;
        quaternion out_rot;
        ASSERT_TRUE(math::decompose(m, out_scale, out_rot, out_translation)) << n;

        EXPECT_TRUE(math::near_equal(out_scale, scale, 1e-3f)) << n;
        EXPECT_TRUE(math::near_equal(out_translation, translation, 1e-4f)) << n;
        EXPECT_TRUE(math::same_rotation(out_rot, rot, 1e-3f)) << n;

        // The one that has to hold even where the parts are ambiguous.
        EXPECT_TRUE(math::near_equal(
            math::compose(out_scale, out_rot, out_translation), m, 1e-3f)) << n;
    }
}

TEST(transform_decompose, optional_overload_owns_its_success_value) {
    const matrix4x4 source = math::compose(
        vector3{2, 3, 4},
        math::quaternion_from_axis_angle(vector3{1, 2, 3}, 0.7f),
        vector3{5, 6, 7});

    const auto parts = math::decompose(source);
    ASSERT_TRUE(parts.has_value());
    EXPECT_TRUE(math::near_equal(
        math::compose(parts->scale, parts->rotation, parts->translation),
        source, 1e-4f));

    EXPECT_FALSE(math::decompose(math::scaling_matrix(vector3{1, 0, 1}))
                     .has_value());
}

// A rotation matrix has determinant +1, so a mirror cannot be one. The
// convention is to fold the reflection into a negative X scale; what must hold
// is that recomposing reproduces the original matrix.
TEST(transform_decompose, folds_a_reflection_into_negative_scale) {
    for (const vector3 mirror : {vector3{-1, 1, 1}, vector3{1, -1, 1},
                                 vector3{1, 1, -1}}) {
        const matrix4x4 m = math::scaling_matrix(mirror);
        vector3 scale, translation;
        quaternion rot;
        ASSERT_TRUE(math::decompose(m, scale, rot, translation));

        EXPECT_LT(scale.x * scale.y * scale.z, 0.0f)
            << "the reflection has to survive somewhere in the scale";
        EXPECT_TRUE(math::near_equal(
            math::compose(scale, rot, translation), m, 1e-5f));
    }
}

// A reflection folded into a matrix that ALSO carries a real rotation -- the
// pure-mirror test above keeps the rotation at identity, where a sign error in
// the det<0 fold's interaction with Shepperd's method is invisible. The angle is
// near pi on purpose, so the quaternion extraction runs a trace<0 branch at the
// same time. This is Phase 3's identity-times-identity lesson applied here.
TEST(transform_decompose, folds_reflection_combined_with_rotation) {
    const struct { vector3 scale; vector3 axis; float angle; } cases[] = {
        {{-2, 3, 4}, {1, 2, 3}, 2.5f},
        {{2, -3, 4}, {0, 1, 0}, 3.1f},
        {{2, 3, -4}, {1, 0, 1}, 0.7f},
        {{-1, -2, -3}, {1, 1, 1}, 2.9f},   // det = -6: odd reflection count
    };
    for (const auto& c : cases) {
        const quaternion rot = math::quaternion_from_axis_angle(c.axis, c.angle);
        const vector3 translation{5, -1, 2};
        const matrix4x4 m = math::compose(c.scale, rot, translation);

        vector3 out_scale, out_translation;
        quaternion out_rot;
        ASSERT_TRUE(math::decompose(m, out_scale, out_rot, out_translation));

        EXPECT_LT(out_scale.x * out_scale.y * out_scale.z, 0.0f)
            << "the reflection must survive in the scale's sign";
        // Which axis carries the mirror is a convention, not a recovery -- the
        // one thing that must hold is that the parts recompose to the original.
        EXPECT_TRUE(math::near_equal(
            math::compose(out_scale, out_rot, out_translation), m, 1e-3f))
            << "scale (" << c.scale.x << "," << c.scale.y << "," << c.scale.z
            << ") angle " << c.angle;
    }
}

// A zero scale destroys the direction of a basis vector; no rotation can be
// recovered from it, so the function says so instead of guessing.
TEST(transform_decompose, rejects_degenerate_matrices) {
    vector3 scale, translation;
    quaternion rot;

    EXPECT_FALSE(math::decompose(math::scaling_matrix(vector3{1, 0, 1}), scale,
                                  rot, translation));
    EXPECT_FALSE(math::decompose(math::scaling_matrix(vector3{0, 0, 0}), scale,
                                  rot, translation));
    EXPECT_FALSE(math::decompose(matrix4x4{}, scale, rot, translation))
        << "an all-zero matrix has no decomposition";

    matrix4x4 with_nan = matrix4x4::identity();
    with_nan.m[1][1] = quiet_nan();
    EXPECT_FALSE(math::decompose(with_nan, scale, rot, translation));
}

// ------------------------------------------------------------------- view
// The defining property: the camera's own position lands at the origin, and the
// direction it faces lands on +Z (left-handed) or -Z (right-handed).
TEST(transform_view, places_the_eye_at_the_origin) {
    const vector3 eye{3, 4, -5};
    const vector3 target{1, 0, 2};
    const vector3 up{0, 1, 0};

    for (const matrix4x4& view : {math::look_at_lh(eye, target, up),
                                  math::look_at_rh(eye, target, up)}) {
        EXPECT_TRUE(math::near_equal(math::transform_point(eye, view),
                                     vector3{0, 0, 0}, 1e-4f));
    }
}

TEST(transform_view, handedness_decides_which_way_the_camera_looks) {
    const vector3 eye{0, 0, -5};
    const vector3 target{0, 0, 0};
    const vector3 up{0, 1, 0};

    // The target sits five units in front of the camera either way; the sign of
    // the view-space z is what the handedness decides.
    const vector3 lh = math::transform_point(target, math::look_at_lh(eye, target, up));
    const vector3 rh = math::transform_point(target, math::look_at_rh(eye, target, up));

    EXPECT_NEAR(lh.z, 5.0f, 1e-4f) << "left-handed looks down +Z";
    EXPECT_NEAR(rh.z, -5.0f, 1e-4f) << "right-handed looks down -Z";
}

// Looking straight up with the conventional world up -- an easy real mistake --
// makes forward parallel to up, the cross collapses, and before the guard this
// returned a matrix with two zero basis columns that silently flattened the
// scene. identity on degenerate input is the library-wide policy (singular
// inverse, zero normalize), and now the view matrices follow it too.
TEST(transform_view, parallel_up_and_forward_returns_identity) {
    const vector3 up{0, 1, 0};
    EXPECT_TRUE(math::look_at_lh(vector3{0, 0, 0}, vector3{0, 5, 0}, up) ==
                matrix4x4::identity());
    EXPECT_TRUE(math::look_at_rh(vector3{0, 0, 0}, vector3{0, -5, 0}, up) ==
                matrix4x4::identity());
    EXPECT_TRUE(math::look_to_lh(vector3{1, 2, 3}, vector3{0, -1, 0}, up) ==
                matrix4x4::identity());
    // A zero direction has no view to describe either.
    EXPECT_TRUE(math::look_to_lh(vector3{1, 2, 3}, vector3{0, 0, 0}, up) ==
                matrix4x4::identity());
    // bit_and barely-not-parallel still produces a real view matrix.
    EXPECT_FALSE(math::look_at_lh(vector3{0, 0, 0}, vector3{0.01f, 5, 0}, up) ==
                 matrix4x4::identity());
}

TEST(transform_view, look_at_and_look_to_agree) {
    const vector3 eye{2, -1, 4};
    const vector3 target{-3, 5, 0};
    const vector3 up{0, 1, 0};
    const vector3 direction = target - eye;

    EXPECT_TRUE(math::near_equal(math::look_at_lh(eye, target, up),
                                 math::look_to_lh(eye, direction, up), 1e-5f));
    EXPECT_TRUE(math::near_equal(math::look_at_rh(eye, target, up),
                                 math::look_to_rh(eye, direction, up), 1e-5f));
}

// A view matrix is a rigid motion, so it must not stretch anything.
TEST(transform_view, preserves_distances) {
    const matrix4x4 view =
        math::look_at_lh(vector3{5, 2, -3}, vector3{0, 1, 1}, vector3{0, 1, 0});
    random_vectors gen(random_seed + 302);
    for (int n = 0; n < 32; ++n) {
        const sample s = gen.next();
        const vector3 a{s.f[0], s.f[1], s.f[2]};
        const sample s2 = gen.next();
        const vector3 b{s2.f[0], s2.f[1], s2.f[2]};

        EXPECT_NEAR(math::distance(math::transform_point(a, view),
                                    math::transform_point(b, view)),
                    math::distance(a, b), 1e-2f) << n;
    }
}

// ------------------------------------------------------------- projection
// Direct3D depth: the near plane is 0 and the far plane is 1, not -1 and 1.
TEST(transform_projection, depth_runs_from_zero_at_near_to_one_at_far) {
    const float near_z = 0.5f, far_z = 250.0f;

    const matrix4x4 lh = math::perspective_fov_lh(math::half_pi, 1.6f, near_z, far_z);
    EXPECT_NEAR(project(vector3{0, 0, near_z}, lh).z, 0.0f, 1e-4f);
    EXPECT_NEAR(project(vector3{0, 0, far_z}, lh).z, 1.0f, 1e-4f);

    const matrix4x4 rh = math::perspective_fov_rh(math::half_pi, 1.6f, near_z, far_z);
    EXPECT_NEAR(project(vector3{0, 0, -near_z}, rh).z, 0.0f, 1e-4f);
    EXPECT_NEAR(project(vector3{0, 0, -far_z}, rh).z, 1.0f, 1e-4f);

    const matrix4x4 ol = math::orthographic_lh(4, 4, near_z, far_z);
    EXPECT_NEAR(project(vector3{0, 0, near_z}, ol).z, 0.0f, 1e-4f);
    EXPECT_NEAR(project(vector3{0, 0, far_z}, ol).z, 1.0f, 1e-4f);

    const matrix4x4 orr = math::orthographic_rh(4, 4, near_z, far_z);
    EXPECT_NEAR(project(vector3{0, 0, -near_z}, orr).z, 0.0f, 1e-4f);
    EXPECT_NEAR(project(vector3{0, 0, -far_z}, orr).z, 1.0f, 1e-4f);
}

// Depth must increase monotonically with distance, or the depth buffer sorts
// backwards -- the classic symptom of a swapped near and far.
TEST(transform_projection, depth_increases_with_distance) {
    const matrix4x4 p = math::perspective_fov_lh(1.0f, 1.777f, 0.1f, 1000.0f);
    float previous = -1.0f;
    for (float z = 0.1f; z < 1000.0f; z *= 1.7f) {
        const float depth = project(vector3{0, 0, z}, p).z;
        EXPECT_GT(depth, previous) << "at z = " << z;
        EXPECT_GE(depth, -1e-5f);
        EXPECT_LE(depth, 1.0f + 1e-5f);
        previous = depth;
    }
}

// The field of view has to be the angle it claims: at the near plane, a point on
// the edge of the vertical field lands exactly on the top of the clip cube.
TEST(transform_projection, field_of_view_is_the_angle_it_claims) {
    const float fov = 1.2f;
    const float aspect = 1.5f;
    const float near_z = 2.0f;
    const matrix4x4 p = math::perspective_fov_lh(fov, aspect, near_z, 100.0f);

    // Half the vertical field, at the near plane.
    const float half_height = near_z * math::tan(fov * 0.5f);
    EXPECT_NEAR(project(vector3{0, half_height, near_z}, p).y, 1.0f, 1e-4f);
    EXPECT_NEAR(project(vector3{0, -half_height, near_z}, p).y, -1.0f, 1e-4f);

    // The horizontal field is the vertical one widened by the aspect ratio.
    const float half_width = half_height * aspect;
    EXPECT_NEAR(project(vector3{half_width, 0, near_z}, p).x, 1.0f, 1e-4f);
}

TEST(transform_projection, orthographic_maps_the_box_to_the_clip_cube) {
    const matrix4x4 p = math::orthographic_lh(8.0f, 6.0f, 1.0f, 51.0f);
    EXPECT_TRUE(math::near_equal(project(vector3{4, 3, 1}, p), vector3{1, 1, 0},
                                 1e-4f));
    EXPECT_TRUE(math::near_equal(project(vector3{-4, -3, 51}, p),
                                 vector3{-1, -1, 1}, 1e-4f));
    // Unlike a perspective projection, parallel lines stay parallel: doubling x
    // doubles the projected x regardless of depth.
    EXPECT_NEAR(project(vector3{2, 0, 30}, p).x,
                project(vector3{2, 0, 5}, p).x, 1e-5f);
}

TEST(transform_projection, off_center_matches_centered_when_symmetric) {
    const matrix4x4 centered = math::orthographic_lh(8.0f, 6.0f, 1.0f, 51.0f);
    const matrix4x4 off_center =
        math::orthographic_off_center_lh(-4.0f, 4.0f, -3.0f, 3.0f, 1.0f, 51.0f);
    EXPECT_TRUE(math::near_equal(centered, off_center, 1e-5f));

    const matrix4x4 centered_rh = math::orthographic_rh(8.0f, 6.0f, 1.0f, 51.0f);
    const matrix4x4 off_center_rh =
        math::orthographic_off_center_rh(-4.0f, 4.0f, -3.0f, 3.0f, 1.0f, 51.0f);
    EXPECT_TRUE(math::near_equal(centered_rh, off_center_rh, 1e-5f));
}

// An asymmetric frustum is the point of the off-center form; a symmetric test
// alone would not notice the offset terms being dropped.
TEST(transform_projection, off_center_handles_an_asymmetric_box) {
    const matrix4x4 p =
        math::orthographic_off_center_lh(-1.0f, 7.0f, 2.0f, 8.0f, 1.0f, 11.0f);
    EXPECT_TRUE(math::near_equal(project(vector3{-1, 2, 1}, p),
                                 vector3{-1, -1, 0}, 1e-4f));
    EXPECT_TRUE(math::near_equal(project(vector3{7, 8, 11}, p),
                                 vector3{1, 1, 1}, 1e-4f));
    EXPECT_TRUE(math::near_equal(project(vector3{3, 5, 1}, p),
                                 vector3{0, 0, 0}, 1e-4f))
        << "the centre of the box maps to the centre of the near face";
}

// The two ways to spell the same perspective projection.
TEST(transform_projection, fov_and_size_forms_agree) {
    const float fov = 0.9f, near_z = 0.75f, far_z = 300.0f, aspect = 16.0f / 9.0f;
    const float height = 2.0f * near_z * math::tan(fov * 0.5f);
    const float width = height * aspect;

    EXPECT_TRUE(math::near_equal(math::perspective_fov_lh(fov, aspect, near_z, far_z),
                                 math::perspective_lh(width, height, near_z, far_z),
                                 1e-4f));
    EXPECT_TRUE(math::near_equal(math::perspective_fov_rh(fov, aspect, near_z, far_z),
                                 math::perspective_rh(width, height, near_z, far_z),
                                 1e-4f));
}

// The handed pairs must actually differ, or one of them is quietly wrong.
TEST(transform_projection, handedness_pairs_are_not_the_same_matrix) {
    EXPECT_FALSE(math::near_equal(math::perspective_fov_lh(1.0f, 1.5f, 1, 100),
                                  math::perspective_fov_rh(1.0f, 1.5f, 1, 100),
                                  1e-3f));
    EXPECT_FALSE(math::near_equal(math::orthographic_lh(4, 4, 1, 100),
                                  math::orthographic_rh(4, 4, 1, 100), 1e-3f));
    EXPECT_FALSE(math::near_equal(
        math::look_at_lh(vector3{0, 0, -5}, vector3{}, vector3{0, 1, 0}),
        math::look_at_rh(vector3{0, 0, -5}, vector3{}, vector3{0, 1, 0}), 1e-3f));
}

// ------------------------------------------------------------------- constexpr
static_assert(math::scaling_matrix(vector3{2, 3, 4})(1, 1) == 3.0f);
static_assert(math::translation_matrix(vector3{5, 6, 7})(3, 1) == 6.0f);
static_assert(math::rotation_z(0.0f) == matrix4x4::identity());

constexpr matrix4x4 compile_time_trs =
    math::compose(vector3{2, 2, 2},
                   math::quaternion_from_axis_angle(vector3{0, 0, 1},
                                                  math::half_pi),
                   vector3{10, 0, 0});
static_assert(compile_time_trs.m[3][0] == 10.0f);
static_assert(compile_time_trs.m[0][1] > 1.999f);   // scale 2, quarter turn

constexpr matrix4x4 compile_time_projection =
    math::perspective_fov_lh(math::half_pi, 1.0f, 1.0f, 100.0f);
static_assert(compile_time_projection.m[2][3] == 1.0f);
static_assert(compile_time_projection.m[3][3] == 0.0f);

constexpr matrix4x4 compile_time_view =
    math::look_at_lh(vector3{0, 0, -5}, vector3{0, 0, 0}, vector3{0, 1, 0});
static_assert(compile_time_view.m[3][2] > 4.999f);

// The rest of the constexpr surface, proven rather than presumed. decompose
// takes out-parameters, so it gets a wrapper function.
static_assert(math::rotation_x(0.5f)(1, 1) > 0.87f);
static_assert(math::rotation_y(0.5f)(0, 0) > 0.87f);
static_assert(math::rotation_z(0.5f)(0, 1) > 0.47f);
static_assert(math::scaling_matrix(2.0f)(2, 2) == 2.0f);
static_assert(math::look_at_rh(vector3{0, 0, -5}, vector3{}, vector3{0, 1, 0})
                  .m[2][2] < 0.0f);
static_assert(math::look_to_lh(vector3{}, vector3{0, 0, 1}, vector3{0, 1, 0}) ==
              matrix4x4::identity());
// RH negates the direction on the way in, so looking along +Z puts -1 in the
// basis -- the entry that distinguishes it from the LH form given the same
// direction.
static_assert(math::look_to_rh(vector3{}, vector3{0, 0, 1}, vector3{0, 1, 0})
                  .m[2][2] < 0.0f);
static_assert(math::look_to_lh(vector3{}, vector3{0, 0, 1}, vector3{0, 1, 0})
                  .m[2][2] > 0.0f);
static_assert(math::orthographic_lh(4, 4, 1, 10)(0, 0) == 0.5f);
static_assert(math::orthographic_rh(4, 4, 1, 10)(2, 2) < 0.0f);
static_assert(math::orthographic_off_center_lh(-1, 7, 2, 8, 1, 11)(3, 0) == -0.75f);
static_assert(math::orthographic_off_center_rh(-1, 7, 2, 8, 1, 11)(0, 0) == 0.25f);
static_assert(math::perspective_lh(2, 2, 1, 100)(0, 0) == 1.0f);
static_assert(math::perspective_rh(2, 2, 1, 100)(2, 3) == -1.0f);

namespace {
constexpr float compile_time_decompose() {
    vector3 scale, translation;
    quaternion rot;
    const bool ok = math::decompose(
        math::compose(vector3{2, 3, 4}, quaternion::identity(),
                       vector3{5, 6, 7}),
        scale, rot, translation);
    return ok ? scale.y + translation.z : -1.0f;   // 3 + 7
}
} // namespace
static_assert(compile_time_decompose() == 10.0f);
constexpr auto compile_time_optional_decompose = math::decompose(
    math::compose(vector3{2, 3, 4}, quaternion::identity(), vector3{5, 6, 7}));
static_assert(compile_time_optional_decompose.has_value());
static_assert(compile_time_optional_decompose->scale.y == 3.0f);
static_assert(compile_time_optional_decompose->translation.z == 7.0f);

// In ULP, for the reason given on the quaternion version: Clang may fuse a
// multiply-add at run time that constant evaluation computed unfused.
TEST(transform_constexpr, compile_time_matches_runtime_to_within_a_few_ulp) {
    auto compare = [](const matrix4x4& a, const matrix4x4& b, const char* what) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                EXPECT_TRUE(same_to_within(a.m[i][j], b.m[i][j])) << what << " at " << i << "," << j;
            }
        }
    };

    compare(math::compose(
                vector3{opaque(2.0f), opaque(2.0f), opaque(2.0f)},
                math::quaternion_from_axis_angle(
                    vector3{opaque(0.0f), opaque(0.0f), opaque(1.0f)},
                    opaque(math::half_pi)),
                vector3{opaque(10.0f), opaque(0.0f), opaque(0.0f)}),
            compile_time_trs, "compose");

    compare(math::perspective_fov_lh(opaque(math::half_pi), opaque(1.0f),
                                    opaque(1.0f), opaque(100.0f)),
            compile_time_projection, "perspective_fov_lh");

    compare(math::look_at_lh(vector3{opaque(0.0f), opaque(0.0f), opaque(-5.0f)},
                            vector3{opaque(0.0f), opaque(0.0f), opaque(0.0f)},
                            vector3{opaque(0.0f), opaque(1.0f), opaque(0.0f)}),
            compile_time_view, "look_at_lh");
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHEMATICS_TEST_HAS_DXMATH
namespace {

bool matches_xm(const matrix4x4& mine, DirectX::FXMMATRIX theirs, float eps) {
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

TEST(transform_dx_parity, projections_match_direct_x_math) {
    const float fov = 1.1f, aspect = 1.7778f, near_z = 0.3f, far_z = 500.0f;

    EXPECT_TRUE(matches_xm(math::perspective_fov_lh(fov, aspect, near_z, far_z),
                          DirectX::XMMatrixPerspectiveFovLH(fov, aspect, near_z,
                                                            far_z), 1e-5f));
    EXPECT_TRUE(matches_xm(math::perspective_fov_rh(fov, aspect, near_z, far_z),
                          DirectX::XMMatrixPerspectiveFovRH(fov, aspect, near_z,
                                                            far_z), 1e-5f));
    EXPECT_TRUE(matches_xm(math::perspective_lh(4, 3, near_z, far_z),
                          DirectX::XMMatrixPerspectiveLH(4, 3, near_z, far_z),
                          1e-5f));
    EXPECT_TRUE(matches_xm(math::perspective_rh(4, 3, near_z, far_z),
                          DirectX::XMMatrixPerspectiveRH(4, 3, near_z, far_z),
                          1e-5f));
    EXPECT_TRUE(matches_xm(math::orthographic_lh(8, 6, near_z, far_z),
                          DirectX::XMMatrixOrthographicLH(8, 6, near_z, far_z),
                          1e-5f));
    EXPECT_TRUE(matches_xm(math::orthographic_rh(8, 6, near_z, far_z),
                          DirectX::XMMatrixOrthographicRH(8, 6, near_z, far_z),
                          1e-5f));
    EXPECT_TRUE(matches_xm(
        math::orthographic_off_center_lh(-1, 7, 2, 8, near_z, far_z),
        DirectX::XMMatrixOrthographicOffCenterLH(-1, 7, 2, 8, near_z, far_z),
        1e-5f));
    EXPECT_TRUE(matches_xm(
        math::orthographic_off_center_rh(-1, 7, 2, 8, near_z, far_z),
        DirectX::XMMatrixOrthographicOffCenterRH(-1, 7, 2, 8, near_z, far_z),
        1e-5f));
}

TEST(transform_dx_parity, view_and_basic_transforms_match_direct_x_math) {
    random_vectors gen(random_seed + 320);
    for (int n = 0; n < 32; ++n) {
        const sample s = gen.next();
        const vector3 eye{s.f[0], s.f[1], s.f[2]};
        const sample s2 = gen.next();
        const vector3 target{s2.f[0], s2.f[1], s2.f[2]};
        if (math::length_sq(target - eye) < 1.0f) continue;

        const DirectX::XMVECTOR e =
            DirectX::XMVectorSet(eye.x, eye.y, eye.z, 1.0f);
        const DirectX::XMVECTOR t =
            DirectX::XMVectorSet(target.x, target.y, target.z, 1.0f);
        const DirectX::XMVECTOR u = DirectX::XMVectorSet(0, 1, 0, 0);

        EXPECT_TRUE(matches_xm(math::look_at_lh(eye, target, vector3{0, 1, 0}),
                              DirectX::XMMatrixLookAtLH(e, t, u), 1e-4f)) << n;
        EXPECT_TRUE(matches_xm(math::look_at_rh(eye, target, vector3{0, 1, 0}),
                              DirectX::XMMatrixLookAtRH(e, t, u), 1e-4f)) << n;

        const float a = s.f[3] * 0.03f;
        EXPECT_TRUE(matches_xm(math::rotation_x(a), DirectX::XMMatrixRotationX(a),
                              1e-5f)) << n;
        EXPECT_TRUE(matches_xm(math::rotation_y(a), DirectX::XMMatrixRotationY(a),
                              1e-5f)) << n;
        EXPECT_TRUE(matches_xm(math::rotation_z(a), DirectX::XMMatrixRotationZ(a),
                              1e-5f)) << n;
    }
}

// XMMatrixAffineTransformation is DirectXMath's TRS, and it has to agree about
// the order the three parts apply in.
TEST(transform_dx_parity, compose_matches_affine_transformation) {
    random_vectors gen(random_seed + 321);
    for (int n = 0; n < 32; ++n) {
        const sample s = gen.next();
        const vector3 scale{std::abs(s.f[0]) * 0.05f + 0.5f,
                            std::abs(s.f[1]) * 0.05f + 0.5f,
                            std::abs(s.f[2]) * 0.05f + 0.5f};
        const vector3 translation{s.f[3], s.f[0] * 0.1f, s.f[1] * 0.1f};
        const quaternion rot = math::quaternion_from_pitch_yaw_roll(
            s.f[0] * 0.02f, s.f[1] * 0.02f, s.f[2] * 0.02f);

        EXPECT_TRUE(matches_xm(
            math::compose(scale, rot, translation),
            DirectX::XMMatrixAffineTransformation(
                DirectX::XMVectorSet(scale.x, scale.y, scale.z, 0.0f),
                DirectX::XMVectorZero(),
                DirectX::XMVectorSet(rot.x, rot.y, rot.z, rot.w),
                DirectX::XMVectorSet(translation.x, translation.y,
                                     translation.z, 0.0f)),
            1e-4f)) << n;
    }
}
#endif // MATHEMATICS_TEST_HAS_DXMATH
