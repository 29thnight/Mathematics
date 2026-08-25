# Mathematics 라이브러리 구현 계획

> 목표: DirectXMath와 동급 성능을 내면서, C++20/23의 모던 기능을 활용한
> 플랫폼 중립(x64 SSE/AVX, ARM NEON) 게임 수학 라이브러리.

작성일: 2026-08-24

## 현재 상태

- **기능 구현:** Phase 0~5 완료. 공개 API, 기하, 포맷, 가이드까지 존재한다.
- **공개 이름:** 라이브러리 `Mathematics`, include root `mathematics/`, namespace `math`.
  타입·함수·변수·열거형 값은 `lower_snake_case`를 사용하며 이전 이름의 호환층은 두지 않는다.
- **정확성:** 로컬 5개 프리셋에서 260개 테스트 통과
  (scalar의 아키텍처 감지 1개는 의도적 skip).
- **릴리스 판정:** 미통과. `cross`/전치/쿼터니언 곱 회귀와 80% line coverage
  자동화는 닫혔지만 clang-cl `matrix4x4` 곱 처리량과 전체 성능 표의 CI 자동화가
  남아 있다. 기능 Phase의 완료와 릴리스 게이트 통과를 같은 말로 사용하지 않는다.
- **자동 게이트:** MSVC·clang-cl에서 수정된 5개 latency/throughput 비교를 DXMath 대비
  5%로 강제한다. GCC/gcovr에서는 x86 활성 `include/mathematics` line coverage 80%를
  강제한다. scalar/SSE2/NEON은 별도 correctness matrix가 담당하며 coverage 합산
  대상은 아니다.
- **열린 성능 항목:** clang-cl `matrix4x4` 곱 처리량. 최신 수치는
  [BASELINE.md](BASELINE.md)에 있다.

---

## 1. 목표와 비목표

### 목표
- **성능**: 핵심 연산(vec4 dot/cross/normalize, mat4 곱, quat 곱/회전)에서
  DirectXMath 대비 ±5% 이내. 벤치마크로 상시 검증.
- **모던 API**: concepts, `constexpr`/`consteval`, `std::bit_cast`,
  `[[nodiscard]]`, hidden friend 연산자, structured bindings, `std::format` 지원.
- **플랫폼 중립**: MSVC / Clang / GCC, x64(SSE2~AVX2) / ARM64(NEON) + 스칼라 폴백.
- **컴파일 타임 계산**: 전 API가 `constexpr` 문맥에서 동작
  (DirectXMath가 못 하는 차별점).
- **헤더 온리**: 통합 비용 최소화. CMake INTERFACE 타깃 제공.

### 비목표
- 대형/동적 크기 행렬 (Eigen의 영역 — 다루지 않음)
- double 정밀도 SIMD (1차 릴리스는 float 중심, API만 확장 가능하게 설계)
- C++ 모듈 배포 (헤더 온리 우선, 모듈은 후순위 실험)

---

## 2. 핵심 설계 결정

### 2.1 언어 표준: **C++20 기본선, C++23 조건부 활용**
- C++20 필수: concepts, `std::bit_cast`, `std::is_constant_evaluated()`,
  `[[likely]]`, 지정 이니셜라이저, 3방향 비교.
- C++23 감지 시 추가 활용: `if consteval`, `std::unreachable()`,
  다차원 `operator[]` (`m[r, c]`), `static operator()`.
- 이유: MSVC/Clang의 C++23 지원이 아직 고르지 않음. 기능 매크로로 점진 적용.

### 2.2 성능 아키텍처: DirectXMath의 핵심 트릭 계승
DXMath가 빠른 이유는 인트린식 자체가 아니라 **레지스터 타입과 저장 타입의 분리**다.
이를 그대로 계승하되 모던하게 포장한다.

| 계층 | 타입 | 역할 |
|------|------|------|
| 레지스터 | `math::vec_reg` (= `__m128`/`float32x4_t` 래퍼) | 연산 통화. 항상 값 전달, 레지스터에 상주 |
| 저장 | `math::vector2/3/4`, `matrix4x4` 등 POD | 메모리 레이아웃. 구조체 멤버/배열용 |
| 편의 | `math::vector3/vector4/quaternion/matrix4x4` | 저장+연산 통합. 내부에서 load→연산→store |

- 핫 루프는 레지스터 타입으로 직접 작성 → DXMath와 동일 코드젠.
- 일반 코드는 편의 타입 사용 (SimpleMath 포지션). 단 연산자 체이닝 시
  표현식 내에서 load/store가 융합되도록 인라이닝 보장.
- 호출 규약: MSVC `__vectorcall`, SysV는 기본 벡터 ABI. 매크로 `MATHEMATICS_CALL`.
- 강제 인라인: `MATHEMATICS_INLINE` (`__forceinline` / `[[gnu::always_inline]]`).
- FMA 사용 가능 시 dot/mat 곱에 `fmadd` 경로.
- DXMath의 `Est` 계열에 대응하는 `_est` 접미사의 근사 변형 제공 (`normalize_est` 등, rsqrt 기반).

### 2.3 constexpr 이중 경로
```cpp
constexpr vec_reg add(vec_reg a, vec_reg b) noexcept {
    if (std::is_constant_evaluated()) {
        return scalar_add(a, b);      // 컴파일 타임: 스칼라 경로
    }
    return simd_add(a, b);            // 런타임: 인트린식 경로
}
```
- `vec_reg`를 union이 아닌 `std::bit_cast` 가능한 구조로 설계해
  constexpr 경로에서 UB 없이 동작.
- 주의: `__m128`은 constexpr 문맥에서 직접 못 만짐 → 레지스터 래퍼가
  컴파일 타임에는 `float[4]` 표현을 쓰도록 분기하는 것이 이 설계의 최난점.
  Phase 1에서 가장 먼저 검증한다 (스파이크).

### 2.4 SIMD 백엔드 계층
```
include/mathematics/arch/
  ├─ simd_scalar.hpp   # 항상 존재하는 폴백 + constexpr 경로
  ├─ simd_sse.hpp      # SSE2 기본, SSE4.1/AVX/AVX2+FMA 조건부
  ├─ simd_neon.hpp     # ARM64 NEON
  └─ simd_select.hpp   # 컴파일 타임 ISA 선택 (런타임 디스패치 없음)
```
- 백엔드 API는 자유 함수 집합(add/mul/fma/shuffle/cmp/hadd...)으로 통일.
- 런타임 디스패치는 하지 않음. DXMath와 동일 철학이고 호출 오버헤드를 제거한다.
- `std::simd`(C++26 예정)와 개념적으로 호환되는 형태로 설계해 미래 이관 여지 확보.

### 2.5 수학 관례 (DXMath 호환)
- **row-major 행렬, row-vector 곱 (`v * M`)**: DXMath와 동일. 패리티 테스트와
  벤치마크 비교가 쉬워지고 DirectX 사용자 이주가 자연스러움.
- 좌표계: LH/RH 함수를 모두 제공하되 접미사로 명시
  (`perspective_fov_lh`/`perspective_fov_rh` 스타일). 기본(무접미사)은 제공하지 않음. 모호성 제거.
- 각도는 라디안. `degrees`/`radians` 강타입 래퍼는 선택 검토(Phase 5).

### 2.6 모던 API 표면
- `namespace math`, 파일은 기능별 소분할 (200~400줄, 사용자 규칙 준수).
- concepts: `math::arithmetic`, `math::vector_type` 등으로 제네릭 유틸 제약.
- 전 함수 `[[nodiscard]]` + `noexcept`.
- structured bindings: `auto [x, y, z] = v;` (tuple protocol).
- `std::formatter<math::vector3>` 특수화 제공.
- 스위즐은 함수로 제공 (`v.xzy()`), 프록시 객체 스위즐은 성능/복잡도상 배제.
- 불변 스타일: 모든 연산은 새 값 반환. 변이형 멤버(`normalize in place`) 없음.

### 2.7 표현식 처리: 이름 있는 함수가 정본, 연산자는 얇은 편의 계층

**기본 원칙**: 모든 연산은 자유 함수로 정본(canonical)이 존재한다
(DXMath의 `XMVectorAdd` 포지션). 연산자 오버로드는 정본을 호출하는
hidden friend 인라인 별칭이다. 인라인 후 코드젠은 완전히 동일하다.

```cpp
// 정본
[[nodiscard]] MATHEMATICS_INLINE constexpr vector3 add(vector3 a, vector3 b) noexcept;
[[nodiscard]] MATHEMATICS_INLINE constexpr float dot(vector3 a, vector3 b) noexcept;

// 편의 계층 — hidden friend (ADL 오염 방지)
friend constexpr vector3 operator+(vector3 a, vector3 b) noexcept { return add(a, b); }
```

**연산자 제공 — 의미가 하나뿐인 것만:**

| 연산자 | 의미 |
|--------|------|
| `+`, `-`, 단항 `-` | 성분별 덧셈/뺄셈/부호 반전 |
| 스칼라 `*`, `/` | 스케일 |
| `vec * vec` | 성분별 곱 (HLSL 관례 — 셰이더 코드와 1:1 대응) |
| `v * M`, `M * M` | 행렬 곱 (row-vector 관례이므로 벡터가 왼쪽) |
| `q * q` | 해밀턴 곱 |
| `==` | 정확 비교 (근사 비교는 `near_equal(a, b, eps)` 함수로만) |

**함수만 제공 — 연산자로 쓰면 해석이 갈리는 것:**
- `dot`, `cross` (`*`/`^` 배정은 읽는 사람마다 해석이 갈리므로 배제)
- `normalize`, `lerp`, `reflect`, `transform` 등 의미론이 있는 연산 전부

표현식 템플릿(Eigen 방식)은 의도적으로 배제한다. 지연 평가는 대형 행렬의
임시 객체 할당을 없앨 때 가치가 있는 기술이다. 레지스터 하나에 들어가는 고정 크기
타입에서는 이득이 0이고 컴파일 시간·에러 메시지·디버깅만 악화된다.
128비트 값 타입 + 강제 인라인 조합에서 `a + b * c`는 함수 호출 체인과 동일한
어셈블리로 컴파일된다. Phase 0 코드젠 검사가 이를 검증한다.
DXMath의 `XM_NO_OPERATOR_OVERLOADS` 같은 연산자 비활성화 매크로는 두지 않는다.
연산자가 항상 정본 함수의 인라인 별칭이라 끌 이유가 없다.

---

## 3. 모듈(파일) 구성 계획

```
Mathematics/
├─ CMakeLists.txt              # INTERFACE 라이브러리 + 테스트/벤치 옵션
├─ include/mathematics/
│  ├─ mathematics.hpp          # 우산 헤더
│  ├─ config.hpp               # ISA 감지, MATHEMATICS_CALL/MATHEMATICS_INLINE, 표준 감지
│  ├─ arch/                    # §2.4 SIMD/상수 평가 백엔드
│  ├─ scalar.hpp               # clamp/lerp/saturate/wrap, 근사 삼각함수 등
│  ├─ vec_reg.hpp              # vec_reg + 레지스터 연산 (핵심 통화)
│  ├─ vector.hpp / vector2.hpp / vector3.hpp / vector4.hpp
│  ├─ matrix.hpp / matrix3x3.hpp / matrix4x4.hpp
│  ├─ quaternion.hpp
│  ├─ transform.hpp            # TRS 합성/분해, 뷰/투영 행렬 (LH/RH)
│  ├─ plane.hpp / ray.hpp / bounds.hpp / intersect.hpp / geometry.hpp
│  └─ format.hpp               # std::format 지원 (opt-in 헤더)
├─ tests/                      # GoogleTest, 파일 구성은 include 미러링
├─ bench/                      # Google Benchmark, vs DXMath/GLM 비교
├─ docs/
└─ .github/workflows/ci.yml    # Windows 5개 프리셋 + Linux GCC/Clang(x64/ARM64) + coverage
```

---

## 4. 검증 전략

### 4.1 정확성 (TDD, 커버리지 80%+)
- 각 연산마다 **스칼라 참조 구현 대비 속성 테스트**: SIMD 경로 결과가
  참조 구현과 ULP 허용치 내 일치하는지 무작위 입력으로 검증.
- **constexpr 패리티**: 동일 입력의 컴파일 타임 결과 == 런타임 결과 (`static_assert`).
- **DXMath 패리티** (Windows CI 한정): 동일 입력을 DirectXMath에 넣은 결과와
  비교하는 골든 테스트. 관례(row-major, LH/RH)가 맞는지 여기서 강제 검증.
- 경계값: NaN/Inf/영벡터 정규화, 짐벌락 근처 오일러 변환, 퇴화 행렬 역행렬.

### 4.2 성능 — DXMath 직접 비교 (필수)

본 라이브러리의 성능 기준선은 DirectXMath다. 모든 핵심 연산은 동일 조건에서
DXMath 대응 함수와 1:1 벤치마크로 직접 비교한다. 이 비교는 각 Phase의
완료 조건이며 선택 사항으로 두지 않는다.

**1:1 비교 대상 매핑 (Google Benchmark, 동일 입력·동일 반복 조건):**

| Mathematics | DirectXMath 대응 | 도입 Phase |
|-------|------------------|-----------|
| `add/mul` (vec_reg) | `XMVectorAdd/multiply` | 1 |
| `dot` (vector3/vector4) | `XMVector3Dot` / `XMVector4Dot` | 2 |
| `cross` | `XMVector3Cross` | 2 |
| `normalize` / `normalize_est` | `XMVector3Normalize` / `normalize_est` | 2 |
| `matrix4x4 * matrix4x4` | `XMMatrixMultiply` | 3 |
| `inverse(matrix4x4)` | `XMMatrixInverse` | 3 |
| `transpose(matrix4x4)` | `XMMatrixTranspose` | 3 |
| `q * q` | `XMQuaternionMultiply` | 4 |
| `slerp` | `XMQuaternionSlerp` | 4 |
| 10만 정점 배치 변환 | `XMVector3TransformCoordStream` | 4 |

**측정 조건:**
- Windows + MSVC 및 Windows + clang-cl 두 툴체인에서 측정
  (DXMath가 1차 시민인 환경에서 비교해야 공정).
- 동일 최적화 플래그(`/O2 /fp:fast /arch:AVX2` 등)를 양쪽에 동일 적용.
- 단발 연산의 마이크로벤치 + 배치 처리 스루풋 두 축으로 측정
  (인라인 소멸을 막기 위해 `benchmark::DoNotOptimize` 사용).
- 보조 비교 대상: GLM(aligned+intrinsics), 자체 스칼라 폴백.

**합격선**: 위 표의 전 항목에서 DXMath 대비 ±5% 이내. 미달 항목이 하나라도
있으면 해당 Phase 완료 불가, 릴리스 블록.

지연과 처리량 둘 다 게이트에 포함한다. Phase 0에서는 처리량 측정의 변동이
±15%라 제외했으나 Phase 1에서 두 결함(벡터 구성 비용을 재던 문제, 배치가 L2를
넘겨 대역폭에 묶이던 문제)을 고치자 변동이 1% 수준으로 떨어졌다
([BASELINE.md §2](BASELINE.md)).

GLM과 Sony Vectormath는 부가 비교 대상이며 게이트에 포함하지 않는다. 맥락을
제공할 뿐이고 릴리스를 막는 것은 DirectXMath 대비 수치뿐이다.

- CI에 벤치 결과를 기록해 회귀를 추적한다 (PR마다 비교 리포트).
- 코드젠 검사: 핵심 함수의 어셈블리에 call/불필요한 load-store가 없는지
  DXMath 동일 함수의 어셈블리와 나란히 비교 (MSVC `/FA`, clang `-S`).

---

## 5. 단계별 실행 계획

### Phase 0 — 스파이크 & 골격 (리스크 제거 최우선) — **완료 (2026-08-24)**
- [x] **스파이크: constexpr 이중 경로 검증** → **성공. 설계 변경 불필요.**
      전략 A(`struct vec_reg { __m128 v; }`)가 MSVC/clang-cl 양쪽에서 constexpr
      평가된다. 런타임 어셈블리는 DXMath와 명령어 단위로 동일하다.
      상세: [SPIKE-RESULTS.md](SPIKE-RESULTS.md)
- [x] CMake 골격, GoogleTest/Benchmark/GLM 연동, CI 파이프라인
      (MSVC Release/Debug · clang-cl · 스칼라 폴백 · Linux gcc/clang · Linux ARM64)
- [x] `config.hpp`: ISA/표준 감지, `MATHEMATICS_CALL`/`MATHEMATICS_INLINE`/`MATHEMATICS_IF_CONSTEVAL`
- [x] `vec_reg.hpp` 최소 구현 (스파이크가 검증한 연산만) + 3중 검증 테스트 14개
- [x] 벤치 하네스 배선 및 DXMath/GLM 기준선 확보 →
      [BASELINE.md](BASELINE.md)

**Phase 0에서 얻은 추가 규칙 (Phase 1+에 적용)**
1. 컴파일 타임 레인 접근은 반드시 `lane`/`set_lane` 경유 (MSVC는 union, Clang은
   네이티브 벡터다. 직접 접근하면 한쪽이 깨진다).
2. 저장 타입을 연산에 직접 쓰지 않는다. `float[4]` 기반 값 타입은 `__vectorcall`
   HFA 분해로 ABI 경계마다 재조립 비용이 발생한다(측정: 명령 2개 → 14개).
3. 테스트는 `/fp:precise`, 벤치는 `/fp:fast`. `/fp:fast`에서는 컴파일러가
   테스트의 참조 구현을 FMA 축약·역수 근사로 재작성해 오라클이 피검체보다
   부정확해진다(측정: MSVC 덧셈 ~8 ULP, clang 나눗셈 ~2 ULP 이탈).
4. 되먹임 사슬 마이크로벤치는 수치적 고정점이어야 하며 종료 시 누산기 범위를
   가드해야 한다. 아니면 연산이 아니라 비정규수 스톨을 측정하게 된다.
5. `dot`은 SSE4.1 `dpps` 경로가 필수다. 없으면 DXMath에 진다.

### Phase 1 — SIMD 백엔드 + vec_reg — **완료 (2026-08-24)**
- [x] `arch/reg.hpp` — `vec_reg`, 레인 접근, 비트 헬퍼
- [x] `arch/consteval_ops.hpp` — 모든 연산의 의미를 한 번만 정의한다. 각 백엔드의
      constexpr 분기이자 스칼라 폴백이자 테스트 오라클로 동시에 쓰인다
- [x] `arch/simd_sse.hpp` (SSE2 기준, SSE4.1 dpps / FMA 조건부)
- [x] `arch/simd_neon.hpp` — CI에서 AArch64 Linux(GCC·Clang) 검증 완료.
      MSVC/ARM64(`__n128`) 경로만 아직 미검증
- [x] `arch/simd_scalar.hpp`, `arch/simd_select.hpp`
- [x] 연산 45종: 산술·비교·비트·select·셔플·수평·적재/저장·술어
- [x] 테스트 48개 (3중 검증: constexpr / consteval_ops / DirectXMath)
- [x] 벤치 5종 전부 DXMath ±1% 이내 ([BASELINE.md](BASELINE.md))

**Phase 1에서 얻은 규칙**
1. **MSVC에서 C++23 `if consteval`은 성능 필수 조건이다.** C++20 대체 구현은
   죽은 컴파일 타임 분기의 union 접근 때문에 런타임 표현을 오염시켜 최대 2.2배
   느려진다. 빌드는 C++23을 자동 감지해 사용하고, 안 되면 헤더가 경고한다.
   상세: [SPIKE-RESULTS.md §6](SPIKE-RESULTS.md)
2. 백엔드 간 의미가 갈리는 지점(`min`/`max`의 NaN)은 통일하지 않고 명시적으로
   테스트에 고정한다. DXMath도 같은 선택을 한다.
3. `minps(a,b)`는 `a < b ? a : b`다. 동등해 보이는 `b < a ? b : a`로 쓰면 NaN에서만
   갈라지며 랜덤 입력 패리티는 통과한다. 실제로 그렇게 놓쳤다가 스칼라 빌드의
   NaN 테스트에서 잡혔다.
4. 상쇄가 일어나면 결과를 기준으로 잡은 상대 오차는 무의미해진다. 내적 비교의
   허용오차는 항들의 크기에 비례해야 한다.
5. 컴파일 타임 레인 접근은 컴파일러마다 제약이 정반대다. MSVC는 인트린식 타입이
   union이라 멤버를 직접 쓸 수 있지만 `std::bit_cast`가 막힌다(union 멤버 포함).
   Clang·GCC는 네이티브 벡터 타입이라 첨자 접근이 상수 표현식이 아니지만
   `bit_cast`는 된다. 두 제약이 정확히 상보적이라 백엔드별로 갈라 쓴다.
   로컬 clang은 첨자 접근을 허용해서 CI 전까지 드러나지 않았다.

### Phase 2 — 벡터 타입 — **완료 (2026-08-24)**
- [x] `vector2/3/4` — 사용자 대면 타입 (패킹 8/12/16바이트, 표준 레이아웃)
- [x] `vector_common.hpp` — 세 타입이 공유하는 연산을 concept 제약 템플릿으로 1회 정의
- [x] 산술·비교·abs/min/max/clamp/saturate/lerp·dot/length/distance·
      normalize/normalize_est·reflect/refract·cross(3D 벡터, 2D 스칼라)·perpendicular
- [x] 테스트 37개 추가 (총 85개) — constexpr·손계산·DirectXMath 3중 검증
- [x] 벤치 검증: `vector3` 연쇄가 DXMath `XMVECTOR`와 동일, normalize는 32% 우위

**Phase 2에서 얻은 규칙**
1. 2·3성분 레인별 연산은 스칼라가 빠르다. 폭이 좁으면 SIMD 승격이 손해다
   (pack 1 + extract 3 > SIMD 1회 이득). 4성분과 축약 연산(dot/length/cross)은 SIMD.
2. **미사용 레인이 0이면 벡터 나눗셈이 constexpr에서 막힌다.** `vector3` 나눗셈은
   w 레인에서 0/0이 되는데 런타임에는 버려지는 NaN이지만 상수 평가에서는 정의되지
   않은 연산이라 컴파일이 중단된다. 제수의 미사용 레인을 1로 채워 해결했다.
   `select`는 분기가 아니라 블렌드라 양쪽이 모두 계산된다는 점도 같은 함정
   (`normalize`의 0 길이 나눗셈)을 만든다.
3. constexpr용 스칼라 구현을 런타임에 부르지 않도록 주의한다.
`consteval_ops::sqrt_scalar`는 뉴턴법이라 런타임에 4.6배 느리다.

**명명**: `vector2/3/4` (초안의 `vec2/3/4`에서 변경). DirectXMath의 `XMFLOAT3`보다
SimpleMath의 `vector3`에 가까운 이름이 사용자에게 익숙하다.

**저장 방식 결정: 패킹된 저장 + 연산자** (레지스터 래핑이 아님)

`vector3`는 `float x, y, z`를 직접 갖는 표준 레이아웃 타입이다. 레지스터 래핑도
검토했으나 두 가지가 결정적이었다:
1. `v.x`로 멤버에 직접 접근할 수 있어야 한다. 레지스터 기반이면 `v.get_x()`가 되는데
   DXMath가 `XMVectorGetX`로 겪는 바로 그 불편함이다.
2. 구조체 멤버·정점 버퍼에 그대로 들어가야 한다. 12바이트 패킹이어야 하고
   16바이트 정렬 레지스터로는 불가능하다.

**연산 방식: 폭에 따라 갈라진다.** 측정 결과가 그렇게 나왔다. 설계 취향으로
고른 갈래는 아니다.

처음 가정은 "모든 연산을 `vec_reg`로 승격하고, 강제 인라인이 load/store를 접어줄
것"이었다. 틀렸다. 연쇄 표현식 `a * b + c`를 측정하니:

| | 지연 |
|---|---|
| Mathematics `vector3` (전부 SIMD 승격) | 5.25 ns |
| DXMath `XMFLOAT3` (매 단계 load/store) | 5.53 ns |
| DXMath `XMVECTOR` (레지스터 상주) | **2.24 ns** |
| GLM `vec3` (스칼라) | 3.21 ns |

승격 방식은 `XMFLOAT3`와 같은 급이었고 레지스터 형태보다 2.3배 느렸다. GLM의 스칼라
`vec3`에도 졌다. 3성분에서는 pack 1회 + extract 3회 비용이 SIMD 1회 연산의 이득을
넘어선다.

그래서 폭에 따라 나눴다:
- **`vector2`·`vector3`의 레인별 산술과 `normalize`**: 스칼라. 성분들이 서로 독립적인
  의존 사슬을 이루므로 파이프라이닝되어 결과적으로 SIMD 1회와 같은 지연이 된다.
- **`vector4`**: SIMD. 4레인이 다 차고 16바이트 적재/저장이 한 번에 되므로 승격이 이득.
- **`dot`·`length`·`cross`**: 폭과 무관하게 SIMD. 왕복당 작업량이 충분히 크다.

결과: `vector3` 연쇄는 2.28 ns다. DXMath의 레지스터 상주 `XMVECTOR`와 정확히
동일하다. 12바이트 패킹을 유지하면서 레지스터 타입의 성능을 낸다.

> **함정 기록**: 스칼라 `normalize`를 처음 넣었을 때 4.6배 느려졌다.
> `consteval_ops::sqrt_scalar`(constexpr용 뉴턴법)가 런타임에 돌고 있었기 때문이다.
> 런타임 경로는 반드시 `std::sqrt`로 가야 한다.

핫 루프에서 함수 경계를 넘나드는 코드는 여전히 `vec_reg`를 직접 쓴다.

### Phase 3 — 행렬 — **기능 완료 (2026-08-24), clang 곱 처리량 미통과**
- [x] `matrix4x4`, `matrix3x3` — row-major, row-vector (DXMath 관례)
- [x] 곱셈·전치·행렬식·역행렬·`v * M`·`transform_point`/`transform_direction`
- [x] 테스트 41개 추가 (총 126개). 관례 자체를 DXMath와 대조한다.
      저장 순서, 행벡터, 합성 순서, 이동 성분 위치를 각각 고정
- [x] mat4 곱셈 벤치 합격: 지연 5.51ns 대 5.66, 처리량 334 대 337 M/s (동률)
- [x] SIMD 역행렬 — clang에서 DXMath보다 24% 빠름(101.0 대 81.2 M/s),
      MSVC에서 6% 빠름(72.8 대 68.4). 두 컴파일러 모두 GLM보다 빠름
- [x] 전치 회귀 해결 — 반환 임시를 제거하는 비공개 runtime row factory로 MSVC
      529.0 대 515.1 M/s, clang-cl 515.1 대 419.4 M/s
- [x] 코드 리뷰 — 실제 버그 1건, 경로 불일치 1건 수정. 아래 참조
- [ ] 곱셈은 clang에서 DXMath가 앞선다(335 대 489 M/s). 같은 소스이므로 최적화
      품질 차이로 보이나 원인 미규명

**Phase 3 코드 리뷰에서 나온 것 (2026-08-24)**

리뷰는 세 갈래로 돌렸다. 코드 리뷰어(레인 대수 검증), 테스트 분석기(경로 커버리지),
그리고 배정밀도 Gauss-Jordan 오라클(여인수 전개와 대수를 공유하지 않는 독립 기준)이다.

- **실제 버그**: `near_equal`이 NaN을 통과시켰다. `diff > eps || diff < -eps`는
  NaN에 대해 두 비교 모두 거짓이라 "같다"로 떨어진다. 전 NaN 행렬이 항등행렬과
  근사 동일로 보고됐고 더 나쁘게는 이 파일의 거의 모든 역행렬·곱셈 단언이
  `near_equal`을 거치므로 NaN을 만드는 버그가 테스트를 통과시켰다. 긍정형
  (`!(diff <= eps && diff >= -eps)`)으로 수정했다. 벡터판은 `cmp_le`의 순서 비교 덕에
  처음부터 옳았다. Phase 3에서 스칼라 루프로 손으로 옮기며 생긴 회귀다.
- **경로 불일치**: 비유한 입력. `inf`가 든 행렬에서 SIMD는 0을, 스칼라는 NaN을
  냈다. 스칼라는 상수 평가와 스칼라 빌드가 쓰는 경로이므로 `inverse`가 컴파일
  타임과 런타임에 다른 답을 냈다. 판정을 `det == 0`에서 `is_finite_non_zero(det)`로
  바꿔 해소했다. 성능 비용은 측정되지 않았다(69.4 → 72.8 M/s, 잡음).
- **고치지 않고 계약에 명시한 것**: 특이점 근처의 발산. 두 경로는 행렬식을 다른
  전개로 구하므로 `== 0` 판정이 갈린다. 반올림까지 특이한 행렬 200만 개에서
  23%가 어긋났다(리뷰어가 모델로 예측한 24%와 일치). 같은 전개를 공유시켜도
  23% → 20%였고 FMA 축약이 남아 더는 줄지 않는다. 실제로 발생하는 특이 행렬은
  양쪽 모두 정확히 0이 되어 일치하므로 고치는 대신 한계를 문서화했다.
- **거짓 양성 2건.** (1) MSVC `__forceinline`이 `inline` 없이 ODR을 깨뜨린다는
  지적이 있었다. 2개 TU를 Debug `/Ob0`와 Release 양쪽에서 링크해 확인한 결과
  문제없다. (2) 행렬식 언더플로로 역행렬이 NaN이 된다는 제 초기 판단도 있었다.
  재현에 쓴 행렬이 w까지 축소한 비현실적 형태였고 DXMath도 동일하게 동작한다.
- 레인 대수는 독립적으로 두 번 확인됐다. 리뷰어가 16개 수반행렬 항을 손으로
  전개해 스칼라판과 대조했고 오라클이 난수 196,722개 × 3개 백엔드에서 배정밀도
  기준과 대조했다. AVX2에서는 SIMD가 스칼라보다 정확하다(1.97e-05 대 4.78e-05).

**Phase 3에서 얻은 규칙**
1. `splat_x/y/z/w`는 셔플 포트를 먹는다. 행렬 곱셈처럼 브로드캐스트가 16개
   필요한 곳에서는 셔플 포트(1개)가 FMA보다 먼저 포화된다. 값이 메모리에 있다면
   `load_splat`으로 적재 포트에 넘겨야 한다 (141 → 251 M/s).
2. **128비트만으로는 못 따라잡는 지점이 있다.** DXMath의 `XMMatrixMultiply`는
   AVX2 256비트로 두 행을 동시에 처리한다. 알고리즘이 절반의 연산을 하면 조율로는
   메울 수 없다 (251 → 334 M/s). 경쟁 대상의 구현을 읽어보는 것이 추측보다 빨랐다.
3. 런타임 인덱스 루프는 결과를 스택으로 밀어낸다. 행 단위 연산은 언롤한다.
4. 역행렬 안에서 `transpose()`를 부르면 16개 float을 저장했다가 즉시 다시 적재한다.
   중간 결과를 `matrix4x4`로 되돌리지 마라. 전치를 레지스터에서 수행하니
   47.5 → 56.2 M/s였다. 값이 `vec_reg`로 흐르는 구간에서는 그대로 두어야 한다.
5. 셔플로 무언가를 재배열해야 한다면, 그 재배열을 이미 필요한 다른 연산에
   흡수시킬 수 있는지 먼저 본다. 역행렬의 열 순열 (1,0,3,2)는 전치의 입력 순서를
   바꾸는 것만으로 공짜가 되어 셔플 4개를 없앴다.
6. 부동소수 비교는 긍정형으로 쓴다. `if (diff > eps || diff < -eps) 실패`는
   NaN에서 통과하고 `if (!(|diff| <= eps)) 실패`는 걸러낸다. 두 형태는 유한
   입력에서 동치이므로 테스트가 잡아주지 않는다. SIMD 비교 명령의 순서 비교
   의미론을 스칼라로 옮길 때 특히 위험하다. 벡터판은 옳았는데 손으로 옮긴
   행렬판만 틀렸다.
7. 한 연산에 구현이 둘이면, 정상 입력에서 일치한다는 것으로 충분하지 않다.
   퇴화·비유한 입력에서 갈리는지 반드시 확인한다. 상수 평가는 스칼라 경로를 쓰므로
   그 발산은 곧 "컴파일 타임과 런타임이 다른 답을 낸다"가 된다.
8. 전개 방식과 FMA 축약이 다르면 `det == 0` 같은 칼날 판정이 갈린다. 여인수
   전개는 특이점 근처에서 구현끼리 일치시킬 수 없다. 맞추려 들기보다 계약에
   명시하는 편이 정직하다.
9. 자기 대수를 공유하지 않는 오라클을 하나 둔다. 두 구현이 같은 유도에서
   나왔다면 서로 대조해도 공통 오해는 살아남는다. 배정밀도 Gauss-Jordan은 여인수
   전개와 무관하므로 그 역할을 한다. 외부 라이브러리가 없는 ARM64 CI에서 특히 그렇다.

### Phase 4 — 쿼터니언 & 변환 — **완료 (2026-08-24)**
- [x] `scalar.hpp` — 상수와 삼각함수. 계획에 없었으나 필수였다: 축각·오일러·투영이
      전부 sin/cos/tan/atan2를 필요로 하는데 표준 라이브러리 것은 C++26까지
      상수 평가가 안 되고 "전 API가 constexpr"은 이 프로젝트의 목표다
- [x] `quaternion` — mul/conjugate/inverse/slerp/nlerp/축각/오일러/행렬 변환
      (`quat`이 아니라 `quaternion`: Phase 2에서 `vec`→`vector`로 정한 명명과 일관)
- [x] `transform.hpp` — TRS 합성·분해, look_at/look_to/perspective/ortho,
      전부 LH/RH 접미사 필수. 기본값을 두면 씬이 조용히 거울상이 된다
- [x] 테스트 70개 추가 (총 196개). 짐벌락, 특이 입력, Shepperd 4개 분기,
      투영 깊이 범위, constexpr 대조 포함
- [x] 관례를 DirectXMath에서 관측해 확정 (추측 금지):
      곱셈 순서, 오일러 축·순서, 깊이 범위, TRS 순서
- [x] 벤치 (교차 실행 중앙값): 쿼터니언 곱 처리량 571 대 556 M/s (동률),
      ToMatrix 222 대 161 (+39%), compose 189 대 130 (+46%), slerp 37.2 대 30.6 (+22%),
      rotate 201 대 177 (+13%)
- [x] 코드 리뷰 — 실제 버그 1건, 정책 불일치 1건, 제가 만든 성능 회귀 1건. 아래 참조
- [x] 쿼터니언 곱 지연 회귀 해결 — 고정 상수 최적화가 한쪽에만 적용되던 벤치를
      불투명 입력으로 고치고 DXMath와 같은 의존성 트리를 사용했다. MSVC 5.41 대
      5.89ns, clang-cl 5.23 대 5.23ns (패킹 형태 DXMath 기준)
- [x] `sin_cos`는 DXMath의 `XMScalarSinCos`보다 27% 느리다(184 대 252 M/s). 범위 축소를
      double로 하기 때문이다. 그 대가로 정확도가 전 구간 2.7e-07로 평탄하다
      (DXMath는 큰 각도에서 5e-06대). 게이트 항목은 아니고 의도한 교환이다

**Phase 4 코드 리뷰에서 나온 것 (2026-08-24)**

- **실제 버그**: `atan2(±inf, ±inf)`가 NaN이었다. `inf/inf`는 IEEE-754에서 NaN이라
  `atan`의 NaN 가드에 걸렸다. `std::atan2`는 네 사분면 대각선을 정의한다. 명시적
  분기로 수정했다. 헤더가 "C 라이브러리와 같이 정의된다"고 약속하고 있었으므로 계약 위반이다.
- **정책 불일치**: `up`이 `forward`와 평행할 때 뷰 행렬이 조용히 망가졌다. 외적이
  0 벡터가 되고 `normalize`가 그대로 돌려주어 기저 두 축이 0인 행렬이 나왔다.
  월드 up으로 바로 위를 보는 흔한 실수가 정확히 이 경우다. 라이브러리 정책(특이
  행렬의 `inverse`, 0 벡터의 `normalize`)에 맞춰 항등행렬을 반환하도록 수정했다.
- 부호 있는 0이 `asin`/`atan`을 통과하며 소실됐다. 작은 인자 조기 반환으로 수정했다.
  이 반환은 부호뿐 아니라 상대 정확도도 고친다. 1e-6 근처에서 `half_pi - acos_core(x)`는
  두 큰 값의 상쇄로 절대 2e-7, 즉 상대 25% 오차를 냈고 작은 회전의
  `to_axis_angle` 각도를 잡음으로 만들고 있었다.
- 제가 만든 회귀 하나. 위 부호 문제를 `abs_scalar`의 비트 연산화로 고치려다
  slerp를 22%, sin_cos를 8% 느리게 만들었다. `bit_cast`가 x86에서 범용 레지스터를
  왕복하는데 이 함수는 `reducible()`을 통해 모든 삼각함수 호출 경로에 있다.
  되돌리고 조기 반환으로 해결했다. 벤치 없이 "더 올바른" 형태로 바꾸면 이렇게 된다.
- **리뷰어가 손 유도로 무결 판정한 것**: SIMD 해밀턴 곱 16항, `to_euler` 유도와
  양 극의 짐벌락 분기, Shepperd 4개 분기의 손잡이(켤레 뒤집힘 없음), slerp의
  `a == -b` 경계, `decompose`의 음수 행렬식 접기.
- 테스트 196 → 203. 반사와 비자명 회전의 조합(Phase 3의 identity×identity 구멍과
  같은 모양), `slerp`/`nlerp`/`to_euler`/`to_axis_angle`/`decompose`/`look_to_*`/`orthographic_*`의
  constexpr 증명, `tan`의 문서화된 경계까지의 정확도, 작은 각도 `to_axis_angle`.

**Phase 4에서 얻은 규칙**
1. 관례는 관측한다. 쿼터니언 곱 순서, 오일러 축 순서, 깊이 범위, TRS 순서를
   DirectXMath에 직접 물어 확정했다. 넷 다 "그럴듯한 오답"이 존재하고 작은 각도에서는
   틀린 쪽도 정상으로 보인다. `to_euler`는 손으로 유도했다가 세 항의 부호를 전부
   틀렸고 왕복 테스트가 잡았다.
2. 삼각함수는 0 근처에서 정확하고 멀어지면 무너지는 것이 전형적 실패 모드다.
   손댄 적 없는 축을 테스트한다. 실제로 첫 구현이 |x|가 크면 3.1e-02, atan은 큰
   인자에서 3.5e-04였다. 작은 입력만 쓸어보는 테스트는 둘 다 통과시킨다.
3. **ULP는 영점 근처에서 못 쓴다.** `cos(pi/2)`는 8e-05라 1 ULP가 6e-12이고, FMA
   한 번 차이(2e-08)가 3527 ULP로 보인다. ULP와 절대 오차 중 둘 중 하나만 만족하면
   통과시키는 척도가 맞다.
4. 범위 축소는 double로 한다. float 2π로 빼면 그 표현 오차에 몫이 곱해져 축소된
   각도를 오염시키고 다항식은 엉뚱한 수의 사인을 정확히 계산한다. Cody-Waite 2항
   분할이 1e5까지 버티고 double 곱셈-뺄셈 한 번이 전 구간을 해결한다.
5. 이 기계는 부하가 흔들려 같은 벤치가 실행 간 20%까지 움직였다. 측정은 교차
   실행으로 한다. 교차하지 않으면 그 드리프트가 라이브러리 간 차이로 둔갑한다.
6. 불변 입력을 도는 배치 벤치는 통째로 사라질 수 있다. clang이 DXMath의 slerp
   루프를 제거해 523 G/s를 냈다. 확인하지 않으면 승리로 읽히는 수치다. 반복 시작에
   메모리 배리어를 넣고 그래도 사라지면 그 조합은 비교 불가로 기록한다.
7. 입력과 출력 배열의 상대 페이지 오프셋을 통제한다. 4K 앨리어싱은 같은
   바이너리를 프로세스마다 47/54/74 M/s로 갈라놓았고 Phase 3의 "재현되지 않는
   수치"가 바로 이것이었다.
8. 타입이 개념을 공유하지 않으면 concept도 공유하면 안 된다. `quaternion`에
   `lane_count`를 두지 않아 `vector_like`에서 제외했다. 넣었다면 벡터의 성분별 `operator*`가
   해밀턴 곱과 경쟁했을 것이다. 컴파일되고 거의 맞게 렌더되는 종류의 버그다.

### Phase 5 — 기하 & 마감 — **기능 및 감사 완료 (2026-08-24), 전체 릴리스 게이트 미통과**
- [x] 기하: `plane.hpp` / `ray.hpp` / `bounds.hpp` / `intersect.hpp`,
      우산 `geometry.hpp`. 관례는 DirectXMath·DirectXCollision에서 관측해 확정
- [x] `format.hpp` — `std::format` 지원. 우산 헤더에서 제외했다: `<format>`은
      무겁고 출력하지 않는 번역 단위까지 비용을 물릴 이유가 없다.
      `<format>` 없는 툴체인에서는 `MATHEMATICS_HAS_FORMAT`으로 비활성화
- [x] structured bindings — 추가 코드 없이 이미 동작한다. 패킹 타입이 전부
      공개 데이터 멤버만 가지므로 언어 규칙이 그대로 적용된다. tuple 프로토콜은
      넣지 않았다(YAGNI). 잃어버리지 않도록 테스트로 고정
- [x] `docs/GUIDE.md` — 사용 가이드와 DXMath 이주표(50항목).
      모든 예제와 주장을 `tests/guide_test.cpp`가 컴파일·실행 검증한다
- [x] 최종 성능 감사 — `docs/BASELINE.md` §6
- [x] 테스트 203 → 260
- [x] 기하 회귀 3건 수정 — 스케일된 plane 투영/반사, 기본 aabb의 진짜 empty identity,
      방향 0인 내부 aabb raycast. 퇴화·NaN 경계와 문서 예제까지 회귀 테스트로 고정
- [x] 성능 CI 게이트 — MSVC·clang-cl에서 `cross` 지연/처리량, 전치,
      쿼터니언 곱 지연/처리량의 9회 무작위 교차 중앙값을 DXMath 대비 5%로 강제
- [x] 커버리지 CI 게이트 — `-fkeep-inline-functions`로 사용되지 않은 inline API도
      분모에 남기고 GCC/gcovr로 x86 활성 `include/mathematics` line coverage 80% 강제

**Phase 5에서 얻은 규칙**
1. 문서의 예제는 테스트다. 컴파일되지 않는 문서는 없느니만 못하다. 자신 있게
   틀리고 독자는 믿는다. 가이드의 예제와 표(크기, 퇴화 입력, 이주 대응)를 전부
   실행 검증으로 옮겼다.
2. 감사는 "측정하고 있기는 한가"부터 묻는다. "측정치가 좋은가"는 그다음 질문이다.
   게이트 대상 세 항목(`cross`, `transpose`, 배치 정점 변환)에 벤치가 아예 없었고
   추가하자 둘이 미달이었다. 감사가 없었으면 "전 항목 통과"라고 적을 뻔했다.
3. **베낄 수 없는 관례도 있다.** 레이가 볼륨 안에서 시작할 때 DirectXMath는
   구에서 양수 탈출 거리를, 상자에서 음수 진입 거리를 반환한다. 자기들끼리
   어긋나서 어느 쪽도 맞출 수 없다. 0으로 통일하고 그 이유를 헤더에 적었다.
4. 탐침도 코드다. DXMath 규약을 읽어낸 첫 탐침이 `printf` 인자에서
   `intersects()` 호출과 출력 인자 읽기의 순서를 미지정으로 두어 "안에서 시작하면
   거리 0"이라는 틀린 결론을 냈다. 관례를 관측할 때 관측 장치부터 의심해야
   한다.

각 Phase 말미에 코드 리뷰를 수행했다. 커버리지 80%는 이제 CI에서 측정·강제한다.
독립적인 로컬 MSVC 계측도 1,462/1,466 line(99.73%)을 기록했다. 전체 릴리스 판정은
남은 clang-cl 행렬 곱 처리량과 아직 자동화하지 않은 성능 표 항목 때문에 미통과다.

---

## 6. 리스크와 대응

| 리스크 | 영향 | 대응 |
|--------|------|------|
| constexpr 래퍼가 런타임 코드젠을 해침 (레지스터 스필) | 성능 목표 실패 | Phase 0 스파이크로 최우선 검증. 실패 시 constexpr을 스칼라 타입 한정으로 축소 |
| MSVC/Clang 간 인라이닝·`__vectorcall` 차이 | 플랫폼별 성능 편차 | CI에서 컴파일러별 벤치 결과 분리 추적, 코드젠 검사 자동화 |
| NEON에 없는 x86 셔플 패턴 | NEON 경로 성능 저하 | 백엔드 API를 셔플 나열이 아닌 의미 단위 연산(hadd, dot 등)으로 설계 |
| OneDrive 폴더의 빌드 산출물 동기화 지연/충돌 | 빌드 불안정 | 빌드 디렉터리를 OneDrive 밖(`%LOCALAPPDATA%` 등)으로 두거나 동기화 제외 설정 |
| 헤더 온리 + 강제 인라인으로 컴파일 시간 증가 | DX 저하 | 우산 헤더 대신 세분화 헤더 사용 권장, 벤치로 컴파일 시간도 추적 |

---

## 7. 결정 사항

| 항목 | 결정 | 상태 |
|------|------|------|
| 테스트 프레임워크 | GoogleTest | 확정 (Phase 0 적용) |
| 벤치마크 | Google Benchmark | 확정 (Phase 0 적용) |
| 행렬 관례 | row-major / row-vector (DXMath 동일) | **확정 (2026-08-24)** |
| 저장소 | git 초기화 완료 | 확정 |
| `vec_reg` 저장 전략 | `struct { __m128 v; }` (스파이크 전략 A) | 확정 (실측 근거) |
| double 지원 | 1차 릴리스 제외 | 확정 |

> 모든 항목이 확정되었다. 행렬은 DirectXMath 관례를 따른다.
> row-major 저장, row-vector 곱(`v * M`), 변환 합성은 왼쪽에서 오른쪽으로 읽는다.
> 이에 따라 Phase 3의 골든 테스트는 DXMath 결과와 직접 대조할 수 있다.
