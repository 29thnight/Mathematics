// Regression guard for Phase 0's central claim: the shipping library -- not the
// spike prototypes -- still compiles to the same instructions as DirectXMath.
//
// Worth re-running after any change to vec_reg.hpp's structure. Refactors that
// look behaviour-neutral (hoisting the constexpr path into a helper, wrapping
// scalar semantics in a lambda) are exactly the kind that can quietly cost an
// inline and turn a 2-instruction operation into a call.

#include <mathematics/mdspan.hpp>
#include <mathematics/vec_reg.hpp>
#include <mathematics/views.hpp>

#include <DirectXMath.h>

#include <cstddef>
#include <functional>

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

// ----------------------------------------------------------- C++ view adapters
PROBE float probe_direct_components_sum(const math::vector4& value) noexcept {
    return value.x + value.y + value.z + value.w;
}

PROBE float probe_view_components_sum(const math::vector4& value) noexcept {
    float result = 0.0f;
    for (const float component : math::components(value)) result += component;
    return result;
}

PROBE float probe_fixed_fold_components_sum(const math::vector4& value) noexcept {
    return math::ranges::fold_fixed(
        math::components(value), 0.0f, std::plus<>{});
}

PROBE float probe_fixed_for_each_components_sum(
    const math::vector4& value) noexcept {
    float result = 0.0f;
    math::ranges::for_each_fixed(
        math::components(value),
        [&result](float component) { result += component; });
    return result;
}

PROBE float probe_pipeline_components_sum(const math::vector4& value) noexcept {
    return math::components(value) |
        math::views::transform_fixed([](float component) { return component; }) |
        math::ranges::fold_fixed(0.0f, std::plus<>{});
}

PROBE float probe_direct_transformed_components_sum(
    const math::vector4& value) noexcept {
    return value.x * value.x + value.y * value.y +
           value.z * value.z + value.w * value.w;
}

PROBE float probe_pipeline_transformed_components_sum(
    const math::vector4& value) noexcept {
    return math::components(value) |
        math::views::transform_fixed(
            [](float component) { return component * component; }) |
        math::ranges::fold_fixed(0.0f, std::plus<>{});
}

PROBE float probe_pipeline_transformed_for_each_sum(
    const math::vector4& value) noexcept {
    float result = 0.0f;
    static_cast<void>(
        math::components(value) |
        math::views::transform_fixed(
            [](float component) { return component * component; }) |
        math::ranges::for_each_fixed(
            [&result](float component) { result += component; }));
    return result;
}

PROBE float probe_lazy_transform_components_sum(
    const math::vector4& value) noexcept {
    float result = 0.0f;
    for (const float component :
         math::components(value) |
             math::views::transform_fixed(
                 [](float element) { return element; })) {
        result += component;
    }
    return result;
}

PROBE float probe_direct_rows_sum(const math::matrix4x4& matrix) noexcept {
    float result = 0.0f;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            result += matrix.m[row][column];
        }
    }
    return result;
}

PROBE float probe_view_rows_sum(const math::matrix4x4& matrix) noexcept {
    float result = 0.0f;
    for (const auto row : math::rows(matrix)) {
        for (const float element : row) result += element;
    }
    return result;
}

PROBE float probe_fixed_fold_rows_sum(const math::matrix4x4& matrix) noexcept {
    return math::ranges::fold_fixed(
        math::rows(matrix), 0.0f,
        [](float accumulated, std::span<const float, 4> row) {
            return math::ranges::fold_fixed(row, accumulated, std::plus<>{});
        });
}

PROBE float probe_fixed_for_each_rows_sum(const math::matrix4x4& matrix) noexcept {
    float result = 0.0f;
    math::ranges::for_each_fixed(
        math::rows(matrix), [&result](std::span<const float, 4> row) {
            math::ranges::for_each_fixed(
                row, [&result](float element) { result += element; });
        });
    return result;
}

PROBE void probe_direct_components_transform(const math::vector4& value,
                                             float* output) noexcept {
    output[0] = value.x * 2.0f + 1.0f;
    output[1] = value.y * 2.0f + 1.0f;
    output[2] = value.z * 2.0f + 1.0f;
    output[3] = value.w * 2.0f + 1.0f;
}

PROBE void probe_view_components_transform(const math::vector4& value,
                                           float* output) noexcept {
    for (const float component : math::components(value)) {
        *output++ = component * 2.0f + 1.0f;
    }
}

PROBE void probe_fixed_components_transform(const math::vector4& value,
                                            float* output) noexcept {
    static_cast<void>(math::ranges::transform_fixed(
        math::components(value), output,
        [](float component) { return component * 2.0f + 1.0f; }));
}

PROBE void probe_pipeline_components_transform(const math::vector4& value,
                                               float* output) noexcept {
    static_cast<void>(
        math::components(value) |
        math::ranges::transform_fixed_to(
            output,
            [](float component) { return component * 2.0f + 1.0f; }));
}

PROBE void probe_direct_components_for_each(math::vector4& value) noexcept {
    value.x = value.x * 2.0f + 1.0f;
    value.y = value.y * 2.0f + 1.0f;
    value.z = value.z * 2.0f + 1.0f;
    value.w = value.w * 2.0f + 1.0f;
}

PROBE void probe_view_components_for_each(math::vector4& value) noexcept {
    for (float& component : math::components(value)) {
        component = component * 2.0f + 1.0f;
    }
}

PROBE void probe_fixed_components_for_each(math::vector4& value) noexcept {
    static_cast<void>(math::ranges::for_each_fixed(
        math::components(value),
        [](float& component) { component = component * 2.0f + 1.0f; }));
}

PROBE void probe_pipeline_components_for_each(math::vector4& value) noexcept {
    static_cast<void>(
        math::components(value) |
        math::ranges::for_each_fixed([](float& component) {
            component = component * 2.0f + 1.0f;
        }));
}

#if MATHEMATICS_HAS_MDSPAN
PROBE float probe_direct_matrix_element(const math::matrix4x4& matrix,
                                        std::size_t row,
                                        std::size_t column) noexcept {
    return matrix.m[row][column];
}

PROBE float probe_mdspan_matrix_element(const math::matrix4x4& matrix,
                                        std::size_t row,
                                        std::size_t column) noexcept {
    return math::as_mdspan(matrix)[row, column];
}

PROBE float probe_direct_transpose_element(const math::matrix4x4& matrix,
                                           std::size_t row,
                                           std::size_t column) noexcept {
    return matrix.m[column][row];
}

PROBE float probe_mdspan_transpose_element(const math::matrix4x4& matrix,
                                           std::size_t row,
                                           std::size_t column) noexcept {
    return math::transpose_view(matrix)[row, column];
}
#endif

int main() { return 0; }
