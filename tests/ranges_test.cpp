#include <mathematics/ranges.hpp>
#include <mathematics/views.hpp>

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using component_range =
    decltype(math::components(std::declval<math::vector4&>()));
using row_range = decltype(math::rows(std::declval<math::matrix3x3&>()));

struct square {
    constexpr float operator()(float value) const { return value * value; }
};

using transformed_component_range = decltype(
    math::components(std::declval<math::vector4&>()) |
    math::views::transform_fixed(square{}));

static_assert(math::ranges::static_extent_v<component_range> == 4);
static_assert(math::ranges::static_extent_v<row_range> == 3);
static_assert(math::ranges::static_extent_v<std::span<float, 4>> == 4);
static_assert(math::ranges::static_extent_v<std::array<float, 2>> == 2);
static_assert(math::ranges::static_extent_v<float[3]> == 3);
static_assert(math::ranges::static_extent_v<std::span<float>> ==
              std::dynamic_extent);
static_assert(math::ranges::fixed_random_access_range<component_range>);
static_assert(!math::ranges::fixed_random_access_range<std::span<float>>);
static_assert(std::ranges::view<transformed_component_range>);
static_assert(std::ranges::random_access_range<transformed_component_range>);
static_assert(std::ranges::random_access_range<const transformed_component_range>);
static_assert(std::ranges::sized_range<transformed_component_range>);
static_assert(math::ranges::static_extent_v<transformed_component_range> == 4);
static_assert(std::same_as<
              std::ranges::range_reference_t<transformed_component_range>,
              float>);

// with_static_extent over a plain range carries no accessor, so it has no
// member get<I>(). The tuple protocol still has to reach it, through the free
// get<I> that falls back to indexing the wrapped range.
using wrapped_span_range =
    decltype(math::ranges::with_static_extent<4>(std::declval<std::span<float, 4>&>()));

static_assert(std::tuple_size_v<wrapped_span_range> == 4);
static_assert(std::same_as<std::tuple_element_t<2, wrapped_span_range>, float>);
static_assert(std::tuple_size_v<transformed_component_range> == 4);
static_assert(
    std::same_as<std::tuple_element_t<0, transformed_component_range>, float>);

struct counter {
    int calls = 0;

    constexpr void operator()(float& value) {
        value += 1.0f;
        ++calls;
    }
};

constexpr bool fixed_algorithms_work_during_constant_evaluation() {
    math::vector4 vector{1, 2, 3, 4};

    const float sum = math::ranges::fold_fixed(
        math::components(vector), 0.0f, std::plus<>{});
    const counter result =
        math::ranges::for_each_fixed(math::components(vector), counter{});

    std::array<float, 4> transformed{};
    const auto output = math::ranges::transform_fixed(
        math::components(vector), transformed.begin(),
        [](float value) { return value * 2.0f; });

    return sum == 10.0f && result.calls == 4 &&
           vector == math::vector4{2, 3, 4, 5} &&
           output == transformed.end() &&
           transformed == std::array<float, 4>{4, 6, 8, 10};
}

static_assert(fixed_algorithms_work_during_constant_evaluation());

constexpr bool fixed_pipelines_work_during_constant_evaluation() {
    math::vector4 vector{1, 2, 3, 4};
    auto transformed =
        math::components(vector) |
        math::views::transform_fixed(square{}) |
        math::views::transform_fixed(
            [](float value) { return value + 1.0f; });

    float range_for_sum = 0.0f;
    for (const float value : transformed) range_for_sum += value;

    const float folded = transformed |
        math::ranges::fold_fixed(0.0f, std::plus<>{});

    const counter result = math::components(vector) |
        math::ranges::for_each_fixed(counter{});

    std::array<float, 4> output{};
    const auto output_end = math::components(vector) |
        math::ranges::transform_fixed_to(output.begin(), square{});

    return range_for_sum == 34.0f && folded == 34.0f &&
           result.calls == 4 && vector == math::vector4{2, 3, 4, 5} &&
           output_end == output.end() &&
           output == std::array<float, 4>{4, 9, 16, 25};
}

static_assert(fixed_pipelines_work_during_constant_evaluation());

} // namespace

TEST(fixed_range_algorithms, fold_preserves_left_to_right_order) {
    const math::vector4 value{1, 2, 3, 4};

    const float decimal = math::ranges::fold_fixed(
        math::components(value), 0.0f,
        [](float accumulated, float component) {
            return accumulated * 10.0f + component;
        });

    EXPECT_FLOAT_EQ(decimal, 1234.0f);
}

TEST(fixed_range_algorithms, for_each_aliases_mutable_components) {
    math::vector3 value{1, 2, 3};

    const counter result =
        math::ranges::for_each_fixed(math::components(value), counter{});

    EXPECT_EQ(result.calls, 3);
    EXPECT_EQ(value, math::vector3(2, 3, 4));
}

TEST(fixed_range_algorithms, transform_writes_exact_extent_and_returns_end) {
    const math::vector4 value{1, 2, 3, 4};
    std::array<float, 5> output{-1, -1, -1, -1, 99};

    const auto end = math::ranges::transform_fixed(
        math::components(value), output.begin(),
        [](float component) { return component * component; });

    EXPECT_EQ(end, output.begin() + 4);
    EXPECT_EQ(output, (std::array<float, 5>{1, 4, 9, 16, 99}));
}

TEST(fixed_range_algorithms, nested_fold_handles_fixed_matrix_rows) {
    const math::matrix3x3 matrix{1, 2, 3,
                                 4, 5, 6,
                                 7, 8, 9};

    const float sum = math::ranges::fold_fixed(
        math::rows(matrix), 0.0f,
        [](float accumulated, std::span<const float, 3> row) {
            return math::ranges::fold_fixed(row, accumulated, std::plus<>{});
        });

    EXPECT_FLOAT_EQ(sum, 45.0f);
}

TEST(fixed_range_pipelines, lazy_transform_supports_range_for) {
    const math::vector4 value{1, 2, 3, 4};
    auto transformed =
        math::components(value) |
        math::views::transform_fixed(
            [](float component) { return component * 2.0f; });

    std::array<float, 4> visited{};
    auto output = visited.begin();
    for (const float component : transformed) *output++ = component;

    EXPECT_EQ(visited, (std::array<float, 4>{2, 4, 6, 8}));
}

TEST(fixed_range_pipelines, lazy_transform_can_preserve_mutable_references) {
    math::vector4 value{1, 2, 3, 4};
    auto aliases =
        math::components(value) |
        math::views::transform_fixed(
            [](float& component) -> float& { return component; });

    static_assert(std::same_as<std::ranges::range_reference_t<decltype(aliases)>,
                               float&>);
    for (float& component : aliases) component *= 3.0f;

    EXPECT_EQ(value, math::vector4(3, 6, 9, 12));
}

TEST(fixed_range_pipelines, chained_transforms_preserve_extent_for_terminal) {
    const math::vector4 value{1, 2, 3, 4};
    auto transformed =
        math::components(value) |
        math::views::transform_fixed(square{}) |
        math::views::transform_fixed(
            [](float component) { return component + 1.0f; });

    static_assert(math::ranges::static_extent_v<decltype(transformed)> == 4);
    const float result = transformed |
        math::ranges::fold_fixed(0.0f, std::plus<>{});

    EXPECT_FLOAT_EQ(result, 34.0f);
}

TEST(fixed_range_pipelines, terminal_closures_match_direct_calls) {
    math::vector3 value{1, 2, 3};
    const counter result = math::components(value) |
        math::ranges::for_each_fixed(counter{});

    std::array<float, 3> output{};
    const auto output_end = math::components(value) |
        math::ranges::transform_fixed_to(output.begin(), square{});

    EXPECT_EQ(result.calls, 3);
    EXPECT_EQ(output_end, output.end());
    EXPECT_EQ(output, (std::array<float, 3>{4, 9, 16}));
}

TEST(fixed_extent_view, tuple_protocol_reaches_a_range_without_an_accessor) {
    std::array<float, 4> storage{1, 2, 3, 4};
    std::span<float, 4> elements{storage};
    auto view = math::ranges::with_static_extent<4>(elements);

    auto&& [a, b, c, d] = view;
    EXPECT_FLOAT_EQ(a, 1.0f);
    EXPECT_FLOAT_EQ(d, 4.0f);

    c = -3.0f;
    EXPECT_FLOAT_EQ(storage[2], -3.0f);
}
