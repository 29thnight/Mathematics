// Prints what mathematics/config.hpp detected on this toolchain. CI runs this on every
// compiler/arch combination so a misdetected backend shows up as a visible line
// rather than as a silent fallback to scalar code.

#include <mathematics/config.hpp>

#include <cstdio>

int main() {
    std::printf("Mathematics configuration report\n");
    std::printf("--------------------------------\n");

    std::printf("compiler        : %s\n",
                MATHEMATICS_COMPILER_CLANG ? "Clang"
                : MATHEMATICS_COMPILER_MSVC ? "MSVC"
                : MATHEMATICS_COMPILER_GCC  ? "GCC"
                                      : "unknown");
    std::printf("__cplusplus     : %ld\n", static_cast<long>(MATHEMATICS_CPLUSPLUS));
    std::printf("C++23           : %s\n", MATHEMATICS_HAS_CPP23 ? "yes" : "no");
    std::printf("  if consteval  : %s\n", MATHEMATICS_HAS_IF_CONSTEVAL ? "yes" : "no");
    std::printf("  m[r, c]       : %s\n", MATHEMATICS_HAS_MULTIDIM_SUBSCRIPT ? "yes" : "no");

    std::printf("arch            : %s\n",
                MATHEMATICS_ARCH_ARM64 ? "ARM64" : MATHEMATICS_ARCH_X86 ? "x86" : "other");
    std::printf("SIMD backend    : %s\n", MATHEMATICS_BACKEND_NAME);
    std::printf("  SSE4.1        : %s\n", MATHEMATICS_HAS_SSE4 ? "yes" : "no");
    std::printf("  AVX           : %s\n", MATHEMATICS_HAS_AVX ? "yes" : "no");
    std::printf("  FMA           : %s\n", MATHEMATICS_HAS_FMA ? "yes" : "no");

    std::printf("intrinsic union : %s\n",
                MATHEMATICS_MSVC_INTRINSIC_UNION ? "yes (MSVC lanes via m128_f32/n128_f32)"
                                           : "no (native vector subscript)");
    // Printed from config.hpp's own macro rather than re-deriving the condition,
    // so this report cannot drift away from what the library actually uses.
    std::printf("call convention : %s\n", MATHEMATICS_CALL_NAME);

    // A scalar backend on a SIMD-capable host means the detection logic broke.
    if (MATHEMATICS_SIMD_SCALAR && (MATHEMATICS_ARCH_X86 || MATHEMATICS_ARCH_ARM64)) {
#if !defined(MATHEMATICS_FORCE_SCALAR)
        std::printf("\nWARNING: scalar fallback selected on a SIMD-capable target.\n");
        return 1;
#endif
    }
    return 0;
}
