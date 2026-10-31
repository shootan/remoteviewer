# PC 0.2.155 / APK 0.2.27 — Codex 최종 게시 근거 확인

2026-10-11 KST. 대상 source `35d5937cc74e457f8459e8476b1d4f4dc9c26f67`, release commit `7d9bc68b73e02a1047d7646324f9de8289a9ea29`.

**배포 완료(마일스톤) · 실기 테스트 대기.** 이번 확인은 설치·실기 성공 판정이 아니다.

## 직접 확인

- main worktree HEAD와 `git ls-remote origin refs/heads/main`이 위 release commit과 일치한다. tracked 변경 없음. source→release는 PC 버전과 Android versionName/versionCode 변경 2파일뿐이다.
- PC `0.2.155`, Android `0.2.27`/versionCode `26`을 버전 diff와 manifest에서 확인했다.
- 익명 HTTPS로 PC 11개 + APK 1개를 직접 내려받아 HTTP 200, 크기·SHA-256 **12/12 일치**를 확인했다. 원시 결과: `https_artifacts.json` (checked_at_utc `2026-10-10T15:04:22.288808+00:00`).
- 저장된 PC/Android manifest+sig를 저장소의 `gnlink_verify_manifest.js`로 각각 검증했다. 공개키 환경 override는 제거하고 tracked `update_manifest.cpp`의 공개키를 사용했다. 두 실행 모두 exit 0이며 같은 스크립트의 변조 문서 거부 검사도 통과했다. `signature_checks.json`.
- 보존된 `dist/GNLinkSetup-0.2.155.exe`와 `dist/GNLink-0.2.27.apk`의 해시가 manifest·공개 HTTPS 파일과 일치한다.
- 출하 GNLinkStream의 해시를 확인하고 force-clamp 및 monitor 시험 환경변수 이름 4종이 ASCII/UTF-16LE 모두 없음을 확인했다. `stream_marker_check.json`. 이것은 해당 문자열 검사이며 모든 가능한 시험 동작의 부재를 일반적으로 증명한다는 뜻은 아니다.

## 해시와 URL

| 항목 | SHA-256 |
|---|---|
| windows.manifest | `c143b3f94467fde0141c45fd718dc279a75ff1ada16f26efd7c1b16627403102` |
| windows.sig | `07ab1e9c8078552b26b78ce4a7a3a037628727b9ab9a15c54ae58e3604340072` |
| android.manifest | `1cab88e43e03943d3cfd0c5264c36dcd9892f8f8d9cb5001a7cd85fefe0a6906` |
| android.sig | `ac52de5fc7667092c8eb8d6fdd9119ef749469b15bfb61f5747671f2aba41f72` |
| GNLinkStream.exe | `0119219b43c427c5c71cf22d70823bcfbb8fd7b3c028fe664438e55dbcd92187` |
| GNLinkSetup.exe | `8793a6a5d4bcb7f0a88344904002ea599c8f0705119231f3f6ea5d065674e0d6` |
| APK | `7ba239bf4916654e0759258d91bf370250ddec49c4f1779a820f6720653e83ec` |

- PC 설치본: https://gnlink.shotan.net/updates/0.2.155/GNLinkSetup.exe
- APK: https://gnlink.shotan.net/updates/0.2.27/GNLink-0.2.27.apk

## 저장 로그 및 검증용 보고로 확인한 것

`D:/remote/remote/.claude/rel/release_0.2.155.log`에서 고정 release commit, parity·payload-set·checksums·installer 10/10, 제품 verifier의 서명 수락/변조 거부를 읽었다. `deploy_0.2.155.log`·`deploy_android_0.2.27.log`에서 NAS 게시·서버 파일 검증·외부 해시 검증·직전 PC 0.2.154/APK 0.2.26 manifest pair 백업을 읽었다. NAS 라이브 manifest를 Codex가 따로 받아 비교한 것은 아니다.

APK 빌드 로그의 assembleDebug/testDebugUnitTest 성공을 읽었다. 통합 회귀 26/26 및 APK 인증서 `dcc806ae…`는 검증용 완료 패킷의 보고다. Codex가 통합 빌드·회귀·APK 인증서 검사를 다시 실행했다고 표시하지 않는다.

## 동작 범위와 남은 실기

릴리스의 주 기능은 Android 모니터 선택의 적용 ACK/세대 결합과 사라진 모니터의 primary 폴백이다. UDP 보수 중 Windows 출하 Viewer의 reset advisory와 Windows 시험 하네스의 997/nonblocking 보수는 구분한다. 후자를 출하 Viewer나 현장 주기 끊김의 원인/해결로 기록하지 않는다.

사용자가 PC 0.2.155와 APK 0.2.27로 업데이트한 뒤 폰·태블릿에서 각 모니터 선택/화면 표시, 선택한 모니터를 실제 분리했을 때 primary 폴백을 확인하는 단계가 남았다. 실제 MediaCodec·hot-unplug 성공 및 과거 현장 끊김 원인은 미확인이다. 사용자 제품을 자동 설치·종료·재시작하지 않았다.

이번에는 최종 확인용 미추적 기록만 작성했다. 제품·릴리스 문서 변경이나 commit/push를 추가하지 않았다. 릴리스 docs 기록은 검증용의 기존 통합 작업 범위이며 이 파일은 그 기록의 대체물이 아니다.
