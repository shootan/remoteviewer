# task ua2-apk r1 — 캡처 방식 선택을 '이 PC 의 이번 연결에만' (APK) (검증용 remote#yj8mrmrx, 2026-10-26) — 담당 작업용2 (SH r2 다음)

**이 파일이 정본이다.** 구속 계약: `.claude/codex_rel_0163_r1.md` §1(원문 우선). 상위 계획 `.claude/plan_rel_0163_2026-10-26.md`(R1 = APK 0.2.32). host 쪽 UA-2 는 작업용1(`task_capture_lifecycle_r1.md`) — shared native/protocol 쓰기 금지, 필요한 bridge/protocol 은 작업용1 고정 후보를 받아 쓴다.

## 사용자 실기
10-22 15:24:23 폰 설정 탭에서 캡처 방식 WGC 를 고른 뒤 그 값이 `SessionPersistence.desktop_backend` 전역 저장값으로 남아 매 접속마다 wgc 를 요청 → 호스트 전역 WGC → PC 접속의 UAC 화면이 안 보임.

## 범위
- **기존 전역 저장값의 자동 적용 중단**: 새 APK 는 저장된 desktop_backend 를 다음 연결/다른 host 에 자동 전송하지 않는다. 선택은 현재 entry/connection 에만 보관(KA r2 복수 entry 대비 entry 별). 기존 저장값은 무시(필요하면 이행 시 삭제)하되 다른 영상 설정·SH 저장값 불변.
- **문구**: '이 PC 의 이번 연결에만 적용'. 연결 전에는 적용 대상 표시 또는 비활성.
- **요청 방식 vs 호스트 실제 적용 방식 구분 표시**(예: DXGI 요청 / WGC 격리 적용, window target 은 WGC). 응답 없으면 '적용 중/확인 불가' — radio 만 바꿔 성공처럼 보이지 않게. 실제 적용 상태 수신은 작업용1 의 capability/상태 응답 후보에 맞춘다(번호·플래그는 검증용 배정). 구 host(세션 범위 보장 모름)면 그렇게 안내.
- 초기 UI 는 host 기본/실제 응답을 따른다.

## 시험
AVD + 시험 host(fail-closed): 설정 WGC 선택 → 연결 종료 → 새 연결 시 backend 요청 0·UI 가 host 실제 적용값, 다른 host 전파 0, 계정 변경 전파 0, 응답 전 '확인 불가' 표시, SH 저장값 보존. 렌더된 컨트롤 가시성·활성·실입력 증거(CLAUDE.md 사용자 행동 기준). 폰 자동 설치·조작 금지.

## 규칙
SH r2 와 같은 MainActivity 를 쓰므로 SH r2 branch 위에 쌓거나 같은 고정 후보로 취합(보고에 명시). repo root 밖 삭제 금지, 버전·서명·게시 금지(R1 통합에서), 커밋 Git MCP. 보고 `.claude/report_ua2_apk_r1.md` + remote#yj8mrmrx pin 1회.
