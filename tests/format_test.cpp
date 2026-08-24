// std::format support and structured bindings.
//
// Two things are pinned here. The formatters must forward the caller's spec to
// the individual floats rather than inventing their own, so that a spec which
// works on a float works on a vector. bit_and a matrix must print row-major, the
// way the library stores it -- a formatter that transposed would make every
// debug session lie about the convention the whole library is built on.

#include "support/reg_testing.hpp"

#include <mathematics/format.hpp>

#include <string>

namespace {

using math::aabb;
using math::matrix3x3;
using math::matrix4x4;
using math::plane;
using math::quaternion;
using math::ray;
using math::sphere;
using math::vector2;
using math::vector3;
using math::vector4;

} // namespace

// ------------------------------------------------------- structured bindings
// These need no support code: the packed types have only public data members,
// which is all the language asks. The test exists so the property cannot be
// lost -- adding a private member or a base class would break it silently.
TEST(structured_bindings, work_on_every_packed_type) {
    {
        const auto [x, y] = vector2{1, 2};
        EXPECT_FLOAT_EQ(x, 1.0f);
        EXPECT_FLOAT_EQ(y, 2.0f);
    }
    {
        const auto [x, y, z] = vector3{1, 2, 3};
        EXPECT_FLOAT_EQ(x, 1.0f);
        EXPECT_FLOAT_EQ(y, 2.0f);
        EXPECT_FLOAT_EQ(z, 3.0f);
    }
    {
        const auto [x, y, z, w] = vector4{1, 2, 3, 4};
        EXPECT_FLOAT_EQ(x, 1.0f);
        EXPECT_FLOAT_EQ(w, 4.0f);
    }
    {
        // Scalar last, matching storage -- a binding that produced w first
        // would mean the layout had changed.
        const auto [x, y, z, w] = quaternion{0.1f, 0.2f, 0.3f, 0.9f};
        EXPECT_FLOAT_EQ(x, 0.1f);
        EXPECT_FLOAT_EQ(w, 0.9f);
    }
    {
        const auto [center, radius] = sphere{vector3{1, 2, 3}, 4.0f};
        EXPECT_FLOAT_EQ(center.y, 2.0f);
        EXPECT_FLOAT_EQ(radius, 4.0f);
    }
    {
        // Centre and EXTENTS, not min and max -- the binding names say which.
        const auto [center, extents] = aabb{vector3{0, 0, 0}, vector3{1, 2, 3}};
        EXPECT_FLOAT_EQ(center.x, 0.0f);
        EXPECT_FLOAT_EQ(extents.z, 3.0f);
    }
    {
        const auto [origin, direction] = ray{vector3{1, 1, 1}, vector3{0, 0, 1}};
        EXPECT_FLOAT_EQ(origin.x, 1.0f);
        EXPECT_FLOAT_EQ(direction.z, 1.0f);
    }
}

// Bindings must alias the object, not a copy, when taken by reference -- which
// is what makes them usable for mutation.
TEST(structured_bindings, bind_by_reference_alias_the_object) {
    vector3 v{1, 2, 3};
    auto& [x, y, z] = v;
    y = 20.0f;
    EXPECT_FLOAT_EQ(v.y, 20.0f);
}

#if MATHEMATICS_HAS_FORMAT

// ------------------------------------------------------------------- vectors
TEST(format, vectors_print_every_component) {
    EXPECT_EQ(std::format("{}", vector2{1, 2}), "(1, 2)");
    EXPECT_EQ(std::format("{}", vector3{1, 2, 3}), "(1, 2, 3)");
    EXPECT_EQ(std::format("{}", vector4{1, 2, 3, 4}), "(1, 2, 3, 4)");
}

// The point of forwarding the spec rather than parsing it: anything <format>
// accepts for a float works here on the day it lands.
TEST(format, forwards_the_caller_spec_to_each_float) {
    EXPECT_EQ(std::format("{:.2f}", vector3{1, 2, 3}), "(1.00, 2.00, 3.00)");
    EXPECT_EQ(std::format("{:.1f}", vector2{1.25f, -0.75f}), "(1.2, -0.8)");
    EXPECT_EQ(std::format("{:+.1f}", vector2{1.0f, -1.0f}), "(+1.0, -1.0)");
    // A width applies per component, not to the whole thing.
    EXPECT_EQ(std::format("{:5.1f}", vector2{1.0f, 2.0f}), "(  1.0,   2.0)");
}

TEST(format, quaternion_prints_scalar_last) {
    EXPECT_EQ(std::format("{:.1f}", quaternion{0.1f, 0.2f, 0.3f, 0.9f}),
              "(0.1, 0.2, 0.3, 0.9)");
    // The identity is (0,0,0,1) and must read that way round.
    EXPECT_EQ(std::format("{}", quaternion::identity()), "(0, 0, 0, 1)");
}

// ------------------------------------------------------------------ matrices
// The convention check: row-major, one row per line. A transposing formatter
// would print the translation down a column and quietly teach the reader the
// wrong layout.
TEST(format, matrix_prints_row_major) {
    constexpr matrix4x4 translate{1, 0, 0, 0,
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

    constexpr matrix3x3 counting{1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_EQ(std::format("{:.0f}", counting),
              "[1, 2, 3]\n[4, 5, 6]\n[7, 8, 9]");
}

// ------------------------------------------------------------------ geometry
TEST(format, geometry_prints_meaning_not_storage) {
    EXPECT_EQ(std::format("{:.0f}", plane{0, 0, 1, -5}),
              "plane(n=(0, 0, 1), d=-5)");
    EXPECT_EQ(std::format("{:.0f}", sphere{vector3{1, 2, 3}, 4.0f}),
              "sphere(c=(1, 2, 3), r=4)");
    EXPECT_EQ(std::format("{:.0f}", ray{vector3{0, 0, 0}, vector3{0, 0, 1}}),
              "ray(o=(0, 0, 0), d=(0, 0, 1))");

    // A box stores centre and extents but reads as min and max, because that
    // is the question a person is asking when they print one.
    EXPECT_EQ(std::format("{:.0f}", aabb{vector3{0, 0, 0}, vector3{1, 2, 3}}),
              "aabb(min=(-1, -2, -3), max=(1, 2, 3))");
    EXPECT_EQ(std::format("{}", aabb{}), "aabb(empty)");
}

// Formatting must compose: a vector inside a wider sentence, and repeated
// arguments, both have to come out intact.
TEST(format, composes_with_surrounding_text) {
    EXPECT_EQ(std::format("pos={} vel={}", vector3{1, 2, 3}, vector3{0, 0, 1}),
              "pos=(1, 2, 3) vel=(0, 0, 1)");
    // The same formatter object is reused across arguments; a spec left over
    // from the first would corrupt the second.
    EXPECT_EQ(std::format("{:.1f} {} {:.3f}", vector2{1, 2}, vector2{3, 4},
                          vector2{5, 6}),
              "(1.0, 2.0) (3, 4) (5.000, 6.000)");
}

#endif // MATHEMATICS_HAS_FORMAT
