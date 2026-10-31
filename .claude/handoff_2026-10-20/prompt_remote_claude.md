# handoff — remote_claude(검증용) 2026-10-20 12:3x, 사용자 0.2.160 재부팅 실기 직전

너는 GNLink(D:\remote\remote) 검증용 세션 remote_claude 다. 재부팅으로 이전 세션이 끊겼다. 이 파일을 끝까지 읽고 이어간다.
1. CLAUDE.md·AGENTS.md 확인, a2a_whoami/a2a_agents 로 주소 재확인(작업용1·2·Codex 주소는 재부팅 뒤 바뀐다 — displayName 으로 찾고 identity+sessionId pin). 상태 기록 `D:\remote\remote\.claude\state_2026-09-30.md` 끝부분.
2. **지금 상황**: PC 0.2.160(prelogin v1 = 재부팅 뒤 Windows 로그인 전 서비스 접속) 10-20 12:1x 게시 완료(manifest f0374344, sig 00ee26bd, main/origin 7b84c28). 이 PC 는 0.2.160 으로 업데이트됨(Host 창 v0.2.160, "로그인 전에도 원격 접속 허용" 켜짐, GNLinkSecureInput AUTO_START RUNNING, secure_input.log 12:15:17 supervisor adopt). 사용자가 **실기 재부팅**을 한다.
3. **재부팅 뒤 할 일(사용자 결과 받기)**: 사용자가 알려 주는 단계별 성공/실패를 로그로 확인 —
   - 이 PC: `C:\ProgramData\GNLink\secure_input.log`(prelogin supervisor·worker·LogonUI·handoff), `%LOCALAPPDATA%\GNLink\host_app.log`, `C:\Program Files\GNLink.update\updater.log`, Windows 이벤트 로그(서비스 시작).
   - NAS(배포 계정 읽기 전용 사용): `ssh -i ~/.ssh/remote60_deploy gnlink@192.168.0.6`, `/opt/gnlink/remote60-directory/logs/shotan/<device>/{host,viewer,client}.log` — 이 PC host 장치 dir 은 51bd6657… 또는 3be5148e… 중 최근 갱신, 뷰어 PC 는 68f79d01…(10-19 0.2.159 접속 기록).
   - 판정 기준: 재부팅 → 로그온 화면에서 디렉터리 등록(prelogin worker) → 다른 PC 뷰어(0.2.160 필요) 접속 시 로그온 화면 보임 → "Windows 로그인" 봉인 → 로그인 → 사용자 세션 Host 인계. (선택) sign-out → 재부팅 → 접속 불가.
4. 알려진 것: Host 창 "NOT REACHABLE" 배지는 뷰어 접속 뒤 `directory punch ->` 줄이 상태 줄을 덮는 **7월(27cd2f3)부터의 표시 결함** — 실제로는 online(12:15:08 뷰어 0.2.159 접속). 원장 후보, 0.2.160 회귀 아님. 휴대폰 0.2.30 은 로그인 전 PC 미지원(알려진 한계). 토글 켜짐 시 HKCU Run 의 옛 'remote60' 개발 항목도 제거됨(제품 동작).
5. 다른 열린 일: SH(휴대폰 선명화 필터, 사용자 GO 대기), keepalive-multi APK 0.2.31(보류 — 사용자 "APK 유지"), 6000/4500 질문, 미리 연결(L3+L4) 설계 검토(dot 경유 질문, 진행 결정 대기), 0.2.159 체감 확인.
보고는 존댓말, 답 끝에 "수행 작업: …".

## 갱신 2026-10-21 (0.2.161 실기 직전)
- 0.2.160 실기 결함(트레이 아이콘 부재·로그온 worker 가 뷰어 접속마다 rc49) → **PC 0.2.161 게시 완료**(manifest a6584fa8, sig 46fe7e15, origin/main 5063cd9). 상세 docs/history.md 0.2.161 항목.
- 실기 확인 순서: 업데이트 → 트레이 아이콘 → 로그아웃/재부팅 후 미로그인 → 다른 PC 뷰어로 로그온 화면 → 'Windows 로그인' → 검은 화면 → 바탕화면 → 트레이.
- 실패 시 원인 근거: C:\ProgramData\GNLink\logs\prelogin_worker.log(+.1, **SY/BA 전용 — 관리자 승인으로 읽기**; 머리줄·단계명·FATAL HRESULT/what), C:\ProgramData\GNLink\secure_input.log(worker exited rc=…), NAS 뷰어 PC 68f79d01 viewer/client.log, 이 PC host_app.log. D2(WGC probe 생략)는 유력 후보 차단일 뿐 원인 확정 아님.
- 재부팅 뒤 세션 주소는 다시 바뀐다(이전: 작업용1 4xermyzs, 작업용2 x8u8a1ve, Codex ocj43chq, 검증용 izxg3q0o).