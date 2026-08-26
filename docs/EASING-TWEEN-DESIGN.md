# Easing과 Tween 설계

> 상태: 1차 공개 범위 구현 및 Windows preset 검증 완료
>
> 작성일: 2026-08-26
>
> 대상 표준: C++20 기본, C++23 조건부 활용

이 문서는 Mathematics의 easing과 tween 공개 계약, 소유권,
range view, 타입별 보간, 재생 상태와 검증 기준을 정의한다. 목표는 곡선 몇 개를
편의 함수로 추가하는 데 있지 않다. 기존의 packed 값 타입, `constexpr` API,
DirectXMath 규약과 성능 검증을 유지하면서 게임 코드가 별도 보간 유틸리티 없이
안전하게 애니메이션 값을 만들 수 있게 하는 것이 목표다.

구현은 `<mathematics/easing.hpp>`, `<mathematics/tween.hpp>`,
`<mathematics/tween_views.hpp>`에 분리되며 우산 헤더에 포함된다. 정확성·concept·수명·재생
테스트와 전용 `mathematics_easing_tween_bench`가 이 문서의 계약을 검증한다.

---

## 1. 결론

기능을 네 계층으로 나눈다.

```text
normalized progress
        │
        ▼
easing function        u -> eased u
        │
        ▼
interpolator           (from, to, eased u) -> value
        │
        ├─ stateless tween / range view
        │
        └─ stateful tween<T> ── developer-owned manager
```

1. **Easing**은 진행률 하나를 다른 진행률로 바꾸는 순수 함수다.
2. **Interpolator**는 두 값과 진행률로 결과 값을 만든다.
3. **Stateless tween**과 **tween view**는 위 두 단계를 조합할 뿐 시간을 소유하지 않는다.
4. **`tween<T>`**는 시작값, 끝값, 재생 시간과 진행 상태를 소유하지만 대상 객체는
   소유하거나 참조하지 않는다.
5. Tween manager, 대상 ID, 콜백 dispatch와 실제 값 적용은 소비자 엔진이 소유한다.

`tween<T>`가 manager를 모르고 manager가 대상의 실제 주소를 tween에 넣지 않는 것이
수명 안전성의 핵심이다.

---

## 2. 목표와 비목표

### 목표

- 내장 easing 전체가 `constexpr`, `noexcept`, `[[nodiscard]]`로 동작한다.
- scalar, `vector2/3/4`, `color`, `rect`, `quaternion`을 같은 조합 모델로 다룬다.
- quaternion은 성분별 선형 보간을 숨기지 않고 `nlerp`/`slerp`를 명시한다.
- 일반 range-for와 pipe 조합을 지원한다.
- 고정 길이 range는 기존 `static_extent_v`와 `transform_fixed` 경로를 보존한다.
- 상태형 tween은 값 타입이며 user manager의 `std::vector`, slot map, ECS component에
  직접 저장할 수 있다.
- tween과 view 내부에서는 동적 할당을 하지 않는다.
- 기존 vector/matrix/quaternion/color/rect의 크기, 정렬, 레이아웃을 변경하지 않는다.
- 직접 호출, fixed view, 일반 lazy view, 상태형 갱신의 비용을 따로 측정한다.

### 비목표

- 전역 또는 singleton tween manager
- 대상 객체 포인터, 멤버 포인터, reflection property의 소유
- `std::function` 기반 update/completion callback
- keyframe timeline, sequence, parallel/join graph
- animation clip, skeletal animation, curve editor와 직렬화 포맷
- matrix 성분별 보간
- AABB, sphere, `bounding_frustum`을 일반 tween 대상으로 취급하는 것
- delta time 누적을 range iterator 안에 숨기는 것

전용 tween 라이브러리가 제공하는 이벤트 그래프와 타임라인 기능까지 흡수하지 않는다.
Mathematics는 값 계산과 재생 상태를 제공하고 스케줄링은 엔진에 남긴다.

---

## 3. 용어와 수학적 계약

문서와 API에서 아래 용어를 구분한다.

| 용어 | 기호 | 의미 |
|------|------|------|
| progress | `u` | 시간에서 계산한 원래 진행률 |
| eased progress | `t` | easing을 적용한 진행률 |
| easing | `E(u)` | 진행률을 변환하는 순수 함수 |
| interpolator | `I(a,b,t)` | 두 값 사이의 결과를 계산하는 함수 |
| sample | `I(a,b,E(u))` | 특정 진행률에서 얻은 tween 값 |
| duration | `d` | 한 방향 재생 구간의 시간 |

기본 stateless 계산은 다음과 같다.

```cpp
result = interpolate(from, to, easing(progress));
```

Easing은 시간을 알지 못한다. Interpolator는 재생 상태를 알지 못한다. 이 분리를
지켜야 같은 easing을 vector, color, rect와 quaternion에 재사용할 수 있다.

---

## 4. 파일과 namespace 구성

예상 파일 구성은 다음과 같다.

```text
include/mathematics/
  easing.hpp             # 순수 easing 함수와 easing_function
  tween.hpp              # interpolator, stateless sample, tween<T>
  tween_views.hpp        # math::views pipe adaptor

tests/
  easing_test.cpp
  tween_test.cpp
  tween_views_test.cpp

bench/
  easing_tween_bench.cpp
```

- 공개 namespace는 `math`, easing 함수는 `math::easing`, view adaptor는
  `math::views`에 둔다.
- 타입, 함수, 열거형 값은 기존 규칙대로 `lower_snake_case`를 사용한다.
- `mathematics.hpp`는 세 헤더를 포함한다. 세부 헤더는 각각 단독 include가 가능해야 한다.
- 상태형 API와 range API는 구현 파일을 분리해 include 비용과 의존 관계를 단순하게 한다.

---

## 5. Easing API

### 5.1 제공 곡선

경쟁력 있는 공개 표면은 일부 다항식만이 아니라 통상적인 easing 계열 전체다.

```text
linear, step, smoothstep, smootherstep

quadratic_in, quadratic_out, quadratic_in_out
cubic_in,     cubic_out,     cubic_in_out
quartic_in,   quartic_out,   quartic_in_out
quintic_in,   quintic_out,   quintic_in_out
sine_in,      sine_out,      sine_in_out
circular_in,  circular_out,  circular_in_out
exponential_in, exponential_out, exponential_in_out
elastic_in,     elastic_out,     elastic_in_out
back_in,        back_out,        back_in_out
bounce_in,      bounce_out,      bounce_in_out
```

각 이름은 직접 호출 가능하고 adaptor에 값으로 전달할 수 있는 stateless function object로
제공한다.

```cpp
constexpr float t = math::easing::cubic_in_out(0.25f);
static_assert(t >= 0.0f && t <= 1.0f);
```

함수 객체를 쓰면 `views::ease()`가 곡선 타입을 보존해 인라인할 수 있고, 필요하면
정적 정책형 tween에도 같은 객체를 사용할 수 있다.

### 5.2 입력 범위와 clamp

개별 easing 함수의 계약상 입력은 `[0, 1]`이다. 개별 함수가 입력을 암묵적으로 clamp하지
않는다. 암묵적 clamp는 외삽 가능한 곡선을 막고 파이프 중간의 오류를 숨긴다.

명시적 조합을 제공한다.

```cpp
const float a = math::easing::cubic_in_out(u);          // raw
const float b = math::ease_clamped(u, math::easing::cubic_in_out);
```

- `ease_clamped`는 **입력만** `[0, 1]`로 제한한다.
- `back`과 `elastic`처럼 의도적으로 범위를 넘는 곡선의 출력은 제한하지 않는다.
- `NaN` 입력은 `NaN`으로 전파한다.
- 상태형 tween은 재생 정책이 만든 정규화 progress를 사용하므로 일반 재생 중에는
  easing에 `[0, 1]`만 전달한다.

### 5.3 끝점과 정확도

- 모든 곡선은 `u == 0`과 `u == 1`에서 각각 정확히 `0`과 `1`을 반환한다.
- 삼각·지수 곡선은 근사 함수의 끝점 오차가 노출되지 않도록 끝점을 명시적으로 처리한다.
- `linear`, polynomial 계열은 런타임과 상수 평가에서 같은 식을 사용한다.
- `sine`은 기존 `math::sin`/`math::cos` 경로를 사용한다.
- `circular`, `exponential`, `elastic` 구현 전에 C++20 상수 평가 가능한 `sqrt`와 `exp2`
  지원 범위를 확정한다. 런타임과 상수 평가에 서로 다른 눈에 띄는 결과를 만들지 않는다.

### 5.4 런타임 곡선 표현

Manager가 서로 다른 easing을 가진 `tween<T>`를 하나의 연속 컨테이너에 저장하려면
곡선이 `tween`의 C++ 타입을 바꾸면 안 된다. 기본 `tween<T>`는 다음과 같은 작은
무할당 호출 래퍼를 저장한다.

```cpp
class easing_function {
public:
    using function_type = float (*)(float) noexcept;

    constexpr easing_function() noexcept;

    template<class easing_type>
    constexpr easing_function(easing_type easing) noexcept;

    [[nodiscard]] constexpr float operator()(float progress) const noexcept;
};
```

- 기본 내장 function object와 captureless 사용자 함수만 허용한다.
- capturing lambda와 임의 크기 functor를 저장하기 위한 heap type erasure는 제공하지 않는다.
- 간접 호출 비용은 벤치마크와 codegen으로 확인한다.
- 실제 비용이 문제로 확인될 때만 `basic_tween<T, Easing, Interpolator>` 형태의 정적 정책
  변형을 추가한다. 측정 전에 공개 타입을 이원화하지 않는다.

---

## 6. Interpolation API와 타입 정책

### 6.1 기본 interpolator

Interpolator는 다음 호출 형태를 만족하는 stateless 객체다.

```cpp
T operator()(const T& from, const T& to, float progress) const noexcept;
```

Manager 친화적인 `tween<T>`는 easing과 같은 이유로 작은
`interpolation_function<T>`을 저장할 수 있다. 기본 제공 interpolator는 다음과 같다.

| 값 타입 | 기본/허용 보간 |
|---------|----------------|
| `float` | scalar `lerp` |
| `vector2/3/4` | 기존 SIMD `lerp` |
| `color` | 기존 SIMD `lerp`, 선형 색 공간 값 |
| `rect` | `x/y/width/height` 성분별 `lerp` |
| `quaternion` | `nlerp` 또는 `slerp`를 명시 |

Scalar `lerp(float, float, float)`와 `lerp(rect, rect, float)`는 이 기능의 선행 공개 API로
추가한다. 기존 vector와 color의 `lerp`, quaternion의 `nlerp`/`slerp` 이름은 유지한다.

### 6.2 Quaternion

Quaternion에는 일반 `lerp`라는 모호한 별칭을 추가하지 않는다. 생성 시 회전 보간 정책을
명시한다.

```cpp
auto rotation = math::make_tween(q0, q1, duration,
                                 math::interpolation::spherical,
                                 math::easing::sine_in_out);
```

- `normalized_linear`: 저비용, 최단 호, 각속도 비균일
- `spherical`: 최단 호, 일정한 각속도

두 방식 모두 기존 `nlerp`/`slerp` 정본 함수를 호출한다.

### 6.3 Rect와 Color

- `rect`는 현재와 같이 음수 width/height를 숨기지 않는다.
- 양쪽 rect가 정규화돼 있고 크기가 양수라면 중간 결과도 양수다.
- 유효성 정책을 바꾸기 위해 tween 내부에서 `normalized(rect)`를 호출하지 않는다.
- `color`는 현재 계약대로 선형 float 성분을 보간한다. sRGB 감마 변환을 자동으로 하지 않는다.
- `back`/`elastic` 출력은 색 성분을 `[0, 1]` 밖으로 보낼 수 있다. 필요하면 호출자가 결과에
  `saturate`를 적용한다.

### 6.4 제외 타입

- Matrix 성분 보간은 직교성, 회전과 scale 의미를 보존하지 않으므로 제공하지 않는다.
  TRS로 분해해 vector와 quaternion을 각각 보간한 뒤 다시 합성한다.
- AABB, sphere, frustum은 물체의 애니메이션 결과에서 다시 계산한다.
- 특히 `bounding_frustum`의 필드를 직접 보간하면 유효한 projection/frustum이라는 보장이
  없다. 카메라 위치·회전·투영 파라미터를 보간하고 frustum을 재생성한다.

---

## 7. Stateless tween

값 하나만 필요한 코드는 range나 상태 객체 없이 직접 호출한다.

```cpp
const vector3 position = math::tween_value(
    from, to, progress,
    math::easing::cubic_in_out,
    math::interpolation::linear);
```

계약은 단순하다.

```cpp
return interpolator(from, to, easing(progress));
```

- 함수는 시간을 누적하거나 입력 progress를 암묵적으로 clamp하지 않는다.
- `tween_value_clamped` 또는 명시적 `ease_clamped` 조합으로 clamp를 요청한다.
- 단일 오브젝트의 매 프레임 핫 패스에서는 이 직접 호출이 기본이다.
- wrapper가 수동 조합과 같은 어셈블리로 사라지는지 검사한다.

---

## 8. Range view

### 8.1 기본 조합

View는 progress range를 lazy하게 변환한다.

```cpp
constexpr std::array progress{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

auto positions =
    progress
    | math::views::ease(math::easing::cubic_in_out)
    | math::views::lerp(from, to);

for (const vector3 position : positions) {
    consume(position);
}
```

Quaternion은 보간 의미가 pipe에 드러난다.

```cpp
auto rotations =
    progress
    | math::views::ease(math::easing::sine_in_out)
    | math::views::slerp(q0, q1);
```

`views::tween`은 위 두 adaptor를 합친 편의 closure로 제공할 수 있다.

```cpp
auto positions = progress | math::views::tween(
    from, to, math::easing::cubic_in_out,
    math::interpolation::linear);
```

정본은 분리된 `ease | lerp/slerp` 조합이다. 합성 closure는 동작을 새로 구현하지 않는다.

### 8.2 View 계약

- 입력은 `std::ranges::viewable_range`이고 원소는 `float`로 변환 가능해야 한다.
- base range는 `std::views::all_t` 규칙으로 보관한다.
- 시작값, 끝값, easing, interpolator는 adaptor/view가 값으로 소유한다.
- 결과 원소는 계산된 prvalue이며 원본 progress에 대한 mutable reference가 아니다.
- base가 sized/random-access이면 결과도 해당 성질을 유지한다.
- fixed extent 입력은 기존 `fixed_transform_view`를 사용해 `static_extent_v`를 보존한다.
- 일반 동적 range는 표준 transform view에 해당하는 iterator 경로를 사용한다.
- view 자체는 동적 메모리를 할당하지 않는다.

### 8.3 시간 range 제한

View는 다음 입력에 적합하다.

- 이미 정규화된 progress 배열
- 절대 elapsed time을 duration으로 나눈 range
- 오프라인 샘플 베이킹
- UI 색상/위치 묶음 계산
- 테스트용 curve 샘플

프레임별 `delta_seconds` range를 transform하면서 iterator 내부에 누적 시간을 저장하는 것은
금지한다. 그러면 같은 iterator를 두 번 읽을 때 결과가 달라지고 multi-pass, random-access,
copy 의미가 깨진다. 실시간 delta 누적은 `tween<T>::advance()`가 담당한다.

### 8.4 상태형 tween에서 view 생성

1차 공개 범위에서는 상태형 tween의 snapshot member를 제공하지 않는다. 같은 결과가
필요하면 endpoint와 정책을 명시해 `views::tween`을 만들며, 반환 view는 이를 값으로
소유한다.

```cpp
auto samples = progress | math::views::tween(
    track.from(), track.to(), math::easing::cubic_in_out);
```

반환 view는 `track`을 참조하지 않고 from/to/easing/interpolator를 값으로 보관해야 한다.
Base range의 수명은 일반 view 규칙을 따르지만 track을 참조하지 않으므로 manager가 vector를
재할당하거나 track을 제거해도 이미 만든 view의 endpoint는 dangling하지 않는다.

---

## 9. 상태형 `tween<T>`

### 9.1 소유하는 상태

`tween<T>`는 manager에 바로 저장할 수 있는 self-contained 값 객체다.

```cpp
template<class T>
class tween {
    T from_;
    T to_;

    float elapsed_seconds_;
    float duration_seconds_;
    float initial_delay_seconds_;

    easing_function easing_;
    interpolation_function<T> interpolator_;
    tween_playback playback_;
    tween_state state_;
};
```

정확한 멤버 순서와 padding은 구현·벤치 후 정한다. 위 코드는 소유 관계를 설명하기 위한
것이며 ABI 선언이 아니다.

### 9.2 소유하지 않는 것

`tween<T>`에는 다음을 넣지 않는다.

- `T*`, `T&` 대상 property
- Entity/Actor/Component 포인터
- manager 포인터
- completion/update callback
- `std::function`, virtual base, `unique_ptr`
- frame clock 또는 platform timer

따라서 `std::vector<tween<T>>`가 재할당돼도 내부 수명이 깨지지 않는다. Tween 복사는
독립된 재생 상태 복사이고 이동은 원래 tween의 외부 연결을 옮기지 않는다. 실제 대상 연결은
manager entry가 소유한다.

### 9.3 기본 API

```cpp
template<class T>
struct tween_step {
    T value;
    tween_state state;
    std::uint32_t completed_cycles;

    [[nodiscard]] constexpr bool completed() const noexcept;
};

template<class T>
class tween {
public:
    [[nodiscard]] constexpr T sample() const noexcept;
    [[nodiscard]] constexpr T sample_at(float elapsed_seconds) const noexcept;
    [[nodiscard]] constexpr tween_step<T> advance(float delta_seconds) noexcept;

    constexpr void pause() noexcept;
    constexpr void resume() noexcept;
    constexpr void restart() noexcept;
    constexpr void seek(float elapsed_seconds) noexcept;

    [[nodiscard]] constexpr tween_state state() const noexcept;
    [[nodiscard]] constexpr bool finished() const noexcept;
};
```

- `sample`은 상태를 바꾸지 않는다.
- `sample_at`은 절대 시간에서 값을 계산하고 내부 elapsed를 바꾸지 않는다.
- `advance`만 elapsed와 playback state를 바꾼다.
- `advance`는 값과 함께 이번 호출에서 통과한 cycle 수를 반환한다.
- callback은 없으므로 manager가 반환 결과를 보고 이벤트를 큐잉한다.

### 9.4 생성 API

Builder를 제공하더라도 최종 타입은 선택한 easing에 따라 달라지지 않아야 한다.

```cpp
auto track = math::make_tween(from, to, 0.5f)
    .ease(math::easing::cubic_in_out)
    .delay(0.1f)
    .playback(math::tween_playback::ping_pong)
    .cycles(3);

static_assert(std::same_as<decltype(track), math::tween<vector3>>);
```

Fluent builder는 편의 계층이며 정본 생성자/설정 함수와 다른 의미를 만들지 않는다.

---

## 10. 재생과 시간 정책

### 10.1 상태

```cpp
enum class tween_state : std::uint8_t {
    playing,
    paused,
    completed
};
```

Cancellation은 manager entry의 수명 결정이므로 `tween_state`에 넣지 않는다.

### 10.2 Playback

```cpp
enum class tween_playback : std::uint8_t {
    once,
    loop,
    ping_pong
};
```

- `duration`은 from에서 to까지 한 방향 leg의 시간이다.
- `once`는 한 leg 뒤 to에서 완료한다.
- `loop`는 매 leg 뒤 from으로 돌아가 다시 진행한다.
- `ping_pong`은 forward와 backward 두 leg를 한 cycle로 센다.
- 유한 cycle 수는 1 이상이다.
- 무한 반복은 숫자 sentinel 대신 명시적 `infinite_cycles` 설정으로 표현한다.
- `initial_delay`는 첫 cycle 앞에서만 적용한다.

### 10.3 큰 delta

`advance(delta)`는 delta가 duration보다 크거나 여러 cycle을 건너도 정확해야 한다.
cycle 하나씩 반복하는 루프 대신 나눗셈과 나머지로 현재 leg와 progress를 계산한다.
반환되는 `completed_cycles`로 manager는 프레임 사이에 건너뛴 cycle 경계를 알 수 있다.

### 10.4 퇴화·잘못된 시간

- `duration == 0`은 즉시 to 값을 내는 완료 tween이다.
- 음수 또는 비유한 duration은 계약 위반이다. Debug assertion을 발생시키고 release에서는
  zero-duration 완료 tween으로 정규화해 NaN 상태가 manager 전체로 번지지 않게 한다.
- `delta_seconds == 0`은 현재 값과 상태를 그대로 반환한다.
- 음수 또는 비유한 delta는 계약 위반이다. 역방향 탐색은 `seek`로 명시한다.
- `seek`는 finite elapsed를 받고 playback 정책에 맞게 위치를 계산한다.

시간 단위의 정본은 초 단위 `float`다. Game loop의 기존 `delta_seconds`와 직접 연결되는
것을 우선한다. 필요하면 `std::chrono::duration` overload는 얇은 변환 계층으로 추가한다.

---

## 11. 개발자 소유 Manager 모델

### 11.1 기본 entry

Mathematics는 manager를 제공하지 않는다. 소비자는 대상 식별자와 tween을 한 entry에 둔다.

```cpp
struct position_tween_entry {
    entity_handle target;
    math::tween<math::vector3> track;
    bool remove = false;
};

std::vector<position_tween_entry> active;
```

`entity_handle`은 raw pointer보다 index+generation 또는 소비자 엔진의 검증 가능한 ID가
적합하다. 대상의 실제 소유권은 world/ECS/object system에 남는다.

### 11.2 Update와 제거

```cpp
for (auto& entry : active) {
    if (!world.is_alive(entry.target)) {
        entry.remove = true;
        continue;
    }

    const auto step = entry.track.advance(delta_seconds);
    world.set_position(entry.target, step.value);

    if (step.completed()) {
        completion_events.push_back(entry.target);
        entry.remove = true;
    }
}

std::erase_if(active, [](const auto& entry) {
    return entry.remove;
});

dispatch(completion_events);
```

- iteration 도중 erase하거나 callback을 실행하지 않는다.
- 완료/취소를 표시하고 iteration 뒤 제거한다.
- callback은 제거 후 별도 큐에서 실행해 재진입으로 인한 iterator 무효화를 막는다.
- 대상이 먼저 사라지면 manager가 ID 검증 실패를 보고 tween entry만 제거한다.

### 11.3 Tween handle

외부에서 pause/cancel/seek가 필요하면 manager가 handle을 발급한다.

```cpp
struct tween_handle {
    std::uint32_t index;
    std::uint32_t generation;
};
```

- handle은 소유권이 없는 weak token이다.
- manager가 generation을 검사한 뒤에만 slot에 접근한다.
- `std::vector` iterator나 `tween<T>*`를 외부 handle로 노출하지 않는다.
- handle destructor에서 자동 취소하지 않는다. 임시 객체와 복사에서 예상하지 못한 취소가
  발생하기 때문이다.
- scoped cancellation이 필요하면 소비자 manager가 별도 move-only RAII wrapper를 만든다.

### 11.4 여러 값 타입

Manager는 값 타입별 pool을 두는 방식을 권장한다.

```text
position_pool    tween<vector3>
scale_pool       tween<vector3>
rotation_pool    tween<quaternion>
color_pool       tween<color>
rect_pool        tween<rect>
```

서로 다른 `T`를 한 컨테이너에 넣기 위한 `any_tween`은 virtual dispatch, heap allocation,
큰 union 또는 방문 비용을 요구한다. Mathematics 코어에서는 제공하지 않는다. ECS에서는
각 tween을 별도 component로 저장하는 방식도 같은 소유 모델을 따른다.

---

## 12. ABI, 할당과 예외

- 기존 owning 수학 타입에는 멤버를 추가하지 않는다.
- `tween<T>`는 새 공개 타입이므로 첫 안정 릴리스 전까지 크기를 벤치하고 결정한다.
- `tween<T>`는 적어도 move constructible/move assignable이어야 하며 표준 컨테이너에
  직접 저장할 수 있어야 한다.
- 내장 Mathematics 타입을 사용하는 tween은 복사 가능하며 복사는 독립된 재생 상태를 만든다.
- easing/interpolation runtime wrapper는 동적 할당하지 않는다.
- 공개 연산은 `noexcept`다.
- callback과 대상 참조가 없으므로 destructor는 부작용이 없다.
- 기존 packed vector/matrix/quaternion/color/rect ABI는 그대로다.

---

## 13. 성능 원칙

Easing 다항식 자체의 나노초 차이보다 조합 계층이 불필요한 비용을 만들지 않는지가 더
중요하다.

### 직접 호출

```cpp
tween_value(a, b, u, easing, interpolator)
```

은 다음 수동 코드와 같은 codegen을 목표로 한다.

```cpp
interpolator(a, b, easing(u));
```

### View

- 일반 lazy range-for는 iterator 경로이며 무조건 zero-overhead라고 주장하지 않는다.
- 작은 고정 range는 기존 `fixed_transform_view`/`transform_fixed`로 static extent를
  유지한다.
- 단일 오브젝트 매 프레임 계산에는 직접 `tween_value` 또는 `tween<T>::advance`를 쓴다.
- 다수 샘플 베이킹과 range 조합에서 view를 쓴다.

### Runtime policy

Manager 친화적인 easing/interpolation 간접 호출은 다음과 비교한다.

- runtime function wrapper
- enum switch
- 정적 function object 직접 호출

MSVC와 clang-cl에서 latency와 많은 tween을 순회하는 throughput을 모두 측정한다. 간접 호출이
실제 병목으로 확인되지 않으면 manager의 균일한 타입과 사용자 정의 가능성을 우선한다.

---

## 14. 테스트 계획

### Easing

- 모든 함수의 `constexpr` 호출
- `u == 0`, `u == 1`의 정확한 끝점
- non-overshoot 곡선의 `[0, 1]` 범위와 단조성
- in/out 대칭 관계
- in-out 중앙값과 양 구간 연속성
- `back`/`elastic`의 의도적인 overshoot
- NaN 전파와 raw/clamped 정책
- double reference 또는 고정 golden sample 대비 오차

### Interpolation

- scalar/vector/color 결과와 기존 `lerp` 일치
- `rect` 성분과 음수 크기 정책
- quaternion `nlerp`/`slerp` 최단 호와 단위 길이
- 끝점 정확성
- back/elastic 결과를 임의로 saturate하지 않는지 확인

### View

- `std::ranges::view`, `sized_range`, `random_access_range` 조건
- lvalue/rvalue base range 수명
- pipe chain과 range-for
- fixed extent 보존
- 일반 view와 fixed view 결과 일치
- view가 endpoint와 정책을 값으로 소유하는지 확인
- endpoint와 정책을 소유한 `views::tween`이 track을 참조하지 않는지 확인

### 상태형 tween

- zero-duration 즉시 완료
- pause/resume/restart/seek
- once/loop/ping-pong
- 유한/무한 cycle
- initial delay
- duration보다 큰 delta와 여러 cycle 통과
- 완료 뒤 추가 advance의 안정성
- 복사 후 독립된 elapsed 상태
- move/reallocation 뒤 동일 결과
- manager의 generation handle과 2단계 제거 예제 테스트

### 구성 행렬

- MSVC Debug/Release
- clang-cl Release
- SSE2 baseline
- AVX2/FMA
- scalar fallback
- Linux GCC/Clang
- ARM64 NEON CI
- C++20와 C++23

---

## 15. Benchmark 계획

하나의 숫자로 기능 전체를 평가하지 않고 경로를 나눈다.

| 범주 | 비교 |
|------|------|
| polynomial easing | 직접 식, 내장 easing |
| trigonometric easing | 기존 scalar 삼각함수 경로 |
| smoothstep vector | Mathematics, SimpleMath `SmoothStep`, 동일 식 + `XMVectorLerp` |
| stateless tween | 수동 조합, `tween_value` |
| stateful tween | 수동 elapsed 갱신, `tween<T>::advance` |
| runtime policy | function wrapper, enum switch, 정적 정책 |
| view | 직접 loop, 일반 lazy view, fixed view |
| batch manager | 동일 타입 tween 수백/수천 개 순회 throughput |

View benchmark는 일반 iterator와 fixed extent 결과를 분리한다. DirectXMath에 easing이나
상태형 tween 정본이 없으므로 동일 easing 식과 `XMVectorLerp`를 조합한 수동 baseline을
사용한다. SimpleMath 비교는 제공되는 `SmoothStep` 범위에 한정한다.

성능 회귀 판단은 같은 머신·같은 toolchain에서 baseline과 후보를 비교한다. Hosted runner의
DirectXMath 절대 비율 하나만으로 회귀 원인을 확정하지 않는다.

### 15.1 1차 측정 결과

2026-08-26, Core i7-8700K, MSVC C++23 AVX2/FMA Release에서 512개 batch를 5회 반복한
wall-time median은 다음과 같다. Windows의 짧은 CPU timer는 양자화가 커서 이 표는
wall time을 사용한다.

| 경로 | 512개 또는 1회 median | 수동 기준 |
|------|----------------------:|----------:|
| 수동 smoothstep | 34.2 ns | 1.00x |
| 정적 `easing::smoothstep` | 35.2 ns | 1.03x |
| 수동 vector3 smoothstep+lerp | 451 ns | 1.00x |
| `tween_value<vector3>` | 452 ns | 1.00x |
| fixed `views::tween<vector3>` | 449 ns | 1.00x |
| 동일 식 + DirectXMath `XMVectorLerp`/`XMStoreFloat3` | 818 ns | 1.81x |
| 수동 상태 누산기 1회 | 2.57 ns | 1.00x |
| `tween<vector3>::advance` 1회 | 13.0 ns | 5.06x |

정적 호출과 stateless/fixed-view 조합은 측정 오차 안에서 수동 식과 같다. DirectXMath 행은
동일 저장까지 포함한 이 하니스의 비교이며 라이브러리 전체 우열로 일반화하지 않는다.
상태형 경로의 약 10 ns 추가 비용은 timeline 분기와 easing/interpolation 함수 포인터 두 번의
대가다. 한 종류만 vectorize한 erased easing은 정적 호출보다 크게 느리지만, 서로 다른 두
곡선을 섞은 batch에서는 enum switch 646 ns, function pointer 792 ns로 약 23% 차이였다.

따라서 핫한 단일 정책 batch에는 `tween_value`/view를 쓰고, 서로 다른 정책을 같은
`std::vector<tween<T>>`에 보관해야 할 때 상태형 타입을 쓴다. SimpleMath 헤더는 현재
checkout에 없어 결과·성능 패리티를 주장하지 않으며, DirectXMath의 원시 보간 조합만
재현 가능한 baseline으로 유지한다.

---

## 16. 구현 단계

### 단계 1 — scalar와 easing

- scalar `lerp`, easing에 필요한 `sqrt`/`exp2` 정책 확정
- 전체 easing function object
- raw/clamped 계약
- constexpr, 정확도, 끝점 테스트

### 단계 2 — interpolation과 stateless tween

- `rect` lerp
- interpolation function objects
- `tween_value`/`tween_value_clamped`
- quaternion 명시 정책
- DirectXMath/SimpleMath 결과 비교

### 단계 3 — range view

- `views::ease`, `views::ease_clamped`
- `views::lerp`, `views::nlerp`, `views::slerp`
- 합성 `views::tween`
- dynamic/fixed extent 경로와 concept 테스트

### 단계 4 — 상태형 tween

- `tween<T>`, `tween_step<T>`
- once/loop/ping-pong, delay, cycle
- pause/resume/restart/seek
- runtime easing/interpolation 표현
- 큰 delta와 퇴화 시간 테스트

### 단계 5 — manager 예제와 성능 검증

- generation handle을 사용하는 예제 manager
- mark/sweep와 deferred callback 예제
- MSVC/clang-cl codegen
- Direct/fixed/lazy/stateful/batch benchmark
- GUIDE/EXPRESSIONS/README 문서 반영

각 단계는 테스트와 문서를 함께 끝낸 뒤 다음 단계로 넘어간다. 전체 구현이 끝난 뒤 한 번에
정확성·성능 계약을 붙이지 않는다.

---

## 17. 공개 결정 결과

1. `easing_function`과 `interpolation_function<T>`은 각각 함수 포인터 하나로 정했다.
   enum보다 mixed batch에서 약 23% 느리지만 사용자 정의 stateless 정책과 균일한
   `tween<T>` 타입을 유지한다.
2. `tween<T>`는 endpoint, 초 단위 float 3개, 함수 포인터 2개, cycle/playback/state만
   소유한다. 대상·callback·clock·manager 참조는 없다.
3. `exp2`는 C++20 상수평가 가능한 단일 구현이며 `[-16, 16]` sweep에서 `std::exp2` 대비
   상대 오차 2e-6 이내, `-149`, `-150`, `128` 경계도 테스트한다.
4. `track.samples()`는 넣지 않았다. endpoint/policy를 값으로 보관하는 `views::tween`이
   중복 API 없이 snapshot 역할을 한다.
5. ping-pong 한 cycle은 from→to→from 왕복으로 고정했다. loop 한 cycle은 from→to이고
   once는 cycle 설정과 관계없이 한 번만 재생한다.
6. `std::chrono::duration` overload는 1차 범위에서 제외하고 초 단위 float API 하나만 둔다.
7. adaptor는 `views::all_t`의 static extent를 검사해 fixed transform과 표준 dynamic
   transform을 자동 선택한다. `const`, `ref_view`, `owning_view`도 extent를 전달한다.

확정한 주장은 정적 stateless 조합의 수동 식 패리티, 무할당 manager 저장 모델과 검증된
곡선 범위다. GLM 전체 API 패리티나 모든 view의 보편적 zero-overhead는 주장하지 않는다.

---

## 18. 경쟁 기준

- DirectXMath는 보간 원시 함수와 비교 성능의 기준이다.
- SimpleMath `SmoothStep`은 clamp된 smooth interpolation의 결과·사용성 비교 기준이다.
- GLM `GTX_easing`은 곡선 종류 범위의 기준이다.
- Magnum Animation easing은 raw/clamped 조합과 곡선 문서화의 기준이다.
- Tweeny는 전용 manager/timeline 라이브러리의 범위를 보여주는 참고이며 기능 패리티
  대상이 아니다.

참고:

- [DirectXTK SimpleMath](https://github.com/microsoft/DirectXTK/wiki/SimpleMath)
- [GLM GTX easing](https://github.com/g-truc/glm/blob/master/glm/gtx/easing.hpp)
- [Magnum Animation Easing](https://doc.magnum.graphics/magnum/structMagnum_1_1Animation_1_1BasicEasing.html)
- [Tweeny](https://github.com/mobius3/tweeny)

Mathematics의 차별점은 곡선의 존재만이 아니다. packed 게임 수학 타입, DirectX 규약,
`constexpr`, SIMD 보간, fixed range와 manager 친화적인 무할당 소유 모델이 하나의 검증된
API로 연결되는 것이 차별점이다.
