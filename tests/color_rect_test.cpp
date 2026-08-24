#include <mathematics/color.hpp>
#include <mathematics/rect.hpp>

#include <gtest/gtest.h>

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
