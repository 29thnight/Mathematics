// mathf/config.hpp — compiler, ISA, and language-standard detection.
// Every other Mathf header includes this first. See docs/SPIKE-RESULTS.md for
// the measurements that drove these choices.
#ifndef MATHF_CONFIG_HPP
#define MATHF_CONFIG_HPP

#include <version>
#include <type_traits>   // std::is_constant_evaluated (MATHF_IF_CONSTEVAL fallback)

// ---------------------------------------------------------------- standard
#if defined(_MSVC_LANG)
#  define MATHF_CPLUSPLUS _MSVC_LANG
#else
#  define MATHF_CPLUSPLUS __cplusplus
#endif

#if MATHF_CPLUSPLUS < 202002L
#  error "Mathf requires C++20 or later (MSVC: /std:c++20, others: -std=c++20)."
#endif

#define MATHF_HAS_CPP23 (MATHF_CPLUSPLUS >= 202302L)

// C++23 features are opt-in per-feature: MSVC and Clang adopted them unevenly,
// so each is gated on its own feature-test macro rather than on the standard.
#if MATHF_HAS_CPP23 && defined(__cpp_if_consteval) && __cpp_if_consteval >= 202106L
#  define MATHF_HAS_IF_CONSTEVAL 1
#else
#  define MATHF_HAS_IF_CONSTEVAL 0
#endif

#if MATHF_HAS_CPP23 && defined(__cpp_multidimensional_subscript) \
    && __cpp_multidimensional_subscript >= 202110L
#  define MATHF_HAS_MULTIDIM_SUBSCRIPT 1   // enables m[r, c]
#else
#  define MATHF_HAS_MULTIDIM_SUBSCRIPT 0
#endif

// ---------------------------------------------------------------- compiler
#if defined(__clang__)
#  define MATHF_COMPILER_CLANG 1
#elif defined(_MSC_VER)
#  define MATHF_COMPILER_MSVC 1
#elif defined(__GNUC__)
#  define MATHF_COMPILER_GCC 1
#endif

#ifndef MATHF_COMPILER_CLANG
#  define MATHF_COMPILER_CLANG 0
#endif
#ifndef MATHF_COMPILER_MSVC
#  define MATHF_COMPILER_MSVC 0
#endif
#ifndef MATHF_COMPILER_GCC
#  define MATHF_COMPILER_GCC 0
#endif

// True only for genuine MSVC, where the intrinsic vector types are magic unions
// (__m128 with .m128_f32[], __n128 with .n128_f32[]) rather than native vector
// types. Lane access must go through those members. clang-cl defines _MSC_VER
// too but follows Clang semantics, where the same types are subscriptable.
#define MATHF_MSVC_INTRINSIC_UNION (MATHF_COMPILER_MSVC && !MATHF_COMPILER_CLANG)

// ---------------------------------------------------------- inline & callconv
#if MATHF_COMPILER_MSVC || (MATHF_COMPILER_CLANG && defined(_MSC_VER))
#  define MATHF_INLINE __forceinline
#  define MATHF_NOINLINE __declspec(noinline)
#elif MATHF_COMPILER_CLANG || MATHF_COMPILER_GCC
#  define MATHF_INLINE inline __attribute__((always_inline))
#  define MATHF_NOINLINE __attribute__((noinline))
#else
#  define MATHF_INLINE inline
#  define MATHF_NOINLINE
#endif

// __vectorcall keeps VecReg in xmm registers across non-inlined boundaries,
// matching DirectXMath's XM_CALLCONV. On SysV the default ABI already does this.
//
// _M_ARM64EC must be excluded explicitly: ARM64EC defines _M_X64 for x64
// source compatibility while leaving _M_ARM64 undefined, so testing only for
// those two would enable __vectorcall on a target whose VecReg is backed by
// float32x4_t. DirectXMath's XM_CALLCONV excludes it for the same reason.
#if defined(_M_X64) && (MATHF_COMPILER_MSVC || MATHF_COMPILER_CLANG) \
    && !defined(_M_ARM64) && !defined(_M_ARM64EC) && !defined(MATHF_NO_VECTORCALL)
#  define MATHF_CALL __vectorcall
#  define MATHF_CALL_NAME "__vectorcall"
#else
#  define MATHF_CALL
#  define MATHF_CALL_NAME "default"
#endif

// ---------------------------------------------------------------- architecture
#if defined(_M_ARM64) || defined(__aarch64__) || defined(_M_ARM64EC)
#  define MATHF_ARCH_ARM64 1
#elif defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#  define MATHF_ARCH_X86 1
#endif

#ifndef MATHF_ARCH_ARM64
#  define MATHF_ARCH_ARM64 0
#endif
#ifndef MATHF_ARCH_X86
#  define MATHF_ARCH_X86 0
#endif

// ---------------------------------------------------------------- SIMD backend
// Selection is compile-time only; Mathf performs no runtime ISA dispatch, so
// that call overhead never enters the hot path (same policy as DirectXMath).
// Force a backend with -DMATHF_FORCE_SCALAR for testing the fallback.

#if defined(MATHF_FORCE_SCALAR)
#  define MATHF_SIMD_SCALAR 1
#elif MATHF_ARCH_ARM64 && (defined(__ARM_NEON) || defined(_M_ARM64) || defined(_M_ARM64EC))
#  define MATHF_SIMD_NEON 1
#elif MATHF_ARCH_X86 && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#  define MATHF_SIMD_SSE 1
#else
#  define MATHF_SIMD_SCALAR 1
#endif

#ifndef MATHF_SIMD_SSE
#  define MATHF_SIMD_SSE 0
#endif
#ifndef MATHF_SIMD_NEON
#  define MATHF_SIMD_NEON 0
#endif
#ifndef MATHF_SIMD_SCALAR
#  define MATHF_SIMD_SCALAR 0
#endif

// --- x86 ISA level -----------------------------------------------------------
// MSVC never defines __SSE4_1__, so SSE4.1 (needed for dpps -- see
// docs/SPIKE-RESULTS.md §4) must be inferred from /arch:AVX or opted into
// explicitly with MATHF_ENABLE_SSE4, mirroring DirectXMath's _XM_SSE4_INTRINSICS_.
#if MATHF_SIMD_SSE
#  if defined(__SSE4_1__) || defined(__AVX__) || defined(MATHF_ENABLE_SSE4)
#    define MATHF_HAS_SSE4 1
#  else
#    define MATHF_HAS_SSE4 0
#  endif
#  if defined(__AVX__)
#    define MATHF_HAS_AVX 1
#  else
#    define MATHF_HAS_AVX 0
#  endif
// /arch:AVX2 implies FMA on MSVC; Clang/GCC define __FMA__ separately.
#  if defined(__FMA__) || defined(__AVX2__)
#    define MATHF_HAS_FMA 1
#  else
#    define MATHF_HAS_FMA 0
#  endif
#else
#  define MATHF_HAS_SSE4 0
#  define MATHF_HAS_AVX  0
#  if MATHF_SIMD_NEON && MATHF_ARCH_ARM64
#    define MATHF_HAS_FMA 1   // AArch64 NEON always has fused multiply-add
#  else
#    define MATHF_HAS_FMA 0
#  endif
#endif

// ---------------------------------------------------------------- attributes
#define MATHF_NODISCARD [[nodiscard]]

#if MATHF_HAS_IF_CONSTEVAL
#  define MATHF_IF_CONSTEVAL if consteval
#else
#  define MATHF_IF_CONSTEVAL if (std::is_constant_evaluated())
#endif

// ---------------------------------------------------------------- diagnostics
#define MATHF_STRINGIFY_(x) #x
#define MATHF_STRINGIFY(x) MATHF_STRINGIFY_(x)

#if MATHF_SIMD_SSE
#  define MATHF_BACKEND_NAME "SSE"
#elif MATHF_SIMD_NEON
#  define MATHF_BACKEND_NAME "NEON"
#else
#  define MATHF_BACKEND_NAME "scalar"
#endif

#endif // MATHF_CONFIG_HPP
