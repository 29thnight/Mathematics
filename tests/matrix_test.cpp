// Matrix4x4 and Matrix3x3.
//
// The convention tests matter more than the arithmetic ones here. A matrix
// library with row/column or pre/post-multiply backwards still multiplies
// without complaint and produces a self-consistent world that renders wrong, so
// the layout, the multiplication order, and the translation row are each pinned
// against DirectXMath rather than against Mathf's own output.

#include "support/reg_testing.hpp"

#include <mathf/matrix.hpp>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHF_TEST_HAS_DXMATH 1
#else
#  define MATHF_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace mathf_test;
using mathf::Matrix3x3;
using mathf::Matrix4x4;
using mathf::Vector3;
using mathf::Vector4;

// Distinct, non-symmetric entries: a transpose or index swap that went unnoticed
// with 1..16 in the wrong order would show up immediately.
constexpr Matrix4x4 kCounting{ 1,  2,  3,  4,
                               5,  6,  7,  8,
                               9, 10, 11, 12,
                              13, 14, 15, 16};

Matrix4x4 RandomMatrix(RandomVectors& gen) {
    Matrix4x4 r;
    for (int i = 0; i < 4; ++i) {
        const Sample s = gen.Next();
        for (int j = 0; j < 4; ++j) r.m[i][j] = s.f[j];
    }
    return r;
}

// Invertible by construction: a random matrix is almost surely non-singular,
// but "almost surely" is not a test guarantee, so the diagonal is biased.
Matrix4x4 RandomInvertibleMatrix(RandomVectors& gen) {
    Matrix4x4 r = RandomMatrix(gen);
    for (int i = 0; i < 4; ++i) r.m[i][i] += 200.0f;
    return r;
}

} // namespace

// ---------------------------------------------------------------------- layout
static_assert(sizeof(Matrix4x4) == 64);
static_assert(sizeof(Matrix3x3) == 36);

// Row-major: the first four floats in memory are the first ROW, not the first
// column. Everything else in the file depends on this being true.
TEST(MatrixLayout, StorageIsRowMajor) {
    const float* raw = &kCounting.m[0][0];
    EXPECT_FLOAT_EQ(raw[0], 1.0f);
    EXPECT_FLOAT_EQ(raw[1], 2.0f) << "second float must be row 0 column 1";
    EXPECT_FLOAT_EQ(raw[4], 5.0f) << "fifth float must start row 1";

    EXPECT_TRUE(kCounting.GetRow(0) == Vector4(1, 2, 3, 4));
    EXPECT_TRUE(kCounting.GetColumn(0) == Vector4(1, 5, 9, 13));
}

TEST(MatrixLayout, ElementAccessAgreesWithStorage) {
    EXPECT_FLOAT_EQ(kCounting(0, 1), 2.0f);
    EXPECT_FLOAT_EQ(kCounting(1, 0), 5.0f);
    EXPECT_FLOAT_EQ(kCounting(3, 3), 16.0f);
}

// ------------------------------------------------------------------- identity
static_assert(Matrix4x4::Identity()(0, 0) == 1.0f);
static_assert(Matrix4x4::Identity()(0, 1) == 0.0f);
static_assert(Matrix4x4::Identity() * Matrix4x4::Identity() ==
              Matrix4x4::Identity());

TEST(MatrixIdentity, IsAMultiplicativeIdentity) {
    EXPECT_TRUE(kCounting * Matrix4x4::Identity() == kCounting);
    EXPECT_TRUE(Matrix4x4::Identity() * kCounting == kCounting);
    EXPECT_TRUE(Vector4(1, 2, 3, 4) * Matrix4x4::Identity() ==
                Vector4(1, 2, 3, 4));
}

// ------------------------------------------------------------------- multiply
// Hand-computed, so it does not depend on any other operation being right.
TEST(MatrixMultiply, MatchesHandComputedProduct) {
    constexpr Matrix4x4 a{1, 2, 0, 0,
                          0, 1, 0, 0,
                          0, 0, 1, 0,
                          0, 0, 0, 1};
    constexpr Matrix4x4 b{1, 0, 0, 0,
                          3, 1, 0, 0,
                          0, 0, 1, 0,
                          0, 0, 0, 1};
    // (a*b)[0] = 1*b.row0 + 2*b.row1 = (1,0,0,0) + (6,2,0,0) = (7,2,0,0)
    const Matrix4x4 ab = a * b;
    EXPECT_TRUE(ab.GetRow(0) == Vector4(7, 2, 0, 0));
    EXPECT_TRUE(ab.GetRow(1) == Vector4(3, 1, 0, 0));
}

// Matrix multiplication does not commute, and a test built only from symmetric
// or diagonal inputs would never notice an implementation that swapped its
// operands.
TEST(MatrixMultiply, DoesNotCommute) {
    constexpr Matrix4x4 a{1, 2, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    constexpr Matrix4x4 b{1, 0, 0, 0, 3, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    EXPECT_FALSE(a * b == b * a);
}

// detail::MultiplyScalar runs ONLY during constant evaluation -- every build
// configuration takes MultiplyAvx or CombineRows at run time -- and the only
// constant-evaluated product before this was identity times identity, which is
// symmetric and would survive a swapped row/column index untouched. This is the
// same shape of hole that left Vector4's NormalizeWide unexecuted in Phase 2.
TEST(MatrixMultiply, CompileTimeMatchesRuntime) {
    constexpr Matrix4x4 a{1, 2, 0, 0,
                          0, 1, 0, 0,
                          0, 0, 1, 0,
                          0, 0, 0, 1};
    constexpr Matrix4x4 b{1, 0, 0, 0,
                          3, 1, 0, 0,
                          0, 0, 1, 0,
                          0, 0, 0, 1};
    constexpr Matrix4x4 compileTime = a * b;
    static_assert(compileTime.m[0][0] == 7.0f);
    static_assert(compileTime.m[0][1] == 2.0f);
    static_assert(compileTime.m[1][0] == 3.0f);
    // Not commutative, so this also pins which operand is which.
    static_assert((b * a).m[0][0] == 1.0f);

    const Matrix4x4 runTime = a * b;
    EXPECT_TRUE(compileTime == runTime);
}

// Small integers throughout, so every product is exact in float and the two
// paths must agree bit for bit rather than merely closely -- a rounding
// tolerance here would hide an index bug in the constexpr path.
TEST(MatrixMultiply, CompileTimeMatchesRuntimeOnAFullMatrix) {
    constexpr Matrix4x4 a{ 1,  2,  3,  4,
                           5,  6,  7,  8,
                           9, 10, 11, 12,
                          13, 14, 15, 16};
    constexpr Matrix4x4 b{ 2,  0,  1,  3,
                           1,  4,  0,  2,
                           0,  3,  5,  1,
                           6,  1,  2,  0};
    constexpr Matrix4x4 compileTime = a * b;
    const Matrix4x4 runTime = a * b;
    EXPECT_TRUE(compileTime == runTime)
        << "MultiplyScalar disagrees with the active runtime backend";
}

TEST(MatrixMultiply, IsAssociative) {
    RandomVectors gen(kSeed + 90);
    for (int n = 0; n < 64; ++n) {
        const Matrix4x4 a = RandomMatrix(gen);
        const Matrix4x4 b = RandomMatrix(gen);
        const Matrix4x4 c = RandomMatrix(gen);
        // Entries reach 1e6 after two products, so the tolerance scales with them.
        EXPECT_TRUE(mathf::NearEqual((a * b) * c, a * (b * c), 1.0f)) << n;
    }
}

// -------------------------------------------------- convention: row vectors
// The defining property of the row-vector convention. Under the column-vector
// convention this test fails, which is the point of having it.
TEST(MatrixConvention, VectorsAreRowVectors) {
    // A matrix whose row 0 is (0,1,0,0) sends x to y.
    constexpr Matrix4x4 swapXY{0, 1, 0, 0,
                               1, 0, 0, 0,
                               0, 0, 1, 0,
                               0, 0, 0, 1};
    EXPECT_TRUE(Vector4(1, 0, 0, 0) * swapXY == Vector4(0, 1, 0, 0));
}

// Translation lives in row 3. Putting it in column 3 -- the column-vector
// convention -- would leave every position untranslated.
TEST(MatrixConvention, TranslationLivesInRowThree) {
    constexpr Matrix4x4 translate{1, 0, 0, 0,
                                  0, 1, 0, 0,
                                  0, 0, 1, 0,
                                  10, 20, 30, 1};
    EXPECT_TRUE(mathf::TransformPoint(Vector3(1, 2, 3), translate) ==
                Vector3(11, 22, 33));
    EXPECT_TRUE(translate.Translation() == Vector3(10, 20, 30));
}

// Composition reads left to right in application order.
TEST(MatrixConvention, CompositionAppliesLeftToRight) {
    constexpr Matrix4x4 scale2{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1};
    constexpr Matrix4x4 translate10{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                                    10, 0, 0, 1};

    // scale * translate: scale first, then translate. 1 -> 2 -> 12.
    EXPECT_TRUE(mathf::TransformPoint(Vector3(1, 0, 0), scale2 * translate10) ==
                Vector3(12, 0, 0));
    // The other order translates first, then scales that too. 1 -> 11 -> 22.
    EXPECT_TRUE(mathf::TransformPoint(Vector3(1, 0, 0), translate10 * scale2) ==
                Vector3(22, 0, 0));
}

// The whole reason the two transform functions have separate names.
TEST(MatrixConvention, TransformDirectionIgnoresTranslation) {
    constexpr Matrix4x4 translate{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                                  10, 20, 30, 1};
    EXPECT_TRUE(mathf::TransformDirection(Vector3(1, 0, 0), translate) ==
                Vector3(1, 0, 0));
    EXPECT_TRUE(mathf::TransformPoint(Vector3(1, 0, 0), translate) ==
                Vector3(11, 20, 30));
}

// ----------------------------------------------------------------- comparison
// NearEqual once reported a matrix of NaNs as near-equal to the identity. The
// test written as `diff > eps || diff < -eps` never fires on a NaN, because
// every comparison against a NaN is false -- so the loop fell through to
// "equal". That mattered well beyond the helper itself: almost every inverse and
// multiply assertion in this file goes through NearEqual, so a bug producing a
// NaN made the surrounding test pass rather than fail.
TEST(MatrixNearEqual, RejectsNaN) {
    Matrix4x4 withNan = Matrix4x4::Identity();
    withNan.m[1][2] = QuietNaN();
    EXPECT_FALSE(mathf::NearEqual(withNan, Matrix4x4::Identity()));
    EXPECT_FALSE(mathf::NearEqual(Matrix4x4::Identity(), withNan));

    Matrix4x4 allNan;
    for (auto& row : allNan.m) for (auto& e : row) e = QuietNaN();
    EXPECT_FALSE(mathf::NearEqual(allNan, Matrix4x4::Identity()));
    EXPECT_FALSE(mathf::NearEqual(allNan, allNan))
        << "a NaN is near nothing, not even itself";

    Matrix3x3 nan3 = Matrix3x3::Identity();
    nan3.m[0][1] = QuietNaN();
    EXPECT_FALSE(mathf::NearEqual(nan3, Matrix3x3::Identity()));

    // The scenario the helper exists to catch: a NaN anywhere in a product must
    // fail an identity check, not slip through it.
    constexpr Matrix4x4 m{2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 8, 0, 0, 0, 0, 1};
    Matrix4x4 spoiled = Inverse(m);
    spoiled.m[2][2] = QuietNaN();
    EXPECT_FALSE(mathf::NearEqual(m * spoiled, Matrix4x4::Identity(), 1e-3f));
}

TEST(MatrixNearEqual, AcceptsWhatItShould) {
    EXPECT_TRUE(mathf::NearEqual(kCounting, kCounting));
    Matrix4x4 nudged = kCounting;
    nudged.m[2][1] += 1e-7f;
    EXPECT_TRUE(mathf::NearEqual(nudged, kCounting));
    nudged.m[2][1] += 1.0f;
    EXPECT_FALSE(mathf::NearEqual(nudged, kCounting));
}

// ------------------------------------------------------------------ transpose
static_assert(Transpose(Matrix4x4::Identity()) == Matrix4x4::Identity());

TEST(MatrixTranspose, SwapsRowsAndColumns) {
    const Matrix4x4 t = Transpose(kCounting);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            EXPECT_FLOAT_EQ(t.m[i][j], kCounting.m[j][i]) << i << "," << j;
        }
    }
}

TEST(MatrixTranspose, IsItsOwnInverse) {
    EXPECT_TRUE(Transpose(Transpose(kCounting)) == kCounting);
}

// ---------------------------------------------------------------- determinant
static_assert(Determinant(Matrix4x4::Identity()) == 1.0f);
static_assert(Determinant(Matrix3x3::Identity()) == 1.0f);

TEST(MatrixDeterminant, KnownValues) {
    // A diagonal matrix's determinant is the product of its diagonal.
    constexpr Matrix4x4 diagonal{2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 5};
    EXPECT_FLOAT_EQ(Determinant(diagonal), 120.0f);

    // The counting matrix is singular -- its rows are linearly dependent.
    EXPECT_NEAR(Determinant(kCounting), 0.0f, 1e-3f);

    EXPECT_FLOAT_EQ(Determinant(Matrix3x3{2, 0, 0, 0, 3, 0, 0, 0, 4}), 24.0f);
}

TEST(MatrixDeterminant, IsMultiplicative) {
    RandomVectors gen(kSeed + 91);
    for (int n = 0; n < 32; ++n) {
        const Matrix4x4 a = RandomInvertibleMatrix(gen);
        const Matrix4x4 b = RandomInvertibleMatrix(gen);
        const float lhs = Determinant(a * b);
        const float rhs = Determinant(a) * Determinant(b);
        EXPECT_NEAR(lhs, rhs, std::abs(rhs) * 1e-3f) << n;
    }
}

// -------------------------------------------------------------------- inverse
TEST(MatrixInverse, TimesTheOriginalIsIdentity) {
    RandomVectors gen(kSeed + 92);
    for (int n = 0; n < 64; ++n) {
        const Matrix4x4 a = RandomInvertibleMatrix(gen);
        EXPECT_TRUE(mathf::NearEqual(a * Inverse(a), Matrix4x4::Identity(), 1e-3f))
            << n;
        EXPECT_TRUE(mathf::NearEqual(Inverse(a) * a, Matrix4x4::Identity(), 1e-3f))
            << n;
    }
}

TEST(MatrixInverse, InvertsATransform) {
    constexpr Matrix4x4 transform{2, 0, 0, 0,
                                  0, 3, 0, 0,
                                  0, 0, 4, 0,
                                  10, 20, 30, 1};
    const Matrix4x4 back = Inverse(transform);
    const Vector3 point{1, 2, 3};
    EXPECT_TRUE(mathf::NearEqual(
        mathf::TransformPoint(mathf::TransformPoint(point, transform), back),
        point, 1e-4f));
}

// Documented behaviour, and different from DirectXMath's, so it gets a test of
// its own rather than being left to whatever the arithmetic produces.
TEST(MatrixInverse, SingularReturnsIdentity) {
    EXPECT_TRUE(Inverse(kCounting) == Matrix4x4::Identity());
    EXPECT_TRUE(Inverse(Matrix3x3{1, 2, 3, 2, 4, 6, 7, 8, 9}) ==
                Matrix3x3::Identity());
}

// Inverse has two implementations -- a vectorized one for run time and the
// scalar Laplace expansion it was derived from, which also serves constant
// evaluation and the scalar backend. Two implementations of one contract need
// checking against each other, not just against DirectXMath: a shared
// misunderstanding of the convention would pass a self-comparison, and a
// transcription slip in the vector version would pass nothing else.
static_assert(Inverse(Matrix4x4::Identity()) == Matrix4x4::Identity());
static_assert(Inverse(Matrix4x4{2, 0, 0, 0,
                                0, 4, 0, 0,
                                0, 0, 8, 0,
                                0, 0, 0, 1})(1, 1) == 0.25f);

// The two above are diagonal, so every off-diagonal cofactor is zero and a sign
// or minor-index error in the expansion contributes nothing to check. This one
// is not: it has a non-zero entry in every off-diagonal position that the
// complementary-minor expansion touches, and determinant 1, so the exact inverse
// is representable and the assertion can be equality.
namespace {
constexpr Matrix4x4 kUnitTriangular{1, 0, 0, 0,
                                    2, 1, 0, 0,
                                    3, 4, 1, 0,
                                    5, 6, 7, 1};
}
static_assert(Determinant(kUnitTriangular) == 1.0f);
static_assert(Determinant(Transpose(kUnitTriangular)) == 1.0f);
static_assert(Inverse(kUnitTriangular) * kUnitTriangular ==
              Matrix4x4::Identity());
static_assert(Inverse(kUnitTriangular)(3, 0) == -28.0f);
static_assert(Inverse(kUnitTriangular)(3, 1) == 22.0f);
static_assert(Inverse(kUnitTriangular)(2, 0) == 5.0f);

TEST(MatrixInverse, VectorAndScalarPathsAgree) {
    RandomVectors gen(kSeed + 94);
    for (int n = 0; n < 128; ++n) {
        const Matrix4x4 a = RandomInvertibleMatrix(gen);
        EXPECT_TRUE(mathf::NearEqual(Inverse(a), mathf::detail::InverseScalar(a),
                                     1e-5f)) << n;
    }
}

// The compile-time path is the scalar one, so a constexpr result and a runtime
// result on the same input come from different code and must still agree.
TEST(MatrixInverse, CompileTimeMatchesRuntime) {
    constexpr Matrix4x4 source{2, 1, 0, 0,
                               0, 3, 1, 0,
                               0, 0, 4, 1,
                               1, 0, 0, 5};
    constexpr Matrix4x4 compileTime = Inverse(source);
    const Matrix4x4 runTime = Inverse(source);
    EXPECT_TRUE(mathf::NearEqual(compileTime, runTime, 1e-5f));
}

// A hand-computed inverse, with no reference library involved. Every other
// inverse test either compares the two implementations to each other or leans on
// DirectXMath, and DirectXMath is only available on the Windows legs -- so on
// Linux ARM64 nothing outside Mathf ever checked this answer. The matrix is
// lower-triangular with a unit diagonal, whose inverse is exact in float, so
// this can assert equality rather than nearness.
TEST(MatrixInverse, MatchesHandComputedInverse) {
    constexpr Matrix4x4 m{1, 0, 0, 0,
                          2, 1, 0, 0,
                          3, 4, 1, 0,
                          5, 6, 7, 1};
    // Inverse of a unit lower-triangular matrix, by forward substitution:
    //   row1: -2
    //   row2: -(3) - (4)(-2) = 5,  -4
    //   row3: -(5) - 6(-2) - 7(5) = -28,  -(6) - 7(-4) = 22,  -7
    constexpr Matrix4x4 expected{ 1,  0,  0, 0,
                                 -2,  1,  0, 0,
                                  5, -4,  1, 0,
                                -28, 22, -7, 1};
    EXPECT_TRUE(Inverse(m) == expected);
    EXPECT_TRUE(mathf::detail::InverseScalar(m) == expected);
    static_assert(Inverse(m) == expected);
}

namespace {

// A 4x4 inverse in double by Gauss-Jordan with partial pivoting. This shares no
// algebra at all with a cofactor expansion, which is the point: the tests either
// compare Mathf's two implementations to each other -- and both descend from the
// same Laplace expansion, so a shared misunderstanding survives -- or lean on
// DirectXMath, which only exists on the Windows legs. On Linux ARM64 nothing
// outside Mathf checked these answers at all.
struct DoubleInverse {
    double m[4][4];
    bool ok;
};

DoubleInverse ReferenceInverse(const Matrix4x4& src) {
    double a[4][8] = {};
    double scale = 0.0;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[i][j] = static_cast<double>(src.m[i][j]);
            scale = std::fmax(scale, std::fabs(a[i][j]));
        }
        a[i][4 + i] = 1.0;
    }
    if (scale == 0.0) return DoubleInverse{{}, false};

    for (int col = 0; col < 4; ++col) {
        int piv = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        }
        // Relative, not `== 0`: elimination is inexact even in double, so an
        // exactly singular float matrix leaves a tiny pivot rather than a zero.
        if (std::fabs(a[piv][col]) < 1e-12 * scale) return DoubleInverse{{}, false};
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

    DoubleInverse out{};
    out.ok = true;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out.m[i][j] = a[i][4 + j];
    return out;
}

} // namespace

// Runs on every backend including NEON, where no external library is available.
TEST(MatrixInverse, MatchesDoublePrecisionGaussJordan) {
    RandomVectors gen(kSeed + 95);
    int checked = 0;
    double worst = 0.0;

    for (int n = 0; n < 512; ++n) {
        const Matrix4x4 a = RandomInvertibleMatrix(gen);
        const DoubleInverse ref = ReferenceInverse(a);
        ASSERT_TRUE(ref.ok) << n << ": diagonal-biased matrix should invert";

        double scale = 0.0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                scale = std::fmax(scale, std::fabs(ref.m[i][j]));

        const Matrix4x4 got = Inverse(a);
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
TEST(MatrixInverse, RealisticSingularInputsReturnIdentity) {
    const Matrix4x4 cases[] = {
        {0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},   // zero row
        {1, 2, 3, 4, 1, 2, 3, 4, 9, 10, 11, 13, 14, 15, 17, 19},  // duplicate
        {1, 2, 3, 4, 5, 6, 7, 8, 6, 8, 10, 12, 1, 0, 0, 1},    // row2 = r0 + r1
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // all zero
        {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 1},      // zero scale on Y
    };
    for (int n = 0; n < static_cast<int>(std::size(cases)); ++n) {
        EXPECT_FLOAT_EQ(Determinant(cases[n]), 0.0f) << n;
        EXPECT_TRUE(Inverse(cases[n]) == Matrix4x4::Identity()) << n;
        EXPECT_TRUE(mathf::detail::InverseScalar(cases[n]) ==
                    Matrix4x4::Identity()) << n;
    }
}

// The guard rejects any determinant that is not a finite non-zero, not merely an
// exact zero. Without that, a matrix holding an infinity divided by a NaN
// determinant and returned sixteen NaNs from the scalar path while the vector
// path returned zeros -- so Inverse answered differently at compile time than at
// run time, and differently on a scalar build than on an SSE one.
TEST(MatrixInverse, NonFiniteAndOverflowingInputsReturnIdentity) {
    const float inf = std::numeric_limits<float>::infinity();
    const Matrix4x4 cases[] = {
        {inf, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
        {QuietNaN(), 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, inf, 0, 0, 1},
        // Determinant overflows to infinity though every entry is finite.
        {1e10f, 0, 0, 0, 0, 1e10f, 0, 0, 0, 0, 1e10f, 0, 0, 0, 0, 1e10f},
    };
    for (int n = 0; n < static_cast<int>(std::size(cases)); ++n) {
        EXPECT_TRUE(Inverse(cases[n]) == Matrix4x4::Identity()) << n;
        EXPECT_TRUE(mathf::detail::InverseScalar(cases[n]) ==
                    Matrix4x4::Identity()) << n
            << " -- the two paths must agree here, not just each be defensible";
    }

    EXPECT_TRUE(mathf::Inverse(Matrix3x3{inf, 0, 0, 0, 1, 0, 0, 0, 1}) ==
                Matrix3x3::Identity());
}

TEST(Matrix3x3Inverse, TimesTheOriginalIsIdentity) {
    RandomVectors gen(kSeed + 93);
    for (int n = 0; n < 64; ++n) {
        Matrix3x3 a;
        const Sample s0 = gen.Next(), s1 = gen.Next(), s2 = gen.Next();
        for (int j = 0; j < 3; ++j) {
            a.m[0][j] = s0.f[j];
            a.m[1][j] = s1.f[j];
            a.m[2][j] = s2.f[j];
        }
        for (int i = 0; i < 3; ++i) a.m[i][i] += 200.0f;

        EXPECT_TRUE(mathf::NearEqual(a * Inverse(a), Matrix3x3::Identity(), 1e-3f))
            << n;
    }
}

TEST(Matrix3x3, TransposeAndMultiply) {
    constexpr Matrix3x3 a{1, 2, 3, 4, 5, 6, 7, 8, 9};
    const Matrix3x3 t = Transpose(a);
    EXPECT_FLOAT_EQ(t(0, 1), 4.0f);
    EXPECT_FLOAT_EQ(t(1, 0), 2.0f);
    EXPECT_TRUE(a * Matrix3x3::Identity() == a);
    EXPECT_TRUE(Vector3(1, 0, 0) * a == Vector3(1, 2, 3))
        << "a row vector picks out row 0";
}

// Every Matrix3x3 operation is marked constexpr; nothing proved any of them
// could actually be used in a constant expression. Operands are non-symmetric
// and non-diagonal, so an index swap or a cofactor sign error has somewhere to
// show up -- a diagonal matrix would hide both.
namespace {
constexpr Matrix3x3 kAsym3{1, 2, 3,
                           0, 1, 4,
                           5, 6, 0};
}
static_assert(Transpose(kAsym3)(0, 1) == 0.0f);
static_assert(Transpose(kAsym3)(2, 0) == 3.0f);
static_assert(Determinant(kAsym3) == 1.0f);
// Determinant 1 and integer entries, so the inverse is exact in float.
static_assert(Inverse(kAsym3) == Matrix3x3{-24,  18,   5,
                                            20, -15,  -4,
                                            -5,   4,   1});
static_assert((kAsym3 * Matrix3x3::Identity()) == kAsym3);
static_assert((Vector3(1, 0, 0) * kAsym3) == Vector3(1, 2, 3));

// ---------------------------------------------------------- DirectXMath parity
// The convention checks above say Mathf is self-consistent. These say it agrees
// with DirectXMath, which is what makes a matrix built by one usable by the
// other.
#if MATHF_TEST_HAS_DXMATH
namespace {

DirectX::XMMATRIX ToXm(const Matrix4x4& mat) {
    return DirectX::XMMatrixSet(
        mat.m[0][0], mat.m[0][1], mat.m[0][2], mat.m[0][3],
        mat.m[1][0], mat.m[1][1], mat.m[1][2], mat.m[1][3],
        mat.m[2][0], mat.m[2][1], mat.m[2][2], mat.m[2][3],
        mat.m[3][0], mat.m[3][1], mat.m[3][2], mat.m[3][3]);
}

Matrix4x4 FromXm(DirectX::FXMMATRIX xm) {
    DirectX::XMFLOAT4X4 out{};
    DirectX::XMStoreFloat4x4(&out, xm);
    Matrix4x4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) r.m[i][j] = out.m[i][j];
    }
    return r;
}

} // namespace

// If XMFLOAT4X4's memory layout did not match Matrix4x4's, everything below
// would still "pass" while describing different matrices. Checked first.
TEST(MatrixDxParity, StorageLayoutMatchesXMFLOAT4X4) {
    DirectX::XMFLOAT4X4 xf{};
    DirectX::XMStoreFloat4x4(&xf, ToXm(kCounting));
    const float* mine = &kCounting.m[0][0];
    const float* theirs = &xf.m[0][0];
    for (int i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(mine[i], theirs[i]) << "float " << i;
    }
}

TEST(MatrixDxParity, MultiplyMatchesDirectXMath) {
    RandomVectors gen(kSeed + 100);
    for (int n = 0; n < 64; ++n) {
        const Matrix4x4 a = RandomMatrix(gen);
        const Matrix4x4 b = RandomMatrix(gen);
        EXPECT_TRUE(mathf::NearEqual(
            a * b, FromXm(DirectX::XMMatrixMultiply(ToXm(a), ToXm(b))), 1e-1f))
            << n;
    }
}

TEST(MatrixDxParity, TransposeAndDeterminantMatchDirectXMath) {
    RandomVectors gen(kSeed + 101);
    for (int n = 0; n < 64; ++n) {
        const Matrix4x4 a = RandomInvertibleMatrix(gen);

        EXPECT_TRUE(mathf::NearEqual(
            Transpose(a), FromXm(DirectX::XMMatrixTranspose(ToXm(a))))) << n;

        const float theirs =
            DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(ToXm(a)));
        EXPECT_NEAR(Determinant(a), theirs, std::abs(theirs) * 1e-3f) << n;
    }
}

TEST(MatrixDxParity, InverseMatchesDirectXMath) {
    RandomVectors gen(kSeed + 102);
    for (int n = 0; n < 64; ++n) {
        const Matrix4x4 a = RandomInvertibleMatrix(gen);
        DirectX::XMVECTOR det;
        const Matrix4x4 theirs =
            FromXm(DirectX::XMMatrixInverse(&det, ToXm(a)));
        EXPECT_TRUE(mathf::NearEqual(Inverse(a), theirs, 1e-4f)) << n;
    }
}

// The convention test that matters most: a vector transformed by Mathf and by
// DirectXMath through the same matrix must land in the same place.
TEST(MatrixDxParity, VectorTransformMatchesDirectXMath) {
    RandomVectors gen(kSeed + 103);
    for (int n = 0; n < 64; ++n) {
        const Matrix4x4 a = RandomMatrix(gen);
        const Sample s = gen.Next();
        const Vector4 v{s.f[0], s.f[1], s.f[2], s.f[3]};

        DirectX::XMFLOAT4 out{};
        DirectX::XMStoreFloat4(
            &out, DirectX::XMVector4Transform(
                      DirectX::XMVectorSet(v.x, v.y, v.z, v.w), ToXm(a)));

        const Vector4 mine = v * a;
        EXPECT_TRUE(mathf::NearEqual(mine, Vector4(out.x, out.y, out.z, out.w),
                                     std::abs(out.x) * 1e-4f + 1e-2f)) << n;
    }
}

TEST(MatrixDxParity, PointAndDirectionTransformsMatchDirectXMath) {
    RandomVectors gen(kSeed + 104);
    for (int n = 0; n < 64; ++n) {
        const Matrix4x4 a = RandomMatrix(gen);
        const Sample s = gen.Next();
        const Vector3 v{s.f[0], s.f[1], s.f[2]};
        const DirectX::XMVECTOR xv = DirectX::XMVectorSet(v.x, v.y, v.z, 0.0f);

        DirectX::XMFLOAT3 point{};
        DirectX::XMStoreFloat3(
            &point, DirectX::XMVector3TransformCoord(xv, ToXm(a)));
        DirectX::XMFLOAT3 direction{};
        DirectX::XMStoreFloat3(
            &direction, DirectX::XMVector3TransformNormal(xv, ToXm(a)));

        // TransformCoord divides by w; this matrix's bottom row is arbitrary, so
        // the comparison only holds where w came out as one. Use an affine
        // matrix for the coordinate check instead.
        Matrix4x4 affine = a;
        affine.m[0][3] = 0.0f; affine.m[1][3] = 0.0f;
        affine.m[2][3] = 0.0f; affine.m[3][3] = 1.0f;
        DirectX::XMStoreFloat3(
            &point, DirectX::XMVector3TransformCoord(xv, ToXm(affine)));

        EXPECT_TRUE(mathf::NearEqual(mathf::TransformPoint(v, affine),
                                     Vector3(point.x, point.y, point.z),
                                     1e-2f)) << n;
        EXPECT_TRUE(mathf::NearEqual(mathf::TransformDirection(v, a),
                                     Vector3(direction.x, direction.y,
                                             direction.z),
                                     1e-2f)) << n;
    }
}
#endif // MATHF_TEST_HAS_DXMATH
