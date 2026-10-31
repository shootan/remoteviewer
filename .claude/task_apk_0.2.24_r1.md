# task apk-0.2.24 — r1 (검증용 remote_claude remote#mdeatlz4 발행, 2026-09-30 14:2x)

## 목표
휴대폰 앱 0.2.24 (versionCode 23) **버전 올림 + 빌드 + 자체 검증 + payload 패키징**. manifest 생성·서명·NAS 게시·push 는 검증용이 한다(이 task 밖).
내용: main `e274c37`(0.2.145 와 같은 트리)에 이미 들어 있는 것 — 고정 서버 `https://gnlink.shotan.net`(입력란 없음, `rem.shotan.net` 자격 이행) · 자동 로그인(Keystore 자격) · 계정 상태 문장(승인 대기·정지). 현재 게시된 APK 는 0.2.23(versionCode 22, 09-28)이라 이 셋이 폰에 아직 없다.

## 범위
1. worktree: `git worktree add D:/remote/remote/.claude/worktrees/release-apk-0.2.24 -b release/apk-0.2.24 e274c37` (다른 worktree 건드리지 않음).
2. 버전 줄만: `apps/android_direct_client/app/build.gradle.kts` versionName "0.2.24" / versionCode 23. `product_version.hpp` 는 이미 0.2.145 — 그대로.
   커밋 1개, 제목 `release: APK 0.2.24 -- ...`(0.2.23 의 `.claude/worktrees/release-apk-0.2.23/.claude/msg.txt` 형식: 사용자에게 보이는 변화 3~5줄 + "Product: version only. Test: none. Docs: none."). Git MCP client 로 커밋.
3. 빌드: clean 뒤 **게시된 0.2.23 과 같은 변형·같은 서명 인증서**로. 판정은 인증서로 한다:
   `apksigner verify --print-certs` 를 새 APK 와 `release-apk-0.2.23/.claude/rel/android-0.2.23/GNLink-0.2.23.apk` 둘 다에 걸어 **certificate SHA-256 이 같아야** 한다(다르면 폰이 업데이트 설치를 거부한다). 다르면 게시 후보로 두지 말고 STOP·보고. (기록상 0.2.20 은 release 에 서명 설정이 없어 `assembleDebug` 가 배포본이었다 — 0.2.23 도 같은지 인증서로 확인해 적을 것.)
4. 자체 검증(각각 exit code + 마지막 요약 줄): `aapt2 dump badging` 의 versionName/versionCode · JVM 단위(`testDebugUnitTest`) · fixed-server APK gate(history 12830 의 "gate APK 5" 와 같은 것 — 무엇을 실행했는지 명령을 적을 것) · APK 가 4 ABI 유니버설인지.
5. 산출: `<worktree>/.claude/rel/android-0.2.24/payload/GNLink-0.2.24.apk` + `SHA256SUMS.txt`(apk sha256). 설치본·기기·에뮬레이터 조작 없음(adb 0 이면 그렇게 적음).

## 보고 (remote#mdeatlz4 pin, 버스 불가면 `<worktree>/.claude/report_apk_0.2.24.md`)
고정 commit · apk sha256 · 인증서 SHA-256(새/0.2.23) · 빌드 변형 · 시험별 exit · 제품/테스트/문서 한 줄("제품: 버전 줄만"). 다른 변경 금지. 별도 ACK 불필요.
