// Shared helpers for the backend parity tests.
//
// Every backend operation is checked three ways:
//   1. constexpr parity  -- the compile-time path agrees with the runtime path
//   2. reference parity  -- the runtime path agrees with consteval_ops, which is
//                           the single definition of what each operation means
//   3. DirectXMath parity -- results match the baseline library, where it has an
//                            equivalent
//
// (2) is the strongest of the three and is new in Phase 1: consteval_ops is
// compiled into every build regardless of backend, so the oracle lives in the
// same binary as the code under test rather than in a separate scalar build.
#ifndef MATHEMATICS_TESTS_REG_TESTING_HPP
#define MATHEMATICS_TESTS_REG_TESTING_HPP

#include <mathematics/vec_reg.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

namespace math_test {

using math::vec_reg;

// ------------------------------------------------------------------- inspection
inline std::array<float, 4> to_array(vec_reg v) {
    return {math::lane(v, 0), math::lane(v, 1),
            math::lane(v, 2), math::lane(v, 3)};
}

inline std::array<std::uint32_t, 4> to_bits(vec_reg v) {
    return {math::lane_bits(v, 0), math::lane_bits(v, 1),
            math::lane_bits(v, 2), math::lane_bits(v, 3)};
}

// Bit comparison, not value comparison. Masks are all-ones lanes, which read as
// NaN and therefore never compare equal to themselves; -0.0f and +0.0f are the
// opposite problem, comparing equal while being different results.
inline ::testing::AssertionResult bits_equal(vec_reg actual, vec_reg expected) {
    const auto a = to_bits(actual);
    const auto e = to_bits(expected);
    if (a == e) return ::testing::AssertionSuccess();

    ::testing::Message msg;
    msg << "lane bits differ\n  actual  :";
    for (auto x : a) msg << " 0x" << std::hex << x;
    msg << "\n  expected:";
    for (auto x : e) msg << " 0x" << std::hex << x;
    return ::testing::AssertionFailure() << msg;
}

// Every lane within tolerance. Used where a fused or reassociated SIMD result may
// legitimately differ from the scalar reference in the last places.
inline ::testing::AssertionResult near_equal(vec_reg actual, vec_reg expected,
                                            float rel_tol = 1e-5f,
                                            float abs_tol = 1e-6f) {
    const auto a = to_array(actual);
    const auto e = to_array(expected);
    for (int i = 0; i < 4; ++i) {
        if (std::isnan(a[i]) && std::isnan(e[i])) continue;
        // Infinities have to match exactly, sign included: subtracting them
        // yields NaN, which would make the tolerance check below pass anything.
        if (std::isinf(a[i]) || std::isinf(e[i])) {
            if (a[i] == e[i]) continue;
            return ::testing::AssertionFailure()
                   << "lane " << i << ": actual " << a[i] << ", expected " << e[i];
        }
        const float tol = std::max(abs_tol, std::abs(e[i]) * rel_tol);
        if (!(std::abs(a[i] - e[i]) <= tol)) {
            return ::testing::AssertionFailure()
                   << "lane " << i << ": actual " << a[i] << ", expected " << e[i]
                   << ", tolerance " << tol;
        }
    }
    return ::testing::AssertionSuccess();
}

// distance in representable floats. The right unit for "are these the same
// answer computed two ways": an absolute epsilon means different things at
// different magnitudes, and exact equality is too strong wherever one of the two
// paths is allowed to contract a multiply-add into an FMA and the other is not.
inline std::int64_t ulp_diff(float a, float b) {
    if (a == b) return 0;
    if (std::isnan(a) || std::isnan(b)) return INT64_MAX;

    // Map the float ordering onto a monotonic integer ordering: negatives are
    // stored sign-magnitude, so they have to be reflected around zero.
    auto ordered = [](float x) -> std::int64_t {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &x, sizeof bits);
        return (bits & 0x80000000u)
                   ? -static_cast<std::int64_t>(bits & 0x7FFFFFFFu)
                   : static_cast<std::int64_t>(bits);
    };
    const std::int64_t d = ordered(a) - ordered(b);
    return d < 0 ? -d : d;
}

// "Same answer computed two ways", judged on whichever of the two scales is
// meaningful here.
//
// ULP alone is wrong near a zero crossing: cos(pi/2) is about 8e-05, where one
// ULP is 6e-12, so a difference of 2e-08 -- a single fused multiply-add's worth,
// and utterly negligible -- reads as 3527 ULP. An absolute epsilon alone is
// wrong at large magnitudes, where it silently permits a real divergence. So a
// value passes if it is close on EITHER scale: within a few ULP, or within a few
// ULP of one, which is the floor that matters for anything living in [-1, 1].
inline ::testing::AssertionResult
same_to_within(float actual, float expected, std::int64_t max_ulp = 4,
             float abs_floor = 2e-7f) {
    const std::int64_t ulp = ulp_diff(actual, expected);
    const float diff = std::abs(actual - expected);
    if (ulp <= max_ulp || diff <= abs_floor) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "actual " << actual << ", expected " << expected << " ("
           << ulp << " ulp, abs " << diff << ")";
}

// A dot product's error is bounded by the magnitude of its terms, not of its
// result. When the terms nearly cancel -- two vectors of magnitude 100 whose dot
// happens to land near zero -- the absolute error stays proportional to 1e4
// while the result is tiny, so any relative-to-the-result tolerance is wrong by
// construction. This returns the absolute bound for an n-term dot product:
// roughly n * epsilon * sum|a_i * b_i|, with margin.
inline float dot_tolerance(const std::array<float, 4>& a,
                          const std::array<float, 4>& b, int count) {
    float terms = 0.0f;
    for (int i = 0; i < count; ++i) terms += std::abs(a[i] * b[i]);
    return std::max(1e-6f, terms * 2e-6f);
}

// ------------------------------------------------------------------ generation
// Forces a value to be a materialized, rounded float the optimizer cannot see
// through. Without it the reference is not trustworthy under fast-math: the
// compiler may re-derive an input from the expression that produced it, skipping
// a rounding the SIMD path already took (docs/PLAN.md Phase 0 notes).
inline float opaque(float x) {
    volatile float v = x;
    return v;
}

// A random input in both forms, holding identical bits: `f` for hand-written
// expectations, `v` for the code under test.
struct sample {
    std::array<float, 4> f;
    vec_reg v;
};

// Deterministic: a failure reported by CI must reproduce locally.
class random_vectors {
public:
    explicit random_vectors(unsigned seed) : rng_(seed), dist_(-100.0f, 100.0f) {}

    sample next() {
        // Filled in a loop rather than as call arguments: argument evaluation
        // order is unspecified, so building the vector inline would let two
        // compilers draw from the generator in different orders.
        std::array<float, 4> f{};
        for (auto& x : f) x = opaque(dist_(rng_));
        return sample{f, math::set(f[0], f[1], f[2], f[3])};
    }

    // Strictly positive, for square roots and reciprocals.
    sample next_positive() {
        std::array<float, 4> f{};
        for (auto& x : f) x = opaque(std::abs(dist_(rng_)) + 0.5f);
        return sample{f, math::set(f[0], f[1], f[2], f[3])};
    }

private:
    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_;
};

inline constexpr unsigned random_seed = 0x4D617468u;   // 'Math'
inline constexpr int sample_count = 256;

// Values that break naive implementations: signed zeros, infinities, denormals,
// and the extremes of the range. NaN is deliberately excluded -- operations whose
// NaN behaviour is target-specific are tested separately and explicitly.
inline const std::array<float, 12>& edge_values() {
    static const std::array<float, 12> values = {
        0.0f, -0.0f, 1.0f, -1.0f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::min(),          // smallest normal
        std::numeric_limits<float>::denorm_min(),   // smallest denormal
        std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        1e-30f, 1e30f,
    };
    return values;
}

inline float quiet_nan() { return std::numeric_limits<float>::quiet_NaN(); }

} // namespace math_test

#endif // MATHEMATICS_TESTS_REG_TESTING_HPP
