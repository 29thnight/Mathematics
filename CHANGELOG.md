# 변경 이력

형식은 [Keep a Changelog](https://keepachangelog.com/ko/1.1.0/)를, 버전은
[Semantic Versioning](https://semver.org/lang/ko/)을 따른다. 1.0.0부터 공개 API의
호환성은 주 버전이 보증한다.

## [Unreleased]

### 검증

- **CI 커버리지 게이트를 80%에서 95%로 올렸다.** 1.0.1 시점에 스위트가 98.3%다. 덮지 못한
  51줄은 구조적이므로(상수 평가 전용, 구성에 따른 컴파일아웃, 디버그 `assert`) 95%를 밑돌면
  테스트 없는 API가 들어왔다는 뜻이다.

## [1.0.1] — 2026-09-23

1.0.0의 결함 수정판이다. 공개 API는 바뀌지 않았다.

### 수정

- **MSVC 19.44(Visual Studio 2022)에서 `matrix4x4.hpp`가 경고 C4067을 냈다.** 1.0.0에서 넣은
  `#if defined(__clang__) && __has_builtin(__arithmetic_fence)`는 `__has_builtin`을 모르는
  컴파일러에서도 끝까지 파싱된다. AVX2로 빌드하면 이 헤더를 포함하는 모든 번역 단위에서 경고가
  나고, `/WX`에서는 빌드가 깨진다. `__has_builtin`은 이제 따로 떨어진 `#if`에서 검사한다.
- **공개 헤더가 ASCII만 쓴다.** 줄표·θ·§·± 47자가 주석에 있었다. `/utf-8` 없이 빌드하면
  코드 페이지가 UTF-8이 아닌 Windows(한국어·일본어·중국어)에서 헤더마다 C4819가 났고,
  `/WX`에서는 빌드가 실패했다. 멀티바이트 열이 `//` 주석의 줄바꿈을 삼키면 다음 줄 코드가
  조용히 사라질 수도 있다.

### 성능

- **Visual Studio 2022(MSVC 19.44 이하)에서 cross 처리량이 DirectXMath보다 30.5% 느렸다.**
  1.0.0의 판정은 19.51로 했기 때문에 이 행을 보지 못했다. 이 컴파일러들은 `vector3`를 레지스터에
  올릴 때 레인을 하나씩 조립한다. 그래서 `_MSC_VER < 1950`에서만 `XMLoadFloat3`와 같은 형태로
  적재한다. 기준 기계의 19.44에서 cross 처리량이 24% 앞서게 됐고, 16행 전부가 5% 안이다.
  19.50 이상과 clang-cl의 코드는 바뀌지 않는다([BASELINE §13](docs/BASELINE.md)).
- **vector2/vector3 `normalize`가 길이를 멤버에서 바로 구한다.** 레지스터로 올려 내적하던 것을
  바꿨다. vector3 normalize가 MSVC 19.44에서 11%, 19.51에서 16%, clang-cl에서 9% 빨라졌다.
  위 적재 변경 뒤 한 러너에서 이 행이 DirectXMath보다 8.1% 뒤졌는데, 그 대가를 내던 경로가 이것이다.
  같은 종류의 러너에서 이제 29.7% 앞선다.

### 변경

- `tween::advance`와 `seek`에서 도달할 수 없던 분기 두 개를 걷어냈다. 무한 재생의 끝 시각은
  원래 float 최댓값이라 따로 가를 필요가 없었고, 모든 설정 함수가 타임라인을 재시작하므로
  완료 사이클 수는 줄어들 수 없다. 동작은 같다.

### 검증

- **패키지 소비 테스트를 엄격하게 빌드한다.** 경고를 전부 켜고 오류로 취급하며(`/W4 /WX`,
  `-Wall -Wextra -Wpedantic -Werror`), AVX2 경로를 켜고, `<mathematics/format.hpp>`까지
  포함한다. CMake가 imported 타깃의 include를 시스템 헤더로 취급해 경고를 숨기므로
  `NO_SYSTEM_FROM_IMPORTED`로 그 처리를 끈다. 위 C4067이 이 조건에서 드러난다.
- CI의 Linux 잡이 공개 헤더에 ASCII가 아닌 바이트가 있으면 실패한다. Windows 러너는 코드 페이지
  1252라 컴파일로는 C4819를 볼 수 없다.
- **테스트 50개 추가**(MSVC 기준 340 → 390). GCC 커버리지가 88.2% → 98.3%가 됐다. 늘어난 몫의 상당수는
  이미 테스트되던 코드다. GCC는 인자가 모두 상수인 호출을 -O0에서도 컴파일 시점에 계산해 버려
  컴파일된 코드가 한 번도 실행되지 않았다. 새 테스트는 입력 하나를 `runtime_value()`로 넘긴다.
- 커버리지 빌드의 GCC `<cmath>` 우회(`-U__STDCPP_FLOAT16_T__` 등)가 필요할 때만 켜진다.
  원인은 libstdc++ PR117321로, GCC 13.4, 14.3, 15.1에서 고쳐졌다. 구성 단계가 `<cmath>`의
  링크 여부를 직접 확인하므로 고쳐진 툴체인에서는 우회가 저절로 빠진다.

## [1.0.0] — 2026-09-23

릴리스 기준 "[PLAN §4.2](docs/PLAN.md)의 전 항목 DirectXMath 대비 ±5%"를 통과한 첫 판이다.
기준 기계(i7-8700K)에서 MSVC 19.51과 clang-cl 22.1.3 둘 다 16행 전부가 5% 안이다
([BASELINE §12](docs/BASELINE.md)).

### 추가

- **easing과 tween.** 34개 `constexpr` easing, 값 타입별 보간, pipeable tween view,
  무할당 `tween<T>`. 설계와 소유권 결정은 [EASING-TWEEN-DESIGN](docs/EASING-TWEEN-DESIGN.md)에 있다.
- **설치와 `find_package`.** `cmake --install`이 헤더, `Mathematics::Mathematics` 타깃,
  패키지 설정·버전 파일을 `share/cmake/Mathematics`에 놓는다. 최상위 프로젝트일 때만
  기본으로 켜진다(`MATHEMATICS_INSTALL`). 버전 호환은 `SameMajorVersion`이다.

### 변경

- **clang-cl 4x4 곱.** 피연산자를 `XMMatrixMultiply`처럼 128비트 행 네 개로 적재한다.
  처리량이 33% 뒤지던 것이 동률이 됐다. 마지막 덧셈에는 `__arithmetic_fence`를 걸어
  fast math의 재결합을 막았고, 12% 뒤지던 지연이 12% 앞서게 됐다.
- **slerp.** 가중치 둘을 sin/cos 쌍 두 번으로 만들고, 정의역이 보장되는 구간에서는 범위
  축소를 건너뛴다. clang-cl에서 13% 뒤지던 것이 60% 앞서고, MSVC에서도 23% 앞선다.
  t = 0과 t = 1은 끝점을 비트 단위로 그대로 돌려준다.
- **성능 게이트는 벽시계로 판정한다.** Windows의 CPU 시간은 15.6ms 단위로 양자화돼
  변동계수 p99가 34%였다. 벽시계로는 6.4%다. 같은 CI 단계가 16행 전체를 보고서로 남긴다.
- **CI 성능 게이트의 역할.** 호스티드 러너의 CPU가 최소 다섯 종이라 CI 게이트는 회귀
  탐지선으로 두고, 릴리스 판정은 기준 기계에서 한다([OPEN-ISSUES §1](docs/OPEN-ISSUES.md)).
- 벤치의 fast-math 모델에 `-fno-finite-math-only`를 더했다. clang의 `/fp:fast`가
  DirectXMath의 `XMVectorATan2`를 `ret` 하나로 지워 slerp 비교가 빈 루프를 재고 있었다.

### 수정

- MSVC에서 `vector3` 적재를 `load3`로 바꿨던 것(6c7e3e5)을 되돌렸다. store-to-load
  forwarding이 실패해 `normalize`가 5.4배 느려졌었다.

### 문서

- [OPEN-ISSUES](docs/OPEN-ISSUES.md) 신설 — 닫히지 않은 문제와 각각에서 배제된 원인.
- [GUIDE §4](docs/GUIDE.md): 퇴화 입력 계약은 IEEE 특수값이 살아 있을 때만 성립한다.
  fast math는 무한대·NaN 경계 처리를 지운다.

## [0.1.0] — 2026-08-25

첫 배포본(pre-release). clang-cl `matrix4x4` 곱 처리량이 릴리스 기준을 넘긴 채였다.

[1.0.1]: https://github.com/29thnight/Mathematics/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/29thnight/Mathematics/compare/v0.1.0...v1.0.0
[0.1.0]: https://github.com/29thnight/Mathematics/releases/tag/v0.1.0
