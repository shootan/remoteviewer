# task: updater-health-gate / r1 / kind=investigate-then-fix / 발주 remote_claude(remote#lkvyqpeb)

## 배경 (재조사 불필요, 근거는 읽을 것)
오늘(2026-09-21) 집 호스트(f6e8e54a)가 0.2.131 자동 업데이트에서 **헬스 게이트 오탐→롤백→롤백 실패→~2분 다운**을 겪었다(회사 PC1 15:13 접속 실패의 진짜 원인). 2차 시도는 성공, 현재 0.2.131 안정.
정본 근거·분석·Codex 검토: **`.claude/updater_health_gate_incident_2026-09-21.md`** (반드시 먼저 읽는다).
브랜치 audit/host-pc-recovery(진단 로그 커밋 541abf5/2b65c06/a13f480/bdaefc7 위). 관련 소스: host_app_main.cpp(note_child_log_line/write_health_report/ReadChildOutput), update_relaunch.cpp(헬스 대기·stopStarted), update_health.cpp(scan/judge), update_health.hpp.

## Phase 1 — 최종 원인 규명 (이것부터, 로그 단정 금지)
Codex 지적대로 로그만으로 D1 기전을 단정하지 말 것. 소스+실기 바이트로 확정한다:
1. **설치된 0.2.131 GNLinkHost.exe 바이트가 이 소스(해당 커밋)와 일치**하는지 고정(빌드 산출물 해시 대조). 동일 attempt/PID/생성시각 기준으로만 추론.
2. 첫 실행에 `[host-app][lifecycle] directory-online observed` 와 `health ... directory=ok` 가 **왜 전무한지** 소스로 규명. 후보를 배제/확정: note_child_log_line 실제 호출 여부, append_host_app_log 의 open 모드·seek·flush·오류, 파이프 리더 `log` 핸들과 append writer 의 **상호 덮어쓰기/공유모드 open 실패**, 로그 회전과 updater `logMark` 오프셋 상호작용, 중복 supervisor/다른 userdata 경로 가능성.
3. updater 가 읽는 `healthLogPath` 가 파이프 리더가 회전시키는 그 host_app.log 와 **동일 경로인지**, 회전 시 offset 처리 결함 여부.
4. "진단 로그 증가가 악화" 가설을 **분리 재현**으로 검증(같은 바이너리, 소량/대량 stdout·회전 유무). 변경 귀속이 서면 근거를, 아니면 "미확정" 으로 남긴다.
→ Phase 1 결과를 **근거 파일에 원인 확정/미확정으로 기록**하고, remote_claude 에 한 번 보고(그때 Codex 재검증 붙일지 판단). **확정 전 파괴적 리팩터 금지.**

## Phase 2 — 수정 (Codex 방향 승인됨, 아래 제약 준수)
- **D1 헬스 신호를 로그 파일 offset/stdout 문구 해석에서 분리**한다. updater 가 만든 **attempt nonce + 예상 version + Host PID/생성시각**을 묶어 **fresh readiness 만 수락**. 이전 실행 신호 재사용·늦은 신호 거부. 채널(파일/이벤트/파이프)은 **IPC ACL·대상 검증** 포함. 새 채널도 stdout 문구에만 의존하면 안 됨 — **실제 readiness 상태(디렉터리 online 확정)에서 신호 생성**하고 쓰기/수신 실패를 독립 진단. file 방식이면 같은볼륨 원자 교체·동시 reader·오래된 파일/권한 실패까지 처리.
- **D2 롤백 안전화(독립 필수)**: 새로 띄운 정확한 supervisor/자식/서비스에 정지 요청 → captured handle bounded wait → 전체 교체 대상 quiescence 확인 후에만 복원. **시간 초과면 파괴적 복원 전에 멈추고 복구 가능 상태/사유를 보존**(부분 복원 혼합 상태를 정상 복구로 표시 금지). win32=5 를 'live라서' 로 단정 말고 소유 handle·실제 종료·핸들/ACL 원인을 구분해 처리.
- **헬스 timeout 30s·현재 poll 값은 유지**(늘려서 덮지 말 것). 진단 로그(541abf5 등)는 되돌리지 말 것 — 헬스 게이트를 로그와 분리하는 게 옳은 수정.

## 제약·검증
- 항목별 테스트 + 변이 1개 이상으로 실패 재현→원복. 순수 판정 로직은 함수로 분리해 테스트. 비밀값/nonce 원문 로그 금지, 로그 유계.
- 헬스 신호 신원·버전·freshness·재사용거부·쓰기/수신 실패, 롤백 quiescence·타임아웃시 비파괴 보존을 각각 자동 테스트로.
- Git MCP 커밋(기능 단위, 제품/테스트/문서 한 줄). 브랜치 audit/host-pc-recovery.
- **동작·프로토콜 큰 변경(헬스 채널 신설)은 Phase1 원인 확정 후 착수.** 확정 없이 채널부터 갈아엎지 말 것.

## 완료 보고 (remote#lkvyqpeb 로 1회)
Phase1 확정/미확정 결론 + 커밋 해시·변경 파일·테스트 결과(exit code+요약 줄)·설치본은 만들지 말 것(원인·수정 검증까지가 이 task, 릴리스는 별도 지시). 사용자 창구는 remote_claude.

## Phase 2 확정 (Codex r2 반영, GO)
원인 확정됐다(측정+소스 :191/:387, 실기 귀속은 "소스 경로+재현 근거" 로 표기). 아래 순서·제약으로 착수:
- **과장 표현 정정**: 보고/주석에서 "모든 로그인 호스트 필히 미통과"·"과거 전 성공 부정"·"result 부재=미판정"·"D2 고치면 깨진다" 로 단정 금지. 정확히: D1 공유모드 결함이 헬스 쓰기를 자식 수명 중 실패시켜 **불필요한 롤백을 유발**한다. 범위 밖 단정은 버전별 bytes/attempt 근거 있을 때만.
- **⑴+⑶ 동시**: append_host_app_log 공유모드(_SH_DENYNO) + **단일 writer/공통 lock 으로 open·append·flush·rotate 직렬화**, 실패 반환(무음 return 제거). seek 경합은 flush/close 후 파일 identity·기록 ID 로 대용량·회전에서 보존 확인(테스트).
- **⑵ 별도 fresh readiness 채널**: 실제 online 확정 상태에서 생성, updater nonce+예상 version+Host PID/생성시각+ACL, 구/늦은/타 attempt 거부, ready 직후 사망 비수락. **신 Host 는 기존 로그 헬스 계약을 유지(⑴⑶로 실제 도달)하며 새 채널을 추가로 쓴다. 새 채널은 신 Updater 만 읽는다.**
- 🔴 **호환성 먼저 고정**: 구 Updater(이미 설치)→신 Host 기동 경로가 여전히 통과해야 한다(그래서 ⑴⑶ 로 구 계약을 실제 도달시키는 게 핵심). 실행 주체 포함 (구/신)×(Updater/Host) 버전·bytes 행렬을 먼저 적고, 신 Host 가 구 계약을 깨지 않음을 테스트.
- **⑷ D2 롤백 안전화(독립)**: win32=5 가 live/handle/ACL 중 무엇인지 먼저 분리. 전 대상 quiescence 확인 후에만 복원, timeout 시 혼재 증가 없이 backup/복구자료 보존. 실건강 실패·child/service 미종료·권한 실패 주입 테스트.
- 헬스 30s/poll 유지. 진단 로그 되돌리지 말 것. 설치본 만들지 말 것.
- **출하 게이트(수정 후)**: 로그인 호스트 실 Updater 정상 완주 + 이상 rollback + 로그 홍수/회전 격리 시험. 1회 완주로 불충분. 실기·UAC 는 사용자 몫, 자동화 가능한 데까지 먼저.
- 완료 보고 remote#lkvyqpeb 1회: 커밋 해시·변경 파일·테스트 결과(exit+요약)·버전/bytes 호환 행렬·남은 미검증(실기).
