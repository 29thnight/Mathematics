# 미해결 사항

닫히지 않은 채 남아 있는 문제와, 각각에 대해 **무엇이 배제되었는지** 기록한다.
"아직 안 봤다"와 "봤는데 설명이 안 된다"는 다른 상태이고 뒤엣것만 여기 적는다.
해결되면 항목을 지우고 [BASELINE](BASELINE.md)이나 [PLAN](PLAN.md)으로 옮긴다.

최종 갱신: 2026-09-23

---

## 1. 처리량 게이트의 허용치 — 원인은 대부분 시계였고, 남은 것은 CPU 차이다

**상태**: 흔들림의 주원인(CPU 시간 양자화)은 고쳤다. 허용치는 그대로 두었다 — 남은 차이는
잡음이 아니라 CPU 모델 사이의 실제 차이라서, 허용치로 다룰 문제인지부터가 릴리스 기준의
결정 사항이다.

처음에는 이 흔들림을 코드 배치로 읽었다. DirectXMath 쪽 숫자가 재링크(+7%)와 러너 교체
(4x4 전치 477 → 631 → 1198 M/s)에 움직였기 때문이다. [BASELINE §11](BASELINE.md)의 표본
32잡이 그 읽기를 뒤집었다.

- **시계.** 게이트는 Windows의 15.6ms 틱으로 올라가는 CPU 시간을 쟀다. CV p99가 34%,
  최대 107%였고 벽시계로는 p99 6.4%다. 런이 달라도 끝자리까지 같던 값들은 양자화 눈금이었다.
  게이트는 이제 벽시계로 판정한다.
- **배치.** 벽시계로 보면 같은 CPU에서 `.text`를 0~48바이트 옮길 때 게이트 행이 움직이는
  폭은 대부분 2포인트 이하다(최대: clang 쿼터니언 곱 처리량 5.35pt).
- **하드웨어.** 러너 풀에는 CPU가 최소 다섯 종 있고, 같은 행이 CPU에 따라 수십 포인트 다르다.
  이것은 잡음이 아니라 실제 성능 차이다.

### 지금 허용치와 표본이 보여 주는 것

| 비교 | 허용치 | 표본의 최악 칸 (벽시계, CPU별 중앙값) |
|------|--------|------------------------------|
| Cross latency | 5% | 모든 CPU에서 +0.0% 이하 |
| Quaternion multiply latency | 5% | +2.0% (clang, Zen 5) |
| Cross throughput | 35% | +23.7% (MSVC, Zen 3) |
| Matrix4x4 transpose | 35% | +28.6% (clang, Zen 5) |
| Quaternion multiply throughput | 20% | +1.9% (clang, Granite Rapids) |

쿼터니언 곱 처리량의 20%는 이제 근거가 없다 — 모든 CPU에서 2% 안이다. 나머지 두 처리량 행의
35%는 잡음을 덮던 것이 아니라 **특정 CPU에서의 실제 격차**를 덮고 있다. 5%로 좁히면 CI는
어떤 CPU를 뽑느냐에 따라 빨갛게 되고, 넓혀 두면 그 CPU들의 격차를 게이트가 못 본다.

### 결정이 필요한 것

릴리스 기준 "전 항목 DirectXMath 대비 ±5%"가 **어떤 하드웨어에서** 판정되는지를 정해야
허용치를 정할 수 있다. 기준 기계(i7-8700K)로 정하면 CI 게이트는 회귀 탐지선으로 남고,
러너 풀 전체로 정하면 CPU별 격차 몇 개(BASELINE §11 끝 표)가 곧 릴리스 차단 항목이 된다.

---

## 2. 커버리지가 측정하지 못하는 영역

**상태**: 큰 왜곡은 제거했다. 남은 것은 구조적이라 테스트로 못 채운다.

`-fkeep-inline-functions`와 `always_inline`이 겹쳐 **함수 시그니처 줄 322개**가
영구히 0으로 찍히던 문제는 커버리지 빌드에서 `MATHEMATICS_INLINE`을 맨 키워드로
되돌려 해결했다(라인 75.3% → 88.3%, 시그니처 인공물 322 → 18).

남은 것:

- **`<mdspan>`이 아예 측정되지 않는다.** ubuntu-24.04의 GCC 13에는 `<mdspan>`이 없어
  `MATHEMATICS_HAS_MDSPAN`이 0이고, `mdspan.hpp` 184줄이 커버리지 리포트에
  **파일 자체가 등장하지 않는다**. 분모에도 분자에도 없으니 퍼센트는 영향을 받지 않지만
  그 헤더는 커버리지로 지켜지지 않고 있다. GCC 15 러너가 나오면 자동으로 해소된다.
- **상수평가 전용 경로.** `consteval_ops.hpp`는 (1) SSE/NEON의 컴파일타임 경로,
  (2) 스칼라 백엔드, (3) 테스트 오라클 세 역할을 겸한다. x86 GCC 커버리지 빌드에서
  (1)·(3)은 상수평가라 gcov가 셀 수 없고 (2)는 컴파일아웃된다. `MATHEMATICS_IF_CONSTEVAL`의
  consteval 분기도 같다. 91.2%까지 올라왔지만 나머지는 런타임 도달 불가다.
- **실제 테스트 부채** (이건 채울 수 있다): `color.hpp` 65.9%, `views.hpp` 67.6%,
  `reg.hpp` 50.0%, `vector4.hpp` 62.1%, `rect.hpp` 77.5%.

---

## 3. GCC 13 `<cmath>` 우회는 임시다

**상태**: 막아 뒀다. 툴체인이 고쳐지면 지워야 한다.

커버리지 빌드가 `-U__STDCPP_FLOAT16_T__ -U__STDCPP_BFLOAT16_T__`를 쓴다.
`-fkeep-inline-functions`가 libstdc++의 미사용 인라인까지 방출하는데, GCC 13의
`<cmath>`에 있는 C++23 `std::nextafter(_Float16)` / `(bfloat16_t)`가 링크되지 않기
때문이다. 해당 함수는 이렇게 시작한다.

```cpp
#if __cpp_if_consteval >= 202106L
    // Can't use if (std::__is_constant_evaluated()) here, as it
    // doesn't guarantee optimizing the body away at -O0 and
    // nothing defines nextafterf16.
    if consteval { return __builtin_nextafterf16(__x, __y); }
#endif
```

libstdc++ 자신의 주석이 이유를 적어 뒀는데, `-O0`에서 그 분기가 오브젝트까지 살아남아
`nextafterf16`(glibc에 없음)과 `__builtin_nextafterf16b`(GCC 13이 빌트인으로 인식조차
못 함) 미정의로 링크가 깨진다. **GCC 14도 같은 코드**라 컴파일러 상향은 해결책이 아니다.

러너의 GCC/glibc가 이걸 고치면 두 `-U`를 지운다. 지운 뒤 커버리지 잡이 링크되면 해소된 것이다.

---

## 4. clang-cl 쿼터니언 곱의 러너 민감도

**상태**: 대부분 시계로 설명된다. 다만 +10.59%가 나왔던 CPU가 표본에 다시 나오지 않아
닫지는 않는다.

`bm_mathematics_quaternion_multiply_throughput`은 clang asm이 DirectXMath와 명령어 구성이
같은데 러너에 따라 0.00%와 +10.59%로 판정이 갈렸다. 벽시계로 다시 잰 표본(샘플러 두 번 합)에서는 다섯 CPU
모두 중앙값이 ±2% 안이다(Zen 3 −1.2, Zen 4 0.0, Zen 5 −0.4, Emerald Rapids +0.9,
Granite Rapids +1.9). 남은 것은 같은 Zen 3에서 코드 배치에 따라 5.35포인트 움직인다는 점
하나로, 게이트 행 중 가장 크다. +10.59% 때의 러너(2596MHz, L2 1MiB — Cascade Lake로 추정)가
표본에 다시 걸리면 닫는다.

---

## 5. 구조적 바인딩이 벤치 TU에서만 스칼라 적재를 받는다

**상태**: 형태의 성질이 아님은 배제했다. 왜 이 TU에서만 갈리는지는 설명되지 않았다.

`bm_fixed_ranges_batch_structured_sum`이 **396.8 M/s로, 내부 루프가 남아 있는
range-for판(652.4)보다도 느리다.** 루프를 만들지 않는데도 그렇다.

| 벤치 | 최내곽 루프 | 패킹 연산 | MSVC |
|------|------------|----------|------|
| `batch_direct_sum` | 74명령 / vector4 8개 | 16 | 1087.4 M/s |
| `batch_fold_sum` | 82명령 / vector4 8개 | 16 | 1087.4 M/s |
| `batch_structured_sum` | 75명령 / vector4 8개 | **0** | 396.8 M/s |
| `batch_view_sum` | 4명령 / float 1개 | 0 | 652.4 M/s |

`vmovss` 32개의 스칼라 적재를 받고, `direct`·`fold_fixed`는 `vmovups` 8 + `vaddps` 8을
받는다. 이것이 격차의 전부다.

### 배제된 것

- **구조적 바인딩의 성질이 아니다.** 같은 소스를 같은 벤치 하네스(바깥 루프 +
  `DoNotOptimize`)와 함께 단독 TU로 컴파일하면 `fold_fixed`·`get<I>`판과 **명령어
  구성이 완전히 동일**하다 — 패킹 연산 16개까지 같다.
- **루프가 남은 것이 아니다.** 역방향 분기는 2개로 `direct`와 같다.
  `scripts/check_codegen.ps1`의 `probe_components_structured_sum`도 1개로 통과한다.
- **접근자 경로가 아니다.** `get<I>` 직접 호출판도 단독 TU에서 같은 코드가 나온다.
- **clang이 아니다.** clang에서는 네 형태가 모두 2.12~2.16 G/s로 같다.

### 남은 가설

2251줄짜리 `baseline_bench.cpp` 전체를 두고 내리는 인라인·최적화 예산 판단이 이
함수에서만 다르게 떨어진다. 확인하려면 TU를 쪼개 가며 어느 시점에 패킹 적재가
돌아오는지 이분하는 수밖에 없는데, 그렇게 얻은 답이 다음 커밋에서도 유지된다는
보장이 없다. 한때 §1의 코드 배치 문제와 같은 부류로 봤지만, §1의 표본은 배치 효과가
작다고 보여 주고 이 격차는 명령어 자체가 다르다(스칼라 적재 32개 대 묶음 8개) — 배치가
아니라 코드 생성 문제다. 또 이 측정은 로컬 19.51에서 나왔다. [BASELINE §10](BASELINE.md)의
교훈대로 러너의 19.44에서도 같은 코드가 나오는지는 확인하지 않았다.

그동안의 지침은 [GUIDE](GUIDE.md)에 적은 대로다 — 뜨거운 루프에서는 `fold_fixed`를
쓴다. 구조적 바인딩은 편의와 상수평가를 위한 것이고, 성능 답이 아니다.
