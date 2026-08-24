# Mathematics 표현식과 기능 둘러보기

이 문서는 Mathematics로 게임과 렌더링 코드를 어떤 표현식으로 작성할 수 있는지
예제 중심으로 소개한다. 설치, 퇴화 입력 정책, DirectXMath 이주표까지 포함한 상세 규약은
[GUIDE.md](GUIDE.md)를 참고한다.

## 시작하기

대부분의 코드에서는 우산 헤더 하나로 전체 API를 사용할 수 있다.

```cpp
#include <mathematics/mathematics.hpp>

using math::matrix4x4;
using math::quaternion;
using math::vector2;
using math::vector3;
using math::vector4;
```

`std::format` 지원은 컴파일 비용을 분리하기 위해 별도 헤더에 있다.

```cpp
#include <mathematics/format.hpp>
```

CMake 소비자는 정본 타깃에 링크한다.

```cmake
add_subdirectory(external/mathematics)
target_link_libraries(my_game PRIVATE Mathematics::Mathematics)
target_compile_features(my_game PRIVATE cxx_std_20)
```

## 먼저 기억할 표현 규칙

| 표현 | 의미 |
|------|------|
| `a + b`, `a - b` | 벡터 성분별 덧셈과 뺄셈 |
| `a * b`, `a / b` | 벡터끼리는 성분별 곱셈과 나눗셈 |
| `v * scalar` | 벡터의 스칼라 배 |
| `dot(a, b)` | 벡터 내적. 벡터끼리의 `*`와 다르다 |
| `v * matrix` | 행벡터 변환. `matrix * v`는 제공하지 않는다 |
| `first * second` | 행렬과 쿼터니언 모두 왼쪽 변환을 먼저 적용한다 |
| `a == b` | 부동소수점 값의 정확한 비교 |
| `near_equal(a, b, epsilon)` | 호출자가 허용 오차를 정하는 근사 비교 |
| `radians(60.0f)` | 도를 라디안으로 변환. 각도 API는 라디안을 받는다 |

행렬은 row-major로 저장하며 `m[row][column]`으로 접근한다. 이동 성분은 마지막 열이
아니라 `matrix4x4`의 3행에 있다. 투영 행렬의 깊이 범위는 Direct3D 방식인 `[0, 1]`이다.

## 한눈에 보는 실전 표현식

다음은 자주 쓰는 의도를 Mathematics 표현식으로 옮긴 예다.

```cpp
// 프레임 이동
position += velocity * delta_seconds;

// 두 점 사이의 방향과 거리
const vector3 offset = target - position;
const vector3 direction = math::normalize(offset);
const float distance = math::length(offset);

// 거리 비교만 필요할 때는 제곱근을 피한다.
const bool in_range = math::distance_sq(position, target) <= range * range;

// 속도를 축 방향과 수직 방향으로 분리
const vector3 along_axis = axis * math::dot(velocity, axis); // axis는 단위 벡터
const vector3 lateral = velocity - along_axis;

// 표면 반사와 굴절
const vector3 reflected = math::reflect(incident, normal);   // normal은 단위 벡터
const vector3 refracted = math::refract(incident, normal, eta);

// 색상 보간 후 [0, 1] 범위로 제한
const vector4 color = math::saturate(math::lerp(color_a, color_b, t));

// 크기 -> 회전 -> 이동 순서의 월드 행렬
const matrix4x4 world = math::compose(scale, rotation, translation);

// 로컬 위치와 방향을 월드 공간으로 변환
const vector3 world_position = math::transform_point(local_position, world);
const vector3 world_forward = math::transform_direction(local_forward, world);
```

`normalize(0 벡터)`는 0 벡터를 반환한다. `normalize_est`는 더 빠른 근사 함수지만
이 보호 동작이 없으므로 입력이 정상적인 비영 벡터임을 보장할 수 있는 핫 패스에서만 쓴다.

## 스칼라와 각도

`scalar.hpp`는 C++20에서도 상수 평가할 수 있는 각도 변환과 삼각함수를 제공한다.

```cpp
constexpr float fov = math::radians(60.0f);
constexpr float half_turn = math::pi;
constexpr float quarter_turn = math::half_pi;

constexpr float s = math::sin(quarter_turn);
constexpr float c = math::cos(quarter_turn);
constexpr float angle = math::atan2(1.0f, 1.0f);

float sine = 0.0f;
float cosine = 0.0f;
math::sin_cos(fov, sine, cosine); // 두 값이 모두 필요할 때 한 번에 계산
```

제공 함수는 `sin`, `cos`, `sin_cos`, `tan`, `asin`, `acos`, `atan`, `atan2`다.
상수는 `pi`, `two_pi`, `half_pi`, `quarter_pi`, `inv_pi`, `inv_two_pi`를 제공한다.
`radians`와 `degrees`는 표현식 경계에서 단위를 드러내는 데 사용한다.

```cpp
const float yaw_radians = math::radians(yaw_degrees);
const float displayed_degrees = math::degrees(yaw_radians);
```

## 벡터 표현식

### 생성, 접근, 분해

`vector2`, `vector3`, `vector4`는 패킹된 공개 멤버 타입이다. 0, 1, 단위 축 생성자와
인덱스 접근, 구조적 바인딩을 지원한다.

```cpp
vector3 position{10.0f, 2.0f, -5.0f};
vector3 all_twos{2.0f};

const vector3 origin = vector3::zero();
const vector3 scale = vector3::one();
const vector3 up = vector3::unit_y();

position[1] += 3.0f;
const auto [x, y, z] = position;
```

### 산술과 범위 처리

```cpp
const vector3 sum = a + b;
const vector3 difference = a - b;
const vector3 component_product = a * b;
const vector3 component_quotient = a / b;
const vector3 scaled = a * 2.0f;

const vector3 positive = math::abs(value);
const vector3 lower = math::min(a, b);
const vector3 upper = math::max(a, b);
const vector3 limited = math::clamp(value, minimum, maximum);
const vector3 unit_range = math::saturate(value);
const vector3 blended = math::lerp(a, b, t);
```

`lerp(a, b, t)`는 `t`를 자동으로 `[0, 1]`에 제한하지 않는다. 외삽을 막아야 한다면
호출부에서 `t`를 제한한다.

```cpp
const float clamped_t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
const vector3 blended = math::lerp(a, b, clamped_t);
```

### 길이, 방향, 기하

```cpp
const float alignment = math::dot(forward, to_target);
const float speed_squared = math::length_sq(velocity);
const float speed = math::length(velocity);
const float separation = math::distance(a, b);
const vector3 unit_direction = math::normalize(to_target);

// 오른손 법칙: +X x +Y = +Z
const vector3 face_normal = math::normalize(math::cross(edge_a, edge_b));
```

2D의 `cross`는 3D 외적의 Z 성분에 해당하는 스칼라를 반환한다. 부호로 회전 방향이나
점의 선분 좌우를 판정할 수 있다.

```cpp
const float turn = math::cross(edge, point - line_start);
const bool is_left = turn > 0.0f;
const vector2 left_normal = math::perpendicular(edge); // 반시계 90도 회전
```

`reflect`는 단위 법선을 전제로 하고, `refract`에는 단위 입사 방향과 단위 법선을
사용한다. `refract`에서 전반사가 발생하면 0 벡터를 반환한다.

### 비교

```cpp
if (current == expected) {
    // 비트 수준의 정확한 값 비교가 필요할 때
}

if (math::near_equal(current, expected, 1e-4f)) {
    // 계산 결과를 허용 오차 안에서 비교할 때
}
```

## 행렬과 공간 변환

### 기본 행렬

```cpp
const matrix4x4 identity = matrix4x4::identity();
const matrix4x4 scale_m = math::scaling_matrix(vector3{2.0f, 1.0f, 0.5f});
const matrix4x4 uniform_scale_m = math::scaling_matrix(2.0f);
const matrix4x4 translation_m = math::translation_matrix(vector3{10, 0, 5});
const matrix4x4 rotation_m = math::rotation_y(math::radians(45.0f));
```

`rotation_x`, `rotation_y`, `rotation_z`는 각 축 회전 행렬을 만든다. 여러 변환은 실제
적용 순서대로 왼쪽에서 오른쪽으로 쓴다.

```cpp
// 점에 크기, 회전, 이동을 차례대로 적용한다.
const matrix4x4 world = scale_m * rotation_m * translation_m;

// 같은 TRS 의도를 더 간결하게 표현한다.
const matrix4x4 composed = math::compose(scale, rotation, translation);
```

`compose`로 만든 행렬은 다시 TRS로 분해할 수 있다. 분해할 수 없는 행렬이면 `false`를
반환하고 출력 인자는 바꾸지 않는다.

```cpp
vector3 recovered_scale;
quaternion recovered_rotation;
vector3 recovered_translation;

if (math::decompose(world, recovered_scale,
                    recovered_rotation, recovered_translation)) {
    // 분해 성공
}
```

### 위치, 방향, 동차 좌표

```cpp
const vector3 p = math::transform_point(local_position, world);       // w = 1
const vector3 d = math::transform_direction(local_direction, world); // w = 0
const vector4 clip = vector4{local_position.x, local_position.y,
                             local_position.z, 1.0f} * world_view_projection;
```

위치에는 이동이 적용되지만 방향에는 적용되지 않는다. 비균등 스케일이 포함된 행렬로
법선을 변환할 때는 역전치 행렬을 사용한다.

```cpp
const matrix4x4 normal_matrix = math::transpose(math::inverse(world));
const vector3 world_normal = math::normalize(
    math::transform_direction(local_normal, normal_matrix));
```

### 조회와 역행렬

```cpp
const float d = math::determinant(world);
const matrix4x4 transposed = math::transpose(world);
const matrix4x4 world_to_local = math::inverse(world);

const vector4 first_row = world.get_row(0);
const vector4 first_column = world.get_column(0);
const vector3 right = world.right();
const vector3 up = world.up();
const vector3 forward = world.forward();
const vector3 translation = world.translation();
const float element = world(2, 1);
```

특이하거나 비유한 행렬의 `inverse`는 항등행렬을 반환한다. 역행렬 존재 여부 자체가
중요한 코드에서는 먼저 `determinant`를 검사한다.

## 쿼터니언 회전

쿼터니언은 `(x, y, z, w)` 순서이며 스칼라 `w`가 마지막이다.

```cpp
const quaternion identity = quaternion::identity();

const quaternion yaw = math::quaternion_from_axis_angle(
    vector3::unit_y(), math::radians(45.0f));

const quaternion camera_rotation = math::quaternion_from_pitch_yaw_roll(
    pitch, yaw_angle, roll);

const quaternion same_from_vector = math::quaternion_from_euler(
    vector3{pitch, yaw_angle, roll});
```

쿼터니언 곱도 행렬과 마찬가지로 왼쪽 회전을 먼저 적용한다.

```cpp
const quaternion combined = turn_left * look_up;
const vector3 rotated = math::rotate(local_forward, combined);

// 위 표현과 같은 적용 순서다.
const vector3 same = math::rotate(
    math::rotate(local_forward, turn_left), look_up);

const vector3 local_again = math::inverse_rotate(rotated, combined);
```

보간과 변환도 제공한다.

```cpp
const quaternion smooth = math::slerp(start, end, t);
const quaternion fast = math::nlerp(start, end, t);
const quaternion unit = math::normalize(rotation);
const quaternion undone = math::inverse(rotation);

const matrix4x4 rotation4 = math::rotation_matrix(rotation);
const math::matrix3x3 rotation3 = math::rotation_matrix3x3(rotation);
const quaternion restored = math::quaternion_from_rotation_matrix(rotation4);

const vector3 pitch_yaw_roll = math::to_euler(rotation);
vector3 axis;
float angle = 0.0f;
math::to_axis_angle(rotation, axis, angle);
```

`q`와 `-q`는 값은 다르지만 같은 회전을 나타낸다. 회전 동등성을 검사할 때는
`near_equal` 대신 `same_rotation`을 사용한다.

```cpp
const bool equivalent = math::same_rotation(q, -q);
```

## 카메라와 투영

손잡이는 함수 이름에서 반드시 선택한다. 다음 예는 왼손 좌표계와 Direct3D 깊이 범위를
사용한다.

```cpp
const matrix4x4 view = math::look_at_lh(
    vector3{0, 5, -10}, // eye
    vector3{0, 0, 0},   // target
    vector3::unit_y()); // up

const matrix4x4 projection = math::perspective_fov_lh(
    math::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

// 모델 -> 뷰 -> 투영 순서
const matrix4x4 world_view_projection = world * view * projection;
const vector4 clip = local_vertex * world_view_projection;
const vector3 ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
```

사용 가능한 카메라·투영 생성 함수는 다음과 같다.

| 목적 | 왼손 좌표계 | 오른손 좌표계 |
|------|-------------|---------------|
| 위치와 대상점으로 뷰 생성 | `look_at_lh` | `look_at_rh` |
| 위치와 방향으로 뷰 생성 | `look_to_lh` | `look_to_rh` |
| 수직 FOV 원근 투영 | `perspective_fov_lh` | `perspective_fov_rh` |
| 근평면 크기 원근 투영 | `perspective_lh` | `perspective_rh` |
| 중앙 직교 투영 | `orthographic_lh` | `orthographic_rh` |
| 비대칭 직교 투영 | `orthographic_off_center_lh` | `orthographic_off_center_rh` |

`look_at_*`에서 시선 방향과 up이 평행하거나 방향이 0인 경우에는 항등행렬을 반환한다.

## 기하 도형과 공간 질의

### 도형 생성과 확장

```cpp
const math::sphere trigger{vector3{0, 1, 0}, 2.0f};

// aabb 생성자는 center와 반폭(extents)을 받는다.
const math::aabb centered_box{vector3{0, 0, 0}, vector3{1, 2, 3}};

// 최소/최대 좌표에서 만들 때는 전용 팩터리를 사용한다.
const math::aabb unit_box = math::aabb::from_min_max(
    vector3{0, 0, 0}, vector3{1, 1, 1});

math::aabb bounds; // 빈 상자. merge의 항등원
for (const vector3& point : points) {
    bounds = math::merge(bounds, point);
}

const math::aabb padded = math::expand(bounds, 0.25f);
const math::sphere enclosing_sphere = math::bounding_sphere(bounds);
const vector3 nearest = math::closest_point(bounds, query_point);
```

연속 메모리의 점 배열이 있다면 `aabb_from_points(points, count)`로 한 번에 경계를 만들 수
있다. 구를 감싸는 AABB는 `bounding_box(sphere)`로 만든다.

### 교차와 포함

```cpp
const bool overlaps = math::intersects(player_bounds, trigger);
const bool point_inside = math::intersects(player_bounds, player_position);

const math::containment relation = math::contains(outer, inner);
switch (relation) {
case math::containment::disjoint:
    break;
case math::containment::intersects:
    break;
case math::containment::contains:
    break;
}
```

`contains(outer, inner)`는 비대칭이다. 결과는 두 인자가 떨어짐, 일부 겹침, 두 번째 인자가
첫 번째 인자에 완전히 포함됨을 뜻한다. 경계에 정확히 닿는 경우는 교차로 센다.

### 평면

```cpp
const math::plane ground = math::plane_from_point_normal(
    vector3{0, 0, 0}, vector3::unit_y());

const float height = math::signed_distance(ground, position);
const math::plane_side side = math::classify_point(ground, position);
const vector3 on_ground = math::closest_point_on_plane(ground, position);
const vector3 mirrored = math::reflect_point(ground, position);

const math::plane triangle_plane = math::plane_from_points(v0, v1, v2);
const math::plane reversed = math::flip(triangle_plane);
```

`plane`은 `ax + by + cz + d = 0`을 저장한다. 생성 팩터리는 단위 법선을 만들지만
네 실수 생성자는 자동 정규화하지 않는다. 행렬에서 직접 추출한 평면에는
`math::normalize(plane)`을 적용해야 `signed_distance`가 실제 거리 단위가 된다.

구나 AABB 전체가 평면의 어느 쪽에 있는지는 `classify(volume, plane)`으로 검사한다.
결과는 `plane_side::front`, `back`, `straddling` 중 하나다.

### 레이캐스트

```cpp
const math::ray pick_ray = math::normalize_direction(
    math::ray{camera_position, cursor_direction});

float hit_distance = 0.0f;
if (math::raycast(pick_ray, bounds, hit_distance)) {
    const vector3 hit_position = pick_ray.point_at(hit_distance);
}
```

`raycast`는 `sphere`, `aabb`, `plane` 오버로드를 제공하고 삼각형에는
`raycast_triangle(ray, v0, v1, v2, distance)`를 사용한다. 레이는 반직선이므로 원점 뒤의
교점은 적중이 아니다. 원점이 도형 안에 있으면 거리는 0이다.

방향을 정규화한 레이의 결과는 실제 거리다. 정규화하지 않았다면 결과는
`origin + direction * t`의 매개변수 `t`다.

## 컴파일 타임 표현식

주요 API는 런타임 SIMD 경로와 같은 호출 형태로 `constexpr` 평가할 수 있다.

```cpp
constexpr vector3 x = vector3::unit_x();
constexpr vector3 y = vector3::unit_y();
static_assert(math::cross(x, y) == vector3::unit_z());

constexpr quaternion quarter_turn = math::quaternion_from_axis_angle(
    vector3::unit_z(), math::half_pi);
constexpr vector3 turned = math::rotate(x, quarter_turn);
static_assert(turned.y > 0.999f);

constexpr matrix4x4 identity = matrix4x4::identity();
static_assert(math::inverse(identity) == identity);
```

컴파일 타임과 런타임의 부동소수점 결과는 컴파일러의 FMA 결합 때문에 극소수 ULP만큼
다를 수 있다. 두 경로의 결과를 비교할 때도 `near_equal`을 사용한다.

## 구조적 바인딩과 출력

패킹 타입의 공개 멤버는 구조적 바인딩으로 읽거나 참조할 수 있다.

```cpp
vector3 position{1, 2, 3};
auto& [x, y, z] = position;
y = 10.0f; // position.y도 변경된다.

const auto [center, radius] = math::sphere{position, 5.0f};
const auto [origin, direction] = math::ray{};
```

표준 라이브러리가 `<format>`을 지원하는 환경에서는 `format.hpp`를 포함한 뒤 모든 주요
저장 타입을 출력할 수 있다. 실수 포맷 지정자는 각 성분에 전달된다.

```cpp
#include <mathematics/format.hpp>

const std::string message = std::format(
    "position={:.2f} velocity={:+.1f}", position, velocity);
// position=(1.00, 2.00, 3.00) velocity=(+0.0, +0.0, +1.0)

const std::string box_text = std::format("{}", bounds);
// aabb(min=(...), max=(...)) 또는 aabb(empty)
```

행렬은 저장 규약 그대로 한 행씩 출력되며, 쿼터니언은 `(x, y, z, w)` 순서로 출력된다.

## 기능별 헤더

| 기능 | 헤더 |
|------|------|
| 전체 API | `<mathematics/mathematics.hpp>` |
| 스칼라, 각도, 삼각함수 | `<mathematics/scalar.hpp>` |
| 벡터 | `<mathematics/vector.hpp>` |
| 행렬 | `<mathematics/matrix.hpp>` |
| 쿼터니언 | `<mathematics/quaternion.hpp>` |
| TRS, 뷰, 투영 | `<mathematics/transform.hpp>` |
| 바운드, 평면, 레이, 교차 판정 | `<mathematics/geometry.hpp>` |
| 저수준 SIMD 레지스터 | `<mathematics/vec_reg.hpp>` |
| `std::format` | `<mathematics/format.hpp>` |

일반 게임 코드에는 패킹 타입과 고수준 함수가 적합하다. 인라인되지 않는 핫 루프에서
레지스터 적재와 저장이 병목으로 확인됐을 때만 `vec_reg` 저수준 API를 직접 사용한다.

## 자주 틀리는 표현

| 피해야 할 표현 | 올바른 표현 | 이유 |
|----------------|-------------|------|
| `a * b`를 내적으로 해석 | `dot(a, b)` | 벡터 `*`는 성분별 곱이다 |
| `matrix * vector` | `vector * matrix` | 이 라이브러리는 행벡터 규약이다 |
| `translation * rotation * scale` | `scale * rotation * translation` | 변환은 왼쪽부터 적용된다 |
| 방향에 `transform_point` | `transform_direction` | 방향에는 이동을 적용하지 않는다 |
| 비균등 스케일 법선에 월드 행렬 직접 적용 | 역전치 행렬 사용 | 법선의 직교성을 보존해야 한다 |
| AABB 생성자에 min/max 전달 | `aabb::from_min_max` | 생성자는 center/extents를 받는다 |
| `near_equal(q, -q)`로 회전 비교 | `same_rotation(q, -q)` | 두 값은 같은 회전을 나타낸다 |
| 손잡이 없는 카메라 함수 가정 | `_lh` 또는 `_rh`를 명시 | 기본 손잡이는 제공하지 않는다 |
| 레이 거리만 믿고 비정규화 방향 사용 | `normalize_direction(ray)` | 결과 단위를 실제 거리로 맞춘다 |

입력이 0, 비유한 값, 특이 행렬인 경우의 전체 반환 정책은
[GUIDE.md의 퇴화 입력 규약](GUIDE.md#4-퇴화-입력의-규약)을 참고한다.
