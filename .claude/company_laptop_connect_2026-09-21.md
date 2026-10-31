# 회사 PC1→PC2 접속 간헐 실패 — 2026-09-21 NAS 로그 대조 (remote_claude)

> 명칭 정정(사용자, 09-21): "노트북"은 오기. DEV-SHOTAN-L = **PC2**(회사, 게임+Host 실행). 뷰어 68f79d01 = **PC1**(사용자가 쓰는 회사 PC). 증상 = PC1→PC2 접속 불가. 둘 다 공인 IP 211.218.222.1(같은 회사망).

## 출처 고정
- 디렉터리 서버: NAS journal `user-1006.journal`, node pid 1601140 (서비스 시작 09-18 16:52), 배포 `server.js` sha256 `e627048c…`, `wake_target.js` `48d3b480…`(= audit/host-pc-recovery 의 파일). `hostSendTargetFor` 5곳 → relay-aim 패치 반영본.
- 호스트 로그 `/opt/gnlink/remote60-directory/logs/shotan/<device>/host.log`:
  `51bd6657…` = **DEV-SHOTAN-L = 회사 PC2(게임+호스트)** (hostId df15b73b…, wire 211.218.222.1:43000) /
  `8ec6ecb1…` = **shotan Desktop 집 PC** (hostId f6e8e54a…, 192.168.0.76, wire=집 공유기 헤어핀 → via=lan).
- 뷰어 = 회사 PC1(사용자 조작 PC) `68f79d01…`(211.218.222.1): `client.log` 는 전량. `viewer.log` 는 **13:10:21 이후 pid 69284 세션만** 있음 → 13:02~13:09 의 뷰어 프로세스 로그는 NAS 에 없음.
- 시계: 집 PC 가 NAS 보다 **+5.3s**(date 대조). PC2·회사PC 는 NAS 와 ±1s 이내로 추정.
- 버전: 호스트 둘 다 0.2.130, 뷰어 0.2.130.

## 타임라인 (NAS 시각)
| 시각 | 대상 | 서버 `[wake]` | 호스트 측 | 결과 |
|---|---|---|---|---|
| 13:02:55 | 집 Desktop | via=lan→192.168.0.76 | peer punch 13:03:01.09(=NAS 13:02:55.8) → epoch7 | 성공, 직결(relay bound 없음) |
| 13:02:59 | PC2 | via=wire→211.218.222.1:43000 ×3(0/100/300ms) | **peer punch 로그 없음**. relay bound 13:03:02, c2h=39 = PC2 reject 38건(13:03:01.95~11.8) | **실패** closed newer-connect, h2c=0, ackMs=never |
| 13:03:14 | PC2 | 동일 | **peer punch 없음**. 13:03:19.209 **하트비트**로 capability 2개(54808,52336) 동시 수신 → epoch21 | 5초 뒤 active(helloAck 2674ms) — 25s 하트비트 덕 |
| 13:03:21 | PC2 | 동일 | peer punch 13:03:21.040(1회) → capability 13:03:24.536(3.5s) → epoch22 | active helloAck 1020ms; 13:04:33 client silent |
| 13:03:41 | 집 Desktop | via=lan | peer punch 13:03:47.3 → epoch8 | 성공, 직결 |
| 13:09:35 | PC2 | 동일 | peer punch 13:09:35.588/.750 → capability .010(0.4s) → epoch23 13:09:38 | **성공 3초**(helloAck 2ms) |
| 13:09:42 | 집 Desktop | via=lan | 직결, 13:11 현재 스트리밍 | 성공 |

PC2 reject 66건은 전부 13:03 분에만 있음. 오늘 이전 3일(09-18~20) PC2 로그에 directory 관련 줄 0.

## 코드 사실
- `server.js`(배포본): `sendWakePunch(observeSock, …)` :1382 → **UDP 29181 에서 송신**, `WAKE_PACKETS=[0,100,300]`, `WAKE_MIN_INTERVAL_MS=1000`. 릴레이의 host 방향 전달도 `observeSock.send` :1147. 즉 **wake 와 relay-hello 는 같은 5-tuple**(175.207.45.151:29181 → 211.218.222.1:43000).
- 호스트 `directory_client.cpp:504-509`: kind=Punch 수신 시 `refreshRequested_.exchange(true)` 가 false→true 일 때만 "directory peer punch" 로그. 소비는 하트비트 루프의 **sleep 구간에서만**(:1277, 200ms slice). 주기 25s(`hpp:51`), HTTP 호출당 `kHttpTimeoutMs=6000`(:34), 한 사이클이 health→observe→heartbeat 순차.
- 호스트 `host_startup_control.cpp:311`: capability 미보유 시 hello 는 reject 만, refresh 촉발 없음.
- 뷰어 `viewer_udp_session.hpp:36` hello `budgetMs=10000` (< 25s). e783733 본문이 이미 "Hello budget < heartbeat" 를 후속으로 명시.
- **하트비트/observe 시각은 서버·호스트 어느 쪽도 로그하지 않음**(서버는 LAN 호스트만 `[observe]`). PC2 로그에 directory online/timeout 상태 줄 0건.

## 판정
1. **간헐 = 서버 wake datagram 이 PC2에 닿았는지 여부.** 닿으면 3초 접속. 안 닿으면 capability 가 다음 25s 하트비트까지 못 오고, 뷰어(10s budget)는 그 전에 포기 → 사용자 재클릭이 직전 시도를 죽임(newer connect).
2. 집 Desktop 은 item 12(via=lan) 수정 후 **3/3 직결 성공** — 그 수정은 현장 동작 확인.
3. 같은 5-tuple 의 relay-hello 는 통과하고 wake 만 사라진 이유는 현재 로그로 분리 불가. 남은 가설:
   **H1** 회사 NAT/방화벽 UDP 매핑 만료 < 25s (PC2 OBSERVE 직후만 열림; relay-hello 는 그 창 안) /
   **H2** PC2 `refreshRequested_` 가 이미 true(하트비트 스레드가 sleep 밖 HTTP 지연 중)라 로그도 즉시 소비도 없음.
   둘 다 하트비트/observe 시각 로그 없이는 확정 불가.
4. **포트포워딩은 답이 아님**: 회사 공용망이라 불가(사용자 확인)이고, wake/keepalive 가 맞으면 필요도 없음.

## 수정 방향 후보 (Codex 검증 요청)
- **F1** 뷰어 Hello budget ≥ 하트비트 주기+여유(≈30s) + "호스트 응답 대기" 표시 — 실패를 지연으로 바꿈 (e783733 후속).
- **F2** 호스트 NAT keepalive: observe 엔드포인트로 ~10s 마다 소형 datagram(또는 하트비트 25→10s) — H1 대응.
- **F3** 서버 wake 재송신: 호스트의 다음 하트비트가 capability 를 가져갈 때까지 1s 간격(상한 ~10s) — 일시 손실 대응.
- **F4** 진단 로그: 서버 per-host observe 도착(off-LAN 포함, wire tuple) · 호스트 하트비트 사이클 시각/HTTP 실패/observe timeout · 뷰어 접속 단계 로그를 **실패/짧은 세션도 업로드**(오늘 실패 시도의 viewer.log 가 NAS 에 없음).
- 별건: 회사 뷰어↔회사 PC2(같은 LAN)이 NAS 릴레이로 5.7MB 스트리밍 — LAN 후보가 안 이긴 이유 미확인.

## Codex 검토 r1 (remote_codex 회신, 정본 = seq 2661 한국어본; 판단 동일) — 정정과 조건
- 판정1 정정: "wake 미도달" 은 **미확정**. Punch 로그는 false→true 때만 찍히므로 H2 로도 사라진다. 확정 범위는 "capability 미보유 reject 와 취득 지연" 까지.
- 13:02:59 는 newer-connect 로 닫힌 것이지 10s budget 만료의 실증이 아님. 실패 시도의 viewer.log 가 없으므로 모든 실패를 한 원인으로 묶지 말 것. 25s 도 HTTP/observe 처리 포함 취득 상한이 아님.
- H3 추가(후보): 광고 tuple 진부화 / 같은 tuple 을 다른 시각에 다른 socket·PID 가 소유 / 수신 후 형식·세대·송신원 검사나 수신 큐에서 폐기 / refresh 소비·재설정 경합. 같은 5-tuple 이라도 같은 시각·같은 socket 도달 동등성은 미증명.
- **F4 최우선**: attempt/session 세대를 대응시키고 Punch 수신(중복 포함)→flag 이전값→refresh 소비→HTTP 시작/종료→capability 발행/취득/적용→hello 수신/reject 사유를 시각과 함께 추적. 비밀값 기록 금지, 로그 유계.
- F1 30s: 실험값으로 가능, 제품 상한으로는 미승인. 25s 대기+실측 HTTP/observe 지연을 포함한 총 deadline 설계와, 취소·newer-connect 시 구 처리/지연 도착 결과가 남지 않는 회귀 필요. 대기 연장만을 원인 수정으로 삼지 말 것.
- F2 10s: 실험값. NAT 수명 미측정이라 보장 불가. 실제 43000 수신 socket/같은 mapping 에서 기존 observe 계약으로 보내는지 확인. HTTP 주기 단축과 UDP keepalive 를 혼동하지 말고, 다단말 부하와 HTTP 지연 중 독립성을 평가.
- F3 1s/10s: 실험 상한. 기존 3발 포함 총 송신 수·host 단위 dedup·최신 attempt/세대·전체 rate 상한 명시. heartbeat 도착만으로는 capability 수령/적용 확인이 아니므로 정지 조건 의미 한정.
- **F5 추가**: capability 미보유 hello 로부터 refresh 를 유계로 촉발할 수 있는지 검토. 미인증 UDP 가 무제한 HTTP 를 일으키면 안 됨. 대기 중 flag 재통지 손실·sleep 밖 지연의 반례 테스트를 먼저 작성.
- 회사 포트포워딩 요구 안 함에 동의. "불가"는 사용자 제약, "불필요"는 외향 통신 경로가 성공하는 조건에서만. keepalive/wake 만으로 모든 망의 접속 보장이라고 쓰지 말 것. 이번 라운드 코드·설정 변경 없음.

## pc2-connect-diag r1 — 작업용 완료·remote_claude 독립 검증 (2026-09-21 14:4x)
- 커밋(audit/host-pc-recovery, worktree D:/remote/remote-worktrees/host-pc-recovery): 541abf5(서버 A) · 2b65c06(호스트 B) · a13f480(뷰어 C) · bdaefc7(0.2.131 bump). 24파일 +1480/-25.
- 독립 실행: directory suite `node test/run.js` → "RESULT: ALL PASS", exit 0.
- 비밀값 grep: 신규 로그 줄에 token 원문 없음(observe token 은 8-hex 다이제스트, capability 는 mint/collect 사실만).
- 설치본 `…/host-pc-recovery/.claude/rel/0.2.131/payload/`: SHA256SUMS 전량 OK. GNLinkSetup.exe = eb5abbabd0c1ccac9ca63cb2610552974bb06e47e5ec671a25fc36776c1a7e21. UTF-16 버전 문자열: 0.2.130 = 0건, 0.2.131 = Setup5/Host1/Viewer1/Client1/Stream1(Capture·InputService·Updater 는 원래 없음).
- 서버 패치 `apps/directory/deploy/2026-09-21-connect-diag.patch`: `---/+++` 헤더가 없어 `patch` 가 바로 못 읽음 → 헤더 2줄 붙여 `patch -p0` 로 라이브 사본(sha e627048c…)에 적용 성공, `node --check` OK, 결과 sha **27b730e7a3c89631…**, 추가 줄 수 일치. wake_target.js 변경 없음.
- **배포 미완**: server.js 교체+`systemctl --user restart remote60-directory` 명령이 세션 권한 분류기에 막힘. 13:30 이후 relay 세션 0건(집 PC 는 직결)이라 재시작 영향 없음. 백업 이름 규칙 server.js.bak-<ts>-e627048c.
- 작업용 부수 발견(범위 밖, 미수정): ① drop_should_report(0) 오판(무해, 고침) ② credentials 지운 업로더가 종료 시 0xC0000409 크래시 — ledger 후보.

## 배포 완료 (사용자 "nas랑 설치파일들 전부 배포해" 지시, 2026-09-21 14:45~14:5x)
- **디렉터리 서버**: server.js e627048c… → **27b730e7a3c89631…**(연결 진단 패치 적용본), 백업 `server.js.bak-20260921T144521-e627048c`, `systemctl --user restart` 14:45:21, active, 재시작 직후 `[observe-rx]`·`[host-hb]` 줄 확인. wake_target.js 변경 없음. 릴레이 세션 0건 상태에서 재시작.
- **0.2.131 게시**: `automation/gnlink_deploy.sh` 정식 경로, exit 0. manifest sha256 c760c597…, sig 3a83d9be…, 아티팩트 10개 `/opt/gnlink/updates/0.2.131`, 0.2.130 pair 백업, 외부 https 10개 서명대로 해시 일치. manifest 생성 `.claude/gen_manifest_131.py`, 서명 `.claude/sign_release_131.ps1`(운영키, self-verify + 제품 공개키 verifier 통과), payload-set 게이트 PASS.
- 남은 것: PC1·PC2 실기 업데이트(인앱 업데이트 또는 https://rem.shotan.net/updates/0.2.131/GNLinkSetup.exe) → PC1→PC2 접속 반복 → NAS 로그로 원인 확정.
