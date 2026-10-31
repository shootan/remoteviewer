

## ② 진행 (2026-10-27) — shared native 클라이언트·activation 경로
**고정: `spike/host-admission-r3` 92a689a** (cf8fb70 위 3 commit, tracked clean).

| 커밋 | 범위 | 내용 |
|---|---|---|
| a5a70cd | 제품+시험 | a·b `StandbyEntryClient`, `GenerationAllocator` |
| 6816e4d | 제품 | c·d `StandbyLease`, `ClientSessionConnectArgs::udpHandshake` |
| 92a689a | 시험 | e2e `--product-client` |

- 앱에서는 아직 아무것도 이 코드를 부르지 않는다(자동 preconnect 출하 OFF). 기존 Hello 경로는 `udpHandshake` 가 비어 있으면 그대로다.

### a·b — 순수 상태기계, 단조 generation
- 상태 흐름: Idle → Admitting → Parked(Ping 5 s) → Activating → Active → (세션 끝) Parked.
- Waiting: Busy·Superseded·Lost 일 때 1→2→4→8→16→30 s 기다린 뒤 **새 capability** 를 요청한다.
- Unsupported: Hello 6회에 무응답이면 이 상태다. 자동 connect 와 legacy Hello 를 하지 않는다.
- activation 완료 조건: **이 activation 의 Pong(Granted) + HelloAck**, 순서 무관.
  - 답이 유실되면 **같은 gen** 으로 재전송한다.
  - bare·늦은·취소된·다른 entry/gen 의 답은 아무것도 채택하지 않는다.
  - 취소 시 Release 는 보내지 않는다(Live 면 거부된다). 다음 activation 은 더 큰 gen 이다.
- Ping 이 16 s(lease 20 s 미만) 동안 무응답이면 Lost 다. relay 가 이동 뒤 proof 를 잃은 경우도 여기에 해당하며, ready 로 남지 않는다. Unknown 도 Lost 다.
- `GenerationAllocator`: 입장·activation 이 공유하는 단조 카운터다.
  - 사용 **전에** 저장하고, 재시작 뒤에는 그 위에서 계속한다.
  - 저장 실패면 송신 0. 저장값이 없거나 손상이면 새 기기 id 를 만든다.
- `standby_entry_client_test` **46/46**. 변이 7종 모두 FAIL: bareAck, anyGen, noDead, legacy, newGenRetx, noSave, busyAtOnce.

### c·d — lease 소켓, activation connect 모드
- `StandbyLease`: lease 하나에 UDP 소켓 하나(두 host 를 한 소켓에 다중화하지 않음)와 `StandbyEntryClient` 를 묶는다.
  - 소유자가 Pump 하며, 자체 스레드는 없다.
  - `ActivationArgs()` 는 소켓을 `ClientSessionController` 에 넘기고, Hello 대신 Activate 핸드셰이크를 실행시킨다. 그 뒤는 세션의 기존 경로다.
- e2e `--product-client` (실 GNLinkStream + fake directory, `sh_run.ps1 -NativeExe`) **35/35**:
  - A·B 가 parked 이고 세션은 0 이다.
  - A 가 activation 으로 연결된다(first session by activation, 영상, window list).
  - B 가 12 s ping 하는 동안 새 세션 0, control 종료 0, A 는 연결 유지.
  - B 가 takeover 한다(새 epoch, 영상).
  - A 의 세션이 끝나고, A 는 새 소켓으로 parked 된 뒤 더 큰 gen 으로 되찾는다.
  - 호스트의 모든 세션이 activation 에서 나왔다(ordinary Hello 0).
  - 구 Host 는 Unsupported 이며 세션 0 이다.
- 기존 suite `--old-host --real-directory` **89/89**(회귀).
- 변이(세션이 handshake 를 무시하고 항상 Hello) → 4 FAIL.
- e2e exe 48c55003…, 구 Host `fix-capture-lifecycle-baseline\Release\GNLinkStream.exe`.

### 아직 아님
- **relay 경로**: `/api/hosts` 의 `standby.supported` 확인, lease 소켓의 `standby:true` connect.
- **세션 뒤 소켓 유지**: 지금은 새 소켓을 쓴다. relay 에서는 재입장이 필요하다.
- **e APK 배선**: 브랜치 결정(제안 ①) 대기.
- **§6 비용, §7 교차 기기, 실 NAT**: 미검증.
