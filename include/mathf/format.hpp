// mathf/format.hpp — std::format support, and structured bindings.
//
// An OPT-IN header, not part of mathf.hpp. Including <format> pulls in a large
// chunk of the standard library, and a header-only math library that dragged it
// into every translation unit would be charging every user for a feature most
// of them never call. Include this one where you print.
//
// Every formatter forwards the caller's format spec to the individual floats,
// so a spec that works on a float works here:
//
//   std::format("{}", v)            -> (1, 2, 3)
//   std::format("{:.2f}", v)        -> (1.00, 2.00, 3.00)
//   std::format("{:8.3e}", v)       -> (1.000e+00, 2.000e+00, 3.000e+00)
//
// A matrix prints its rows the way the library stores them -- row-major, one
// row per line -- so what you read matches m[row][col] rather than the
// transpose. Getting that backwards in a debug print is how an afternoon
// disappears.
//
// STRUCTURED BINDINGS need nothing from this header, or from any other. Every
// packed type here has only public data members, which is all the language
// asks, so `auto [x, y, z] = v;` already works out of the box -- for vectors,
// quaternions, spheres, boxes and rays alike. No tuple protocol is provided,
// because adding one would buy `get<0>(v)` and `std::apply` and cost a pile of
// specializations nobody has asked for. The tests pin that bindings work, so
// the property cannot be lost by accident.
#ifndef MATHF_FORMAT_HPP
#define MATHF_FORMAT_HPP

#include <mathf/geometry.hpp>
#include <mathf/matrix.hpp>
#include <mathf/quaternion.hpp>
#include <mathf/vector.hpp>

// GCC only shipped <format> in 13, and libc++ kept it behind a flag for a
// while. Rather than break the build on a toolchain that is otherwise fine,
// this header becomes empty and MATHF_HAS_FORMAT says so -- callers guard on
// it, and the tests skip themselves.
#if defined(__has_include)
#  if __has_include(<format>)
#    include <format>
#  endif
#endif

#if defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
#  define MATHF_HAS_FORMAT 1
#else
#  define MATHF_HAS_FORMAT 0
#endif

#if MATHF_HAS_FORMAT

#include <string>

namespace mathf::detail {

// The shared body of every formatter here: remember the caller's spec, then
// replay it around each float. Storing the spec rather than parsing it means a
// new <format> float spec works the day it lands, with nothing to update.
struct FloatSpecFormatter {
    std::string spec;

    constexpr auto parse(std::format_parse_context& context) {
        auto it = context.begin();
        const auto end = context.end();
        spec.clear();
        while (it != end && *it != '}') {
            spec.push_back(*it);
            ++it;
        }
        return it;
    }

    template <typename Out>
    Out WriteFloat(Out out, float value) const {
        // Rebuilt per call rather than cached: a formatter object is allowed to
        // be const during formatting, and the cost is nothing next to the
        // formatting itself.
        return std::vformat_to(out, "{:" + spec + "}",
                               std::make_format_args(value));
    }
};

} // namespace mathf::detail

// ------------------------------------------------------------------- vectors
template <>
struct std::formatter<mathf::Vector2> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::Vector2& v, Context& context) const {
        auto out = context.out();
        *out++ = '(';
        out = WriteFloat(out, v.x);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, v.y);
        *out++ = ')';
        return out;
    }
};

template <>
struct std::formatter<mathf::Vector3> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::Vector3& v, Context& context) const {
        auto out = context.out();
        *out++ = '(';
        out = WriteFloat(out, v.x);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, v.y);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, v.z);
        *out++ = ')';
        return out;
    }
};

template <>
struct std::formatter<mathf::Vector4> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::Vector4& v, Context& context) const {
        auto out = context.out();
        *out++ = '(';
        out = WriteFloat(out, v.x);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, v.y);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, v.z);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, v.w);
        *out++ = ')';
        return out;
    }
};

// A quaternion prints in storage order (x, y, z, w), scalar last, matching the
// layout -- so what you read lines up with what a debugger shows.
template <>
struct std::formatter<mathf::Quaternion> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::Quaternion& q, Context& context) const {
        auto out = context.out();
        *out++ = '(';
        out = WriteFloat(out, q.x);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, q.y);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, q.z);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, q.w);
        *out++ = ')';
        return out;
    }
};

// ------------------------------------------------------------------ matrices
// Row-major, one row per line, in the order the library stores them.
template <>
struct std::formatter<mathf::Matrix4x4> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::Matrix4x4& m, Context& context) const {
        auto out = context.out();
        for (int row = 0; row < 4; ++row) {
            *out++ = '[';
            for (int col = 0; col < 4; ++col) {
                if (col != 0) { *out++ = ','; *out++ = ' '; }
                out = WriteFloat(out, m.m[row][col]);
            }
            *out++ = ']';
            if (row != 3) *out++ = '\n';
        }
        return out;
    }
};

template <>
struct std::formatter<mathf::Matrix3x3> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::Matrix3x3& m, Context& context) const {
        auto out = context.out();
        for (int row = 0; row < 3; ++row) {
            *out++ = '[';
            for (int col = 0; col < 3; ++col) {
                if (col != 0) { *out++ = ','; *out++ = ' '; }
                out = WriteFloat(out, m.m[row][col]);
            }
            *out++ = ']';
            if (row != 2) *out++ = '\n';
        }
        return out;
    }
};

// ------------------------------------------------------------------ geometry
// These print their MEANING, not their storage: an AABB shows its minimum and
// maximum because that is what a person reads a box for, even though it stores
// a centre and half-widths. The storage is the machine's business.
template <>
struct std::formatter<mathf::Plane> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::Plane& p, Context& context) const {
        auto out = context.out();
        out = std::format_to(out, "plane(n=(");
        out = WriteFloat(out, p.a);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, p.b);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, p.c);
        out = std::format_to(out, "), d=");
        out = WriteFloat(out, p.d);
        *out++ = ')';
        return out;
    }
};

template <>
struct std::formatter<mathf::Sphere> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::Sphere& s, Context& context) const {
        auto out = context.out();
        out = std::format_to(out, "sphere(c=(");
        out = WriteFloat(out, s.center.x);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, s.center.y);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, s.center.z);
        out = std::format_to(out, "), r=");
        out = WriteFloat(out, s.radius);
        *out++ = ')';
        return out;
    }
};

template <>
struct std::formatter<mathf::AABB> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::AABB& box, Context& context) const {
        const mathf::Vector3 lo = box.Min();
        const mathf::Vector3 hi = box.Max();
        auto out = context.out();
        out = std::format_to(out, "aabb(min=(");
        out = WriteFloat(out, lo.x);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, lo.y);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, lo.z);
        out = std::format_to(out, "), max=(");
        out = WriteFloat(out, hi.x);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, hi.y);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, hi.z);
        out = std::format_to(out, "))");
        return out;
    }
};

template <>
struct std::formatter<mathf::Ray> : mathf::detail::FloatSpecFormatter {
    template <typename Context>
    auto format(const mathf::Ray& ray, Context& context) const {
        auto out = context.out();
        out = std::format_to(out, "ray(o=(");
        out = WriteFloat(out, ray.origin.x);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, ray.origin.y);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, ray.origin.z);
        out = std::format_to(out, "), d=(");
        out = WriteFloat(out, ray.direction.x);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, ray.direction.y);
        *out++ = ',';  *out++ = ' ';
        out = WriteFloat(out, ray.direction.z);
        out = std::format_to(out, "))");
        return out;
    }
};

#endif // MATHF_HAS_FORMAT

#endif // MATHF_FORMAT_HPP
