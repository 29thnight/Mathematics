// mathf/arch/simd_scalar.hpp — portable fallback backend.
//
// Selected when no SIMD backend fits the target, or forced with
// MATHF_FORCE_SCALAR so CI keeps this path compiled and tested. It is a
// correctness fallback, not a performance one: expect several times the cost of
// the SSE or NEON backends.
//
// Almost everything forwards straight to consteval_ops, which already defines
// the semantics in plain scalar arithmetic. The exceptions are the square-root
// family, where consteval_ops must hand-roll Newton-Raphson because std::sqrt is
// not constant-evaluable -- at runtime that would be needlessly slow, so the
// standard library is used instead.
#ifndef MATHF_ARCH_SIMD_SCALAR_HPP
#define MATHF_ARCH_SIMD_SCALAR_HPP

#include <mathf/arch/consteval_ops.hpp>

#include <cmath>

namespace mathf {

// ---------------------------------------------------------------- construction
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Set(float x, float y, float z, float w) noexcept {
    return consteval_ops::Set(x, y, z, w);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Splat(float s) noexcept {
    return consteval_ops::Splat(s);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Zero() noexcept {
    return consteval_ops::Zero();
}

// ------------------------------------------------------------------ load, store
MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL Load(const float* p) noexcept {
    return MakeReg(p[0], p[1], p[2], p[3]);
}

// No alignment requirement to exploit without vector instructions; kept for
// interface parity so callers need not branch on the backend.
MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL LoadAligned(const float* p) noexcept {
    return Load(p);
}

MATHF_INLINE void MATHF_CALL Store(float* p, VecReg a) noexcept {
    p[0] = Lane(a, 0);
    p[1] = Lane(a, 1);
    p[2] = Lane(a, 2);
    p[3] = Lane(a, 3);
}

MATHF_INLINE void MATHF_CALL StoreAligned(float* p, VecReg a) noexcept {
    Store(p, a);
}

// ------------------------------------------------------------------- arithmetic
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Add(VecReg a, VecReg b) noexcept { return consteval_ops::Add(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Sub(VecReg a, VecReg b) noexcept { return consteval_ops::Sub(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Mul(VecReg a, VecReg b) noexcept { return consteval_ops::Mul(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Div(VecReg a, VecReg b) noexcept { return consteval_ops::Div(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Negate(VecReg a) noexcept { return consteval_ops::Negate(a); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Abs(VecReg a) noexcept { return consteval_ops::Abs(a); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
MulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    return consteval_ops::MulAdd(a, b, c);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
MulSub(VecReg a, VecReg b, VecReg c) noexcept {
    return consteval_ops::MulSub(a, b, c);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
NegMulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    return consteval_ops::NegMulAdd(a, b, c);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Min(VecReg a, VecReg b) noexcept { return consteval_ops::Min(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Max(VecReg a, VecReg b) noexcept { return consteval_ops::Max(a, b); }

// ----------------------------------------------------------- roots, reciprocals
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Sqrt(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::Sqrt(a); }
    return MakeReg(std::sqrt(Lane(a, 0)), std::sqrt(Lane(a, 1)),
                   std::sqrt(Lane(a, 2)), std::sqrt(Lane(a, 3)));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL RSqrt(VecReg a) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::RSqrt(a); }
    return MakeReg(1.0f / std::sqrt(Lane(a, 0)), 1.0f / std::sqrt(Lane(a, 1)),
                   1.0f / std::sqrt(Lane(a, 2)), 1.0f / std::sqrt(Lane(a, 3)));
}

// No approximate reciprocal instruction to reach for, so the Est forms are exact
// here. Callers get a correct answer either way; only the cost differs by target.
MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL RSqrtEst(VecReg a) noexcept {
    return RSqrt(a);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Recip(VecReg a) noexcept {
    return consteval_ops::Recip(a);
}

MATHF_NODISCARD MATHF_INLINE VecReg MATHF_CALL RecipEst(VecReg a) noexcept {
    return Recip(a);
}

// ---------------------------------------------------------------------- bitwise
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
And(VecReg a, VecReg b) noexcept { return consteval_ops::And(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
AndNot(VecReg a, VecReg b) noexcept { return consteval_ops::AndNot(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Or(VecReg a, VecReg b) noexcept { return consteval_ops::Or(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Xor(VecReg a, VecReg b) noexcept { return consteval_ops::Xor(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Not(VecReg a) noexcept { return consteval_ops::Not(a); }

// ------------------------------------------------------------------- comparison
MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpEq(VecReg a, VecReg b) noexcept { return consteval_ops::CmpEq(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpNe(VecReg a, VecReg b) noexcept { return consteval_ops::CmpNe(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpLt(VecReg a, VecReg b) noexcept { return consteval_ops::CmpLt(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpLe(VecReg a, VecReg b) noexcept { return consteval_ops::CmpLe(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpGt(VecReg a, VecReg b) noexcept { return consteval_ops::CmpGt(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg MATHF_CALL
CmpGe(VecReg a, VecReg b) noexcept { return consteval_ops::CmpGe(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Select(MaskReg mask, VecReg ifTrue, VecReg ifFalse) noexcept {
    return consteval_ops::Select(mask, ifTrue, ifFalse);
}

// ------------------------------------------------------------------- predicates
MATHF_NODISCARD MATHF_INLINE constexpr int MATHF_CALL
MoveMask(VecReg a) noexcept { return consteval_ops::MoveMask(a); }

MATHF_NODISCARD MATHF_INLINE constexpr bool MATHF_CALL
AllTrue(MaskReg m) noexcept { return consteval_ops::AllTrue(m); }

MATHF_NODISCARD MATHF_INLINE constexpr bool MATHF_CALL
AnyTrue(MaskReg m) noexcept { return consteval_ops::AnyTrue(m); }

// ------------------------------------------------------------------- reductions
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot2(VecReg a, VecReg b) noexcept { return consteval_ops::Dot2(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot3(VecReg a, VecReg b) noexcept { return consteval_ops::Dot3(a, b); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Dot4(VecReg a, VecReg b) noexcept { return consteval_ops::Dot4(a, b); }

// ---------------------------------------------------------------------- shuffle
template <int X, int Y, int Z, int W>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL Shuffle(VecReg a) noexcept {
    return consteval_ops::Shuffle<X, Y, Z, W>(a);
}

template <int X, int Y, int Z, int W>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
Shuffle(VecReg a, VecReg b) noexcept {
    return consteval_ops::Shuffle<X, Y, Z, W>(a, b);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
SplatX(VecReg a) noexcept { return consteval_ops::SplatX(a); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
SplatY(VecReg a) noexcept { return consteval_ops::SplatY(a); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
SplatZ(VecReg a) noexcept { return consteval_ops::SplatZ(a); }

MATHF_NODISCARD MATHF_INLINE constexpr VecReg MATHF_CALL
SplatW(VecReg a) noexcept { return consteval_ops::SplatW(a); }

// ------------------------------------------------------------------ lane readout
MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL
GetX(VecReg a) noexcept { return Lane(a, 0); }

MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL
GetY(VecReg a) noexcept { return Lane(a, 1); }

MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL
GetZ(VecReg a) noexcept { return Lane(a, 2); }

MATHF_NODISCARD MATHF_INLINE constexpr float MATHF_CALL
GetW(VecReg a) noexcept { return Lane(a, 3); }

} // namespace mathf

#endif // MATHF_ARCH_SIMD_SCALAR_HPP
