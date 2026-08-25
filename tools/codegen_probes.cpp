// tools/codegen_probes.cpp -- shapes whose CODE STRUCTURE is gated, not timed.
//
// docs/BASELINE.md section 8 records that shifting .text by sixteen bytes moves
// a 2x penalty between benchmarks that compile to identical instruction bytes.
// A timing gate on this family therefore reports the linker's mood. What does
// survive a relink is structure, and section 9 found the structure that matters
// here: MSVC does not unroll an iterator-driven loop nested inside another loop
// -- any such loop, a std::views pipeline and a raw const float* alike. The
// library's answer is the set of shapes below, which form no inner loop at all.
//
// Each probe carries the number of backward branches its body is allowed. One
// is the outer loop. A second means a fixed-extent loop survived, and that is
// the regression this file exists to catch.
//
// This is compiled, never linked: scripts/check_codegen.ps1 asks cl for an
// assembly listing and reads it. The escape hatch below is deliberately an
// undeclared external call rather than a benchmark-library macro, so the file
// depends on nothing but the headers under test.

#include <mathematics/views.hpp>

#include <atomic>
#include <array>
#include <cstddef>
#include <functional>
#include <span>

extern "C" void mathematics_codegen_escape(void* address) noexcept;

namespace {

// Forces the object into memory and hides its value from the optimizer.
template <typename type>
inline void escape(type& value) noexcept {
    mathematics_codegen_escape(const_cast<void*>(
        static_cast<const volatile void*>(&value)));
}

// Barrier at the top of every iteration, so no iteration can be hoisted out.
inline void clobber() noexcept {
    std::atomic_signal_fence(std::memory_order_acq_rel);
}

math::vector4 sample_vector() noexcept {
    return math::vector4{1.25f, -2.5f, 3.75f, 4.5f};
}

math::matrix4x4 sample_matrix() noexcept {
    return math::matrix4x4{1, 2, 3, 4,
                           5, 6, 7, 8,
                           9, 10, 11, 12,
                           13, 14, 15, 16};
}

} // namespace

extern "C" {

// CODEGEN-GATE: probe_components_direct_sum backward_branches<=1
// The hand-written control. Every shape below has to match it.
void probe_components_direct_sum(std::size_t count) {
    math::vector4 value = sample_vector();
    escape(value);
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        clobber();
        float result = value.x + value.y + value.z + value.w;
        escape(result);
    }
}

// CODEGEN-GATE: probe_components_fold_sum backward_branches<=1
void probe_components_fold_sum(std::size_t count) {
    math::vector4 value = sample_vector();
    escape(value);
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        clobber();
        float result = math::ranges::fold_fixed(
            math::components(value), 0.0f, std::plus<>{});
        escape(result);
    }
}

// CODEGEN-GATE: probe_components_pipeline_fold_sum backward_branches<=1
void probe_components_pipeline_fold_sum(std::size_t count) {
    math::vector4 value = sample_vector();
    escape(value);
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        clobber();
        float result =
            math::components(value) |
            math::views::transform_fixed(
                [](float component) { return component * component; }) |
            math::ranges::fold_fixed(0.0f, std::plus<>{});
        escape(result);
    }
}

// CODEGEN-GATE: probe_components_structured_sum backward_branches<=1
void probe_components_structured_sum(std::size_t count) {
    math::vector4 value = sample_vector();
    escape(value);
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        clobber();
        auto&& [x, y, z, w] = math::components(value);
        float result = x + y + z + w;
        escape(result);
    }
}

// CODEGEN-GATE: probe_components_fixed_for_each backward_branches<=1
void probe_components_fixed_for_each(std::size_t count) {
    math::vector4 value = sample_vector();
    escape(value);
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        clobber();
        static_cast<void>(math::ranges::for_each_fixed(
            math::components(value), [](float& component) {
                component = component * 0.5f + 0.5f;
            }));
        escape(value);
    }
}

// CODEGEN-GATE: probe_components_fixed_transform backward_branches<=1
void probe_components_fixed_transform(std::size_t count) {
    math::vector4 value = sample_vector();
    std::array<float, 4> output{};
    escape(value);
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        clobber();
        static_cast<void>(math::ranges::transform_fixed(
            math::components(value), output.begin(),
            [](float component) { return component * 2.0f + 1.0f; }));
        escape(output);
    }
}

// CODEGEN-GATE: probe_rows_direct_sum backward_branches<=1
void probe_rows_direct_sum(std::size_t count) {
    math::matrix4x4 value = sample_matrix();
    escape(value);
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        clobber();
        float result = 0.0f;
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                result += value.m[row][column];
            }
        }
        escape(result);
    }
}

// CODEGEN-GATE: probe_rows_fold_sum backward_branches<=1
void probe_rows_fold_sum(std::size_t count) {
    math::matrix4x4 value = sample_matrix();
    escape(value);
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        clobber();
        float result = math::ranges::fold_fixed(
            math::rows(value), 0.0f,
            [](float accumulated, std::span<const float, 4> row) {
                return math::ranges::fold_fixed(row, accumulated,
                                                std::plus<>{});
            });
        escape(result);
    }
}

// CODEGEN-GATE: probe_rows_structured_sum backward_branches<=1
void probe_rows_structured_sum(std::size_t count) {
    math::matrix4x4 value = sample_matrix();
    escape(value);
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        clobber();
        auto&& [row0, row1, row2, row3] = math::rows(value);
        float result = row0[0] + row1[1] + row2[2] + row3[3];
        escape(result);
    }
}

} // extern "C"
