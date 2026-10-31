> **대체됨 → `proposal_np_r2.md`(2026-10-27, Codex codex_np_design_r1.md 반영).** 아래는 r1 원문 보존.

# proposal np-design r1 — 직접 연결 우선 + 지연 없는 relay fallback (작업용2, 2026-10-27)

> task np-design / r1 / kind=design. 제품 코드 변경 0, 공유기·NAS·운영 서버 무변경, 시험 실행 없음(읽기 전용 측정만).
> 소스 기준 **main 2137f3a**(`git show main:` 로 읽음; 작업 트리는 refactor/viewer-split). KA 비교 대상 `spike/host-admission-r3` cf8fb70.
> 결정은 Codex 확인 사항(§6). **측정 = 로그로 본 사실**, **가설 = 원인 후보**로 구분해 적는다.

## 0) 요약

- **측정 1**: 회사 → 집 PC(.76)에서, Host 의 공유기 관찰 port 가 bind port 와 **다르면 전부 중계**였다(10-22 오전 29814/16016; 서버 bound 19/19, Host 쪽 판정 14 중계·1 판정 불가). **같으면(3478)** 10-22 16:34 이후~10-23 은 **22/23 직접**(판정 불가 1 제외), 10-26 은 **10/19 직접**이다. 10-23→10-26 하락은 같은 Host 프로세스(pid 2456)·같은 버전(0.2.162)·같은 3478 이라 **원인 미설명**이다.
- **측정 2 (메커니즘)**: 직접 연결에 성공한 경우, Host 가 `directory punch ->` 를 보낸 뒤 **3~11 ms 안에** control 세션이 열린다. 기간 전체 Host 로그에 회사 IP 에서 온 punch 수신(`[dir-punch] from=211.…`)은 **0건**이다. 따라서 직접 연결은 "뷰어 punch 에 Host 가 답한 것"이 아니다. **Host 의 outbound punch 가 뷰어에 도착해 뷰어가 그 출처를 후보와 일치로 인정한 것**이다.
- **측정 3 (타이밍은 원인 아님)**: wake 에서 Host 첫 punch 까지 걸린 시간은 직접일 때 중앙값 331 ms, 중계일 때 375 ms 로 같다. 실패한 경우에도 양쪽이 2.5 s 내내 punch 를 보낸다(Host 25×200 ms, 뷰어 150 ms 간격). 실패는 **tuple 불일치**다. 어느 쪽 tuple 이 틀렸는지는 미확정이다(§2 H-a/H-b).
- **설계(한 안, 단계형)**: ⓪ 실제 socket 관찰 계측 → ①a peer-reflexive 수용(Host 실제 외부 tuple 을 뷰어가 받아들임) → ①b relay 를 즉시 준비하고 확정 시점은 뷰어가 정한다(새 뷰어만, 구 뷰어는 서버 2.5 s grace 그대로). 기대 효과: 중계로 끝나는 접속의 대기 2.5 s → Codex 가 정하는 확정 창(근거 값 ≈0.45~0.65 s). 직접 성공 비율은 ⓪ 결과를 보고 판정한다.

## 1) 측정 (출처 고정)

**출처**
- 서버: NAS `journalctl --user -u remote60-directory`, 2026-10-22 01:28:45 ~ 10-27 12:18 KST(10-22 문서 기간 바로 뒤), 77,624줄.
- PC 뷰어: NAS `shotan/68f79d01…/viewer.log*`(회전 때문에 10-26 11:15 이후만 남음, 0.2.162).
- 폰: `SM-S948N-b7378c/apk.log`(10-22~, 0.2.31).
- 집 PC Host(.76, hostId f6e8e54a): `8ec6ecb1…/host.log*`.
- 증거 사본: `.claude/np_evidence_2026-10-27/`. **클라이언트 IP 포함, 미추적 운영 자료라 커밋·업로드하지 않는다.**

| 파일 | sha256(앞 16) |
|---|---|
| journal_since_1022.txt | fc0b90fcbf4a17bd |
| viewer_attempts.txt | 1d0c27a40f7ae9f0 |
| apk_punch.txt | b302c483121c9b73 |
| homepc_host_punch.txt | 3395c1c77e2181f0 |
| aggregate_np.py → aggregate_out.txt | 037d927a3bed6539 → 2ca989901c8d8b4d |
| homepc_host_timing_summary.txt | cbbbea6f3d2800ac |

**판정 정의**
- 서버 `bound` 는 뷰어가 relay 를 골랐다는 뜻이다.
- 뷰어 로그에서 relay 가 이기면 `kind=public` 으로 찍힌다(10-22 문서 §6 라벨 손실, 이번 로그에서 재확인). 그래서 `directory chose …:29190` 으로 구분했다.
- Host 쪽 판정: `directory punch ->` 뒤 control session 이 <1 s 안에 열리면 직접, 약 2.1 s 뒤에 열리면 중계(relay grace).

### 표 A — 집 PC(.76) ← 회사 client: Host 쪽 판정, 기간·관찰 port 별

| 기간 | Host bind → 공유기 관찰 port | 직접 | 중계 | 판정 없음 |
|---|---|---|---|---|
| 10-22 10:07~16:26 | 3478/43000 → 16016·29814 (재매핑) | 0 | 14 | 1 |
| 10-22 16:39~18:57 | 3478 → 3478 (보존) | 8 | 0 | 1 |
| 10-23 | 3478 → 3478 | 14 | 1 | 0 |
| 10-26 | 3478 → 3478 | 10 | 9 | 0 |
| 10-27 | 3478 → 3478 | 0 | 1 | 0 |

- 표 A 는 회사 대역(211.218.222.x) client 만 센다. 같은 날 이동통신 118.235 1건(19:16)은 직접이었다.
- 서버 기준 교차 확인(aggregate_out): 관찰 port 보존 44 connect 중 bound 11, 재매핑 19 중 bound 19.
- 이동통신 175.209.x → 집 PC 는 7/7 중계, 118.235.x 는 1/1 직접.
- 집 PC2(.16, 관찰 port 5236, bind 43000 → 재매핑)는 회사 PC 뷰어에서 2/2 중계(10-27 12:04, 12:04 — 사용자 실기 재현).

### 표 B — 경합 응답 시간 (뷰어·폰이 직접 기록)

| 경로 | 결과 | n | ms |
|---|---|---|---|
| 회사 PC 뷰어 → 집 PC(3478 보존), 10-26~27 | 직접 | 9 | 281~415 (중앙 357) |
| 〃 | 중계 | 9 | 2501~2507 |
| 회사 PC 뷰어 → 집 PC2(재매핑) | 중계 | 2 | 2502, 2505 |
| 폰 → 회사 PC (이동통신 175.209/118.235) | 직접 | 9 | 405~616 |
| 폰 → 집 PC (회사망/이동통신) | 직접 2 / 중계 7 | | 직접 554, 626 / 중계 2551~2769 |
| 폰(회사 Wi-Fi) → 회사 PC | 중계 | 9 | 2533~2870 |
| 회사 PC 뷰어 → 회사 PC (private) | 직접 | 2 | 0 |

- 폰 ms 는 `directory_connect` 에서 `directory_chosen` 까지의 로그 간격이라 PC ms 와 기준점이 다르다.
- 폰(회사 Wi-Fi) → 회사 PC 9/9 중계는 같은 망 hairpin/Wi-Fi 격리 문제(작업목록 SL)로, 이 설계의 범위가 아니다.

### 표 C — Host 쪽 타이밍 (집 PC .76, 10-22~27, n=67)

| 결과 | wake → `directory punch ->` (ms) | punch → session |
|---|---|---|
| 직접 33 | 262~463, 중앙 331 | +2~28 ms |
| 중계 32 (+판정 불가 2) | 264~482, 중앙 375 | +~2.0~2.3 s (relay 응답 시점) |

- 기간 전체 Host 로그에서 회사·이동통신 IP 로부터의 `[dir-punch] from=` 수신은 0건이다. 모든 수신 출처는 192.168.0.6(디렉터리 wake)이다.
- 10-26 직접 4건(11:37, 14:39, 14:44, 15:55)과 중계 3건(11:15, 12:06, 15:53)을 원문으로 대조했다. 직접에서는 punch 송신 3 ms 뒤 `session epoch peer=new` 가 열린다. 중계에서는 약 2.1 s 뒤 `endpoint translated expected=211.218.222.1:P actual=192.168.0.6:29181` 이 찍힌다.

### 소스 사실 (main)
- **뷰어 `PunchAny`** (`directory_rendezvous.cpp:276-379`, PC·APK 공용):
  - 150 ms 마다 모든 후보에 49 B Punch 를 보낸다. 예산은 4000 ms(`directory_session_bootstrap.hpp:48`).
  - "answered" 는 **출처가 후보 ip:port 와 정확히 일치하는 아무 datagram** 이다(:359-367). 내용은 검사하지 않는다. **일치하지 않는 출처의 수신은 기록 없이 버린다.**
  - punch 한 socket 이 그대로 media socket 이 된다(`viewer_startup.cpp:410`, APK `native_bridge.cpp:316`).
- **Host** (`directory_client.cpp`):
  - wake → 200 ms sleep slice → observe(고정 250 ms) → heartbeat HTTP → `Punch()` 순서다. Punch 는 25×200 ms 이고 media socket 에서 나가며, 대상은 서버가 관찰한 뷰어 tuple 하나뿐이다(:1410-1459).
  - **Punch 패킷은 `UdpHelloPacket{}` 에 kind 만 채운다. 토큰이 없다**(:1447-1448).
  - 수신 punch 응답은 directory 이벤트로 열린 10 s 창 안에서, 출처로 1개씩 보낸다(`punch_reply.hpp`). 즉 **Host 는 이미 출처 기준으로 답한다(peer-reflexive 수용과 같은 동작)**.
- **서버**:
  - 후보 순서는 private → public → public-alt → relay(`server.js:1254-1282`).
  - LAN 에서 관찰한 Host 는 공인 IP + hairpin port 로 광고한다(`observedAddressFor` :315-326).
  - relay punch 응답은 첫 punch 뒤 2500/2620/2740 ms 에 3회 보낸다(:1677-1692).
  - **집 LAN 밖의 관찰자가 없다.** 디렉터리·relay 모두 NAS(집 LAN)에 있어, 집 Host 의 외부 mapping 은 hairpin 으로만 보인다.

## 2) 원인 가설 (미확정)

- **H-a — 집 공유기 쪽**: `.76:3478 → 회사` 의 outbound mapping 이 hairpin 에서 관찰한 3478 이 아닌 port Y 를 받는다.
  - 그러면 Host punch 는 `175.207.45.151:Y` 에서 뷰어에 도착한다. 뷰어는 후보(:3478)와 다르다며 **조용히 버린다**.
  - 뷰어의 punch(→:3478)는 공유기 filter 에서 막힌다.
  - 10-22 문서의 "광고 port ≠ 실제 외부 mapping" 가설이 **port 보존 상태에서도 간헐적으로** 일어나는 형태다.
- **H-b — 회사 NAT 쪽**: 회사 NAT 의 `→175.207.45.151:3478` mapping 또는 filter 가 `→NAS 관찰 port` 의 것과 다르다(목적지 port 의존 mapping, 주소 풀 등).
  - 그러면 Host punch 는 회사 NAT 에서 버려지고, 뷰어 punch 는 집 공유기에서 버려진다.
- **판별 수단**: 지금 로그로는 둘을 가를 수 없다. H-a 이면 뷰어가 실패한 경합 동안 `175.207.45.151:Y` 출처 패킷을 **받고 있다**. 이것이 ⓪ 계측이 보려는 것이다.
- 재매핑 기간 19/19 중계는 H-a 와 같은 방향이다(Host punch 가 다른 port 로 나가면 뷰어가 버린다). 다만 그 기간에도 Host 의 실제 outbound port 는 관찰되지 않았다.

## 3) 설계안 (한 안 · 단계형)

### ⓪ 실제 socket 관찰 (동작 불변, 계측만)

**뷰어 `PunchAny`** — PC·APK 공용 `directory_rendezvous.cpp` / bootstrap:
- 경합 중 **후보와 일치하지 않는 출처의 수신**을 rate-limit 해서 집계한다. 출처 tuple, 길이, 49 B Punch 형식 여부, 첫 도착 ms, 개수를 남긴다. 토큰·키 원문은 남기지 않는다.
- 결과 줄에 후보별 송신 횟수를 함께 남긴다.

**Host** — Punch 송신 줄에 이미 있는 `directory punch ->` 에 더해:
- 창이 열린 동안 **비-directory 출처 수신**을 이미 `[dir-punch]` 로 남기고 있다. 그대로 쓴다.

**라벨 보정 (10-22 §6)**:
- PC 뷰어 경로에서 relay kind 를 보존한다. `kind=relay` 가 로그와 toolbar relayPath 에 남아야 KPI 를 뷰어 로그만으로 읽을 수 있다.
- 동작 변화는 relayPath 표시가 정확해지는 것뿐이다.

**⓪ 결과별 다음 단계**:
| ⓪ 관찰 | 뜻 | 다음 |
|---|---|---|
| 실패 경합에서 `175.207.45.151:Y` Punch 수신 | H-a 확인 | ①a 로 바로 해결 가능 |
| 수신 없음 | H-b 쪽(회사 NAT)이거나 Host punch 미도달 | ①a 효과 제한. 직접 비율 개선은 공유기/UPnP·외부 관찰자 같은 **별도 승인 사항**으로 넘기고, ①b(대기 제거)만 진행 |

- ⓪ 은 같은 릴리스(R2)에 ①a/①b 와 함께 실어도 된다. 판정 근거로 쓰는 것은 필드 로그다.

### ①a peer-reflexive 수용 (직접 경로 확보)

**새 뷰어**:
- 경합 중 다음 조건을 **모두** 만족하는 datagram 을 새 후보 `prflx` 로 추가한다.
  1. 49 B, kind=Punch.
  2. 출처 IP 가 host 의 public 후보 IP 와 같다.
  3. 출처 port 가 relay·public-alt 후보 port 가 아니다.
  4. (새 Host 일 때) connect 의 punchToken 과 일치한다.
- 추가 즉시 그 tuple 로 punch 를 보내고 Hello 로 진행한다.
- 동시에 "answered" 를 **내용 검사**로 바꾼다. kind=Punch(또는 relay 응답 형식)만 인정하고, 아무 datagram 이나 인정하지 않는다.

**새 Host**:
- `Punch()` 패킷 `authToken` 에 그 connect 의 punchToken 을 싣는다. punchToken 은 이미 `pendingPunch` 에 있다.
- 와이어 형식은 기존 필드를 사용한다. KA r3 의 `hostProof` 가 같은 필드를 standby 대상에 쓰므로, 필드 공유 규칙은 Codex 가 확정한다.

**구 Host 호환**: 구 Host punch 에는 토큰이 없다. 그래서 새 뷰어는 조건 1~3(약한 규칙)으로 수용한다.
- 위험: 같은 공인 IP 의 다른 장치(집 PC2, NAS)가 보낸 Punch 를 오인할 수 있다.
- 완화: 그 경로로 보낸 Hello 의 HelloAck(기존 인증)이 실패하면 버리고, 다음 후보/relay 로 간다. 이 경우 오인은 연결 실패가 아니라 지연으로 끝난다.
- Codex 결정: 약한 규칙을 허용할지, 새 Host 에만 적용할지.

**Host 쪽 변경**: 0 이 가능하다. 기존 Host 는 이미 출처로 응답하고, 창이 열려 있으면 prflx 뷰어 punch 에도 답한다.

**재매핑 Host 의 처리**: "hairpin port 가 재매핑된 Host 는 직접을 건너뛴다"는 **제안하지 않는다**. ①a 로 19/19 중계였던 경우가 직접이 될 수 있다.

### ①b relay 지연 제거

- 서버 grace(2.5 s)는 **줄이지 않는다.** 금지된 timeout 단축이 아니라 분기다.
- **새 뷰어**: `/api/connect` 에 capability 플래그(예: `relayEarly:1`)를 보낸다.
- **서버**: 플래그가 있는 connect 에 한해 relay punch 응답을 **즉시** 보낸다. 플래그 없는(구) connect 는 지금처럼 2500 ms 다.
- **뷰어**: relay 응답을 받아도 바로 확정하지 않고 다음 순서를 따른다.
  1. 직접(또는 prflx)이 답하면 직접을 고른다.
  2. 확정 창 `T_commit` 이 지나면 relay 를 고른다.
  3. relay 확정 직후에도 그 socket 으로 들어오는 직접 응답은 무시한다(경로 전환 없음).
- **`T_commit` 은 Codex 결정**. 근거:
  - 이번 표본의 직접 성공 최대는 PC 415 ms, 폰 626 ms(폰은 기준점이 다름).
  - prflx 는 RTT 1회(≈3~5 ms)를 더한다.
  - Host 첫 punch 는 wake 기준 최대 482 ms 다.
- 너무 짧게 잡으면 직접으로 됐을 접속이 중계로 확정된다(대역·NAS 부하). 회귀 판정은 KPI 의 직접 비율로 한다.
- 제안값 **1000 ms**: 관찰된 직접 성공을 모두 포함하는 값이다. 고정값이며 설정으로 바꿀 수 없다. 단축 실험이 아니라 측정 상한에 여유를 둔 값이다.

**기대 효과**:
- 중계로 끝나는 접속: punch→선택이 약 2.5 s → 약 1.0 s.
- 직접 접속: 변화 없음(281~415 ms).

**보류한 대안 — relay 즉시 확정 후 직접으로 이전**:
- 대기가 0 이 되지만 다음이 필요하다. 활성 세션의 endpoint 이전(Host `endpoint translated` 처리), NACK/epoch 상태 이전, control-resume 상호작용, KA relay lease 와의 정합.
- R2 범위 밖으로 둔다.

### 선택 (낮은 우선) — wake 에 client tuple 싣기

- Host 첫 punch 가 약 330 ms 앞당겨진다. 하지만 이미 성공하는 경우만 빨라진다.
- wake 진위(위조 wake 로 Host 가 임의 주소에 punch 를 25발 보내게 할 수 있음) 문제가 있어 **보류**한다.

## 4) 프로토콜·서버 변경, 호환

| 단계 | 와이어/서버 | 구 Host | 구 뷰어/구 APK | 새 뷰어 + 구 서버 |
|---|---|---|---|---|
| ⓪ | 없음 | 무관 | 무관 | 무관 |
| ①a | Host punch 에 token(기존 필드 사용, KA hostProof 와 규칙 확정) | 약한 규칙으로 동작, Host 변경 0 | 영향 없음(Host 가 token 을 실어도 구 뷰어는 내용을 보지 않음) | 무관 |
| ①b | `/api/connect` 플래그 1개 + relay 응답 분기 | 무관 | 플래그 없음 → 2500 ms 유지, **회귀 없음** | 서버가 플래그를 모르면 2500 ms 응답 → 지금과 같음 |

- 서버 배포(①b)는 운영 배포이므로 R2 게시 절차 안에서만 한다. 이번 r1 에서는 하지 않는다.

## 5) KA r2 와의 관계 · 파일 소유

**KA r3 diff**(`main...spike/host-admission-r3`, 23 files): `server.js`, `directory_client.cpp/.hpp`, `host_startup_connect.cpp`, `host_startup_control.cpp`, `poc_protocol.hpp`, `host_standby_*`.
- `relayScheduleReply`·grace·후보 순서·`observedAddressFor` 는 변경 없음.
- `PunchTarget.hostProof` 가 punch `authToken` 을 사용한다.

**NP 파일**:
- ⓪/①a 뷰어: `directory_rendezvous.cpp`, `directory_session_bootstrap.cpp`, `directory_session_client.cpp`, `connect_candidates.hpp`, `viewer_startup.cpp`. KA diff 에 없다. 다만 PC·APK 공용 **shared native 라 writer 는 작업용1**이다.
- ①a Host token: `directory_client.cpp` — **KA 와 같은 파일**.
- ①b: `server.js` relay 응답 경로 — **KA 와 같은 파일**.

**순서 제안**:
1. KA r2 가 main 에 들어간다.
2. NP ⓪ + ①a 뷰어 + 라벨 보정.
3. ①a Host token + ①b 서버/뷰어(KA 위에 rebase).

- APK 는 공용 native 를 다시 빌드하면 ⓪/①a 를 받는다. ①b 플래그는 Kotlin `DirectoryClient.connect` 에 1줄이다.

## 6) Codex 확인 요청

1. 단계 구성(⓪ → ①a → ①b) 과 "⓪ 결과에 따라 ①a 효과를 판정" 하는 방식.
2. ①a 의 구 Host 약한 규칙(공인 IP 일치 + 비 relay port + HelloAck 실패 시 폐기) 허용 여부와 token 필드 규칙(KA hostProof 와 공유).
3. ①b 의 `T_commit` 값(제안 1000 ms 고정)과 플래그 방식. 서버 grace 2500 ms 는 구 connect 에 유지.
4. relay 즉시 확정 후 이전·wake tuple 은 R2 범위 밖 보류로 둘지.

## 7) 시험 계획 (격리만)

**단위/결합** — loopback 사용자 공간 NAT 대역(시험 전용 UDP 중계 프로세스, 127.0.0.1 bind):
- 모드 ① port 보존 + 주소·port filter.
- 모드 ② outbound port 재매핑(H-a).
- 모드 ③ 회사 쪽 목적지 port 의존 mapping(H-b).

| 모드 | main | ①a | 의미 |
|---|---|---|---|
| ② | **반드시 중계(직접 실패)** | 직접 성공 | 부정 대조. main 이 실패하지 않으면 시험이 무효 |
| ③ | 중계 | 중계 | 한계 확인 |

- ①b 시간 계측: relay 확정이 `T_commit ± 50 ms` 안에 일어나는지 본다.
- 직접이 `T_commit` 안에 답하면 relay 응답이 먼저 와도 직접이 선택되는지 본다.
- prflx 약한 규칙 부정 대조: 같은 IP 의 다른 port 에서 무효 Punch 를 보내면 HelloAck 가 실패하고 → 다음 후보로 간다.

**디렉터리**: `apps/directory/test` 에 플래그 분기를 시험한다(플래그 있음 = 즉시, 없음 = 기존 grace, 기존 시험 grace 400 ms 유지). 시험 임시 루트 규칙은 KA N4(`.claude/test-tmp`)를 따른다.

**필드 KPI** (게시 후, 사용자의 평소 사용 · NAS 읽기 전용):
- 회사 → 집 PC 의 `bound/eligible`. 기준선은 10-26 9/19, 재매핑 기간 19/19. 20 connect 이상으로 본다.
- 뷰어 `[attempt]` 의 punch→선택 ms 와 ⓪ 의 비일치 수신 집계.
- 집 PC2(재매핑) 2/2 중계가 바뀌는지.

**금지 유지**: 공유기·NAS 설정, 운영 서버 배포(게시 절차 밖), 사용자 설치본·서비스 조작, 화면 위 시험 창.

## 8) 위험

- prflx 오인: 위 완화(token, 구 Host 는 HelloAck 검증)를 둔다. 잔여 위험은 지연뿐이고, 다른 호스트 화면으로 연결되지는 않는다(HelloAck 인증).
- ①b 확정 창이 너무 짧으면 직접 비율이 떨어진다. KPI 로 감시한다. 값은 Codex 가 확정한다.
- shared native 공용 파일이라 APK 출하 주기에 묶인다.
- H-b 가 주원인이면 ①a 로는 직접 비율이 거의 오르지 않는다. 그때 개선은 ①b(대기 2.5 → ~1 s)뿐이다. 그 이상은 외부 관찰자·UPnP 등 **별도 승인 사항**이다.

## 9) ledger (고치지 않음, 기록만)

- `public-alt` 후보는 punch 에 절대 답하지 않는다. Host `lanSock` 이 agent 에 공급되지 않기 때문이다(`host_startup_connect.cpp:431-437`). 경합에서 이길 수 없는 후보다.
- 뷰어 "answered" 는 내용을 보지 않는다(①a 에서 함께 고침 — 기능 필수 범위).
- PC 뷰어 relay 라벨 손실(⓪ 에서 고침).
- 10-23 → 10-26 직접 비율 하락(같은 프로세스·버전·port) 원인 미설명.
