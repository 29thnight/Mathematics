// std::format support and structured bindings.
//
// Two things are pinned here. The formatters must forward the caller's spec to
// the individual floats rather than inventing their own, so that a spec which
// works on a float works on a vector. And a matrix must print row-major, the
// way the library stores it -- a formatter that transposed would make every
// debug session lie about the convention the whole library is built on.

#include "support/reg_testing.hpp"

#include <mathf/format.hpp>

#include <string>

namespace {

using mathf::AABB;
using mathf::Matrix3x3;
using mathf::Matrix4x4;
using mathf::Plane;
using mathf::Quaternion;
using mathf::Ray;
using mathf::Sphere;
using mathf::Vector2;
using mathf::Vector3;
using mathf::Vector4;

} // namespace

// ------------------------------------------------------- structured bindings
// These need no support code: the packed types have only public data members,
// which is all the language asks. The test exists so the property cannot be
// lost -- adding a private member or a base class would break it silently.
TEST(StructuredBindings, WorkOnEveryPackedType) {
    {
        const auto [x, y] = Vector2{1, 2};
        EXPECT_FLOAT_EQ(x, 1.0f);
        EXPECT_FLOAT_EQ(y, 2.0f);
    }
    {
        const auto [x, y, z] = Vector3{1, 2, 3};
        EXPECT_FLOAT_EQ(x, 1.0f);
        EXPECT_FLOAT_EQ(y, 2.0f);
        EXPECT_FLOAT_EQ(z, 3.0f);
    }
    {
        const auto [x, y, z, w] = Vector4{1, 2, 3, 4};
        EXPECT_FLOAT_EQ(x, 1.0f);
        EXPECT_FLOAT_EQ(w, 4.0f);
    }
    {
        // Scalar last, matching storage -- a binding that produced w first
        // would mean the layout had changed.
        const auto [x, y, z, w] = Quaternion{0.1f, 0.2f, 0.3f, 0.9f};
        EXPECT_FLOAT_EQ(x, 0.1f);
        EXPECT_FLOAT_EQ(w, 0.9f);
    }
    {
        const auto [center, radius] = Sphere{Vector3{1, 2, 3}, 4.0f};
        EXPECT_FLOAT_EQ(center.y, 2.0f);
        EXPECT_FLOAT_EQ(radius, 4.0f);
    }
    {
        // Centre and EXTENTS, not min and max -- the binding names say which.
        const auto [center, extents] = AABB{Vector3{0, 0, 0}, Vector3{1, 2, 3}};
        EXPECT_FLOAT_EQ(center.x, 0.0f);
        EXPECT_FLOAT_EQ(extents.z, 3.0f);
    }
    {
        const auto [origin, direction] = Ray{Vector3{1, 1, 1}, Vector3{0, 0, 1}};
        EXPECT_FLOAT_EQ(origin.x, 1.0f);
        EXPECT_FLOAT_EQ(direction.z, 1.0f);
    }
}

// Bindings must alias the object, not a copy, when taken by reference -- which
// is what makes them usable for mutation.
TEST(StructuredBindings, BindByReferenceAliasTheObject) {
    Vector3 v{1, 2, 3};
    auto& [x, y, z] = v;
    y = 20.0f;
    EXPECT_FLOAT_EQ(v.y, 20.0f);
}

#if MATHF_HAS_FORMAT

// ------------------------------------------------------------------- vectors
TEST(Format, VectorsPrintEveryComponent) {
    EXPECT_EQ(std::format("{}", Vector2{1, 2}), "(1, 2)");
    EXPECT_EQ(std::format("{}", Vector3{1, 2, 3}), "(1, 2, 3)");
    EXPECT_EQ(std::format("{}", Vector4{1, 2, 3, 4}), "(1, 2, 3, 4)");
}

// The point of forwarding the spec rather than parsing it: anything <format>
// accepts for a float works here on the day it lands.
TEST(Format, ForwardsTheCallerSpecToEachFloat) {
    EXPECT_EQ(std::format("{:.2f}", Vector3{1, 2, 3}), "(1.00, 2.00, 3.00)");
    EXPECT_EQ(std::format("{:.1f}", Vector2{1.25f, -0.75f}), "(1.2, -0.8)");
    EXPECT_EQ(std::format("{:+.1f}", Vector2{1.0f, -1.0f}), "(+1.0, -1.0)");
    // A width applies per component, not to the whole thing.
    EXPECT_EQ(std::format("{:5.1f}", Vector2{1.0f, 2.0f}), "(  1.0,   2.0)");
}

TEST(Format, QuaternionPrintsScalarLast) {
    EXPECT_EQ(std::format("{:.1f}", Quaternion{0.1f, 0.2f, 0.3f, 0.9f}),
              "(0.1, 0.2, 0.3, 0.9)");
    // The identity is (0,0,0,1) and must read that way round.
    EXPECT_EQ(std::format("{}", Quaternion::Identity()), "(0, 0, 0, 1)");
}

// ------------------------------------------------------------------ matrices
// The convention check: row-major, one row per line. A transposing formatter
// would print the translation down a column and quietly teach the reader the
// wrong layout.
TEST(Format, MatrixPrintsRowMajor) {
    constexpr Matrix4x4 translate{1, 0, 0, 0,
                                  0, 1, 0, 0,
                                  0, 0, 1, 0,
                                  10, 20, 30, 1};
    const std::string text = std::format("{:.0f}", translate);
    EXPECT_EQ(text,
              "[1, 0, 0, 0]\n"
              "[0, 1, 0, 0]\n"
              "[0, 0, 1, 0]\n"
              "[10, 20, 30, 1]")
        << "the translation belongs on the LAST LINE, not down the last column";

    constexpr Matrix3x3 counting{1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_EQ(std::format("{:.0f}", counting),
              "[1, 2, 3]\n[4, 5, 6]\n[7, 8, 9]");
}

// ------------------------------------------------------------------ geometry
TEST(Format, GeometryPrintsMeaningNotStorage) {
    EXPECT_EQ(std::format("{:.0f}", Plane{0, 0, 1, -5}),
              "plane(n=(0, 0, 1), d=-5)");
    EXPECT_EQ(std::format("{:.0f}", Sphere{Vector3{1, 2, 3}, 4.0f}),
              "sphere(c=(1, 2, 3), r=4)");
    EXPECT_EQ(std::format("{:.0f}", Ray{Vector3{0, 0, 0}, Vector3{0, 0, 1}}),
              "ray(o=(0, 0, 0), d=(0, 0, 1))");

    // A box stores centre and extents but reads as min and max, because that
    // is the question a person is asking when they print one.
    EXPECT_EQ(std::format("{:.0f}", AABB{Vector3{0, 0, 0}, Vector3{1, 2, 3}}),
              "aabb(min=(-1, -2, -3), max=(1, 2, 3))");
}

// Formatting must compose: a vector inside a wider sentence, and repeated
// arguments, both have to come out intact.
TEST(Format, ComposesWithSurroundingText) {
    EXPECT_EQ(std::format("pos={} vel={}", Vector3{1, 2, 3}, Vector3{0, 0, 1}),
              "pos=(1, 2, 3) vel=(0, 0, 1)");
    // The same formatter object is reused across arguments; a spec left over
    // from the first would corrupt the second.
    EXPECT_EQ(std::format("{:.1f} {} {:.3f}", Vector2{1, 2}, Vector2{3, 4},
                          Vector2{5, 6}),
              "(1.0, 2.0) (3, 4) (5.000, 6.000)");
}

#endif // MATHF_HAS_FORMAT
