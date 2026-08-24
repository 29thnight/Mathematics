// mathf/arch/simd_neon.hpp — AArch64 NEON backend.
//
// Verified by CI on AArch64 Linux (GCC and Clang), which is also the only place
// it is exercised -- no ARM toolchain is available locally. The MSVC/ARM64 path
// through __n128 remains unverified: no CI leg builds it. Correctness is
// favoured over instruction count throughout; the places worth tuning are
// marked.
//
// Targets AArch64, which is what _M_ARM64, _M_ARM64EC and __aarch64__ select.
// Several intrinsics used here (vdivq_f32, vsqrtq_f32, vaddvq_f32) do not exist
// on 32-bit ARM, which is out of scope -- config.hpp routes it to the scalar
// backend instead.
#ifndef MATHF_ARCH_SIMD_NEON_HPP
#define MATHF_ARCH_SIMD_NEON_HPP

#include <mathf/arch/consteval_ops.hpp>

#if !MATHF_SIMD_NEON
#  error "simd_neon.hpp included without the NEON backend selected"
#endif

namespace mathf {

namespace detail {

MATHF_NODISCARD MATHF_INLINE uint32x4_t AsBits(VecReg a) noexcept {
    return vreinterpretq_u32_f32(a.v);
}

MATHF_NODISCARD MATHF_INLINE VecReg FromBitsReg(uint32x4_t b) noexcept {
    return VecReg{vreinterpretq_f32_u32(b)};
}

} // namespace detail

// ---------------------------------------------------------------- construction
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Set(float x, float y, float z, float w) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Set(x, y, z, w); }
    // Loaded from a local rather than brace-initializing float32x4_t: the
    // vector-literal form is a compiler extension that GCC and Clang do not
    // spell identically, while vld1q_f32 is plain ACLE.
    const float lanes[4] = {x, y, z, w};
    return VecReg{vld1q_f32(lanes)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Splat(float s) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Splat(s); }
    return VecReg{vdupq_n_f32(s)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Zero() noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Zero(); }
    return VecReg{vdupq_n_f32(0.0f)};
}

// ------------------------------------------------------------------ load, store
// NEON loads tolerate unaligned addresses, so the Aligned variants carry no
// extra instruction here -- they exist for interface parity with SSE.
MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL Load(const float* p) noexcept {
    return VecReg{vld1q_f32(p)};
}

MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL LoadAligned(const float* p) noexcept {
    return VecReg{vld1q_f32(p)};
}

MATHF_INLINE void MATHF_CALL Store(float* p, VecReg a) noexcept {
    vst1q_f32(p, a.v);
}

MATHF_INLINE void MATHF_CALL StoreAligned(float* p, VecReg a) noexcept {
    vst1q_f32(p, a.v);
}

// ------------------------------------------------------------------- arithmetic
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Add(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Add(a, b); }
    return VecReg{vaddq_f32(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Sub(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Sub(a, b); }
    return VecReg{vsubq_f32(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Mul(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Mul(a, b); }
    return VecReg{vmulq_f32(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Div(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Div(a, b); }
    return VecReg{vdivq_f32(a.v, b.v)};
}

// XOR of the sign bit, not vnegq_f32: negating -0.0f must give +0.0f, which is
// what consteval_ops and the SSE path both do.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Negate(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Negate(a); }
    return detail::FromBitsReg(veorq_u32(detail::AsBits(a), vdupq_n_u32(kSignBit)));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Abs(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Abs(a); }
    return detail::FromBitsReg(vandq_u32(detail::AsBits(a), vdupq_n_u32(kAbsMask)));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
MulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::MulAdd(a, b, c); }
    return VecReg{vfmaq_f32(c.v, a.v, b.v)};   // c + a*b
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
MulSub(VecReg a, VecReg b, VecReg c) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::MulSub(a, b, c); }
    // -c + a*b, computed directly. The tempting spelling -(c - a*b) via
    // vnegq_f32(vfmsq_f32(...)) gives the wrong zero sign when a*b == c exactly:
    // the subtraction yields +0.0 and the negate then flips it to -0.0, while
    // every other backend and consteval_ops produce +0.0.
    return VecReg{vfmaq_f32(vnegq_f32(c.v), a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
NegMulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::NegMulAdd(a, b, c); }
    return VecReg{vfmsq_f32(c.v, a.v, b.v)};   // c - a*b
}

// KNOWN DIVERGENCES from the SSE backend and from consteval_ops, in two
// distinct cases. DirectXMath carries the same split and leaves both
// unspecified; matching x86 would cost a compare and a select on every call, for
// behaviour no caller should be relying on. Tests assert both explicitly per
// target rather than excluding them.
//
//   NaN    ARM FMIN/FMAX return a quiet NaN when either operand is NaN.
//          minps/maxps return the second operand. So Min(NaN, x) is NaN here
//          and x on x86.
//
//   ±0.0   ARM FMIN/FMAX treat -0.0 as strictly less than +0.0 regardless of
//          operand order. minps/maxps compare them equal and fall through to
//          the second operand. So Min(-0.0, +0.0) is -0.0 here and +0.0 on x86.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Min(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Min(a, b); }
    return VecReg{vminq_f32(a.v, b.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Max(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Max(a, b); }
    return VecReg{vmaxq_f32(a.v, b.v)};
}

// ----------------------------------------------------------- roots, reciprocals
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Sqrt(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Sqrt(a); }
    return VecReg{vsqrtq_f32(a.v)};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL RSqrt(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::RSqrt(a); }
    return VecReg{vdivq_f32(vdupq_n_f32(1.0f), vsqrtq_f32(a.v))};
}

// One Newton-Raphson step on top of the estimate. ARM's vrsqrteq_f32 alone is
// specified to roughly 8 bits, well short of the ~12 that SSE's rsqrtps gives,
// and the refinement step is what the instruction is designed to be paired with.
// Still far cheaper than the exact form, and it keeps Est meaning the same thing
// to callers on both targets.
MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL RSqrtEst(VecReg a) noexcept {
    const float32x4_t e = vrsqrteq_f32(a.v);
    return VecReg{vmulq_f32(e, vrsqrtsq_f32(vmulq_f32(a.v, e), e))};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Recip(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Recip(a); }
    return VecReg{vdivq_f32(vdupq_n_f32(1.0f), a.v)};
}

// Refined for the same reason as RSqrtEst above.
MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL RecipEst(VecReg a) noexcept {
    const float32x4_t e = vrecpeq_f32(a.v);
    return VecReg{vmulq_f32(e, vrecpsq_f32(a.v, e))};
}

// ---------------------------------------------------------------------- bitwise
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
And(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::And(a, b); }
    return detail::FromBitsReg(vandq_u32(detail::AsBits(a), detail::AsBits(b)));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
AndNot(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::AndNot(a, b); }
    // vbicq_u32(x, y) is x & ~y, so the operands swap to produce ~a & b.
    return detail::FromBitsReg(vbicq_u32(detail::AsBits(b), detail::AsBits(a)));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Or(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Or(a, b); }
    return detail::FromBitsReg(vorrq_u32(detail::AsBits(a), detail::AsBits(b)));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Xor(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Xor(a, b); }
    return detail::FromBitsReg(veorq_u32(detail::AsBits(a), detail::AsBits(b)));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Not(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Not(a); }
    return detail::FromBitsReg(vmvnq_u32(detail::AsBits(a)));
}

// ------------------------------------------------------------------- comparison
MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpEq(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpEq(a, b); }
    return detail::FromBitsReg(vceqq_f32(a.v, b.v));
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpNe(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpNe(a, b); }
    return detail::FromBitsReg(vmvnq_u32(vceqq_f32(a.v, b.v)));
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpLt(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpLt(a, b); }
    return detail::FromBitsReg(vcltq_f32(a.v, b.v));
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpLe(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpLe(a, b); }
    return detail::FromBitsReg(vcleq_f32(a.v, b.v));
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpGt(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpGt(a, b); }
    return detail::FromBitsReg(vcgtq_f32(a.v, b.v));
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpGe(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::CmpGe(a, b); }
    return detail::FromBitsReg(vcgeq_f32(a.v, b.v));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Select(MaskReg mask, VecReg ifTrue, VecReg ifFalse) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Select(mask, ifTrue, ifFalse); }
    return VecReg{vbslq_f32(detail::AsBits(mask), ifTrue.v, ifFalse.v)};
}

// ------------------------------------------------------------------- predicates
// NEON has no movmskps. Each lane's sign bit is isolated, shifted into its own
// bit position, and the four are summed.
MATHF_NODISCARD MATHF_INLINE constexpr int MATHF_CALL MoveMask(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::MoveMask(a); }
    const uint32x4_t signs = vshrq_n_u32(detail::AsBits(a), 31);
    const int32_t positions[4] = {0, 1, 2, 3};
    return static_cast<int>(vaddvq_u32(vshlq_u32(signs, vld1q_s32(positions))));
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
    const float32x4_t m = vmulq_f32(a.v, b.v);
    return VecReg{vdupq_n_f32(vgetq_lane_f32(m, 0) + vgetq_lane_f32(m, 1))};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot3(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Dot3(a, b); }
    const float32x4_t m = vsetq_lane_f32(0.0f, vmulq_f32(a.v, b.v), 3);
    return VecReg{vdupq_n_f32(vaddvq_f32(m))};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot4(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Dot4(a, b); }
    return VecReg{vdupq_n_f32(vaddvq_f32(vmulq_f32(a.v, b.v)))};
}

// ---------------------------------------------------------------------- shuffle
// NEON has no general single-instruction permute, so an arbitrary shuffle is
// built lane by lane. The template indices make each vgetq/vsetq index a
// constant, which the intrinsics require. Splats below use vdupq_laneq_f32,
// which is a single instruction and covers the common cases; a wider table of
// specialisations for patterns the matrix code leans on belongs to Phase 3, once
// there is a benchmark showing which ones matter.
template <int X, int Y, int Z, int W>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Shuffle(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Shuffle<X, Y, Z, W>(a); }
    // Seeded from a.v rather than a zero vector: all four lanes are overwritten
    // below, so materializing a zero first is pure waste.
    float32x4_t r = a.v;
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, X), r, 0);
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, Y), r, 1);
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, Z), r, 2);
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, W), r, 3);
    return VecReg{r};
}

template <int X, int Y, int Z, int W>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Shuffle(VecReg a, VecReg b) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Shuffle<X, Y, Z, W>(a, b); }
    // Seeded from a.v rather than a zero vector: all four lanes are overwritten
    // below, so materializing a zero first is pure waste.
    float32x4_t r = a.v;
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, X), r, 0);
    r = vsetq_lane_f32(vgetq_lane_f32(a.v, Y), r, 1);
    r = vsetq_lane_f32(vgetq_lane_f32(b.v, Z), r, 2);
    r = vsetq_lane_f32(vgetq_lane_f32(b.v, W), r, 3);
    return VecReg{r};
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL SplatX(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::SplatX(a); }
    return VecReg{vdupq_laneq_f32(a.v, 0)};
}
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL SplatY(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::SplatY(a); }
    return VecReg{vdupq_laneq_f32(a.v, 1)};
}
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL SplatZ(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::SplatZ(a); }
    return VecReg{vdupq_laneq_f32(a.v, 2)};
}
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL SplatW(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::SplatW(a); }
    return VecReg{vdupq_laneq_f32(a.v, 3)};
}

// ------------------------------------------------------------------ lane readout
MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL GetX(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return Lane(a, 0); }
    return vgetq_lane_f32(a.v, 0);
}
MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL GetY(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return Lane(a, 1); }
    return vgetq_lane_f32(a.v, 1);
}
MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL GetZ(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return Lane(a, 2); }
    return vgetq_lane_f32(a.v, 2);
}
MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL GetW(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return Lane(a, 3); }
    return vgetq_lane_f32(a.v, 3);
}

} // namespace mathf

#endif // MATHF_ARCH_SIMD_NEON_HPP
