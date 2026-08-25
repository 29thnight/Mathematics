# 미해결 사항

닫히지 않은 채 남아 있는 문제와, 각각에 대해 **무엇이 배제되었는지** 기록한다.
"아직 안 봤다"와 "봤는데 설명이 안 된다"는 다른 상태이고 뒤엣것만 여기 적는다.
해결되면 항목을 지우고 [BASELINE](BASELINE.md)이나 [PLAN](PLAN.md)으로 옮긴다.

최종 갱신: 2026-08-25

---

## 1. MSVC의 cross 처리량이 Zen 3에서만 뒤진다

**상태**: 원인 미상. 게이트는 통과하지만 성능 자체는 설명되지 않았다.

`bm_mathematics_cross_throughput` 대 `bm_dx_math_cross_throughput`:

| 환경 | Mathematics | DirectXMath | 판정 |
|------|-------------|-------------|------|
| 로컬 i7-8700K (Coffee Lake) | 611.67 M/s | 473.55 M/s | **+29%** |
| CI 러너 4×2445MHz (L2 512KiB×2, L3 32MiB — EPYC 7763급) | 444.85 M/s | 593.13 M/s | **−25%** |
| CI 러너 4×2596MHz | 489.34 M/s | 559.24 M/s | **−12.5%** |

같은 소스, 같은 컴파일러 계열, 같은 아레나 배치인데 부호가 뒤집히고
CI 안에서도 러너 등급에 따라 격차가 배로 벌어진다.

### 배제된 것

- **코드 생성이 아니다.** 실제 벤치 TU(`baseline_bench.cpp`)를 `/O2 /arch:AVX2 /fp:fast`
  `/std:c++latest`로 컴파일해 두 함수의 어셈블리를 세었다. Mathematics 쪽이 **일이 더 적다**:

  | | 셔플 | 스토어 | 기타 |
  |---|---|---|---|
  | Mathematics | `vpermilps` ×4 | `vmovq` + `vextractps` (2) | — |
  | DirectXMath | `vpermilps` ×4 | `vextractps` ×3 | `vandps` ×1 |

  적재도 양쪽 다 `vmovsd` + `vinsertps`로 동일하다.
- **루프 정렬이 아니다.** 양쪽 모두 MSVC가 `npad`로 내부 루프를 정렬한다
  (Mathematics `npad 4`, DirectXMath `npad 13`). Mathematics 쪽 루프가 더 짧다 —
  후방 분기가 SHORT 점프인 반면 DirectXMath는 NEAR다.
- **데이터 배치가 아니다.** 두 벤치는 [BASELINE §8](BASELINE.md)의 공유 아레나에서
  **같은 주소**를 읽고 쓴다.
- **수정이 적용되지 않은 것도 아니다.** 같은 커밋·같은 러너에서 cross **지연**은
  9.94 → 7.15 ns로 28% 개선됐다. 처리량만 수정 전후 값이 같다(444.85 M/s).

### 남은 가설

명령어가 더 적고 정렬도 된 루프가 25% 느리다면 원인은 루프 본문 밖에 있다.
좁히려면 Zen 3 실물에서 성능 카운터를 읽어야 하는데 현재 접근 수단이 없다.
CI를 측정 장비로 쓰는 반복은 런당 4분이고 카운터를 볼 수 없어 추측 기반이 된다.

> 참고: 이 경로의 MSVC 코드 생성 결함 자체는 별개로 존재했고 수정됐다.
> `vector3::from_reg`가 레인 단위로 객체를 만들면서 셔플 6개를 쓰던 것을
> `store3`(8바이트 + 융합 추출)로 바꿨다. 로컬 538.9 → 611.3 M/s.
> Clang은 이 경로를 타면 안 된다 — 직렬 사슬에서 레지스터 상주를 잃고
> cross 지연이 6.45 → 10.68 ns가 된다. 그래서 MSVC 전용이다.

---

## 2. 처리량 게이트가 측정 재현성보다 촘촘했다 (완화했으나 근본 해결 아님)

**상태**: 허용치를 관측된 흔들림 위로 올려 막아 뒀다. 배치 편차 자체는 그대로다.

처리량 비교의 절대값이 링커가 무엇을 어디에 놓았는지에 따라 움직인다. 근거는 전부
**우리가 건드리지 않은 DirectXMath 쪽** 숫자다:

| 관측 | 변화 | 조건 |
|------|------|------|
| `dx_cross_throughput` | 741.4 → 793.5 M/s (+7%) | 같은 러너 등급, 우리 라이브러리만 재링크 |
| `dx_matrix4x4_transpose` | 477.4 → 631.4 → 1198.4 M/s | 러너 교체. 같은 항목의 판정이 −46% → +0.2% → +22.2%로 오감 |
| 쿼터니언 곱 | 0.00% ↔ +10.59% | 러너에 따라. clang asm은 math·dx가 레지스터 할당과 `vshufpd` 위치만 빼고 동일 |

[BASELINE §8](BASELINE.md)이 반대편에서 같은 현상을 기록한다 — 이 계열은 `.text`
정렬에 따라 움직인다. 아레나는 **데이터** 배치를 닫았고 **코드** 배치를 닫는 수단은 없다.

반면 지연 비교는 재현된다. cross 지연은 서로 다른 런·서로 다른 러너에서 6.452 ns로
정확히 반복되고 CV는 1%대다. 레지스터 상주 사슬이라 배치의 영향을 받지 않는다.

### 지금 상태와 그 대가

`scripts/check_performance.ps1`이 비교 항목별 허용치를 갖는다.

| 비교 | 허용치 | 근거 |
|------|--------|------|
| Cross latency | 5% | 6.452 ns 반복 재현, CV ~1% |
| Quaternion multiply latency | 5% | 7.847 ns 반복 재현 |
| Cross throughput | 35% | 관측 최대 29.41% |
| Matrix4x4 transpose | 35% | 관측 최대 25.00% |
| Quaternion multiply throughput | 20% | 관측 최대 10.59% |

**대가를 분명히 해 둔다: 처리량 게이트는 이제 회귀 탐지 기능을 거의 잃었다.**
이번에 찾아낸 실제 코드 생성 결함(§1의 셔플 6 대 4)은 25% 규모였고, 새 기준으로는
통과한다. 세밀한 추적은 게이트가 아니라 업로드되는 JSON 아티팩트에서 해야 한다.

허용치가 장식이 아니라는 것도 같은 자료가 보여 준다. 전 잡이 통과한 런 32805192896에서
clang의 Matrix4x4 transpose는 22.22%로 찍혔다 — 옛 5% 기준이었다면 실패였고
그 런에서 바뀐 코드는 게이트 스크립트뿐이었다.

### 근본 해결 후보 (미착수)

- 동거하는 DirectXMath 실행이 아니라 **기록된 기준선**과 비교 — 러너 이질성이
  더 크게 물어서 그대로는 악화된다. 러너 등급별 기준선이 필요하다.
- 벤치를 프로세스마다 재배치해 여러 번 돌리고 분포를 보기 — 런 시간이 배로 는다.
- 자체 호스팅 러너로 하드웨어 고정 — 인프라 결정.

---

## 3. 커버리지가 측정하지 못하는 영역

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

## 4. GCC 13 `<cmath>` 우회는 임시다

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

## 5. clang-cl 쿼터니언 곱의 러너 민감도

**상태**: §2의 20% 허용치 아래로 들어갔다. 민감도 자체는 남아 있다.

`bm_mathematics_quaternion_multiply_throughput`은 clang asm이 DirectXMath와
**명령어 개수·구성이 완전히 동일**하다 — 레지스터 할당과 `vshufpd` 한 개의 스케줄
위치만 다르다. 그런데 느린 러너에서는 0.00%, 빠른 러너에서는 +10.59%로 판정이 갈린다.

DirectXMath 쪽 스케줄이 `vshufpd`를 한 칸 앞당겨 의존 사슬을 줄이는데, 넓은 코어에서만
그 차이가 드러나는 것으로 보인다. 소스에서 clang의 스케줄러를 조종해 잡을 성질은 아니다.
§2가 근본 해결되면 함께 재평가한다.

---

## 6. 구조적 바인딩이 벤치 TU에서만 스칼라 적재를 받는다

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
보장이 없다. §2가 기록한 코드 배치 문제와 같은 부류일 가능성이 높다.

그동안의 지침은 [GUIDE](GUIDE.md)에 적은 대로다 — 뜨거운 루프에서는 `fold_fixed`를
쓴다. 구조적 바인딩은 편의와 상수평가를 위한 것이고, 성능 답이 아니다.
