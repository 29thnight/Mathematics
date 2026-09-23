#include <mathematics/color.hpp>
#include <mathematics/rect.hpp>

#include "support/runtime_value.hpp"

#include <gtest/gtest.h>

#include <limits>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHEMATICS_TEST_HAS_DIRECTXMATH_COLOR 1
#else
#  define MATHEMATICS_TEST_HAS_DIRECTXMATH_COLOR 0
#endif

using math::color;
using math::rect;
using math::vector2;
using math::vector3;
using math::vector4;

namespace {

constexpr bool color_constexpr_contract() {
    const color value{0.25f, 0.5f, 0.75f, 0.5f};
    const color premultiplied = math::premultiply(value);
    const color midpoint = math::lerp(color::black(), color::white(), 0.5f);
    const color round_trip = math::unpack_rgba8(math::pack_rgba8(
        color{1.0f, 0.0f, 0.5f, 1.0f}));
    return premultiplied == color{0.125f, 0.25f, 0.375f, 0.5f} &&
           midpoint == color{0.5f, 0.5f, 0.5f, 1.0f} &&
           round_trip.r == 1.0f && round_trip.g == 0.0f &&
           round_trip.a == 1.0f;
}

constexpr bool rect_constexpr_contract() {
    const rect outer{10.0f, 20.0f, 30.0f, 40.0f};
    const rect overlap = math::intersection(
        outer, rect{30.0f, 40.0f, 20.0f, 30.0f});
    return math::contains(outer, vector2{10.0f, 20.0f}) &&
           !math::contains(outer, vector2{40.0f, 60.0f}) &&
           overlap == rect{30.0f, 40.0f, 10.0f, 20.0f};
}

static_assert(color_constexpr_contract());
static_assert(rect_constexpr_contract());
static_assert(color{}.a == 1.0f);
static_assert(rect{}.is_empty());

TEST(color_storage, constructors_conversion_and_constants) {
    EXPECT_EQ(color{}, color::black());
    EXPECT_EQ(color::transparent(), color(0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ((color{vector3{0.1f, 0.2f, 0.3f}}),
              color(0.1f, 0.2f, 0.3f, 1.0f));
    EXPECT_EQ((color{vector4{0.1f, 0.2f, 0.3f, 0.4f}}.rgba()),
              vector4(0.1f, 0.2f, 0.3f, 0.4f));
    EXPECT_EQ(color::red().rgb(), vector3(1.0f, 0.0f, 0.0f));
}

TEST(color_operations, arithmetic_saturate_and_premultiply) {
    const color x{0.2f, 0.4f, 0.6f, 0.5f};
    const color y{0.5f, 0.25f, 2.0f, 0.5f};
    EXPECT_TRUE(math::near_equal(x * y,
                                 color{0.1f, 0.1f, 1.2f, 0.25f}));
    EXPECT_TRUE(math::near_equal(math::saturate(x * y),
                                 color{0.1f, 0.1f, 1.0f, 0.25f}));
    EXPECT_TRUE(math::near_equal(math::premultiply(x),
                                 color{0.1f, 0.2f, 0.3f, 0.5f}));
    EXPECT_TRUE(math::near_equal(math::negative(x),
                                 color{0.8f, 0.6f, 0.4f, 0.5f}));
}

TEST(color_operations, saturation_and_contrast_preserve_alpha) {
    const color value{0.8f, 0.3f, 0.1f, 0.25f};
    const color grey = math::adjust_saturation(value, 0.0f);
    EXPECT_FLOAT_EQ(grey.r, grey.g);
    EXPECT_FLOAT_EQ(grey.g, grey.b);
    EXPECT_FLOAT_EQ(grey.a, value.a);
    EXPECT_TRUE(math::near_equal(math::adjust_saturation(value, 1.0f),
                                 value, 1e-7f));
    EXPECT_TRUE(math::near_equal(math::adjust_contrast(value, 1.0f),
                                 value, 1e-7f));
    EXPECT_EQ(math::adjust_contrast(value, 0.0f),
              color(0.5f, 0.5f, 0.5f, value.a));
}

TEST(color_packing, rgba_and_bgra_layouts_are_explicit) {
    const color value{1.0f, 0.5f, 0.0f, 1.0f};
    EXPECT_EQ(math::pack_rgba8(value), 0xff0080ffu);
    EXPECT_EQ(math::pack_bgra8(value), 0xffff8000u);
    EXPECT_TRUE(math::near_equal(
        math::unpack_rgba8(math::pack_rgba8(value)), value, 1.0f / 255.0f));
    EXPECT_TRUE(math::near_equal(
        math::unpack_bgra8(math::pack_bgra8(value)), value, 1.0f / 255.0f));
}

TEST(rect_storage, normalized_and_min_max_construction) {
    EXPECT_EQ(math::normalized(rect{10.0f, 20.0f, -4.0f, -6.0f}),
              rect(6.0f, 14.0f, 4.0f, 6.0f));
    EXPECT_EQ(rect::from_min_max(vector2{2.0f, 3.0f},
                                 vector2{8.0f, 10.0f}),
              rect(2.0f, 3.0f, 6.0f, 7.0f));
    EXPECT_EQ((rect{2.0f, 3.0f, 6.0f, 8.0f}.center()),
              vector2(5.0f, 7.0f));
}

TEST(rect_queries, uses_half_open_edges_and_positive_area_overlap) {
    const rect value{0.0f, 0.0f, 10.0f, 10.0f};
    EXPECT_TRUE(math::contains(value, vector2{0.0f, 0.0f}));
    EXPECT_TRUE(math::contains(value, vector2{9.999f, 9.999f}));
    EXPECT_FALSE(math::contains(value, vector2{10.0f, 5.0f}));
    EXPECT_FALSE(math::intersects(value, rect{10.0f, 0.0f, 2.0f, 2.0f}));
    EXPECT_TRUE(math::intersects(value, rect{9.0f, 9.0f, 2.0f, 2.0f}));
}

TEST(rect_operations, intersection_merge_offset_inflate_and_closest_point) {
    const rect x{0.0f, 0.0f, 10.0f, 8.0f};
    const rect y{8.0f, 3.0f, 4.0f, 8.0f};
    EXPECT_EQ(math::intersection(x, y), rect(8.0f, 3.0f, 2.0f, 5.0f));
    EXPECT_EQ(math::merge(x, y), rect(0.0f, 0.0f, 12.0f, 11.0f));
    EXPECT_EQ(math::offset(x, vector2{2.0f, -1.0f}),
              rect(2.0f, -1.0f, 10.0f, 8.0f));
    EXPECT_EQ(math::inflate(x, vector2{2.0f, 1.0f}),
              rect(-2.0f, -1.0f, 14.0f, 10.0f));
    EXPECT_EQ(math::closest_point(x, vector2{20.0f, -5.0f}),
              vector2(10.0f, 0.0f));
}

#if MATHEMATICS_TEST_HAS_DIRECTXMATH_COLOR
color from_xm(DirectX::FXMVECTOR value) {
    DirectX::XMFLOAT4 stored;
    DirectX::XMStoreFloat4(&stored, value);
    return color{stored.x, stored.y, stored.z, stored.w};
}

TEST(color_dx_parity, adjustment_helpers_match_direct_x_math) {
    const color value{0.8f, 0.3f, 0.1f, 0.25f};
    const DirectX::XMVECTOR dx =
        DirectX::XMVectorSet(value.r, value.g, value.b, value.a);
    EXPECT_TRUE(math::near_equal(
        math::adjust_saturation(value, 0.35f),
        from_xm(DirectX::XMColorAdjustSaturation(dx, 0.35f)), 1e-6f));
    EXPECT_TRUE(math::near_equal(
        math::adjust_contrast(value, 1.4f),
        from_xm(DirectX::XMColorAdjustContrast(dx, 1.4f)), 1e-6f));
    EXPECT_TRUE(math::near_equal(
        math::negative(value), from_xm(DirectX::XMColorNegative(dx)), 1e-6f));
}
#endif

} // namespace

// ----------------------------------------------------- operators and queries
// Each input passes through runtime_value so the compiled operator runs; with
// every argument a literal, GCC evaluates the whole call at compile time.
TEST(color_operations, arithmetic_operators_act_per_channel) {
    const color x = math_test::runtime_value(color{0.2f, 0.4f, 0.6f, 0.8f});
    const color y{0.1f, 0.2f, 0.3f, 0.4f};

    EXPECT_TRUE(math::near_equal(x + y, color{0.3f, 0.6f, 0.9f, 1.2f}));
    EXPECT_TRUE(math::near_equal(x - y, color{0.1f, 0.2f, 0.3f, 0.4f}));
    EXPECT_TRUE(math::near_equal(x / y, color{2.0f, 2.0f, 2.0f, 2.0f}));
    EXPECT_TRUE(math::near_equal(x * 0.5f, color{0.1f, 0.2f, 0.3f, 0.4f}));
    EXPECT_TRUE(math::near_equal(0.5f * x, x * 0.5f));
    EXPECT_TRUE(math::near_equal(x / 2.0f, x * 0.5f));
    EXPECT_TRUE(math::near_equal(-x, color{-0.2f, -0.4f, -0.6f, -0.8f}));
    // Alpha is a channel like the others here -- premultiply is the operation
    // that treats it differently.
    EXPECT_TRUE(math::near_equal(math::modulate(x, y), x * y));
}

TEST(color_operations, compound_assignment_matches_the_binary_forms) {
    const color start = math_test::runtime_value(color{0.2f, 0.4f, 0.6f, 0.8f});
    const color other{0.1f, 0.2f, 0.4f, 0.5f};

    color value = start;
    value += other;
    EXPECT_EQ(value, start + other);
    value = start;
    value -= other;
    EXPECT_EQ(value, start - other);
    value = start;
    value *= other;
    EXPECT_EQ(value, start * other);
    value = start;
    value *= 3.0f;
    EXPECT_EQ(value, start * 3.0f);
    value = start;
    value /= other;
    EXPECT_EQ(value, start / other);
    value = start;
    value /= 4.0f;
    EXPECT_EQ(value, start / 4.0f);
}

TEST(color_storage, indexing_names_rgba_in_order_and_writes_through) {
    color value = math_test::runtime_value(color::green());
    EXPECT_EQ(value, color(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(math_test::runtime_value(color::blue()),
              color(0.0f, 0.0f, 1.0f, 1.0f));

    value[0] = 0.25f;
    value[3] = 0.5f;
    EXPECT_EQ(value, color(0.25f, 1.0f, 0.0f, 0.5f));
    const color& view = value;
    EXPECT_EQ(view[1], 1.0f);
    EXPECT_EQ(view[2], 0.0f);
}

TEST(rect_storage, default_is_an_empty_rect_at_the_origin) {
    const rect value = math_test::runtime_value(rect{});
    EXPECT_EQ(value, rect(0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(value.is_empty());
    EXPECT_EQ(value.area(), 0.0f);
}

TEST(rect_queries, area_is_zero_for_every_kind_of_empty) {
    EXPECT_EQ(math_test::runtime_value(rect{1.0f, 2.0f, 3.0f, 4.0f}).area(),
              12.0f);
    EXPECT_EQ(math_test::runtime_value(rect{1.0f, 2.0f, -3.0f, 4.0f}).area(),
              0.0f)
        << "negative width is empty, not negative area";
    EXPECT_EQ(math_test::runtime_value(rect{1.0f, 2.0f, 3.0f, 0.0f}).area(),
              0.0f);
    EXPECT_EQ(math_test::runtime_value(
                  rect{0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(),
                       1.0f}).area(),
              0.0f);
}

// Closed on every edge, unlike the half-open point test: a rect contains
// itself, and one sharing an outer edge.
TEST(rect_queries, containment_of_a_rect_is_closed_and_rejects_empties) {
    const rect outer = math_test::runtime_value(rect{0.0f, 0.0f, 10.0f, 10.0f});
    EXPECT_TRUE(math::contains(outer, outer));
    EXPECT_TRUE(math::contains(outer, rect{5.0f, 5.0f, 5.0f, 5.0f}));
    EXPECT_FALSE(math::contains(outer, rect{5.0f, 5.0f, 5.5f, 5.0f}));
    EXPECT_FALSE(math::contains(outer, rect{-0.5f, 2.0f, 1.0f, 1.0f}));
    EXPECT_FALSE(math::contains(outer, rect{2.0f, 2.0f, 0.0f, 1.0f}))
        << "an empty rect is not contained, even when it lies inside";
    EXPECT_FALSE(math::contains(rect{}, rect{}));
}

TEST(rect_queries, near_equal_bounds_every_field_by_epsilon) {
    const rect x = math_test::runtime_value(rect{1.0f, 2.0f, 3.0f, 4.0f});
    EXPECT_TRUE(math::near_equal(x, rect{1.000001f, 2.0f, 3.0f, 4.0f}));
    EXPECT_FALSE(math::near_equal(x, rect{1.0f, 2.1f, 3.0f, 4.0f}));
    EXPECT_FALSE(math::near_equal(x, rect{1.0f, 2.0f, 3.0f, 3.9f}));
    EXPECT_TRUE(math::near_equal(x, rect{1.0f, 2.0f, 3.05f, 4.0f}, 0.1f));
    EXPECT_FALSE(math::near_equal(
        x, rect{1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), 4.0f}))
        << "NaN in any field fails the positive-form comparison";
}
