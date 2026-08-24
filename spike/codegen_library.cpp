// Regression guard for Phase 0's central claim: the shipping library -- not the
// spike prototypes -- still compiles to the same instructions as DirectXMath.
//
// Worth re-running after any change to vec_reg.hpp's structure. Refactors that
// look behaviour-neutral (hoisting the constexpr path into a helper, wrapping
// scalar semantics in a lambda) are exactly the kind that can quietly cost an
// inline and turn a 2-instruction operation into a call.

#include <mathematics/vec_reg.hpp>

#include <DirectXMath.h>

#define PROBE MATHEMATICS_NOINLINE

// -------------------------------------------------------------------- mul_add
PROBE math::vec_reg MATHEMATICS_CALL probe_a_muladd(math::vec_reg a, math::vec_reg b,
                                                   math::vec_reg c) noexcept {
    return math::mul_add(a, b, c);
}

PROBE DirectX::XMVECTOR XM_CALLCONV probe_dx_muladd(DirectX::XMVECTOR a,
                                                    DirectX::XMVECTOR b,
                                                    DirectX::XMVECTOR c) noexcept {
    return DirectX::XMVectorMultiplyAdd(a, b, c);
}

// ----------------------------------------------------------------------- dot4
PROBE math::vec_reg MATHEMATICS_CALL probe_a_dot4(math::vec_reg a,
                                                 math::vec_reg b) noexcept {
    return math::dot4(a, b);
}

PROBE DirectX::XMVECTOR XM_CALLCONV probe_dx_dot4(DirectX::XMVECTOR a,
                                                  DirectX::XMVECTOR b) noexcept {
    return DirectX::XMVector4Dot(a, b);
}

// ------------------------------------------------------- chained expression
// Verifies intermediates stay in registers across composed calls.
PROBE math::vec_reg MATHEMATICS_CALL probe_a_chain(math::vec_reg a, math::vec_reg b,
                                                  math::vec_reg c,
                                                  math::vec_reg d) noexcept {
    return math::add(math::mul_add(a, b, c), math::mul(d, d));
}

PROBE DirectX::XMVECTOR XM_CALLCONV probe_dx_chain(DirectX::XMVECTOR a,
                                                   DirectX::XMVECTOR b,
                                                   DirectX::XMVECTOR c,
                                                   DirectX::XMVECTOR d) noexcept {
    return DirectX::XMVectorAdd(DirectX::XMVectorMultiplyAdd(a, b, c),
                                DirectX::XMVectorMultiply(d, d));
}

// -------------------------------------------------------- basic arithmetic
PROBE math::vec_reg MATHEMATICS_CALL probe_a_add(math::vec_reg a,
                                                math::vec_reg b) noexcept {
    return math::add(a, b);
}

PROBE DirectX::XMVECTOR XM_CALLCONV probe_dx_add(DirectX::XMVECTOR a,
                                                 DirectX::XMVECTOR b) noexcept {
    return DirectX::XMVectorAdd(a, b);
}

int main() { return 0; }
