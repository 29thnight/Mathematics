// mathf/vector4.hpp — packed four-component vector.
#ifndef MATHF_VECTOR4_HPP
#define MATHF_VECTOR4_HPP

#include <mathf/vector_common.hpp>

namespace mathf {

// Sixteen bytes, standard layout, four-byte aligned -- it drops into vertex
// buffers, constant buffers and struct members with no padding and no alignment
// demands on whatever contains it.
//
// Arithmetic lives in vector_common.hpp, as free templates shared with Vector2
// and Vector3. Only the storage, the conversions, and the constants are here.
struct Vector4 {
    float x, y, z, w;

    static constexpr int kLanes = 4;

    // Default-constructs to zero rather than leaving the lanes indeterminate.
    // DirectXMath and GLM both leave them uninitialized by default; the cost of
    // zeroing is near nothing next to a whole class of silent uninitialized-read
    // bugs, and a compiler drops it wherever the value is overwritten anyway.
    constexpr Vector4() noexcept : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}

    constexpr Vector4(float xIn, float yIn, float zIn, float wIn) noexcept
        : x(xIn), y(yIn), z(zIn), w(wIn) {}

    explicit constexpr Vector4(float s) noexcept : x(s), y(s), z(s), w(s) {}

    // ------------------------------------------------------------ conversions
    MATHF_NODISCARD MATHF_INLINE constexpr VecReg Reg() const noexcept {
        MATHF_IF_CONSTEVAL { return Set(x, y, z, w); }
        // Members are contiguous and the object is a full sixteen bytes, so one
        // unaligned load reaches all four.
        return Load(&x);
    }

    MATHF_NODISCARD MATHF_INLINE static constexpr Vector4
    FromReg(VecReg r) noexcept {
        MATHF_IF_CONSTEVAL {
            return Vector4{Lane(r, 0), Lane(r, 1), Lane(r, 2), Lane(r, 3)};
        }
        Vector4 out;
        Store(&out.x, r);
        return out;
    }

    // ------------------------------------------------------------- lane access
    MATHF_NODISCARD constexpr float operator[](int i) const noexcept {
        return (&x)[i];
    }
    MATHF_NODISCARD constexpr float& operator[](int i) noexcept {
        return (&x)[i];
    }

    // --------------------------------------------------------------- constants
    MATHF_NODISCARD static constexpr Vector4 Zero() noexcept {
        return Vector4{0.0f, 0.0f, 0.0f, 0.0f};
    }
    MATHF_NODISCARD static constexpr Vector4 One() noexcept {
        return Vector4{1.0f, 1.0f, 1.0f, 1.0f};
    }
    MATHF_NODISCARD static constexpr Vector4 UnitX() noexcept {
        return Vector4{1.0f, 0.0f, 0.0f, 0.0f};
    }
    MATHF_NODISCARD static constexpr Vector4 UnitY() noexcept {
        return Vector4{0.0f, 1.0f, 0.0f, 0.0f};
    }
    MATHF_NODISCARD static constexpr Vector4 UnitZ() noexcept {
        return Vector4{0.0f, 0.0f, 1.0f, 0.0f};
    }
    MATHF_NODISCARD static constexpr Vector4 UnitW() noexcept {
        return Vector4{0.0f, 0.0f, 0.0f, 1.0f};
    }
};

static_assert(sizeof(Vector4) == 16, "Vector4 must stay packed");
static_assert(std::is_standard_layout_v<Vector4>);
static_assert(std::is_trivially_copyable_v<Vector4>);

} // namespace mathf

#endif // MATHF_VECTOR4_HPP
