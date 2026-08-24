// matrix4x4 and matrix3x3.
//
// The convention tests matter more than the arithmetic ones here. A matrix
// library with row/column or pre/post-multiply backwards still multiplies
// without complaint and produces a self-consistent world that renders wrong, so
// the layout, the multiplication order, and the translation row are each pinned
// against DirectXMath rather than against Mathematics's own output.

#include "support/reg_testing.hpp"

#include <mathematics/matrix.hpp>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHEMATICS_TEST_HAS_DXMATH 1
#else
#  define MATHEMATICS_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace math_test;
using math::matrix3x3;
using math::matrix4x4;
using math::vector3;
using math::vector4;

// Distinct, non-symmetric entries: a transpose or index swap that went unnoticed
// with 1..16 in the wrong order would show up immediately.
constexpr matrix4x4 counting_matrix{ 1,  2,  3,  4,
                               5,  6,  7,  8,
                               9, 10, 11, 12,
                              13, 14, 15, 16};

matrix4x4 random_matrix(random_vectors& gen) {
    matrix4x4 r;
    for (int i = 0; i < 4; ++i) {
        const sample s = gen.next();
        for (int j = 0; j < 4; ++j) r.m[i][j] = s.f[j];
    }
    return r;
}

// Invertible by construction: a random matrix is almost surely non-singular,
// but "almost surely" is not a test guarantee, so the diagonal is biased.
matrix4x4 random_invertible_matrix(random_vectors& gen) {
    matrix4x4 r = random_matrix(gen);
    for (int i = 0; i < 4; ++i) r.m[i][i] += 200.0f;
    return r;
}

} // namespace

// ---------------------------------------------------------------------- layout
static_assert(sizeof(matrix4x4) == 64);
static_assert(sizeof(matrix3x3) == 36);

// Row-major: the first four floats in memory are the first ROW, not the first
// column. Everything else in the file depends on this being true.
TEST(matrix_layout, storage_is_row_major) {
    const float* raw = &counting_matrix.m[0][0];
    EXPECT_FLOAT_EQ(raw[0], 1.0f);
    EXPECT_FLOAT_EQ(raw[1], 2.0f) << "second float must be row 0 column 1";
    EXPECT_FLOAT_EQ(raw[4], 5.0f) << "fifth float must start row 1";

    EXPECT_TRUE(counting_matrix.get_row(0) == vector4(1, 2, 3, 4));
    EXPECT_TRUE(counting_matrix.get_column(0) == vector4(1, 5, 9, 13));
}

TEST(matrix_layout, element_access_agrees_with_storage) {
    EXPECT_FLOAT_EQ(counting_matrix(0, 1), 2.0f);
    EXPECT_FLOAT_EQ(counting_matrix(1, 0), 5.0f);
    EXPECT_FLOAT_EQ(counting_matrix(3, 3), 16.0f);
}

// ------------------------------------------------------------------- identity
static_assert(matrix4x4::identity()(0, 0) == 1.0f);
static_assert(matrix4x4::identity()(0, 1) == 0.0f);
static_assert(matrix4x4::identity() * matrix4x4::identity() ==
              matrix4x4::identity());

TEST(matrix_identity, is_a_multiplicative_identity) {
    EXPECT_TRUE(counting_matrix * matrix4x4::identity() == counting_matrix);
    EXPECT_TRUE(matrix4x4::identity() * counting_matrix == counting_matrix);
    EXPECT_TRUE(vector4(1, 2, 3, 4) * matrix4x4::identity() ==
                vector4(1, 2, 3, 4));
}

// ------------------------------------------------------------------- multiply
// Hand-computed, so it does not depend on any other operation being right.
TEST(matrix_multiply, matches_hand_computed_product) {
    constexpr matrix4x4 a{1, 2, 0, 0,
                          0, 1, 0, 0,
                          0, 0, 1, 0,
                          0, 0, 0, 1};
    constexpr matrix4x4 b{1, 0, 0, 0,
                          3, 1, 0, 0,
                          0, 0, 1, 0,
                          0, 0, 0, 1};
    // (a*b)[0] = 1*b.row0 + 2*b.row1 = (1,0,0,0) + (6,2,0,0) = (7,2,0,0)
    const matrix4x4 ab = a * b;
    EXPECT_TRUE(ab.get_row(0) == vector4(7, 2, 0, 0));
    EXPECT_TRUE(ab.get_row(1) == vector4(3, 1, 0, 0));
}

// Matrix multiplication does not commute, and a test built only from symmetric
// or diagonal inputs would never notice an implementation that swapped its
// operands.
TEST(matrix_multiply, does_not_commute) {
    constexpr matrix4x4 a{1, 2, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    constexpr matrix4x4 b{1, 0, 0, 0, 3, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    EXPECT_FALSE(a * b == b * a);
}

// detail::multiply_scalar runs ONLY during constant evaluation -- every build
// configuration takes multiply_avx or combine_rows at run time -- and the only
// constant-evaluated product before this was identity times identity, which is
// symmetric and would survive a swapped row/column index untouched. This is the
// same shape of hole that left vector4's normalize_wide unexecuted in Phase 2.
TEST(matrix_multiply, compile_time_matches_runtime) {
    constexpr matrix4x4 a{1, 2, 0, 0,
                          0, 1, 0, 0,
                          0, 0, 1, 0,
                          0, 0, 0, 1};
    constexpr matrix4x4 b{1, 0, 0, 0,
                          3, 1, 0, 0,
                          0, 0, 1, 0,
                          0, 0, 0, 1};
    constexpr matrix4x4 compile_time = a * b;
    static_assert(compile_time.m[0][0] == 7.0f);
    static_assert(compile_time.m[0][1] == 2.0f);
    static_assert(compile_time.m[1][0] == 3.0f);
    // bit_not commutative, so this also pins which operand is which.
    static_assert((b * a).m[0][0] == 1.0f);

    const matrix4x4 run_time = a * b;
    EXPECT_TRUE(compile_time == run_time);
}

// Small integers throughout, so every product is exact in float and the two
// paths must agree bit for bit rather than merely closely -- a rounding
// tolerance here would hide an index bug in the constexpr path.
TEST(matrix_multiply, compile_time_matches_runtime_on_a_full_matrix) {
    constexpr matrix4x4 a{ 1,  2,  3,  4,
                           5,  6,  7,  8,
                           9, 10, 11, 12,
                          13, 14, 15, 16};
    constexpr matrix4x4 b{ 2,  0,  1,  3,
                           1,  4,  0,  2,
                           0,  3,  5,  1,
                           6,  1,  2,  0};
    constexpr matrix4x4 compile_time = a * b;
    const matrix4x4 run_time = a * b;
    EXPECT_TRUE(compile_time == run_time)
        << "multiply_scalar disagrees with the active runtime backend";
}

TEST(matrix_multiply, is_associative) {
    random_vectors gen(random_seed + 90);
    for (int n = 0; n < 64; ++n) {
        const matrix4x4 a = random_matrix(gen);
        const matrix4x4 b = random_matrix(gen);
        const matrix4x4 c = random_matrix(gen);
        // Entries reach 1e6 after two products, so the tolerance scales with them.
        EXPECT_TRUE(math::near_equal((a * b) * c, a * (b * c), 1.0f)) << n;
    }
}

// -------------------------------------------------- convention: row vectors
// The defining property of the row-vector convention. Under the column-vector
// convention this test fails, which is the point of having it.
TEST(matrix_convention, vectors_are_row_vectors) {
    // A matrix whose row 0 is (0,1,0,0) sends x to y.
    constexpr matrix4x4 swap_xy{0, 1, 0, 0,
                               1, 0, 0, 0,
                               0, 0, 1, 0,
                               0, 0, 0, 1};
    EXPECT_TRUE(vector4(1, 0, 0, 0) * swap_xy == vector4(0, 1, 0, 0));
}

// translation lives in row 3. Putting it in column 3 -- the column-vector
// convention -- would leave every position untranslated.
TEST(matrix_convention, translation_lives_in_row_three) {
    constexpr matrix4x4 translate{1, 0, 0, 0,
                                  0, 1, 0, 0,
                                  0, 0, 1, 0,
                                  10, 20, 30, 1};
    EXPECT_TRUE(math::transform_point(vector3(1, 2, 3), translate) ==
                vector3(11, 22, 33));
    EXPECT_TRUE(translate.translation() == vector3(10, 20, 30));
}

// Composition reads left to right in application order.
TEST(matrix_convention, composition_applies_left_to_right) {
    constexpr matrix4x4 scale2{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1};
    constexpr matrix4x4 translate10{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                                    10, 0, 0, 1};

    // scale * translate: scale first, then translate. 1 -> 2 -> 12.
    EXPECT_TRUE(math::transform_point(vector3(1, 0, 0), scale2 * translate10) ==
                vector3(12, 0, 0));
    // The other order translates first, then scales that too. 1 -> 11 -> 22.
    EXPECT_TRUE(math::transform_point(vector3(1, 0, 0), translate10 * scale2) ==
                vector3(22, 0, 0));
}

// The whole reason the two transform functions have separate names.
TEST(matrix_convention, transform_direction_ignores_translation) {
    constexpr matrix4x4 translate{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                                  10, 20, 30, 1};
    EXPECT_TRUE(math::transform_direction(vector3(1, 0, 0), translate) ==
                vector3(1, 0, 0));
    EXPECT_TRUE(math::transform_point(vector3(1, 0, 0), translate) ==
                vector3(11, 20, 30));
}

// ----------------------------------------------------------------- comparison
// near_equal once reported a matrix of NaNs as near-equal to the identity. The
// test written as `diff > eps || diff < -eps` never fires on a NaN, because
// every comparison against a NaN is false -- so the loop fell through to
// "equal". That mattered well beyond the helper itself: almost every inverse and
// multiply assertion in this file goes through near_equal, so a bug producing a
// NaN made the surrounding test pass rather than fail.
TEST(matrix_near_equal, rejects_na_n) {
    matrix4x4 with_nan = matrix4x4::identity();
    with_nan.m[1][2] = quiet_nan();
    EXPECT_FALSE(math::near_equal(with_nan, matrix4x4::identity()));
    EXPECT_FALSE(math::near_equal(matrix4x4::identity(), with_nan));

    matrix4x4 all_nan;
    for (auto& row : all_nan.m) for (auto& e : row) e = quiet_nan();
    EXPECT_FALSE(math::near_equal(all_nan, matrix4x4::identity()));
    EXPECT_FALSE(math::near_equal(all_nan, all_nan))
        << "a NaN is near nothing, not even itself";

    matrix3x3 nan3 = matrix3x3::identity();
    nan3.m[0][1] = quiet_nan();
    EXPECT_FALSE(math::near_equal(nan3, matrix3x3::identity()));

    // The scenario the helper exists to catch: a NaN anywhere in a product must
    // fail an identity check, not slip through it.
    constexpr matrix4x4 m{2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 8, 0, 0, 0, 0, 1};
    matrix4x4 spoiled = inverse(m);
    spoiled.m[2][2] = quiet_nan();
    EXPECT_FALSE(math::near_equal(m * spoiled, matrix4x4::identity(), 1e-3f));
}

TEST(matrix_near_equal, accepts_what_it_should) {
    EXPECT_TRUE(math::near_equal(counting_matrix, counting_matrix));
    matrix4x4 nudged = counting_matrix;
    nudged.m[2][1] += 1e-7f;
    EXPECT_TRUE(math::near_equal(nudged, counting_matrix));
    nudged.m[2][1] += 1.0f;
    EXPECT_FALSE(math::near_equal(nudged, counting_matrix));
}

// ------------------------------------------------------------------ transpose
static_assert(transpose(matrix4x4::identity()) == matrix4x4::identity());

TEST(matrix_transpose, swaps_rows_and_columns) {
    const matrix4x4 t = transpose(counting_matrix);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            EXPECT_FLOAT_EQ(t.m[i][j], counting_matrix.m[j][i]) << i << "," << j;
        }
    }
}

TEST(matrix_transpose, is_its_own_inverse) {
    EXPECT_TRUE(transpose(transpose(counting_matrix)) == counting_matrix);
}

// ---------------------------------------------------------------- determinant
static_assert(determinant(matrix4x4::identity()) == 1.0f);
static_assert(determinant(matrix3x3::identity()) == 1.0f);

TEST(matrix_determinant, known_values) {
    // A diagonal matrix's determinant is the product of its diagonal.
    constexpr matrix4x4 diagonal{2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 5};
    EXPECT_FLOAT_EQ(determinant(diagonal), 120.0f);

    // The counting matrix is singular -- its rows are linearly dependent.
    EXPECT_NEAR(determinant(counting_matrix), 0.0f, 1e-3f);

    EXPECT_FLOAT_EQ(determinant(matrix3x3{2, 0, 0, 0, 3, 0, 0, 0, 4}), 24.0f);
}

TEST(matrix_determinant, is_multiplicative) {
    random_vectors gen(random_seed + 91);
    for (int n = 0; n < 32; ++n) {
        const matrix4x4 a = random_invertible_matrix(gen);
        const matrix4x4 b = random_invertible_matrix(gen);
        const float lhs = determinant(a * b);
        const float rhs = determinant(a) * determinant(b);
        EXPECT_NEAR(lhs, rhs, std::abs(rhs) * 1e-3f) << n;
    }
}

// -------------------------------------------------------------------- inverse
TEST(matrix_inverse, times_the_original_is_identity) {
    random_vectors gen(random_seed + 92);
    for (int n = 0; n < 64; ++n) {
        const matrix4x4 a = random_invertible_matrix(gen);
        EXPECT_TRUE(math::near_equal(a * inverse(a), matrix4x4::identity(), 1e-3f))
            << n;
        EXPECT_TRUE(math::near_equal(inverse(a) * a, matrix4x4::identity(), 1e-3f))
            << n;
    }
}

TEST(matrix_inverse, inverts_a_transform) {
    constexpr matrix4x4 transform{2, 0, 0, 0,
                                  0, 3, 0, 0,
                                  0, 0, 4, 0,
                                  10, 20, 30, 1};
    const matrix4x4 back = inverse(transform);
    const vector3 point{1, 2, 3};
    EXPECT_TRUE(math::near_equal(
        math::transform_point(math::transform_point(point, transform), back),
        point, 1e-4f));
}

// Documented behaviour, and different from DirectXMath's, so it gets a test of
// its own rather than being left to whatever the arithmetic produces.
TEST(matrix_inverse, singular_returns_identity) {
    EXPECT_TRUE(inverse(counting_matrix) == matrix4x4::identity());
    EXPECT_TRUE(inverse(matrix3x3{1, 2, 3, 2, 4, 6, 7, 8, 9}) ==
                matrix3x3::identity());
}

// inverse has two implementations -- a vectorized one for run time and the
// scalar Laplace expansion it was derived from, which also serves constant
// evaluation and the scalar backend. Two implementations of one contract need
// checking against each other, not just against DirectXMath: a shared
// misunderstanding of the convention would pass a self-comparison, and a
// transcription slip in the vector version would pass nothing else.
static_assert(inverse(matrix4x4::identity()) == matrix4x4::identity());
static_assert(inverse(matrix4x4{2, 0, 0, 0,
                                0, 4, 0, 0,
                                0, 0, 8, 0,
                                0, 0, 0, 1})(1, 1) == 0.25f);

// The two above are diagonal, so every off-diagonal cofactor is zero and a sign
// or minor-index error in the expansion contributes nothing to check. This one
// is not: it has a non-zero entry in every off-diagonal position that the
// complementary-minor expansion touches, and determinant 1, so the exact inverse
// is representable and the assertion can be equality.
namespace {
constexpr matrix4x4 unit_triangular_matrix{1, 0, 0, 0,
                                    2, 1, 0, 0,
                                    3, 4, 1, 0,
                                    5, 6, 7, 1};
}
static_assert(determinant(unit_triangular_matrix) == 1.0f);
static_assert(determinant(transpose(unit_triangular_matrix)) == 1.0f);
static_assert(inverse(unit_triangular_matrix) * unit_triangular_matrix ==
              matrix4x4::identity());
static_assert(inverse(unit_triangular_matrix)(3, 0) == -28.0f);
static_assert(inverse(unit_triangular_matrix)(3, 1) == 22.0f);
static_assert(inverse(unit_triangular_matrix)(2, 0) == 5.0f);

TEST(matrix_inverse, vector_and_scalar_paths_agree) {
    random_vectors gen(random_seed + 94);
    for (int n = 0; n < 128; ++n) {
        const matrix4x4 a = random_invertible_matrix(gen);
        EXPECT_TRUE(math::near_equal(inverse(a), math::detail::inverse_scalar(a),
                                     1e-5f)) << n;
    }
}

// The compile-time path is the scalar one, so a constexpr result and a runtime
// result on the same input come from different code and must still agree.
TEST(matrix_inverse, compile_time_matches_runtime) {
    constexpr matrix4x4 source{2, 1, 0, 0,
                               0, 3, 1, 0,
                               0, 0, 4, 1,
                               1, 0, 0, 5};
    constexpr matrix4x4 compile_time = inverse(source);
    const matrix4x4 run_time = inverse(source);
    EXPECT_TRUE(math::near_equal(compile_time, run_time, 1e-5f));
}

// A hand-computed inverse, with no reference library involved. Every other
// inverse test either compares the two implementations to each other or leans on
// DirectXMath, and DirectXMath is only available on the Windows legs -- so on
// Linux ARM64 nothing outside Mathematics ever checked this answer. The matrix is
// lower-triangular with a unit diagonal, whose inverse is exact in float, so
// this can assert equality rather than nearness.
TEST(matrix_inverse, matches_hand_computed_inverse) {
    constexpr matrix4x4 m{1, 0, 0, 0,
                          2, 1, 0, 0,
                          3, 4, 1, 0,
                          5, 6, 7, 1};
    // inverse of a unit lower-triangular matrix, by forward substitution:
    //   row1: -2
    //   row2: -(3) - (4)(-2) = 5,  -4
    //   row3: -(5) - 6(-2) - 7(5) = -28,  -(6) - 7(-4) = 22,  -7
    constexpr matrix4x4 expected{ 1,  0,  0, 0,
                                 -2,  1,  0, 0,
                                  5, -4,  1, 0,
                                -28, 22, -7, 1};
    EXPECT_TRUE(inverse(m) == expected);
    EXPECT_TRUE(math::detail::inverse_scalar(m) == expected);
    static_assert(inverse(m) == expected);
}

namespace {

// A 4x4 inverse in double by Gauss-Jordan with partial pivoting. This shares no
// algebra at all with a cofactor expansion, which is the point: the tests either
// compare Mathematics's two implementations to each other -- and both descend from the
// same Laplace expansion, so a shared misunderstanding survives -- or lean on
// DirectXMath, which only exists on the Windows legs. On Linux ARM64 nothing
// outside Mathematics checked these answers at all.
struct double_inverse {
    double m[4][4];
    bool ok;
};

double_inverse reference_inverse(const matrix4x4& src) {
    double a[4][8] = {};
    double scale = 0.0;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[i][j] = static_cast<double>(src.m[i][j]);
            scale = std::fmax(scale, std::fabs(a[i][j]));
        }
        a[i][4 + i] = 1.0;
    }
    if (scale == 0.0) return double_inverse{{}, false};

    for (int col = 0; col < 4; ++col) {
        int piv = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        }
        // Relative, not `== 0`: elimination is inexact even in double, so an
        // exactly singular float matrix leaves a tiny pivot rather than a zero.
        if (std::fabs(a[piv][col]) < 1e-12 * scale) return double_inverse{{}, false};
        if (piv != col) {
            for (int j = 0; j < 8; ++j) std::swap(a[col][j], a[piv][j]);
        }
        const double inv = 1.0 / a[col][col];
        for (int j = 0; j < 8; ++j) a[col][j] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const double f = a[r][col];
            if (f == 0.0) continue;
            for (int j = 0; j < 8; ++j) a[r][j] -= f * a[col][j];
        }
    }

    double_inverse out{};
    out.ok = true;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out.m[i][j] = a[i][4 + j];
    return out;
}

} // namespace

// Runs on every backend including NEON, where no external library is available.
TEST(matrix_inverse, matches_double_precision_gauss_jordan) {
    random_vectors gen(random_seed + 95);
    int checked = 0;
    double worst = 0.0;

    for (int n = 0; n < 512; ++n) {
        const matrix4x4 a = random_invertible_matrix(gen);
        const double_inverse ref = reference_inverse(a);
        ASSERT_TRUE(ref.ok) << n << ": diagonal-biased matrix should invert";

        double scale = 0.0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                scale = std::fmax(scale, std::fabs(ref.m[i][j]));

        const matrix4x4 got = inverse(a);
        double err = 0.0;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                err = std::fmax(err, std::fabs(static_cast<double>(got.m[i][j]) -
                                               ref.m[i][j]));
            }
        }
        // Relative to the largest entry of the inverse, not to each entry: the
        // small entries of an inverse carry the accumulated error of the large
        // ones and judging them individually would be a tolerance on noise.
        worst = std::fmax(worst, err / std::fmax(scale, 1e-30));
        ++checked;
    }

    EXPECT_EQ(checked, 512);
    EXPECT_LT(worst, 1e-4) << "worst relative error against the double reference";
}

// Degenerate inputs. These are the singular matrices that actually turn up --
// an uninitialized bone, a zero scale, a duplicated row -- and unlike a matrix
// that is merely close to singular, every one of them drives the determinant to
// exactly zero in both implementations, so the two agree.
TEST(matrix_inverse, realistic_singular_inputs_return_identity) {
    const matrix4x4 cases[] = {
        {0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},   // zero row
        {1, 2, 3, 4, 1, 2, 3, 4, 9, 10, 11, 13, 14, 15, 17, 19},  // duplicate
        {1, 2, 3, 4, 5, 6, 7, 8, 6, 8, 10, 12, 1, 0, 0, 1},    // row2 = r0 + r1
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // all zero
        {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 1},      // zero scale on Y
    };
    for (int n = 0; n < static_cast<int>(std::size(cases)); ++n) {
        EXPECT_FLOAT_EQ(determinant(cases[n]), 0.0f) << n;
        EXPECT_TRUE(inverse(cases[n]) == matrix4x4::identity()) << n;
        EXPECT_TRUE(math::detail::inverse_scalar(cases[n]) ==
                    matrix4x4::identity()) << n;
    }
}

// The guard rejects any determinant that is not a finite non-zero, not merely an
// exact zero. Without that, a matrix holding an infinity divided by a NaN
// determinant and returned sixteen NaNs from the scalar path while the vector
// path returned zeros -- so inverse answered differently at compile time than at
// run time, and differently on a scalar build than on an SSE one.
TEST(matrix_inverse, non_finite_and_overflowing_inputs_return_identity) {
    const float inf = std::numeric_limits<float>::infinity();
    const matrix4x4 cases[] = {
        {inf, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
        {quiet_nan(), 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, inf, 0, 0, 1},
        // determinant overflows to infinity though every entry is finite.
        {1e10f, 0, 0, 0, 0, 1e10f, 0, 0, 0, 0, 1e10f, 0, 0, 0, 0, 1e10f},
    };
    for (int n = 0; n < static_cast<int>(std::size(cases)); ++n) {
        EXPECT_TRUE(inverse(cases[n]) == matrix4x4::identity()) << n;
        EXPECT_TRUE(math::detail::inverse_scalar(cases[n]) ==
                    matrix4x4::identity()) << n
            << " -- the two paths must agree here, not just each be defensible";
    }

    EXPECT_TRUE(math::inverse(matrix3x3{inf, 0, 0, 0, 1, 0, 0, 0, 1}) ==
                matrix3x3::identity());
}

TEST(matrix3x3_inverse, times_the_original_is_identity) {
    random_vectors gen(random_seed + 93);
    for (int n = 0; n < 64; ++n) {
        matrix3x3 a;
        const sample s0 = gen.next(), s1 = gen.next(), s2 = gen.next();
        for (int j = 0; j < 3; ++j) {
            a.m[0][j] = s0.f[j];
            a.m[1][j] = s1.f[j];
            a.m[2][j] = s2.f[j];
        }
        for (int i = 0; i < 3; ++i) a.m[i][i] += 200.0f;

        EXPECT_TRUE(math::near_equal(a * inverse(a), matrix3x3::identity(), 1e-3f))
            << n;
    }
}

TEST(matrix3x3, transpose_and_multiply) {
    constexpr matrix3x3 a{1, 2, 3, 4, 5, 6, 7, 8, 9};
    const matrix3x3 t = transpose(a);
    EXPECT_FLOAT_EQ(t(0, 1), 4.0f);
    EXPECT_FLOAT_EQ(t(1, 0), 2.0f);
    EXPECT_TRUE(a * matrix3x3::identity() == a);
    EXPECT_TRUE(vector3(1, 0, 0) * a == vector3(1, 2, 3))
        << "a row vector picks out row 0";
}

// Every matrix3x3 operation is marked constexpr; nothing proved any of them
// could actually be used in a constant expression. Operands are non-symmetric
// and non-diagonal, so an index swap or a cofactor sign error has somewhere to
// show up -- a diagonal matrix would hide both.
namespace {
constexpr matrix3x3 asymmetric_matrix3{1, 2, 3,
                           0, 1, 4,
                           5, 6, 0};
}
static_assert(transpose(asymmetric_matrix3)(0, 1) == 0.0f);
static_assert(transpose(asymmetric_matrix3)(2, 0) == 3.0f);
static_assert(determinant(asymmetric_matrix3) == 1.0f);
// determinant 1 and integer entries, so the inverse is exact in float.
static_assert(inverse(asymmetric_matrix3) == matrix3x3{-24,  18,   5,
                                            20, -15,  -4,
                                            -5,   4,   1});
static_assert((asymmetric_matrix3 * matrix3x3::identity()) == asymmetric_matrix3);
static_assert((vector3(1, 0, 0) * asymmetric_matrix3) == vector3(1, 2, 3));

// ---------------------------------------------------------- DirectXMath parity
// The convention checks above say Mathematics is self-consistent. These say it agrees
// with DirectXMath, which is what makes a matrix built by one usable by the
// other.
#if MATHEMATICS_TEST_HAS_DXMATH
namespace {

DirectX::XMMATRIX to_xm(const matrix4x4& mat) {
    return DirectX::XMMatrixSet(
        mat.m[0][0], mat.m[0][1], mat.m[0][2], mat.m[0][3],
        mat.m[1][0], mat.m[1][1], mat.m[1][2], mat.m[1][3],
        mat.m[2][0], mat.m[2][1], mat.m[2][2], mat.m[2][3],
        mat.m[3][0], mat.m[3][1], mat.m[3][2], mat.m[3][3]);
}

matrix4x4 from_xm(DirectX::FXMMATRIX xm) {
    DirectX::XMFLOAT4X4 out{};
    DirectX::XMStoreFloat4x4(&out, xm);
    matrix4x4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) r.m[i][j] = out.m[i][j];
    }
    return r;
}

} // namespace

// If XMFLOAT4X4's memory layout did not match matrix4x4's, everything below
// would still "pass" while describing different matrices. Checked first.
TEST(matrix_dx_parity, storage_layout_matches_xmfloat4_x4) {
    DirectX::XMFLOAT4X4 xf{};
    DirectX::XMStoreFloat4x4(&xf, to_xm(counting_matrix));
    const float* mine = &counting_matrix.m[0][0];
    const float* theirs = &xf.m[0][0];
    for (int i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(mine[i], theirs[i]) << "float " << i;
    }
}

TEST(matrix_dx_parity, multiply_matches_direct_x_math) {
    random_vectors gen(random_seed + 100);
    for (int n = 0; n < 64; ++n) {
        const matrix4x4 a = random_matrix(gen);
        const matrix4x4 b = random_matrix(gen);
        EXPECT_TRUE(math::near_equal(
            a * b, from_xm(DirectX::XMMatrixMultiply(to_xm(a), to_xm(b))), 1e-1f))
            << n;
    }
}

TEST(matrix_dx_parity, transpose_and_determinant_match_direct_x_math) {
    random_vectors gen(random_seed + 101);
    for (int n = 0; n < 64; ++n) {
        const matrix4x4 a = random_invertible_matrix(gen);

        EXPECT_TRUE(math::near_equal(
            transpose(a), from_xm(DirectX::XMMatrixTranspose(to_xm(a))))) << n;

        const float theirs =
            DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(to_xm(a)));
        EXPECT_NEAR(determinant(a), theirs, std::abs(theirs) * 1e-3f) << n;
    }
}

TEST(matrix_dx_parity, inverse_matches_direct_x_math) {
    random_vectors gen(random_seed + 102);
    for (int n = 0; n < 64; ++n) {
        const matrix4x4 a = random_invertible_matrix(gen);
        DirectX::XMVECTOR det;
        const matrix4x4 theirs =
            from_xm(DirectX::XMMatrixInverse(&det, to_xm(a)));
        EXPECT_TRUE(math::near_equal(inverse(a), theirs, 1e-4f)) << n;
    }
}

// The convention test that matters most: a vector transformed by Mathematics and by
// DirectXMath through the same matrix must land in the same place.
TEST(matrix_dx_parity, vector_transform_matches_direct_x_math) {
    random_vectors gen(random_seed + 103);
    for (int n = 0; n < 64; ++n) {
        const matrix4x4 a = random_matrix(gen);
        const sample s = gen.next();
        const vector4 v{s.f[0], s.f[1], s.f[2], s.f[3]};

        DirectX::XMFLOAT4 out{};
        DirectX::XMStoreFloat4(
            &out, DirectX::XMVector4Transform(
                      DirectX::XMVectorSet(v.x, v.y, v.z, v.w), to_xm(a)));

        const vector4 mine = v * a;
        EXPECT_TRUE(math::near_equal(mine, vector4(out.x, out.y, out.z, out.w),
                                     std::abs(out.x) * 1e-4f + 1e-2f)) << n;
    }
}

TEST(matrix_dx_parity, point_and_direction_transforms_match_direct_x_math) {
    random_vectors gen(random_seed + 104);
    for (int n = 0; n < 64; ++n) {
        const matrix4x4 a = random_matrix(gen);
        const sample s = gen.next();
        const vector3 v{s.f[0], s.f[1], s.f[2]};
        const DirectX::XMVECTOR xv = DirectX::XMVectorSet(v.x, v.y, v.z, 0.0f);

        DirectX::XMFLOAT3 point{};
        DirectX::XMStoreFloat3(
            &point, DirectX::XMVector3TransformCoord(xv, to_xm(a)));
        DirectX::XMFLOAT3 direction{};
        DirectX::XMStoreFloat3(
            &direction, DirectX::XMVector3TransformNormal(xv, to_xm(a)));

        // TransformCoord divides by w; this matrix's bottom row is arbitrary, so
        // the comparison only holds where w came out as one. Use an affine
        // matrix for the coordinate check instead.
        matrix4x4 affine = a;
        affine.m[0][3] = 0.0f; affine.m[1][3] = 0.0f;
        affine.m[2][3] = 0.0f; affine.m[3][3] = 1.0f;
        DirectX::XMStoreFloat3(
            &point, DirectX::XMVector3TransformCoord(xv, to_xm(affine)));

        EXPECT_TRUE(math::near_equal(math::transform_point(v, affine),
                                     vector3(point.x, point.y, point.z),
                                     1e-2f)) << n;
        EXPECT_TRUE(math::near_equal(math::transform_direction(v, a),
                                     vector3(direction.x, direction.y,
                                             direction.z),
                                     1e-2f)) << n;
    }
}
#endif // MATHEMATICS_TEST_HAS_DXMATH
