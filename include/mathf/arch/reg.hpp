// mathf/arch/reg.hpp — the register type and the primitives every backend needs.
//
// Layer 1 of the backend stack:
//   reg.hpp            <- this file: VecReg, lane access, bit helpers
//   consteval_ops.hpp  <- scalar semantics, the single definition of what each
//                         operation means
//   simd_{sse,neon,scalar}.hpp <- runtime instruction selection per target
//   simd_select.hpp    <- includes exactly one backend
#ifndef MATHF_ARCH_REG_HPP
#define MATHF_ARCH_REG_HPP

#include <mathf/config.hpp>

#include <bit>
#include <cstdint>

#if MATHF_SIMD_SSE
#  include <immintrin.h>
#elif MATHF_SIMD_NEON
#  include <arm_neon.h>
#endif

namespace mathf {

// ---------------------------------------------------------------- register type
#if MATHF_SIMD_SSE
using RegNative = __m128;
#elif MATHF_SIMD_NEON
using RegNative = float32x4_t;
#else
struct RegNative { float f[4]; };
#endif

// One 128-bit SIMD register: four floats, always passed by value, never used as
// a storage type. Phase 2's Float2/3/4 own memory layout; VecReg owns computation.
struct VecReg {
    RegNative v;
};

// Masks are VecReg too, with each lane either all-ones or all-zeros -- the same
// convention DirectXMath uses for XMVECTOR masks. Comparisons produce them and
// Select consumes them. An all-ones lane read as a float is a NaN, so mask lanes
// must only ever be moved and combined bitwise, never used in arithmetic.
using MaskReg = VecReg;

// ------------------------------------------------------------------ bit helpers
inline constexpr std::uint32_t kLaneTrue = 0xFFFFFFFFu;
inline constexpr std::uint32_t kLaneFalse = 0x00000000u;
inline constexpr std::uint32_t kSignBit = 0x80000000u;
inline constexpr std::uint32_t kAbsMask = 0x7FFFFFFFu;

MATHF_NODISCARD MATHF_INLINE constexpr std::uint32_t BitsOf(float x) noexcept {
    return std::bit_cast<std::uint32_t>(x);
}

MATHF_NODISCARD MATHF_INLINE constexpr float FromBits(std::uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
}

// ------------------------------------------------------------------ lane access
// Lane layout is NOT portable. On genuine MSVC the intrinsic vector types are
// magic unions (__m128 with m128_f32[], __n128 with n128_f32[]); on Clang and GCC
// they are native vector types that subscript directly. Every constant-evaluated
// read or write must route through these two helpers.
//
// Deliberately not written with vst1q_f32/_mm_store_ps, which would be portable
// but are not constant-evaluable -- and serving the compile-time path is the
// entire reason these exist.
//
// NOT VERIFIED ON MSVC/ARM64: no ARM toolchain was available when this was
// written, so the __n128 branch rests on documented layout and is first
// exercised by CI.
// The four lanes as a plain array. Used only to reinterpret a register during
// constant evaluation on Clang and GCC, where subscripting a native vector type
// is not a constant expression -- Clang rejects it outright with
// -Winvalid-constexpr. std::bit_cast is constant-evaluable for those compilers
// because their vector types contain no union members; on MSVC the intrinsic
// type IS a union, which rules bit_cast out but makes the member accessible
// directly instead. The two restrictions are exactly complementary.
struct RegLanes {
    float f[4];
};

MATHF_NODISCARD MATHF_INLINE constexpr float Lane(const VecReg& r, int i) noexcept {
#if MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_SSE
    return r.v.m128_f32[i];
#elif MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_NEON
    return r.v.n128_f32[i];
#elif MATHF_SIMD_SSE || MATHF_SIMD_NEON
    MATHF_IF_CONSTEVAL { return std::bit_cast<RegLanes>(r.v).f[i]; }
    return r.v[i];
#else
    return r.v.f[i];
#endif
}

namespace detail {

// Writes a lane in place. Valid during constant evaluation only on MSVC, where
// the intrinsic types are unions: GCC -- and some Clang versions -- reject
// assignment to an element of a native vector type in a constant expression.
// The portable compile-time path is MakeReg, which builds the whole register at
// once; this helper exists for MakeReg's own MSVC branch and for runtime writes.
MATHF_INLINE constexpr void WriteLane(RegNative& v, int i, float x) noexcept {
#if MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_SSE
    v.m128_f32[i] = x;
#elif MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_NEON
    v.n128_f32[i] = x;
#elif MATHF_SIMD_SSE || MATHF_SIMD_NEON
    // Same split as Lane: assigning an element of a native vector type is not a
    // constant expression, so the compile-time path goes through the lane array.
    MATHF_IF_CONSTEVAL {
        RegLanes lanes = std::bit_cast<RegLanes>(v);
        lanes.f[i] = x;
        v = std::bit_cast<RegNative>(lanes);
        return;
    }
    v[i] = x;
#else
    v.f[i] = x;
#endif
}

} // namespace detail

MATHF_NODISCARD MATHF_INLINE constexpr std::uint32_t
LaneBits(const VecReg& r, int i) noexcept {
    return BitsOf(Lane(r, i));
}

// ----------------------------------------------------------------- construction
// The one way to build a register from four scalars.
//
// The runtime and compile-time paths are split even though the compile-time one
// would compile at runtime too, because on MSVC it produces a value the
// optimizer then handles badly. Writing lanes -- or aggregate-initializing
// `__m128{x, y, z, w}`, which is the same thing through the union's float[4]
// member -- leaves MSVC treating the object as four scalars for the rest of its
// lifetime, emitting every later access element-wise. A value built that way and
// carried into a loop gets rebuilt with vinsertps on entry and torn apart with
// vshufps on exit, around a single arithmetic instruction. Measured on a MulAdd
// chain: 1.9x slower than DirectXMath, with the arithmetic itself identical.
//
// The intrinsics keep the value vector-typed, and constant evaluation never sees
// them.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg
MakeReg(float x, float y, float z, float w) noexcept {
    MATHF_IF_CONSTEVAL {
        // The compile-time form is per-compiler because the register type is:
        // MSVC's intrinsic types are unions whose float member can be written
        // lane by lane, while Clang's and GCC's are native vector types that
        // must be initialized whole -- GCC rejects element assignment on one
        // during constant evaluation outright, and Clang does depending on
        // version. Both were caught by CI, having passed locally.
#if MATHF_MSVC_INTRINSIC_UNION
        VecReg r{};
        detail::WriteLane(r.v, 0, x);
        detail::WriteLane(r.v, 1, y);
        detail::WriteLane(r.v, 2, z);
        detail::WriteLane(r.v, 3, w);
        return r;
#elif MATHF_SIMD_SSE || MATHF_SIMD_NEON
        // bit_cast rather than brace-initializing the vector type: the literal
        // form is a compiler extension that GCC and Clang do not spell alike,
        // while bit_cast is standard and constant-evaluable for both.
        return VecReg{std::bit_cast<RegNative>(RegLanes{{x, y, z, w}})};
#else
        return VecReg{RegNative{{x, y, z, w}}};
#endif
    }
#if MATHF_SIMD_SSE
    return VecReg{_mm_setr_ps(x, y, z, w)};   // setr takes lanes in order
#elif MATHF_SIMD_NEON
    // Loaded rather than brace-initializing float32x4_t: vld1q_f32 is plain
    // ACLE, where the vector-literal form is a compiler extension.
    const float lanes[4] = {x, y, z, w};
    return VecReg{vld1q_f32(lanes)};
#else
    return VecReg{RegNative{{x, y, z, w}}};
#endif
}

// Writing a single lane during constant evaluation goes through MakeReg, which
// is the only portable way to build a register (see the note above). At runtime
// the direct write is used.
MATHF_INLINE constexpr void SetLane(VecReg& r, int i, float x) noexcept {
    MATHF_IF_CONSTEVAL {
        r = MakeReg(i == 0 ? x : Lane(r, 0), i == 1 ? x : Lane(r, 1),
                    i == 2 ? x : Lane(r, 2), i == 3 ? x : Lane(r, 3));
        return;
    }
    detail::WriteLane(r.v, i, x);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg
MakeMaskReg(std::uint32_t x, std::uint32_t y,
            std::uint32_t z, std::uint32_t w) noexcept {
    return MakeReg(FromBits(x), FromBits(y), FromBits(z), FromBits(w));
}

} // namespace mathf

#endif // MATHF_ARCH_REG_HPP
