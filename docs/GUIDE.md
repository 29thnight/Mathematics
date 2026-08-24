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

`mathematics/format.hpp`만 우산 헤더에서 빠져 있다. `<format>`은 무겁고, 출력하지 않는
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
| `plane` | 16 B | `(a,b,c,d)`, `d = -dot(n, p)` |
| `sphere` | 16 B | 중심 + 반지름 |
| `aabb` | 24 B | 중심 + **extents**(반폭) |
| `ray` | 24 B | 원점 + 방향, **반직선** |
| `vec_reg` | 16 B | 레지스터 타입. 핫 루프에서 직접 |

`quaternion`이 `vector_like`가 아닌 것은 의도다. 벡터의 `*`는 HLSL을 따라
성분별 곱인데, 쿼터니언에 그게 적용되면 컴파일되고 거의 맞게 렌더되는 버그가
된다. `static_assert`로 막아두었다.

### aabb의 함정

`aabb`는 두 `vector3`를 갖는데 그것이 **중심과 반폭**이지 최소와 최대가 아니다.
둘은 모양이 같아서 잘못 넘겨도 컴파일된다.

```cpp
math::aabb a{math::vector3{0,0,0}, math::vector3{1,1,1}}; // [-1,1] 상자
math::aabb b = math::aabb::from_min_max(                   // [0,1] 상자 — 다르다
    math::vector3{0,0,0}, math::vector3{1,1,1});
```

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
  적재/저장이 생긴다. 인라인되는 표현식 안에서는 문제없지만, 인라인되지 않는
  경계를 넘나든다면 `mathematics/vec_reg.hpp`의 레지스터 타입을 직접 쓴다.
- **C++23으로 빌드하라.** MSVC에서 C++20으로 빌드하면 최대 2배 느려진다 —
  `if consteval`이 없으면 컴파일 타임 분기가 런타임 표현을 오염시킨다.
  빌드 시 경고가 나온다.
- **`/fp:fast`는 벤치용, `/fp:precise`는 테스트용.** 프로젝트가 그렇게 나눠
  쓴다. `_est` 접미사 함수(`normalize_est` 등)는 정밀도를 속도와 맞바꾼다.

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
| `XMPlaneFromPointNormal` / `from_points` | `plane_from_point_normal` / `plane_from_points` |
| `XMPlaneDotCoord` | `signed_distance` |
| `XMPlaneDotNormal` | `dot_normal` |
| `XMPlaneNormalize` | `normalize(plane)` |
| `XMScalarSinCos` | `sin_cos` |
| `BoundingSphere` / `BoundingBox` | `sphere` / `aabb` |
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
