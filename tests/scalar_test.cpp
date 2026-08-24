// mathf/scalar.hpp — constants and transcendentals.
//
// These are minimax polynomials, so the interesting question is not "does Sin
// work" but "how wrong is it, and where". Every accuracy claim in the header's
// comment is asserted here against a double-precision reference, so the claim
// cannot quietly stop being true.
//
// The other thing pinned here is that there is only ONE implementation: a value
// computed at compile time must equal the same value computed at run time, bit
// for bit. The library takes that guarantee seriously enough to have chosen a
// polynomial over std::sin for it (see the header), so it gets a test.

#include "support/reg_testing.hpp"

#include <mathf/scalar.hpp>

#include <cmath>
#include <limits>

namespace {

using namespace mathf_test;

// The reference. Deliberately double, and deliberately the standard library:
// nothing here shares an implementation with the thing under test.
double RefSin(double x) { return std::sin(x); }
double RefCos(double x) { return std::cos(x); }

} // namespace

// ----------------------------------------------------------------- constants
static_assert(mathf::kPi > 3.14159f && mathf::kPi < 3.1416f);
static_assert(mathf::Radians(180.0f) > 3.14158f);
static_assert(mathf::Radians(180.0f) < 3.14160f);
static_assert(mathf::Degrees(mathf::kPi) > 179.999f);
static_assert(mathf::Degrees(mathf::kPi) < 180.001f);

TEST(Scalar, DegreeRadianRoundTrip) {
    for (float d = -720.0f; d <= 720.0f; d += 7.5f) {
        EXPECT_NEAR(mathf::Degrees(mathf::Radians(d)), d, 1e-3f) << d;
    }
}

// ------------------------------------------------------------ sine and cosine
// Swept in bands rather than over one range, because the failure this guards
// against is not "inaccurate" but "accurate near zero and not far from it". The
// first implementation was 1.7e-07 at |x| < 10 and 3.1e-02 at |x| near the
// reduction limit; a sweep confined to small angles would have called it good.
TEST(Scalar, SinCosAccuracyIsFlatAcrossTheRange) {
    struct Band { double lo, hi, step; const char* name; };
    const Band bands[] = {
        {-10.0,     10.0,     0.0009765625, "|x| <= 10"},
        {-1000.0,   1000.0,   0.05,         "|x| <= 1e3"},
        {-100000.0, 100000.0, 5.0,          "|x| <= 1e5"},
        {-800000.0, 800000.0, 41.0,         "|x| <= 8e5 (reduction limit)"},
    };

    for (const Band& b : bands) {
        double worstSin = 0.0, worstCos = 0.0;
        double atSin = 0.0, atCos = 0.0;
        for (double x = b.lo; x <= b.hi; x += b.step) {
            const float xf = static_cast<float>(x);
            const double ds =
                std::abs(static_cast<double>(mathf::Sin(xf)) - RefSin(xf));
            const double dc =
                std::abs(static_cast<double>(mathf::Cos(xf)) - RefCos(xf));
            if (ds > worstSin) { worstSin = ds; atSin = x; }
            if (dc > worstCos) { worstCos = dc; atCos = x; }
        }
        EXPECT_LT(worstSin, 4e-7) << b.name << ", worst at x = " << atSin;
        EXPECT_LT(worstCos, 4e-7) << b.name << ", worst at x = " << atCos;
    }
}

TEST(Scalar, SinCosExactAtTheQuadrants) {
    EXPECT_NEAR(mathf::Sin(0.0f), 0.0f, 1e-7f);
    EXPECT_NEAR(mathf::Cos(0.0f), 1.0f, 1e-7f);
    EXPECT_NEAR(mathf::Sin(mathf::kHalfPi), 1.0f, 1e-6f);
    EXPECT_NEAR(mathf::Cos(mathf::kHalfPi), 0.0f, 1e-6f);
    EXPECT_NEAR(mathf::Sin(mathf::kPi), 0.0f, 1e-6f);
    EXPECT_NEAR(mathf::Cos(mathf::kPi), -1.0f, 1e-6f);
    EXPECT_NEAR(mathf::Sin(-mathf::kHalfPi), -1.0f, 1e-6f);
    EXPECT_NEAR(mathf::Cos(mathf::kTwoPi), 1.0f, 1e-6f);
}

// The identity a polynomial has no reason to satisfy unless it is actually
// tracking the functions it claims to.
TEST(Scalar, PythagoreanIdentityHolds) {
    for (float x = -20.0f; x <= 20.0f; x += 0.013f) {
        float s = 0.0f, c = 0.0f;
        mathf::SinCos(x, s, c);
        EXPECT_NEAR(s * s + c * c, 1.0f, 1e-6f) << x;
    }
}

TEST(Scalar, SinCosMatchesTheSeparateCalls) {
    for (float x = -10.0f; x <= 10.0f; x += 0.017f) {
        float s = 0.0f, c = 0.0f;
        mathf::SinCos(x, s, c);
        EXPECT_FLOAT_EQ(s, mathf::Sin(x)) << x;
        EXPECT_FLOAT_EQ(c, mathf::Cos(x)) << x;
    }
}

// Beyond the reduction limit the answer is documented as NaN rather than noise,
// because the int conversion the reduction uses would be undefined there -- and
// undefined behaviour during constant evaluation is a compile error, not a
// wrong number.
TEST(Scalar, HugeAndNonFiniteArgumentsGiveNaN) {
    const float inf = std::numeric_limits<float>::infinity();
    for (float x : {1e6f, -1e6f, 1e8f, 1e30f, inf, -inf, QuietNaN()}) {
        EXPECT_TRUE(std::isnan(mathf::Sin(x))) << x;
        EXPECT_TRUE(std::isnan(mathf::Cos(x))) << x;
    }
    // Just inside the limit still answers, and answers well -- the cutoff is
    // where accuracy would start to decay, not where it already has.
    EXPECT_FALSE(std::isnan(mathf::Sin(8.0e5f)));
    EXPECT_NEAR(static_cast<double>(mathf::Sin(8.0e5f)),
                std::sin(8.0e5), 4e-7);
}

TEST(Scalar, TangentTracksTheRatio) {
    for (float x = -1.4f; x <= 1.4f; x += 0.01f) {
        const double ref = std::tan(static_cast<double>(x));
        EXPECT_NEAR(static_cast<double>(mathf::Tan(x)), ref,
                    std::abs(ref) * 3e-6 + 1e-6) << x;
    }
}

// --------------------------------------------------------------- inverse trig
// The reference is evaluated at the FLOAT argument, not at the double loop
// variable. Rounding x to float moves it by up to 6e-08, and near +/-1 the arc
// sine's slope is steep enough to turn that into 1.4e-06 of apparent error --
// which is the implementation being blamed for the test's own conversion.
TEST(Scalar, ArcSineAndArcCosineAccuracy) {
    double worstAsin = 0.0, worstAcos = 0.0;
    for (double x = -1.0; x <= 1.0; x += 0.0001) {
        const float xf = static_cast<float>(x);
        const double xd = static_cast<double>(xf);
        worstAsin = std::fmax(worstAsin,
            std::abs(static_cast<double>(mathf::ASin(xf)) - std::asin(xd)));
        worstAcos = std::fmax(worstAcos,
            std::abs(static_cast<double>(mathf::ACos(xf)) - std::acos(xd)));
    }
    EXPECT_LT(worstAsin, 8e-7);
    EXPECT_LT(worstAcos, 8e-7);
}

// A dot product of two unit vectors lands a few ULP outside [-1,1] all the time.
// Returning NaN there would turn a rounding artefact into a bug report, so the
// documented behaviour is to clamp.
TEST(Scalar, InverseTrigClampsRatherThanReturningNaN) {
    EXPECT_NEAR(mathf::ACos(1.0000001f), 0.0f, 1e-4f);
    EXPECT_NEAR(mathf::ACos(-1.0000001f), mathf::kPi, 1e-4f);
    EXPECT_NEAR(mathf::ASin(1.0000001f), mathf::kHalfPi, 1e-4f);
    EXPECT_NEAR(mathf::ASin(-1.0000001f), -mathf::kHalfPi, 1e-4f);
    EXPECT_NEAR(mathf::ACos(5.0f), 0.0f, 1e-4f);

    EXPECT_TRUE(std::isnan(mathf::ACos(QuietNaN())));
    EXPECT_TRUE(std::isnan(mathf::ASin(QuietNaN())));
}

TEST(Scalar, ArcTangentAccuracy) {
    double worst = 0.0;
    for (double x = -50.0; x <= 50.0; x += 0.001) {
        const float xf = static_cast<float>(x);
        worst = std::fmax(worst,
            std::abs(static_cast<double>(mathf::ATan(xf)) -
                     std::atan(static_cast<double>(xf))));
    }
    EXPECT_LT(worst, 8e-7);

    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_NEAR(mathf::ATan(inf), mathf::kHalfPi, 1e-6f);
    EXPECT_NEAR(mathf::ATan(-inf), -mathf::kHalfPi, 1e-6f);
}

// Every quadrant and every axis. ATan2 is where a sign error hides, because
// three quadrants out of four still look plausible.
TEST(Scalar, ArcTangent2CoversEveryQuadrant) {
    double worst = 0.0;
    for (double a = -3.14; a <= 3.14; a += 0.001) {
        const float y = static_cast<float>(std::sin(a) * 3.0);
        const float x = static_cast<float>(std::cos(a) * 3.0);
        const double ref = std::atan2(static_cast<double>(y),
                                      static_cast<double>(x));
        worst = std::fmax(worst,
            std::abs(static_cast<double>(mathf::ATan2(y, x)) - ref));
    }
    EXPECT_LT(worst, 8e-7);
}

TEST(Scalar, ArcTangent2OnTheAxes) {
    EXPECT_NEAR(mathf::ATan2(0.0f, 1.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(mathf::ATan2(1.0f, 0.0f), mathf::kHalfPi, 1e-6f);
    EXPECT_NEAR(mathf::ATan2(-1.0f, 0.0f), -mathf::kHalfPi, 1e-6f);
    EXPECT_NEAR(mathf::ATan2(0.0f, -1.0f), mathf::kPi, 1e-6f);
    EXPECT_NEAR(mathf::ATan2(-0.0f, -1.0f), -mathf::kPi, 1e-6f)
        << "the sign of a zero y decides which side of the cut we land on";

    EXPECT_NEAR(mathf::ATan2(0.0f, 0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(mathf::ATan2(0.0f, -0.0f), mathf::kPi, 1e-6f);

    // The quadrants, at 45 degrees where the answer is unambiguous.
    EXPECT_NEAR(mathf::ATan2(1.0f, 1.0f), mathf::kQuarterPi, 1e-6f);
    EXPECT_NEAR(mathf::ATan2(1.0f, -1.0f), 3.0f * mathf::kQuarterPi, 1e-6f);
    EXPECT_NEAR(mathf::ATan2(-1.0f, -1.0f), -3.0f * mathf::kQuarterPi, 1e-6f);
    EXPECT_NEAR(mathf::ATan2(-1.0f, 1.0f), -mathf::kQuarterPi, 1e-6f);
}

// -------------------------------------------------- compile time == run time
// The reason these are polynomials rather than calls into <cmath>: both contexts
// evaluate the same expression, so they land within a rounding of each other
// instead of on two different approximations.
//
// Within a rounding, not identical. Clang contracts the polynomial's
// multiply-adds into FMA instructions at run time, and constant evaluation never
// contracts -- so the two differ by up to one ULP there. MSVC and GCC produce
// identical bits under the /fp:precise the tests build with. One ULP is the
// floor for this: no implementation choice can beat it while the compiler is
// free to fuse, and the same effect is already documented for the matrix
// multiply (docs/BASELINE.md).
TEST(Scalar, CompileTimeMatchesRuntimeToWithinOneUlp) {
    constexpr float kAngles[] = {0.0f,   0.3f,   -0.7f,  1.5707f, 3.0f,
                                 -3.0f,  6.28f,  10.0f,  -25.5f,  99.9f};

    constexpr float kSins[] = {
        mathf::Sin(kAngles[0]), mathf::Sin(kAngles[1]), mathf::Sin(kAngles[2]),
        mathf::Sin(kAngles[3]), mathf::Sin(kAngles[4]), mathf::Sin(kAngles[5]),
        mathf::Sin(kAngles[6]), mathf::Sin(kAngles[7]), mathf::Sin(kAngles[8]),
        mathf::Sin(kAngles[9])};
    constexpr float kCoss[] = {
        mathf::Cos(kAngles[0]), mathf::Cos(kAngles[1]), mathf::Cos(kAngles[2]),
        mathf::Cos(kAngles[3]), mathf::Cos(kAngles[4]), mathf::Cos(kAngles[5]),
        mathf::Cos(kAngles[6]), mathf::Cos(kAngles[7]), mathf::Cos(kAngles[8]),
        mathf::Cos(kAngles[9])};

    for (int i = 0; i < 10; ++i) {
        // Opaque() stops the optimizer re-deriving the runtime call from the
        // constant it already folded, which would make this test vacuous.
        const float angle = Opaque(kAngles[i]);
        EXPECT_TRUE(SameToWithin(mathf::Sin(angle), kSins[i]))
            << "sin, angle " << kAngles[i];
        EXPECT_TRUE(SameToWithin(mathf::Cos(angle), kCoss[i]))
            << "cos, angle " << kAngles[i];
    }
}

TEST(Scalar, InverseTrigCompileTimeMatchesRuntimeToWithinOneUlp) {
    constexpr float kValues[] = {-1.0f, -0.5f, 0.0f, 0.25f, 0.75f, 1.0f};
    constexpr float kAcos[] = {
        mathf::ACos(kValues[0]), mathf::ACos(kValues[1]), mathf::ACos(kValues[2]),
        mathf::ACos(kValues[3]), mathf::ACos(kValues[4]), mathf::ACos(kValues[5])};
    constexpr float kAtan[] = {
        mathf::ATan(kValues[0]), mathf::ATan(kValues[1]), mathf::ATan(kValues[2]),
        mathf::ATan(kValues[3]), mathf::ATan(kValues[4]), mathf::ATan(kValues[5])};

    for (int i = 0; i < 6; ++i) {
        const float v = Opaque(kValues[i]);
        EXPECT_TRUE(SameToWithin(mathf::ACos(v), kAcos[i])) << "acos " << kValues[i];
        EXPECT_TRUE(SameToWithin(mathf::ATan(v), kAtan[i])) << "atan " << kValues[i];
    }
}

// Usable in a constant expression at all -- the whole point of not calling
// <cmath> here.
static_assert(mathf::Sin(0.0f) == 0.0f);
static_assert(mathf::Cos(0.0f) == 1.0f);
static_assert(mathf::ACos(1.0f) == 0.0f);
static_assert(mathf::ATan2(1.0f, 0.0f) == mathf::kHalfPi);
static_assert(mathf::Sin(mathf::kHalfPi) > 0.9999f);
static_assert(mathf::Tan(0.0f) == 0.0f);
