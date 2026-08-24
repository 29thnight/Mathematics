# Mathf

DirectXMath급 성능을 목표로 하는 C++20/23 게임 수학 라이브러리.
헤더 온리이며 x64(SSE/AVX2)와 ARM64(NEON), 스칼라 폴백을 지원한다.

> **상태: Phase 0 완료.** 설계 검증과 빌드/측정 기반이 갖춰졌고,
> 벡터·행렬·쿼터니언 API는 아직 없다. 로드맵은 [docs/PLAN.md](docs/PLAN.md) 참조.

## 무엇이 다른가

**성능은 DirectXMath와 같고, API는 현대적이다.** 이건 목표가 아니라 측정된 사실이다 —
Phase 0에서 핵심 연산의 어셈블리가 DXMath와 명령어 단위로 동일함을 확인했다
([docs/SPIKE-RESULTS.md](docs/SPIKE-RESULTS.md)).

```cpp
#include <mathf/vec_reg.hpp>

// 런타임: SSE/NEON 인트린식으로 실행
const auto v = mathf::MulAdd(a, b, c);

// 컴파일 타임: 같은 코드가 상수 평가된다. DXMath는 이걸 못 한다.
static_assert(mathf::GetX(mathf::Dot4(mathf::Set(1, 2, 3, 4),
                                      mathf::Set(1, 1, 1, 1))) == 10.0f);
```

| | Mathf | DirectXMath |
|---|---|---|
| MulAdd 지연 | 2.14 ns | 2.14 ns |
| Dot4 지연 | 4.25 ns | 4.25 ns |
| constexpr 전체 지원 | O | X |
| 플랫폼 | Windows / Linux / ARM64 | Windows 중심 |

측정 조건과 주의사항은 [docs/BASELINE.md](docs/BASELINE.md)에 있다.

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

빌드 산출물은 `%LOCALAPPDATA%\MathfBuild\`에 생성된다 — 이 저장소가 OneDrive 동기화
폴더에 있을 때 빌드 중 파일이 동기화되어 깨지는 것을 막기 위해서다.

### Visual Studio 2026에서 확인하기

솔루션을 생성하고 IDE로 여는 것까지 한 번에:

```bash
scripts\open_vs.bat
```

`%LOCALAPPDATA%\MathfBuild\vs2026\Mathf.slnx`가 만들어지고 Visual Studio가 열린다.
(VS2026의 CMake 생성기는 XML 솔루션 형식인 `.slnx`를 쓴다. 생성만 하고 열지 않으려면
`--no-open`.) 솔루션은 멀티 구성이라 `Debug`/`Release`/`RelWithDebInfo`/`MinSizeRel`을
IDE에서 골라 쓰면 되고, 시작 프로젝트가 `mathf_tests`로 지정되어 있어 F5로 바로 테스트가
돈다. 전체 테스트는 `RUN_TESTS` 프로젝트를 빌드하면 ctest로 실행된다. 테스트 탐색기에서
개별 테스트를 다루려면 GoogleTest 어댑터가 필요한데, 이 저장소의 `.vsconfig`에 포함되어
있어 구성 요소가 빠져 있으면 VS가 설치를 제안한다.

솔루션 탐색기 구성:

| 폴더 | 프로젝트 |
|------|---------|
| `Mathf` | `mathf_tests`, `mathf_bench`, `mathf_config_report`, `mathf_headers` |
| `ThirdParty` | GoogleTest, Google Benchmark, GLM |

`mathf_headers`는 빌드되지 않는 열람 전용 프로젝트다 — 헤더 온리 라이브러리는 그대로
두면 솔루션에 아무 프로젝트도 만들지 않아 `include/mathf/`를 IDE에서 탐색할 수 없다.

같은 솔루션을 IDE 없이 명령줄에서 빌드·검증하려면:

```bash
scripts\build.bat vs2026-release
```

저장소를 "폴더 열기"로 열어도 된다. VS가 `CMakePresets.json`을 읽어 `vs2026`을 포함한
프리셋 전체를 드롭다운에 보여준다.

스칼라 폴백을 IDE에서 확인하려면 별도 디렉터리에 솔루션을 하나 더 만든다. `-B`로
출력 위치를 바꾸지 않으면 `MATHF_FORCE_SCALAR`가 기본 솔루션의 캐시에 눌러앉아, 이후
평범하게 다시 생성해도 스칼라 빌드가 계속 나온다:

```bash
cmake --preset vs2026 -B "%LOCALAPPDATA%\MathfBuild\vs2026-scalar" -D MATHF_FORCE_SCALAR=ON
```

> CI는 Ninja 프리셋만 돌린다. GitHub 러너에는 VS2026이 없어서, 솔루션 경로는 로컬 검증
> 수단이다.

## 프로젝트에 통합

```cmake
add_subdirectory(external/mathf)
target_link_libraries(my_game PRIVATE Mathf::mathf)
```

`add_subdirectory`로 포함하면 테스트·벤치마크·경고 설정은 따라오지 않는다.

## 저장소 구조

| 경로 | 내용 |
|------|------|
| `include/mathf/` | 라이브러리 (헤더 온리) |
| `tests/` | GoogleTest — constexpr·스칼라 참조·DirectXMath 3중 패리티 검증 |
| `bench/` | Google Benchmark — DXMath/GLM 대조 |
| `spike/` | Phase 0 설계 검증 실험 (재현 가능하게 보존) |
| `scripts/` | 빌드 스크립트 — Ninja 프리셋과 VS 솔루션 생성 |
| `tools/` | `mathf_config_report` — 감지된 ISA/백엔드 출력 |
| `docs/` | 계획, 스파이크 결과, 성능 기준선 |

## 문서

- [PLAN.md](docs/PLAN.md) — 설계 결정과 Phase 로드맵
- [SPIKE-RESULTS.md](docs/SPIKE-RESULTS.md) — constexpr/코드젠 검증 실측
- [BASELINE.md](docs/BASELINE.md) — DXMath 대비 성능 기준선

## 라이선스

미정 (MIT 예정).
