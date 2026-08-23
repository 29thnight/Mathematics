# Phase 0 스파이크 결과 — constexpr 이중 경로 & 코드젠

측정일: 2026-08-24
환경: Windows 11 / MSVC 14.51.36231 / clang-cl (VS 18.8) / Windows SDK 10.0.26100
플래그: `/std:c++20 /O2 /arch:AVX2 /fp:fast`

---

## 결론 요약

| 질문 | 결과 |
|------|------|
| constexpr 이중 경로가 MSVC/Clang 양쪽에서 되는가? | **된다** |
| 그 래퍼가 런타임 코드젠을 해치는가? | **해치지 않는다 — DXMath와 명령어 단위 동일** |
| 채택할 `VecReg` 저장 전략은? | **전략 A: `struct VecReg { __m128 v; }`** |

> PLAN.md §2.3이 최난점으로 지목했던 리스크는 해소되었다. 설계 변경 없이 진행한다.

---

## 1. constexpr 평가 가능성

네 가지 저장 전략을 각각 컴파일하고, `static_assert`로 컴파일 타임 평가를 강제했다.

| 전략 | 저장 타입 | MSVC | clang-cl |
|------|-----------|------|----------|
| A | `struct { __m128 v; }` | PASS | PASS |
| B | `union { float f[4]; __m128 v; }` | PASS | PASS |
| C | `struct alignas(16) { float f[4]; }` | PASS | PASS |
| D | `float __attribute__((vector_size(16)))` | PASS(무시됨) | PASS |

### 네거티브 컨트롤 (필수 검증)

"전부 PASS"는 `static_assert`가 실제로 평가되지 않았을 가능성을 의심하게 한다.
따라서 동일 코드에서 기댓값만 틀리게(`== 999.0f`) 바꾼 대조군을 컴파일했고,
**네 경우 모두 정확히 static_assert 실패로 거부**되었다:

```
MSVC : error C2338: static assertion failed
clang: error: static assertion failed ... note: expression evaluates to '11 == 999'
```

clang이 `11 == 999`로 보고한 것은 **`__m128` 레인 경로를 통해 컴파일 타임에 11을
실제로 계산했다**는 직접 증거다. 컴파일 타임 평가는 진짜다.

### 이식성 함정 (설계에 반영됨)

`__m128`의 레인 접근은 컴파일러마다 다르다:
- MSVC: `__m128`은 `__declspec(intrin_type)` **union** → `v.m128_f32[i]`
- Clang/GCC: `__m128`은 **네이티브 벡터 타입** → `v[i]`

clang-cl에서 `m128_f32`를 직접 쓰면 `error: member reference base type '__m128'
is not a structure or union`으로 실패한다. 따라서 **모든 컴파일 타임 레인 접근은
`Lane()` / `SetLane()` 헬퍼를 반드시 경유해야 한다.**

---

## 2. 코드젠 비교 (핵심)

동일 연산을 전략 A / 전략 C / DirectXMath 세 방식으로 작성하고 어셈블리를 비교했다.
호출 규약은 세 방식 모두 `__vectorcall` (DXMath의 `XM_CALLCONV`와 동일).

### MulAdd — `a * b + c`

| 구현 | MSVC | clang-cl |
|------|------|----------|
| **전략 A** | `vfmadd132ps xmm0, xmm2, xmm1` + `ret` | `vfmadd213ps xmm0, xmm1, xmm2` + `ret` |
| **DirectXMath** | `vfmadd132ps xmm0, xmm2, xmm1` + `ret` | `vfmadd213ps xmm0, xmm1, xmm2` + `ret` |
| **전략 C** | 14개 명령 (`sub rsp,24`, `vshufps`×3, `vinsertps`×2 …) | 10개 명령 (`vinsertps`×3, `vmovshdup`, `vshufpd` …) |

**전략 A는 DXMath와 완전히 동일한 코드를 낸다.** constexpr 래퍼의 런타임 비용은 0이다.

### 체인 표현식 — `MulAdd(a,b,c) + d*d`

전략 A와 DXMath 모두 **양쪽 컴파일러에서 4개 명령으로 완전히 동일**:
```
vfmadd213ps xmm0, xmm1, xmm2
vfmadd231ps xmm0, xmm3, xmm3
ret
```
중간값이 전부 레지스터에 머무르며 스택 왕복이 전혀 없다.
→ **PLAN.md §2.7의 "연산자는 정본 함수의 인라인 별칭" 설계가 무비용임이 실증되었다.**
   표현식 템플릿을 배제한 판단도 함께 뒷받침된다.

---

## 3. 전략 C가 탈락한 이유 (중요)

`float[4]` 저장 방식은 load/store 명령이 남는 문제가 아니라 **ABI 문제**로 무너졌다.

4-float 구조체는 **HFA(Homogeneous Float Aggregate)** 로 분류되어 `__vectorcall`에서
xmm0~xmm3 **4개 레지스터에 스칼라로 분해되어** 전달된다. 함수는 매번 이를
`vinsertps`로 재조립하고, 반환할 때 다시 분해해야 한다. MSVC는 여기에 더해
`sub rsp, 24`로 스택 프레임까지 잡는다.

인라인되는 구간에서는 문제가 없지만, **인라인되지 않는 모든 경계에서 비용이 발생**하므로
라이브러리의 기본 저장 타입으로는 실격이다.

> 이 결과는 저장 타입(`Float2/3/4`)과 레지스터 타입(`VecReg`)을 분리하는
> PLAN.md §2.2의 3계층 설계가 왜 필요한지도 함께 증명한다. `Float4`는 메모리
> 레이아웃 용도로만 쓰고, 연산은 반드시 `VecReg`로 승격시켜야 한다.

---

## 4. 부수 발견 — Dot4는 `dpps`를 써야 한다

| 구현 | 명령 |
|------|------|
| 스파이크(수동 `hadd`) | `vmulps` + `vhaddps` + `vhaddps` (3) |
| DirectXMath | `vdpps xmm0, xmm0, xmm1, 255` (1) |

`_mm_dp_ps`(SSE4.1)를 쓰지 않으면 Dot에서 DXMath에 진다.
**Phase 2 작업 항목**: `Dot`은 SSE4.1 사용 가능 시 `dpps` 경로를 우선한다.
단, MSVC는 `__SSE4_1__`을 정의하지 않으므로 `MATHF_ENABLE_SSE4` opt-in 매크로가
필요하다 (DXMath의 `_XM_SSE4_INTRINSICS_`와 동일한 접근).

---

## 5. 배포 헤더 재검증 — MSVC `/GS` 스택 쿠키 (알려진 차이)

§2는 스파이크 프로토타입을 측정한 것이다. 실제 `include/mathf/vec_reg.hpp`로
다시 측정한 결과, **연산 코드젠은 동일하지만 MSVC에서만 함수당 스택 쿠키가 붙는다.**

64회 벡터 연산을 수행하는 실사용 형태 함수(전부 인라인됨)의 MSVC 결과:

| | Mathf | DirectXMath |
|---|---|---|
| **SIMD 명령 수** | **46** | **46** |
| `/GS` 관련 명령 | 7 | 0 |
| 전체 | 77 | 70 |

**연산 자체는 완전히 동일하다.** 차이 7개는 전부 `/GS`(버퍼 보안 검사) 스캐폴딩이며,
연산 수와 무관한 **함수당 상수 비용**이다(연산 64회든 6400회든 7개).

### 원인
MSVC의 `__m128`은 `float m128_f32[4]`를 품은 union이다. constexpr 분기에서 레인을
읽으려면 이 배열 멤버에 접근해야 하는데, `/GS` 분석은 이를 "인덱싱 가능한 버퍼"로
분류해 스택 쿠키를 삽입한다. 해당 분기는 런타임에 죽은 코드지만 `/GS` 판정은 그보다
앞선 단계에서 이뤄진다. DirectXMath는 constexpr 경로가 없어 배열 접근이 없고,
따라서 쿠키도 없다.

### 시도했으나 효과가 없던 것 (기록)
| 시도 | 결과 |
|------|------|
| 지역 변수 변경 대신 `Set()`으로 결과 구성 | 변화 없음 |
| 루프를 상수 인덱스로 완전 언롤 | 변화 없음 |
| `MATHF_INLINE`에 `__declspec(safebuffers)` 부착 | **변화 없음** — `/GS` 판정은 *호출자* 기준이라 인라인된 피호출자의 속성은 전파되지 않는다 |

`safebuffers`는 효과 없이 보안만 약화시키므로 되돌렸다.

### 현재 판단
- **clang-cl은 이 문제가 없다 — 모든 probe에서 DXMath와 완전 동일.**
- MSVC의 비용은 함수당 상수이며, 핫 루프에서는 무시할 수준이다.
- 제거가 필요한 사용자는 해당 함수에 `__declspec(safebuffers)`를 붙이거나
  `/GS-`로 빌드하면 된다(둘 다 라이브러리가 아니라 사용자 쪽 결정).
- 이 차이는 `Float4` 저장 타입이 생기는 Phase 2에서 재평가한다. 저장 타입 기반
  경로가 주가 되면 constexpr 레인 접근의 노출 빈도가 달라질 수 있다.

재현: `spike\run_codegen.bat` → `library_msvc.asm` / `library_clang.asm`

---

## 6. 확정된 설계

```cpp
struct VecReg { __m128 v; };          // x64. NEON은 float32x4_t.

// 모든 컴파일 타임 레인 접근은 반드시 이 헬퍼를 경유한다.
constexpr float Lane(const __m128& v, int i) noexcept;
constexpr void  SetLane(__m128& v, int i, float x) noexcept;

MATHF_INLINE constexpr VecReg Add(VecReg a, VecReg b) noexcept {
    if (std::is_constant_evaluated()) { /* Lane 기반 스칼라 */ }
    return VecReg{_mm_add_ps(a.v, b.v)};   // DXMath와 동일 코드젠
}
```

**후속 Phase 규칙**
1. `VecReg`는 값 전달, `MATHF_CALL`(`__vectorcall`), `MATHF_INLINE`(강제 인라인) 필수.
2. 컴파일 타임 레인 접근은 `Lane`/`SetLane`만 사용 — 원시 `m128_f32` 직접 접근 금지.
3. 저장 타입(`Float3` 등)을 연산에 직접 쓰지 않는다. 반드시 `VecReg`로 load 후 연산.
4. 새 연산 추가 시 `static_assert` 패리티 + DXMath 어셈블리 대조를 함께 넣는다.

## 재현 방법

```bat
spike\run_spikes.bat
spike\run_codegen.bat
```
```powershell
pwsh -File spike\extract_asm.ps1 -AsmPath "$env:LOCALAPPDATA\MathfSpike\codegen_msvc.asm" -Flavor msvc
pwsh -File spike\extract_asm.ps1 -AsmPath "$env:LOCALAPPDATA\MathfSpike\codegen_clang.asm" -Flavor clang
```
