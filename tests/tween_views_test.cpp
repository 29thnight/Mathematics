#include <mathematics/tween_views.hpp>

#include <gtest/gtest.h>

#include <array>
#include <ranges>
#include <type_traits>
#include <vector>

namespace {

constexpr std::array<float, 5> progress{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

using fixed_eased_type = decltype(
    progress | math::views::ease(math::easing::smoothstep));
using fixed_tween_type = decltype(
    progress | math::views::tween(
        0.0f, 10.0f, math::easing::smoothstep));
using dynamic_eased_type = decltype(
    std::declval<std::vector<float>&>() |
    math::views::ease(math::easing::smoothstep));

static_assert(std::ranges::view<fixed_eased_type>);
static_assert(std::ranges::random_access_range<fixed_eased_type>);
static_assert(std::ranges::sized_range<fixed_eased_type>);
static_assert(math::ranges::static_extent_v<fixed_eased_type> == 5);
static_assert(math::ranges::static_extent_v<fixed_tween_type> == 5);
static_assert(math::ranges::static_extent_v<dynamic_eased_type> ==
              std::dynamic_extent);

constexpr bool tween_view_constexpr_contract() {
    auto values = progress |
        math::views::ease(math::easing::smoothstep) |
        math::views::lerp(0.0f, 8.0f);
    auto fused = progress |
        math::views::tween(0.0f, 8.0f, math::easing::smoothstep);
    return values[0] == 0.0f && values[2] == 4.0f && values[4] == 8.0f &&
           fused[0] == values[0] && fused[2] == values[2] &&
           fused[4] == values[4];
}

static_assert(tween_view_constexpr_contract());

} // namespace

TEST(tween_views, fixed_pipeline_is_lazy_pipeable_and_extent_preserving) {
    auto values = progress |
        math::views::ease(math::easing::quadratic_in) |
        math::views::lerp(math::vector2{0.0f, 10.0f},
                          math::vector2{8.0f, 18.0f});

    static_assert(math::ranges::static_extent_v<decltype(values)> == 5);
    EXPECT_EQ(values[0], math::vector2(0.0f, 10.0f));
    EXPECT_EQ(values[2], math::vector2(2.0f, 12.0f));
    EXPECT_EQ(values[4], math::vector2(8.0f, 18.0f));
}

TEST(tween_views, dynamic_pipeline_uses_standard_dynamic_extent) {
    std::vector<float> input{-1.0f, 0.5f, 2.0f};
    auto values = input |
        math::views::ease_clamped(math::easing::quadratic_in) |
        math::views::lerp(0.0f, 4.0f);

    static_assert(math::ranges::static_extent_v<decltype(values)> ==
                  std::dynamic_extent);
    EXPECT_FLOAT_EQ(values[0], 0.0f);
    EXPECT_FLOAT_EQ(values[1], 1.0f);
    EXPECT_FLOAT_EQ(values[2], 4.0f);
}

TEST(tween_views, rvalue_fixed_range_is_owned_by_the_view) {
    auto values = std::array<float, 3>{0.0f, 0.5f, 1.0f} |
        math::views::tween(2.0f, 6.0f, math::easing::linear);

    static_assert(math::ranges::static_extent_v<decltype(values)> == 3);
    EXPECT_FLOAT_EQ(values[0], 2.0f);
    EXPECT_FLOAT_EQ(values[1], 4.0f);
    EXPECT_FLOAT_EQ(values[2], 6.0f);
}

TEST(tween_views, quaternion_slerp_is_an_explicit_pipeable_policy) {
    const math::quaternion from = math::quaternion::identity();
    const math::quaternion to = math::quaternion_from_axis_angle(
        math::vector3{0.0f, 0.0f, 1.0f}, math::half_pi);
    auto rotations = progress | math::views::slerp(from, to);

    EXPECT_TRUE(math::same_rotation(
        rotations[2],
        math::quaternion_from_axis_angle(
            math::vector3{0.0f, 0.0f, 1.0f}, math::half_pi * 0.5f),
        1e-5f));
}

TEST(tween_views, fused_and_composed_forms_agree) {
    auto composed = progress |
        math::views::ease(math::easing::cubic_in_out) |
        math::views::lerp(math::rect{0.0f, 0.0f, 2.0f, 4.0f},
                          math::rect{8.0f, 6.0f, -2.0f, 12.0f});
    auto fused = progress |
        math::views::tween(math::rect{0.0f, 0.0f, 2.0f, 4.0f},
                           math::rect{8.0f, 6.0f, -2.0f, 12.0f},
                           math::easing::cubic_in_out);

    for (std::size_t index = 0; index < progress.size(); ++index)
        EXPECT_EQ(composed[index], fused[index]);
}
