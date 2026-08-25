#include <benchmark/benchmark.h>

#include <mathematics/geometry.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(MATHEMATICS_BENCH_HAS_DXMATH) && __has_include(<DirectXCollision.h>)
#  include <DirectXCollision.h>
#  define MATHEMATICS_BENCH_HAS_DX_BOUNDING_BOX 1
#else
#  define MATHEMATICS_BENCH_HAS_DX_BOUNDING_BOX 0
#endif

namespace {

constexpr std::size_t batch_size = 1024;

float sample(std::uint32_t& state, float minimum, float maximum) noexcept {
    state = state * 1664525u + 1013904223u;
    const float unit = static_cast<float>(state >> 8) * (1.0f / 16777215.0f);
    return minimum + (maximum - minimum) * unit;
}

#if MATHEMATICS_BENCH_HAS_DX_BOUNDING_BOX
DirectX::XMMATRIX to_xm(const math::matrix4x4& value) noexcept {
    return DirectX::XMMATRIX(
        value.m[0][0], value.m[0][1], value.m[0][2], value.m[0][3],
        value.m[1][0], value.m[1][1], value.m[1][2], value.m[1][3],
        value.m[2][0], value.m[2][1], value.m[2][2], value.m[2][3],
        value.m[3][0], value.m[3][1], value.m[3][2], value.m[3][3]);
}
#endif

struct benchmark_data {
    alignas(4096) std::array<math::aabb, batch_size> boxes{};
    alignas(4096) std::array<math::matrix4x4, batch_size> matrices{};
    alignas(4096) std::array<math::aabb, batch_size> outputs{};
#if MATHEMATICS_BENCH_HAS_DX_BOUNDING_BOX
    alignas(4096) std::array<DirectX::BoundingBox, batch_size> dx_boxes{};
    alignas(4096) std::array<DirectX::XMMATRIX, batch_size> dx_matrices{};
    alignas(4096) std::array<DirectX::BoundingBox, batch_size> dx_outputs{};
#endif
};

benchmark_data make_data() {
    benchmark_data result{};
    std::uint32_t random_state = 0xaabb2026u;
    for (std::size_t i = 0; i < batch_size; ++i) {
        result.boxes[i] = math::aabb{
            math::vector3{sample(random_state, -100.0f, 100.0f),
                          sample(random_state, -100.0f, 100.0f),
                          sample(random_state, -100.0f, 100.0f)},
            math::vector3{sample(random_state, 0.01f, 10.0f),
                          sample(random_state, 0.01f, 10.0f),
                          sample(random_state, 0.01f, 10.0f)}};
        const math::quaternion rotation = math::quaternion_from_axis_angle(
            math::normalize(math::vector3{
                sample(random_state, -1.0f, 1.0f),
                sample(random_state, -1.0f, 1.0f),
                sample(random_state, -1.0f, 1.0f)}),
            sample(random_state, -math::pi, math::pi));
        result.matrices[i] = math::compose(
            math::vector3{sample(random_state, -3.0f, 3.0f),
                          sample(random_state, -3.0f, 3.0f),
                          sample(random_state, -3.0f, 3.0f)},
            rotation,
            math::vector3{sample(random_state, -50.0f, 50.0f),
                          sample(random_state, -50.0f, 50.0f),
                          sample(random_state, -50.0f, 50.0f)});
        result.matrices[i].m[0][1] += sample(random_state, -0.35f, 0.35f);
        result.matrices[i].m[2][0] += sample(random_state, -0.35f, 0.35f);

#if MATHEMATICS_BENCH_HAS_DX_BOUNDING_BOX
        const math::aabb& box = result.boxes[i];
        result.dx_boxes[i] = DirectX::BoundingBox{
            DirectX::XMFLOAT3{box.center.x, box.center.y, box.center.z},
            DirectX::XMFLOAT3{box.extents.x, box.extents.y, box.extents.z}};
        result.dx_matrices[i] = to_xm(result.matrices[i]);
#endif
    }
    return result;
}

benchmark_data& data() {
    static benchmark_data value = make_data();
    return value;
}

void mathematics_affine_single(benchmark::State& state) {
    benchmark_data& values = data();
    std::size_t index = 0;
    for (auto _ : state) {
        math::aabb output =
            math::transform(values.boxes[index], values.matrices[index]);
        benchmark::DoNotOptimize(output);
        index = (index + 1) & (batch_size - 1);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(mathematics_affine_single)
    ->Name("aabb_transform/single/mathematics_affine");

void mathematics_affine_batch(benchmark::State& state) {
    benchmark_data& values = data();
    for (auto _ : state) {
        for (std::size_t i = 0; i < batch_size; ++i) {
            values.outputs[i] = math::transform(values.boxes[i], values.matrices[i]);
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

BENCHMARK(mathematics_affine_batch)
    ->Name("aabb_transform/batch/mathematics_affine");

#if MATHEMATICS_BENCH_HAS_DX_BOUNDING_BOX
void directx_bounding_box_single(benchmark::State& state) {
    benchmark_data& values = data();
    std::size_t index = 0;
    for (auto _ : state) {
        DirectX::BoundingBox output;
        values.dx_boxes[index].Transform(output, values.dx_matrices[index]);
        benchmark::DoNotOptimize(output);
        index = (index + 1) & (batch_size - 1);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(directx_bounding_box_single)
    ->Name("aabb_transform/single/directx_bounding_box");

void directx_bounding_box_batch(benchmark::State& state) {
    benchmark_data& values = data();
    for (auto _ : state) {
        for (std::size_t i = 0; i < batch_size; ++i) {
            values.dx_boxes[i].Transform(values.dx_outputs[i],
                                         values.dx_matrices[i]);
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

BENCHMARK(directx_bounding_box_batch)
    ->Name("aabb_transform/batch/directx_bounding_box");
#endif

} // namespace

BENCHMARK_MAIN();
