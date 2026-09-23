// Exercises the installed headers through both of the library's paths: the
// constant-evaluated one, which a static_assert forces, and the runtime SIMD one,
// which the volatile keeps the optimizer from folding away. Either path reaching
// the wrong header, or no header, fails the build or the run.

#include <mathematics/mathematics.hpp>
// Not part of the umbrella header, so named separately: every public header has
// to compile warning-free in a user's build.
#include <mathematics/format.hpp>

#include <cstdio>

int main() {
    constexpr math::vector3 x{1.0f, 0.0f, 0.0f};
    constexpr math::vector3 y{0.0f, 1.0f, 0.0f};
    static_assert(math::cross(x, y) == math::vector3{0.0f, 0.0f, 1.0f},
                  "constant-evaluated cross through the installed headers");

    volatile float scale = 2.0f;
    const math::vector3 z = math::cross(x * static_cast<float>(scale), y);
    if (!(z == math::vector3{0.0f, 0.0f, 2.0f})) {
        std::printf("runtime cross: got (%g, %g, %g)\n", z.x, z.y, z.z);
        return 1;
    }

    std::printf("package consumer ok\n");
    return 0;
}
