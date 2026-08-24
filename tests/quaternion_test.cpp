// quaternion.
//
// As with the matrices, the conventions matter more than the arithmetic. A
// quaternion library with the multiplication order reversed still composes,
// still normalizes, still round-trips through a matrix, and puts every rotation
// in a scene the wrong way round. So the order is pinned three ways: against a
// hand-computed rotation, against the matrix layer it has to agree with, and
// against DirectXMath.

#include "support/reg_testing.hpp"

#include <mathematics/quaternion.hpp>

#include <cmath>
#include <limits>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHEMATICS_TEST_HAS_DXMATH 1
#else
#  define MATHEMATICS_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace math_test;
using math::matrix3x3;
using math::matrix4x4;
using math::quaternion;
using math::vector3;

constexpr float epsilon = 1e-5f;

quaternion random_rotation(random_vectors& gen) {
    const sample s = gen.next();
    // Any non-degenerate axis will do; the magnitude is normalized away.
    const vector3 axis{s.f[0], s.f[1], s.f[2]};
    if (math::length_sq(axis) < 1e-6f) return quaternion::identity();
    return math::quaternion_from_axis_angle(axis, s.f[3] * 0.03f);
}

bool near_vector(const vector3& a, const vector3& b, float eps = epsilon) {
    return math::near_equal(a, b, eps);
}

} // namespace

// ---------------------------------------------------------------------- layout
static_assert(sizeof(quaternion) == 16);
static_assert(std::is_standard_layout_v<quaternion>);

// A default-constructed rotation must be usable as a rotation. zero is not one.
static_assert(quaternion{}.w == 1.0f);
static_assert(quaternion{} == quaternion::identity());

TEST(quaternion_layout, scalar_is_last) {
    const quaternion q{1, 2, 3, 4};
    const float* raw = &q.x;
    EXPECT_FLOAT_EQ(raw[0], 1.0f);
    EXPECT_FLOAT_EQ(raw[3], 4.0f) << "w must be the fourth float, not the first";
}

// ---------------------------------------------------------------- construction
// Hand-computed: a quarter turn about +Z is (0, 0, sin45, cos45).
TEST(quaternion_construction, axis_angle_matches_the_half_angle_form) {
    const quaternion q =
        math::quaternion_from_axis_angle(vector3{0, 0, 1}, math::half_pi);
    const float r = 0.70710678f;
    EXPECT_NEAR(q.x, 0.0f, epsilon);
    EXPECT_NEAR(q.y, 0.0f, epsilon);
    EXPECT_NEAR(q.z, r, epsilon);
    EXPECT_NEAR(q.w, r, epsilon);
}

// right-handed: about +Z, the +X axis turns toward +Y.
TEST(quaternion_construction, rotation_is_right_handed) {
    const quaternion qz =
        math::quaternion_from_axis_angle(vector3{0, 0, 1}, math::half_pi);
    EXPECT_TRUE(near_vector(math::rotate(vector3{1, 0, 0}, qz), vector3{0, 1, 0}));

    const quaternion qx =
        math::quaternion_from_axis_angle(vector3{1, 0, 0}, math::half_pi);
    EXPECT_TRUE(near_vector(math::rotate(vector3{0, 1, 0}, qx), vector3{0, 0, 1}));

    const quaternion qy =
        math::quaternion_from_axis_angle(vector3{0, 1, 0}, math::half_pi);
    EXPECT_TRUE(near_vector(math::rotate(vector3{0, 0, 1}, qy), vector3{1, 0, 0}));
}

TEST(quaternion_construction, axis_is_normalized_for_you) {
    const quaternion a =
        math::quaternion_from_axis_angle(vector3{0, 0, 1}, 0.9f);
    const quaternion b =
        math::quaternion_from_axis_angle(vector3{0, 0, 17.5f}, 0.9f);
    EXPECT_TRUE(math::near_equal(a, b, epsilon));

    // A zero axis has no rotation to describe; identity beats NaN.
    EXPECT_TRUE(math::quaternion_from_axis_angle(vector3{0, 0, 0}, 1.0f) ==
                quaternion::identity());
}

// Each Euler angle turns about the axis its name claims.
TEST(quaternion_construction, euler_angles_use_the_named_axes) {
    const float t = 0.6f;
    EXPECT_TRUE(math::same_rotation(
        math::quaternion_from_pitch_yaw_roll(t, 0, 0),
        math::quaternion_from_axis_angle(vector3{1, 0, 0}, t), epsilon))
        << "pitch turns about X";
    EXPECT_TRUE(math::same_rotation(
        math::quaternion_from_pitch_yaw_roll(0, t, 0),
        math::quaternion_from_axis_angle(vector3{0, 1, 0}, t), epsilon))
        << "yaw turns about Y";
    EXPECT_TRUE(math::same_rotation(
        math::quaternion_from_pitch_yaw_roll(0, 0, t),
        math::quaternion_from_axis_angle(vector3{0, 0, 1}, t), epsilon))
        << "roll turns about Z";
}

// ------------------------------------------------------------ multiply order
// THE test of this file. `a * b` is "a, then b" -- the reverse of the textbook
// Hamilton product. Rotating +X by a quarter turn about Z lands on +Y; rotating
// that by a quarter turn about X lands on +Z. If the order were flipped the
// answer would be +Y, which is a perfectly plausible vector.
TEST(quaternion_multiply, applies_left_to_right) {
    const quaternion qz =
        math::quaternion_from_axis_angle(vector3{0, 0, 1}, math::half_pi);
    const quaternion qx =
        math::quaternion_from_axis_angle(vector3{1, 0, 0}, math::half_pi);

    const vector3 v{1, 0, 0};
    EXPECT_TRUE(near_vector(math::rotate(v, qz * qx), vector3{0, 0, 1}))
        << "qz * qx must mean qz first";
    EXPECT_TRUE(near_vector(math::rotate(v, qx * qz), vector3{0, 1, 0}))
        << "and the other order must differ";
}

TEST(quaternion_multiply, matches_rotating_twice) {
    random_vectors gen(random_seed + 200);
    for (int n = 0; n < 128; ++n) {
        const quaternion a = random_rotation(gen);
        const quaternion b = random_rotation(gen);
        const sample s = gen.next();
        const vector3 v{s.f[0], s.f[1], s.f[2]};

        EXPECT_TRUE(near_vector(math::rotate(v, a * b),
                               math::rotate(math::rotate(v, a), b), 1e-2f))
            << n;
    }
}

TEST(quaternion_multiply, is_associative_and_has_an_identity) {
    random_vectors gen(random_seed + 201);
    for (int n = 0; n < 64; ++n) {
        const quaternion a = random_rotation(gen);
        const quaternion b = random_rotation(gen);
        const quaternion c = random_rotation(gen);
        EXPECT_TRUE(math::near_equal((a * b) * c, a * (b * c), 1e-4f)) << n;
        EXPECT_TRUE(math::near_equal(a * quaternion::identity(), a, epsilon)) << n;
        EXPECT_TRUE(math::near_equal(quaternion::identity() * a, a, epsilon)) << n;
    }
}

TEST(quaternion_multiply, does_not_commute) {
    const quaternion a =
        math::quaternion_from_axis_angle(vector3{0, 0, 1}, 0.7f);
    const quaternion b =
        math::quaternion_from_axis_angle(vector3{1, 0, 0}, 0.4f);
    EXPECT_FALSE(math::near_equal(a * b, b * a, 1e-3f));
}

// --------------------------------------------------------- matrix agreement
// The reason the product is ordered the way it is: with this convention no
// layer has to reverse anything.
TEST(quaternion_matrix, composition_agrees_with_matrix_composition) {
    random_vectors gen(random_seed + 202);
    for (int n = 0; n < 64; ++n) {
        const quaternion a = random_rotation(gen);
        const quaternion b = random_rotation(gen);
        EXPECT_TRUE(math::near_equal(math::rotation_matrix(a * b),
                                     math::rotation_matrix(a) *
                                         math::rotation_matrix(b),
                                     1e-4f)) << n;
    }
}

TEST(quaternion_matrix, rotating_by_hand_agrees_with_the_matrix) {
    random_vectors gen(random_seed + 203);
    for (int n = 0; n < 64; ++n) {
        const quaternion q = random_rotation(gen);
        const sample s = gen.next();
        const vector3 v{s.f[0], s.f[1], s.f[2]};
        EXPECT_TRUE(near_vector(
            math::rotate(v, q),
            math::transform_direction(v, math::rotation_matrix(q)), 1e-2f)) << n;
    }
}

TEST(quaternion_matrix, rotation_matrix_is_orthonormal) {
    random_vectors gen(random_seed + 204);
    for (int n = 0; n < 64; ++n) {
        const matrix4x4 m = math::rotation_matrix(random_rotation(gen));
        EXPECT_TRUE(math::near_equal(m * transpose(m), matrix4x4::identity(),
                                     1e-4f)) << n;
        EXPECT_NEAR(determinant(m), 1.0f, 1e-4f) << n;
    }
}

// quaternion_from_rotation_matrix picks one of four branches by which diagonal
// term is largest. Three of them only run for rotations near 180 degrees, which
// random small rotations never reach -- so they are named explicitly here. The
// naive single-branch formula fails exactly on these.
TEST(quaternion_matrix, round_trip_covers_every_shepperd_branch) {
    const struct { vector3 axis; float angle; const char* branch; } cases[] = {
        {{0, 0, 1}, 0.3f,          "trace > 0"},
        {{1, 0, 0}, math::pi,    "m00 largest (180 about X)"},
        {{0, 1, 0}, math::pi,    "m11 largest (180 about Y)"},
        {{0, 0, 1}, math::pi,    "m22 largest (180 about Z)"},
        {{1, 1, 0}, math::pi,    "180 about a diagonal"},
        {{1, 2, 3}, 3.0f,          "near 180, off axis"},
    };

    for (const auto& c : cases) {
        const quaternion q = math::quaternion_from_axis_angle(c.axis, c.angle);
        const quaternion back =
            math::quaternion_from_rotation_matrix(math::rotation_matrix(q));
        EXPECT_TRUE(math::same_rotation(q, back, 1e-4f)) << c.branch;

        // The 3x3 overload must agree with the 4x4 one.
        const quaternion back3 =
            math::quaternion_from_rotation_matrix(math::rotation_matrix3x3(q));
        EXPECT_TRUE(math::same_rotation(q, back3, 1e-4f)) << c.branch << " (3x3)";
    }
}

TEST(quaternion_matrix, round_trip_over_random_rotations) {
    random_vectors gen(random_seed + 205);
    for (int n = 0; n < 256; ++n) {
        const sample s = gen.next();
        const vector3 axis{s.f[0], s.f[1], s.f[2]};
        if (math::length_sq(axis) < 1e-6f) continue;
        // Full angular range, so the 180-degree branches get exercised too.
        const quaternion q = math::quaternion_from_axis_angle(axis, s.f[3] * 0.03f);
        const quaternion back =
            math::quaternion_from_rotation_matrix(math::rotation_matrix(q));
        EXPECT_TRUE(math::same_rotation(q, back, 1e-3f)) << n;
    }
}

// ---------------------------------------------------------------- inverses
TEST(quaternion_inverse, undoes_the_rotation) {
    random_vectors gen(random_seed + 206);
    for (int n = 0; n < 64; ++n) {
        const quaternion q = random_rotation(gen);
        const sample s = gen.next();
        const vector3 v{s.f[0], s.f[1], s.f[2]};

        EXPECT_TRUE(near_vector(math::rotate(math::rotate(v, q),
                                             math::inverse(q)), v, 1e-2f)) << n;
        EXPECT_TRUE(near_vector(math::inverse_rotate(math::rotate(v, q), q), v,
                               1e-2f)) << n;
        EXPECT_TRUE(math::near_equal(q * math::inverse(q),
                                     quaternion::identity(), 1e-4f)) << n;
    }
}

// For a unit quaternion the two agree; for one that has drifted they must not,
// and inverse is the one that stays right.
TEST(quaternion_inverse, conjugate_and_inverse_differ_off_the_unit_sphere) {
    const quaternion unit =
        math::quaternion_from_axis_angle(vector3{0, 1, 0}, 0.8f);
    EXPECT_TRUE(math::near_equal(math::conjugate(unit), math::inverse(unit),
                                 epsilon));

    const quaternion scaled = unit * 3.0f;
    EXPECT_FALSE(math::near_equal(math::conjugate(scaled),
                                  math::inverse(scaled), 1e-3f));
    EXPECT_TRUE(math::near_equal(scaled * math::inverse(scaled),
                                 quaternion::identity(), 1e-4f))
        << "inverse must still invert a non-unit quaternion";
}

TEST(quaternion_normalize, degenerate_input_gives_identity_not_na_n) {
    EXPECT_TRUE(math::normalize(quaternion{0, 0, 0, 0}) ==
                quaternion::identity());
    EXPECT_TRUE(math::inverse(quaternion{0, 0, 0, 0}) ==
                quaternion::identity());

    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(math::normalize(quaternion{inf, 0, 0, 0}) ==
                quaternion::identity());
    EXPECT_TRUE(math::normalize(quaternion{quiet_nan(), 0, 0, 1}) ==
                quaternion::identity());

    EXPECT_NEAR(math::length(math::normalize(quaternion{1, 2, 3, 4})), 1.0f,
                epsilon);
}

// ------------------------------------------------------------ interpolation
TEST(quaternion_slerp, hits_both_endpoints) {
    random_vectors gen(random_seed + 207);
    for (int n = 0; n < 32; ++n) {
        const quaternion a = random_rotation(gen);
        const quaternion b = random_rotation(gen);
        EXPECT_TRUE(math::same_rotation(math::slerp(a, b, 0.0f), a, 1e-4f)) << n;
        EXPECT_TRUE(math::same_rotation(math::slerp(a, b, 1.0f), b, 1e-4f)) << n;
        EXPECT_TRUE(math::same_rotation(math::nlerp(a, b, 0.0f), a, 1e-4f)) << n;
        EXPECT_TRUE(math::same_rotation(math::nlerp(a, b, 1.0f), b, 1e-4f)) << n;
    }
}

// The property that separates slerp from nlerp: equal steps in t are equal
// steps in angle.
TEST(quaternion_slerp, moves_at_constant_angular_speed) {
    const quaternion a = quaternion::identity();
    const quaternion b =
        math::quaternion_from_axis_angle(vector3{0, 0, 1}, 2.0f);

    float previous = 0.0f;
    for (int i = 1; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        const quaternion q = math::slerp(a, b, t);
        vector3 axis; float angle = 0.0f;
        math::to_axis_angle(q, axis, angle);
        EXPECT_NEAR(angle, 2.0f * t, 1e-3f) << "t = " << t;
        EXPECT_GT(angle, previous);
        previous = angle;
    }
}

// q and -q are the same rotation, so interpolating toward the far
// representation must not take the long way round.
TEST(quaternion_slerp, takes_the_short_arc) {
    const quaternion a = quaternion::identity();
    const quaternion b =
        math::quaternion_from_axis_angle(vector3{0, 0, 1}, math::half_pi);

    EXPECT_TRUE(math::same_rotation(math::slerp(a, b, 0.5f),
                                    math::slerp(a, -b, 0.5f), 1e-4f));
    EXPECT_TRUE(math::same_rotation(math::nlerp(a, b, 0.5f),
                                    math::nlerp(a, -b, 0.5f), 1e-4f));

    // Halfway between identity and a quarter turn is an eighth turn either way.
    vector3 axis; float angle = 0.0f;
    math::to_axis_angle(math::slerp(a, -b, 0.5f), axis, angle);
    EXPECT_NEAR(angle, math::quarter_pi, 1e-3f);
}

TEST(quaternion_slerp, stays_normalized) {
    random_vectors gen(random_seed + 208);
    for (int n = 0; n < 64; ++n) {
        const quaternion a = random_rotation(gen);
        const quaternion b = random_rotation(gen);
        for (float t = 0.0f; t <= 1.0f; t += 0.125f) {
            EXPECT_NEAR(math::length(math::slerp(a, b, t)), 1.0f, 1e-4f) << n;
            EXPECT_NEAR(math::length(math::nlerp(a, b, t)), 1.0f, 1e-4f) << n;
        }
    }
}

// Where the sine of the half-angle divides to zero. The nlerp fallback has to
// hand over without a visible seam.
TEST(quaternion_slerp, handles_nearly_identical_endpoints) {
    const quaternion a =
        math::quaternion_from_axis_angle(vector3{0, 1, 0}, 0.5f);
    for (float delta : {0.0f, 1e-7f, 1e-5f, 1e-3f}) {
        const quaternion b =
            math::quaternion_from_axis_angle(vector3{0, 1, 0}, 0.5f + delta);
        const quaternion mid = math::slerp(a, b, 0.5f);
        EXPECT_FALSE(std::isnan(mid.w)) << delta;
        EXPECT_NEAR(math::length(mid), 1.0f, 1e-4f) << delta;
        EXPECT_TRUE(math::same_rotation(mid, a, 1e-2f)) << delta;
    }
    // Identical endpoints must come back unchanged, not as a division by zero.
    EXPECT_TRUE(math::same_rotation(math::slerp(a, a, 0.5f), a, 1e-5f));
}

// ------------------------------------------------------------ decomposition
TEST(quaternion_decompose, axis_angle_round_trips) {
    random_vectors gen(random_seed + 209);
    for (int n = 0; n < 128; ++n) {
        const sample s = gen.next();
        const vector3 axis{s.f[0], s.f[1], s.f[2]};
        if (math::length_sq(axis) < 1e-4f) continue;
        const quaternion q = math::quaternion_from_axis_angle(axis, s.f[3] * 0.03f);

        vector3 out_axis; float out_angle = 0.0f;
        math::to_axis_angle(q, out_axis, out_angle);
        EXPECT_TRUE(math::same_rotation(
            q, math::quaternion_from_axis_angle(out_axis, out_angle), 1e-3f)) << n;
        EXPECT_NEAR(math::length(out_axis), 1.0f, 1e-4f) << n;
    }
}

// A tiny but non-zero angle: v_length is ~5e-7, so 1/v_length is enormous and any
// sloppiness in the normalize-and-scale shows up as a non-unit axis. The random
// sweeps essentially never land here.
TEST(quaternion_decompose, tiny_angle_keeps_a_unit_axis) {
    for (float angle : {1e-6f, 1e-5f, 1e-4f}) {
        const quaternion q =
            math::quaternion_from_axis_angle(vector3{0, 1, 0}, angle);
        vector3 axis; float out_angle = 0.0f;
        math::to_axis_angle(q, axis, out_angle);
        EXPECT_NEAR(math::length(axis), 1.0f, 1e-4f) << angle;
        // Relative, not absolute: the angle itself is the small quantity here.
        EXPECT_NEAR(out_angle, angle, angle * 1e-2f + 1e-9f) << angle;
        EXPECT_TRUE(near_vector(axis, vector3{0, 1, 0}, 1e-3f)) << angle;
    }
}

// The identity has no axis. Reporting +X with a zero angle beats dividing by
// the zero-length vector part.
TEST(quaternion_decompose, identity_has_a_defined_axis) {
    vector3 axis; float angle = 1.0f;
    math::to_axis_angle(quaternion::identity(), axis, angle);
    EXPECT_TRUE(near_vector(axis, vector3{1, 0, 0}));
    EXPECT_NEAR(angle, 0.0f, 1e-6f);
}

TEST(quaternion_decompose, euler_round_trips) {
    random_vectors gen(random_seed + 210);
    for (int n = 0; n < 256; ++n) {
        const sample s = gen.next();
        const vector3 axis{s.f[0], s.f[1], s.f[2]};
        if (math::length_sq(axis) < 1e-4f) continue;
        const quaternion q = math::quaternion_from_axis_angle(axis, s.f[3] * 0.03f);

        const vector3 e = math::to_euler(q);
        EXPECT_TRUE(math::same_rotation(
            q, math::quaternion_from_euler(e), 1e-3f)) << n;
    }
}

// Pitch at +/-90 degrees. Yaw and roll then act on the same axis and cannot be
// told apart -- that is a property of Euler angles, not a defect. What must
// still hold is that the decomposition names the SAME rotation.
TEST(quaternion_decompose, gimbal_lock_still_round_trips_the_rotation) {
    for (float pitch : {math::half_pi, -math::half_pi}) {
        for (float yaw : {0.0f, 0.6f, -1.3f, 2.9f}) {
            for (float roll : {0.0f, 0.4f, -0.9f}) {
                const quaternion q =
                    math::quaternion_from_pitch_yaw_roll(pitch, yaw, roll);
                const vector3 e = math::to_euler(q);

                EXPECT_NEAR(std::abs(e.x), math::half_pi, 1e-3f)
                    << "pitch must come back at the pole";
                EXPECT_NEAR(e.z, 0.0f, 1e-4f)
                    << "the convention puts everything in yaw, roll at zero";
                EXPECT_TRUE(math::same_rotation(
                    q, math::quaternion_from_euler(e), 1e-3f))
                    << "pitch " << pitch << " yaw " << yaw << " roll " << roll;
            }
        }
    }
}

// Just off the pole, where the general branch runs but is poorly conditioned.
TEST(quaternion_decompose, near_gimbal_lock_round_trips) {
    for (float delta : {1e-3f, 1e-4f, 1e-5f}) {
        for (float sign : {1.0f, -1.0f}) {
            const float pitch = sign * (math::half_pi - delta);
            const quaternion q =
                math::quaternion_from_pitch_yaw_roll(pitch, 0.7f, -0.5f);
            const vector3 e = math::to_euler(q);
            EXPECT_TRUE(math::same_rotation(
                q, math::quaternion_from_euler(e), 1e-2f)) << delta << " " << sign;
        }
    }
}

// ------------------------------------------------------------------ comparison
TEST(quaternion_compare, near_equal_rejects_na_n) {
    quaternion with_nan = quaternion::identity();
    with_nan.y = quiet_nan();
    EXPECT_FALSE(math::near_equal(with_nan, quaternion::identity()));
    EXPECT_FALSE(math::near_equal(with_nan, with_nan))
        << "a NaN is near nothing, not even itself";
}

// q and -q are the same rotation but different quaternions, and the two
// questions get different functions.
TEST(quaternion_compare, same_rotation_sees_through_negation) {
    const quaternion q =
        math::quaternion_from_axis_angle(vector3{1, 2, 3}, 1.1f);
    EXPECT_FALSE(math::near_equal(q, -q, 1e-3f));
    EXPECT_TRUE(math::same_rotation(q, -q));
    EXPECT_TRUE(math::same_rotation(q, q));

    // bit_and they really do rotate identically.
    const vector3 v{0.3f, -0.7f, 1.1f};
    EXPECT_TRUE(near_vector(math::rotate(v, q), math::rotate(v, -q)));
}

// ------------------------------------------------------------------- constexpr
// The differentiator against DirectXMath: none of this needs a runtime.
constexpr quaternion compile_time_z =
    math::quaternion_from_axis_angle(vector3{0, 0, 1}, math::half_pi);
static_assert(compile_time_z.z > 0.7070f && compile_time_z.z < 0.7072f);
static_assert(compile_time_z.w > 0.7070f && compile_time_z.w < 0.7072f);

constexpr quaternion compile_time_product = compile_time_z * compile_time_z;
static_assert(compile_time_product.z > 0.9999f);   // a half turn about Z

static_assert(math::conjugate(compile_time_z).z < 0.0f);
static_assert(math::dot(quaternion::identity(), quaternion::identity()) == 1.0f);
static_assert(math::length_sq(quaternion::identity()) == 1.0f);

constexpr matrix4x4 compile_time_matrix = math::rotation_matrix(compile_time_z);
static_assert(compile_time_matrix.m[0][1] > 0.999f);   // +X row maps to +Y

constexpr vector3 compile_time_rotated =
    math::rotate(vector3{1, 0, 0}, compile_time_z);
static_assert(compile_time_rotated.y > 0.999f);

// slerp, nlerp and the decompositions carry constexpr; nothing proved they
// survive constant evaluation until a user tried. Out-parameter functions get
// constexpr wrapper functions, since a static_assert cannot bind an lvalue.
constexpr quaternion compile_time_slerp =
    math::slerp(quaternion::identity(), compile_time_z, 0.5f);
static_assert(compile_time_slerp.w > 0.9f);   // an eighth turn about Z
static_assert(compile_time_slerp.z > 0.38f && compile_time_slerp.z < 0.39f);

constexpr quaternion compile_time_nlerp =
    math::nlerp(quaternion::identity(), compile_time_z, 0.25f);
static_assert(compile_time_nlerp.w > 0.9f);

constexpr vector3 compile_time_euler = math::to_euler(compile_time_z);
static_assert(compile_time_euler.z > 1.57f && compile_time_euler.z < 1.58f);
static_assert(compile_time_euler.x < 1e-5f && compile_time_euler.x > -1e-5f);

namespace {
constexpr float compile_time_axis_angle() {
    vector3 axis; float angle = 0.0f;
    math::to_axis_angle(compile_time_z, axis, angle);
    return angle + axis.z;   // both halves observable in one value
}
} // namespace
static_assert(compile_time_axis_angle() > 2.57f);   // pi/2 + 1.0

// The whole pipeline in one constant expression.
constexpr quaternion round_tripped =
    math::quaternion_from_rotation_matrix(math::rotation_matrix(compile_time_z));
static_assert(round_tripped.z > 0.7070f && round_tripped.z < 0.7072f);

// Compile time and run time run the same code, so they agree to within the one
// rounding a compiler is allowed to move: Clang fuses the multiply-adds inside
// the trigonometric polynomials at run time and constant evaluation never
// fuses. Measured in ULP rather than with an epsilon, because a few ULP is the
// real bound and an epsilon would hide a genuine divergence at small magnitudes.
TEST(quaternion_constexpr, compile_time_matches_runtime_to_within_a_few_ulp) {
    const quaternion runtime = math::quaternion_from_axis_angle(
        vector3{opaque(0.0f), opaque(0.0f), opaque(1.0f)},
        opaque(math::half_pi));
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(same_to_within(runtime[i], compile_time_z[i])) << "lane " << i;
    }

    const quaternion runtime_product = runtime * runtime;
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(same_to_within(runtime_product[i], compile_time_product[i])) << "lane " << i;
    }

    const matrix4x4 runtime_matrix = math::rotation_matrix(runtime);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            EXPECT_TRUE(same_to_within(runtime_matrix.m[i][j], compile_time_matrix.m[i][j])) << i << "," << j;
        }
    }
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHEMATICS_TEST_HAS_DXMATH
namespace {

DirectX::XMVECTOR to_xm(const quaternion& q) {
    return DirectX::XMVectorSet(q.x, q.y, q.z, q.w);
}

bool matches_xm(const quaternion& mine, DirectX::FXMVECTOR theirs, float eps) {
    DirectX::XMFLOAT4 f{};
    DirectX::XMStoreFloat4(&f, theirs);
    return math::near_equal(mine, quaternion{f.x, f.y, f.z, f.w}, eps);
}

} // namespace

TEST(quaternion_dx_parity, construction_matches_direct_x_math) {
    random_vectors gen(random_seed + 220);
    for (int n = 0; n < 128; ++n) {
        const sample s = gen.next();
        const vector3 axis{s.f[0], s.f[1], s.f[2]};
        if (math::length_sq(axis) < 1e-4f) continue;
        const float angle = s.f[3] * 0.03f;

        EXPECT_TRUE(matches_xm(
            math::quaternion_from_axis_angle(axis, angle),
            DirectX::XMQuaternionRotationAxis(
                DirectX::XMVectorSet(axis.x, axis.y, axis.z, 0.0f), angle),
            1e-5f)) << n;

        const float p = s.f[0] * 0.03f, y = s.f[1] * 0.03f, r = s.f[2] * 0.03f;
        EXPECT_TRUE(matches_xm(
            math::quaternion_from_pitch_yaw_roll(p, y, r),
            DirectX::XMQuaternionRotationRollPitchYaw(p, y, r), 1e-5f)) << n;
    }
}

// The convention check that matters most. XMQuaternionMultiply(a, b) is
// documented as "a followed by b", and Mathematics has to mean the same thing.
TEST(quaternion_dx_parity, multiply_order_matches_direct_x_math) {
    random_vectors gen(random_seed + 221);
    for (int n = 0; n < 128; ++n) {
        const quaternion a = random_rotation(gen);
        const quaternion b = random_rotation(gen);
        EXPECT_TRUE(matches_xm(
            a * b, DirectX::XMQuaternionMultiply(to_xm(a), to_xm(b)), 1e-5f)) << n;
    }
}

TEST(quaternion_dx_parity, matrix_and_slerp_match_direct_x_math) {
    random_vectors gen(random_seed + 222);
    for (int n = 0; n < 64; ++n) {
        const quaternion a = random_rotation(gen);
        const quaternion b = random_rotation(gen);

        DirectX::XMFLOAT4X4 theirs{};
        DirectX::XMStoreFloat4x4(
            &theirs, DirectX::XMMatrixRotationQuaternion(to_xm(a)));
        const matrix4x4 mine = math::rotation_matrix(a);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                EXPECT_NEAR(mine.m[i][j], theirs.m[i][j], 1e-5f) << n;
            }
        }

        const float t = 0.25f + 0.5f * static_cast<float>(n % 3);
        EXPECT_TRUE(matches_xm(
            math::slerp(a, b, t),
            DirectX::XMQuaternionSlerp(to_xm(a), to_xm(b), t), 1e-4f)) << n;

        EXPECT_TRUE(matches_xm(
            math::inverse(a), DirectX::XMQuaternionInverse(to_xm(a)), 1e-5f)) << n;
    }
}
#endif // MATHEMATICS_TEST_HAS_DXMATH
