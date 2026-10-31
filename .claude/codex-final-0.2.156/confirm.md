# PC 0.2.156 / APK 0.2.28 — 최종 근거 대조

2026-10-11 KST. **배포 완료(마일스톤) · 실기 테스트 대기.** 사용자 보고는 현재 사용자 창구인 검증용이 이미 진행하므로 별도 A2A 확인 답장은 보내지 않는다.

## 소스·버전

- 통합 `5ea2cc6c36471f53c951d9fd78325367ca762811`은 검토한 host 로그 `798bfca4caeacb090b4a120680d60b79694267ca`와 키보드 `f7079c0dd3e691c6ec6d92a126e0f463af27dbfe`의 merge다. 공통 조상과 부모/결과 tree를 대조해 서로 다르게 겹친 파일 및 예상치 못한 blob/모드 변경이 모두 없음을 확인했다.
- `fba1056→798bfca`는 승인 조건이던 auth marker 복원과 합성 반례 3건만 포함한다.
- release `236b85811c5c17be43596bb5d5d48ed1662975c1`은 통합본에서 PC 버전·Android versionName/versionCode의 2파일만 변경한다. `HEAD`와 `git ls-remote origin refs/heads/main`이 이 commit과 일치하고 tracked 변경은 없다.

## Codex 직접 확인

원시 결과는 `result.json`, merge/payload 결과는 `source_payload.json`에 보존했다. HTTPS 확인 시각은 `2026-10-10T19:41:47.773967+00:00`(KST 10-11)이다.

- PC 11개 + APK 1개를 공개 HTTPS로 직접 수신: HTTP 200·크기·SHA-256 **12/12 일치**.
- 두 manifest와 sig의 로컬 hash가 게시 패킷 및 배포 로그와 일치한다.
- 저장소 공개키를 사용하는 `gnlink_verify_manifest.js`를 두 플랫폼에 직접 실행해 서명 수락·변조 문서 거부, exit 0 확인. 공개키 환경 override는 제거했다.
- dist의 Setup/APK 해시가 manifest·공개 HTTPS 결과와 같다.
- payload의 버전 문자열을 확인했다. 해당 wide 문자열을 가진 Client/Host/Setup/Stream/Viewer는 모두 `0.2.156`, 모든 payload exe에서 `0.2.155` wide 문자열은 없었다. Capture/InputService/ClipHelper/Updater는 두 문자열 모두 없어 문자열 검사만으로 개별 버전을 단정하지 않는다.
- 공개 파일과 해시가 같은 GNLinkHost payload에 `credential marker` 문자열이 있다. 이것만으로 실제 crash 업로드 행동을 확인했다고 주장하지 않는다.
- 출하 APK를 aapt2로 읽어 versionName `0.2.28`, versionCode `27`, 4 ABI를 확인했다. 한국어 탭 값 **휴대폰 자판 / PC 키**도 컴파일된 리소스에 존재한다.

## 주요 hash와 공개 위치

| 항목 | SHA-256 |
|---|---|
| windows.manifest | `667b7bbf55762c9b5988f7b6c2ccfc5839642a3b2cd4e92d226a3fed507de3c5` |
| windows.sig | `73f394277e17e9855872ef1244b71007feb5d47e9c7ad709ef606395c0d9f161` |
| android.manifest | `7a4ee99ca0b82759b93e7d49f58a34ff026bf76ec4ad8f9ced0e64765c6dcc9a` |
| android.sig | `f8216eebdeabe1457dee63d640853352ac898a9e61a3c31f8d9f6da4523a7823` |
| Setup | `baea0a3720404c2352eff5bcc6183d69b3d4056eb1a014e01f1e5f5c40238a27` |
| APK | `4910e741ade268de4ac0aa6c6c5d26ada2f5291c4945d5a612974e7cfd15ff0e` |

- https://gnlink.shotan.net/updates/0.2.156/GNLinkSetup.exe
- https://gnlink.shotan.net/updates/0.2.28/GNLink-0.2.28.apk

## 로그·검증용 보고와의 구분

main worktree `.claude/rel/release_0.2.156.log`, `deploy-0.2.156.log`, `deploy-android-0.2.28.log`, `apk_0.2.28_build.log`를 읽었다. 릴리스 gate, 서버 파일 일치, Windows 0.2.155/Android 0.2.27 manifest pair 백업, 게시·외부 검증 완료 및 APK 빌드 성공 기록을 확인했다. NAS 라이브 manifest를 Codex가 별도로 가져온 것은 아니다.

통합 회귀 30/30, Android unit 177/0, APK 인증서 `dcc806ae…` 동일성은 검증용 FINAL_PACKET의 독립 실행 보고로 수용한다. Codex가 이 빌드/회귀/인증서 검사를 재실행했다고 표시하지 않는다.

## 남은 행동 확인

반영 기능은 감독자 종료 코드/재기동 계획·새 lastLine 정제와 host stream 업로드, APK 키보드 탭 접근·가로 키 라벨·뷰어 이탈 방향 처리다. 실제 폰/태블릿·Samsung IME·하드웨어 키보드, 실제 GNLinkStream 비정상 종료의 NAS 도달은 아직 미확인이다. 사용자 단계는 검증용이 안내한 PC 업데이트/UAC, 폰 앱 재시작 후 업데이트 및 실기 확인이다.

사용자 설치본·프로세스·설정을 조작하지 않았다. 미추적 최종 확인 기록만 작성했으며 제품·릴리스 docs·commit/push를 추가하지 않았다. 이 기록은 검증용의 릴리스 docs 작업을 대체하지 않는다.
