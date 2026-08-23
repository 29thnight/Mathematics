// Regression guard for Phase 0's central claim: the shipping library -- not the
// spike prototypes -- still compiles to the same instructions as DirectXMath.
//
// Worth re-running after any change to vec_reg.hpp's structure. Refactors that
// look behaviour-neutral (hoisting the constexpr path into a helper, wrapping
// scalar semantics in a lambda) are exactly the kind that can quietly cost an
// inline and turn a 2-instruction operation into a call.

#include <mathf/vec_reg.hpp>

#include <DirectXMath.h>

#define PROBE MATHF_NOINLINE

// --------------------------------------------------------------------- MulAdd
PROBE mathf::VecReg MATHF_CALL probe_a_muladd(mathf::VecReg a, mathf::VecReg b,
                                              mathf::VecReg c) noexcept {
    return mathf::MulAdd(a, b, c);
}

PROBE DirectX::XMVECTOR XM_CALLCONV probe_dx_muladd(DirectX::XMVECTOR a,
                                                    DirectX::XMVECTOR b,
                                                    DirectX::XMVECTOR c) noexcept {
    return DirectX::XMVectorMultiplyAdd(a, b, c);
}

// ----------------------------------------------------------------------- Dot4
PROBE mathf::VecReg MATHF_CALL probe_a_dot4(mathf::VecReg a,
                                            mathf::VecReg b) noexcept {
    return mathf::Dot4(a, b);
}

PROBE DirectX::XMVECTOR XM_CALLCONV probe_dx_dot4(DirectX::XMVECTOR a,
                                                  DirectX::XMVECTOR b) noexcept {
    return DirectX::XMVector4Dot(a, b);
}

// ------------------------------------------------------- chained expression
// Verifies intermediates stay in registers across composed calls.
PROBE mathf::VecReg MATHF_CALL probe_a_chain(mathf::VecReg a, mathf::VecReg b,
                                             mathf::VecReg c,
                                             mathf::VecReg d) noexcept {
    return mathf::Add(mathf::MulAdd(a, b, c), mathf::Mul(d, d));
}

PROBE DirectX::XMVECTOR XM_CALLCONV probe_dx_chain(DirectX::XMVECTOR a,
                                                   DirectX::XMVECTOR b,
                                                   DirectX::XMVECTOR c,
                                                   DirectX::XMVECTOR d) noexcept {
    return DirectX::XMVectorAdd(DirectX::XMVectorMultiplyAdd(a, b, c),
                                DirectX::XMVectorMultiply(d, d));
}

// -------------------------------------------------------- basic arithmetic
PROBE mathf::VecReg MATHF_CALL probe_a_add(mathf::VecReg a,
                                           mathf::VecReg b) noexcept {
    return mathf::Add(a, b);
}

PROBE DirectX::XMVECTOR XM_CALLCONV probe_dx_add(DirectX::XMVECTOR a,
                                                 DirectX::XMVECTOR b) noexcept {
    return DirectX::XMVectorAdd(a, b);
}

int main() { return 0; }
