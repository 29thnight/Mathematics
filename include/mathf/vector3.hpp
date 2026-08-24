// mathf/vector3.hpp — packed three-component vector.
#ifndef MATHF_VECTOR3_HPP
#define MATHF_VECTOR3_HPP

#include <mathf/vector_common.hpp>

namespace mathf {

// Twelve bytes, standard layout. The packing is the point: a Vector3 array is a
// position stream a GPU can read directly, with no gaps to strip out first.
//
// Arithmetic lives in vector_common.hpp. Only storage, conversions, constants
// and the cross product are here.
struct Vector3 {
    float x, y, z;

    static constexpr int kLanes = 3;

    constexpr Vector3() noexcept : x(0.0f), y(0.0f), z(0.0f) {}

    constexpr Vector3(float xIn, float yIn, float zIn) noexcept
        : x(xIn), y(yIn), z(zIn) {}

    explicit constexpr Vector3(float s) noexcept : x(s), y(s), z(s) {}

    // ------------------------------------------------------------ conversions
    // The register's w lane is zero. Every operation that consumes a Vector3
    // uses Dot3, and FromReg drops w, so it never reaches a result -- but it
    // being zero rather than indeterminate keeps Length and Normalize honest if
    // one ever slips through a Dot4.
    MATHF_NODISCARD MATHF_INLINE constexpr VecReg Reg() const noexcept {
        // Deliberately not a 16-byte load of &x: the object is twelve bytes, so
        // that would read past the end and can fault at a page boundary.
        return Set(x, y, z, 0.0f);
    }

    MATHF_NODISCARD MATHF_INLINE static constexpr Vector3
    FromReg(VecReg r) noexcept {
        return Vector3{GetX(r), GetY(r), GetZ(r)};
    }

    // ------------------------------------------------------------- lane access
    MATHF_NODISCARD constexpr float operator[](int i) const noexcept {
        return (&x)[i];
    }
    MATHF_NODISCARD constexpr float& operator[](int i) noexcept {
        return (&x)[i];
    }

    // --------------------------------------------------------------- constants
    MATHF_NODISCARD static constexpr Vector3 Zero() noexcept {
        return Vector3{0.0f, 0.0f, 0.0f};
    }
    MATHF_NODISCARD static constexpr Vector3 One() noexcept {
        return Vector3{1.0f, 1.0f, 1.0f};
    }
    MATHF_NODISCARD static constexpr Vector3 UnitX() noexcept {
        return Vector3{1.0f, 0.0f, 0.0f};
    }
    MATHF_NODISCARD static constexpr Vector3 UnitY() noexcept {
        return Vector3{0.0f, 1.0f, 0.0f};
    }
    MATHF_NODISCARD static constexpr Vector3 UnitZ() noexcept {
        return Vector3{0.0f, 0.0f, 1.0f};
    }
};

static_assert(sizeof(Vector3) == 12, "Vector3 must stay packed");
static_assert(std::is_standard_layout_v<Vector3>);
static_assert(std::is_trivially_copyable_v<Vector3>);

// ---------------------------------------------------------------- cross product
// Right-handed: Cross(UnitX, UnitY) == UnitZ.
//
// Computed with two shuffles rather than the textbook three, by evaluating the
// permuted product once and rotating the result -- the same trick DirectXMath
// uses in XMVector3Cross.
MATHF_NODISCARD MATHF_INLINE constexpr Vector3
Cross(Vector3 a, Vector3 b) noexcept {
    const VecReg ra = a.Reg();
    const VecReg rb = b.Reg();

    // (a.yzx * b.zxy) - (a.zxy * b.yzx)
    const VecReg aYZX = Shuffle<1, 2, 0, 3>(ra);
    const VecReg bZXY = Shuffle<2, 0, 1, 3>(rb);
    const VecReg aZXY = Shuffle<2, 0, 1, 3>(ra);
    const VecReg bYZX = Shuffle<1, 2, 0, 3>(rb);

    return Vector3::FromReg(Sub(Mul(aYZX, bZXY), Mul(aZXY, bYZX)));
}

} // namespace mathf

#endif // MATHF_VECTOR3_HPP
