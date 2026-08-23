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

사용 가능한 프리셋: `msvc-release`, `msvc-debug`, `clang-release`, `scalar-release`.

CMake를 직접 쓸 경우:

```bash
cmake --preset msvc-release && cmake --build --preset msvc-release && ctest --preset msvc-release
```

빌드 산출물은 `%LOCALAPPDATA%\MathfBuild\`에 생성된다 — 이 저장소가 OneDrive 동기화
폴더에 있을 때 빌드 중 파일이 동기화되어 깨지는 것을 막기 위해서다.

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
| `tools/` | `mathf_config_report` — 감지된 ISA/백엔드 출력 |
| `docs/` | 계획, 스파이크 결과, 성능 기준선 |

## 문서

- [PLAN.md](docs/PLAN.md) — 설계 결정과 Phase 로드맵
- [SPIKE-RESULTS.md](docs/SPIKE-RESULTS.md) — constexpr/코드젠 검증 실측
- [BASELINE.md](docs/BASELINE.md) — DXMath 대비 성능 기준선

## 라이선스

미정 (MIT 예정).
