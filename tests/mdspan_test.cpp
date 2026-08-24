#include <mathematics/mdspan.hpp>

#include <gtest/gtest.h>

#include <type_traits>

#if MATHEMATICS_HAS_MDSPAN

namespace {

static_assert(MATHEMATICS_HAS_CPP23);
static_assert(math::matrix3x3_mdspan::rank() == 2);
static_assert(math::matrix3x3_mdspan::static_extent(0) == 3);
static_assert(math::matrix3x3_mdspan::static_extent(1) == 3);
static_assert(math::matrix4x4_mdspan::static_extent(0) == 4);
static_assert(std::same_as<math::matrix4x4_mdspan::layout_type,
                           std::layout_right>);
static_assert(std::same_as<math::matrix4x4_transpose_mdspan::layout_type,
                           std::layout_left>);
static_assert(std::same_as<decltype(std::declval<math::matrix4x4_mdspan>()[0, 0]),
                           float&>);
static_assert(std::same_as<
              decltype(std::declval<math::const_matrix4x4_mdspan>()[0, 0]),
              const float&>);

constexpr bool mdspan_views_work_during_constant_evaluation() {
    math::matrix3x3 matrix{1, 2, 3,
                           4, 5, 6,
                           7, 8, 9};
    auto view = math::as_mdspan(matrix);
    view[1, 2] = 42.0f;
    const auto transposed = math::transpose_view(matrix);
    return matrix.m[1][2] == 42.0f && transposed[2, 1] == 42.0f &&
           transposed[0, 2] == 7.0f;
}

static_assert(mdspan_views_work_during_constant_evaluation());

} // namespace

TEST(mdspan_view, fixed_extents_alias_matrix_storage) {
    math::matrix4x4 matrix{
        1,  2,  3,  4,
        5,  6,  7,  8,
        9, 10, 11, 12,
       13, 14, 15, 16};
    auto view = math::as_mdspan(matrix);

    EXPECT_EQ(view.extent(0), 4u);
    EXPECT_EQ(view.extent(1), 4u);
    EXPECT_FLOAT_EQ((view[2, 3]), 12.0f);
    view[3, 0] = 99.0f;
    EXPECT_FLOAT_EQ(matrix.m[3][0], 99.0f);
}

TEST(mdspan_view, const_conversion_preserves_aliasing) {
    math::matrix3x3 matrix = math::matrix3x3::identity();
    const math::const_matrix3x3_mdspan view = math::as_mdspan(matrix);

    EXPECT_FLOAT_EQ((view[0, 0]), 1.0f);
    EXPECT_FLOAT_EQ((view[1, 2]), 0.0f);
}

TEST(mdspan_view, transpose_view_swaps_indices_without_copying) {
    math::matrix4x4 matrix{
        1,  2,  3,  4,
        5,  6,  7,  8,
        9, 10, 11, 12,
       13, 14, 15, 16};
    auto transposed = math::transpose_view(matrix);

    EXPECT_FLOAT_EQ((transposed[0, 3]), matrix.m[3][0]);
    EXPECT_FLOAT_EQ((transposed[2, 1]), matrix.m[1][2]);
    transposed[1, 3] = -14.0f;
    EXPECT_FLOAT_EQ(matrix.m[3][1], -14.0f);
}

#else

static_assert(!MATHEMATICS_HAS_MDSPAN);

TEST(mdspan_view, unavailable_before_cpp23) {
    GTEST_SKIP() << "std::mdspan is not available in this language/library mode";
}

#endif
