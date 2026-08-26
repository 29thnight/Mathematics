// Easing/tween microbenchmarks. DirectXMath has interpolation primitives but
// no easing catalogue, lazy tween view or playback object, so the closest fair
// baselines are the same scalar easing expression plus XMVectorLerp and a
// hand-written elapsed-time accumulator.

#include <mathematics/tween_views.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(MATHEMATICS_BENCH_HAS_DXMATH)
#  include <DirectXMath.h>
#endif

namespace {

constexpr std::size_t batch_size = 512;

std::array<float, batch_size> make_progress() {
    std::array<float, batch_size> result{};
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<float>(index) /
                        static_cast<float>(result.size() - 1u);
    return result;
}

const std::array<float, batch_size>& progress_data() {
    static const std::array<float, batch_size> values = make_progress();
    return values;
}

enum class runtime_easing_kind : std::uint8_t {
    smoothstep,
    cubic_in_out
};

float switch_ease(runtime_easing_kind kind, float progress) noexcept {
    switch (kind) {
    case runtime_easing_kind::smoothstep:
        return math::easing::smoothstep(progress);
    case runtime_easing_kind::cubic_in_out:
        return math::easing::cubic_in_out(progress);
    }
    return progress;
}

const std::array<runtime_easing_kind, batch_size>& easing_kinds() {
    static const auto values = [] {
        std::array<runtime_easing_kind, batch_size> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] = (index & 1u) == 0u
                ? runtime_easing_kind::smoothstep
                : runtime_easing_kind::cubic_in_out;
        return result;
    }();
    return values;
}

const std::array<math::easing_function, batch_size>& erased_easings() {
    static const auto values = [] {
        std::array<math::easing_function, batch_size> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] = (index & 1u) == 0u
                ? math::easing_function{math::easing::smoothstep}
                : math::easing_function{math::easing::cubic_in_out};
        return result;
    }();
    return values;
}

float manual_smoothstep(float t) noexcept {
    return t * t * (3.0f - 2.0f * t);
}

void BM_Ease_ManualSmoothstep(benchmark::State& state) {
    const auto& input = progress_data();
    std::array<float, batch_size> output{};
    for (auto _ : state) {
        for (std::size_t index = 0; index < input.size(); ++index)
            output[index] = manual_smoothstep(input[index]);
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_Ease_MathematicsStatic(benchmark::State& state) {
    const auto& input = progress_data();
    std::array<float, batch_size> output{};
    for (auto _ : state) {
        for (std::size_t index = 0; index < input.size(); ++index)
            output[index] = math::easing::smoothstep(input[index]);
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_Ease_MathematicsErased(benchmark::State& state) {
    const auto& input = progress_data();
    const math::easing_function easing{math::easing::smoothstep};
    std::array<float, batch_size> output{};
    for (auto _ : state) {
        for (std::size_t index = 0; index < input.size(); ++index)
            output[index] = easing(input[index]);
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_Ease_MixedEnumSwitch(benchmark::State& state) {
    const auto& input = progress_data();
    const auto& kinds = easing_kinds();
    std::array<float, batch_size> output{};
    for (auto _ : state) {
        for (std::size_t index = 0; index < input.size(); ++index)
            output[index] = switch_ease(kinds[index], input[index]);
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_Ease_MixedFunctionPointer(benchmark::State& state) {
    const auto& input = progress_data();
    const auto& policies = erased_easings();
    std::array<float, batch_size> output{};
    for (auto _ : state) {
        for (std::size_t index = 0; index < input.size(); ++index)
            output[index] = policies[index](input[index]);
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_TweenVector3_Manual(benchmark::State& state) {
    const auto& input = progress_data();
    const math::vector3 from{-8.0f, 2.0f, 12.0f};
    const math::vector3 to{16.0f, -4.0f, 3.0f};
    std::array<math::vector3, batch_size> output{};
    for (auto _ : state) {
        for (std::size_t index = 0; index < input.size(); ++index)
            output[index] = math::lerp(
                from, to, manual_smoothstep(input[index]));
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_TweenVector3_MathematicsValue(benchmark::State& state) {
    const auto& input = progress_data();
    const math::vector3 from{-8.0f, 2.0f, 12.0f};
    const math::vector3 to{16.0f, -4.0f, 3.0f};
    std::array<math::vector3, batch_size> output{};
    for (auto _ : state) {
        for (std::size_t index = 0; index < input.size(); ++index)
            output[index] = math::tween_value(
                from, to, input[index], math::easing::smoothstep);
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_TweenVector3_MathematicsFixedView(benchmark::State& state) {
    const auto& input = progress_data();
    std::array<math::vector3, batch_size> output{};
    for (auto _ : state) {
        auto values = input | math::views::tween(
            math::vector3{-8.0f, 2.0f, 12.0f},
            math::vector3{16.0f, -4.0f, 3.0f},
            math::easing::smoothstep);
        for (std::size_t index = 0; index < input.size(); ++index)
            output[index] = values[index];
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

#if defined(MATHEMATICS_BENCH_HAS_DXMATH)
void BM_TweenVector3_DirectXMath(benchmark::State& state) {
    const auto& input = progress_data();
    const DirectX::XMVECTOR from = DirectX::XMVectorSet(-8.0f, 2.0f, 12.0f, 0.0f);
    const DirectX::XMVECTOR to = DirectX::XMVectorSet(16.0f, -4.0f, 3.0f, 0.0f);
    std::array<DirectX::XMFLOAT3, batch_size> output{};
    for (auto _ : state) {
        for (std::size_t index = 0; index < input.size(); ++index) {
            const DirectX::XMVECTOR value = DirectX::XMVectorLerp(
                from, to, manual_smoothstep(input[index]));
            DirectX::XMStoreFloat3(&output[index], value);
        }
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}
#endif

void BM_Playback_ManualInfiniteLoop(benchmark::State& state) {
    constexpr float delta = 1.0f / 60.0f;
    constexpr float duration = 0.75f;
    float elapsed = 0.0f;
    math::vector3 value{};
    const math::vector3 from{-8.0f, 2.0f, 12.0f};
    const math::vector3 to{16.0f, -4.0f, 3.0f};
    for (auto _ : state) {
        elapsed += delta;
        if (elapsed >= duration) elapsed -= duration;
        value = math::lerp(
            from, to, manual_smoothstep(elapsed / duration));
        benchmark::DoNotOptimize(value);
    }
}

void BM_Playback_MathematicsTween(benchmark::State& state) {
    auto track = math::make_tween(
        math::vector3{-8.0f, 2.0f, 12.0f},
        math::vector3{16.0f, -4.0f, 3.0f}, 0.75f,
        math::easing::smoothstep);
    track.playback(math::tween_playback::loop)
         .cycles(math::infinite_cycles);
    for (auto _ : state) {
        auto step = track.advance(1.0f / 60.0f);
        benchmark::DoNotOptimize(step.value);
        // Keep float timeline resolution representative of an active manager,
        // not a synthetic multi-day run inside one benchmark invocation.
        if (track.elapsed_seconds() >= 600.0f) track.restart();
    }
}

BENCHMARK(BM_Ease_ManualSmoothstep);
BENCHMARK(BM_Ease_MathematicsStatic);
BENCHMARK(BM_Ease_MathematicsErased);
BENCHMARK(BM_Ease_MixedEnumSwitch);
BENCHMARK(BM_Ease_MixedFunctionPointer);
BENCHMARK(BM_TweenVector3_Manual);
BENCHMARK(BM_TweenVector3_MathematicsValue);
BENCHMARK(BM_TweenVector3_MathematicsFixedView);
#if defined(MATHEMATICS_BENCH_HAS_DXMATH)
BENCHMARK(BM_TweenVector3_DirectXMath);
#endif
BENCHMARK(BM_Playback_ManualInfiniteLoop);
BENCHMARK(BM_Playback_MathematicsTween);

} // namespace

BENCHMARK_MAIN();
