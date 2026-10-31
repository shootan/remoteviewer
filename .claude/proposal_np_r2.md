# proposal np-design r2 — 정본 (작업용2, 2026-10-27)

> **r1(`proposal_np_r1.md`)을 대체한다.**
> 반영한 판정: Codex `codex_np_design_r1.md` — 수정 조건부 GO, ⓪ + ①b 먼저.
> 측정 표·증거 해시는 아래 "측정" 절(r1 §1 재수록)에 있다. 증거는 `.claude/np_evidence_2026-10-27/`. 이 문서만으로 읽힌다.
> 구현: `feat/np-early-relay` c803768 (`report_np_impl_r1.md`).

## 변경 요약 (r1 → r2)

1. **원인 표현**
   - "실패는 tuple 불일치" 를 **가설**로 낮춘다.
   - wake 지연 분포가 겹친다는 사실만으로는 **타이밍 원인을 배제할 수 없다.** 손실·필터·Host/HTTP 일정·대조 시각 같은 다른 원인도 남는다.
   - Host `Punch()` 25×200 ms 는 **최대 송신 기간**이다. 실패할 때 양쪽 패킷이 상대에게 도착했다는 증거가 아니다.
   - Host 세션 시차로 추론한 직접/중계 분류는 실제 peer tuple 판정과 다르다.
2. **⓪ 계측의 해석**
   - 유효 Punch 가 공인 IP 의 다른 port 에서 도착하면, 그 시도의 후보 불일치를 **지지**한다.
   - 수신이 0 이면 H-b 확정이 아니다. "그 socket 에서 관측하지 못했다" 는 뜻이다.
   - 필드 KPI 20건은 초기 관찰값이다. 보편적인 개선률을 보장하지 않는다.
3. **HelloAck 은 상호 인증이 아니다.**
   - r1 §3 의 "HelloAck(기존 인증)으로 약한 규칙의 위험이 지연에만 제한된다" 는 **삭제**한다.
   - 인증 bit 와 정상 형식은 상대가 그 Host 라는 증명이 아니다.
4. **구 Host 무토큰 prflx 는 NO-GO**
   - 구 Host 또는 지원 여부를 모르는 Host 에는 정확한 tuple 후보 + relay 만 쓴다.
5. **①a 는 별도 후속**
   - 새 Host 지원을 신뢰 가능한 directory metadata 로 확인한 시도에만 적용한다.
   - 시도별 무작위 128-bit **connectivity proof** 를 인증된 connect 응답과 그 Host 의 pendingPunch 로 전달한다.
   - KA `hostProof`(relay host-facing pin 증명)와 분리한다. 49 B `authToken` 슬롯에는 목적지·용도별로 한 값만 넣는다.
6. **T_commit**: 새 협상 경로에서 1000 ms. 450~650 ms 는 혼합 기준 표본이라 근거로 쓰지 않는다.
7. **보류 유지**: relay 를 먼저 쓰고 직접으로 옮기는 방식과 wake 에 tuple 을 싣는 방식은 R2 범위 밖이다.

## 측정 (r1 §1 재수록 — r2 에서 이 문서만으로 읽히도록)

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

> r2 해석 주의(Codex): 표 C 의 겹치는 wake 지연은 타이밍 원인을 **배제하지 않는다**. Punch 25×200 ms 는 최대 송신 기간이며 도착 증거가 아니다. Host 쪽 직접/중계 분류는 세션 시차로 추론한 값이지 실제 peer tuple 판정이 아니다.

## 원인 가설 (미확정 — r2 표현)

- H-a 집 공유기 outbound mapping 이 hairpin 관찰 port 와 다름 / H-b 회사 NAT 의 목적지별 mapping·filter 차이 / 그 밖에 손실·필터·Host/HTTP 일정·대조 시각. 어느 것도 확정하지 않는다.
- 뷰어 `PunchAny` 는 후보와 일치하지 않는 출처를 버리므로(main) H-a 이면 그 패킷이 보이지 않았다 — ⓪ 계측이 이것을 기록한다.

## ⓪ + ①b 계약 (확정 — 구현 c803768 이 따르는 것)

1. **비일치 수신 계측**
   - 길이·magic·declared bytes·version·kind 유효성, 첫 도착 경과시간, tuple 별 집계를 남긴다.
   - 집계는 bounded 다(최대 8 tuple, 나머지는 개수만).
   - 원문 token·proof·패킷은 기록하지 않는다.
   - 기존 round/read budget 은 그대로 둔다.
   - 최종 직접/relay 라벨은 PC 끝까지 보존한다(`CandidateKind::Relay`). APK 는 kind 문자열을 원래 보존한다.
2. **`relayEarly` 플래그**
   - 유효한 인증·관찰·**legacy relay 예약**을 만든 그 connect 에만 적용한다.
   - 응답에 `relayEarly:"accepted"` 를 명시한다. 아닐 때는 이유를 준다(`standby`/`no-relay`/`not-accepted`/`no-wire-port`).
   - 즉시 응답은 그 connect 의 wire tuple 에서 온 punch 에만 한다.
   - 구 connect, 같은 wire IP 의 다른 소켓, standby 는 2500 ms grace 를 유지한다. 플래그를 모르는 구 서버와의 호환도 유지한다.
   - standby 와 legacy 는 섞지 않는다.
3. **T0 와 대기**
   - T0 는 prepared media socket 으로 첫 candidate Punch 를 보내기 직전의 monotonic 시각이다.
   - T0+1000 **전**에 온 유효 direct 응답이 relay 보다 우선한다. 정확히 1000 에는 commit 이 먼저다.
   - 수신 timeout 은 다음 송신 시각과 commit deadline 중 이른 쪽에 맞춘다.
   - 전체 connect 시각(`openMs`)은 따로 기록한다.
4. **확정 규칙**
   - 1000 ms 시점에 **검증된 relay 응답이 있을 때만** relay 를 선택한다.
   - 없으면 기존 4 s 예산 안에서 계속 기다리고, 유효한 응답이 오는 즉시 선택한다.
   - 구 서버의 2500 ms 응답보다 먼저 1000 ms timeout 으로 실패하지 않는다.
   - cancel 은 모든 선택보다 우선한다. 선택은 1회이고, 선택한 뒤에는 바꾸지 않는다.
5. **응답 검사**
   - 정확한 구조(49 B·magic·size 49·version ≥ 2·kind Punch)와 후보 tuple 을 검사한다.
   - **Punch 응답은 reachability 증거일 뿐 인증이 아니다.** Hello 와 세션 인증은 기존대로 한다.
   - relay 준비·경로 선택·HelloAck·첫 화면 지연은 각각 따로 측정한다. 1000 ms 선택이 첫 화면 1000 ms 를 보장하지는 않는다.

## 순서·소유 (갱신)

- **이번 묶음**: ⓪ + ①b. 기준 KA spike/host-admission-r3 0a5e71d. 이 기간 NP 파일의 writer 는 작업용2 다.
- **APK**: JNI `nativeDirectoryConnectAny` 의 PunchAny 호출부가 feat/apk-0.2.33 과 같은 줄이다. 그래서 **0.2.33 이 들어간 뒤** 두 변경을 함께 넣는다.
  1. JNI 가 commit 창을 PunchAny 에 전달한다.
  2. Kotlin `DirectoryClient.connect` 가 플래그를 보내고 응답을 파싱한다.
  - 플래그만 먼저 보내면 relay 즉시 승리 회귀가 생긴다. 공용 native 를 다시 빌드하는 것만으로는 APK 가 legacy 경합을 그대로 쓴다(형식 검사·계측만 받음).
- **①a**: 위 proof 계약으로 별도 단계. KA hostProof 경로와 겹치는 `directory_client.cpp` 를 건드리므로 KA 순서를 따른다.

## 필드 KPI (게시 뒤, 분모 고정)

- **대상**: 회사(211.218.222.x) → 집 PC(f6e8e54a), PC 뷰어 새 버전, connect 단위.
- **기준선**: 10-26 직접 10/19, 재매핑 기간 전부 중계.
- **지표**:
  - 서버 `[relay] ... eligible ... relayEarly=accepted` 대비 `bound` 비율.
  - 뷰어 `[race]` 의 decision·chosenMs·strangers.
  - 첫 화면(revealed) 시각.
- **부하**: relay 바이트·세션 수를 10-26 과 같은 시간대에서 비교한다.
- **분리**: 직접 비율 변화(①b 는 이 비율을 바꾸지 않는 것이 목표)와 대기 감소를 나눠 본다.
