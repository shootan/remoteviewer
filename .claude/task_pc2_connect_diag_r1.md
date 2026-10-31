# task: pc2-connect-diag / r1 / kind=implement / 발주 remote_claude(remote#mz86et29)

## 목표
회사 PC1(뷰어)→PC2(호스트, 게임 실행 PC) 접속이 간헐적으로 실패한다. 다음 실패 때 원인을 로그만으로 확정할 수 있게
**서버·호스트·뷰어 세 곳에 진단 로그를 촘촘히 심는다.** 동작 변경(대기 연장·자가 refresh·keepalive·재송신)은 이번 범위 밖 — 넣지 말 것.
근거·현재 로그 공백: `.claude/company_laptop_connect_2026-09-21.md` (Codex 검토 r1 포함, 반드시 먼저 읽을 것).

## 범위
### A. 디렉터리 서버 `apps/directory/server.js`
1. `/api/connect` 마다 이미 있는 connectId 를 기준으로, 호스트별 **observe/heartbeat 도착**(off-LAN 호스트 포함, wire tuple·publicIp·lastSeen 갱신)을 로그. 현재는 LAN 호스트 `[observe]` 만 찍힘. 한 호스트당 rate 를 두어 25s 주기 이상으로 늘지 않게.
2. `[wake]` 각 datagram(0/100/300ms) 의 실제 send 완료·대상·`wakeLastSentByHost` suppress 사유를 connectId 로 기록.
3. 호스트가 capability 를 **가져간 시각**(heartbeat 응답에 실어 보낸 시점)과 어느 connectId 들이 함께 나갔는지 기록. 발급→수령 지연이 서버 로그만으로 계산되게.
4. `[relay]` hello 를 host 로 전달할 때 첫 전달·host 에서 첫 응답까지의 시각, reject 로 보이는 정황(h2c=0 지속) 을 connectId 로.
- 배포본은 repo 와 드리프트돼 있다(배포 sha256 `e627048c…`, 기준은 NAS `/opt/gnlink/remote60-directory/server.js`). 이전 관례대로 **배포본 기준 패치** `apps/directory/deploy/<날짜>-connect-diag.patch` 도 만들고 적용 검증(패치 적용 결과 == 테스트한 파일)까지. 배포·재시작은 하지 말 것(remote_claude 가 한다).

### B. 호스트 `apps/native_poc/src/directory_client.cpp` · `host_startup_control.cpp`
1. Punch 수신 **매 건**(중복 포함): from tuple, `refreshRequested_` 이전값, 시각. 현재는 false→true 1회만 로그.
2. 하트비트 루프 사이클: 시작 시각, 촉발 원인(정기/refresh), `FetchObserveEndpointFromHealth`·`RefreshObservedAddress`·`Heartbeat` 각각의 시작/종료/성공·실패·소요 ms, 받은 capability 개수와 connectId(있으면), sleep 진입/탈출.
3. hello reject(`:311`) 에 사유 세분: capability 없음 / 불일치 / 만료 / peer 불일치 등 `ClassifyDirectoryHello` 가 구분 가능한 만큼. 같은 사유 연속은 N건마다 1줄 + 누계로 유계.
4. 43000 수신 socket 의 bind 주소·pid 를 시작 시 1회 기록(H3: 같은 포트를 다른 PID 가 잡는 가설 대비).

### C. 뷰어 `apps/native_poc/src/viewer_startup.cpp` · `native_video_client_session.cpp` · 로그 업로드
1. 접속 시도마다: `/api/connect` 응답(connectId, 후보 목록 종류: lan/public/relay, punchToken 유무 — 토큰 값은 절대 기록 금지), hello 송신 시작/재시도 횟수/budget 만료/HelloAck 수신·어느 후보에서, 취소·newer-connect 로 종료된 경우 그 사유.
2. **실패하거나 몇 초 만에 끝난 시도의 로그도 NAS 에 업로드**되게. 오늘 13:02~13:09 실패 시도의 viewer.log 가 NAS 에 없었다 — 프로세스 종료 전 flush 또는 업로더 경로 문제인지 먼저 원인을 밝히고 고칠 것 (`log_upload.cpp`, `log_upload.diag`).

## 공통 제약
- 비밀값(토큰·capability·authToken 원문) 기록 금지. 모든 신규 로그는 유계(rate/누계). 기존 로그 형식·키 이름 변경 금지(집계 스크립트가 읽는다).
- 제품 동작 변경 없음. 순수 관측만. 예외: C-2 의 업로드 결함 수정은 허용.
- 각 항목에 테스트(로그 줄이 실제로 찍히는 것 + 유계 동작 + 비밀값 미포함) 를 붙이고, 변이 1개씩으로 테스트가 실제로 실패하는지 확인.
- 커밋은 Git MCP 클라이언트(`.claude/mcp_git_client.py`)로, 기능 단위. 커밋마다 "제품/테스트/문서" 한 줄.
- 브랜치: item 12 작업이 있는 `audit/host-pc-recovery`(cc981d0) 위. 다른 곳에 두면 이유를 보고.

## 완료 기준 / 보고
- 완료 보고 1회(remote_claude 로, a2a_send pin `remote#mz86et29`): 커밋 해시 목록, 변경 파일, 테스트 실행 결과(exit code + 마지막 요약 줄), 서버 패치 파일 경로와 배포본 sha 대조, **PC용 설치본 1개**(버전 bump 포함, `.claude/rel/<버전>`; 게시는 하지 말 것) 경로와 해시.
- 진행 중 A2A 는 실제 결정 필요·blocker 만. 착수 ACK 불필요.
