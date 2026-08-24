# Mathf 사용 가이드

헤더 온리다. `include/`를 인클루드 경로에 넣거나 CMake에서 `mathf` INTERFACE
타깃에 링크하면 끝이다.

```cmake
add_subdirectory(path/to/mathf)
target_link_libraries(my_game PRIVATE mathf)
```

```cpp
#include <mathf/mathf.hpp>   // 전부
// 또는 필요한 것만
#include <mathf/vector.hpp>
#include <mathf/transform.hpp>
```

`mathf/format.hpp`만 우산 헤더에서 빠져 있다. `<format>`은 무겁고, 출력하지 않는
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
| 손잡이 | 함수 이름에 **항상** 있다. `LookAtLH` / `LookAtRH`, 기본값 없음 |
| 깊이 범위 | Direct3D식 `[0, 1]` (OpenGL의 `[-1, 1]`이 아니다) |
| 각도 | 라디안. `Radians()` / `Degrees()`로 변환 |

### 쿼터니언 곱 순서가 반대인 이유

교과서는 "a 다음 b"를 `b · a`로 쓴다. 이 라이브러리는 `a * b`로 쓴다.
그래야 모든 계층이 뒤집기 없이 맞아떨어지기 때문이다:

```cpp
Rotate(v, a * b) == Rotate(Rotate(v, a), b);              // 좌→우
RotationMatrix(a * b) == RotationMatrix(a) * RotationMatrix(b);
v * (M1 * M2) == (v * M1) * M2;
```

교과서 순서를 택했다면 쿼터니언에서 행렬로 넘어갈 때마다 순서를 뒤집어야 한다.

---

## 2. 5분 예제

```cpp
#include <mathf/mathf.hpp>
using namespace mathf;

// 물체의 월드 행렬 — 크기, 회전, 이동
const Quaternion spin = QuaternionFromAxisAngle(Vector3{0, 1, 0}, Radians(30.0f));
const Matrix4x4 world = Compose(Vector3{2, 2, 2}, spin, Vector3{10, 0, 5});

// 카메라
const Matrix4x4 view = LookAtLH(Vector3{0, 5, -10},   // 눈
                                Vector3{0, 0, 0},      // 대상
                                Vector3{0, 1, 0});     // 위
const Matrix4x4 proj = PerspectiveFovLH(Radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

// 좌→우로 읽는다: 모델 → 뷰 → 투영
const Matrix4x4 mvp = world * view * proj;

// 정점 하나를 클립 공간으로
const Vector4 clip = Vector4{1, 0, 0, 1} * mvp;
const Vector3 ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
```

### 방향과 위치는 다른 함수다

```cpp
const Vector3 worldPos    = TransformPoint(localPos, world);      // w = 1, 이동 적용
const Vector3 worldNormal = TransformDirection(localNormal, world); // w = 0, 이동 무시
```

법선에 `TransformPoint`를 쓰는 것이 고전적인 버그라서 이름을 나눴다.
(비균등 스케일이 있으면 법선은 역전치로 변환해야 한다 —
`Transpose(Inverse(world))`.)

### 컴파일 타임 계산

전 API가 `constexpr` 문맥에서 동작한다. DirectXMath가 못 하는 지점이다.

```cpp
constexpr Matrix4x4 kProj = PerspectiveFovLH(kHalfPi, 1.0f, 1.0f, 100.0f);
constexpr Quaternion kTurn = QuaternionFromAxisAngle(Vector3{0, 0, 1}, kHalfPi);
constexpr float kSin = Sin(0.5f);
static_assert(Inverse(kIdentity) == kIdentity);
```

---

## 3. 타입

| 타입 | 크기 | 비고 |
|------|------|------|
| `Vector2/3/4` | 8 / 12 / 16 B | 패킹. 정점 버퍼에 그대로 들어간다 |
| `Quaternion` | 16 B | `(x,y,z,w)`. **`VectorLike`가 아니다** |
| `Matrix3x3` | 36 B | 회전·크기. 이동 없음 |
| `Matrix4x4` | 64 B | 상수 버퍼에 그대로 업로드 가능 |
| `Plane` | 16 B | `(a,b,c,d)`, `d = -dot(n, p)` |
| `Sphere` | 16 B | 중심 + 반지름 |
| `AABB` | 24 B | 중심 + **extents**(반폭) |
| `Ray` | 24 B | 원점 + 방향, **반직선** |
| `VecReg` | 16 B | 레지스터 타입. 핫 루프에서 직접 |

`Quaternion`이 `VectorLike`가 아닌 것은 의도다. 벡터의 `*`는 HLSL을 따라
성분별 곱인데, 쿼터니언에 그게 적용되면 컴파일되고 거의 맞게 렌더되는 버그가
된다. `static_assert`로 막아두었다.

### AABB의 함정

`AABB`는 두 `Vector3`를 갖는데 그것이 **중심과 반폭**이지 최소와 최대가 아니다.
둘은 모양이 같아서 잘못 넘겨도 컴파일된다.

```cpp
AABB a{Vector3{0,0,0}, Vector3{1,1,1}};                  // [-1,1] 상자
AABB b = AABB::FromMinMax(Vector3{0,0,0}, Vector3{1,1,1}); // [0,1] 상자 — 다르다
```

---

## 4. 퇴화 입력의 규약

라이브러리 전체가 하나의 정책을 따른다: **NaN을 퍼뜨리는 대신 쓸 수 있는 값을
반환한다.** NaN은 원인에서 한참 떨어진 곳에서 발견되기 때문이다.

| 입력 | 결과 |
|------|------|
| `Normalize(0 벡터)` | 0 벡터 |
| `Normalize(무한 벡터)` | NaN (DXMath와 동일) |
| `Inverse(특이 행렬)` | 항등행렬 |
| `Inverse(비유한 성분 행렬)` | 항등행렬 |
| `Normalize(0 쿼터니언)` | 항등 쿼터니언 |
| `QuaternionFromAxisAngle(0 축, θ)` | 항등 쿼터니언 |
| `Decompose(0 스케일 행렬)` | `false` 반환, 출력 미변경 |
| `LookAtLH(up ∥ forward)` | 항등행렬 |
| `PlaneFromPointNormal(p, 0)` | 기본 XY 평면 |
| `Sin/Cos(\|x\| ≥ 8.2e5)` | NaN — 그 범위 밖은 답이 무의미하다 |

특이 여부가 중요하면 `Inverse`가 항등을 돌려줬는지 보지 말고 `Determinant`를
직접 물어라. 특이점 **근처**에서는 SIMD 경로와 스칼라 경로의 판정이 갈릴 수 있다
(`matrix4x4.hpp`의 주석 참조).

---

## 5. 기하 질의

```cpp
Intersects(a, b)  -> bool          겹치는가
Contains(a, b)    -> Containment   b가 a 안에 있는가 (비대칭!)
Classify(a, p)    -> PlaneSide     평면 p의 어느 쪽인가
Raycast(r, a, d)  -> bool + float  맞는가, 얼마나 멀리
```

**`Contains`는 비대칭이다.** `Contains(box, sphere)`는 "상자가 구를 담는가"를
묻는다. 큰 구가 상자를 삼켜도 답은 `Contains`가 아니라 `Intersects`다.

**접촉은 교차로 센다.** 정확히 맞닿은 두 구는 `Intersects`다. 다르게 하면
접촉 순간에 물체가 깜빡이며 떨어진다.

**레이는 반직선이다.** 원점 뒤의 적중은 적중이 아니다. 원점이 볼륨 안이면
거리 0으로 적중한다 — 이 한 가지만 DirectXMath와 다른데, DirectXMath가
자기 원시형끼리 어긋나서 맞출 수가 없다 (`ray.hpp` 참조).

---

## 6. 성능을 위해 알아둘 것

- **핫 루프는 `VecReg`로.** `Vector3`는 12바이트 패킹이라 함수 경계를 넘을 때
  적재/저장이 생긴다. 인라인되는 표현식 안에서는 문제없지만, 인라인되지 않는
  경계를 넘나든다면 `mathf/vec_reg.hpp`의 레지스터 타입을 직접 쓴다.
- **C++23으로 빌드하라.** MSVC에서 C++20으로 빌드하면 최대 2배 느려진다 —
  `if consteval`이 없으면 컴파일 타임 분기가 런타임 표현을 오염시킨다.
  빌드 시 경고가 나온다.
- **`/fp:fast`는 벤치용, `/fp:precise`는 테스트용.** 프로젝트가 그렇게 나눠
  쓴다. `Est` 접미사 함수(`NormalizeEst` 등)는 정밀도를 속도와 맞바꾼다.

측정치는 [BASELINE.md](BASELINE.md)에 있다.

---

## 7. DirectXMath 이주표

| DirectXMath | Mathf |
|-------------|-------|
| `XMVECTOR` | `VecReg` (핫 루프) / `Vector4` (저장) |
| `XMFLOAT2/3/4` | `Vector2/3/4` |
| `XMMATRIX` / `XMFLOAT4X4` | `Matrix4x4` |
| `XMVectorAdd/Subtract/Multiply` | `+` `-` `*` (성분별) |
| `XMVectorMultiplyAdd` | `MulAdd` |
| `XMVector3Dot` / `XMVector4Dot` | `Dot` (인자 타입이 결정) |
| `XMVector3Cross` | `Cross` |
| `XMVector3Length` / `LengthSq` | `Length` / `LengthSq` |
| `XMVector3Normalize` | `Normalize` |
| `XMVector3NormalizeEst` | `NormalizeEst` |
| `XMVectorLerp` | `Lerp` |
| `XMVectorClamp` / `Saturate` | `Clamp` / `Saturate` |
| `XMMatrixMultiply(a, b)` | `a * b` (순서 동일) |
| `XMMatrixTranspose` | `Transpose` |
| `XMMatrixInverse(&det, m)` | `Inverse(m)` + `Determinant(m)` |
| `XMMatrixIdentity` | `Matrix4x4::Identity()` |
| `XMMatrixScaling/Translation` | `ScalingMatrix` / `TranslationMatrix` |
| `XMMatrixRotationX/Y/Z` | `RotationX/Y/Z` |
| `XMMatrixRotationQuaternion` | `RotationMatrix(q)` |
| `XMMatrixAffineTransformation` | `Compose(scale, rot, translation)` |
| `XMMatrixDecompose` | `Decompose` |
| `XMMatrixLookAtLH/RH` | `LookAtLH/RH` (동일) |
| `XMMatrixPerspectiveFovLH/RH` | `PerspectiveFovLH/RH` (동일) |
| `XMMatrixOrthographicLH/RH` | `OrthographicLH/RH` (동일) |
| `XMVector3Transform` | `TransformDirection` 또는 `v * M` |
| `XMVector3TransformCoord` | `TransformPoint` |
| `XMVector3TransformNormal` | `TransformDirection` |
| `XMQuaternionIdentity` | `Quaternion::Identity()` |
| `XMQuaternionMultiply(a, b)` | `a * b` (**순서 동일**) |
| `XMQuaternionRotationAxis` | `QuaternionFromAxisAngle` |
| `XMQuaternionRotationRollPitchYaw(p,y,r)` | `QuaternionFromPitchYawRoll(p,y,r)` |
| `XMQuaternionSlerp` / `Normalize` | `Slerp` / `Normalize` |
| `XMQuaternionConjugate` / `Inverse` | `Conjugate` / `Inverse` |
| `XMVector3Rotate` | `Rotate(v, q)` |
| `XMPlaneFromPointNormal` / `FromPoints` | `PlaneFromPointNormal` / `PlaneFromPoints` |
| `XMPlaneDotCoord` | `SignedDistance` |
| `XMPlaneDotNormal` | `DotNormal` |
| `XMPlaneNormalize` | `Normalize(plane)` |
| `XMScalarSinCos` | `SinCos` |
| `BoundingSphere` / `BoundingBox` | `Sphere` / `AABB` |
| `ContainmentType` | `Containment` |
| `PlaneIntersectionType` | `PlaneSide` (`INTERSECTING` → `Straddling`) |
| `sphere.Intersects(o, d, dist)` | `Raycast(ray, sphere, dist)` |

### 이주할 때 주의할 차이

1. **`Est` 이외의 근사 없음.** DXMath의 일부 함수는 기본이 근사인데,
   여기서는 정확한 쪽이 기본이고 근사는 접미사로 명시된다.
2. **레이 안쪽 시작.** 위 §5 참조 — 유일한 의도적 divergence다.
3. **`SinCos` 정확도.** DXMath보다 27% 느린 대신 큰 각도에서 정확도가
   20배 이상 좋다 (2.7e-07 대 5e-06).
4. **NaN 정책.** DXMath가 QNaN을 돌려주는 자리에서 이 라이브러리는 항등원 같은
   쓸 수 있는 값을 돌려준다. §4 표 참조.
5. **`constexpr`.** 전 API가 컴파일 타임에 동작한다. 룩업 테이블을 런타임에
   만들 이유가 없어진다.
