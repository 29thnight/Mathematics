// mathf/arch/simd_select.hpp — includes exactly one backend.
//
// Selection is entirely compile-time. Mathf performs no runtime ISA dispatch, so
// that an indirect call never enters the hot path; this is the same policy
// DirectXMath follows. config.hpp decides which backend applies from the target
// and the compiler's own ISA macros.
#ifndef MATHF_ARCH_SIMD_SELECT_HPP
#define MATHF_ARCH_SIMD_SELECT_HPP

#include <mathf/config.hpp>

#if MATHF_SIMD_SSE
#  include <mathf/arch/simd_sse.hpp>
#elif MATHF_SIMD_NEON
#  include <mathf/arch/simd_neon.hpp>
#elif MATHF_SIMD_SCALAR
#  include <mathf/arch/simd_scalar.hpp>
#else
#  error "no SIMD backend selected; config.hpp should always pick one"
#endif

#endif // MATHF_ARCH_SIMD_SELECT_HPP
