


### ② r3 — Codex `codex_keepalive_r2_scope2.md` N1–N3 (2026-10-28)
**커밋** (ka-r3 `spike/host-admission-r3`):
- **03716ec** — N3 + N1 공통 판정.
- **68c8272** — N2 + N1 lease.
- feat/apk-0.2.33 에 **a629942** 로 merge(git CLI, 두 parent).
- 두 커밋 모두 제품 헤더·시험·CMake 만 바꿨다. NP 소유 파일은 건드리지 않았다.

**N1 — 활성화 HelloAck**
- `udp_hello_ack_acceptable()` (`native_video_client_tcp_control.hpp`): 일반 Hello 의 ack 판정을 **그대로 뽑아낸 것**이다. 판정 대상: 크기·magic·kind·version·VideoFec, 디렉터리 인증이면 DirectoryAuth.
  - 일반 핸드셰이크는 이 함수를 호출하도록 바뀌었을 뿐 식은 같다.
- lease 의 Receive 는 이 함수(DirectoryAuth 필수)에 **선언 size == sizeof** 를 더해 검사한다. 통과하지 못한 ack 는 통째로 버린다(state 도 features 도 바뀌지 않음).
- 무태그 Ack 계약(lease 헤더 주석에 명시):
  - HelloAck 에는 generation 이 없다. 어느 grant 인지는 Pong(entry+gen)이 정한다.
  - Ack 는 진행 중인 활성화를 **그 활성화 자신의 Pong 과 함께일 때만** 완성한다.
  - 취소된 활성화의 늦은 Ack 는 Host 의 같은 세션 admission 응답이고, 혼자서는 grant 를 만들지 못한다.
- 시험 (`standby_lease_test`, loopback 가짜 Host):
  - 잘못된 Ack 5종(버전, DirectoryAuth 없음, VideoFec 없음, 선언 size 불일치, 짧은 datagram)과 granted Pong 이 와도 400 ms 동안 Activating 에 머문다.
  - 이어서 유효한 Ack 가 오면 Active 가 되고, 그 Ack 의 features 를 그대로 받는다.
  - A 취소 → B 시작 → A 의 늦은 Pong + 늦은 Ack → 아직 Activating. B 자신의 Pong 이 와서야 Active 가 되고, features 는 검증된 Ack 의 값이다.
  - entry client 단위 시험에도 같은 순서를 추가했다.

**N2 — 수명·직렬화**
- `ActivationArgs(args, keepAlive, &op)`. runner 의 `TakeActivation(key, args, &op)` 는 keepAlive 로 **slot(shared_ptr)** 을 넘긴다. handshake 클로저가 slot 을 쥐므로 Release/ReleaseAll 뒤에도 lease 가 살아 있다(raw this 는 이 소유 아래에서만 쓴다).
- op 번호는 프로세스 전역이다. 그래서 같은 key 의 새 lease 와 번호가 겹치지 않는다.
- 다른 op 의 `SessionOver(key, op)` / `Abandon(key, op, args)` 는 아무것도 하지 않는다. handshake 가 도는 동안에는 둘 다 거부된다.
- stale handshake 는 시작부터 거부된다. abandon 됐거나 다른 op 인 경우, lock 아래에서 op_·handedOff_·cancel_ 을 확인하고 handshaking_ 을 세운다.
- 핸드오프 중의 Release 는 진행 중인 handshake 를 취소한다(false 반환, 아무것도 채택하지 않음). 이때는 아무것도 보내지 않는다. grant 가 있었다면 그 세션과 함께 끝난다.
- **읽는 쪽은 한 번에 하나**: pump 는 `reading_` 을 세운 뒤 **lock 없이** 20 ms 기다린다. ActivationArgs·ObserveFrom 은 그 대기가 끝난 다음에 소켓을 넘긴다.
  - ⚠️ 첫 구현에서는 lock 을 쥔 채 기다렸다(lease mu_ 를 select 동안, slot mu 를 Pump 동안). 변이 실행 3회가 UI 호출(TakeActivation·SessionOver·Abandon)에서 굶어 멈췄다.
  - 원인은 Windows mutex 가 공정하지 않다는 것이다. 풀어 주는 쪽이 바로 다시 잡는다.
  - 지금은 어떤 lock 도 대기를 가로질러 쥐지 않는다. runner 의 pump 도 slot lock 없이 돈다.
- StatusOf 는 lease snapshot 을 한 lock 아래에서 읽는다. Publish 는 statusMu → lease 순서다.
- 이벤트 기록은 최근 32개만 남는다.
- 시험(같은 파일):
  - args 직후 ReleaseAll: slot 은 args 안에서만 살아 있고(liveSlots), 그 handshake 는 false 를 돌려주며 아무것도 보내지 않는다. Release datagram 0, args 를 놓으면 slot 도 사라진다.
  - handshake 대기 중: 조기 SessionOver·Abandon 이 거부되고 handedOff 가 유지된다. Release 하면 handshake 가 약 100 ms 안에 false 를 돌려준다.
  - 화면 이탈(활성화 중 Release) 뒤 같은 key 의 새 lease:
    - 옛 op 의 SessionOver 는 무시되고 Parked 가 유지된다.
    - 새 활성화 중 옛 op 의 SessionOver·Abandon 은 거부되고, 세션 handle 도 열린 채로 남는다.
  - 핸드오프 6 s: pump 의 Ping 0회.
  - Abandon: handle 이 닫히고 lease 가 소켓을 되찾은 뒤 Ping 이 재개된다.
  - abandon 된 handshake 사본(실소켓 사용): 즉시 false, Activate 0회, Parked 유지.
  - 핸드오프 중 두 번째 TakeActivation 은 거부된다.

**N3 — 영속 저장**
- `FileGenerationStore` 의 저장 순서: write → FlushFileBuffers/fsync → close(결과 확인) → MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)/rename → POSIX 는 디렉터리 fsync.
  - 모든 단계가 성공해야 true 다. 하나라도 실패하면 그 generation 으로는 보내지 않는다.
- 실패 모델(헤더 주석): 앱 kill, Save 가 true 를 반환한 **뒤**의 OS 크래시·전원 손실. 단계 사이에서 죽으면 이전 파일 아니면 새 파일이 남는다. 이미 보낸 것보다 작은 generation 은 생기지 않는다.
- writer 는 하나다. `<path>.lock` 을 Windows 는 share 0, POSIX 는 flock 으로 배타 점유한다. 두 번째 store 는 모든 Load/Save 가 실패한다(공유하지 않고 닫는다).
- 경로는 UTF-8 이고 Windows 에서는 UTF-16 으로 연다.
- 손상된 파일이면 새 CSPRNG incarnation 을 쓴다.
- APK bridge 는 `nativeStandbyInit` 에서 `g_standby` guard 아래로 **한 번만** store 를 만든다(feat `native_bridge.cpp:1371-1374`). 따라서 lock 이 두 번째 생성과 충돌하는 경로는 없다.
- 시험(`standby_entry_client_test`, REMOTE60_TEST_TMP 아래 `세대` 디렉터리만 쓰고, 자기 파일과 디렉터리는 스스로 지운다):
  - 두 번째 store 거부.
  - 재시작 후 같은 id 이고 카운터가 이어진다.
  - Write/Flush/Close/Replace/DirSync 를 각각 실패시키면(주입) 아무것도 보내지 않고 Waiting 이 된다. reopen 하면 같은 id 이고, next 는 보낸 최댓값보다 크다.
  - 손상된 파일이면 새 id 를 쓰고 1부터 시작한다.
  - 이 PC 에서는 크래시를 일으키지 않았다.

**결과** — 종료 코드와 RESULT 줄을 함께 확인했다:

| 시험 | 결과 |
|---|---|
| `remote60_standby_lease_test` 43/43 (exe 446d7027…) | exit 0, **15회 반복 15/15** |
| `remote60_standby_entry_client_test` 72/72 (exe d71cdc64…) | exit 0 |
| e2e `--product-client --real-directory --old-host` (sh_run -NativeExe, 슬롯 처리, exe 4282a5cf…) | **66/66**, rc 0. 63 → 66 은 op 를 지정한 SessionOver 확인 3개가 늘어난 것 |
| feat a629942 `assembleDebug` | BUILD SUCCESSFUL. buildCMakeDebug 4 ABI 재실행, apk 2a9d5faf… |

변이 10개 — 모두 FAIL 줄로 잡혔다. 시계로 kill 된 것은 0개다:

| 변이 | 잡은 확인 |
|---|---|
| ackUnchecked | 잘못된 Ack 로 활성화됨, FAIL 5 |
| sizeUnchecked | 선언 size 를 보지 않음, FAIL 5 |
| noKeep | slot 수명 FAIL. args 를 놓을 때 UAF 로 0xC0000409 종료 |
| opIgnored | FAIL 6 |
| handshakeIgnored | 조기 SessionOver FAIL 등 4, 0xC0000409 |
| releaseNoCancel | FAIL 2 |
| staleHandshake | 4 s 동안 Activate 재전송 FAIL. 처음에는 무효 소켓으로 시험해 변이가 살아남았고, 실소켓으로 고쳤다 |
| eventsUnbounded | 200 개 남음 |
| perLeaseOp | 늦은 op 가 일치 FAIL 5 |
| pumpWhileHandedOff | 핸드오프 중 Ping 2회 |

**계약으로 남는 것(코드가 강제하지 않음)**
- handshake 가 **끝난 뒤** 세션이 Connected 인 동안의 조기 SessionOver: lease 는 받아들이고 pump 가 다시 읽기 시작한다.
  - lease 는 세션의 reader 가 멈췄는지 알 수 없다. 그래서 "Disconnect 가 **반환된 뒤** SessionOver" 는 호출자 계약이다.
  - 강제되는 것은 handshake 창(handshaking_)뿐이다. KA e 배선에서는 bridge 가 Disconnect 반환 뒤에만 부르도록 하고, 그 순서를 시험한다.
- StatusOf 의 원자성은 구조상의 성질(한 lock)이고, 경쟁을 따로 시험하지는 않았다.

**미검증**: 실 NAT. APK 에서의 TakeActivation 호출(배선 전, `SEND_TO_REAL_HOSTS=false`). Android flock 실기.
**다음**: KA e 배선. kept 항목 모델 A/B 결정(seq 14381)을 기다리는 중이다.
