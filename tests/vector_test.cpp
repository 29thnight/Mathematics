// vector2/3/4 storage, construction, operators, and lane-wise math.
// Geometry and DirectXMath parity live in vector_geometry_test.cpp.

#include "support/reg_testing.hpp"

#include <mathematics/vector.hpp>

#include <type_traits>
#include <vector>

namespace {

using namespace math_test;
using math::vector2;
using math::vector3;
using math::vector4;

} // namespace

// ---------------------------------------------------------------------- layout
// The packing is a promise to callers: a vector3 array has to be a position
// stream a GPU can read with no gaps, and the types have to sit in structs
// without imposing alignment. A silent change here breaks binary interop rather
// than any test's arithmetic, so it is asserted directly.
static_assert(sizeof(vector2) == 8);
static_assert(sizeof(vector3) == 12);
static_assert(sizeof(vector4) == 16);
static_assert(alignof(vector3) == alignof(float));

TEST(vector_layout, arrays_are_contiguous_with_no_padding) {
    const std::vector<vector3> stream = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    const float* raw = &stream[0].x;
    for (int i = 0; i < 9; ++i) {
        EXPECT_FLOAT_EQ(raw[i], static_cast<float>(i + 1)) << "element " << i;
    }
}

TEST(vector_layout, indexing_reaches_the_named_members) {
    vector4 v{1, 2, 3, 4};
    EXPECT_FLOAT_EQ(v[0], v.x);
    EXPECT_FLOAT_EQ(v[3], v.w);
    v[2] = 30.0f;
    EXPECT_FLOAT_EQ(v.z, 30.0f);
}

// ---------------------------------------------------------------- construction
static_assert(vector3{}.x == 0.0f && vector3{}.y == 0.0f && vector3{}.z == 0.0f);
static_assert(vector4{}.w == 0.0f);
static_assert(vector3{2.0f}.y == 2.0f);
static_assert(vector3::unit_y().y == 1.0f && vector3::unit_y().x == 0.0f);
static_assert(vector4::one().w == 1.0f);

TEST(vector_construction, default_is_zero) {
    // Deliberately different from DirectXMath and GLM, which leave the lanes
    // indeterminate. Cheap, and it removes a class of uninitialized-read bugs.
    EXPECT_TRUE(vector3{} == vector3::zero());
    EXPECT_TRUE(vector4{} == vector4::zero());
    EXPECT_TRUE(vector2{} == vector2::zero());
}

TEST(vector_construction, splat_fills_every_component) {
    const vector4 v{3.5f};
    EXPECT_FLOAT_EQ(v.x, 3.5f);
    EXPECT_FLOAT_EQ(v.y, 3.5f);
    EXPECT_FLOAT_EQ(v.z, 3.5f);
    EXPECT_FLOAT_EQ(v.w, 3.5f);
}

// ------------------------------------------------------------ register round trip
// Everything else assumes reg() and from_reg() preserve the components exactly,
// so that is checked on its own -- a failure here would otherwise surface as an
// arithmetic bug somewhere unrelated.
static_assert(vector4::from_reg(vector4{1, 2, 3, 4}.reg()).z == 3.0f);
static_assert(vector3::from_reg(vector3{1, 2, 3}.reg()).z == 3.0f);
static_assert(vector2::from_reg(vector2{1, 2}.reg()).y == 2.0f);

TEST(vector_register, round_trips_exactly) {
    random_vectors gen(random_seed + 60);
    for (int n = 0; n < sample_count; ++n) {
        const sample s = gen.next();
        const vector4 v4{s.f[0], s.f[1], s.f[2], s.f[3]};
        const vector4 r4 = vector4::from_reg(v4.reg());
        EXPECT_EQ(r4.x, v4.x) << n;
        EXPECT_EQ(r4.y, v4.y) << n;
        EXPECT_EQ(r4.z, v4.z) << n;
        EXPECT_EQ(r4.w, v4.w) << n;

        const vector3 v3{s.f[0], s.f[1], s.f[2]};
        const vector3 r3 = vector3::from_reg(v3.reg());
        EXPECT_EQ(r3.x, v3.x) << n;
        EXPECT_EQ(r3.y, v3.y) << n;
        EXPECT_EQ(r3.z, v3.z) << n;
    }
}

// A vector3 occupies twelve bytes, so its register cannot come from a sixteen
// byte load. This pins the w lane down: length and normalize would silently
// absorb whatever was there otherwise.
TEST(vector_register, vector3_leaves_the_fourth_lane_zero) {
    EXPECT_FLOAT_EQ(math::get_w(vector3{1, 2, 3}.reg()), 0.0f);
    EXPECT_FLOAT_EQ(math::get_z(vector2{1, 2}.reg()), 0.0f);
    EXPECT_FLOAT_EQ(math::get_w(vector2{1, 2}.reg()), 0.0f);
}

// ------------------------------------------------------------------- arithmetic
static_assert((vector3{1, 2, 3} + vector3{10, 20, 30}).y == 22.0f);
static_assert((vector3{10, 20, 30} - vector3{1, 2, 3}).z == 27.0f);
static_assert((vector3{2, 3, 4} * vector3{3, 3, 3}).x == 6.0f);
static_assert((vector3{10, 20, 30} / vector3{2, 2, 2}).y == 10.0f);
static_assert((vector3{1, 2, 3} * 2.0f).z == 6.0f);
static_assert((2.0f * vector3{1, 2, 3}).z == 6.0f);
static_assert((vector3{10, 20, 30} / 2.0f).x == 5.0f);
static_assert((-vector3{1, -2, 3}).y == 2.0f);

// Division has to survive constant evaluation at every width. vector2 and
// vector3 load their unused lanes as zero, so dividing by another vector would
// compute 0/0 in lanes the type does not own -- fine at run time, where the
// result is discarded, but undefined arithmetic that aborts a constexpr. The
// divisor's unused lanes are filled with one to prevent it, and these pin that.
static_assert((vector2{10, 20} / vector2{2, 4}).y == 5.0f);
static_assert((vector3{10, 20, 30} / vector3{2, 4, 5}).z == 6.0f);
static_assert((vector4{10, 20, 30, 40} / vector4{2, 4, 5, 8}).w == 5.0f);
static_assert((vector2{10, 20} / 2.0f).y == 10.0f);
static_assert((vector3{10, 20, 30} / 2.0f).z == 15.0f);
static_assert((vector4{10, 20, 30, 40} / 2.0f).w == 20.0f);

// Note for anyone tempted to give operator/(V, float) the same unused-lane
// guard: it does not need one. Dividing by a zero scalar is rejected during
// constant evaluation for EVERY width, vector4 included, because [expr.mul]/4
// makes division by zero undefined regardless of the operand type -- the lanes
// the vector actually owns fail first. The guard on operator/(V, V) is needed
// because there the divisor's used lanes can be nonzero while its unused lanes
// are zero, which is a case only the narrow types can reach.

TEST(vector_arithmetic, component_wise_against_hand_computed_values) {
    const vector4 a{1, 2, 3, 4};
    const vector4 b{10, 20, 30, 40};

    EXPECT_TRUE(a + b == vector4(11, 22, 33, 44));
    EXPECT_TRUE(b - a == vector4(9, 18, 27, 36));
    EXPECT_TRUE(a * b == vector4(10, 40, 90, 160));
    EXPECT_TRUE(b / a == vector4(10, 10, 10, 10));
    EXPECT_TRUE(a * 2.0f == vector4(2, 4, 6, 8));
    EXPECT_TRUE(-a == vector4(-1, -2, -3, -4));
}

// `*` is component-wise, following HLSL and GLM. Asserted explicitly because the
// alternative reading -- a dot product -- would still compile and return
// something plausible if the convention ever drifted.
TEST(vector_arithmetic, multiply_is_component_wise_not_dot_product) {
    const vector3 a{1, 2, 3};
    const vector3 b{4, 5, 6};
    EXPECT_TRUE(a * b == vector3(4, 10, 18));
    EXPECT_FLOAT_EQ(math::dot(a, b), 32.0f);
}

TEST(vector_arithmetic, compound_assignment_matches_the_binary_form) {
    vector3 v{1, 2, 3};
    v += vector3{1, 1, 1};
    EXPECT_TRUE(v == vector3(2, 3, 4));
    v -= vector3{1, 1, 1};
    EXPECT_TRUE(v == vector3(1, 2, 3));
    v *= 2.0f;
    EXPECT_TRUE(v == vector3(2, 4, 6));
    v /= 2.0f;
    EXPECT_TRUE(v == vector3(1, 2, 3));
}

// Each type must use exactly its own lanes. A vector2 that quietly computed over
// four would still pass every test above.
TEST(vector_arithmetic, operations_use_only_their_own_lanes) {
    EXPECT_FLOAT_EQ(math::dot(vector2{3, 4}, vector2{3, 4}), 25.0f);
    EXPECT_FLOAT_EQ(math::dot(vector3{1, 2, 3}, vector3{1, 1, 1}), 6.0f);
    EXPECT_FLOAT_EQ(math::dot(vector4{1, 2, 3, 4}, vector4{1, 1, 1, 1}), 10.0f);

    EXPECT_FLOAT_EQ(math::length(vector2{3, 4}), 5.0f);
    EXPECT_FLOAT_EQ(math::length(vector3{0, 3, 4}), 5.0f);
    EXPECT_FLOAT_EQ(math::length(vector4{0, 0, 3, 4}), 5.0f);
}

// ------------------------------------------------------------------ comparison
TEST(vector_comparison, equality_is_exact_and_per_component) {
    EXPECT_TRUE(vector3(1, 2, 3) == vector3(1, 2, 3));
    EXPECT_FALSE(vector3(1, 2, 3) == vector3(1, 2, 3.0001f));
    EXPECT_TRUE(vector3(1, 2, 3) != vector3(1, 2, 3.0001f));
}

// vector2 and vector3 leave the unused register lanes zero, but equality must
// not depend on that -- it compares only the lanes the type owns.
TEST(vector_comparison, equality_ignores_unused_lanes) {
    EXPECT_TRUE(vector2(1, 2) == vector2(1, 2));
    EXPECT_TRUE(vector3(1, 2, 3) == vector3(1, 2, 3));
    EXPECT_FALSE(vector3(1, 2, 3) == vector3(1, 2, 4));
}

TEST(vector_comparison, near_equal_accepts_small_differences) {
    EXPECT_TRUE(math::near_equal(vector3(1, 2, 3), vector3(1, 2, 3.000001f)));
    EXPECT_FALSE(math::near_equal(vector3(1, 2, 3), vector3(1, 2, 3.1f)));
    EXPECT_TRUE(math::near_equal(vector3(1, 2, 3), vector3(1, 2, 3.1f), 0.2f));
}

// ---------------------------------------------------------------- lane-wise math
static_assert(math::abs(vector3{-1, 2, -3}).x == 1.0f);
static_assert(math::min(vector3{1, 5, 3}, vector3{4, 2, 6}).y == 2.0f);
static_assert(math::max(vector3{1, 5, 3}, vector3{4, 2, 6}).x == 4.0f);
static_assert(math::lerp(vector3{0, 0, 0}, vector3{10, 20, 30}, 0.5f).y == 10.0f);

TEST(vector_lane_math, abs_min_max_clamp_saturate) {
    EXPECT_TRUE(math::abs(vector3(-1, 2, -3)) == vector3(1, 2, 3));
    EXPECT_TRUE(math::min(vector3(1, 5, 3), vector3(4, 2, 6)) == vector3(1, 2, 3));
    EXPECT_TRUE(math::max(vector3(1, 5, 3), vector3(4, 2, 6)) == vector3(4, 5, 6));
    EXPECT_TRUE(math::clamp(vector3(-1, 5, 0.5f), vector3::zero(),
                             vector3::one()) == vector3(0, 1, 0.5f));
    EXPECT_TRUE(math::saturate(vector3(-1, 5, 0.5f)) == vector3(0, 1, 0.5f));
}

// lerp is written as a + t * (b - a) so that the endpoints come back exactly.
// The algebraically equal (1-t)*a + t*b does not guarantee that.
TEST(vector_lane_math, lerp_is_exact_at_its_endpoints) {
    const vector3 a{1.1f, 2.2f, 3.3f};
    const vector3 b{9.9f, 8.8f, 7.7f};
    EXPECT_TRUE(math::lerp(a, b, 0.0f) == a);
    EXPECT_TRUE(math::lerp(a, b, 1.0f) == b);
    EXPECT_TRUE(math::near_equal(math::lerp(a, b, 0.5f),
                                 vector3(5.5f, 5.5f, 5.5f)));
}

TEST(vector_lane_math, lerp_extrapolates_outside_the_unit_interval) {
    const vector3 a{0, 0, 0};
    const vector3 b{10, 10, 10};
    EXPECT_TRUE(math::near_equal(math::lerp(a, b, 2.0f), vector3(20, 20, 20)));
    EXPECT_TRUE(math::near_equal(math::lerp(a, b, -1.0f),
                                 vector3(-10, -10, -10)));
}
