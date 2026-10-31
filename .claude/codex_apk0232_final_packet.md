# APK 0.2.32 FINAL_PACKET — Codex 최종 확인

> **Finding-3 추가:** 게시·서명/hash·main/dist 사실은 유지하되 실제 Host selector/renderer·SH 등의 재확인에는 독립 wake/전원 효과 차단도 포함한다. 아래 배포 확인은 무조작 행동 검증 완료를 뜻하지 않는다. [Finding-3](codex_test_injection_finding.md).

> **추가 정정(2026-10-28 Finding-2): 게시·서명/해시·외부 도달·main/dist 정합 확인은 유지한다.** 다만 android-select/prelogin fixture의 행동 EVIDENCE는 안전한 재확인 대기이며, 아래 배포 마일스톤을 무주입 격리/전체 행동 검증 완료로 읽지 않는다. [입력 주입 Finding-2](codex_test_injection_finding.md) 참조.

2026-10-27 / remote#tt7fcvky → 검증용 remote#7fsfy22z.

**FINAL OK — 배포 완료(마일스톤) · 실폰 테스트 대기.** 고정 후보 SOURCE/EVIDENCE OK에 이어 서명·게시·외부 artifact·서버 pair·main/origin·dist 정합을 확인했다. 실폰 설치/사용 성공이나 실제 Winlogon/UAC까지 전체 task 성공을 주장하지 않는다. 사용자 최종 안내는 검증용이 한 번에 수행한다.

## 고정점

- 버전 **0.2.32 / versionCode31**, source/test 빌드 **b554e76**. 통합5f53879는 시험 하네스만 추가한다. 병합 **0baf28cbd964a7fd6d7e546784b7d6c7b62446bb**, 릴리스 기록/main **6fed2a326614fb4218da6c4d3808bbb269ab39a4**.
- APK **13,990,300 bytes**, SHA-256 **1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab**.
- 게시 URL: https://gnlink.shotan.net/updates/0.2.32/GNLink-0.2.32.apk
- Android manifest **e4f046d57408b35537dfdaeced69cd3e3c7b9126f922757af0dcfedea8439cda**; sig **090ca467fb7b5cbef5b7bc11d74c1fa0cca359ba6148ea3733e5d14099f4ece3**.

## Codex가 이번에 직접 확인한 것

1. 익명 HTTPS로 APK 전체를 메모리에서 수신: **HTTP200, 13,990,300 bytes, 위 SHA 일치**. 로컬 release payload와 `dist/GNLink-0.2.32.apk`도 같은 SHA다. credential이 필요한 manifest endpoint의401을 성공 근거로 삼지 않았다.
2. 로컬 manifest/sig의 전체 SHA를 계산하고 `gnlink_verify_manifest.js ... android`를 실행해 정상 서명·1바이트 변조 거부를 통과했다. 이 도구는 제품 trusted public key와 server 구현을 사용한다.
3. `gnlink` 계정으로 **읽기 전용 SSH sha256sum**: 게시된 Android manifest/sig가 위 값과 일치한다. Windows manifest **6d89ae487a125ebc620e46e6be5396cb3131dc6380878b596a85b47edf64dfd3**, sig **57d1d2ca5b34568e580ea0676504e4aed7583610870b2b0f50f4716a0174d01a**로 기존0.2.163 pair가 유지된다. Windows 제품/서버 재시작을 실행하지 않았다.
4. `git ls-remote origin refs/heads/main`과 로컬 main이 **6fed2a3**으로 일치한다. merge 부모가2137f3a+5f53879이고 `git diff 5f53879 0baf28c -- apps libs`는 비어 있다. 6fed2a3의 history·계획·작업목록 변경을 읽었다. 작업트리 추적 변경0. Codex 자체 commit/push는 없다.
5. `verify_0232/deploy_android_0232.log`의 사전검사·서명검증·payload1/1·서버검증·0.2.31 pair 백업·게시·외부 hash 단계를 읽었다. **배포 실행과 백업 생성은 검증용 귀속**이다. Codex가 재배포/서명/백업을 반복하지 않았다.

기존 SOURCE와 실제 APK UI/renderer/UA-2/prelogin fixture·스크롤 관찰의 실행/직접 열람 범위는 `codex_apk0232_evidence_final.md`를 유지한다. 패키지4 ABI·인증서dcc806ae 및 Windows C++ verifier의 arm64 WrongPlatform/변조 거부·기존0.2.31 동일성 확인은 검증용 보고 귀속이다. JS 서명 통과를 실제 폰 updater 설치 성공으로 확대하지 않는다.

## 남은 범위/안내

- 사용자는 폰에서0.2.32로 업데이트 후 로그인 전 PC의 바탕화면 선택, 강하게0.60/화면 크기(실험)의 화질·스크롤·지연·발열, 이번 연결에만 적용되는 캡처 방식의 요청/실제 표시를 확인한다. 원격 SYSTEM/Winlogon/UAC·실폰GPU/S4·실 NAT는 기존 미검증을 유지한다. 요청 없는 설치/재시작은 하지 않는다.
- H1의 하네스 저장소 밖 adb 삭제 호출은 후속 실행 전 제거·원본 보존/운영기록이 필요하다. 제품 APK/게시 정합과 분리된 잔여이며 여기서 완료했다고 처리하지 않는다.
- 작은 문서 정합: `docs/작업목록.md` SH 행은 제목이0.60이지만 설명 열에 이전0.35가 남아 있다. 다음 관련 기록에서 현재0.60/기본A·B실험 선택 의미로 맞춘다. 이 문구 정정을 이유로 재빌드/재게시/추가 릴리스 관문을 만들 필요 없다.
- 0.2.33/KA SOURCE 반려 및 NP 설계 후속은 별도 작업으로 계속한다. 이번0.2.32 게시 승인이 자동 preconnect/새 KA 출하 승인으로 승계되지 않는다.

사용자 창구 검증용이 버전·main commit·게시 위치·확인한 행동/해시·남은 실기·지금 할 일을 한 번에 보고한다. Codex 작업은 읽기 전용 검증과 이 미추적 운영 판정서 작성이며 공개 history/계획·Git MCP commit은 이미6fed2a3으로 취합됐다.
