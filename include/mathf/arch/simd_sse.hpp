// mathf/arch/simd_sse.hpp — x86 SSE/AVX backend.
//
// Every function has the same two-part shape: the compile-time branch defers to
// consteval_ops, which defines what the operation means, and the runtime branch
// picks instructions. Nothing here may redefine semantics -- if a result differs
// from consteval_ops, that is a bug in this file, and the parity tests exist to
// catch it.
//
// SSE2 is the floor. SSE4.1 (dpps, blendv) and FMA are used when available;
// MATHF_HAS_SSE4 and MATHF_HAS_FMA say which, and every such path has an SSE2
// fallback below it.
#ifndef MATHF_ARCH_SIMD_SSE_HPP
#define MATHF_ARCH_SIMD_SSE_HPP

#include <mathf/arch/consteval_ops.hpp>

#if !MATHF_SIMD_SSE
#  error "simd_sse.hpp included without the SSE backend selected"
#endif

namespace mathf {

namespace detail {

// _mm_shuffle_ps takes its selectors in reverse lane order, and its two-source
// form draws lanes 0-1 from the first operand and 2-3 from the second. Wrapping
// it in lane order removes a persistent source of transposed-argument bugs.
template <int X, int Y, int Z, int W>
MATHF_NODISCARD MATHF_INLINE __m128 ShuffleRaw(__m128 a, __m128 b) noexcept {
    return _mm_shuffle_ps(a, b, _MM_SHUFFLE(W, Z, Y, X));
}

MATHF_NODISCARD MATHF_INLINE __m128 SignMask() noexcept {
    return _mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(kSignBit)));
}

MATHF_NODISCARD MATHF_INLINE __m128 AbsMask() noexcept {
    return _mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(kAbsMask)));
}

MATHF_NODISCARD MATHF_INLINE __m128 AllOnes() noexcept {
    return _mm_castsi128_ps(_mm_set1_epi32(-1));
}

// Horizontal sum broadcast to every lane, in two shuffle/add pairs. Used only
// where dpps is unavailable.
MATHF_NODISCARD MATHF_INLINE __m128 HSumAll(__m128 m) noexcept {
    const __m128 t = _mm_add_ps(m, ShuffleRaw<1, 0, 3, 2>(m, m));
    return _mm_add_ps(t, ShuffleRaw<2, 3, 0, 1>(t, t));
}

} // namespace detail

// ---------------------------------------------------------------- construction
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Set(float x, float y, float z, float w) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Set(x, y, z, w); }
    return VecReg{_mm_set_ps(w, z, y, x)};   // takes lanes in reverse order
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Splat(float s) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Splat(s); }
    return VecReg{_mm_set1_ps(s)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Zero() noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Zero(); }
    return VecReg{_mm_setzero_ps()};
}

// ------------------------------------------------------------------ load, store
// Load/Store take unaligned pointers; the Aligned forms require 16-byte
// alignment and fault otherwise.
MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL Load(const float* p) noexcept {
    return VecReg{_mm_loadu_ps(p)};
}

MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL LoadAligned(const float* p) noexcept {
    return VecReg{_mm_load_ps(p)};
}

MATHF_INLINE void MATHF_CALL Store(float* p, VecReg a) noexcept {
    _mm_storeu_ps(p, a.v);
}

MATHF_INLINE void MATHF_CALL StoreAligned(float* p, VecReg a) noexcept {
    _mm_store_ps(p, a.v);
}

// ------------------------------------------------------------------- arithmetic
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Add(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Add(a, b); }
    return VecReg{_mm_add_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Sub(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Sub(a, b); }
    return VecReg{_mm_sub_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Mul(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Mul(a, b); }
    return VecReg{_mm_mul_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Div(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Div(a, b); }
    return VecReg{_mm_div_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Negate(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Negate(a); }
    return VecReg{_mm_xor_ps(a.v, detail::SignMask())};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Abs(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Abs(a); }
    return VecReg{_mm_and_ps(a.v, detail::AbsMask())};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
MulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::MulAdd(a, b, c); }
#if MATHF_HAS_FMA
    return VecReg{_mm_fmadd_ps(a.v, b.v, c.v)};
#else
    return VecReg{_mm_add_ps(_mm_mul_ps(a.v, b.v), c.v)};
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
MulSub(VecReg a, VecReg b, VecReg c) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::MulSub(a, b, c); }
#if MATHF_HAS_FMA
    return VecReg{_mm_fmsub_ps(a.v, b.v, c.v)};
#else
    return VecReg{_mm_sub_ps(_mm_mul_ps(a.v, b.v), c.v)};
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
NegMulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::NegMulAdd(a, b, c); }
#if MATHF_HAS_FMA
    return VecReg{_mm_fnmadd_ps(a.v, b.v, c.v)};   // c - a*b
#else
    return VecReg{_mm_sub_ps(c.v, _mm_mul_ps(a.v, b.v))};
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Min(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Min(a, b); }
    return VecReg{_mm_min_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Max(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Max(a, b); }
    return VecReg{_mm_max_ps(a.v, b.v)};
}

// ----------------------------------------------------------- roots, reciprocals
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Sqrt(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Sqrt(a); }
    return VecReg{_mm_sqrt_ps(a.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL RSqrt(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::RSqrt(a); }
    return VecReg{_mm_div_ps(_mm_set1_ps(1.0f), _mm_sqrt_ps(a.v))};
}

// ~12-bit approximation from rsqrtps. Roughly an order of magnitude cheaper than
// the exact form and accurate enough for normalizing render-time vectors, which
// is why DirectXMath exposes the same Est distinction.
MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL RSqrtEst(VecReg a) noexcept {
    return VecReg{_mm_rsqrt_ps(a.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Recip(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Recip(a); }
    return VecReg{_mm_div_ps(_mm_set1_ps(1.0f), a.v)};
}

MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL RecipEst(VecReg a) noexcept {
    return VecReg{_mm_rcp_ps(a.v)};
}

// ---------------------------------------------------------------------- bitwise
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
And(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::And(a, b); }
    return VecReg{_mm_and_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
AndNot(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::AndNot(a, b); }
    return VecReg{_mm_andnot_ps(a.v, b.v)};   // ~a & b
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Or(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Or(a, b); }
    return VecReg{_mm_or_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Xor(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Xor(a, b); }
    return VecReg{_mm_xor_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Not(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Not(a); }
    return VecReg{_mm_xor_ps(a.v, detail::AllOnes())};
}

// ------------------------------------------------------------------- comparison
MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpEq(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpEq(a, b); }
    return VecReg{_mm_cmpeq_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpNe(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpNe(a, b); }
    return VecReg{_mm_cmpneq_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpLt(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpLt(a, b); }
    return VecReg{_mm_cmplt_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpLe(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpLe(a, b); }
    return VecReg{_mm_cmple_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpGt(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpGt(a, b); }
    return VecReg{_mm_cmpgt_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpGe(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpGe(a, b); }
    return VecReg{_mm_cmpge_ps(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Select(MaskReg mask, VecReg ifTrue, VecReg ifFalse) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Select(mask, ifTrue, ifFalse); }
#if MATHF_HAS_SSE4
    // blendv selects per lane on the sign bit alone. For the all-ones/all-zeros
    // masks comparisons produce it agrees with the bitwise blend below; for a
    // hand-built partial mask it does not, which is why masks are documented as
    // all-ones or all-zeros in reg.hpp.
    return VecReg{_mm_blendv_ps(ifFalse.v, ifTrue.v, mask.v)};
#else
    return VecReg{_mm_or_ps(_mm_and_ps(mask.v, ifTrue.v),
                            _mm_andnot_ps(mask.v, ifFalse.v))};
#endif
}

// ------------------------------------------------------------------- predicates
MATHF_NODISCARD MATHF_INLINE constexpr int MATHF_CALL MoveMask(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::MoveMask(a); }
    return _mm_movemask_ps(a.v);
}

MATHF_NODISCARD MATHF_INLINE constexpr bool MATHF_CALL AllTrue(MaskReg m) noexcept {
    return MoveMask(m) == 0xF;
}

MATHF_NODISCARD MATHF_INLINE constexpr bool MATHF_CALL AnyTrue(MaskReg m) noexcept {
    return MoveMask(m) != 0;
}

// ------------------------------------------------------------------- reductions
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot2(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Dot2(a, b); }
#if MATHF_HAS_SSE4
    return VecReg{_mm_dp_ps(a.v, b.v, 0x3F)};   // multiply xy, broadcast to all
#else
    const __m128 m = _mm_mul_ps(a.v, b.v);
    const __m128 xy = _mm_add_ps(m, detail::ShuffleRaw<1, 1, 1, 1>(m, m));
    return VecReg{detail::ShuffleRaw<0, 0, 0, 0>(xy, xy)};
#endif
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot3(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Dot3(a, b); }
#if MATHF_HAS_SSE4
    return VecReg{_mm_dp_ps(a.v, b.v, 0x7F)};   // multiply xyz, broadcast to all
#else
    const __m128 m = _mm_mul_ps(a.v, b.v);
    // Clear w before summing so it cannot contribute.
    const __m128 keepXYZ = _mm_castsi128_ps(_mm_setr_epi32(-1, -1, -1, 0));
    return VecReg{detail::HSumAll(_mm_and_ps(m, keepXYZ))};
#endif
}

// dpps in one instruction where SSE4.1 exists; without it the shuffle chain
// costs three and loses to DirectXMath (docs/SPIKE-RESULTS.md §4).
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot4(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Dot4(a, b); }
#if MATHF_HAS_SSE4
    return VecReg{_mm_dp_ps(a.v, b.v, 0xFF)};
#else
    return VecReg{detail::HSumAll(_mm_mul_ps(a.v, b.v))};
#endif
}

// ---------------------------------------------------------------------- shuffle
template <int X, int Y, int Z, int W>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Shuffle(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Shuffle<X, Y, Z, W>(a); }
    return VecReg{detail::ShuffleRaw<X, Y, Z, W>(a.v, a.v)};
}

template <int X, int Y, int Z, int W>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Shuffle(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Shuffle<X, Y, Z, W>(a, b); }
    return VecReg{detail::ShuffleRaw<X, Y, Z, W>(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL SplatX(VecReg a) noexcept {
    return Shuffle<0, 0, 0, 0>(a);
}
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL SplatY(VecReg a) noexcept {
    return Shuffle<1, 1, 1, 1>(a);
}
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL SplatZ(VecReg a) noexcept {
    return Shuffle<2, 2, 2, 2>(a);
}
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL SplatW(VecReg a) noexcept {
    return Shuffle<3, 3, 3, 3>(a);
}

// ------------------------------------------------------------------ lane readout
MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL GetX(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return Lane(a, 0); }
    return _mm_cvtss_f32(a.v);
}

MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL GetY(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return Lane(a, 1); }
    return _mm_cvtss_f32(detail::ShuffleRaw<1, 1, 1, 1>(a.v, a.v));
}

MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL GetZ(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return Lane(a, 2); }
    return _mm_cvtss_f32(detail::ShuffleRaw<2, 2, 2, 2>(a.v, a.v));
}

MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL GetW(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return Lane(a, 3); }
    return _mm_cvtss_f32(detail::ShuffleRaw<3, 3, 3, 3>(a.v, a.v));
}

} // namespace mathf

#endif // MATHF_ARCH_SIMD_SSE_HPP
