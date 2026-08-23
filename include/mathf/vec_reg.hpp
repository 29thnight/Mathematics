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
MATHF_NODISCARD MATHF_INLINE constexpr float Lane(const VecReg& r, int i) noexcept {
#if MATHF_SIMD_SSE && MATHF_MSVC_INTRINSIC_UNION
    return r.v.m128_f32[i];
#elif MATHF_SIMD_SSE || MATHF_SIMD_NEON
    return r.v[i];
#else
    return r.v.f[i];
#endif
}

MATHF_INLINE constexpr void SetLane(VecReg& r, int i, float x) noexcept {
#if MATHF_SIMD_SSE && MATHF_MSVC_INTRINSIC_UNION
    r.v.m128_f32[i] = x;
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
        VecReg r{};
        SetLane(r, 0, x); SetLane(r, 1, y); SetLane(r, 2, z); SetLane(r, 3, w);
        return r;
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
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Add(VecReg a, VecReg b) noexcept {
#if MATHF_SIMD_SSE
    MATHF_IF_CONSTEVAL { VecReg r{}; for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) + Lane(b, i)); return r; }
    return VecReg{_mm_add_ps(a.v, b.v)};
#elif MATHF_SIMD_NEON
    MATHF_IF_CONSTEVAL { VecReg r{}; for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) + Lane(b, i)); return r; }
    return VecReg{vaddq_f32(a.v, b.v)};
#else
    VecReg r{};
    for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) + Lane(b, i));
    return r;
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Sub(VecReg a, VecReg b) noexcept {
#if MATHF_SIMD_SSE
    MATHF_IF_CONSTEVAL { VecReg r{}; for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) - Lane(b, i)); return r; }
    return VecReg{_mm_sub_ps(a.v, b.v)};
#elif MATHF_SIMD_NEON
    MATHF_IF_CONSTEVAL { VecReg r{}; for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) - Lane(b, i)); return r; }
    return VecReg{vsubq_f32(a.v, b.v)};
#else
    VecReg r{};
    for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) - Lane(b, i));
    return r;
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Mul(VecReg a, VecReg b) noexcept {
#if MATHF_SIMD_SSE
    MATHF_IF_CONSTEVAL { VecReg r{}; for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) * Lane(b, i)); return r; }
    return VecReg{_mm_mul_ps(a.v, b.v)};
#elif MATHF_SIMD_NEON
    MATHF_IF_CONSTEVAL { VecReg r{}; for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) * Lane(b, i)); return r; }
    return VecReg{vmulq_f32(a.v, b.v)};
#else
    VecReg r{};
    for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) * Lane(b, i));
    return r;
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Div(VecReg a, VecReg b) noexcept {
#if MATHF_SIMD_SSE
    MATHF_IF_CONSTEVAL { VecReg r{}; for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) / Lane(b, i)); return r; }
    return VecReg{_mm_div_ps(a.v, b.v)};
#elif MATHF_SIMD_NEON
    MATHF_IF_CONSTEVAL { VecReg r{}; for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) / Lane(b, i)); return r; }
    return VecReg{vdivq_f32(a.v, b.v)};
#else
    VecReg r{};
    for (int i = 0; i < 4; ++i) SetLane(r, i, Lane(a, i) / Lane(b, i));
    return r;
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
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot4(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL {
        float s = 0.0f;
        for (int i = 0; i < 4; ++i) s += Lane(a, i) * Lane(b, i);
        return Splat(s);
    }
#if MATHF_SIMD_SSE && MATHF_HAS_SSE4
    return VecReg{_mm_dp_ps(a.v, b.v, 0xFF)};
#elif MATHF_SIMD_SSE
    const __m128 m = _mm_mul_ps(a.v, b.v);
    const __m128 t = _mm_add_ps(m, _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 3, 0, 1)));
    return VecReg{_mm_add_ps(t, _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 3, 2)))};
#elif MATHF_SIMD_NEON
    return VecReg{vdupq_n_f32(vaddvq_f32(vmulq_f32(a.v, b.v)))};
#else
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) s += Lane(a, i) * Lane(b, i);
    return Splat(s);
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
