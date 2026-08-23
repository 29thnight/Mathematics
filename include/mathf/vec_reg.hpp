// mathf/vec_reg.hpp — the register-level vector type, Mathf's unit of computation.
//
// VecReg wraps one 128-bit SIMD register and is ALWAYS passed by value. It is not
// a storage type: structs and arrays hold Float2/3/4 (Phase 2) and promote to
// VecReg to compute. docs/SPIKE-RESULTS.md §3 measures why that separation is
// mandatory rather than stylistic.
//
// Every operation has two paths, selected by MATHF_IF_CONSTEVAL: a scalar path
// for constant evaluation and an intrinsic path for runtime. The Phase 0 spike
// verified the wrapper is free -- runtime output is instruction-identical to
// DirectXMath.
//
// Phase 0 scope: only the operations the spike validated. Phase 1 extends this
// set and splits the backends into arch/simd_{sse,neon,scalar}.hpp.
#ifndef MATHF_VEC_REG_HPP
#define MATHF_VEC_REG_HPP

#include <mathf/config.hpp>

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

struct VecReg {
    RegNative v;
};

// ----------------------------------------------------------------- lane access
// Lane layout is NOT portable: MSVC's __m128 is a union (m128_f32[]), Clang/GCC's
// is a native vector type. Reading lanes any other way breaks one compiler or the
// other, so constant-evaluated code must route through these two helpers.
// See docs/SPIKE-RESULTS.md §1.
// NOT VERIFIED ON MSVC/ARM64: no ARM toolchain was available when this was
// written, so the __n128 branch rests on MSVC's documented layout and is first
// exercised by CI. The Clang/GCC NEON branch is likewise CI-verified only.
//
// Deliberately not written with vst1q_f32/vld1q_f32, which would be the obvious
// portable spelling: those are not constant-evaluable, and these two helpers
// exist specifically to serve the compile-time path.
MATHF_NODISCARD MATHF_INLINE constexpr float Lane(const VecReg& r, int i) noexcept {
#if MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_SSE
    return r.v.m128_f32[i];
#elif MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_NEON
    return r.v.n128_f32[i];
#elif MATHF_SIMD_SSE || MATHF_SIMD_NEON
    return r.v[i];
#else
    return r.v.f[i];
#endif
}

MATHF_INLINE constexpr void SetLane(VecReg& r, int i, float x) noexcept {
#if MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_SSE
    r.v.m128_f32[i] = x;
#elif MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_NEON
    r.v.n128_f32[i] = x;
#elif MATHF_SIMD_SSE || MATHF_SIMD_NEON
    r.v[i] = x;
#else
    r.v.f[i] = x;
#endif
}

// ---------------------------------------------------------------- construction
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Set(float x, float y, float z, float w) noexcept {
    MATHF_IF_CONSTEVAL {
        // Aggregate-initialized rather than default-constructed and then written
        // lane by lane. The mutate-in-place form leaves an indexable array in the
        // frame, which makes MSVC's /GS analysis insert a stack cookie -- and
        // because these functions are force-inlined, that cookie propagates into
        // the caller even though the branch is dead at runtime. Measured: a
        // 64-iteration caller went from 70 instructions (DirectXMath parity) to
        // 77 with two __security_cookie references.
#if MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_SSE
        return VecReg{__m128{x, y, z, w}};
#elif MATHF_MSVC_INTRINSIC_UNION && MATHF_SIMD_NEON
        return VecReg{__n128{x, y, z, w}};
#elif MATHF_SIMD_SSE
        return VecReg{__m128{x, y, z, w}};
#elif MATHF_SIMD_NEON
        return VecReg{float32x4_t{x, y, z, w}};
#else
        return VecReg{RegNative{{x, y, z, w}}};
#endif
    }
#if MATHF_SIMD_SSE
    return VecReg{_mm_set_ps(w, z, y, x)};   // _mm_set_ps takes reverse lane order
#elif MATHF_SIMD_NEON
    // Built through vld1q_f32 rather than brace-initializing float32x4_t: the
    // latter is a compiler vector-extension form that GCC and Clang do not
    // accept identically.
    const float lanes[4] = {x, y, z, w};
    return VecReg{vld1q_f32(lanes)};
#else
    return VecReg{RegNative{{x, y, z, w}}};
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Splat(float s) noexcept {
    MATHF_IF_CONSTEVAL { return Set(s, s, s, s); }
#if MATHF_SIMD_SSE
    return VecReg{_mm_set1_ps(s)};
#elif MATHF_SIMD_NEON
    return VecReg{vdupq_n_f32(s)};
#else
    return Set(s, s, s, s);
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Zero() noexcept {
    return Splat(0.0f);
}

// ------------------------------------------------------------------ arithmetic
// Each operation states its scalar semantics exactly once, above the backend
// selection, so the compile-time path and the scalar fallback cannot drift apart
// from each other or between backends as operations are edited.
namespace detail {

// Fully unrolled with literal lane indices, and building the result through Set
// rather than mutating one in place -- both for the /GS reason described in Set.
template <typename Op>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg ApplyLanewise(VecReg a, VecReg b,
                                                            Op op) noexcept {
    return Set(op(Lane(a, 0), Lane(b, 0)), op(Lane(a, 1), Lane(b, 1)),
               op(Lane(a, 2), Lane(b, 2)), op(Lane(a, 3), Lane(b, 3)));
}

} // namespace detail

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Add(VecReg a, VecReg b) noexcept {
    const auto scalar = [](float x, float y) noexcept { return x + y; };
#if MATHF_SIMD_SSE
    MATHF_IF_CONSTEVAL { return detail::ApplyLanewise(a, b, scalar); }
    return VecReg{_mm_add_ps(a.v, b.v)};
#elif MATHF_SIMD_NEON
    MATHF_IF_CONSTEVAL { return detail::ApplyLanewise(a, b, scalar); }
    return VecReg{vaddq_f32(a.v, b.v)};
#else
    return detail::ApplyLanewise(a, b, scalar);
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Sub(VecReg a, VecReg b) noexcept {
    const auto scalar = [](float x, float y) noexcept { return x - y; };
#if MATHF_SIMD_SSE
    MATHF_IF_CONSTEVAL { return detail::ApplyLanewise(a, b, scalar); }
    return VecReg{_mm_sub_ps(a.v, b.v)};
#elif MATHF_SIMD_NEON
    MATHF_IF_CONSTEVAL { return detail::ApplyLanewise(a, b, scalar); }
    return VecReg{vsubq_f32(a.v, b.v)};
#else
    return detail::ApplyLanewise(a, b, scalar);
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Mul(VecReg a, VecReg b) noexcept {
    const auto scalar = [](float x, float y) noexcept { return x * y; };
#if MATHF_SIMD_SSE
    MATHF_IF_CONSTEVAL { return detail::ApplyLanewise(a, b, scalar); }
    return VecReg{_mm_mul_ps(a.v, b.v)};
#elif MATHF_SIMD_NEON
    MATHF_IF_CONSTEVAL { return detail::ApplyLanewise(a, b, scalar); }
    return VecReg{vmulq_f32(a.v, b.v)};
#else
    return detail::ApplyLanewise(a, b, scalar);
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Div(VecReg a, VecReg b) noexcept {
    const auto scalar = [](float x, float y) noexcept { return x / y; };
#if MATHF_SIMD_SSE
    MATHF_IF_CONSTEVAL { return detail::ApplyLanewise(a, b, scalar); }
    return VecReg{_mm_div_ps(a.v, b.v)};
#elif MATHF_SIMD_NEON
    MATHF_IF_CONSTEVAL { return detail::ApplyLanewise(a, b, scalar); }
    return VecReg{vdivq_f32(a.v, b.v)};
#else
    return detail::ApplyLanewise(a, b, scalar);
#endif
}

// Fused multiply-add: a * b + c. Uses a real FMA instruction where available,
// which is both faster and more accurate than a separate multiply and add.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
MulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    MATHF_IF_CONSTEVAL { return Add(Mul(a, b), c); }
#if MATHF_SIMD_SSE && MATHF_HAS_FMA
    return VecReg{_mm_fmadd_ps(a.v, b.v, c.v)};
#elif MATHF_SIMD_SSE
    return VecReg{_mm_add_ps(_mm_mul_ps(a.v, b.v), c.v)};
#elif MATHF_SIMD_NEON
    return VecReg{vfmaq_f32(c.v, a.v, b.v)};
#else
    return Add(Mul(a, b), c);
#endif
}

// ------------------------------------------------------------------ reductions
// Returns the 4-component dot product splatted across all lanes.
// SSE4.1's dpps does this in one instruction; without it the fallback costs three
// and loses to DirectXMath (docs/SPIKE-RESULTS.md §4).
namespace detail {

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Dot4Scalar(VecReg a, VecReg b) noexcept {
    const float s = Lane(a, 0) * Lane(b, 0) + Lane(a, 1) * Lane(b, 1)
                  + Lane(a, 2) * Lane(b, 2) + Lane(a, 3) * Lane(b, 3);
    return Splat(s);
}

} // namespace detail

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot4(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return detail::Dot4Scalar(a, b); }
#if MATHF_SIMD_SSE && MATHF_HAS_SSE4
    return VecReg{_mm_dp_ps(a.v, b.v, 0xFF)};
#elif MATHF_SIMD_SSE
    const __m128 m = _mm_mul_ps(a.v, b.v);
    const __m128 t = _mm_add_ps(m, _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 3, 0, 1)));
    return VecReg{_mm_add_ps(t, _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 3, 2)))};
#elif MATHF_SIMD_NEON
    return VecReg{vdupq_n_f32(vaddvq_f32(vmulq_f32(a.v, b.v)))};
#else
    return detail::Dot4Scalar(a, b);
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL GetX(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return Lane(a, 0); }
#if MATHF_SIMD_SSE
    return _mm_cvtss_f32(a.v);
#elif MATHF_SIMD_NEON
    return vgetq_lane_f32(a.v, 0);
#else
    return Lane(a, 0);
#endif
}

} // namespace mathf

#endif // MATHF_VEC_REG_HPP
