# 변경 이력

형식은 [Keep a Changelog](https://keepachangelog.com/ko/1.1.0/)를, 버전은
[Semantic Versioning](https://semver.org/lang/ko/)을 따른다. 1.0.0부터 공개 API의
호환성은 주 버전이 보증한다.

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

[1.0.0]: https://github.com/29thnight/Mathematics/compare/v0.1.0...v1.0.0
[0.1.0]: https://github.com/29thnight/Mathematics/releases/tag/v0.1.0
