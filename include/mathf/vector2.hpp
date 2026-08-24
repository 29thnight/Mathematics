// mathf/vector2.hpp — packed two-component vector.
#ifndef MATHF_VECTOR2_HPP
#define MATHF_VECTOR2_HPP

#include <mathf/vector_common.hpp>

namespace mathf {

// Eight bytes, standard layout. Arithmetic lives in vector_common.hpp; only
// storage, conversions, constants and the 2D cross are here.
struct Vector2 {
    float x, y;

    static constexpr int kLanes = 2;

    constexpr Vector2() noexcept : x(0.0f), y(0.0f) {}

    constexpr Vector2(float xIn, float yIn) noexcept : x(xIn), y(yIn) {}

    explicit constexpr Vector2(float s) noexcept : x(s), y(s) {}

    // ------------------------------------------------------------ conversions
    MATHF_NODISCARD MATHF_INLINE constexpr VecReg Reg() const noexcept {
        return Set(x, y, 0.0f, 0.0f);
    }

    MATHF_NODISCARD MATHF_INLINE static constexpr Vector2
    FromReg(VecReg r) noexcept {
        return Vector2{GetX(r), GetY(r)};
    }

    // ------------------------------------------------------------- lane access
    MATHF_NODISCARD constexpr float operator[](int i) const noexcept {
        return (&x)[i];
    }
    MATHF_NODISCARD constexpr float& operator[](int i) noexcept {
        return (&x)[i];
    }

    // --------------------------------------------------------------- constants
    MATHF_NODISCARD static constexpr Vector2 Zero() noexcept {
        return Vector2{0.0f, 0.0f};
    }
    MATHF_NODISCARD static constexpr Vector2 One() noexcept {
        return Vector2{1.0f, 1.0f};
    }
    MATHF_NODISCARD static constexpr Vector2 UnitX() noexcept {
        return Vector2{1.0f, 0.0f};
    }
    MATHF_NODISCARD static constexpr Vector2 UnitY() noexcept {
        return Vector2{0.0f, 1.0f};
    }
};

static_assert(sizeof(Vector2) == 8, "Vector2 must stay packed");
static_assert(std::is_standard_layout_v<Vector2>);
static_assert(std::is_trivially_copyable_v<Vector2>);

// The 2D cross product is a scalar -- the z component the 3D cross would
// produce. Positive when b lies counter-clockwise from a, which makes it the
// usual winding and side-of-line test.
MATHF_NODISCARD MATHF_INLINE constexpr float
Cross(Vector2 a, Vector2 b) noexcept {
    return a.x * b.y - a.y * b.x;
}

// Rotated a quarter turn counter-clockwise. Common enough in 2D work to be
// worth a name rather than an open-coded swap and negate.
MATHF_NODISCARD MATHF_INLINE constexpr Vector2 Perpendicular(Vector2 v) noexcept {
    return Vector2{-v.y, v.x};
}

} // namespace mathf

#endif // MATHF_VECTOR2_HPP
