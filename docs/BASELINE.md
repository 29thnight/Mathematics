# 성능 기준선 — Mathematics / DirectXMath / GLM / Sony Vectormath

측정일: 2026-08-24 (Phase 1)
CPU: Intel Core i7-8700K @ 3.70GHz (6C/12T, Coffee Lake)
컴파일러: MSVC 14.51.36231 / **C++23** / `/O2 /arch:AVX2 /fp:fast`
설정: `--benchmark_min_time=0.6s --benchmark_repetitions=7`

비교 대상 버전: DirectXMath (Windows SDK 10.0.26100) / GLM 1.0.1
(`GLM_FORCE_INTRINSICS`, `GLM_FORCE_ALIGNED_GENTYPES`) /
Sony Vectormath — [glampert 포크](https://github.com/glampert/vectormath) `7105ef34`

> **표준 표기는 필수 정보다.** MSVC에서 C++20으로 빌드하면 아래 수치의 약 2배까지
> 느려진다 — `if consteval`이 없으면 컴파일 타임 분기가 런타임 표현을 오염시킨다
> ([SPIKE-RESULTS.md §6](SPIKE-RESULTS.md)). Clang은 두 표준 모두 동일하다.

---

## 1. 지연 (latency) — 직렬 의존 사슬

**게이트 대상**: Mathematics는 DirectXMath 대비 ±5% 이내여야 한다.

| 연산 | Mathematics | DirectXMath | GLM | Vectormath |
|------|-------|-------------|-----|------------|
| add | 2.26 ns | 2.25 ns | 2.24 ns | 2.24 ns |
| mul_add | 2.25 ns | 2.25 ns | 2.25 ns | 2.31 ns |
| sqrt | 4.28 ns | 4.17 ns | — | — |
| dot3 | 4.41 ns | 4.42 ns | — | — |
| dot4 | 4.42 ns | 4.40 ns | — | — |
| **dot4 → 스칼라** | 4.43 ns | 4.41 ns | **4.16 ns** | **5.97 ns** |

측정 변동(cv)은 대부분 1% 미만. **Mathematics는 전 항목에서 DXMath 대비 ±3% 이내로 PASS.**

### 왜 내적만 두 줄인가
API가 다르기 때문이다. Mathematics와 DXMath의 `dot`은 결과를 **전 레인에 splat**해서
반환하고, GLM은 **스칼라 float**을, Vectormath는 `FloatInVec`을 반환한다. 벡터 사슬로
비교하면 GLM에만 브로드캐스트 비용이 붙어 불공정하다.

그래서 **"내적을 계산해 스칼라로 쓴다"** 는 실제 사용 형태를 별도로 측정했다 —
네 라이브러리 모두 `float 브로드캐스트 → 내적 → 한 레인 읽기`로 형태가 동일하다.
여기서 **GLM이 가장 빠른데(4.16ns), 스칼라 반환이 네이티브라 추출 단계가 없기 때문**이다.
반대로 Vectormath는 5.97ns로 가장 느리다.

> 즉 splat 반환은 벡터 연산을 이어갈 때 유리하고, 스칼라로 쓸 때는 불리하다.
> Mathematics가 DXMath 관례를 따르므로 두 성질을 그대로 물려받는다.

---

## 2. 처리량 (throughput) — 512개 배치, 정렬 적재/저장

| 구현 | 시간 | 처리율 | cv |
|------|------|--------|-----|
| **Mathematics** | 537 ns | **976 M items/s** | 1.2% |
| Sony Vectormath | 537 ns | 982 M/s | 0.6% |
| DirectXMath | 539 ns | 968 M/s | 0.6% |
| GLM | 908 ns | 586 M/s | 1.5% |

Mathematics·Vectormath·DXMath는 서로 잡음 범위 안에서 동일하다.
**GLM만 약 40% 느린데**, `GLM_FORCE_INTRINSICS`를 켜도 `a * b + c`가 FMA 하나로
합쳐지지 않고 곱셈과 덧셈으로 남기 때문이다.

### 이 측정은 두 번 고쳐졌다 (기록)
초기 버전은 신뢰할 수 없었고, 두 가지가 문제였다.

**(1) 연산이 아니라 벡터 구성 비용을 재고 있었다.** 매 반복마다 스칼라 4개로 벡터를
만들고 다시 스칼라 4개로 풀어 저장했다. Phase 1에서 `load`/`store`가 생긴 뒤 정렬
적재/저장으로 바꾸자 전 라이브러리가 약 2배 빨라졌다 (Mathematics 308 → 729 M/s).
DXMath만 `XMStoreFloat4`를 쓰고 Mathematics는 `lane()` 4회를 쓰던 비대칭도 함께 해소됐다.

**(2) 배치가 커서 메모리 대역폭에 묶여 있었다.** 4096개 × 64바이트 = 256KiB로 L2를
넘겨, 네 라이브러리가 모두 대역폭에 수렴하고 **변동이 11~17%** 에 달했다. L1에 들어가는
512개(32KiB)로 줄이자 **변동이 1% 수준으로** 떨어지고 라이브러리 간 차이가 드러났다.

> **결론: 처리량도 이제 게이트에 쓸 수 있다.** 이전 버전 문서에서 "처리량은 잡음이 커서
> 게이트에서 제외"라고 한 판단은 위 두 결함에서 비롯된 것이었고, 결함을 고치니 근거가
> 사라졌다.

---

## 3. vector3 — 사용자 대면 타입 (Phase 2)

`vector3`는 12바이트 패킹 타입이다. 아래 두 측정이 Phase 2의 설계를 검증한다.

### 연쇄 표현식 `a * b + c` (지연)

| 구현 | 지연 | 비고 |
|------|------|------|
| **Mathematics `vector3`** | **2.25 ns** | 12바이트 패킹 |
| DXMath `XMVECTOR` | 2.28 ns | 레지스터 상주 — 상한 |
| GLM `vec3` | 3.21 ns | 스칼라 |
| DXMath `XMFLOAT3` | 5.59 ns | 매 단계 load/store |

**패킹 타입이 레지스터 타입과 동일한 성능을 낸다.** 3성분 산술을 스칼라로 두면
성분들이 독립적인 의존 사슬로 파이프라이닝되어, SIMD 1회와 같은 지연이 나온다.
(초기 SIMD 승격 방식은 5.25ns로 `XMFLOAT3` 수준이었다 — PLAN.md Phase 2 참조.)

### normalize 처리량 (512개 스트림)

| 구현 | 처리율 | 퇴화 입력 보장 |
|------|--------|----------------|
| GLM | 465 M/s | **없음** — 플래그에 따라 달라짐 |
| **Mathematics** | **342 M/s** | 있음 (0 → 0, ∞ → NaN) |
| DirectXMath | 259 M/s | 있음 |

Mathematics는 DXMath보다 **32% 빠르다**. GLM이 더 빠른 것은 퇴화 케이스를 아예 다루지
않기 때문이며, **그 동작은 보장되지 않는다** — 실측 확인:

| GLM `normalize(vec3(0))` | 결과 |
|---|---|
| `/fp:fast` | `(0, 0, 0)` |
| `/fp:precise` | `NaN` |

즉 GLM에서 0 벡터 정규화의 결과는 컴파일 플래그에 좌우된다. Mathematics와 DXMath는
플래그와 무관하게 0을 반환한다. 씬 그래프를 타고 번지는 NaN을 막는 값이 이 차이다.

---

## 4. matrix4x4 (Phase 3)

> **재측정 (2026-08-24, Phase 3 코드 리뷰).** 아래 표는 `--benchmark_min_time=1.0s
> --benchmark_repetitions=9`로 다시 잰 값이며 전 항목 cv < 1.2%다. 리뷰 중
> 초판 수치가 **재현되지 않는다**는 것이 드러나 교체했다 — 초판은 역행렬을
> Mathematics 56.5 / DXMath 67.2로 적었으나, 같은 커밋을 같은 기계에서 다시 재면
> 69.4 / 68.3이 나온다. DXMath 쪽은 거의 그대로인데 Mathematics 쪽만 크게 달라졌으므로
> 기계 상태 차이로는 설명되지 않는다. **원인은 Phase 4 재측정에서 밝혀졌다 —
> 4K 앨리어싱이고, §5에 기록했다.** 재현되지 않는 수치는 없는 것보다 나쁘므로
> 초판을 폐기하고 재현 절차와 함께 아래 값을 남긴다.

### 곱셈

| 구현 | 지연 | 처리량 (256개 배치) |
|------|------|---------------------|
| **Mathematics** | **5.51 ns** | 334 M/s |
| DirectXMath | 5.66 ns | 337 M/s |
| GLM | — | 249 M/s |

지연은 Mathematics가 근소하게 앞서고 처리량은 잡음 범위 안에서 동률이다 — 게이트(±5%)
통과. 여기까지 오는 데 **두 번의 수정**이 필요했다:

**(1) 셔플 포트 병목 (141 → 251 M/s).** 처음엔 행을 적재한 뒤 `splat_x/y/z/w`로
성분을 뽑았는데, 이는 곱셈 1회당 셔플 16개를 만든다. 셔플 포트는 하나뿐이라
FMA 16개(이론 최소)가 아니라 셔플이 병목이 된다. `load_splat`(메모리에서 직접
브로드캐스트)을 백엔드에 추가해 작업을 적재 포트로 옮겼다.

**(2) 알고리즘 격차 — 256비트 (251 → 334 M/s).** 그래도 DXMath에 못 미쳐
`DirectXMathMatrix.inl`을 읽어보니 `XMMatrixMultiply`가 **AVX2 256비트 경로**로
두 행을 한 번에 처리하고 있었다. 128비트 안에서 아무리 조율해도 연산량이 절반인
알고리즘은 따라잡을 수 없다. 같은 방식을 구현했다 — 라이브러리에서 128비트를
넘는 유일한 지점이다.

> **clang에서는 아직 뒤진다** (Mathematics 335 M/s 대 DXMath 489). 같은 알고리즘인데
> clang이 DXMath 쪽 루프를 더 잘 최적화한다. MSVC에서는 반대다(307). 원인 미규명.

### 역행렬 — 두 컴파일러 모두 통과

| 구현 | MSVC | clang |
|------|------|-------|
| **Mathematics** | **72.8 M/s** | **101.0 M/s** |
| DirectXMath | 68.4 M/s | 81.2 M/s |
| GLM | 45.4 M/s | 51.6 M/s |

MSVC에서 6% 앞서고 clang에서 24% 앞선다. (초판은 MSVC를 56.5로 적어 16% 열세라고
했으나, 위 상자에 적은 대로 재현되지 않았다.)

SIMD화 과정 — 상대 개선은 재측정 후에도 유효하다:

| 단계 | 비고 |
|------|------|
| 스칼라 라플라스 전개 (약 150회 연산) | 출발점 |
| + 벡터화 | +11% |
| + 전치를 레지스터에서 수행 (메모리 왕복 제거, 셔플 4개 절약) | +18% |
| + 역수를 벡터에 유지 (스칼라 왕복 제거) | +1% |

가장 큰 단일 개선은 **전치의 메모리 왕복 제거**였다. `transpose()`는 `matrix4x4`를
반환하므로 16개 float을 저장했다가 즉시 다시 적재한다. 전치를 레지스터 안에서
수행하되 행 순서를 (1,0,3,2)로 바꿔 수행하면, 수반행렬이 필요로 하는 열 순열이
전치와 함께 공짜로 나온다.

### 역행렬의 정확도와 한계 (리뷰에서 측정)

배정밀도 Gauss-Jordan(여인수 전개와 대수를 공유하지 않는 참조)을 기준으로 잰 값:

| | AVX2 | SSE2 | 스칼라 |
|---|---|---|---|
| inverse 대 배정밀도 참조 | 1.97e-05 | 3.21e-05 | 4.78e-05 |
| SIMD 대 스칼라 불일치 | 2.81e-05 | 2.78e-05 | 0 |

난수 196,722개(양호 조건) 기준. **AVX2에서는 SIMD 경로가 스칼라보다 정확한데**,
FMA가 중간 반올림을 없애기 때문이다.

**단, 특이점 근처에서는 두 경로가 일치하지 않으며 일치시킬 수 없다.** 반올림까지
특이한 행렬 200만 개에서 특이 여부 판정이 **23% 어긋났고**, 둘 다 가역으로 본
경우에도 결과가 최대 300배 달랐다. 두 경로에 같은 전개를 쓰게 해도 23% → 20%로
줄 뿐이라(FMA 축약과 합산 순서가 남는다) 고치지 않고 계약에 명시했다. 실제로
발생하는 특이 행렬(0인 행, 중복 행, 0 스케일)은 양쪽 모두 행렬식이 정확히 0이 되어
일치한다. 자세한 내용은 `matrix4x4.hpp`의 `inverse` 주석에 있다.

---

## 5. 쿼터니언과 변환 (Phase 4)

재측정 2026-08-24 (Phase 4 코드 리뷰). MSVC, `--benchmark_repetitions=9
--benchmark_enable_random_interleaving=true`, **중앙값**. 교차 실행은 필수다 —
이 기계는 측정 중 부하가 흔들려 같은 벤치가 실행 간 20%까지 움직였고, 교차하지
않으면 그 드리프트가 라이브러리 간 차이로 둔갑한다.

| 연산 | Mathematics | DirectXMath | GLM | 판정 |
|------|-------|-------------|-----|------|
| 쿼터니언 곱 (처리량) | 570.9 M/s | 556.1 M/s | — | 동률 |
| 쿼터니언 곱 (지연) | 5.24 ns | 4.51 ns※ | — | **16% 열세** |
| slerp | 37.2 M/s | 30.6 M/s | — | +22% |
| 벡터 회전 | 200.7 M/s | 177.2 M/s | — | +13% |
| 쿼터니언 → 행렬 | 222.4 M/s | 160.6 M/s | — | +39% |
| TRS 합성 | 188.9 M/s | 129.7 M/s | — | +46% |
| mat4 곱 (처리량) | 329.4 M/s | 303.4 M/s | 237.9 M/s | +9% |
| mat4 역행렬 | 74.5 M/s | 65.9 M/s | 42.8 M/s | +13% |

※ 패킹 형태 DXMath(`XMFLOAT4` 누산기) 기준. 레지스터 상주 `XMVECTOR`는 4.07ns다.
`quaternion`이 16바이트 패킹 타입이므로 전자가 공정한 비교다.

clang: 역행렬 118.6 대 81.2 (+46%), 곱셈 342.9 대 410.0 (**16% 열세**). 곱셈의
clang 열세가 측정 인공물이 아닌지 `volatile` 고정 탐침으로 따로 확인했고,
350.6 대 521.9로 방향이 재현됐다 — 실제 격차다.

### 곱셈: 스칼라에서 SIMD로

처음엔 네 성분을 스칼라 식으로 썼고 443 M/s로 DXMath의 596에 26% 뒤졌다.
**4레인은 레지스터 승격이 값을 하는 정확한 지점이다** — 2~3성분 벡터를 스칼라로
두기로 한 Phase 2의 결정과 같은 근거에서 반대 방향의 결론이 나온다.

남은 지연 격차는 규명하지 못했다. 어셈블리를 확인하면 곱 1회당 **10개 명령**
(`vpermilps` 3, `vmulps` 4, `vfmadd231ps` 3), 스필 없음, 부호 상수와 b의 splat이
모두 루프 밖으로 호이스팅, 누산기는 xmm 상주로 적재/저장 왕복이 없다 — DXMath와
동형이다. 누산을 직렬 사슬에서 트리로 바꿔봤으나 5.53 → 5.41ns로 잡음 범위였다.

### 이 재측정에서 드러난 측정 결함 두 가지

**(1) 4K 앨리어싱이 역행렬 수치를 프로세스마다 갈랐다.** 같은 바이너리가
47 / 54 / 74 M/s를 각각 내부적으로는 일관되게 냈다. 저장 주소의 하위 12비트가
곧 이어질 적재와 겹치면 그 적재가 거짓으로 지연되는데, 입력 배열과 출력 배열이
그렇게 충돌하는지를 ASLR이 프로세스마다 다르게 결정하고 있었다. 상대 오프셋을
직접 통제한 탐침이 원인을 특정했다:

| 출력 대 입력 상대 오프셋 | 처리율 |
|---|---|
| 64 바이트 | 48.6 M/s |
| 16 바이트 | 58.1 M/s |
| 128 바이트 이상 | 79 M/s (평탄) |

역행렬 벤치는 이제 두 배열을 한 아레나에 반 페이지 간격으로 배치한다.
**Phase 3이 "재현되지 않는다"고 기록한 혼란(§4 상자)의 정체가 이것이었다.**

**(2) clang-cl은 DirectXMath의 slerp 루프를 통째로 제거한다.** 하네스에서
128회 slerp가 0.245ns, 523 G/s로 나왔다 — 확인하지 않으면 승리로 읽히는 종류의
수치다. `DoNotOptimize(out.data())`, 원소 단위 탈출, 반복 시작의 메모리 배리어,
`volatile`로 고정한 보간 계수까지 모두 넣었지만 clang-cl은 여전히 제거한다.
언어가 보장하는 `volatile`만 쓴 별도 탐침에서도 같았고, 같은 탐침을 MSVC로
빌드하면 31.5 대 30.0으로 정상이다. 따라서 **clang에서 slerp는 DXMath와 비교할
수 없다**. Mathematics 쪽은 두 컴파일러 모두에서 정상 측정된다.

> 배치 벤치 전부에 반복 시작 시점의 메모리 배리어를 넣었다. 입력이 바깥 루프에
> 대해 불변이면 컴파일러는 배치 전체를 루프 밖으로 끌어낼 권리가 있고, 그러면
> 남는 것은 저장뿐이다.

### 삼각함수 — 의도한 교환

| 구현 | 처리량 | 정확도 (절대 오차) |
|------|--------|--------------------|
| DirectXMath `XMScalarSinCos` | 251.9 M/s | 큰 각도에서 5e-06대 |
| **Mathematics `sin_cos`** | **183.5 M/s** | **2.7e-07, 전 구간 평탄** |
| `std::sin` + `std::cos` | 71.9 M/s | 정확 |

DXMath보다 **27% 느리고 std보다 2.6배 빠르다.** 느린 이유는 범위 축소를 double로
하기 때문이며, 그 대가가 오른쪽 열이다 — DXMath와 같은 float 상수로 축소하면
|x|가 커질수록 축소된 각도가 오염되어 3.1e-02까지 벌어진다. 게이트 항목이 아니고,
게임에서 각도는 시간에 따라 누적되므로 평탄한 정확도를 택했다.

> 리뷰 중 이 경로에서 **22% 회귀를 한 번 만들었다가 되돌렸다.** `abs_scalar`를
> 비트 연산(`from_bits(bits_of(x) & abs_mask)`)으로 바꿨더니 slerp가 37.8 → 29.3,
> sin_cos가 201 → 184로 떨어졌다. x86에서 `bit_cast`는 값을 범용 레지스터로
> 왕복시키는데, 이 함수는 `reducible()`을 통해 **모든 삼각함수 호출**에 들어간다.
> 비교 형태로 되돌리고, 부호 있는 0은 작은 인자 조기 반환으로 처리했다.

> **컴파일 타임 = 런타임은 1 ULP까지만 보장된다.** 다항식을 상수 평가와 런타임이
> 같이 쓰지만, clang은 런타임에 곱셈-덧셈을 FMA로 축약하고 상수 평가는 축약하지
> 않는다. MSVC와 GCC는 비트 단위로 같다. 테스트는 이 경계를 ULP와 절대 오차
> **둘 중 하나**로 판정한다 — `cos(pi/2)`처럼 0에 가까운 값에서는 FMA 한 번 차이가
> 3527 ULP로 보이기 때문이다.

---

## 6. 최종 성능 감사 (Phase 5)

MSVC, `--benchmark_min_time=0.4s --benchmark_repetitions=9
--benchmark_enable_random_interleaving=true` 중앙값. 게이트는 PLAN §4.2의
**DXMath 대비 5% 이내**이며, 더 빠른 결과는 회귀가 아니다.

| 연산 | Mathematics | DirectXMath | 판정 |
|------|-------|-------------|------|
| add (지연) | 2.19 ns | 2.18 ns | 통과 |
| mul_add (지연) | 2.13 ns | 2.12 ns | 통과 |
| mul_add (처리량) | 1.007 G/s | 1.007 G/s | 통과 |
| sqrt (지연) | 4.08 ns | 4.07 ns | 통과 |
| dot3 / dot4 (지연) | 4.24 / 4.24 ns | 4.28 / 4.24 ns | 통과 |
| vector3 연쇄 (지연) | 2.14 ns | 2.13 ns※ | 통과 |
| normalize (처리량) | 356.8 M/s | 259.5 M/s | **+38%** |
| cross (지연) | 4.05 ns | 6.98 ns※ | **+42%** |
| cross (처리량) | 515.1 M/s | 466.0 M/s | **+11%** |
| mat4 곱 (지연) | 5.27 ns | 5.69 ns | +8% |
| mat4 곱 (처리량) | 341.3 M/s | 329.4 M/s | 통과 |
| mat4 역행렬 | 74.1 M/s | 68.3 M/s | +9% |
| mat4 전치 | 529.0 M/s | 515.1 M/s | 통과 |
| 배치 정점 변환 | 851.0 M/s | 652.4 M/s | **+30%** |
| 쿼터니언 곱 (지연) | 5.41 ns | 5.89 ns※ | **+8%** |
| 쿼터니언 곱 (처리량) | 582.5 M/s | 582.5 M/s | 통과 |
| slerp | 34.1 M/s | 30.6 M/s | +11% |
| 벡터 회전 | 203.9 M/s | 178.4 M/s | +14% |
| 쿼터니언 → 행렬 | 229.4 M/s | 160.6 M/s | +43% |
| TRS 합성 | 194.6 M/s | 133.8 M/s | +45% |
| sin_cos | 194.6 M/s | 259.5 M/s | −25% (의도) |

※ Mathematics의 12/16바이트 값 반환과 같은 저장 비용을 내는 `XMFLOAT3`/`XMFLOAT4` packed
경로다. 레지스터 상주 `XMVECTOR`는 참고 ceiling으로만 남기고 게이트에는 쓰지 않는다.

같은 게이트를 clang-cl로 실행한 수정 대상 결과:

| 연산 | Mathematics | DirectXMath packed | 판정 |
|------|-------|--------------------|------|
| cross (지연) | 3.793 ns | 3.793 ns | 통과 |
| cross (처리량) | 565.0 M/s | 593.1 M/s | 통과 (−4.75%) |
| mat4 전치 | 489.3 M/s | 407.8 M/s | **+20.0%** |
| 쿼터니언 곱 (지연) | 5.362 ns | 5.362 ns | 통과 |
| 쿼터니언 곱 (처리량) | 752.8 M/s | 762.6 M/s | 통과 (−1.28%) |

### 감사 자체가 찾아낸 것: 게이트 항목 세 개에 벤치가 없었다

PLAN §4.2가 게이트 대상으로 명시한 `cross`, `transpose(matrix4x4)`, 배치 정점 변환은
**단 한 번도 측정된 적이 없었다.** 평가할 수 없는 게이트는 게이트가 아니다.
셋을 추가하자 둘이 미달로 드러났다. 이후 수정과 재측정으로 `cross`와 전치를
닫았지만, 감사가 없었으면 미측정 상태를 "전 항목 통과"라고 적을 뻔했다. 배치 정점
변환은 반대로 DXMath의 전용 스트리밍 진입점(
`XMVector3TransformCoordStream`)보다 30% 빠르다. 전용 API 없이 평범한 루프로
두기로 한 설계가 값을 한 셈이다.

### 감사에서 찾은 세 회귀를 닫은 방법

1. **전치:** 변경 전 MSVC는 결과를 스택에 4×16바이트로 만든 뒤 2×32바이트로 다시
   읽어 목적지에 복사했다. 공개 기본 생성자의 zero-init 계약은 유지하고, 64바이트를
   전부 덮는 비공개 runtime row factory만 추가하자 임시가 사라져 `4 load + 8 shuffle +
   4 direct store`가 됐다. 별도 `transpose_to` 공개 API는 필요하지 않았다.
2. **cross:** 이전 latency 벤치는 실제로 `normalize(cross())`를 재면서 packed `vector3`를
   레지스터 상주 `XMVECTOR`와 비교했다. raw cross와 packed DX 경로로 고쳤고, 연산은
   `neg_mul_add`와 DXMath와 같은 evolving shuffle로 맞췄다. clang-cl에서는 8+4바이트로
   저장한 결과를 다음 반복에 4+8바이트 경계로 읽어 store-to-load forwarding이 깨졌다.
   12바이트 안전 `load3`로 읽기 경계를 맞추자 5.86ns에서 3.77ns로 내려갔다.
3. **quaternion:** 고정 half-turn 상수가 MSVC에서 DXMath 쪽에만 특수화되어 서로 다른
   연산을 비교하고 있었다. 양쪽 입력을 불투명하게 만들고 DXMath와 같은 evolving
   shuffle/누산 의존성 트리를 사용했다. 이 형태는 clang의 fast-math가 긴 직렬 FMA
   사슬을 다시 만드는 것도 막는다.

### 자동 게이트

`scripts/check_performance.ps1`가 위 세 수정의 다섯 비교를 MSVC와 clang-cl에서 각각
9회 무작위 교차 실행하고 median이 DXMath보다 5% 넘게 느리면 실패한다. latency는
`cpu_time`, throughput은 `items_per_second`를 사용하며 JSON 결과를 CI artifact로 남긴다.
해당 metric의 반복 CV가 10%를 넘으면 성능 판정 전에 불안정 표본으로 거부한다.
별도의 GCC/gcovr job은 `include/mathematics` line coverage가 80% 미만이면 실패한다.
이 수치는 GCC x86에서 활성화된 코드의 line coverage다. scalar/SSE2/NEON 경로는 별도
correctness matrix에서 실행하지만 이 coverage 보고서에는 합산하지 않는다. 공유 Windows
runner의 마이크로벤치 잡음은 9회 무작위 교차 median으로 줄이고 실패 표본은 한 번
자동 재시도하지만, 고정 하드웨어 측정만큼 안정적이지는 않으므로 CI artifact의 CV도
함께 확인해야 한다.

---

## 7. 표 요약 (프로젝트 초기 비교표 검증)

프로젝트 시작 시 참고한 라이브러리 비교표의 성능 항목을 실측으로 확인한 결과:

| 항목 | 표의 평가 | 실측 결과 |
|------|-----------|-----------|
| DirectXMath Vector SIMD ★★★★★ | 최상위 | **확인** — 전 항목 기준점 |
| GLM Vector SIMD ★★★★ | 준수 | **과대평가** — 지연은 동급이나 처리량 40% 열세. FMA 미형성 |
| Vectormath Vector SIMD ★★★★★ | 최상위 | **확인** — 처리량은 DXMath 동급, 스칼라 내적만 열세 |
| Vectormath 유지보수 ★★ | 낮음 | **확인** — 원본 방치, 포크에 태그조차 없어 커밋 SHA로 고정해야 함 |

---

## 재현

```bash
scripts\build.bat msvc-release
```
```bash
"%LOCALAPPDATA%\MathematicsBuild\msvc-release\bench\mathematics_bench.exe" --benchmark_min_time=0.6s --benchmark_repetitions=7 --benchmark_report_aggregates_only=true
```

수정 대상 CI 성능 게이트는 다음 한 줄로 그대로 재현한다.

```powershell
scripts\check_performance.ps1 -BenchmarkExe "$env:LOCALAPPDATA\MathematicsBuild\msvc-release\bench\mathematics_bench.exe" -OutputPath "$env:TEMP\mathematics-performance.json"
```

GLM과 Vectormath는 `-D MATHEMATICS_BENCH_GLM=OFF` / `-D MATHEMATICS_BENCH_VECTORMATH=OFF`로 뺄 수 있다.
둘 다 부가 참고 대상이며, 릴리스를 막는 것은 DirectXMath 대비 수치뿐이다.
