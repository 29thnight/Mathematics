# 성능 기준선 — Mathf / DirectXMath / GLM / Sony Vectormath

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

**게이트 대상**: Mathf는 DirectXMath 대비 ±5% 이내여야 한다.

| 연산 | Mathf | DirectXMath | GLM | Vectormath |
|------|-------|-------------|-----|------------|
| Add | 2.26 ns | 2.25 ns | 2.24 ns | 2.24 ns |
| MulAdd | 2.25 ns | 2.25 ns | 2.25 ns | 2.31 ns |
| Sqrt | 4.28 ns | 4.17 ns | — | — |
| Dot3 | 4.41 ns | 4.42 ns | — | — |
| Dot4 | 4.42 ns | 4.40 ns | — | — |
| **Dot4 → 스칼라** | 4.43 ns | 4.41 ns | **4.16 ns** | **5.97 ns** |

측정 변동(cv)은 대부분 1% 미만. **Mathf는 전 항목에서 DXMath 대비 ±3% 이내로 PASS.**

### 왜 내적만 두 줄인가
API가 다르기 때문이다. Mathf와 DXMath의 `Dot`은 결과를 **전 레인에 splat**해서
반환하고, GLM은 **스칼라 float**을, Vectormath는 `FloatInVec`을 반환한다. 벡터 사슬로
비교하면 GLM에만 브로드캐스트 비용이 붙어 불공정하다.

그래서 **"내적을 계산해 스칼라로 쓴다"** 는 실제 사용 형태를 별도로 측정했다 —
네 라이브러리 모두 `float 브로드캐스트 → 내적 → 한 레인 읽기`로 형태가 동일하다.
여기서 **GLM이 가장 빠른데(4.16ns), 스칼라 반환이 네이티브라 추출 단계가 없기 때문**이다.
반대로 Vectormath는 5.97ns로 가장 느리다.

> 즉 splat 반환은 벡터 연산을 이어갈 때 유리하고, 스칼라로 쓸 때는 불리하다.
> Mathf가 DXMath 관례를 따르므로 두 성질을 그대로 물려받는다.

---

## 2. 처리량 (throughput) — 512개 배치, 정렬 적재/저장

| 구현 | 시간 | 처리율 | cv |
|------|------|--------|-----|
| **Mathf** | 537 ns | **976 M items/s** | 1.2% |
| Sony Vectormath | 537 ns | 982 M/s | 0.6% |
| DirectXMath | 539 ns | 968 M/s | 0.6% |
| GLM | 908 ns | 586 M/s | 1.5% |

Mathf·Vectormath·DXMath는 서로 잡음 범위 안에서 동일하다.
**GLM만 약 40% 느린데**, `GLM_FORCE_INTRINSICS`를 켜도 `a * b + c`가 FMA 하나로
합쳐지지 않고 곱셈과 덧셈으로 남기 때문이다.

### 이 측정은 두 번 고쳐졌다 (기록)
초기 버전은 신뢰할 수 없었고, 두 가지가 문제였다.

**(1) 연산이 아니라 벡터 구성 비용을 재고 있었다.** 매 반복마다 스칼라 4개로 벡터를
만들고 다시 스칼라 4개로 풀어 저장했다. Phase 1에서 `Load`/`Store`가 생긴 뒤 정렬
적재/저장으로 바꾸자 전 라이브러리가 약 2배 빨라졌다 (Mathf 308 → 729 M/s).
DXMath만 `XMStoreFloat4`를 쓰고 Mathf는 `Lane()` 4회를 쓰던 비대칭도 함께 해소됐다.

**(2) 배치가 커서 메모리 대역폭에 묶여 있었다.** 4096개 × 64바이트 = 256KiB로 L2를
넘겨, 네 라이브러리가 모두 대역폭에 수렴하고 **변동이 11~17%** 에 달했다. L1에 들어가는
512개(32KiB)로 줄이자 **변동이 1% 수준으로** 떨어지고 라이브러리 간 차이가 드러났다.

> **결론: 처리량도 이제 게이트에 쓸 수 있다.** 이전 버전 문서에서 "처리량은 잡음이 커서
> 게이트에서 제외"라고 한 판단은 위 두 결함에서 비롯된 것이었고, 결함을 고치니 근거가
> 사라졌다.

---

## 3. Vector3 — 사용자 대면 타입 (Phase 2)

`Vector3`는 12바이트 패킹 타입이다. 아래 두 측정이 Phase 2의 설계를 검증한다.

### 연쇄 표현식 `a * b + c` (지연)

| 구현 | 지연 | 비고 |
|------|------|------|
| **Mathf `Vector3`** | **2.25 ns** | 12바이트 패킹 |
| DXMath `XMVECTOR` | 2.28 ns | 레지스터 상주 — 상한 |
| GLM `vec3` | 3.21 ns | 스칼라 |
| DXMath `XMFLOAT3` | 5.59 ns | 매 단계 load/store |

**패킹 타입이 레지스터 타입과 동일한 성능을 낸다.** 3성분 산술을 스칼라로 두면
성분들이 독립적인 의존 사슬로 파이프라이닝되어, SIMD 1회와 같은 지연이 나온다.
(초기 SIMD 승격 방식은 5.25ns로 `XMFLOAT3` 수준이었다 — PLAN.md Phase 2 참조.)

### Normalize 처리량 (512개 스트림)

| 구현 | 처리율 | 퇴화 입력 보장 |
|------|--------|----------------|
| GLM | 465 M/s | **없음** — 플래그에 따라 달라짐 |
| **Mathf** | **342 M/s** | 있음 (0 → 0, ∞ → NaN) |
| DirectXMath | 259 M/s | 있음 |

Mathf는 DXMath보다 **32% 빠르다**. GLM이 더 빠른 것은 퇴화 케이스를 아예 다루지
않기 때문이며, **그 동작은 보장되지 않는다** — 실측 확인:

| GLM `normalize(vec3(0))` | 결과 |
|---|---|
| `/fp:fast` | `(0, 0, 0)` |
| `/fp:precise` | `NaN` |

즉 GLM에서 0 벡터 정규화의 결과는 컴파일 플래그에 좌우된다. Mathf와 DXMath는
플래그와 무관하게 0을 반환한다. 씬 그래프를 타고 번지는 NaN을 막는 값이 이 차이다.

---

## 4. Matrix4x4 (Phase 3)

### 곱셈 — **게이트 통과 (MSVC)**

| 구현 | 지연 | 처리량 (256개 배치) |
|------|------|---------------------|
| **Mathf** | **5.83 ns** | **334 M/s** |
| DirectXMath | 6.10 ns | 307 M/s |
| GLM | — | 221 M/s |

MSVC 기준 두 축 모두 DXMath보다 앞선다. 여기까지 오는 데 **두 번의 수정**이 필요했다:

**(1) 셔플 포트 병목 (141 → 251 M/s).** 처음엔 행을 적재한 뒤 `SplatX/Y/Z/W`로
성분을 뽑았는데, 이는 곱셈 1회당 셔플 16개를 만든다. 셔플 포트는 하나뿐이라
FMA 16개(이론 최소)가 아니라 셔플이 병목이 된다. `LoadSplat`(메모리에서 직접
브로드캐스트)을 백엔드에 추가해 작업을 적재 포트로 옮겼다.

**(2) 알고리즘 격차 — 256비트 (251 → 334 M/s).** 그래도 DXMath에 못 미쳐
`DirectXMathMatrix.inl`을 읽어보니 `XMMatrixMultiply`가 **AVX2 256비트 경로**로
두 행을 한 번에 처리하고 있었다. 128비트 안에서 아무리 조율해도 연산량이 절반인
알고리즘은 따라잡을 수 없다. 같은 방식을 구현했다 — 라이브러리에서 128비트를
넘는 유일한 지점이다.

> **clang에서는 아직 뒤진다** (Mathf 335 M/s 대 DXMath 489). 같은 알고리즘인데
> clang이 DXMath 쪽 루프를 더 잘 최적화한다. MSVC에서는 반대다(307). 원인 미규명.

### 역행렬 — clang 통과, MSVC 미달

| 구현 | MSVC | clang |
|------|------|-------|
| **Mathf** | 56.5 M/s | **101.0 M/s** |
| DirectXMath | 67.2 M/s | 81.2 M/s |
| GLM | 44.9 M/s | 51.6 M/s |

**clang에서는 DXMath보다 24% 빠르고, MSVC에서는 16% 뒤진다.** 두 컴파일러 모두에서
GLM보다는 빠르다.

SIMD화 과정 (MSVC 기준 42.8 → 56.5, clang 71.5 → 101.0):

| 단계 | MSVC |
|------|------|
| 스칼라 라플라스 전개 (약 150회 연산) | 42.8 M/s |
| + 벡터화 | 47.5 M/s |
| + 전치를 레지스터에서 수행 (메모리 왕복 제거, 셔플 4개 절약) | 56.2 M/s |
| + 역수를 벡터에 유지 (스칼라 왕복 제거) | 56.5 M/s |

가장 큰 단일 개선은 **전치의 메모리 왕복 제거**였다. `Transpose()`는 `Matrix4x4`를
반환하므로 16개 float을 저장했다가 즉시 다시 적재한다. 전치를 레지스터 안에서
수행하되 행 순서를 (1,0,3,2)로 바꿔 수행하면, 수반행렬이 필요로 하는 열 순열이
전치와 함께 공짜로 나온다.

> **남은 MSVC 격차는 컴파일러 최적화 품질로 보인다.** 같은 코드가 clang에서는
> DXMath를 24% 앞서고 MSVC에서는 16% 뒤진다. 곱셈에서는 정확히 반대 방향이었다
> (MSVC에서 우리가 앞서고 clang에서 DXMath가 앞섬).

---

## 5. 표 요약 (프로젝트 초기 비교표 검증)

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
"%LOCALAPPDATA%\MathfBuild\msvc-release\bench\mathf_bench.exe" --benchmark_min_time=0.6s --benchmark_repetitions=7 --benchmark_report_aggregates_only=true
```

GLM과 Vectormath는 `-D MATHF_BENCH_GLM=OFF` / `-D MATHF_BENCH_VECTORMATH=OFF`로 뺄 수 있다.
둘 다 부가 참고 대상이며, 릴리스를 막는 것은 DirectXMath 대비 수치뿐이다.
