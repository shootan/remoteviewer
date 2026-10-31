# capture-lifecycle r4 — SOURCE 최종 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 **t-l5o6hmjt** / 고정 후보 **adc948212c7cefaa58c300909ae1f426d0805f9a** / 검토 diff **6ffd94b..adc9482**.

**SOURCE OK. N1-a FullFrame 실패 retry의 마지막 지적을 닫는다.** 이전 r3에서 닫은 N1-b/N2-end/N3-host/DesktopRebuild와 r2의 N4/client Query, PS-B OK도 유지한다. capture-lifecycle SOURCE 추가 보수 요청은 없다. 이 판정은 통합 EVIDENCE/출하·게시 승인 또는 실제 UAC 성공 선언과 구분한다.

요청 diff7파일·r4 보고 전문·직접 호출부를 읽고 후보 HEAD 일치/tracked clean/whitespace rc0를 확인했다. Codex의 새 빌드·실행 시험은 없다.

## 마지막 지적의 보완

- `host_stage_time_limit.cpp`의 일반 retry는 **RestartOwner::General**만 처리한다. DesktopRebuild와 FullFrame이 같은8초 backoff 경로로 무한 재시작되던 틈을 막았다. 일반 geometry/선택 회복의 기존 정책은 유지한다.
- `ask_full_frame`은 pending이 **자기 FullFrame 실패**일 때 wrapper backoff를 기다린 뒤, 동일 source key의 episode 안에서 다음 ask를 차감한다. 기존3회 상한·1초 간격·부착 유예를 유지한다. 다른 owner의 실패를 대신 재시작하지 않는다.
- backend는 ask가 올린 flag만 소비하며 pending이 없거나 FullFrame 자신의 실패일 때 재시작한다. 제품의 flag producer는 ask_full_frame 한 곳임을 확인했다. 최초와 retry 모두 owner=FullFrame이므로 자체 Detach로 외부 source boundary를 새로 열지 않는다. 소진 뒤 일반 tick/새 kick이4번째 재시작을 만들지 않는다. 새 source key에서는 기존 N4대로 새 유한 budget이 열린다.
- 추가 WGC_STILL 대역은 STREAM_TEST_SEAM 안이다. inactive 프레임의 캐시 무효화 뒤에 적용되어 '보지 않는 동안 화면이 바뀌었지만 캐시가 살아 있음'을 시험이 감추지 않는다. 출하물에 이 env/시험 define이 없다는 검사는 기존 통합 gate에 포함한다.

## 저장 실행 근거의 대조

`caplife/r4_final/e2e_uaff.txt` 원문에서 다음을 확인했다(실행 주체는 작업용).

1. episode1의 ask1 실패→같은 episode/source/epoch의 ask2 성공, 재시작2회·그림 복귀.
2. episode2의 ask1·2·3 실제 Detach 뒤 실패→12초 추가 시작0/일반 retry0/그림0/연결 유지, ALL PASS18/0.
3. epoch2의 새 source에서 episode3 ask1 성공·그림 복귀.

`r4_mut/M16.txt`는 일반 retry를 복원했을 때 ask2 경로/횟수 조건 실패 및 총6회 시도, RESULT FAILED18/3이다. 최종 복원 빌드의 rebuild-fails21·PS32·nofirst26·UA54 rc0 요약 줄도 대조했다. **작업용 실행 산출물 열람이며 Codex/검증용 독립 PASS가 아니다.** 같은 SOURCE 승인을 위해 전량 시험을 다시 반복할 요청은 없다.

## 다음 담당·승인 경계

검증용은 기존 R1 흐름으로 **LK-UX + capture-lifecycle + SH/UA-2 APK**를 고정 통합 후보에 취합하고, 합의한 독립 행동 검사·compile/byte gate·충돌 해소 근거를 Codex에 전달한다. R1=PC0.2.163/APK0.2.32, R2 KA/NP와 분리한다. 이 SOURCE OK 때문에 별도 추상 go를 기다릴 필요는 없다.

통합 EVIDENCE는 사용자 행동과 실제 출력으로 판정한다. 정지 첫 원본/IDR→수신·decode→render, 같은 연결 desktop 전환과 실패 회복·원본 출처, backend 세션 범위/실제 상태 UI·구 조합, LK 재시도와 기존 회복 사슬, 새 시험 표식/define·TestStill Off·시험 hook 제품 호출0을 기존 정본에 따라 취합한다. FrameBuffer 계수/DecodingSink는 renderer 자체가 아니며 MF 지연 출력과 input header 매핑 한계를 유지한다. 아직 송신 시작 안 한 옛 AU의 fence도 확인하고 이미 시작한 AU의 P11 한계는 구분한다.

실 UAC 예/아니오 픽셀·SYSTEM/Winlogon 재부팅·실 정지 화면 하드웨어·폰은 별도 실기다. 유한 재획득을 모두 소진한 뒤 같은 source가 계속 무응답이면 추가 무한 재시작하지 않으며, 새로운 source 경계/명시적 재접속 등으로 회복하는 제한을 기록한다. 실기 성공을 자동 fixture PASS로 대신하지 않는다. 서명/게시/최종 사용자 보고는 고정 통합·릴리스 검토 뒤 기존 승인 흐름으로 잇는다.

Codex 산출물은 미추적 판정서·인계뿐이다. 제품/공개 문서 편집·빌드·설치·commit/push 없음. 구현 history/계획checkbox/Git MCP 커밋은 기존 기능/통합 task에 취합한다. 지금 사용자 조치는 없다.
