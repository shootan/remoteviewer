# 실기 판독 — 크래시 재부팅 뒤 로그인 전 화면 영상 없음 (0.2.162, 검증용 remote#yj8mrmrx, 2026-10-22)

## 사용자 보고
"강제 재부팅되었는데, gnlink 는 접속 안 되더라. oslink 로 해결했어."

## 출처 (이 PC 시계)
- 워커: `D:\remote\remote\temp\prelogin_worker.log` (사용자가 관리자 권한으로 `C:\ProgramData\GNLink\logs\` 에서 복사)
- 서비스: `C:\ProgramData\GNLink\secure_input.log`
- NAS: `shotan/68f79d01-…/viewer.log`(PC 뷰어 0.2.162) · `shotan/SM-S948N-b7378c/apk.log`(APK 0.2.31) · `shotan/8ec6ecb1-…/host.log`
- 이벤트 로그: 16:28:35 비정상 종료(0xA0000001 amdkmdag), 16:29:53 부팅, 16:34:10 로그온(OSLink 경유)

## 경과
- 16:30:00 서비스가 로그인 전 워커 시작(pid 5368) → 16:31:17 디렉터리 online. 자동 로그인 없음 = 진짜 로그인 전 화면.
- 16:33:13 PC 뷰어 연결(relay 29190, hello 3 ms, 버전 교환 OK). 뷰어는 picker 상태라 `stream-state active=0`.
  - 워커: DXGI 시작 직후 내용 프레임 1개(`dxgi-acquire acquires=1 dxgiContentAcquires=1`) → **stream inactive 라 flush**(`capture-pipeline-flushed reason=stream-inactive`).
  - 그 뒤 로그인 화면이 정지 상태라 DXGI 새 프레임 0(`acquires=0 timeouts≈90/s`), keyframe-request 는 `consumed` 만 되고 encodedFrames=0. 16:33:18 `capture detached (idle)`.
- 16:33:30 같은 뷰어 재접속(session-rollover) → `stream restored for new session` → **`[prelogin] capture cannot continue on the logon desktop (wgc requested) … stop serving and leave (r10 T2)`** → 워커 종료 rc=15(16:33:31), 1 s 뒤 재기동(pid 4164).
- 16:33:38 PC 뷰어 재연결 → 16:33:40.729 `stream-state active=1` → `stream active; forcing keyframe` 그러나 DXGI 는 `dxgiPointerOnly=2` 뿐, 내용 프레임 0 → 영상 0. 뷰어 keyframe 재요청 반복 후 종료.
- 16:33:52 휴대폰 연결(rollover; `desktop-backend-request backend=wgc` 는 `ignored (only dxgi before login)` 로 정상 무시) → 16:33:53 active=1 → 동일하게 프레임 0 → APK `select_timeout`, "window list not available at the logon screen".
- 16:34:08 워커 통계 `encodedFrames=0 sentFrames=0`. 16:34:10 OSLink 로 로그온 → 일반 호스트 정상.

## 판정
1. **결함 A (주원인, 제품 결함)**: 로그인 전 워커(DXGI 전용)는 정지 화면에서 새 프레임을 받지 못한다. 첫 내용 프레임은 stream 비활성(picker) 중에 flush 되고, 활성화·keyframe 요청 때 다시 찍을 원본이 없다(마지막 프레임 재사용 또는 duplication 재생성으로 전체 프레임 재획득이 없음). 결과: **화면이 바뀌기 전까지 로그인 화면이 영원히 안 보인다.**
2. **결함 B (코드 위치 확인)**: 캡처가 idle detach(16:33:18) 된 뒤 stream 이 다시 활성화되면(16:33:30 rollover 의 `stream restored for new session`) `host_stage_stream_active.cpp:134-143` 재부착 경로가 "보안 데스크톱이면 DXGI 대신 WGC" 규칙으로 `backend.active=Wgc` → `restart_capture_session` → `host_capture_session.cpp:600` 이 prelogin 이면 `PreloginStopServing("wgc requested")` → 종료 rc=15. 로그인 전 화면은 항상 보안 데스크톱이므로 **picker 에서 5 s 이상 머문 뒤(또는 재접속 뒤) 바탕화면을 고르면 워커가 죽는다.** pid 4164 는 활성화 시점(16:33:40)에 아직 attach 상태라 살아남았다. 뷰어의 명시 backend 요청은 `host_stage_backend.cpp:126` 에서 정상 무시됨(16:33:52).
   - 회귀 시험 공백: `viewer_prelogin_login_e2e_test.cpp:1194` 는 `media datagrams > 0` 만 본다 — picker 비활성 → idle detach → 활성화 → 정지 화면 첫 프레임 경로를 밟지 않는다.
3. GPU 크래시와의 관계: DXGI 디바이스·AMD 인코더 생성과 첫 프레임 획득은 정상(`driver=31.0.21924.61`) → 드라이버 상태가 원인이라는 근거 없음.
4. 기존 메모: 10-21 재부팅 실기는 자동 로그인 뒤 잠금 화면이었다 — 진짜 로그인 전 화면 + picker 경유 + 정지 화면 조합은 실기에서 처음 밟은 것으로 보인다(이전 통과 여부는 미확인).

## 수정 제안 (Codex 확정 필요)
- A: prelogin(및 DXGI 정지 화면 일반)에서 stream 활성화·keyframe 요청 시 원본 프레임 보장 — 마지막 내용 프레임 보존 후 재인코딩, 또는 duplication 재생성으로 초기 전체 프레임 획득. flush 가 마지막 정지 프레임까지 버리지 않게.
- B: prelogin 워커에서 rollover/restore 경로가 backend 를 wgc 로 되돌리지 않게(명시 요청과 동일하게 무시), 또는 rollover 를 종료 사유에서 제외.
- 시험: 실제 로그인 전 화면(또는 동등 fixture)에서 picker 경유 → 바탕화면 선택 → 정지 화면 첫 프레임 ≤1 s, 같은 뷰어 재접속 2회·휴대폰 교차 접속 중 워커 생존. 잠금 화면 회귀.
