// mathf/matrix3x3.hpp — 3x3 matrix, row-major with row-vector convention.
//
// Same conventions as Matrix4x4: row-major storage, `v * M`, composition reading
// left to right. A 3x3 carries rotation and scale but no translation, which is
// what makes it the right type for transforming normals and for the linear part
// of a transform.
#ifndef MATHF_MATRIX3X3_HPP
#define MATHF_MATRIX3X3_HPP

#include <mathf/vector.hpp>

namespace mathf {

// Thirty-six bytes, packed. A row is three contiguous floats, so unlike
// Matrix4x4 it does not fill a register -- the operations here stay scalar for
// the same measured reason Vector3's do (docs/PLAN.md Phase 2): at three
// components the register round trip costs more than it saves.
struct Matrix3x3 {
    float m[3][3];

    constexpr Matrix3x3() noexcept : m{} {}

    constexpr Matrix3x3(float m00, float m01, float m02,
                        float m10, float m11, float m12,
                        float m20, float m21, float m22) noexcept
        : m{{m00, m01, m02}, {m10, m11, m12}, {m20, m21, m22}} {}

    // ------------------------------------------------------------ element access
    MATHF_NODISCARD constexpr float operator()(int row, int col) const noexcept {
        return m[row][col];
    }
    MATHF_NODISCARD constexpr float& operator()(int row, int col) noexcept {
        return m[row][col];
    }

#if MATHF_HAS_MULTIDIM_SUBSCRIPT
    MATHF_NODISCARD constexpr float operator[](int row, int col) const noexcept {
        return m[row][col];
    }
    MATHF_NODISCARD constexpr float& operator[](int row, int col) noexcept {
        return m[row][col];
    }
#endif

    MATHF_NODISCARD constexpr Vector3 GetRow(int i) const noexcept {
        return Vector3{m[i][0], m[i][1], m[i][2]};
    }

    MATHF_NODISCARD constexpr Vector3 GetColumn(int j) const noexcept {
        return Vector3{m[0][j], m[1][j], m[2][j]};
    }

    MATHF_NODISCARD static constexpr Matrix3x3 Identity() noexcept {
        return Matrix3x3{1, 0, 0,
                         0, 1, 0,
                         0, 0, 1};
    }
};

static_assert(sizeof(Matrix3x3) == 36, "Matrix3x3 must stay packed");
static_assert(std::is_standard_layout_v<Matrix3x3>);
static_assert(std::is_trivially_copyable_v<Matrix3x3>);

// ------------------------------------------------------------------- multiply
MATHF_NODISCARD MATHF_INLINE constexpr Matrix3x3
operator*(const Matrix3x3& a, const Matrix3x3& b) noexcept {
    Matrix3x3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]
                      + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

MATHF_INLINE constexpr Matrix3x3& operator*=(Matrix3x3& a,
                                             const Matrix3x3& b) noexcept {
    return a = a * b;
}

// Row vector times matrix, matching the 4x4 convention.
MATHF_NODISCARD MATHF_INLINE constexpr Vector3
operator*(const Vector3& v, const Matrix3x3& mat) noexcept {
    return Vector3{
        v.x * mat.m[0][0] + v.y * mat.m[1][0] + v.z * mat.m[2][0],
        v.x * mat.m[0][1] + v.y * mat.m[1][1] + v.z * mat.m[2][1],
        v.x * mat.m[0][2] + v.y * mat.m[1][2] + v.z * mat.m[2][2]};
}

// ------------------------------------------------------------------ transpose
MATHF_NODISCARD MATHF_INLINE constexpr Matrix3x3
Transpose(const Matrix3x3& mat) noexcept {
    return Matrix3x3{mat.m[0][0], mat.m[1][0], mat.m[2][0],
                     mat.m[0][1], mat.m[1][1], mat.m[2][1],
                     mat.m[0][2], mat.m[1][2], mat.m[2][2]};
}

// -------------------------------------------------------- determinant, inverse
MATHF_NODISCARD MATHF_INLINE constexpr float
Determinant(const Matrix3x3& mat) noexcept {
    const auto& x = mat.m;
    return x[0][0] * (x[1][1] * x[2][2] - x[1][2] * x[2][1])
         - x[0][1] * (x[1][0] * x[2][2] - x[1][2] * x[2][0])
         + x[0][2] * (x[1][0] * x[2][1] - x[1][1] * x[2][0]);
}

// Returns the identity unless the determinant is a finite non-zero, matching
// Matrix4x4's choice -- see the longer note there for why the guard rejects
// infinities and NaN rather than only exact zero.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix3x3
Inverse(const Matrix3x3& mat) noexcept {
    const auto& x = mat.m;

    // The adjugate is the transpose of the cofactor matrix, so the row and
    // column indices swap on the way out.
    const float c00 = x[1][1] * x[2][2] - x[1][2] * x[2][1];
    const float c01 = x[1][2] * x[2][0] - x[1][0] * x[2][2];
    const float c02 = x[1][0] * x[2][1] - x[1][1] * x[2][0];

    const float det = x[0][0] * c00 + x[0][1] * c01 + x[0][2] * c02;
    if (!detail::IsFiniteNonZero(det)) return Matrix3x3::Identity();
    const float invDet = 1.0f / det;

    return Matrix3x3{
        c00 * invDet,
        (x[0][2] * x[2][1] - x[0][1] * x[2][2]) * invDet,
        (x[0][1] * x[1][2] - x[0][2] * x[1][1]) * invDet,

        c01 * invDet,
        (x[0][0] * x[2][2] - x[0][2] * x[2][0]) * invDet,
        (x[0][2] * x[1][0] - x[0][0] * x[1][2]) * invDet,

        c02 * invDet,
        (x[0][1] * x[2][0] - x[0][0] * x[2][1]) * invDet,
        (x[0][0] * x[1][1] - x[0][1] * x[1][0]) * invDet};
}

// ------------------------------------------------------------------ comparison
MATHF_NODISCARD MATHF_INLINE constexpr bool
operator==(const Matrix3x3& a, const Matrix3x3& b) noexcept {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (a.m[i][j] != b.m[i][j]) return false;
        }
    }
    return true;
}

// Positive test, not a negated one -- see the note on Matrix4x4's NearEqual for
// why the negated spelling silently accepts NaN.
MATHF_NODISCARD MATHF_INLINE constexpr bool
NearEqual(const Matrix3x3& a, const Matrix3x3& b,
          float epsilon = 1e-5f) noexcept {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const float diff = a.m[i][j] - b.m[i][j];
            if (!(diff <= epsilon && diff >= -epsilon)) return false;
        }
    }
    return true;
}

} // namespace mathf

#endif // MATHF_MATRIX3X3_HPP
