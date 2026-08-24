// mathf/matrix4x4.hpp — 4x4 matrix, row-major with row-vector convention.
//
// The convention is DirectXMath's, decided in docs/PLAN.md §7 and load-bearing
// for everything here:
//
//   * Storage is ROW-MAJOR: m[row][col], and a row is four contiguous floats,
//     so it loads into a register in one go.
//   * Vectors are ROW vectors, transformed as `v * M`, never `M * v`.
//   * Composition therefore reads left to right in application order:
//     `world = scale * rotation * translation` scales first and translates last.
//   * Translation lives in row 3 -- m[3][0..2] -- not in column 3.
//
// Getting any of these backwards produces a matrix that still multiplies without
// complaint and renders everything in the wrong place, so the tests pin all four
// against DirectXMath rather than against themselves.
#ifndef MATHF_MATRIX4X4_HPP
#define MATHF_MATRIX4X4_HPP

#include <mathf/vector.hpp>

namespace mathf {

// Sixty-four bytes, standard layout, four-byte aligned -- it uploads to a
// constant buffer as-is.
struct Matrix4x4 {
    float m[4][4];

    constexpr Matrix4x4() noexcept : m{} {}

    constexpr Matrix4x4(float m00, float m01, float m02, float m03,
                        float m10, float m11, float m12, float m13,
                        float m20, float m21, float m22, float m23,
                        float m30, float m31, float m32, float m33) noexcept
        : m{{m00, m01, m02, m03},
            {m10, m11, m12, m13},
            {m20, m21, m22, m23},
            {m30, m31, m32, m33}} {}

    // ------------------------------------------------------------ element access
    MATHF_NODISCARD constexpr float operator()(int row, int col) const noexcept {
        return m[row][col];
    }
    MATHF_NODISCARD constexpr float& operator()(int row, int col) noexcept {
        return m[row][col];
    }

#if MATHF_HAS_MULTIDIM_SUBSCRIPT
    // C++23 spelling: M[row, col]. The call form above stays available so the
    // same source compiles either way.
    MATHF_NODISCARD constexpr float operator[](int row, int col) const noexcept {
        return m[row][col];
    }
    MATHF_NODISCARD constexpr float& operator[](int row, int col) noexcept {
        return m[row][col];
    }
#endif

    // ---------------------------------------------------------------- row access
    // A row is four contiguous floats, which is the entire reason for storing
    // row-major: this is one load, and the multiply below is built out of it.
    MATHF_NODISCARD MATHF_INLINE constexpr VecReg Row(int i) const noexcept {
        MATHF_IF_CONSTEVAL { return Set(m[i][0], m[i][1], m[i][2], m[i][3]); }
        return Load(&m[i][0]);
    }

    MATHF_INLINE constexpr void SetRow(int i, VecReg r) noexcept {
        MATHF_IF_CONSTEVAL {
            m[i][0] = Lane(r, 0); m[i][1] = Lane(r, 1);
            m[i][2] = Lane(r, 2); m[i][3] = Lane(r, 3);
            return;
        }
        Store(&m[i][0], r);
    }

    MATHF_NODISCARD constexpr Vector4 GetRow(int i) const noexcept {
        return Vector4{m[i][0], m[i][1], m[i][2], m[i][3]};
    }

    MATHF_NODISCARD constexpr Vector4 GetColumn(int j) const noexcept {
        return Vector4{m[0][j], m[1][j], m[2][j], m[3][j]};
    }

    // Rows 0-2 carry the basis under this convention, row 3 the translation.
    MATHF_NODISCARD constexpr Vector3 Right() const noexcept {
        return Vector3{m[0][0], m[0][1], m[0][2]};
    }
    MATHF_NODISCARD constexpr Vector3 Up() const noexcept {
        return Vector3{m[1][0], m[1][1], m[1][2]};
    }
    MATHF_NODISCARD constexpr Vector3 Forward() const noexcept {
        return Vector3{m[2][0], m[2][1], m[2][2]};
    }
    MATHF_NODISCARD constexpr Vector3 Translation() const noexcept {
        return Vector3{m[3][0], m[3][1], m[3][2]};
    }

    // --------------------------------------------------------------- constants
    MATHF_NODISCARD static constexpr Matrix4x4 Identity() noexcept {
        return Matrix4x4{1, 0, 0, 0,
                         0, 1, 0, 0,
                         0, 0, 1, 0,
                         0, 0, 0, 1};
    }
};

static_assert(sizeof(Matrix4x4) == 64, "Matrix4x4 must stay packed");
static_assert(std::is_standard_layout_v<Matrix4x4>);
static_assert(std::is_trivially_copyable_v<Matrix4x4>);

// ------------------------------------------------------------------- multiply
// (a * b)[i] = sum over k of a[i][k] * b[k], which with row-major storage is one
// broadcast and one fused multiply-add per element of a's row against a whole
// row of b. Sixteen FMAs and no transpose -- the same shape as
// XMMatrixMultiply, and the reason the row-vector convention is worth keeping.
namespace detail {

// One output row: each of a's four elements scales a whole row of b.
//
// The scalars are broadcast straight from memory rather than loaded as a row and
// shuffled apart. The shuffle form issues sixteen shuffles per multiply and,
// with a single shuffle port, that becomes the bottleneck while the arithmetic
// units idle -- it measured 141 M/s against 251 for this version.
MATHF_NODISCARD MATHF_INLINE VecReg
CombineRows(const float* aRow, VecReg b0, VecReg b1, VecReg b2,
            VecReg b3) noexcept {
    VecReg acc = Mul(LoadSplat(aRow + 0), b0);
    acc = MulAdd(LoadSplat(aRow + 1), b1, acc);
    acc = MulAdd(LoadSplat(aRow + 2), b2, acc);
    acc = MulAdd(LoadSplat(aRow + 3), b3, acc);
    return acc;
}

#if MATHF_SIMD_SSE && MATHF_HAS_AVX && MATHF_HAS_FMA
// Two rows at a time in 256-bit registers, halving the operation count: eight
// wide multiply-adds instead of sixteen narrow ones.
//
// This is the only place in the library that reaches past 128 bits, and it is
// here because the 128-bit version could not close the gap to DirectXMath --
// XMMatrixMultiply takes exactly this path, and no amount of tuning within 128
// bits matches an algorithm doing half the work. The narrow version above stays
// as the fallback for targets without AVX.
MATHF_NODISCARD MATHF_INLINE Matrix4x4
MultiplyAvx(const Matrix4x4& a, const Matrix4x4& b) noexcept {
    // Rows 0-1 and 2-3 of each operand, packed into one register per pair.
    __m256 t0 = _mm256_loadu_ps(&a.m[0][0]);
    __m256 t1 = _mm256_loadu_ps(&a.m[2][0]);
    const __m256 u0 = _mm256_loadu_ps(&b.m[0][0]);
    const __m256 u1 = _mm256_loadu_ps(&b.m[2][0]);

    // permute2f128 with 0x00 broadcasts b's row 0 to both halves, 0x11 its row 1
    // -- so one wide operand serves both output rows at once.
    __m256 s0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(0, 0, 0, 0));
    __m256 s1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(0, 0, 0, 0));
    __m256 r0 = _mm256_permute2f128_ps(u0, u0, 0x00);
    __m256 c0 = _mm256_mul_ps(s0, r0);
    __m256 c1 = _mm256_mul_ps(s1, r0);

    s0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(1, 1, 1, 1));
    s1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(1, 1, 1, 1));
    r0 = _mm256_permute2f128_ps(u0, u0, 0x11);
    const __m256 c2 = _mm256_fmadd_ps(s0, r0, c0);
    const __m256 c3 = _mm256_fmadd_ps(s1, r0, c1);

    s0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(2, 2, 2, 2));
    s1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(2, 2, 2, 2));
    __m256 r1 = _mm256_permute2f128_ps(u1, u1, 0x00);
    const __m256 c4 = _mm256_mul_ps(s0, r1);
    const __m256 c5 = _mm256_mul_ps(s1, r1);

    s0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(3, 3, 3, 3));
    s1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(3, 3, 3, 3));
    r1 = _mm256_permute2f128_ps(u1, u1, 0x11);
    const __m256 c6 = _mm256_fmadd_ps(s0, r1, c4);
    const __m256 c7 = _mm256_fmadd_ps(s1, r1, c5);

    t0 = _mm256_add_ps(c2, c6);
    t1 = _mm256_add_ps(c3, c7);

    Matrix4x4 result;
    _mm256_storeu_ps(&result.m[0][0], t0);
    _mm256_storeu_ps(&result.m[2][0], t1);
    return result;
}
#endif

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
MultiplyScalar(const Matrix4x4& a, const Matrix4x4& b) noexcept {
    Matrix4x4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]
                      + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    }
    return r;
}

} // namespace detail

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
operator*(const Matrix4x4& a, const Matrix4x4& b) noexcept {
    MATHF_IF_CONSTEVAL { return detail::MultiplyScalar(a, b); }

#if MATHF_SIMD_SSE && MATHF_HAS_AVX && MATHF_HAS_FMA
    return detail::MultiplyAvx(a, b);
#else
    const VecReg b0 = b.Row(0);
    const VecReg b1 = b.Row(1);
    const VecReg b2 = b.Row(2);
    const VecReg b3 = b.Row(3);

    // Unrolled rather than looping over the row index: with a runtime index the
    // compiler cannot keep the result in registers and every SetRow becomes a
    // stack round trip.
    Matrix4x4 result;
    result.SetRow(0, detail::CombineRows(&a.m[0][0], b0, b1, b2, b3));
    result.SetRow(1, detail::CombineRows(&a.m[1][0], b0, b1, b2, b3));
    result.SetRow(2, detail::CombineRows(&a.m[2][0], b0, b1, b2, b3));
    result.SetRow(3, detail::CombineRows(&a.m[3][0], b0, b1, b2, b3));
    return result;
#endif
}

MATHF_INLINE constexpr Matrix4x4& operator*=(Matrix4x4& a,
                                             const Matrix4x4& b) noexcept {
    return a = a * b;
}

// ------------------------------------------------------------------ transform
// Row vector times matrix. The vector's components broadcast across the matrix's
// rows, which is the same shape as the matrix multiply above.
MATHF_NODISCARD MATHF_INLINE constexpr Vector4
operator*(const Vector4& v, const Matrix4x4& mat) noexcept {
    const VecReg r = v.Reg();
    VecReg acc = Mul(SplatX(r), mat.Row(0));
    acc = MulAdd(SplatY(r), mat.Row(1), acc);
    acc = MulAdd(SplatZ(r), mat.Row(2), acc);
    acc = MulAdd(SplatW(r), mat.Row(3), acc);
    return Vector4::FromReg(acc);
}

// A position: w is taken as 1, so the translation row applies.
MATHF_NODISCARD MATHF_INLINE constexpr Vector3
TransformPoint(const Vector3& p, const Matrix4x4& mat) noexcept {
    const VecReg r = p.Reg();
    VecReg acc = Mul(SplatX(r), mat.Row(0));
    acc = MulAdd(SplatY(r), mat.Row(1), acc);
    acc = MulAdd(SplatZ(r), mat.Row(2), acc);
    acc = Add(acc, mat.Row(3));   // w == 1
    return Vector3::FromReg(acc);
}

// A direction: w is taken as 0, so translation is ignored. Using TransformPoint
// for a normal or a velocity is a classic bug; the two names exist to make the
// choice explicit at every call site.
MATHF_NODISCARD MATHF_INLINE constexpr Vector3
TransformDirection(const Vector3& d, const Matrix4x4& mat) noexcept {
    const VecReg r = d.Reg();
    VecReg acc = Mul(SplatX(r), mat.Row(0));
    acc = MulAdd(SplatY(r), mat.Row(1), acc);
    acc = MulAdd(SplatZ(r), mat.Row(2), acc);
    return Vector3::FromReg(acc);
}

// ------------------------------------------------------------------ transpose
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
Transpose(const Matrix4x4& mat) noexcept {
    const VecReg r0 = mat.Row(0);
    const VecReg r1 = mat.Row(1);
    const VecReg r2 = mat.Row(2);
    const VecReg r3 = mat.Row(3);

    // Interleave in two passes: pair up the low and high halves of adjacent
    // rows, then pick alternating lanes out of the pairs. Eight shuffles total.
    const VecReg lo01 = Shuffle<0, 1, 0, 1>(r0, r1);   // x0 y0 x1 y1
    const VecReg hi01 = Shuffle<2, 3, 2, 3>(r0, r1);   // z0 w0 z1 w1
    const VecReg lo23 = Shuffle<0, 1, 0, 1>(r2, r3);   // x2 y2 x3 y3
    const VecReg hi23 = Shuffle<2, 3, 2, 3>(r2, r3);   // z2 w2 z3 w3

    Matrix4x4 result;
    result.SetRow(0, Shuffle<0, 2, 0, 2>(lo01, lo23));
    result.SetRow(1, Shuffle<1, 3, 1, 3>(lo01, lo23));
    result.SetRow(2, Shuffle<0, 2, 0, 2>(hi01, hi23));
    result.SetRow(3, Shuffle<1, 3, 1, 3>(hi01, hi23));
    return result;
}

// -------------------------------------------------------- determinant, inverse
namespace detail {

// The six 2x2 minors of the top two rows and the six of the bottom two. Laplace
// expansion by complementary minors: every cofactor of the 4x4 is a product of
// one from each group, so these twelve values carry the whole computation --
// determinant and all sixteen adjugate entries alike.
struct Minors4x4 {
    float s0, s1, s2, s3, s4, s5;   // rows 0,1 over each column pair
    float c0, c1, c2, c3, c4, c5;   // rows 2,3 over each column pair
};

MATHF_NODISCARD MATHF_INLINE constexpr Minors4x4
ComputeMinors(const Matrix4x4& x) noexcept {
    return Minors4x4{
        x.m[0][0] * x.m[1][1] - x.m[0][1] * x.m[1][0],
        x.m[0][0] * x.m[1][2] - x.m[0][2] * x.m[1][0],
        x.m[0][0] * x.m[1][3] - x.m[0][3] * x.m[1][0],
        x.m[0][1] * x.m[1][2] - x.m[0][2] * x.m[1][1],
        x.m[0][1] * x.m[1][3] - x.m[0][3] * x.m[1][1],
        x.m[0][2] * x.m[1][3] - x.m[0][3] * x.m[1][2],

        x.m[2][0] * x.m[3][1] - x.m[2][1] * x.m[3][0],
        x.m[2][0] * x.m[3][2] - x.m[2][2] * x.m[3][0],
        x.m[2][0] * x.m[3][3] - x.m[2][3] * x.m[3][0],
        x.m[2][1] * x.m[3][2] - x.m[2][2] * x.m[3][1],
        x.m[2][1] * x.m[3][3] - x.m[2][3] * x.m[3][1],
        x.m[2][2] * x.m[3][3] - x.m[2][3] * x.m[3][2]};
}

} // namespace detail

MATHF_NODISCARD MATHF_INLINE constexpr float
Determinant(const Matrix4x4& mat) noexcept {
    const detail::Minors4x4 k = detail::ComputeMinors(mat);
    return k.s0 * k.c5 - k.s1 * k.c4 + k.s2 * k.c3
         + k.s3 * k.c2 - k.s4 * k.c1 + k.s5 * k.c0;
}

// Returns the identity for a singular matrix rather than filling it with
// infinities. DirectXMath writes QNaN instead and sets its optional determinant
// output to zero; a caller that ignores the return value gets NaN spreading
// through the scene either way, so this returns something usable and reports
// singularity through the Determinant call the caller should already be making.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
Inverse(const Matrix4x4& mat) noexcept {
    const detail::Minors4x4 k = detail::ComputeMinors(mat);

    const float det = k.s0 * k.c5 - k.s1 * k.c4 + k.s2 * k.c3
                    + k.s3 * k.c2 - k.s4 * k.c1 + k.s5 * k.c0;
    if (det == 0.0f) return Matrix4x4::Identity();

    const float invDet = 1.0f / det;
    const auto& x = mat.m;

    Matrix4x4 r;
    r.m[0][0] = ( x[1][1] * k.c5 - x[1][2] * k.c4 + x[1][3] * k.c3) * invDet;
    r.m[0][1] = (-x[0][1] * k.c5 + x[0][2] * k.c4 - x[0][3] * k.c3) * invDet;
    r.m[0][2] = ( x[3][1] * k.s5 - x[3][2] * k.s4 + x[3][3] * k.s3) * invDet;
    r.m[0][3] = (-x[2][1] * k.s5 + x[2][2] * k.s4 - x[2][3] * k.s3) * invDet;

    r.m[1][0] = (-x[1][0] * k.c5 + x[1][2] * k.c2 - x[1][3] * k.c1) * invDet;
    r.m[1][1] = ( x[0][0] * k.c5 - x[0][2] * k.c2 + x[0][3] * k.c1) * invDet;
    r.m[1][2] = (-x[3][0] * k.s5 + x[3][2] * k.s2 - x[3][3] * k.s1) * invDet;
    r.m[1][3] = ( x[2][0] * k.s5 - x[2][2] * k.s2 + x[2][3] * k.s1) * invDet;

    r.m[2][0] = ( x[1][0] * k.c4 - x[1][1] * k.c2 + x[1][3] * k.c0) * invDet;
    r.m[2][1] = (-x[0][0] * k.c4 + x[0][1] * k.c2 - x[0][3] * k.c0) * invDet;
    r.m[2][2] = ( x[3][0] * k.s4 - x[3][1] * k.s2 + x[3][3] * k.s0) * invDet;
    r.m[2][3] = (-x[2][0] * k.s4 + x[2][1] * k.s2 - x[2][3] * k.s0) * invDet;

    r.m[3][0] = (-x[1][0] * k.c3 + x[1][1] * k.c1 - x[1][2] * k.c0) * invDet;
    r.m[3][1] = ( x[0][0] * k.c3 - x[0][1] * k.c1 + x[0][2] * k.c0) * invDet;
    r.m[3][2] = (-x[3][0] * k.s3 + x[3][1] * k.s1 - x[3][2] * k.s0) * invDet;
    r.m[3][3] = ( x[2][0] * k.s3 - x[2][1] * k.s1 + x[2][2] * k.s0) * invDet;
    return r;
}

// ------------------------------------------------------------------ comparison
MATHF_NODISCARD MATHF_INLINE constexpr bool
operator==(const Matrix4x4& a, const Matrix4x4& b) noexcept {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (a.m[i][j] != b.m[i][j]) return false;
        }
    }
    return true;
}

MATHF_NODISCARD MATHF_INLINE constexpr bool
NearEqual(const Matrix4x4& a, const Matrix4x4& b,
          float epsilon = 1e-5f) noexcept {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const float diff = a.m[i][j] - b.m[i][j];
            if (diff > epsilon || diff < -epsilon) return false;
        }
    }
    return true;
}

} // namespace mathf

#endif // MATHF_MATRIX4X4_HPP
