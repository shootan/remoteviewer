
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
- Activity 의 ReleaseAll 호출을 제거했다. 미사용 JNI export(`nativeStandbyReleaseAll`)와 Kotlin external 선언은 소스에 남아 있고, runner 소멸자도 ReleaseAll 을 호출한다. ("소멸자 외 소스에 ReleaseAll 이 전혀 없다"는 뜻이 아니다.)

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
| e2e `--product-client --real-directory --old-host` (ka-r3 0fb4676, `kae_e3`) | **78/78**, scenario rc 0. ⚠️ **과거 실행**: 10-28 07:50–07:53(KST), 10-28 입력·전원 안전 규칙(d9a3a91, 09:08) 이전이다. 이 하네스(0fb4676 의 `host_standby_admission_e2e_test`)는 Host 에 `--input-injection-mode none` 을 주지 않았다. 그래서 Host 는 **제품 기본(입력 주입 켜짐)** 으로 떴고, 전원 정책 옵션도 없었다. 그 실행에서 OS 입력이 실제로 일어났는지는 **미확인**이다. 현재 정책상 `--old-host` 경우는 **NOT RUN** 이므로 이 수치는 그 정책 이전의 기록일 뿐이다. |
| Kotlin 단위 (1aecc48) | **313/0**, rc 0. 새 3개: punches 읽기, owner prefix, 복귀 fallback 의 baseline. 변이(fallback 의 preparedReturn 무시) → 그 시험 FAIL |
| AVD ke_r20 (6451ec1 APK, 10-28 07:55–08:13) | **40/40 PASS**(E1–E9 와 E7h a 의 준비 단계). 이어서 E7h(a) 동작 직전에 하네스 오류(`StopIteration`: 끊기 위치를 잴 카드 없음)로 중단했다. E7h 결과는 없다. |
| AVD ke_r21 (같은 APK, 10-28 08:17–08:33) | **29/29 PASS** 뒤 E7e 에서 중단했다(B 세션 `peer_lost` → viewer 메뉴를 찾지 못함). E7h 에는 도달하지 못했다. |

**AVD 실 UI — R1** (E7h) — **미실행**
- 방법: C 를 일반 연결 → targets 화면 → Host fault `pong_delay_ms=2000` 으로 PC 목록의 fence 를 약 2 s 붙잡는다 → fence 도중(목록 탭 후 약 0.4 s) 아래 동작 → 8 s 뒤 확인. 확인 항목: `list_return_dropped` 있음, `list_return_end`·`standby_first_transition`·first_transition `standby_start` 없음.

| 경우 | 동작 | 결과 |
|---|---|---|
| a | C 의 카드 끊기 | 미실행 |
| b | 모두 끊기(B 를 먼저 대기시켜 배너를 띄움) | 미실행 |
| c | 앱 이탈(HOME) → 복귀 | 미실행 |
| d | 로그아웃 → 다시 로그인 | 미실행 |

- 미실행 이유:
  - ke_r20 은 a 의 준비 단계 뒤 하네스 오류로 멈췄다.
  - ke_r21 은 E7e 에서 멈춰 E7h 에 닿지 못했다.
  - 10-28 입력·전원 안전 규칙 이후에는 AVD 를 다시 실행하지 않았다.
  - 그 뒤 하네스의 위치 측정을 고쳤지만(아래 하네스 해시), 고친 판으로는 실행한 적이 없다.
- R1 의 근거는 지금은 소스와 단위 시험뿐이다.

**AVD 실행 경과** (둘 다 10-28 입력·전원 안전 규칙 **이전**의 과거 실행)
- 두 실행에서 Host 4개(a·b·c·old)의 입력·전원 상태. 각 Host 로그(`directoryhost_host.log`)를 직접 확인했다.
  - 입력: **`input injection disabled (unsupported mode) mode=none` 한 줄이 모두 있다**. 하네스(feat 의 `mobile_abr_session_e2e_test --scenario directoryhost`)가 `--input-injection-mode none` 을 주었다. 수신 입력 로그는 `injectionEnabled=0` 이다(a: 2줄).
  - 전원: `host power management=` 줄이 **0** 이다. 두 시험 Host 빌드(test 44a43610…, old b6ed8984…)는 전원 정책 옵션 이전 빌드이므로 제품 기본 전원 동작(실행 상태 유지, 스트림 시작 시 display wake)이었을 것이다. 그 실제 효과는 따로 측정하지 않았다.
  - gate: 그때 하네스는 명령줄에 인자가 있는지만 검사했다(`e2e_command_is_isolated`). **출력 두 줄 gate(공용 guard)는 적용되지 않았다.**
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
- 하네스 sha256 (feat-apk-0233 `.claude/e2e/`, 저장소 미추적):
  - `ke_avd.py` 현재 `8c204d970614e2b9…`. mtime 08:14 로, ke_r21(08:17)이 쓴 판이다. ke_r20(07:55)이 쓴 그 이전 판의 해시는 **미확인**이다.
  - `ka_avd.py` `a6f787fc2e540f41…`, `ke_fixture.ps1` `1cd59dcb413b9323…`, `ke_slot.ps1` `0f908fd3728a6545…`, `ka_delay_proxy.js` `6303f50e995a20bc…`. 모두 두 실행 이전 mtime 이다.
  - fixture 기록(`ke_r20/fixture.txt`): host 하네스 exe `38ddf6cdf51ffb33…`, test host `44a43610c61590c2…`, old host `b6ed8984e202255c…`, server.js `b329e22249e87aec`.


**AVD 하네스가 띄우는 Host 와 공용 guard** (확인만 했고 실행하지 않았다)

| 구성 | 입력 OFF 인자 | 전원 OFF 인자 | 출력 두 줄 gate (`e2e_injection_guard.hpp`) | 비고 |
|---|---|---|---|---|
| `ke_fixture.ps1` → feat `mobile_abr_session_e2e_test --scenario directoryhost` (a·b·c) | `--input-injection-mode none` 있음 | **없음** | **없음**. 명령줄 검사만 한다. feat-apk-0233 에는 guard 헤더가 없다(ka-r3 쪽에만 있음). | 다음 AVD 실행 전에 ka-r3 guard(입력 none + 전원 off + 두 줄 gate + 실행 중 감시)를 이 하네스에 통합해야 한다. |
| 같은 하네스, old host (`GNLinkStreamPinnedOld.exe`) | 있음 | 없음, **줄 수도 없음** | 없음 | 전원 옵션 이전 빌드라 `--host-power-management off` 를 거부한다. guard 를 적용하면 gate 가 거부하므로, 지금 정책(old-host NOT RUN)과 같다. |
| pinned test host (`GNLinkStreamPinnedSelectTest.exe` 44a43610…) | 받음 | 이 빌드는 전원 옵션 이전 | — | guard 를 적용하려면 전원 정책이 들어간 빌드로 pinned host 를 다시 만들어야 한다. |

→ **지금 상태로는 AVD 하네스를 다시 실행하지 않는다.** 위 통합이 먼저다(이번 패킷 범위 밖, 지시를 기다린다).

**결정 요청**
1. **R3 실 clipboard**: AVD clipboard 공유가 꺼져 있는지 확인할 수 없어 차단 중이다. 공용 emulator-5554 는 `-no-window` 이고 clipboard 옵션이 없다. 선택지는 셋이다.
   - (a) clipboard 공유가 꺼진 것을 확인한 **별도 에뮬레이터**
   - (b) **실기기**(사용자 1회 협조)
   - (c) **미검증으로 둠**(지금 근거: 소스 + 순수 판단 단위 시험 + 변이)
2. **fallback 경로용 pinned 시험 Host fault 추가 여부**: activation 실패를 결정적으로 만드는 fault 가 없다. 공용 Host 코드에 seam 빌드 한정 fault 를 추가할지(+ pinned host 재빌드, 위 guard 통합과 함께), 아니면 fallback 경로를 단위 시험 근거로만 둘지 정해야 한다.

**고정 commit 정리**
- **e r3 의 SOURCE 대상은 feat `1aecc48`** (+ ka-r3 `0fb4676`) 이다. 이 보고의 R1–R3·D1 은 모두 1aecc48 까지의 내용이다.
- feat HEAD `7f07f1a` 는 1aecc48 위에 APK X 경계(3ef6ee6·50340ce·7c5d027·7f07f1a)만 더한 것이다. X 경계는 따로 SOURCE OK 를 받았다. `git diff 1aecc48..7f07f1a` 는 X 경계 파일(`client_mouse_xbutton_fence.hpp`, session cpp/hpp, X 시험, CMake)뿐이다.
- 통합 후보로 한 번에 보려면 7f07f1a 를 쓰면 된다. e r3 범위 자체는 1aecc48 로 고정한다.
- ke_r21 E7e `peer_lost`: **원인 미확정, 간헐 가능**. 위 기록 그대로이며 1회 통과로 닫지 않는다.
