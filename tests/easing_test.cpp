#include <mathematics/easing.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {

constexpr std::array<math::easing_function, 34> all_easings{
    math::easing::linear,
    math::easing::step,
    math::easing::smoothstep,
    math::easing::smootherstep,
    math::easing::quadratic_in,
    math::easing::quadratic_out,
    math::easing::quadratic_in_out,
    math::easing::cubic_in,
    math::easing::cubic_out,
    math::easing::cubic_in_out,
    math::easing::quartic_in,
    math::easing::quartic_out,
    math::easing::quartic_in_out,
    math::easing::quintic_in,
    math::easing::quintic_out,
    math::easing::quintic_in_out,
    math::easing::sine_in,
    math::easing::sine_out,
    math::easing::sine_in_out,
    math::easing::circular_in,
    math::easing::circular_out,
    math::easing::circular_in_out,
    math::easing::exponential_in,
    math::easing::exponential_out,
    math::easing::exponential_in_out,
    math::easing::elastic_in,
    math::easing::elastic_out,
    math::easing::elastic_in_out,
    math::easing::back_in,
    math::easing::back_out,
    math::easing::back_in_out,
    math::easing::bounce_in,
    math::easing::bounce_out,
    math::easing::bounce_in_out};

constexpr bool easing_constexpr_contract() {
    const math::easing_function erased{math::easing::cubic_in_out};
    return math::easing::linear(0.25f) == 0.25f &&
           math::easing::smoothstep(0.5f) == 0.5f &&
           math::easing::smootherstep(0.5f) == 0.5f &&
           math::easing::quadratic_in_out(0.5f) == 0.5f &&
           erased(0.5f) == 0.5f &&
           math::ease_clamped(-1.0f, math::easing::quadratic_in) == 0.0f &&
           math::ease_clamped(2.0f, math::easing::quadratic_in) == 1.0f &&
           math::exp2(-10.0f) == 1.0f / 1024.0f;
}

static_assert(easing_constexpr_contract());
static_assert(sizeof(math::easing_function) == sizeof(void*));
static_assert(std::is_trivially_copyable_v<math::easing_function>);

template <typename in_type, typename out_type>
void expect_mirrored(in_type ease_in, out_type ease_out) {
    for (int index = 0; index <= 100; ++index) {
        const float t = static_cast<float>(index) / 100.0f;
        EXPECT_NEAR(ease_in(t), 1.0f - ease_out(1.0f - t), 2e-5f);
    }
}

} // namespace

TEST(easing_contract, every_curve_has_exact_normalized_endpoints) {
    for (const math::easing_function curve : all_easings) {
        EXPECT_FLOAT_EQ(curve(0.0f), 0.0f);
        EXPECT_FLOAT_EQ(curve(1.0f), 1.0f);
    }
}

TEST(easing_contract, raw_and_clamped_input_are_explicitly_different) {
    EXPECT_FLOAT_EQ(math::easing::quadratic_in(-0.5f), 0.25f);
    EXPECT_FLOAT_EQ(
        math::ease_clamped(-0.5f, math::easing::quadratic_in), 0.0f);
    EXPECT_FLOAT_EQ(math::easing::linear(1.5f), 1.5f);
    EXPECT_FLOAT_EQ(
        math::ease_clamped(1.5f, math::easing::linear), 1.0f);
}

TEST(easing_contract, nan_progress_is_propagated) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(std::isnan(math::easing::linear(nan)));
    EXPECT_TRUE(std::isnan(math::easing::step(nan)));
    EXPECT_TRUE(std::isnan(
        math::ease_clamped(nan, math::easing::quadratic_in)));
}

TEST(easing_shape, polynomial_and_trigonometric_pairs_are_mirrored) {
    expect_mirrored(math::easing::quadratic_in, math::easing::quadratic_out);
    expect_mirrored(math::easing::cubic_in, math::easing::cubic_out);
    expect_mirrored(math::easing::quartic_in, math::easing::quartic_out);
    expect_mirrored(math::easing::quintic_in, math::easing::quintic_out);
    expect_mirrored(math::easing::sine_in, math::easing::sine_out);
    expect_mirrored(math::easing::circular_in, math::easing::circular_out);
    expect_mirrored(math::easing::exponential_in,
                    math::easing::exponential_out);
    expect_mirrored(math::easing::elastic_in, math::easing::elastic_out);
    expect_mirrored(math::easing::back_in, math::easing::back_out);
    expect_mirrored(math::easing::bounce_in, math::easing::bounce_out);
}

TEST(easing_shape, ordinary_curves_stay_bounded_and_monotonic) {
    const std::array<math::easing_function, 25> curves{
        math::easing::linear,
        math::easing::step,
        math::easing::smoothstep,
        math::easing::smootherstep,
        math::easing::quadratic_in,
        math::easing::quadratic_out,
        math::easing::quadratic_in_out,
        math::easing::cubic_in,
        math::easing::cubic_out,
        math::easing::cubic_in_out,
        math::easing::quartic_in,
        math::easing::quartic_out,
        math::easing::quartic_in_out,
        math::easing::quintic_in,
        math::easing::quintic_out,
        math::easing::quintic_in_out,
        math::easing::sine_in,
        math::easing::sine_out,
        math::easing::sine_in_out,
        math::easing::circular_in,
        math::easing::circular_out,
        math::easing::circular_in_out,
        math::easing::exponential_in,
        math::easing::exponential_out,
        math::easing::exponential_in_out};

    for (const math::easing_function curve : curves) {
        float previous = curve(0.0f);
        for (int index = 1; index <= 1000; ++index) {
            const float t = static_cast<float>(index) / 1000.0f;
            const float current = curve(t);
            EXPECT_GE(current, -1e-6f);
            EXPECT_LE(current, 1.0f + 1e-6f);
            EXPECT_GE(current + 1e-6f, previous);
            previous = current;
        }
    }
}

TEST(easing_shape, overshoot_curves_retain_their_intended_range) {
    float back_min = 0.0f;
    float back_max = 1.0f;
    float elastic_min = 0.0f;
    float elastic_max = 1.0f;
    for (int index = 0; index <= 1000; ++index) {
        const float t = static_cast<float>(index) / 1000.0f;
        back_min = std::min(back_min, math::easing::back_in(t));
        back_max = std::max(back_max, math::easing::back_out(t));
        elastic_min = std::min(elastic_min, math::easing::elastic_in(t));
        elastic_max = std::max(elastic_max, math::easing::elastic_out(t));
    }
    EXPECT_LT(back_min, 0.0f);
    EXPECT_GT(back_max, 1.0f);
    EXPECT_LT(elastic_min, 0.0f);
    EXPECT_GT(elastic_max, 1.0f);
}

TEST(scalar_exp2, matches_standard_library_across_normal_easing_range) {
    for (int index = -1024; index <= 1024; ++index) {
        const float exponent = static_cast<float>(index) / 64.0f;
        EXPECT_NEAR(math::exp2(exponent), std::exp2(exponent),
                    std::exp2(exponent) * 2e-6f);
    }
    EXPECT_FLOAT_EQ(math::exp2(-149.0f), std::exp2(-149.0f));
    EXPECT_FLOAT_EQ(math::exp2(-150.0f), 0.0f);
    EXPECT_TRUE(std::isinf(math::exp2(128.0f)));
}
