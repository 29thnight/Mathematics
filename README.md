# Mathematics

DirectXMath급 성능을 목표로 하는 C++20/23 게임 수학 라이브러리.
헤더 온리이며 x64(SSE/AVX2)와 ARM64(NEON), 스칼라 폴백을 지원한다.

> **상태: Phase 0~5 기능 구현 완료, 0.1 릴리스 게이트는 아직 미통과.** 기하 회귀 3건과
> `cross`/전치/쿼터니언 곱 성능 회귀는 해결됐고, 두 Windows 컴파일러의 해당 성능 비교와
> GCC x86 활성 코드의 line coverage 80%를 CI가 강제한다. 남은 릴리스 블록은 clang-cl 행렬 곱 처리량과
> 전체 성능 표의 자동화 범위다. 현재 판정은 [docs/PLAN.md](docs/PLAN.md), 측정치는
> [docs/BASELINE.md](docs/BASELINE.md) 참조.

## 무엇이 다른가

**대부분의 핵심 연산은 DirectXMath와 동급이거나 더 빠르고, API는 현대적이다.**
Phase 0에서 저수준 연산의 어셈블리가 DXMath와 명령어 단위로 동일함을 확인했으며
([docs/SPIKE-RESULTS.md](docs/SPIKE-RESULTS.md)), 전체 API 감사에서 발견한 회귀와 아직
남은 clang-cl 행렬 곱 차이도 측정 근거와 함께 기록한다
([docs/BASELINE.md §6](docs/BASELINE.md#6-최종-성능-감사-phase-5)).

```cpp
#include <mathematics/vec_reg.hpp>

// 런타임: SSE/NEON 인트린식으로 실행
const auto v = math::mul_add(a, b, c);

// 컴파일 타임: 같은 코드가 상수 평가된다. DXMath는 이걸 못 한다.
static_assert(math::get_x(math::dot4(math::set(1, 2, 3, 4),
                                      math::set(1, 1, 1, 1))) == 10.0f);
```

| | Mathematics | DirectXMath | GLM | Vectormath |
|---|---|---|---|---|
| mul_add 지연 | 2.25 ns | 2.25 ns | 2.25 ns | 2.31 ns |
| dot4 지연 | 4.42 ns | 4.40 ns | — | — |
| mul_add 처리량 | 976 M/s | 968 M/s | 586 M/s | 982 M/s |
| constexpr 전체 지원 | O | X | 부분 | X |
| 플랫폼 | Windows / Linux / ARM64 | Windows 중심 | 전 플랫폼 | 전 플랫폼 |

측정 조건과 주의사항은 [docs/BASELINE.md](docs/BASELINE.md)에 있다 — 특히 MSVC에서
**C++20으로 빌드하면 약 2배 느려진다** (`if consteval` 부재).

## API 명명

- 라이브러리와 CMake 패키지 이름은 `Mathematics`다.
- 공개 헤더는 `<mathematics/...>`, 전체 헤더는 `<mathematics/mathematics.hpp>`다.
- C++ 네임스페이스는 `math`다.
- 타입, 함수, 변수, 열거형 값은 STL과 같은 `lower_snake_case`를 사용한다.

```cpp
#include <mathematics/mathematics.hpp>

const math::vector3 axis{0.0f, 1.0f, 0.0f};
const math::quaternion turn =
    math::quaternion_from_axis_angle(axis, math::half_pi);
```

## 요구 사항

- C++20 이상 (C++23 기능은 감지되면 자동 활용)
- MSVC 19.3x+ / Clang 15+ / GCC 12+
- CMake 3.24+

## 빌드

Windows에서는 Visual Studio 개발자 환경을 자동으로 잡아주는 스크립트를 쓴다.

```bash
scripts\build.bat msvc-release
```

사용 가능한 프리셋: `msvc-release`, `msvc-debug`, `clang-release`, `scalar-release`,
그리고 Visual Studio 솔루션용 `vs2026-release`, `vs2026-debug`.

CMake를 직접 쓸 경우:

```bash
cmake --preset msvc-release && cmake --build --preset msvc-release && ctest --preset msvc-release
```

빌드 산출물은 `%LOCALAPPDATA%\MathematicsBuild\`에 생성된다 — 이 저장소가 OneDrive 동기화
폴더에 있을 때 빌드 중 파일이 동기화되어 깨지는 것을 막기 위해서다.

### Visual Studio 2026에서 확인하기

솔루션을 생성하고 IDE로 여는 것까지 한 번에:

```bash
scripts\open_vs.bat
```

`%LOCALAPPDATA%\MathematicsBuild\vs2026\Mathematics.slnx`가 만들어지고 Visual Studio가 열린다.
(VS2026의 CMake 생성기는 XML 솔루션 형식인 `.slnx`를 쓴다. 생성만 하고 열지 않으려면
`--no-open`.) 솔루션은 멀티 구성이라 `Debug`/`Release`/`RelWithDebInfo`/`MinSizeRel`을
IDE에서 골라 쓰면 되고, 시작 프로젝트가 `mathematics_tests`로 지정되어 있어 F5로 바로 테스트가
돈다. 전체 테스트는 `RUN_TESTS` 프로젝트를 빌드하면 ctest로 실행된다. 테스트 탐색기에서
개별 테스트를 다루려면 GoogleTest 어댑터가 필요한데, 이 저장소의 `.vsconfig`에 포함되어
있어 구성 요소가 빠져 있으면 VS가 설치를 제안한다.

솔루션 탐색기 구성:

| 폴더 | 프로젝트 |
|------|---------|
| `Mathematics` | `mathematics_tests`, `mathematics_bench`, `mathematics_config_report`, `mathematics_headers` |
| `ThirdParty` | GoogleTest, Google Benchmark, GLM |

`mathematics_headers`는 빌드되지 않는 열람 전용 프로젝트다 — 헤더 온리 라이브러리는 그대로
두면 솔루션에 아무 프로젝트도 만들지 않아 `include/mathematics/`를 IDE에서 탐색할 수 없다.

같은 솔루션을 IDE 없이 명령줄에서 빌드·검증하려면:

```bash
scripts\build.bat vs2026-release
```

저장소를 "폴더 열기"로 열어도 된다. VS가 `CMakePresets.json`을 읽어 `vs2026`을 포함한
프리셋 전체를 드롭다운에 보여준다.

스칼라 폴백을 IDE에서 확인하려면 별도 디렉터리에 솔루션을 하나 더 만든다. `-B`로
출력 위치를 바꾸지 않으면 `MATHEMATICS_FORCE_SCALAR`가 기본 솔루션의 캐시에 눌러앉아, 이후
평범하게 다시 생성해도 스칼라 빌드가 계속 나온다:

```bash
cmake --preset vs2026 -B "%LOCALAPPDATA%\MathematicsBuild\vs2026-scalar" -D MATHEMATICS_FORCE_SCALAR=ON
```

> CI는 Ninja 프리셋만 돌린다. GitHub 러너에는 VS2026이 없어서, 솔루션 경로는 로컬 검증
> 수단이다.

## 프로젝트에 통합

```cmake
add_subdirectory(external/mathematics)
target_link_libraries(my_game PRIVATE Mathematics::Mathematics)
```

`add_subdirectory`로 포함하면 테스트·벤치마크·경고 설정은 따라오지 않는다.

## 저장소 구조

| 경로 | 내용 |
|------|------|
| `include/mathematics/` | 라이브러리 (헤더 온리) |
| `tests/` | GoogleTest — constexpr·스칼라 참조·DirectXMath 3중 패리티 검증 |
| `bench/` | Google Benchmark — DXMath/GLM 대조 |
| `spike/` | Phase 0 설계 검증 실험 (재현 가능하게 보존) |
| `scripts/` | 빌드 스크립트 — Ninja 프리셋과 VS 솔루션 생성 |
| `tools/` | `mathematics_config_report` — 감지된 ISA/백엔드 출력 |
| `docs/` | 계획, 스파이크 결과, 성능 기준선 |

## 문서

- [PLAN.md](docs/PLAN.md) — 설계 결정과 Phase 로드맵
- [SPIKE-RESULTS.md](docs/SPIKE-RESULTS.md) — constexpr/코드젠 검증 실측
- [BASELINE.md](docs/BASELINE.md) — DXMath 대비 성능 기준선

## 라이선스

미정 (MIT 예정).
