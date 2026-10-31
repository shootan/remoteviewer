# T1 + J1 — SOURCE 최종 재검토

2026-10-28 · **SOURCE OK. T1과 J1을 닫고, 해당 T self-test 및 지정 AVD 시나리오의 SOURCE 보류를 해제한다.** 실제 APK 행동의 EVIDENCE·통합 출하 승인은 아니다.

## 고정 범위

- T1: ka-r3 **af49c26ff8fff967003433d07126f58384f93cc3**, 8a84198 위 gate cleanup/root test 2파일. feat merge **9f9a91c852419a2d2848be73aab077d779b0550c**에 반영됐다.
- J1: **4f04415cc30e2eb94fe5a662c7a560edd542556a**, de2d60a 위 job start/header/test/CMake 4파일. HEAD 9f9a91c에서 J1 세 source 파일은 변경 0이다.
- report_gate_temp_T.md SHA-256 **0daf146220a9366bf28a020cf582137f31c56459c4831109f68b4d582c081348**, report_avd_guard_r4.md **ca69c7f5cff2ac187ff5cda2f755a6bd5f3e4f6b0f0356eaf81a9cc934238ed9** 일치. tracked 변경 없음, 대상 diff --check 통과.
- 실행 script는 기존 **avd_guard_r3_snapshot**이다. manifest의 wrapper/import/helper/stub **9파일 해시가 그대로임을 직접 확인**했다. 이전 G1/fixture G2·제품 Unknown fallback의 SOURCE 수용과 함께 적용한다.

## T1 — 실제 junction 진입 반례 해소

새 cleanup은 topdown에서 하위 directory를 **진입 전에** 검사해 reparse/link를 traversal 목록에서 뺀다. 선택된 일반 파일을 먼저 정리하고, 정상 directory를 깊은 순서로 회수한다. 비정상 root는 아무것도 건드리지 않는다. junction을 뒤늦게 남기면서 그 안 파일은 지우던 8a84198 경로가 닫혔다.

Codex가 보존해 둔 실제 자기 junction → 자기 sibling sentinel 구성에 고정 af49c26을 직접 적용했다.

1. 삭제 함수 기록 대역에서 **run 자신의 새 파일만 선택**, junction 아래 sentinel 선택 0을 확인했다.
2. 실제 cleanup은 삭제 대상이 자기 run 안인지 검사하는 추가 guard 아래 실행했다. **자기 파일 1개만 제거**했고 sibling sentinel·junction은 보존됐다.
3. link와 비어 있지 않은 run은 left로 보고돼 성공한 전체 cleanup이라고 하지 않았다. 외부/사용자 경로 삭제 시험을 하지 않았다.

자료: `.claude/codex_ka_e_review/gate_T_af49/junction_closure.json`, 고정 gate와 이전 자기 junction/sentinel 보존.

병합본 9f9a91c의 gate는 T 코드와 함께 기존 추가 출하 marker들도 유지한다. **동결한 병합본 self-test를 실제 허용 D repository의 새 root에서 직접 실행**했다.

- REMOTE60_TEST_TMP: `.claude/test-tmp/codex-gate-final-1793167047204404200`
- 실행 run: 그 아래 `gate-selftest-14184-1793167047303-0`
- 결과: **rc 0, ALL PASS 58 checks**, 자기 run cleanup 후 지정 root 내부 0개. root 자체는 보존했다.
- 로그: `.claude/codex_ka_e_review/gate_T_af49/selftest_9f9.log`
- log SHA-256: **c38f407957375519cf1b50a8cec46097ad92b140558082d8723c90549afbfc16**

작업용의 ka-r3 root_test 20/20·self-test 53/53은 보고 근거다. Codex 직접 결과는 **실제 junction 반례의 closure와 추가 marker가 있는 feat 병합본 58개**다. 서로 다른 source의 check 수를 같은 run으로 합치지 않는다. 사용자 TEMP는 읽거나 만들거나 정리하지 않았다.

## J1 — 종료 시도와 확인 구분

JobStartApi로 분리된 실제 start 경로를 수용한다.

- assign/membership 실패는 suspended child를 자기 process handle로 종료 시도하고, **wait가 signal을 본 경우에만** ended_confirmed=1/exit 2다. 종료 시도가 실패했어도 이미 종료돼 signal이 확인되면 확인된 상태로 보고할 수 있다. wait timeout/실패는 ended_confirmed=0/exit 98이다.
- ResumeThread 실패는 running으로 넘기지 않는다. 자기 job 종료를 시도하고 command 종료와 job empty를 둘 다 확인해야 confirmed다. 그렇지 않으면 98이다.
- tool은 실패 진단을 쓰고 자기 handles를 닫으며 그 결과를 반환한다. 성공 경로의 편입/membership 확인→resume→command 대기→잔여 회수/active 0 확인은 그대로다.

Codex는 **고정 header와 test를 별도 빌드해 8/8 PASS, rc 0**을 직접 확인했다. assign=false/terminate=false/WAIT_TIMEOUT의 원 반례, membership 조회/판정 실패, resume 실패의 종료 미확인·job 비어 있지 않음, 성공 대조가 포함됐다. Win32 대역으로 **실제 process/job을 만들지 않는 시험**이다.

- `.claude/codex_ka_e_review/job_J1_4f04/job_start_4f04.exe`
- exe SHA-256: **c11c9a7e4c6a550484999700799d6b36d4fdacdf0dee41571d01aa43ed1bb61c**
- 같은 폴더 build.log/run.log 및 동결 원문 보존.

기존 orchestration 18/18·mutant 및 잔여 0은 작업용의 stub 실행 근거로 유지한다. Codex는 실제 orchestration/AVD를 재실행하지 않았다.

## 실행에 연결할 산출물과 범위

읽기 전용 SHA-256 직접 확인:

| 산출물 | SHA-256 |
|---|---|
| e2e_job_run.exe | dc4c5049e8e9d32a8c9096005a2702c95ef30cce2d3f1998bd4005da8c89462f |
| GNLinkStreamPinnedSelectTest.exe | d806e375019bc5eec850443efda64cc35289f1a7a73cffbe473bcf8391e12eaa |
| remote60_mobile_abr_session_e2e_test.exe | 722bf981137966cb7be817efced6836e1bc1fd99fa52a5af0779e6f6443217bd |

**검증용은 별도 go를 다시 받을 필요 없이** 다음으로 진행할 수 있다.

- T self-test: af49c26 또는 그 보수를 포함한 고정 병합본, mandatory REMOTE60_TEST_TMP의 허용 anchor 바로 아래 일반 폴더·새 run·링크 없는 경로 조건으로 실행한다. 옛 기본 TEMP/8a84198 cleanup 경로로 돌아가지 않는다.
- AVD: 9f9a91c와 위 고정 artifacts·r3 script snapshot을 연결하고, 해당 제품 source로 만든 APK off/on의 source·hash도 기록한다. **기존 slot이 해제된 뒤** ke_slot의 준비/identity/guard/job/final 판정을 통해 지정 directoryhost 기반 E 시나리오를 실행한다. mutable 실행 사본은 snapshot과 대조한다. read-only 사본을 직접 실행하지 않는다는 이유로 다른 내용을 실행하지 않는다.

이는 실제 AVD의 gate 통과·monitor/final 상태, E13의 같은 탭 ordinary 1회·첫 fresh frame, E13b의 새 tap 취소를 이제 독립 관찰하라는 SOURCE 승인이다. 결과에서 NOT RUN과 실패/미확인을 분리하고, APK/NDK/4 ABI 및 최종 통합 EVIDENCE는 그 근거로 따로 판정한다.

실 clipboard (c), old-host/E7f/E8/E12 NOT RUN, E7h/Activity 교체/raw AUTO, ke_r21 peer_lost 원인 등의 기존 미결은 유지한다. 사용자 화면/입력·clipboard·금지된 prelogin 시험의 경계는 바뀌지 않는다. DW2의 NVIDIA 게임 실기 조율과도 별개다.

## 기록과 마무리

과거 기본 TEMP의 **8개 도구 호출 / 프로젝트 43개 호출·8세션** 정정과 실행/check/실제 경로·개별 삭제 결과의 미확인은 그대로 유지한다. 제한된 TEMP prefix 개수 조회 사실과 정리 0도 구분한다. 새 Source OK로 과거 경계 위반을 소급해 준수로 바꾸지 않는다. 기존 commit 경로 오타의 후속 정정·amend 미실행도 수용한다.

이번 Codex는 source·hash 읽기, 자기 repository 내 제한 cleanup/self-test 및 child 없는 J1 대역 시험만 수행했다. 실제 Host/Viewer/AVD/adb·운영·설치·외부 삭제·제품 편집·commit/push는 없다. 기록은 기존 구현·안전 ledger/history에 검증용이 취합한다. 사용자 현재 조치 없음.
