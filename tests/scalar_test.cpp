// mathematics/scalar.hpp — constants and transcendentals.
//
// These are minimax polynomials, so the interesting question is not "does sin
// work" but "how wrong is it, and where". Every accuracy claim in the header's
// comment is asserted here against a double-precision reference, so the claim
// cannot quietly stop being true.
//
// The other thing pinned here is that there is only ONE implementation: a value
// computed at compile time must equal the same value computed at run time, bit
// for bit. The library takes that guarantee seriously enough to have chosen a
// polynomial over std::sin for it (see the header), so it gets a test.

#include "support/reg_testing.hpp"

#include <mathematics/scalar.hpp>

#include <cmath>
#include <limits>

namespace {

using namespace math_test;

// The reference. Deliberately double, and deliberately the standard library:
// nothing here shares an implementation with the thing under test.
double ref_sin(double x) { return std::sin(x); }
double ref_cos(double x) { return std::cos(x); }

} // namespace

// ----------------------------------------------------------------- constants
static_assert(math::pi > 3.14159f && math::pi < 3.1416f);
static_assert(math::radians(180.0f) > 3.14158f);
static_assert(math::radians(180.0f) < 3.14160f);
static_assert(math::degrees(math::pi) > 179.999f);
static_assert(math::degrees(math::pi) < 180.001f);

TEST(scalar, degree_radian_round_trip) {
    for (float d = -720.0f; d <= 720.0f; d += 7.5f) {
        EXPECT_NEAR(math::degrees(math::radians(d)), d, 1e-3f) << d;
    }
}

// ------------------------------------------------------------ sine and cosine
// Swept in bands rather than over one range, because the failure this guards
// against is not "inaccurate" but "accurate near zero and not far from it". The
// first implementation was 1.7e-07 at |x| < 10 and 3.1e-02 at |x| near the
// reduction limit; a sweep confined to small angles would have called it good.
TEST(scalar, sin_cos_accuracy_is_flat_across_the_range) {
    struct band { double lo, hi, step; const char* name; };
    const band bands[] = {
        {-10.0,     10.0,     0.0009765625, "|x| <= 10"},
        {-1000.0,   1000.0,   0.05,         "|x| <= 1e3"},
        {-100000.0, 100000.0, 5.0,          "|x| <= 1e5"},
        {-800000.0, 800000.0, 41.0,         "|x| <= 8e5 (reduction limit)"},
    };

    for (const band& b : bands) {
        double worst_sin = 0.0, worst_cos = 0.0;
        double at_sin = 0.0, at_cos = 0.0;
        for (double x = b.lo; x <= b.hi; x += b.step) {
            const float xf = static_cast<float>(x);
            const double ds =
                std::abs(static_cast<double>(math::sin(xf)) - ref_sin(xf));
            const double dc =
                std::abs(static_cast<double>(math::cos(xf)) - ref_cos(xf));
            if (ds > worst_sin) { worst_sin = ds; at_sin = x; }
            if (dc > worst_cos) { worst_cos = dc; at_cos = x; }
        }
        EXPECT_LT(worst_sin, 4e-7) << b.name << ", worst at x = " << at_sin;
        EXPECT_LT(worst_cos, 4e-7) << b.name << ", worst at x = " << at_cos;
    }
}

TEST(scalar, sin_cos_exact_at_the_quadrants) {
    EXPECT_NEAR(math::sin(0.0f), 0.0f, 1e-7f);
    EXPECT_NEAR(math::cos(0.0f), 1.0f, 1e-7f);
    EXPECT_NEAR(math::sin(math::half_pi), 1.0f, 1e-6f);
    EXPECT_NEAR(math::cos(math::half_pi), 0.0f, 1e-6f);
    EXPECT_NEAR(math::sin(math::pi), 0.0f, 1e-6f);
    EXPECT_NEAR(math::cos(math::pi), -1.0f, 1e-6f);
    EXPECT_NEAR(math::sin(-math::half_pi), -1.0f, 1e-6f);
    EXPECT_NEAR(math::cos(math::two_pi), 1.0f, 1e-6f);
}

// The identity a polynomial has no reason to satisfy unless it is actually
// tracking the functions it claims to.
TEST(scalar, pythagorean_identity_holds) {
    for (float x = -20.0f; x <= 20.0f; x += 0.013f) {
        float s = 0.0f, c = 0.0f;
        math::sin_cos(x, s, c);
        EXPECT_NEAR(s * s + c * c, 1.0f, 1e-6f) << x;
    }
}

TEST(scalar, sin_cos_matches_the_separate_calls) {
    for (float x = -10.0f; x <= 10.0f; x += 0.017f) {
        float s = 0.0f, c = 0.0f;
        math::sin_cos(x, s, c);
        EXPECT_FLOAT_EQ(s, math::sin(x)) << x;
        EXPECT_FLOAT_EQ(c, math::cos(x)) << x;
    }
}

// Beyond the reduction limit the answer is documented as NaN rather than noise,
// because the int conversion the reduction uses would be undefined there -- and
// undefined behaviour during constant evaluation is a compile error, not a
// wrong number.
TEST(scalar, huge_and_non_finite_arguments_give_na_n) {
    const float inf = std::numeric_limits<float>::infinity();
    for (float x : {1e6f, -1e6f, 1e8f, 1e30f, inf, -inf, quiet_nan()}) {
        EXPECT_TRUE(std::isnan(math::sin(x))) << x;
        EXPECT_TRUE(std::isnan(math::cos(x))) << x;
    }
    // Just inside the limit still answers, and answers well -- the cutoff is
    // where accuracy would start to decay, not where it already has.
    EXPECT_FALSE(std::isnan(math::sin(8.0e5f)));
    EXPECT_NEAR(static_cast<double>(math::sin(8.0e5f)),
                std::sin(8.0e5), 4e-7);
}

TEST(scalar, tangent_tracks_the_ratio) {
    for (float x = -1.4f; x <= 1.4f; x += 0.01f) {
        const double ref = std::tan(static_cast<double>(x));
        EXPECT_NEAR(static_cast<double>(math::tan(x)), ref,
                    std::abs(ref) * 3e-6 + 1e-6) << x;
    }
}

// Swept right up to the |x| < 1.5 the header claims, not just the comfortable
// middle: near the pole the cosine underneath is smallest and the division is
// most sensitive, which is exactly where an accuracy claim quietly dies.
TEST(scalar, tangent_accuracy_holds_to_the_claimed_boundary) {
    double worst_rel = 0.0, at = 0.0;
    for (double x = 1.40; x <= 1.499; x += 0.0005) {
        for (double sign : {1.0, -1.0}) {
            const float xf = static_cast<float>(x * sign);
            const double ref = std::tan(static_cast<double>(xf));
            const double rel =
                std::abs(static_cast<double>(math::tan(xf)) - ref) / std::abs(ref);
            if (rel > worst_rel) { worst_rel = rel; at = x * sign; }
        }
    }
    EXPECT_LT(worst_rel, 3e-6) << "worst at x = " << at;
}

// --------------------------------------------------------------- inverse trig
// The reference is evaluated at the FLOAT argument, not at the double loop
// variable. Rounding x to float moves it by up to 6e-08, and near +/-1 the arc
// sine's slope is steep enough to turn that into 1.4e-06 of apparent error --
// which is the implementation being blamed for the test's own conversion.
TEST(scalar, arc_sine_and_arc_cosine_accuracy) {
    double worst_asin = 0.0, worst_acos = 0.0;
    for (double x = -1.0; x <= 1.0; x += 0.0001) {
        const float xf = static_cast<float>(x);
        const double xd = static_cast<double>(xf);
        worst_asin = std::fmax(worst_asin,
            std::abs(static_cast<double>(math::asin(xf)) - std::asin(xd)));
        worst_acos = std::fmax(worst_acos,
            std::abs(static_cast<double>(math::acos(xf)) - std::acos(xd)));
    }
    EXPECT_LT(worst_asin, 8e-7);
    EXPECT_LT(worst_acos, 8e-7);
}

// A dot product of two unit vectors lands a few ULP outside [-1,1] all the time.
// Returning NaN there would turn a rounding artefact into a bug report, so the
// documented behaviour is to clamp.
TEST(scalar, inverse_trig_clamps_rather_than_returning_na_n) {
    EXPECT_NEAR(math::acos(1.0000001f), 0.0f, 1e-4f);
    EXPECT_NEAR(math::acos(-1.0000001f), math::pi, 1e-4f);
    EXPECT_NEAR(math::asin(1.0000001f), math::half_pi, 1e-4f);
    EXPECT_NEAR(math::asin(-1.0000001f), -math::half_pi, 1e-4f);
    EXPECT_NEAR(math::acos(5.0f), 0.0f, 1e-4f);

    EXPECT_TRUE(std::isnan(math::acos(quiet_nan())));
    EXPECT_TRUE(std::isnan(math::asin(quiet_nan())));
}

TEST(scalar, arc_tangent_accuracy) {
    double worst = 0.0;
    for (double x = -50.0; x <= 50.0; x += 0.001) {
        const float xf = static_cast<float>(x);
        worst = std::fmax(worst,
            std::abs(static_cast<double>(math::atan(xf)) -
                     std::atan(static_cast<double>(xf))));
    }
    EXPECT_LT(worst, 8e-7);

    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_NEAR(math::atan(inf), math::half_pi, 1e-6f);
    EXPECT_NEAR(math::atan(-inf), -math::half_pi, 1e-6f);
}

// Every quadrant and every axis. atan2 is where a sign error hides, because
// three quadrants out of four still look plausible.
TEST(scalar, arc_tangent2_covers_every_quadrant) {
    double worst = 0.0;
    for (double a = -3.14; a <= 3.14; a += 0.001) {
        const float y = static_cast<float>(std::sin(a) * 3.0);
        const float x = static_cast<float>(std::cos(a) * 3.0);
        const double ref = std::atan2(static_cast<double>(y),
                                      static_cast<double>(x));
        worst = std::fmax(worst,
            std::abs(static_cast<double>(math::atan2(y, x)) - ref));
    }
    EXPECT_LT(worst, 8e-7);
}

// Both arguments infinite: inf/inf is NaN by IEEE-754, so the general path's
// division cannot express these -- they went through atan's NaN guard and came
// back NaN while std::atan2 defines all four as quadrant diagonals. Found in the
// Phase 4 code review; the fix is an explicit branch, and this pins it.
TEST(scalar, arc_tangent2_with_both_arguments_infinite) {
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_NEAR(math::atan2(inf, inf), math::quarter_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(inf, -inf), 3.0f * math::quarter_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(-inf, inf), -math::quarter_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(-inf, -inf), -3.0f * math::quarter_pi, 1e-6f);

    // one argument infinite keeps working through the ordinary division.
    EXPECT_NEAR(math::atan2(inf, 5.0f), math::half_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(-inf, 5.0f), -math::half_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(3.0f, -inf), math::pi, 1e-6f);
    EXPECT_NEAR(math::atan2(-3.0f, -inf), -math::pi, 1e-6f);
    EXPECT_NEAR(math::atan2(3.0f, inf), 0.0f, 1e-6f);
}

// Magnitude ratios whose division overflows to infinity or underflows to zero.
// Both must land on the axis answers, not on garbage from the intermediate.
TEST(scalar, arc_tangent2_extreme_magnitude_ratios) {
    EXPECT_NEAR(math::atan2(1e30f, 1e-30f), math::half_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(-1e30f, 1e-30f), -math::half_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(1e-30f, 1e30f), 0.0f, 1e-6f);
    EXPECT_NEAR(math::atan2(1e-30f, -1e30f), math::pi, 1e-6f);
}

// The sign of a zero is data: asin(-0) is -0 per the C library, and losing it
// flips which side of the branch cut atan2 lands on. AbsScalar originally used
// `x < 0 ? -x : x`, which hands -0.0f back unchanged because the two zeros
// compare equal -- the bitwise form and the early returns are what these pin.
TEST(scalar, signed_zero_survives_the_inverse_functions) {
    EXPECT_TRUE(std::signbit(math::asin(-0.0f)));
    EXPECT_FALSE(std::signbit(math::asin(0.0f)));
    EXPECT_TRUE(std::signbit(math::atan(-0.0f)));
    EXPECT_FALSE(std::signbit(math::atan(0.0f)));
    EXPECT_TRUE(std::signbit(math::atan2(-0.0f, 1.0f)))
        << "atan2(-0, +x) must be -0, matching std::atan2";
    EXPECT_FALSE(std::signbit(math::atan2(0.0f, 1.0f)));
    EXPECT_TRUE(std::signbit(math::atan2(-3.0f, std::numeric_limits<float>::infinity())))
        << "a negative y over +inf divides to -0 and must stay negative";
}

TEST(scalar, arc_tangent2_on_the_axes) {
    EXPECT_NEAR(math::atan2(0.0f, 1.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(math::atan2(1.0f, 0.0f), math::half_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(-1.0f, 0.0f), -math::half_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(0.0f, -1.0f), math::pi, 1e-6f);
    EXPECT_NEAR(math::atan2(-0.0f, -1.0f), -math::pi, 1e-6f)
        << "the sign of a zero y decides which side of the cut we land on";

    EXPECT_NEAR(math::atan2(0.0f, 0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(math::atan2(0.0f, -0.0f), math::pi, 1e-6f);

    // The quadrants, at 45 degrees where the answer is unambiguous.
    EXPECT_NEAR(math::atan2(1.0f, 1.0f), math::quarter_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(1.0f, -1.0f), 3.0f * math::quarter_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(-1.0f, -1.0f), -3.0f * math::quarter_pi, 1e-6f);
    EXPECT_NEAR(math::atan2(-1.0f, 1.0f), -math::quarter_pi, 1e-6f);
}

// -------------------------------------------------- compile time == run time
// The reason these are polynomials rather than calls into <cmath>: both contexts
// evaluate the same expression, so they land within a rounding of each other
// instead of on two different approximations.
//
// Within a rounding, not identical. Clang contracts the polynomial's
// multiply-adds into FMA instructions at run time, and constant evaluation never
// contracts -- so the two differ by up to one ULP there. MSVC and GCC produce
// identical bits under the /fp:precise the tests build with. one ULP is the
// floor for this: no implementation choice can beat it while the compiler is
// free to fuse, and the same effect is already documented for the matrix
// multiply (docs/BASELINE.md).
TEST(scalar, compile_time_matches_runtime_to_within_one_ulp) {
    constexpr float angles[] = {0.0f,   0.3f,   -0.7f,  1.5707f, 3.0f,
                                 -3.0f,  6.28f,  10.0f,  -25.5f,  99.9f};

    constexpr float sine_values[] = {
        math::sin(angles[0]), math::sin(angles[1]), math::sin(angles[2]),
        math::sin(angles[3]), math::sin(angles[4]), math::sin(angles[5]),
        math::sin(angles[6]), math::sin(angles[7]), math::sin(angles[8]),
        math::sin(angles[9])};
    constexpr float cosine_values[] = {
        math::cos(angles[0]), math::cos(angles[1]), math::cos(angles[2]),
        math::cos(angles[3]), math::cos(angles[4]), math::cos(angles[5]),
        math::cos(angles[6]), math::cos(angles[7]), math::cos(angles[8]),
        math::cos(angles[9])};

    for (int i = 0; i < 10; ++i) {
        // opaque() stops the optimizer re-deriving the runtime call from the
        // constant it already folded, which would make this test vacuous.
        const float angle = opaque(angles[i]);
        EXPECT_TRUE(same_to_within(math::sin(angle), sine_values[i]))
            << "sin, angle " << angles[i];
        EXPECT_TRUE(same_to_within(math::cos(angle), cosine_values[i]))
            << "cos, angle " << angles[i];
    }
}

TEST(scalar, inverse_trig_compile_time_matches_runtime_to_within_one_ulp) {
    constexpr float input_values[] = {-1.0f, -0.5f, 0.0f, 0.25f, 0.75f, 1.0f};
    constexpr float acos_values[] = {
        math::acos(input_values[0]), math::acos(input_values[1]), math::acos(input_values[2]),
        math::acos(input_values[3]), math::acos(input_values[4]), math::acos(input_values[5])};
    constexpr float atan_values[] = {
        math::atan(input_values[0]), math::atan(input_values[1]), math::atan(input_values[2]),
        math::atan(input_values[3]), math::atan(input_values[4]), math::atan(input_values[5])};

    for (int i = 0; i < 6; ++i) {
        const float v = opaque(input_values[i]);
        EXPECT_TRUE(same_to_within(math::acos(v), acos_values[i])) << "acos " << input_values[i];
        EXPECT_TRUE(same_to_within(math::atan(v), atan_values[i])) << "atan " << input_values[i];
    }
}

// Usable in a constant expression at all -- the whole point of not calling
// <cmath> here.
static_assert(math::sin(0.0f) == 0.0f);
// Angles past pi/2, so the reduction's fold branches run during constant
// evaluation too -- quarter_pi alone never leaves the no-fold path.
static_assert(math::cos(math::pi) < -0.9999f);
static_assert(math::cos(-math::pi) < -0.9999f);
static_assert(math::sin(3.0f) > 0.14f && math::sin(3.0f) < 0.15f);
static_assert(math::sin(-3.0f) < -0.14f);
static_assert(math::atan2(1.0f, 1.0f) > 0.78f);
static_assert(math::cos(0.0f) == 1.0f);
static_assert(math::acos(1.0f) == 0.0f);
static_assert(math::atan2(1.0f, 0.0f) == math::half_pi);
static_assert(math::sin(math::half_pi) > 0.9999f);
static_assert(math::tan(0.0f) == 0.0f);
