# rel-0164 old APK lane r3 SOURCE

2026-10-29. **494d05b SOURCE OK. N1/N2를 닫고 검증용의 S→P 한정 실행을 허용한다.** 아직 두 arm의 호환 EVIDENCE 또는 PC 0.2.164 출하 승인은 아니다.

## 고정

- HEAD `494d05b55c995b4dd687deccf618169d18df1df0`, 부모 7714c85, tracked clean(미추적 시험 산출물 있음). diff --check 통과.
- 보고 `report_old_apk_lane_r1.md` r3 SHA-256 `1af03aac862277959f4d0b073e3f66095ce8d26269b6525f494cdeddc624b339`.
- snapshot `oa_lane_r3_SHA256SUMS.txt` SHA-256 `4ca00450cc7ea0aac5e659881a47e0ba49ec45d320de1b184abd6adafe817c86`.
- **17개 파일 모두** 해당 commit blob의 canonical hash와 checkout raw hash를 직접 대조했다. CRLF 차이는 명시된 raw/canonical 두 hash로 구분했다.
- 제품/서버 runtime/구 APK source 변경 없이 시험 도구만 보수한 범위를 확인했다. 앞 S/P 대체 결정과 r2의 무인증 update GET 허용 범위는 유지한다.

## N1 — 초기화 전제의 사전 차단 수용

install의 rc0/Success 및 pm clear의 rc0/정확한 stdout Success를 확인한 뒤에만 진행한다. clear 실패/빈 응답/timeout/예외는 Invalid로 올라가 앱 시작·tap을 하지 않는다. P의 시작 뒤 NO_CREDENTIAL/sign_in_store 0 확인도 추가 방어로 남아 있다.

Codex는 snapshot의 실제 `oa_avd.py`와 fake adb를 자기 저장소 안 새 review 복사본에서 실행했다. **실제 adb는 사용하지 않았다.**

| arm/대조 | 결과 |
|---|---|
| P: pm clear rc1 + Success 문자열 | exit2, start0, tap0, clear1 |
| P: rc0 + Failed | exit2, start0, tap0, clear1 |
| P: rc0 + 빈 출력 | exit2, start0, tap0, clear1 |
| P: timeout | exit2, start0, tap0, clear1 |
| P: install 실패 | exit2, start0, tap0, clear0 |
| S: pm clear rc1 | exit2, start0, tap0, clear1 |
| P: install/clear 성공 | am start에 도달(양성 대조 목적 달성 뒤 시험 parent 종료) |

**7/7 통과.** 양성 대조는 실제 P의 로그인/연결 성공이 아니라 초기화 gate를 통과해 start를 호출한다는 뜻이다. worker의 전체 12건/음성9건은 제출 보고에 귀속하고 Codex가 전량 재실행했다고 하지 않는다.

fake adb는 pin과 함께 주면 import 시 거부되며, 실제 oa_slot은 SDK client hash를 설정하고 OA_TEST_FAKE_ADB를 제거한다. 실제 실행은 승인된 oa_slot 경로를 쓴다. `OA_PM_CLEAR_TIMEOUT_SEC=60`을 실제 실행 환경에서 고정하며 unit의 2초 설정을 상속하지 않는다. 전역 환경을 수정하는 대신 해당 실행 process 환경에만 둔다.

## N2 — child 결과와 holder/번들 exit 연결 수용

holder는 Release 송신 성공·15초 안 root 종료와 공용 `children_end_all()` 누적 결과를 최종 exit에 넣는다. 정상 종료 deadline 미달/송신 실패는5, child end/EOF/log 실패는6이며, ready였다는 이유만으로0을 만들던 경로가 닫혔다. 우선순위가 앞선 다른 실패 code를 반환해도 nonzero라는 사실은 유지된다. 실제 강제 회수는 cleanup이며 정상 Release 성공으로 세지 않는다.

Codex가 고정 holder verdict header/test를 독립 컴파일·실행해 **7/7, rc0**을 확인했다. 실제 tool이 `proc.End()` 뒤 `children_end_all()`을 호출하고 그 결과를 HolderEnd에 넣는 source 배선도 직접 확인했다. worker의 orchestration18(ready 뒤 holder5→번들3)은 보고의 대조로 유지한다. pure verdict PASS를 실제 Viewer 종료 성공으로 확대하지 않는다.

실제 arm 결과에서는 tool exit만이 아니라 viewer/holder 원문·end trailer·fixture final guard·job residual도 연결한다. Release를 보낸 뒤 프로세스가 끝났다는 시간 관계만으로 모든 종료 원인까지 증명하지 말고, Bye/종료 이유와 예상 밖 실패를 구분해 기록한다.

## Codex 검토 산출물

- `.claude/codex_old_apk_review/r3_checks.py`
- `r3_494/`의 snapshot 복사본·fake case별 호출 기록·`pmclear_review.json`·`build.log`·`verdict.log`.
- 통합 검토 로그 `r3_checks.log`: `51b0a1df65676c8f87db839ce2529994349a0f6e243d9d1cadd8be6c53ce7278`.
- fake 결과 JSON: `fea2bec091c4ca07fd1de315dd3d87d37b747a0fce87100933f7d6a6d1505e2a`.
- holder verdict log: `0e2edb1ef4d2b7690320448b07467c9b5d0343b9155581b4b204dbcaecd2249f`.

## S→P 실행 조건과 범위

검증용은 494d05b에서 5 target을 독립 빌드하고 특히 변경된 holder를 재빌드해 실제 hash를 남긴다. 최종 통합 server/Host의 source·runtime hash와 시험 wrapper/fixture/SDK adb도 고정한 뒤, 기존 슬롯 조율 아래 **S 1회 → P 1회, 각 별도 새 run/슬롯**으로 실행한다. 정상 단계마다 추가 go를 받지 않는다.

- S APK `50080b2bd586ee66ce446ab8f647ce4e5c11fafc22593d15bef7e948d2816962` / P APK `1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab` 원본 hash를 실행 전 대조한다. 되받은 base.apk hash/version 검사는 완화하지 않는다.
- S의 fixture login/directory/relay·기존 park/resume와 P의 수동 TCP control/UDP video·종료/새 연결 매핑을 구분한다. S는 합의한 WithStandby로 공존을 관찰한다. P에서 WithStandby를 쓰지 않으면⑦은 NOT RUN으로 남기며 S의 결과를 P에 복사하지 않는다.
- 실제 입력은 Host input none에서 수신/기록만 검사하고 OS 주입·clipboard를 하지 않는다. 공용 power off·두 줄 launch/final guard·job/EOF·자기 repo run root·격리 target/승인된 시험 package·adb hash/serial/boot·운영 로그인 금지를 유지한다. 사용자 물리 앱/설치 Host는 건드리지 않는다.
- P의 1회 시작에 따른 **무인증 운영 update manifest 논리 check 1회 허용**은 유지한다. update_check 결과만 남기고 download/install/update 버튼·운영 자격 증명 전송/로그 업로드로 확대하지 않는다.
- 초기화·identity·guard·기기 readiness 실패는 INVALID다. 기능 실패/peer_lost는 원문을 남기고 FAIL로 판정하며 알려진 APK 결함이라고 자동 면제하지 않는다. 무조건 재실행하지 않는다.
- 두 arm이 통과해도 게시 APK 원본의 고정 HTTPS 주소를 통한 새 server 로그인/relay를 직접 검증했다고 표현하지 않는다. 기존 승인한 합성 근거 문구와 미검증 gap을 유지한다.

다음 담당은 검증용의 실제 실행·독립 결과 보고다. 다른 통합 lane/서버 먼저 게시·Android pair 불변·push 보류, APK 0.2.33 liveness/control의 별도 출하 보류도 유지한다.

이번 Codex는 고정 source/hash 검토와 자기 복사본의 fake adb·순수 holder verdict만 실행했다. 실제 기기/adb server·Host/Viewer/GPU·OS 입력·운영 API·앱 설치/초기화는 실행하지 않았다. 제품 변경·삭제·commit/push도 없고, 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.

## 실행 INVALID 보충 — default holder 경로

검증용의 첫 S/P는 **호환 기능 근거가 없는 INVALID**로 보존한다. `verify_0164/old_apk_lane_summary.txt`의 S/P 각 rc2·6초, 슬롯 23:56:08–23:56:20과 build hash 기록을 확인했다. 보고된 run은 `oa-s-20261029235608-39644`, `oa-p-20261029235614-42792`다. 시험 Host 준비 뒤 holder 시작 전 fixture가 실패했고, AVD 미시작·기기 조작0·job residual0이라는 상세는 검증용 보고에 귀속한다.

494d05b의 oa_fixture.ps1에서 `$hc = if (...) { $HolderCmd } else { @("...holder_tool.exe") }`가 실제 그대로 있음을 확인했다. if 출력의 단일 원소가 문자열로 풀리면 `$hc[0]`은 실행 파일의 첫 글자가 된다. 앞 Source 검토에서 default holder 경로의 이 오류를 놓쳤다. 기존 다원소 HolderCmd stub 대조는 그 default 경로를 덮지 않았다.

N1의 초기화 차단과 N2의 child 결과 배선 수용은 유지하되 **현재 script로 S/P 재실행하지 않는다.** 검증용이 이미 요청한 배열 고정·같은 패턴 확인·기본 경로의 보수 전 실패/후 성공 대조를 같은 task에 연결한다. 다음 검토는 고정 보수 commit의 해당 diff/대조에 한정하며 제품/기존 관문을 전량 다시 열지 않는다. 확인 시점 HEAD는 아직494d05b로 보수 후보는 없었다. 사용자 조치 없음.
