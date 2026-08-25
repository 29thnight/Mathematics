#include <mathematics/views.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <ranges>
#include <span>
#include <tuple>
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

// The tuple protocol exists so a caller can consume a fixed range without
// forming a loop. MSVC does not unroll an iterator-driven loop nested inside
// another loop -- see docs/BASELINE.md section 9 -- so this is the shape that
// keeps a range-based spelling and still compiles to straight-line code.
static_assert(std::tuple_size_v<mutable_components> == 4);
static_assert(std::tuple_size_v<const_components> == 4);
static_assert(std::tuple_size_v<mutable_rows> == 4);
static_assert(std::tuple_size_v<
                  decltype(math::rows(std::declval<math::matrix3x3&>()))> == 3);
static_assert(std::same_as<std::tuple_element_t<0, mutable_components>, float>);
static_assert(
    std::same_as<std::tuple_element_t<3, const_components>, const float>);
static_assert(
    std::same_as<std::tuple_element_t<1, mutable_rows>, std::span<float, 4>>);

constexpr bool structured_bindings_alias_the_object() {
    math::vector4 vector{1, 2, 3, 4};
    auto&& [x, y, z, w] = math::components(vector);
    const float sum = x + y + z + w;
    x += 1.0f;
    w += 1.0f;

    math::matrix3x3 matrix{1, 2, 3,
                           4, 5, 6,
                           7, 8, 9};
    auto&& [row0, row1, row2] = math::rows(matrix);
    row1[2] = 42.0f;

    return sum == 10.0f && vector == math::vector4{2, 2, 3, 5} &&
           row0[0] == 1.0f && row2[2] == 9.0f && matrix.m[1][2] == 42.0f;
}

static_assert(structured_bindings_alias_the_object());

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

TEST(components_view, structured_bindings_write_through_to_the_vector) {
    math::vector3 value{1, 2, 3};
    auto&& [x, y, z] = math::components(value);

    EXPECT_FLOAT_EQ(x, 1.0f);
    EXPECT_FLOAT_EQ(z, 3.0f);

    y = -2.0f;
    EXPECT_EQ(value, math::vector3(1, -2, 3));
}

// std::apply and the rest of the C++23 tuple-like machinery only accept the
// standard tuple types, so the free get<I> is the spelling generic code has to
// use on these views.
TEST(components_view, free_get_names_each_component) {
    math::vector4 value{1, 2, 3, 4};
    auto view = math::components(value);

    EXPECT_FLOAT_EQ(math::ranges::get<0>(view), 1.0f);
    EXPECT_FLOAT_EQ(math::ranges::get<3>(view), 4.0f);

    math::ranges::get<2>(view) = -3.0f;
    EXPECT_EQ(value, math::vector4(1, 2, -3, 4));
}

TEST(rows_view, structured_bindings_yield_fixed_extent_rows) {
    math::matrix4x4 matrix = math::matrix4x4::identity();
    auto&& [row0, row1, row2, row3] = math::rows(matrix);

    static_assert(decltype(row0)::extent == 4);
    row3[1] = 7.0f;

    EXPECT_FLOAT_EQ(row0[0], 1.0f);
    EXPECT_FLOAT_EQ(row2[2], 1.0f);
    EXPECT_FLOAT_EQ(matrix.m[3][1], 7.0f);
}

TEST(fixed_transform_view, structured_bindings_apply_the_function) {
    const math::vector4 value{1, 2, 3, 4};
    auto&& [a, b, c, d] =
        math::components(value) |
        math::views::transform_fixed([](float component) {
            return component * component;
        });

    EXPECT_FLOAT_EQ(a, 1.0f);
    EXPECT_FLOAT_EQ(b, 4.0f);
    EXPECT_FLOAT_EQ(c, 9.0f);
    EXPECT_FLOAT_EQ(d, 16.0f);
}
