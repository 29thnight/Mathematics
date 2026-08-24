// Geometric vector operations, checked against hand-computed values and against
// DirectXMath. The degenerate cases matter most here: a normalize that returns
// NaN instead of zero propagates through a whole scene graph before anyone sees
// it.

#include "support/reg_testing.hpp"

#include <mathematics/vector.hpp>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHEMATICS_TEST_HAS_DXMATH 1
#else
#  define MATHEMATICS_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace math_test;
using math::vector2;
using math::vector3;
using math::vector4;

vector3 random_vector3(random_vectors& gen) {
    const sample s = gen.next();
    return vector3{s.f[0], s.f[1], s.f[2]};
}

} // namespace

// ---------------------------------------------------------- dot, length
static_assert(math::dot(vector3{1, 2, 3}, vector3{1, 1, 1}) == 6.0f);
static_assert(math::length_sq(vector3{0, 3, 4}) == 25.0f);
static_assert(math::length(vector3{0, 3, 4}) == 5.0f);
static_assert(math::distance(vector3{1, 0, 0}, vector3{4, 4, 0}) == 5.0f);

TEST(vector_geometry, length_and_distance) {
    EXPECT_FLOAT_EQ(math::length(vector3(3, 4, 0)), 5.0f);
    EXPECT_FLOAT_EQ(math::length_sq(vector3(3, 4, 0)), 25.0f);
    EXPECT_FLOAT_EQ(math::distance(vector3(1, 1, 1), vector3(4, 5, 1)), 5.0f);
    EXPECT_FLOAT_EQ(math::distance_sq(vector3(1, 1, 1), vector3(4, 5, 1)), 25.0f);
}

// ------------------------------------------------------------------ normalize
TEST(vector_geometry, normalize_produces_unit_length) {
    random_vectors gen(random_seed + 70);
    for (int n = 0; n < sample_count; ++n) {
        const vector3 v = random_vector3(gen);
        if (math::length_sq(v) < 1e-6f) continue;
        EXPECT_NEAR(math::length(math::normalize(v)), 1.0f, 1e-5f) << n;
    }
}

TEST(vector_geometry, normalize_preserves_direction) {
    const vector3 v{3, 4, 0};
    const vector3 n = math::normalize(v);
    EXPECT_TRUE(math::near_equal(n, vector3(0.6f, 0.8f, 0.0f)));
}

// The whole reason normalize costs two selects rather than a bare divide.
TEST(vector_geometry, normalize_of_zero_is_zero_not_na_n) {
    const vector3 n = math::normalize(vector3::zero());
    EXPECT_FALSE(std::isnan(n.x)) << "a zero vector must not normalize to NaN";
    EXPECT_TRUE(n == vector3::zero());
}

TEST(vector_geometry, normalize_of_infinite_is_na_n) {
    const float inf = std::numeric_limits<float>::infinity();
    const vector3 n = math::normalize(vector3(opaque(inf), 0.0f, 0.0f));
    EXPECT_TRUE(std::isnan(n.x));
}

// The approximate form trades those guards for speed, which is the point of it.
TEST(vector_geometry, normalize_est_is_close_but_not_exact) {
    random_vectors gen(random_seed + 71);
    for (int n = 0; n < sample_count; ++n) {
        const vector3 v = random_vector3(gen);
        if (math::length_sq(v) < 1.0f) continue;
        EXPECT_NEAR(math::length(math::normalize_est(v)), 1.0f, 4e-3f) << n;
    }
}

// ---------------------------------------------------------------------- cross
static_assert(math::cross(vector3::unit_x(), vector3::unit_y()).z == 1.0f);
static_assert(math::cross(vector2{1, 0}, vector2{0, 1}) == 1.0f);

// right-handed, matching DirectXMath. Getting the handedness backwards flips
// every surface normal in a renderer, so all three basis pairs are pinned.
TEST(vector_geometry, cross_is_right_handed) {
    EXPECT_TRUE(math::cross(vector3::unit_x(), vector3::unit_y()) ==
                vector3::unit_z());
    EXPECT_TRUE(math::cross(vector3::unit_y(), vector3::unit_z()) ==
                vector3::unit_x());
    EXPECT_TRUE(math::cross(vector3::unit_z(), vector3::unit_x()) ==
                vector3::unit_y());
}

TEST(vector_geometry, cross_is_anti_commutative_and_orthogonal) {
    random_vectors gen(random_seed + 72);
    for (int n = 0; n < sample_count; ++n) {
        const vector3 a = random_vector3(gen);
        const vector3 b = random_vector3(gen);
        const vector3 c = math::cross(a, b);

        EXPECT_TRUE(math::near_equal(math::cross(b, a), -c, 1e-2f)) << n;

        // Orthogonality is checked relative to the magnitudes involved: with
        // components up to 100 the cross reaches 1e4, and a dot of two such
        // values cancels to a residue proportional to that, not to zero.
        const float scale = math::length(a) * math::length(b) *
                            std::max(math::length(a), math::length(b));
        EXPECT_NEAR(math::dot(a, c), 0.0f, scale * 1e-5f) << n;
        EXPECT_NEAR(math::dot(b, c), 0.0f, scale * 1e-5f) << n;
    }
}

TEST(vector_geometry, cross2_d_is_the_signed_area) {
    EXPECT_FLOAT_EQ(math::cross(vector2(1, 0), vector2(0, 1)), 1.0f);
    EXPECT_FLOAT_EQ(math::cross(vector2(0, 1), vector2(1, 0)), -1.0f);
    EXPECT_FLOAT_EQ(math::cross(vector2(2, 0), vector2(0, 3)), 6.0f);
    EXPECT_FLOAT_EQ(math::cross(vector2(1, 1), vector2(2, 2)), 0.0f);
}

TEST(vector_geometry, perpendicular_turns_counter_clockwise) {
    EXPECT_TRUE(math::perpendicular(vector2(1, 0)) == vector2(0, 1));
    EXPECT_TRUE(math::perpendicular(vector2(0, 1)) == vector2(-1, 0));
    EXPECT_FLOAT_EQ(math::dot(vector2(3, 4), math::perpendicular(vector2(3, 4))),
                    0.0f);
}

// -------------------------------------------------------------------- reflect
TEST(vector_geometry, reflect_mirrors_about_the_surface) {
    // Straight down onto a floor comes straight back up.
    const vector3 r = math::reflect(vector3(0, -1, 0), vector3::unit_y());
    EXPECT_TRUE(math::near_equal(r, vector3(0, 1, 0)));

    // A 45-degree incidence leaves at 45 degrees.
    const vector3 diagonal = math::normalize(vector3(1, -1, 0));
    const vector3 bounced = math::reflect(diagonal, vector3::unit_y());
    EXPECT_TRUE(math::near_equal(bounced, math::normalize(vector3(1, 1, 0))));
}

TEST(vector_geometry, reflect_preserves_length) {
    random_vectors gen(random_seed + 73);
    for (int n = 0; n < sample_count; ++n) {
        const vector3 incident = random_vector3(gen);
        const vector3 normal = math::normalize(random_vector3(gen));
        if (std::isnan(normal.x)) continue;
        EXPECT_NEAR(math::length(math::reflect(incident, normal)),
                    math::length(incident),
                    math::length(incident) * 1e-4f) << n;
    }
}

// -------------------------------------------------------------------- refract
TEST(vector_geometry, refract_bends_toward_the_normal_entering_denser_medium) {
    const vector3 incident = math::normalize(vector3(1, -1, 0));
    const vector3 normal = vector3::unit_y();
    const vector3 out = math::refract(incident, normal, 1.0f / 1.5f);

    EXPECT_FALSE(out == vector3::zero()) << "should not be total internal reflection";
    EXPECT_NEAR(math::length(out), 1.0f, 1e-4f);
    // Bending toward the normal means a steeper descent: |y| grows.
    EXPECT_GT(std::abs(out.y), std::abs(incident.y));
}

TEST(vector_geometry, refract_returns_zero_on_total_internal_reflection) {
    // Leaving glass for air at a shallow angle: past the critical angle.
    const vector3 incident = math::normalize(vector3(1, -0.05f, 0));
    const vector3 out = math::refract(incident, vector3::unit_y(), 1.5f);
    EXPECT_TRUE(out == vector3::zero());
}

// ------------------------------------------- every width, not just vector3
// vector2 and vector3 take the scalar path; vector4 takes the branchless
// select-based one. They are separate implementations of the same contract, so
// testing only vector3 -- as the first version of this file did -- left
// normalize_wide entirely unexercised.
static_assert(math::normalize(vector2{3, 4}).x == 0.6f);
static_assert(math::normalize(vector4{0, 0, 3, 4}).w == 0.8f);
static_assert(math::saturate(vector4{-1, 5, 0.5f, 2}).y == 1.0f);
static_assert(math::clamp(vector2{-1, 5}, vector2::zero(), vector2::one()).x == 0.0f);
static_assert(math::reflect(vector4{0, -1, 0, 0}, vector4::unit_y()).y == 1.0f);

TEST(vector_all_widths, normalize_produces_unit_length) {
    EXPECT_TRUE(math::near_equal(math::normalize(vector2(3, 4)),
                                 vector2(0.6f, 0.8f)));
    EXPECT_NEAR(math::length(math::normalize(vector4(1, 2, 3, 4))), 1.0f, 1e-5f);
    EXPECT_NEAR(math::length(math::normalize(vector2(-7, 11))), 1.0f, 1e-5f);
}

// The degenerate contract has to hold on the SIMD path too, not just the scalar
// one -- vector4 reaches it through completely different code.
TEST(vector_all_widths, normalize_degenerate_cases_hold_at_every_width) {
    EXPECT_TRUE(math::normalize(vector2::zero()) == vector2::zero());
    EXPECT_TRUE(math::normalize(vector3::zero()) == vector3::zero());
    EXPECT_TRUE(math::normalize(vector4::zero()) == vector4::zero());

    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(std::isnan(math::normalize(vector2(opaque(inf), 0.0f)).x));
    EXPECT_TRUE(std::isnan(math::normalize(vector4(opaque(inf), 0, 0, 0)).x));
}

TEST(vector_all_widths, normalize_est_at_every_width) {
    EXPECT_NEAR(math::length(math::normalize_est(vector2(3, 4))), 1.0f, 4e-3f);
    EXPECT_NEAR(math::length(math::normalize_est(vector4(1, 2, 3, 4))), 1.0f, 4e-3f);
}

TEST(vector_all_widths, lane_wise_math_at_every_width) {
    EXPECT_TRUE(math::abs(vector2(-1, 2)) == vector2(1, 2));
    EXPECT_TRUE(math::abs(vector4(-1, 2, -3, 4)) == vector4(1, 2, 3, 4));

    EXPECT_TRUE(math::min(vector2(1, 5), vector2(4, 2)) == vector2(1, 2));
    EXPECT_TRUE(math::max(vector4(1, 5, 3, 7), vector4(4, 2, 6, 0)) ==
                vector4(4, 5, 6, 7));

    EXPECT_TRUE(math::saturate(vector4(-1, 5, 0.5f, 2)) == vector4(0, 1, 0.5f, 1));
    EXPECT_TRUE(math::clamp(vector2(-1, 5), vector2::zero(), vector2::one()) ==
                vector2(0, 1));

    EXPECT_TRUE(math::lerp(vector2(0, 0), vector2(10, 20), 0.5f) == vector2(5, 10));
    EXPECT_TRUE(math::lerp(vector4(0, 0, 0, 0), vector4(10, 20, 30, 40), 0.5f) ==
                vector4(5, 10, 15, 20));
}

TEST(vector_all_widths, reflect_and_refract_at_every_width) {
    EXPECT_TRUE(math::near_equal(math::reflect(vector2(0, -1), vector2::unit_y()),
                                 vector2(0, 1)));
    EXPECT_TRUE(math::near_equal(
        math::reflect(vector4(0, -1, 0, 0), vector4::unit_y()),
        vector4(0, 1, 0, 0)));

    // Total internal reflection returns zero at every width.
    const vector2 shallow = math::normalize(vector2(1, -0.05f));
    EXPECT_TRUE(math::refract(shallow, vector2::unit_y(), 1.5f) == vector2::zero());
}

TEST(vector_all_widths, near_equal_uses_only_its_own_lanes) {
    EXPECT_TRUE(math::near_equal(vector2(1, 2), vector2(1, 2.000001f)));
    EXPECT_FALSE(math::near_equal(vector2(1, 2), vector2(1, 2.1f)));
    EXPECT_TRUE(math::near_equal(vector4(1, 2, 3, 4), vector4(1, 2, 3, 4.000001f)));
    EXPECT_FALSE(math::near_equal(vector4(1, 2, 3, 4), vector4(1, 2, 3, 4.1f)));
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHEMATICS_TEST_HAS_DXMATH
namespace {

DirectX::XMVECTOR to_xm(vector3 v) {
    return DirectX::XMVectorSet(v.x, v.y, v.z, 0.0f);
}

vector3 from_xm3(DirectX::FXMVECTOR v) {
    DirectX::XMFLOAT3 out{};
    DirectX::XMStoreFloat3(&out, v);
    return vector3{out.x, out.y, out.z};
}

float first_lane(DirectX::FXMVECTOR v) { return DirectX::XMVectorGetX(v); }

} // namespace

TEST(vector_dx_parity, dot_length_and_cross_match_direct_x_math) {
    random_vectors gen(random_seed + 80);
    for (int n = 0; n < sample_count; ++n) {
        const vector3 a = random_vector3(gen);
        const vector3 b = random_vector3(gen);
        const DirectX::XMVECTOR xa = to_xm(a);
        const DirectX::XMVECTOR xb = to_xm(b);

        const float terms = std::abs(a.x * b.x) + std::abs(a.y * b.y) +
                            std::abs(a.z * b.z);
        EXPECT_NEAR(math::dot(a, b), first_lane(DirectX::XMVector3Dot(xa, xb)),
                    std::max(1e-6f, terms * 2e-6f)) << n;

        EXPECT_NEAR(math::length(a), first_lane(DirectX::XMVector3Length(xa)),
                    math::length(a) * 1e-5f) << n;

        EXPECT_TRUE(math::near_equal(math::cross(a, b),
                                     from_xm3(DirectX::XMVector3Cross(xa, xb)),
                                     1e-2f)) << n;
    }
}

TEST(vector_dx_parity, normalize_matches_direct_x_math) {
    random_vectors gen(random_seed + 81);
    for (int n = 0; n < sample_count; ++n) {
        const vector3 a = random_vector3(gen);
        EXPECT_TRUE(math::near_equal(
            math::normalize(a),
            from_xm3(DirectX::XMVector3Normalize(to_xm(a))), 1e-5f)) << n;
    }
}

// The degenerate cases are the ones worth matching exactly, because they are
// the ones a caller will not have thought about.
TEST(vector_dx_parity, normalize_degenerate_cases_match_direct_x_math) {
    const vector3 zero_result = math::normalize(vector3::zero());
    const vector3 dx_zero_result =
        from_xm3(DirectX::XMVector3Normalize(to_xm(vector3::zero())));
    EXPECT_TRUE(zero_result == dx_zero_result)
        << "zero-length normalize must agree with DirectXMath";

    const float inf = std::numeric_limits<float>::infinity();
    const vector3 inf_input{opaque(inf), 0.0f, 0.0f};
    const vector3 inf_result = math::normalize(inf_input);
    const vector3 dx_inf_result =
        from_xm3(DirectX::XMVector3Normalize(to_xm(inf_input)));
    EXPECT_EQ(std::isnan(inf_result.x), std::isnan(dx_inf_result.x))
        << "infinite-length normalize must agree with DirectXMath";
}

TEST(vector_dx_parity, reflect_and_refract_match_direct_x_math) {
    random_vectors gen(random_seed + 82);
    for (int n = 0; n < sample_count; ++n) {
        const vector3 incident = math::normalize(random_vector3(gen));
        const vector3 normal = math::normalize(random_vector3(gen));
        if (std::isnan(incident.x) || std::isnan(normal.x)) continue;

        EXPECT_TRUE(math::near_equal(
            math::reflect(incident, normal),
            from_xm3(DirectX::XMVector3Reflect(to_xm(incident), to_xm(normal))),
            1e-4f)) << n;

        constexpr float eta_value = 1.0f / 1.33f;
        EXPECT_TRUE(math::near_equal(
            math::refract(incident, normal, eta_value),
            from_xm3(DirectX::XMVector3Refract(to_xm(incident), to_xm(normal),
                                              eta_value)),
            1e-4f)) << n;
    }
}

TEST(vector_dx_parity, lane_wise_math_matches_direct_x_math) {
    random_vectors gen(random_seed + 83);
    for (int n = 0; n < sample_count; ++n) {
        const vector3 a = random_vector3(gen);
        const vector3 b = random_vector3(gen);

        EXPECT_TRUE(math::near_equal(
            math::lerp(a, b, 0.25f),
            from_xm3(DirectX::XMVectorLerp(to_xm(a), to_xm(b), 0.25f)), 1e-3f)) << n;
        EXPECT_TRUE(math::near_equal(
            math::min(a, b),
            from_xm3(DirectX::XMVectorMin(to_xm(a), to_xm(b))))) << n;
        EXPECT_TRUE(math::near_equal(
            math::max(a, b),
            from_xm3(DirectX::XMVectorMax(to_xm(a), to_xm(b))))) << n;
        EXPECT_TRUE(math::near_equal(
            math::saturate(a),
            from_xm3(DirectX::XMVectorSaturate(to_xm(a))))) << n;
    }
}
#endif // MATHEMATICS_TEST_HAS_DXMATH
