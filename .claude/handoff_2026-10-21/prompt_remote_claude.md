# handoff — remote_claude(검증용) 2026-10-21 저녁, 세션 clear 뒤 이어받기

너는 GNLink(D:\remote\remote) 검증용 세션 **remote_claude** 다. 이전 세션을 clear 하고 이어받는다. 이 파일을 끝까지 읽고 그대로 진행한다.
작업용1·작업용2 도 clear 됐다(진행 중이던 조사 2건은 보고 없이 끊김 → 아래 A·B 를 다시 지시). Codex 는 clear 여부 미상 — a2a_agents 로 확인.

## 0. 시작 순서
1. `CLAUDE.md`·`AGENTS.md` 확인, `a2a_whoami`·`a2a_agents` 로 주소 재확인(작업용1/2·Codex·나 모두 바뀌었을 수 있음 — displayName 으로 찾고 identity+sessionId pin). onCall=false 면 사용자에게 켜 달라고 1회 요청.
2. 상태 기록 `D:\remote\remote\.claude\state_2026-09-30.md` 끝 40줄.
3. 아래 A·B 를 작업용1·작업용2 에 **다시** 지시(지시서 그대로, 새 세션이라 "지시서가 정본, 끝까지 읽고 CLAUDE.md 대로" 문구 포함, 보고는 새 검증용 주소로 pin).
4. 사용자에게 짧게 보고: 이어받았음 + A·B 재지시 + 대기 중 결정(아래 E).

## 1. 역할·규칙 요약 (상세는 CLAUDE.md·메모리)
- 검증용 = 계획·지시서 작성, 독립 빌드/시험(build-vs 등 별도 출력), Codex 판정 받기, 통합·릴리스, 사용자 보고(존댓말, 답 끝 "수행 작업: …"). 제품 코드는 직접 수정하지 않는다(작업용이 수정).
- Codex 는 설계·신뢰경계·보안 판정. 사용자가 Codex 에 "A2A 오면 알아서 진행" 이라고 함 — 같은 범위 단계별 GO 대기 없이 진행. **게시·push 는 사용자 "게시해" 지시가 있을 때만**(게시 = push 포함).
- 커밋은 Git MCP(`.claude/mcp_git_client_wt.py`, worktree 경로 고정 사본; mcp 라이브러리 `isError`→`is_error` 보정됨). merge commit 은 git CLI(두 번째 parent 보존).
- 측정 슬롯 `C:\work\gnlink-wt\SLOT_REQUEST.txt` CreateNew. 화면 입력(SendInput/UI) 시험은 사용자가 PC 미사용·Unity 꺼짐을 확인해 줄 때만. 사용자 설치본·서비스·세션 조작 금지.
- 릴리스 절차(PC): rel 브랜치(main 위 merge + 버전 commit, product_version.hpp **BOM 유지**) → `gnlink_release.sh --dry-run`(새 worktree 는 webview2 **실복사**, junction 이면 rmdir 먼저) → 산출물 검사(해시·UTF-16 버전·parity·seam 부재) → 사용자 "게시해" → `gnlink_release_sign.ps1` 로 dry-run 산출물에 **직접 서명** → `remote60_verify_release`+`gnlink_verify_manifest.js` → `gnlink_deploy.sh --dry-run` → 실게시 → 서버 ssh 해시 대조(android pair 불변 확인) → main ff + history/작업목록 docs commit → push(PowerShell) → dist 복사 → Codex FINAL_PACKET → 작업용에 worktree 정리 지시.
- NAS 로그: `ssh -i ~/.ssh/remote60_deploy gnlink@192.168.0.6`(읽기만), `/opt/gnlink/remote60-directory/logs/shotan/<device>/{host,viewer,client}.log`, 휴대폰 `SM-S948N-b7378c/apk.log`. claude 읽기 전용 계정 키는 지금 없음.
- 장치 ID: 이 PC(집) Host hostId **f6e8e54a7d5aff7c**(host.log 장치 dir 8ec6ecb1…), 회사 PC **DEV-SHOTAN-L hostId df15b73b18cda3b1**(host.log 장치 dir **51bd6657…**, NVIDIA), 사용자가 주로 쓰는 뷰어 PC **68f79d01…**.

## 2. 현재 배포 상태
- **PC 0.2.161** 게시(10-21, manifest a6584fa8… sig 46fe7e15…, origin/main **5063cd9**). 0.2.160 실기 결함(트레이 아이콘 부재·로그온 worker rc49·worker 로그 없음·NOT REACHABLE 배지) 수정판. 상세 `docs/history.md` 0.2.161 항목.
- **APK 0.2.30 유지**(사용자 지시 "apk는 지금으로 유지"). 휴대폰 0.2.30 은 로그인 전 PC 미지원(알려진 한계).
- 이 PC(집): 0.2.161 설치·업데이트 정상, "로그인 전에도 원격 접속 허용" 켜짐, GNLinkSecureInput AUTO_START. 화면은 **1920×1080 하나(HDP-V104 HDMI 더미)** — 이전에 "3840×2160" 이라 한 것은 잘못(정정 완료). 휴대폰 흐림 원인 설명도 "1920→휴대폰 1.33배 확대 + OSLink 선명화" 로 정정됨.

## 3. 진행 중이던 일 (다시 지시)
### A. 잠금/로그인 화면 비밀번호 입력 불가 — 작업용1, 지시서 `D:\remote\remote\.claude\task_lockscreen_keys_r1.md`(사실·사용자 결정 절 포함, 그대로 재지시)
- 사실: 10-21 14:34 재부팅 → Windows 자동 로그인+잠금(OSLink 서비스 로그 14:34:24 WTS LOGON, 14:34:29 LOCK) → 사용자가 본 건 **잠금 화면**. 마우스는 secure agent 로 도달, **뷰어(0.2.160)가 key down 0건 송신**, 툴바에 'Windows 로그인' 버튼 없음(client_session_toolbar.cpp:196 — preloginCredAvailable 일 때만), **Ctrl+Alt+U → 비밀번호 창 1회 → 입력·Enter 무반응(호스트·서비스에 unlock 줄 0) → 이후 다시 안 뜸**. 사용자는 OSLink 로 비밀번호 입력.
- **사용자 결정(확정, 선택지 2)**: 비밀번호는 'Windows 로그인' 버튼·Ctrl+Alt+U(암호화 경로)로만. **로그인 전·잠금 화면 모두** 버튼 표시·동작, Ctrl+Alt+U 매번 뜨고 실제 전달·결과 표시. 키 직접 입력은 이번에 안 함(사용자는 평문 위험을 수용한다고 했다가 "일단 2번" 으로 정정). 글자 막을 때 안내 필수.
- 흐름: 조사 보고 → Codex 설계 확정 → 수정·시험(**잠금 화면과 진짜 로그인 전 화면 둘 다** 실제 로그인까지) → 0.2.162.
### B. 회사 PC 첫 접속 몇 분 안에 멈춤·뷰어 창 닫힘 — 작업용2, 지시서 `D:\remote\remote\.claude\task_dxgi_wedge_first_session_r1.md`(그대로 재지시)
- 사실: 10-21 15:16:51 회사 PC `dxgi-worker-wedge phase=release` 5.2 s → GNLinkStream exit 44 → host-app 이 1 s 뒤 **WGC recovery backend** 로 재기동 → 뷰어는 재연결 없이 15:17:07 session-dead close. 10-17 13:46:50 동일 wedge. "호스트 시작 뒤 첫 접속만" = wedge 뒤 그 실행 동안 WGC 유지, 호스트 재시작 시 DXGI 로 복귀. 회사 PC 는 지금(재시작 전까지) WGC. 사용자에게 게임 이름·전체 화면 여부 물어 둠(답 없음).
- 개선 후보: wedge 이력 유지/게임 PC 는 처음부터 WGC(바꾸기 전 지연 측정), 뷰어 자동 재연결, 근본은 캡처 프로세스 분리(D).
### C. OSLink 분석(REA) — 사용자가 REA 를 다른 세션에 연결해 직접 돌림
- 프롬프트 `D:\remote\remote\.claude\prompt_oslink_re_2026-10-21.md`, 결과 폴더 **`C:\work\oslink-analysis\`**(README.md·q1~q6·evidence·tools.md). 결과가 오면 GNLink 설계 시사점만 우리 말로 `docs/` 에 정리(OSLink 코드 복제 금지).
- 이미 로그로 본 것: `.claude/oslink_lockscreen_capture_2026-10-21.md`(데스크톱 전환마다 capture.exe 를 Winlogon 에 재기동·연결 유지, 잠금 화면은 UIA 로 PasswordField_2 포커스 후 키 그대로), 기존 `docs/OSLink_구조분석.md`.
### D. 캡처 프로세스 분리 — 2026-09 `docs/OSLink_구조분석.md` §5 에서 "캡처+인코딩 분리(GNLinkMediaWorker) 권고" 만 하고 작업목록 미등재. A·B 급한 수정 뒤 큰 작업으로 잡자고 사용자에게 제안해 둠 — 작업목록에 행 추가 필요.

## 4. 사용자 결정 대기 (E)
- SH: 휴대폰 화면 선명화 필터(데이터량 그대로, CAS 계열) — GO 대기.
- keepalive-multi(APK 0.2.31, 여러 PC 동시 유지, 최대 3대·PC별 5분) — 구현·Codex OK, APK 유지 지시로 보류.
- 휴대폰 최고화질 6000 vs 4500 — 사용자 이유 대기.
- 미리 연결(L3 제어 연결 사전 수립 + L4 마지막 모니터 자동 선택) 설계 검토 — 진행 결정 대기.
- PC 뷰어 양옆 검은 여백(뷰어 창이 16:9 보다 넓어 aspect-fit) — 전체 화면 모드 추가 여부.
- 0.2.159 체감, 0.2.161 트레이 아이콘 실기(아직 보고 없음), **진짜 로그인 전 화면 실기는 아직 안 됨**(14:34 은 자동 로그인 잠금이었음) — D2 효과 미확인.

## 5. 원장·정리 후보
- 원장: host_gpu_scaler(타이밍)·host_wire_cap_defer 간헐, update_credential_hops 간헐(996), secure_input.log 가 Users 쓰기 가능 폴더(별건), gnlink_deploy.sh :442/:445 문구, APK build gate(GA).
- 디스크: C: 여유 ~12 GB, D: ~7 GB. 오래된 worktree 다수(`git worktree list`) — 저장소 밖 삭제는 사용자 확인 뒤, junction 은 rmdir 먼저. 내 검증 worktree `C:/work/gnlink-wt/verify-eop`(build-vs, dec6006) 는 A·B 독립 검증에 재사용 가능.
