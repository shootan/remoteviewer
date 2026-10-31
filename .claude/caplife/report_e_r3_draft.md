
### ② e r3 — Codex `codex_ka_e_src_r2.md` R1–R3·D1 계측 보수 (2026-10-28)

**커밋**
- ka-r3 **0fb4676**: D1(Punch 를 stray 와 분리) + R2(`StandbyRunner::ReleaseWhere(prefix)`).
- feat/apk-0.2.33:
  - **f51e8c6** merge ka-r3 — 0fb4676 와 함께 **6c3cb5a·20bc108·534242b·ecf1004**(Host punch 큐 수명·시험 root·주석)가 들어왔다. feat 의 Host 는 이제 검수된 ka-r3 계열과 같다.
  - **6451ec1** R1·R2·R3·D1 status.
  - **1aecc48** R3 판단을 순수 함수(`StandbyKeep.isReturn`)로 옮기고 단위 시험을 붙였다. 동작은 6451ec1 과 같다.
- NP 소유 파일은 건드리지 않았다.

**R1 — 목록 복귀의 끝은 그 entry 와 함께 사라진다**
- `forgetKept(handle)` 가 그 handle 의 `listReturnEnds` 항목을 지운다(`list_return_dropped`). 카드 끊기·discard 등 entry 를 잊는 모든 경로가 여기를 지난다.
- `endAllKept` 가 남은 항목을 모두 지운다. 모두 끊기·앱 이탈·로그아웃·세션 거부가 여기를 지난다. native end-all 이 그 세션들을 끝낸다.
- `startStandby` 자체가 검사한다: 끝난 화면(`owner_ended`), 다른 로그인(`not_this_sign_in`), 사용자가 놓은 PC(`let_go`)에는 lease 를 만들지 않는다(`standby_start_skipped`). 목록 복귀의 끝이든 앱의 자동 준비든 같다.
- kept 카드의 끊기도 그 PC 를 suppress 한다(lease 카드의 끊기와 같게).
- 다른 PC 를 탭한 것만으로는 아무것도 취소하지 않는다(E7e 유지).

**R2 — 화면(Activity)마다 자기 lease 만 정리한다**
- native runner 는 process 에 하나, 화면은 여럿이다. 화면마다 owner prefix `o<n>|` 를 둔다(n 은 process 안에서 재사용하지 않음).
- 모든 native standby 호출은 `standbyKey()` 를 거친다(이전 `StandbyKeep.nativeKey` 직접 호출 5곳). native 쪽(`standby_session_link`)은 Kotlin 이 준 key 만 쓰고 따로 만들지 않는다(확인).
- 화면 정리는 `nativeStandbyReleaseOwner(prefix)` → `ReleaseWhere` 다. 옛 화면의 늦은 정리는 자기 lease 는 회수하고(누수 없음) 새 화면의 lease 는 건드리지 않는다. 같은 PC 라도 그렇다.
- `ReleaseAll` 은 runner 의 끝(소멸자)에만 남는다.

**R3 — 복귀의 fallback 도 clipboard baseline 을 유지한다**
- `PendingCommit.preparedReturn`: 사용자가 standby 인 PC 로 돌아간 것.
  - activation 으로 연결된 경우.
  - activation 이 실패하고 **같은 탭**이 일반 연결로 간 경우(RETRY).
  - lease 가 아직 준비되지 않아 그 lease 를 놓고 일반 연결로 간 경우. 이유: 사용자가 PC 목록으로 떠난 PC 로 돌아가는 것이고, baseline 을 seed 하는 쪽이 자동 push 를 줄이는 안전한 방향이다.
- 이 셋은 경로(pathKind)와 상관없이 seed/hold 로 시작한다. 사용자가 따로 요청한 cold 접속은 이전처럼 Fresh 다.
- 판단은 `StandbyKeep.isReturn(preparedReturn, pathKind)` 한 곳이다.

**D1 계측 — strays 는 영상 tail 계수가 아니다**
- Codex probe(세션 없이 Punch 1개 → strays 1, tail 1)를 그대로 받아들인다.
- lease 가 target 에서 온 Punch 를 따로 센다(`punches`). strays 는 이제 "standby·HelloAck·Punch 가 아닌 target datagram" 이다. 영상·제어의 개수가 아니다.
- **r2 보고 정정**: r2 의 "ke_r10 E7b strays 27 → 49" 와 "ke_r16·r17 strays 23–25" 를 **잔류 영상 tail** 로 쓴 문장은 철회한다. strays 는 종류를 가리지 않은 계수였다.
- ke_r20(분리 계수, E1–E9 구간)의 `standby_back`/`standby_tail_quiet` 16줄: **punches 는 모두 0**, strays 는 4·25·28·49·50(대부분 25 의 배수).
  - 이 fixture 는 relay 경로다. relay 는 Host 의 Punch 를 client 로 넘기지 않는다(`apps/directory/server.js:2119`, `handshake.kind === UDP_KIND_PUNCH` 이면 return). 그래서 여기서 punches=0 은 당연하고, r2 의 25 들도 Punch 가 아니다.
  - 그러면 그 25 개가 무엇인지(영상 조각·제어·그 밖)는 **확인하지 못했다**. lease 는 종류별로 세지 않는다. Codex 의 probe 는 직접 경로(relay 없음)에서 Punch 가 stray 로 세이던 것을 보였고, 그것은 이번 분리로 막혔다.
  - 따라서 strays 는 "target 에서 온 비-standby·비-Punch datagram 수" 로만 쓴다. 영상 잔량이나 "안정 standby 영상 0" 의 근거로 쓰지 않는다.

**시험 결과** (모두 exit 와 RESULT 를 확인)

| 시험 | 결과 |
|---|---|
| `standby_lease_test` (0fb4676) | **75/75**, ×3. 새 9개: Punch 5개 → punches 5·strays 0·tail 없음, ReleaseWhere 가 o1 만 회수(o2·o10 유지, Host release 2), 옛 key 의 Provide 무효, 빈 prefix 0 |
| 변이 | Punch 를 stray 로 셈 → FAIL, ReleaseWhere 를 ReleaseAll 로 → FAIL(3). 복원 후 75/75 |
| e2e `--product-client --real-directory --old-host` (ka-r3 0fb4676, `kae_e3`) | **78/78**, scenario rc 0 |
| Kotlin 단위 (1aecc48) | **313/0**, rc 0. 새 3개: punches 읽기, owner prefix, 복귀 fallback 의 baseline. 변이(fallback 의 preparedReturn 무시) → 그 시험 FAIL |
| AVD ke_r20 (6451ec1 APK) | __KE_R20__ |

**AVD 실 UI — R1** (E7h, ke_r20)
- 방법: C 를 일반 연결 → targets 화면 → Host fault `pong_delay_ms=2000` 으로 PC 목록의 fence 를 약 2 s 붙잡는다 → fence 도중(목록 탭 후 약 0.4 s) 아래 동작 → 8 s 뒤 확인. 확인 항목: `list_return_dropped` 있음, `list_return_end`·`standby_first_transition`·first_transition `standby_start` 없음.

| 경우 | 동작 | 결과 |
|---|---|---|
| a | C 의 카드 끊기 | __E7H_A__ |
| b | 모두 끊기(B 를 먼저 대기시켜 배너를 띄움) | __E7H_B__ |
| c | 앱 이탈(HOME) → 복귀 | __E7H_C__ |
| d | 로그아웃 → 다시 로그인 | __E7H_D__ |

**AVD 실행 경과**
- ke_r20: E1–E9 40/40 PASS 뒤 E7h(a) 에서 하네스 오류(끊기 위치를 잴 카드가 없음) → 각 경우에 B 를 먼저 대기시켜 위치를 재게 고침.
- ke_r21: 29/29 PASS 뒤 **E7e 에서 B 세션이 `peer_lost` 로 끝나** 하네스가 viewer 메뉴를 찾지 못하고 멈췄다. 미해결 관찰:
  - B 는 activation 으로 열리고 영상·창 선택까지 정상이었다(23:32:06–19, 폰 시각).
  - 그 뒤 약 23:32:18 부터 양방향 제어가 끊겼다. Host 는 08:32:27 `udp control session ended … reason=peer-lost`, 폰은 23:32:29.5 `keep_end reason=peer_lost`(Host 시계가 폰보다 약 1 s 늦음). Host 는 그동안 영상을 계속 보냈다.
  - relay 의 B standby 세션(b2cb645a, client 127.0.0.1:61476)은 닫히지 않았다. C 의 새 lease(client 61161)와 endpoint 가 겹치지 않는다. C 의 첫 전환은 23:32:01 에 이미 끝나 있었다(동시 진행 아님).
  - relay 에 패킷별 계수가 없어 끊긴 쪽(폰 송신 / relay 전달)을 가리지 못했다. **원인 미확정**. ke_r16–r19 에서는 같은 시나리오가 4회 통과했다. 간헐 결함일 수 있어 1회 통과로 닫지 않는다.

**미검증·차단**
- **R3 실 clipboard: 차단**. 이유는 사용자 clipboard 보호다.
  - 공용 에뮬레이터(emulator-5554)는 `-no-window` 로 떠 있고 clipboard 공유를 끄는 인자가 없다. emulator `-help` 에도 clipboard 옵션이 없다. headless 에서 guest clipboard 가 Windows clipboard 로 동기화되는지 확인할 수 없었다.
  - 그래서 AVD 의 clipboard 를 바꾸는 시험(폰 copy, Host→폰 sync 포함)은 하지 않았다. 동기화가 켜져 있다면 사용자의 Windows clipboard 를 덮어쓴다.
  - 하지 않은 것: 실 clipboard 의 자동 push 0, 새 copy 1회, 다른 PC baseline 오염 0. Host 쪽은 private window station(parkclip 방식)으로 격리할 수 있지만, 폰 쪽 격리가 먼저 필요하다.
  - 진행하려면 둘 중 하나가 필요하다: clipboard 공유가 꺼진 것이 확인된 별도 에뮬레이터, 또는 실기기. 결정 요청.
  - 또 activation 실패를 결정적으로 만드는 Host fault 가 없다. fallback 경로의 AVD 확인에는 pinned 시험 Host 에 fault 추가(공용 Host 코드, seam 빌드 한정)가 필요하다. 하지 않았다.
  - 지금 근거: 소스 경로 + 순수 판단 단위 시험(변이 확인). AVD 로그에서는 지금까지 모든 activation 복귀가 `clipboard_baseline start=hold` 였다(폰 clipboard 비어 있음).
- **R2 Activity 교체 실 UI**: 하지 않았다. capability job 을 결정적으로 멈추는 수단이 AVD 에 없다. 근거는 native 두 owner 시험(위)과 owner prefix 단위 시험이다.
- N4 raw-IP+AUTO 실 UI, prelogin 실 UI, 실 NAT·실기기·PC 교차·비용·p95: 기존 그대로 미검증.

**고정 대상**
- feat **1aecc48**(APK 동작은 6451ec1 과 같음), ka-r3 **0fb4676**. tracked clean.
- ke_r20 APK(6451ec1 소스): off a2a2c0db6fcf7203…, on e33d0f48f6b6c5f2….
- 하네스 sha256 앞 16자리: __HARNESS__
