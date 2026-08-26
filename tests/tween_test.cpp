#include <mathematics/tween.hpp>

#include <gtest/gtest.h>

#include <type_traits>
#include <vector>

namespace {

constexpr bool stateless_tween_constexpr_contract() {
    const math::vector3 midpoint = math::tween_value(
        math::vector3{0.0f, 2.0f, 4.0f},
        math::vector3{2.0f, 4.0f, 6.0f}, 0.5f,
        math::easing::smoothstep);
    const math::rect layout = math::tween_value(
        math::rect{0.0f, 10.0f, -4.0f, 2.0f},
        math::rect{10.0f, 20.0f, -8.0f, 6.0f}, 0.5f);
    return midpoint == math::vector3{1.0f, 3.0f, 5.0f} &&
           layout == math::rect{5.0f, 15.0f, -6.0f, 4.0f} &&
           math::tween_value(2.0f, 6.0f, 0.25f) == 3.0f &&
           math::tween_value_clamped(2.0f, 6.0f, 2.0f) == 6.0f;
}

static_assert(stateless_tween_constexpr_contract());
static_assert(math::linearly_interpolable<float>);
static_assert(math::linearly_interpolable<math::vector2>);
static_assert(math::linearly_interpolable<math::vector3>);
static_assert(math::linearly_interpolable<math::vector4>);
static_assert(math::linearly_interpolable<math::color>);
static_assert(math::linearly_interpolable<math::rect>);
static_assert(std::is_same_v<
              decltype(math::make_tween(0.0f, 1.0f, 1.0f,
                                        math::easing::linear)),
              decltype(math::make_tween(0.0f, 1.0f, 1.0f,
                                        math::easing::bounce_out))>);
static_assert(std::is_trivially_copyable_v<math::tween<float>>);
static_assert(std::is_same_v<math::tween<math::vector3>::value_type,
                             math::vector3>);
static_assert(std::is_same_v<
              decltype(math::make_tween(0.0f, 1.0f, 1.0f)
                           .ease(math::easing::smoothstep)
                           .delay(0.1f)
                           .playback(math::tween_playback::loop)
                           .cycles(2u)),
              math::tween<float>&&>);

} // namespace

TEST(tween_value, supports_game_value_types_and_explicit_quaternion_policy) {
    EXPECT_EQ(math::tween_value(
                  math::color::black(), math::color::white(), 0.25f),
              math::color(0.25f, 0.25f, 0.25f, 1.0f));

    const math::quaternion from = math::quaternion::identity();
    const math::quaternion to = math::quaternion_from_axis_angle(
        math::vector3{0.0f, 1.0f, 0.0f}, math::half_pi);
    const math::quaternion halfway = math::tween_value(
        from, to, 0.5f, math::easing::linear,
        math::interpolation::spherical);
    EXPECT_TRUE(math::same_rotation(
        halfway,
        math::quaternion_from_axis_angle(
            math::vector3{0.0f, 1.0f, 0.0f}, math::half_pi * 0.5f),
        1e-5f));
}

TEST(tween_state, once_advances_delays_pauses_and_restarts) {
    auto track = math::make_tween(0.0f, 10.0f, 2.0f,
                                  math::easing::smoothstep);
    track.delay(1.0f);

    EXPECT_FLOAT_EQ(track.sample(), 0.0f);
    EXPECT_FLOAT_EQ(track.advance(0.5f).value, 0.0f);
    EXPECT_FLOAT_EQ(track.advance(0.5f).value, 0.0f);
    EXPECT_FLOAT_EQ(track.advance(1.0f).value, 5.0f);

    track.pause();
    const auto paused = track.advance(1.0f);
    EXPECT_EQ(paused.state, math::tween_state::paused);
    EXPECT_FLOAT_EQ(paused.value, 5.0f);

    track.resume();
    const auto completed = track.advance(1.0f);
    EXPECT_TRUE(completed.completed());
    EXPECT_FLOAT_EQ(completed.value, 10.0f);
    EXPECT_EQ(completed.completed_cycles, 1u);

    track.restart();
    EXPECT_EQ(track.state(), math::tween_state::playing);
    EXPECT_FLOAT_EQ(track.elapsed_seconds(), 0.0f);
    EXPECT_FLOAT_EQ(track.sample(), 0.0f);
}

TEST(tween_state, loop_counts_crossed_cycles_and_finishes_at_destination) {
    auto track = math::make_tween(0.0f, 8.0f, 1.0f);
    track.playback(math::tween_playback::loop).cycles(3u);

    const auto large_step = track.advance(2.5f);
    EXPECT_EQ(large_step.completed_cycles, 2u);
    EXPECT_FLOAT_EQ(large_step.value, 4.0f);
    EXPECT_FALSE(large_step.completed());

    const auto end = track.advance(0.5f);
    EXPECT_EQ(end.completed_cycles, 1u);
    EXPECT_TRUE(end.completed());
    EXPECT_FLOAT_EQ(end.value, 8.0f);
}

TEST(tween_state, ping_pong_cycle_is_a_forward_and_return_pair) {
    auto track = math::make_tween(0.0f, 10.0f, 1.0f);
    track.playback(math::tween_playback::ping_pong).cycles(2u);

    EXPECT_FLOAT_EQ(track.advance(0.5f).value, 5.0f);
    EXPECT_FLOAT_EQ(track.advance(0.5f).value, 10.0f);
    EXPECT_FLOAT_EQ(track.advance(0.5f).value, 5.0f);
    const auto first_cycle = track.advance(0.5f);
    EXPECT_FLOAT_EQ(first_cycle.value, 0.0f);
    EXPECT_EQ(first_cycle.completed_cycles, 1u);
    EXPECT_FALSE(first_cycle.completed());

    const auto final_cycle = track.advance(2.0f);
    EXPECT_FLOAT_EQ(final_cycle.value, 0.0f);
    EXPECT_EQ(final_cycle.completed_cycles, 1u);
    EXPECT_TRUE(final_cycle.completed());
}

TEST(tween_state, infinite_playback_never_completes) {
    auto track = math::make_tween(0.0f, 1.0f, 0.25f);
    track.playback(math::tween_playback::loop)
         .cycles(math::infinite_cycles);

    const auto result = track.advance(100.125f);
    EXPECT_EQ(result.state, math::tween_state::playing);
    EXPECT_FALSE(track.finished());
    EXPECT_FLOAT_EQ(result.value, 0.5f);
    EXPECT_EQ(result.completed_cycles, 400u);
}

TEST(tween_state, seek_preserves_pause_and_clamps_finite_timeline) {
    auto track = math::make_tween(2.0f, 6.0f, 2.0f);
    track.pause();
    track.seek(1.0f);
    EXPECT_EQ(track.state(), math::tween_state::paused);
    EXPECT_FLOAT_EQ(track.sample(), 4.0f);

    track.seek(20.0f);
    EXPECT_TRUE(track.finished());
    EXPECT_FLOAT_EQ(track.sample(), 6.0f);
}

TEST(tween_state, zero_duration_completes_immediately) {
    auto track = math::make_tween(2.0f, 6.0f, 0.0f);
    EXPECT_TRUE(track.finished());
    EXPECT_FLOAT_EQ(track.sample(), 6.0f);
    const auto result = track.advance(1.0f);
    EXPECT_TRUE(result.completed());
    EXPECT_FLOAT_EQ(result.value, 6.0f);
    EXPECT_EQ(result.completed_cycles, 0u);
}

TEST(tween_state, once_ignores_cycle_configuration_and_ends_once) {
    auto track = math::make_tween(0.0f, 1.0f, 1.0f);
    track.cycles(math::infinite_cycles);
    const auto result = track.advance(10.0f);
    EXPECT_TRUE(result.completed());
    EXPECT_FLOAT_EQ(track.elapsed_seconds(), 1.0f);
    EXPECT_EQ(result.completed_cycles, 1u);
}

TEST(tween_ownership, copies_and_vector_reallocation_are_independent) {
    auto original = math::make_tween(0.0f, 10.0f, 1.0f);
    auto copy = original;
    (void)original.advance(0.25f);
    (void)copy.advance(0.75f);
    EXPECT_FLOAT_EQ(original.sample(), 2.5f);
    EXPECT_FLOAT_EQ(copy.sample(), 7.5f);

    std::vector<math::tween<float>> manager;
    manager.reserve(1);
    manager.push_back(original);
    manager.push_back(copy); // forces ordinary value relocation
    EXPECT_FLOAT_EQ(manager[0].sample(), 2.5f);
    EXPECT_FLOAT_EQ(manager[1].sample(), 7.5f);
}
