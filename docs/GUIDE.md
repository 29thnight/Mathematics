# Mathematics 사용 가이드

헤더 온리다. `include/`를 인클루드 경로에 넣거나 CMake에서 `mathematics` INTERFACE
타깃에 링크하면 끝이다.

```cmake
add_subdirectory(path/to/mathematics)
target_link_libraries(my_game PRIVATE Mathematics::Mathematics)
```

```cpp
#include <mathematics/mathematics.hpp>   // 전부
// 또는 필요한 것만
#include <mathematics/vector.hpp>
#include <mathematics/transform.hpp>
```

`mathematics/format.hpp`만 우산 헤더에서 빠져 있다. `<format>`은 무겁다. 출력하지 않는
번역 단위까지 그 비용을 물릴 이유가 없다.

---

## 1. 관례 — 먼저 읽을 것

이 여덟 줄을 모르면 컴파일되고 렌더링되지만 틀린 코드를 쓰게 된다. 전부
**DirectXMath에서 관측해 확정**했다.

| 항목 | 결정 |
|------|------|
| 행렬 저장 | row-major, `m[row][col]`, 한 행이 연속 |
| 벡터 | **행벡터**, `v * M` (`M * v`가 아니다) |
| 합성 순서 | 왼쪽에서 오른쪽, 적용 순서 그대로 — `S * R * T`는 크기→회전→이동 |
| 이동 성분 | **3행** `m[3][0..2]`, 3열이 아니다 |
| 쿼터니언 저장 | `(x, y, z, w)`, **스칼라가 마지막** |
| 쿼터니언 곱 | `a * b`는 **"a 적용 후 b"** — 교과서 해밀턴 곱의 반대 |
| 손잡이 | 함수 이름에 **항상** 있다. `look_at_lh` / `look_at_rh`, 기본값 없음 |
| 깊이 범위 | Direct3D식 `[0, 1]` (OpenGL의 `[-1, 1]`이 아니다) |
| 각도 | 라디안. `radians()` / `degrees()`로 변환 |

### 쿼터니언 곱 순서가 반대인 이유

교과서는 "a 다음 b"를 `b · a`로 쓴다. 이 라이브러리는 `a * b`로 쓴다.
그래야 모든 계층이 뒤집기 없이 맞아떨어지기 때문이다:

```cpp
math::rotate(v, a * b) == math::rotate(math::rotate(v, a), b); // 좌→우
math::rotation_matrix(a * b) == math::rotation_matrix(a) * math::rotation_matrix(b);
v * (m1 * m2) == (v * m1) * m2;
```

교과서 순서를 택했다면 쿼터니언에서 행렬로 넘어갈 때마다 순서를 뒤집어야 한다.

---

## 2. 5분 예제

```cpp
#include <mathematics/mathematics.hpp>

// 물체의 월드 행렬 — 크기, 회전, 이동
const math::quaternion spin = math::quaternion_from_axis_angle(
    math::vector3{0, 1, 0}, math::radians(30.0f));
const math::matrix4x4 world = math::compose(
    math::vector3{2, 2, 2}, spin, math::vector3{10, 0, 5});

// 카메라
const math::matrix4x4 view = math::look_at_lh(
    math::vector3{0, 5, -10}, // 눈
    math::vector3{0, 0, 0},   // 대상
    math::vector3{0, 1, 0});  // 위
const math::matrix4x4 proj = math::perspective_fov_lh(
    math::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

// 좌→우로 읽는다: 모델 → 뷰 → 투영
const math::matrix4x4 mvp = world * view * proj;

// 정점 하나를 클립 공간으로
const math::vector4 clip = math::vector4{1, 0, 0, 1} * mvp;
const math::vector3 ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
```

### 방향과 위치는 다른 함수다

```cpp
const math::vector3 world_pos = math::transform_point(local_pos, world); // w = 1, 이동 적용
const math::vector3 world_normal =
    math::transform_direction(local_normal, world); // w = 0, 이동 무시
```

법선에 `transform_point`를 쓰는 것이 고전적인 버그라서 이름을 나눴다.
(비균등 스케일이 있으면 법선은 역전치로 변환해야 한다 —
`transpose(inverse(world))`.)

### 컴파일 타임 계산

전 API가 `constexpr` 문맥에서 동작한다. DirectXMath가 못 하는 지점이다.

```cpp
constexpr math::matrix4x4 proj =
    math::perspective_fov_lh(math::half_pi, 1.0f, 1.0f, 100.0f);
constexpr math::quaternion turn = math::quaternion_from_axis_angle(
    math::vector3{0, 0, 1}, math::half_pi);
constexpr float sine = math::sin(0.5f);
static_assert(math::inverse(math::matrix4x4::identity()) ==
              math::matrix4x4::identity());
```

---

## 3. 타입

| 타입 | 크기 | 비고 |
|------|------|------|
| `vector2/3/4` | 8 / 12 / 16 B | 패킹. 정점 버퍼에 그대로 들어간다 |
| `quaternion` | 16 B | `(x,y,z,w)`. **`vector_like`가 아니다** |
| `matrix3x3` | 36 B | 회전·크기. 이동 없음 |
| `matrix4x4` | 64 B | 상수 버퍼에 그대로 업로드 가능 |
| `color` | 16 B | 선형 RGBA, 기본값은 opaque black |
| `rect` | 16 B | float `x/y/width/height`, 점 포함은 half-open |
| `plane` | 16 B | `(a,b,c,d)`, `d = -dot(n, p)` |
| `sphere` | 16 B | 중심 + 반지름 |
| `aabb` | 24 B | 중심 + **extents**(반폭) |
| `ray` | 24 B | 원점 + 방향, **반직선** |
| `bounding_frustum` | 52 B | origin/orientation + 4 slopes + near/far |
| `vec_reg` | 16 B | 레지스터 타입. 핫 루프에서 직접 |

`quaternion`이 `vector_like`가 아닌 것은 의도다. 벡터의 `*`는 HLSL을 따라
성분별 곱인데, 쿼터니언에 그게 적용되면 컴파일되고 거의 맞게 렌더되는 버그가
된다. `static_assert`로 막아두었다.

### color와 rect

`color`는 `vector4`와 같은 16바이트지만 `r/g/b/a` 의미를 가진 별도 타입이다.
값은 자동으로 sRGB 변환되지 않는 선형 float이며 `premultiply`, `adjust_saturation`,
`adjust_contrast`, `pack_rgba8`/`pack_bgra8`를 제공한다. 기본값은 SimpleMath와 같은
opaque black이다.

`rect`는 float `x/y/width/height`를 저장한다. 점 포함은 최소 변을 포함하고 최대 변은
제외하는 half-open 규칙이다. 변만 맞닿은 두 rect는 면적이 없으므로 교차하지 않는다.
음수 크기는 자동으로 숨기지 않고 `normalized(rect)`로 명시적으로 바로잡는다.

### bounding_frustum

```cpp
const math::bounding_frustum frustum =
    math::bounding_frustum_from_projection_lh(projection);

if (math::contains(frustum, camera_position) == math::containment::contains &&
    math::intersects(frustum, world_bounds)) {
    draw_visible_object();
}

const auto corners = frustum.corners();
const auto planes = math::frustum_planes(frustum); // 내부는 전부 <= 0
```

표현은 DirectXCollision의 `BoundingFrustum`과 같다: origin, orientation,
right/left/top/bottom slope, near/far 순서다. LH와 RH 생성은
`bounding_frustum_from_projection_lh/rh`로 분리했다. 특이 투영을 구분해야 하면
`try_bounding_frustum_from_projection_lh/rh`를 쓴다. 구는 면·모서리·꼭짓점까지,
AABB와 frustum 쌍은 face normal과 edge cross axis까지 검사하므로 6개 평면만 보는
보수적 컬링 테스트가 아니라 정밀 교차다.

DirectXTK SimpleMath는 별도 `BoundingFrustum` 래퍼를 정의하지 않고
DirectXCollision 타입을 사용하므로, 패리티 테스트의 비교 기준도
`DirectX::BoundingFrustum`이다. LH/RH 생성 필드와 코너, TRS 변환, 점·구·AABB·
frustum 포함/교차, raycast를 같은 입력으로 대조한다.

### aabb의 함정

`aabb`는 두 `vector3`를 갖는데 그것이 **중심과 반폭**이지 최소와 최대가 아니다.
둘은 모양이 같아서 잘못 넘겨도 컴파일된다.

```cpp
math::aabb a{math::vector3{0,0,0}, math::vector3{1,1,1}}; // [-1,1] 상자
math::aabb b = math::aabb::from_min_max(                   // [0,1] 상자 — 다르다
    math::vector3{0,0,0}, math::vector3{1,1,1});

const math::matrix4x4 world =
    math::translation_matrix(math::vector3{10, 0, 0});
const math::aabb world_bounds = math::transform(a, world);
```

`transform(aabb, matrix4x4)`는 affine 행렬의 이동·회전·비균등/음수 스케일·shear를
적용한 뒤 그 결과를 다시 축 정렬해 가장 작은 AABB를 반환한다. 중심은 점으로 변환하고
extents는 행렬 선형부의 절댓값으로 투영한다. 이는 8개 코너를 변환하는
`DirectX::BoundingBox::Transform`과 같은 결과지만 perspective 행렬은 계약 밖이다.
DirectX와 같은 uniform `scale, rotation, translation` 오버로드도 제공한다.

### C++20 range view와 C++23 mdspan

`<mathematics/views.hpp>`는 소유 타입을 바꾸지 않고 벡터 성분과 행렬 행을 range로
노출한다. 반환값은 원본을 참조하며 임시 객체에는 호출할 수 없다.

```cpp
math::vector4 color{1, 0.5f, 0.25f, 1};
for (float& component : math::components(color)) component *= 0.5f;

math::matrix4x4 world = math::matrix4x4::identity();
for (std::span<float, 4> row : math::rows(world)) {
    serialize(row);
}
```

`components`와 `rows`는 extent를 반환 타입에 보존한다.
`math::views::transform_fixed`는 이 extent를 다음 view로 전달하므로 일반 range-for와
고정 크기 종단 연산 양쪽에서 사용할 수 있다.

```cpp
auto squared =
    math::components(color) |
    math::views::transform_fixed([](float value) { return value * value; });

for (float value : squared) consume(value);

float sum = squared |
    math::ranges::fold_fixed(0.0f, std::plus<>{});

math::components(color) |
    math::ranges::for_each_fixed(
        [](float& value) { value *= 0.5f; });

std::array<float, 4> output;
math::components(color) |
    math::ranges::transform_fixed_to(
        output.begin(), [](float value) { return value * value; });
```

`fold_fixed`, `for_each_fixed`, `transform_fixed_to`는 원소 접근을 extent만큼 컴파일
타임에 전개한다. 함수 호출 형태인 `fold_fixed(range, initial, operation)`도 그대로
지원한다. 파이프 closure 자체는 어셈블리에서 직접 호출과 동일하게 제거된다.

두 view는 튜플 프로토콜도 만족하므로 루프 없이 이름으로 받을 수 있다.

```cpp
auto&& [x, y, z, w] = math::components(color);
auto&& [row0, row1, row2, row3] = math::rows(world);
```

바인딩은 원본을 참조하므로 쓰기가 그대로 관통하고 상수평가에서도 동작한다. C++23의
`tuple-like` 개념은 표준 튜플 타입만 인정하므로 `std::apply`에는 넣을 수 없다 —
일반 코드에서는 `math::ranges::get<I>(view)`를 쓴다.

**뜨거운 루프 안에서는 range-for를 피하고 `fold_fixed` 계열을 쓴다.** MSVC는 다른
루프 안에 중첩된 반복자 구동 루프를 전개하지 않는다. 이는 `std::views`의 문제가
아니라 반복자 루프 일반의 문제여서 `std::span`이나 생 포인터로 바꿔도 같다. 남은
내부 루프는 바깥 루프의 전개까지 막으므로, 배열을 도는 형태에서 성분 합은 40%,
행 합은 64% 느려진다([BASELINE §9](BASELINE.md)). `fold_fixed`는 같은 자리에서 손으로
쓴 코드와 명령어까지 같다. 단일 객체를 한 번 훑는 자리라면 range-for도 전개되므로
차이가 없다. clang은 어느 형태에서도 차이가 없다.

callback 호출 순서와 부작용을 보존해야 하므로 fixed terminal도 항상 SIMD가 되는
것은 아니다. SIMD가 중요한 전용 산술은 기존 벡터 연산을 우선한다.

C++23 표준 라이브러리에 `std::mdspan`이 있으면 `<mathematics/mdspan.hpp>`가 고정
extent 2차원 view를 제공한다. `as_mdspan`은 `m[row][column]`과 같은 매핑이고
`transpose_view`는 복사 없이 두 인덱스만 뒤집는다.

```cpp
#if MATHEMATICS_HAS_MDSPAN
auto view = math::as_mdspan(world);
view[2, 1] = 3.0f;                 // world.m[2][1]

auto transposed = math::transpose_view(world);
transposed[1, 2] = 4.0f;           // world.m[2][1]
#endif
```

이 view들은 수명도 늘리지 않고 데이터를 소유하지도 않는다. 행렬 연산 자체는 기존
`matrix4x4`/SIMD API를 사용하고 view는 검사·직렬화·범용 알고리즘 연결에 사용한다.

---

## 4. 퇴화 입력의 규약

라이브러리 전체가 하나의 정책을 따른다: **NaN을 퍼뜨리는 대신 쓸 수 있는 값을
반환한다.** NaN은 원인에서 한참 떨어진 곳에서 발견되기 때문이다.

| 입력 | 결과 |
|------|------|
| `normalize(0 벡터)` | 0 벡터 |
| `normalize(무한 벡터)` | NaN (DXMath와 동일) |
| `inverse(특이 행렬)` | 항등행렬 |
| `inverse(비유한 성분 행렬)` | 항등행렬 |
| `normalize(0 쿼터니언)` | 항등 쿼터니언 |
| `quaternion_from_axis_angle(0 축, θ)` | 항등 쿼터니언 |
| `decompose(0 스케일 행렬)` | `false` 반환, 출력 미변경 |
| `look_at_lh(up ∥ forward)` | 항등행렬 |
| `plane_from_point_normal(p, 0)` | 기본 XY 평면 |
| 기본 `aabb{}` | 빈 상자. `merge`의 항등원이며 어떤 점과도 교차하지 않음 |
| `sin/cos(\|x\| ≥ 8.2e5)` | NaN — 그 범위 밖은 답이 무의미하다 |

특이 여부가 중요하면 `inverse`가 항등을 돌려줬는지 보지 말고 `determinant`를
직접 물어라. 특이점 **근처**에서는 SIMD 경로와 스칼라 경로의 판정이 갈릴 수 있다
(`matrix4x4.hpp`의 주석 참조).

---

## 5. 기하 질의

```cpp
intersects(a, b)  -> bool          겹치는가
contains(a, b)    -> containment   b가 a 안에 있는가 (비대칭!)
classify(a, p)    -> plane_side     평면 p의 어느 쪽인가
raycast(r, a, d)  -> bool + float  맞는가, 얼마나 멀리
```

**`contains`는 비대칭이다.** `contains(box, sphere)`는 "상자가 구를 담는가"를
묻는다. 큰 구가 상자를 삼켜도 답은 `contains`가 아니라 `intersects`다.

**접촉은 교차로 센다.** 정확히 맞닿은 두 구는 `intersects`다. 다르게 하면
접촉 순간에 물체가 깜빡이며 떨어진다.

**레이는 반직선이다.** 원점 뒤의 적중은 적중이 아니다. 원점이 볼륨 안이면
거리 0으로 적중한다 — 이 한 가지만 DirectXMath와 다른데, DirectXMath가
자기 원시형끼리 어긋나서 맞출 수가 없다 (`ray.hpp` 참조).

---

## 6. 성능을 위해 알아둘 것

- **핫 루프는 `vec_reg`로.** `vector3`는 12바이트 패킹이라 함수 경계를 넘을 때
  적재/저장이 생긴다. 인라인되는 표현식 안에서는 문제없지만 인라인되지 않는
  경계를 넘나든다면 `mathematics/vec_reg.hpp`의 레지스터 타입을 직접 쓴다.
- **C++23으로 빌드하라.** MSVC에서 C++20으로 빌드하면 최대 2배 느려진다 —
  `if consteval`이 없으면 컴파일 타임 분기가 런타임 표현을 오염시킨다.
  빌드 시 경고가 나온다.
- **`/fp:fast`는 벤치용, `/fp:precise`는 테스트용.** 프로젝트가 그렇게 나눠
  쓴다. `normalize_unchecked`는 정확한 제곱근을 유지하고 입력 검사만 생략한다.
  `_est` 접미사 함수(`normalize_est` 등)는 정밀도를 속도와 맞바꾼다.
- **view는 연결 API다.** `components`와 `rows`는 generic range 코드에 편리하지만
  행렬 핫 루프에서는 직접 멤버 접근과 기존 SIMD 연산을 쓴다. `as_mdspan`과
  `transpose_view`는 원소 접근이 직접 접근과 같은 코드가 되는지 `spike`의 어셈블리
  프로브로 확인한다. 포인터/count AABB 경로도 범위 오버로드와 독립 루프를 유지한다.

측정치는 [BASELINE.md](BASELINE.md)에 있다.

---

## 7. DirectXMath 이주표

| DirectXMath | Mathematics |
|-------------|-------|
| `XMVECTOR` | `vec_reg` (핫 루프) / `vector4` (저장) |
| `XMFLOAT2/3/4` | `vector2/3/4` |
| `XMMATRIX` / `XMFLOAT4X4` | `matrix4x4` |
| `XMVectorAdd/Subtract/multiply` | `+` `-` `*` (성분별) |
| `XMVectorMultiplyAdd` | `mul_add` |
| `XMVector3Dot` / `XMVector4Dot` | `dot` (인자 타입이 결정) |
| `XMVector3Cross` | `cross` |
| `XMVector3Length` / `length_sq` | `length` / `length_sq` |
| `XMVector3Normalize` | `normalize` |
| `XMVector3NormalizeEst` | `normalize_est` |
| `XMVectorLerp` | `lerp` |
| `XMVectorClamp` / `saturate` | `clamp` / `saturate` |
| `XMMatrixMultiply(a, b)` | `a * b` (순서 동일) |
| `XMMatrixTranspose` | `transpose` |
| `XMMatrixInverse(&det, m)` | `inverse(m)` + `determinant(m)` |
| `XMMatrixIdentity` | `matrix4x4::identity()` |
| `XMMatrixScaling/translation` | `scaling_matrix` / `translation_matrix` |
| `XMMatrixRotationX/Y/Z` | `rotation_x/y/z` |
| `XMMatrixRotationQuaternion` | `rotation_matrix(q)` |
| `XMMatrixAffineTransformation` | `compose(scale, rot, translation)` |
| `XMMatrixDecompose` | `decompose` |
| `XMMatrixLookAtLH/RH` | `look_at_lh/rh` (동일) |
| `XMMatrixPerspectiveFovLH/RH` | `perspective_fov_lh/rh` (동일) |
| `XMMatrixOrthographicLH/RH` | `orthographic_lh/rh` (동일) |
| `XMVector3Transform` | `transform_direction` 또는 `v * m` |
| `XMVector3TransformCoord` | `transform_point` |
| `XMVector3TransformNormal` | `transform_direction` |
| `XMQuaternionIdentity` | `quaternion::identity()` |
| `XMQuaternionMultiply(a, b)` | `a * b` (**순서 동일**) |
| `XMQuaternionRotationAxis` | `quaternion_from_axis_angle` |
| `XMQuaternionRotationRollPitchYaw(p,y,r)` | `quaternion_from_pitch_yaw_roll(p,y,r)` |
| `XMQuaternionSlerp` / `normalize` | `slerp` / `normalize` |
| `XMQuaternionConjugate` / `inverse` | `conjugate` / `inverse` |
| `XMVector3Rotate` | `rotate(v, q)` |
| `SimpleMath::Color` | `color` |
| `SimpleMath::Rectangle` | `rect` (float 크기, 플랫폼 `RECT` 의존 없음) |
| `XMPlaneFromPointNormal` / `from_points` | `plane_from_point_normal` / `plane_from_points` |
| `XMPlaneDotCoord` | `signed_distance` |
| `XMPlaneDotNormal` | `dot_normal` |
| `XMPlaneNormalize` | `normalize(plane)` |
| `XMScalarSinCos` | `sin_cos` |
| `BoundingSphere` / `BoundingBox` | `sphere` / `aabb` |
| `BoundingBox::Transform` | `transform(aabb, matrix)` / `transform(aabb, scale, rotation, translation)` |
| `BoundingFrustum` | `bounding_frustum` |
| `BoundingFrustum::CreateFromMatrix` | `bounding_frustum_from_projection_lh/rh` |
| `BoundingFrustum::GetCorners/GetPlanes` | `corners()` / `frustum_planes` |
| `BoundingFrustum::Transform` | `transform(frustum, ...)` |
| `ContainmentType` | `containment` |
| `PlaneIntersectionType` | `plane_side` (`INTERSECTING` → `straddling`) |
| `sphere.intersects(o, d, dist)` | `raycast(ray, sphere, dist)` |

### 이주할 때 주의할 차이

1. **`_est` 이외의 근사 없음.** DXMath의 일부 함수는 기본이 근사인데,
   여기서는 정확한 쪽이 기본이고 근사는 접미사로 명시된다.
2. **레이 안쪽 시작.** 위 §5 참조 — 유일한 의도적 divergence다.
3. **`sin_cos` 정확도.** DXMath보다 27% 느린 대신 큰 각도에서 정확도가
   20배 이상 좋다 (2.7e-07 대 5e-06).
4. **NaN 정책.** DXMath가 QNaN을 돌려주는 자리에서 이 라이브러리는 항등원 같은
   쓸 수 있는 값을 돌려준다. §4 표 참조.
5. **`constexpr`.** 전 API가 컴파일 타임에 동작한다. 룩업 테이블을 런타임에
   만들 이유가 없어진다.
