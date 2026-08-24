// mathf/scalar.hpp — scalar constants and transcendentals.
//
// These exist because the rest of Phase 4 needs them: an axis-angle quaternion
// needs sine and cosine, Euler extraction needs arc tangent, a perspective
// matrix needs a tangent, and slerp needs an arc cosine. The standard library
// has all of them and none of them are constant-evaluable before C++26, so a
// library that promises `constexpr` everywhere has to supply its own.
//
// ONE implementation, used at compile time and at run time alike. The obvious
// alternative -- a polynomial for constant evaluation and std::sin at run time,
// the way ScalarSqrt is split -- would make `constexpr Quaternion q =
// FromAxisAngle(a, t)` hold visibly different numbers than the same call in a
// function body. Phase 3 spent a long day on exactly that shape of divergence in
// Inverse (docs/PLAN.md, rule 7), and there the two implementations at least had
// a reason to exist. Here they would not: these are the same minimax
// approximations DirectXMath evaluates at run time, so using them everywhere
// costs nothing against the baseline and keeps the two contexts as close as the
// language permits.
//
// As close as permitted is not identical, and the tests measure the gap rather
// than assume it away. Clang contracts the polynomials' multiply-adds into FMA
// instructions at run time; constant evaluation never contracts. So a value
// computed at compile time and the same value computed at run time can differ by
// one ULP there, and do not differ at all on MSVC or GCC. One ULP is the floor
// while the compiler is free to fuse -- no choice of implementation gets below
// it -- and the tests assert that bound instead of an epsilon, so a real
// divergence could not hide inside a generous tolerance.
//
// Accuracy, measured against a double-precision reference and asserted by the
// tests, so these numbers cannot quietly stop being true:
//
//   Sin, Cos      2.7e-07 absolute, flat across the whole admissible range
//   ASin, ACos    4.2e-07 absolute
//   ATan, ATan2   4.6e-07 absolute
//   Tan           1.7e-06 relative, |x| < 1.5
//
// Float epsilon is 1.2e-07, so every one of these is within a few ULP of the
// best a float result can be. "Flat" is worth the word: the naive versions of
// both the range reduction and the arc tangent were accurate near zero and fell
// apart away from it -- 3.1e-02 for Sin at large angles, 3.5e-04 for ATan at
// large arguments -- which is the failure mode that survives a test suite built
// only from small inputs.
#ifndef MATHF_SCALAR_HPP
#define MATHF_SCALAR_HPP

#include <mathf/arch/consteval_ops.hpp>

#include <cmath>

namespace mathf {

// ------------------------------------------------------------------ constants
inline constexpr float kPi        = 3.141592654f;
inline constexpr float kTwoPi     = 6.283185307f;
inline constexpr float kHalfPi    = 1.570796327f;
inline constexpr float kQuarterPi = 0.785398163f;
inline constexpr float kInvPi     = 0.318309886f;
inline constexpr float kInvTwoPi  = 0.159154943f;

inline constexpr float kDegToRad = 0.01745329252f;
inline constexpr float kRadToDeg = 57.29577951f;

MATHF_NODISCARD MATHF_INLINE constexpr float Radians(float degrees) noexcept {
    return degrees * kDegToRad;
}
MATHF_NODISCARD MATHF_INLINE constexpr float Degrees(float radians) noexcept {
    return radians * kRadToDeg;
}

// -------------------------------------------------------------- small helpers
namespace detail {

MATHF_NODISCARD MATHF_INLINE constexpr float AbsScalar(float x) noexcept {
    return x < 0.0f ? -x : x;
}

MATHF_NODISCARD MATHF_INLINE constexpr bool SignBitSet(float x) noexcept {
    return (BitsOf(x) & kSignBit) != 0u;
}

// The one square root the scalar layer needs, split the way the rest of the
// library splits it: consteval_ops::SqrtScalar hand-rolls Newton-Raphson in
// double because std::sqrt is not constant-evaluable, and paying for that at run
// time is a measured disaster -- 4.6x, when it slipped into Normalize during
// Phase 2. Defined here rather than in vector_common.hpp because the inverse
// trigonometric functions below need it and must not drag in the vector layer.
MATHF_NODISCARD MATHF_INLINE constexpr float ScalarSqrt(float x) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::SqrtScalar(x); }
    return std::sqrt(x);
}

// True when the argument is finite and small enough to reduce modulo 2pi with
// the exactness the reduction below relies on.
//
// The bound is not arbitrary. The reduction subtracts `quotient * kTwoPiHi`, and
// that product is only exact while the quotient fits in the bits kTwoPiHi leaves
// free -- 2^17. Past there accuracy decays; well past there the int conversion
// itself overflows, which is undefined behaviour rather than a wrong answer, and
// undefined behaviour during constant evaluation is a compile error. Rejecting
// with NaN at a bound we can actually honour beats returning noise that looks
// like an answer. 2^17 periods is about 820000 radians, or 130000 full turns.
MATHF_NODISCARD MATHF_INLINE constexpr bool Reducible(float x) noexcept {
    return x - x == 0.0f && AbsScalar(x) < 8.2e5f;
}

} // namespace detail

// ------------------------------------------------------------ sine and cosine
// Range reduction to [-pi/2, pi/2] followed by the minimax polynomials
// DirectXMath uses in XMScalarSinCos: 11 degrees for sine, 10 for cosine, both
// odd/even in the reduced argument so they evaluate in y squared.
namespace detail {

struct SinCosPair {
    float sin;
    float cos;
};

MATHF_NODISCARD MATHF_INLINE constexpr SinCosPair
SinCosImpl(float radians) noexcept {
    if (!Reducible(radians)) return SinCosPair{consteval_ops::kQuietNaN,
                                               consteval_ops::kQuietNaN};

    // Nearest multiple of 2pi, removed. Rounding through int rather than a
    // library round keeps this constant-evaluable.
    const float scaled = radians * kInvTwoPi;
    const float quotient = static_cast<float>(
        static_cast<int>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f));

    // The subtraction happens in double, and that is the whole reason this
    // function is accurate away from zero.
    //
    // In float, `radians - quotient * kTwoPi` is hopeless: kTwoPi is 2pi rounded
    // to 24 bits, so it is already off by ~5e-8, and multiplying that error by a
    // quotient in the thousands puts more error into the reduced angle than the
    // polynomial's own. The polynomial then faithfully computes the sine of the
    // wrong number. Measured worst error over |x| <= 1000: 5.4e-06 that way.
    // Splitting 2pi into two floats (Cody-Waite) gets it to 2.8e-07, but decays
    // again past |x| ~ 1e5 as the products stop being exact.
    //
    // One double multiply and subtract removes the problem outright rather than
    // pushing it further out: 53 bits of mantissa leave the reduced angle good
    // to ~1e-10 across the entire admissible range, and on every target this
    // library builds for, double arithmetic is hardware. Measured 1.5e-07 worst,
    // flat from |x| = 1 to the reduction limit.
    constexpr double kTwoPiExact = 6.28318530717958647692528676655900577;
    const float y0 = static_cast<float>(static_cast<double>(radians) -
                                        static_cast<double>(quotient) * kTwoPiExact);
    float y = y0;

    // Fold [-pi, pi] into [-pi/2, pi/2]. Sine is unchanged by the reflection,
    // cosine changes sign, which is what `sign` carries.
    float sign = 1.0f;
    if (y > kHalfPi) {
        y = kPi - y;
        sign = -1.0f;
    } else if (y < -kHalfPi) {
        y = -kPi - y;
        sign = -1.0f;
    }

    const float y2 = y * y;

    const float s = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2
                        - 0.00019840874f) * y2 + 0.0083333310f) * y2
                      - 0.16666667f) * y2 + 1.0f) * y;

    const float c = ((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2
                       - 0.0013888378f) * y2 + 0.041666638f) * y2
                     - 0.5f) * y2 + 1.0f;

    return SinCosPair{s, sign * c};
}

} // namespace detail

MATHF_NODISCARD MATHF_INLINE constexpr float Sin(float radians) noexcept {
    return detail::SinCosImpl(radians).sin;
}

MATHF_NODISCARD MATHF_INLINE constexpr float Cos(float radians) noexcept {
    return detail::SinCosImpl(radians).cos;
}

// Both at once. Every rotation constructor needs the pair, and the reduction --
// the expensive half -- is shared, so asking for them separately does the work
// twice.
MATHF_INLINE constexpr void SinCos(float radians, float& sinOut,
                                   float& cosOut) noexcept {
    const detail::SinCosPair p = detail::SinCosImpl(radians);
    sinOut = p.sin;
    cosOut = p.cos;
}

MATHF_NODISCARD MATHF_INLINE constexpr float Tan(float radians) noexcept {
    const detail::SinCosPair p = detail::SinCosImpl(radians);
    return p.sin / p.cos;
}

// --------------------------------------------------------------- inverse trig
// Seven-degree minimax in |x|, scaled by sqrt(1 - |x|), again following
// DirectXMath. The square root is what carries the vertical tangent at the ends
// of the domain, where a plain polynomial in x cannot follow the curve.
namespace detail {

MATHF_NODISCARD MATHF_INLINE constexpr float ACosCore(float x) noexcept {
    const float p = ((((((-0.0012624911f * x + 0.0066700901f) * x
                         - 0.0170881256f) * x + 0.0308918810f) * x
                       - 0.0501743046f) * x + 0.0889789874f) * x
                     - 0.2145988016f) * x + 1.5707963050f;
    return p * ScalarSqrt(1.0f - x);
}

} // namespace detail

// Out-of-domain input is clamped rather than turned into NaN. A dot product of
// two unit vectors is the usual argument here and rounding routinely pushes it a
// few ULP past one; a NaN at that point would be a bug report, not a diagnosis.
MATHF_NODISCARD MATHF_INLINE constexpr float ACos(float value) noexcept {
    if (value != value) return consteval_ops::kQuietNaN;
    const float v = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
    const bool nonNegative = v >= 0.0f;
    const float x = detail::AbsScalar(v);
    const float r = detail::ACosCore(x);
    return nonNegative ? r : kPi - r;
}

MATHF_NODISCARD MATHF_INLINE constexpr float ASin(float value) noexcept {
    if (value != value) return consteval_ops::kQuietNaN;
    const float v = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
    const bool nonNegative = v >= 0.0f;
    const float x = detail::AbsScalar(v);
    const float r = kHalfPi - detail::ACosCore(x);
    return nonNegative ? r : -r;
}

// Arc tangent by way of the identity atan(x) = asin(x / sqrt(1 + x*x)), which
// reuses the polynomial above instead of introducing a fourth one.
//
// Only for |x| <= 1. Beyond that the identity is numerically useless: the
// argument to ASin closes on +/-1, where the arc sine's slope runs to infinity,
// so a rounding error of one ULP in the argument becomes an unbounded error in
// the answer. Measured 3.5e-04 at |x| = 5000 before the fold below was added.
// Reflecting through atan(x) = pi/2 - atan(1/x) keeps the argument in the half
// of the domain where the identity is well conditioned, and costs one divide.
MATHF_NODISCARD MATHF_INLINE constexpr float ATan(float value) noexcept {
    if (value != value) return consteval_ops::kQuietNaN;
    if (value == consteval_ops::kInfinity) return kHalfPi;
    if (value == -consteval_ops::kInfinity) return -kHalfPi;

    const float a = detail::AbsScalar(value);
    const float x = a > 1.0f ? 1.0f / a : a;
    float r = ASin(x / detail::ScalarSqrt(1.0f + x * x));
    if (a > 1.0f) r = kHalfPi - r;
    return value < 0.0f ? -r : r;
}

// Quadrant-correct, and defined at the axes and at the origin the way the C
// library defines them, so callers do not have to special-case a zero component.
MATHF_NODISCARD MATHF_INLINE constexpr float ATan2(float y, float x) noexcept {
    if (y != y || x != x) return consteval_ops::kQuietNaN;

    if (x == 0.0f) {
        if (y > 0.0f) return kHalfPi;
        if (y < 0.0f) return -kHalfPi;
        // Both zero: sign of x decides, matching std::atan2's treatment of the
        // signed zero.
        return detail::SignBitSet(x) ? kPi : 0.0f;
    }

    const float base = ATan(y / x);
    if (x > 0.0f) return base;
    // Left half plane: shift by pi, toward whichever side y sits on. y == 0 with
    // x negative is +pi by convention on a positive zero.
    return detail::SignBitSet(y) ? base - kPi : base + kPi;
}

} // namespace mathf

#endif // MATHF_SCALAR_HPP
