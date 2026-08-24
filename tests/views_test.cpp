#include <mathematics/views.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace {

template <typename type>
concept has_components = requires(type&& value) {
    math::components(std::forward<type>(value));
};

template <typename type>
concept has_rows = requires(type&& value) {
    math::rows(std::forward<type>(value));
};

using mutable_components =
    decltype(math::components(std::declval<math::vector4&>()));
using const_components =
    decltype(math::components(std::declval<const math::vector4&>()));
using mutable_rows = decltype(math::rows(std::declval<math::matrix4x4&>()));
using const_rows = decltype(math::rows(std::declval<const math::matrix4x4&>()));

static_assert(std::ranges::view<mutable_components>);
static_assert(std::ranges::random_access_range<mutable_components>);
static_assert(std::ranges::sized_range<mutable_components>);
static_assert(!std::ranges::contiguous_range<mutable_components>);
static_assert(std::same_as<std::ranges::range_reference_t<mutable_components>,
                           float&>);
static_assert(std::same_as<std::ranges::range_reference_t<const_components>,
                           const float&>);

static_assert(std::ranges::view<mutable_rows>);
static_assert(std::ranges::random_access_range<mutable_rows>);
static_assert(std::same_as<std::ranges::range_reference_t<mutable_rows>,
                           std::span<float, 4>>);
static_assert(std::same_as<std::ranges::range_reference_t<const_rows>,
                           std::span<const float, 4>>);

static_assert(has_components<math::vector2&>);
static_assert(has_components<const math::vector3&>);
static_assert(!has_components<math::vector4>);
static_assert(has_rows<math::matrix3x3&>);
static_assert(has_rows<const math::matrix4x4&>);
static_assert(!has_rows<math::matrix4x4>);

constexpr bool views_work_during_constant_evaluation() {
    math::vector4 vector{1, 2, 3, 4};
    float sum = 0.0f;
    for (float& component : math::components(vector)) {
        sum += component;
        component += 1.0f;
    }

    math::matrix3x3 matrix{1, 2, 3,
                           4, 5, 6,
                           7, 8, 9};
    auto matrix_rows = math::rows(matrix);
    matrix_rows[1][2] = 42.0f;
    return sum == 10.0f && vector == math::vector4{2, 3, 4, 5} &&
           matrix.m[1][2] == 42.0f;
}

static_assert(views_work_during_constant_evaluation());

} // namespace

TEST(components_view, aliases_every_vector_width) {
    math::vector2 v2{1, 2};
    math::vector3 v3{3, 4, 5};
    math::vector4 v4{6, 7, 8, 9};

    std::ranges::fill(math::components(v2), 10.0f);
    for (float& component : math::components(v3)) component *= 2.0f;
    auto c4 = math::components(v4);
    c4[0] = -6.0f;
    c4[3] = -9.0f;

    EXPECT_EQ(v2, math::vector2(10, 10));
    EXPECT_EQ(v3, math::vector3(6, 8, 10));
    EXPECT_EQ(v4, math::vector4(-6, 7, 8, -9));
}

TEST(components_view, const_view_reads_named_members_in_order) {
    const math::vector4 value{1, 2, 3, 4};
    const auto view = math::components(value);

    ASSERT_EQ(std::ranges::size(view), 4u);
    EXPECT_EQ(view[0], value.x);
    EXPECT_EQ(view[1], value.y);
    EXPECT_EQ(view[2], value.z);
    EXPECT_EQ(view[3], value.w);
}

TEST(rows_view, exposes_fixed_extent_rows_that_alias_the_matrix) {
    math::matrix4x4 matrix = math::matrix4x4::identity();
    auto view = math::rows(matrix);

    static_assert(decltype(view[0])::extent == 4);
    ASSERT_EQ(std::ranges::size(view), 4u);
    view[2][1] = 12.0f;
    view[3][0] = 20.0f;

    EXPECT_FLOAT_EQ(matrix.m[2][1], 12.0f);
    EXPECT_FLOAT_EQ(matrix.m[3][0], 20.0f);
}

TEST(rows_view, const_rows_preserve_row_major_order) {
    const math::matrix3x3 matrix{1, 2, 3,
                                 4, 5, 6,
                                 7, 8, 9};
    const auto view = math::rows(matrix);

    static_assert(decltype(view[0])::extent == 3);
    EXPECT_FLOAT_EQ(view[0][2], 3.0f);
    EXPECT_FLOAT_EQ(view[1][0], 4.0f);
    EXPECT_FLOAT_EQ(view[2][1], 8.0f);
}
