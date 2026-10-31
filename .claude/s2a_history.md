
### 433) 2026-09-08 업데이트 step2-1 — 버전 비교 계약 + 3런타임 공통 테스트 벡터 (설계 2(d) 모델, 검증용 remote#0jkgf453 위임)
- 목적: 설계(`48f09ec`) 2(d) 확정대로 버전 비교를 **계약 + 공유 벡터**로 고정한다. "같은 헤더를 쓰자" 가 불가능한 이유는 비교가 **서버 JS · Windows C++ · Android Kotlin** 세 런타임에서 각각 일어나기 때문이고, 그래서 공유하는 것은 코드가 아니라 **정답표**다. 기존 `compare_versions()` 는 설치기 익명 네임스페이스에 갇혀 있었고 **단위 테스트가 0건**이었다.
- **계약 파일(신규)**: `apps/shared/version_compare_vectors.txt` — 규칙 6개를 파일 머리에 적고 그 아래 `left|right|expect` 형식으로 **46개 벡터**. 규칙: ① `.` 로 구분된 숫자 성분 ② 성분은 **숫자 비교**(2 < 10) ③ 누락 성분은 0("0.1" == "0.1.0") ④ 선행 0 무의미("01" == "1") ⑤ **숫자도 `.` 도 아닌 첫 문자에서 비교 중단** — 접미사는 어느 방향으로도 우열을 만들지 못한다("1.2" == "1.2-beta") ⑥ 성분은 **2147483647 에서 포화**. JSON 이 아니라 줄 단위 텍스트로 둔 것은 세 런타임이 파서 의존 없이 읽게 하기 위함이다.
- **⑥은 의도된 동작 변경이다**(추출 전 C++ 구현은 오버플로에서 wrap 했다). 세 런타임을 같은 방식으로 넘치게 만들 수 없어 포화로 고정했고, 도달하려면 성분이 2^31 을 넘어야 하므로 **실제 버전 문자열로는 재현 불가능한 입력**이다. 벡터 파일 주석에 그대로 적었다.
- **C++**: `apps/native_poc/src/version_compare.hpp` 신설(헤더 온리). `installer_main.cpp` 의 file-local 구현을 삭제하고 이 헤더를 쓴다 — **③ 추출은 C++ 안에서만**이라는 확정 그대로. 문자열 리터럴(`kProductVersion` 은 `const wchar_t[8]`)이 call site 라 템플릿 추론이 안 되므로 `std::wstring_view`/`std::string_view` **두 오버로드 + 내부 템플릿 core** 구조로 했다. 설치기는 `/utf-8` 없이 컴파일되므로 헤더는 **ASCII 전용**으로 유지(C4819 회피).
- **JS**: `apps/directory/version_compare.js` 신설. 비문자열 입력은 던지지 않고 빈 문자열로 취급한다(와이어에서 온 manifest 필드라 "버전 없음" 은 예외가 아니라 "더 오래됨" 이어야 한다). JS 는 원래 오버플로가 없으므로 **포화 clamp 를 명시**해야 다른 런타임과 어긋나지 않는다.
- **Kotlin**: `VersionCompare.kt` 신설(`app/src/main/java/com/remote60/androiddirect/`). `Char.isDigit()` 은 비-ASCII 숫자를 받아들여 계약과 다르므로 **쓰지 않고** `in '0'..'9'` 로 판정한다. null 은 빈 문자열 취급.
- **테스트 3종이 같은 벡터 파일을 읽는다**: `remote60_version_compare_test`(신규 CMake 타깃, 벡터 경로를 `REMOTE60_VERSION_VECTORS_PATH` 로 주입) · `apps/directory/test/version_compare_test.js`(`run.js` 최상단에 배선 — 서버가 필요 없어 가장 먼저 돈다) · `VersionCompareTest.kt`(`app/src/test/`, JVM 단위 테스트. gradle 에 `testImplementation("junit:junit:4.13.2")` 추가 — **APK 에는 들어가지 않는다**). 세 테스트 모두 **반대칭성**(인자를 뒤집으면 부호가 뒤집힌다)을 벡터마다 추가로 단정하고, **벡터 파일이 비면 실패**하도록 했다(조용한 통과 방지).
- 검증(RDP 세션 없음 — `qwinsta` 확인: console 만 Active, rdp-tcp 는 Listen):
  - C++ `remote60_version_compare_test` **139 checks / 46 vectors / 0 failed**, exit 0
  - JS `version_compare_test.js` **95 checks / 46 vectors / 0 failed**, exit 0
  - Kotlin `:app:testDebugUnitTest` **tests=2 failures=0 errors=0**(`matchesSharedVectors`, `nullComparesAsMissing`), BUILD SUCCESSFUL
  - 디렉터리 스위트 전체 `node test/run.js` **exit 0, 전 구간 ALL PASS**(신규 항목이 최상단에서 먼저 통과)
  - `remote60_installer` 재빌드 exit 0 — 추출 뒤에도 설치기가 그대로 링크된다
- 중간 사고 1건(기록): `run.js` 배선을 bash heredoc 안의 Python 으로 넣다가 `\n` 이 실제 개행으로 치환돼 문자열 리터럴이 깨졌다(`SyntaxError`). Edit 로 정정 후 스위트 통과. 메모리에 이미 있는 heredoc 백슬래시 주의사항을 또 밟았다.
- 변경 파일: `apps/shared/version_compare_vectors.txt`(신규) · `apps/native_poc/src/version_compare.hpp`(신규) · `apps/native_poc/src/version_compare_test.cpp`(신규) · `apps/native_poc/CMakeLists.txt` · `apps/native_poc/installer/installer_main.cpp` · `apps/directory/version_compare.js`(신규) · `apps/directory/test/version_compare_test.js`(신규) · `apps/directory/test/run.js` · `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/VersionCompare.kt`(신규) · `apps/android_direct_client/app/src/test/java/com/remote60/androiddirect/VersionCompareTest.kt`(신규) · `apps/android_direct_client/app/build.gradle.kts` · `docs/history.md` · `docs/구현계획.md`.
- 설치본 생성·설치·라이브 조작·서버 배포·push 없음. 운영 서명키 관련 작업 없음.
- 상태: step2 3항목 중 1번 완료. 다음은 manifest 파서 + 서명 검증 seam. **전체 완료 아님.**
