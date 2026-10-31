# KA PC r4 — 제품 SOURCE 재검토

**NEEDS_CHANGES — 이전 R2·R4와 R1의 원 반례는 닫는다. 새 replaced 장부와 release drain이 남기는 경계를 같은 PC task에서 보수한다.** 독립 EVIDENCE·실제 UI·출하 승인은 아니다.

## 고정 범위

- 제품 후보 **d81148e5e1dfe24fae5038246f64d5a083e60426**, 비교 **5a1fb45..d81148e**. 이 commit의 전체 11파일에는 시험 2파일이 포함돼 있다. 이번 제품 검토는 **나머지 9파일 +204/-52**이며 ka_pc_managed/shell_e2e 변경은 제외했다. diff --check 정상.
- 고정 원문을 git show로 읽고 `.claude/codex_ka_pc_review/r4_src/`에 보존했다. 읽는 시점 보고는 이미 후속 **42782c2/6b9cf79 merge** 설명을 포함했고, SHA-256 **9f4981bc4f0263b7266841ac755958727e82a10ddecfc84268042f935dfab758**로 요청한 ff27f9aa…와 달랐다. 후속 코드를 d811의 성과로 섞지 않았다. 이전 report 재송신은 필요 없고 다음 패킷을 최종 commit과 고정하면 된다.
- units/IPC PASS 및 과거 r4-1 수치는 **작업용 보고**다. 입력/전원 공용 guard가 검수되지 않은 실제 Host 시험의 완료 근거는 계속 보류한다. 이번 Codex는 Host/Viewer/셸/UI·실 입력/전원/clipboard API를 실행하지 않았다.

## 닫는 지적

- **이전 R1의 D 예약→A 해제→E 허용 반례:** occupied에 진행 중 예약 하나를 더해 해당 빈자리를 다시 주지 않는다. 실제 고정 helper의 메모리 fixture에서 **reserve_D=1, admit_E=0, logical_PCs=3**을 확인했다. 성공 시 다시 세고 초과 후보를 owner-end로 종료하는 방어도 추가됐다. 아래 새 장부 누락은 별개다.
- **R2 pump/exit:** note_pump_end와 note_exit_decided가 같은 gConnectedMu 아래서 서로의 완료 표식을 소비한다. 기존 timeout 뒤 pump 종료→표식 재삽입 문제를 닫는다. 고정 두 함수의 양순서 1000개 op 제한 실행에서 **잔류 표식 0**이었다. 실제 셸 강제 경합/owner 전환 EVIDENCE까지 대체하지 않는다.
- **R4 ordinary exit 상태:** handle_viewer_exit의 idle/error/reconnecting과 ordinary connecting/connected가 hostId를 갖는다. 이전 ordinary A 종료가 managed B의 connecting 카드를 전역으로 푸는 caller 결함은 SOURCE에서 닫는다. 실제 혼합 UI 검증은 안전 하네스 뒤에 확인한다.
- **결정①:** connect cancel과 owner-end를 서로 다른 상속 handle로 구분하고, replaced Viewer에도 owner 종료를 전달/회수하는 방향을 수용한다. 새 fence watcher는 established 뒤 cancel을 한 번 보고 무시하고 owner-end를 계속 감시한다. 아래 R1-a의 장부 보수는 별도로 필요하다.

## R1-a (P1) — 살아 있는 replaced Viewer를 PC 수에서 누락

client_shell_main.cpp의 **gReplacedViewers(:136)**는 owner 종료·개별 exit 회수에는 쓰지만, **logical_pcs(:1979)·pc_visible(:1990)**에는 포함되지 않는다. 슬롯에는 hostId도 없다. 기존 세션이 실제로 성립했지만 parent connected 통지가 늦으면 새 connect cancel은 해당 세션을 보존하고, 이전 슬롯은 vector로 옮긴다(:1621–1622). 그 뒤 새 실행이 실패하면 그 PC가 살아 있는데도 map/managed 장부에서 사라진다.

허용 순서:

1. ordinary **A/B/C**가 열려 있다. A의 연결 통지만 parent에서 늦는다.
2. A 재탭 → 이전 A는 established라 cancel에 종료되지 않는다. shell은 A 슬롯을 **gReplacedViewers**로 이동한다.
3. 교체 A의 **CreateProcess 실패**, 또는 연결 전 실패 후 exit → 현재 A 슬롯은 없다. 기존 A는 계속 살아 있다.
4. D 시작 → logical_pcs는 **B/C 두 PC**만 세어 허용한다. B/C/D 세 PC라고 확정하지만 실제로는 **A/B/C/D 네 PC**가 열려 있다.

실제 고정 용량 helper와 위 registry 이동을 메모리 fixture로 실행한 결과:

```
replaced_live_A=1 admit_D=1 logical_PCs=3 actual_distinct_PCs=4
```

이는 실제 프로세스 launch 실패를 주입한 전체 셸 재현은 아니다. 고정 logical_pcs가 보존된 슬롯을 제외한다는 반례다. **replaced에도 hostId/owner/op를 유지하고 살아 있는 모든 owned Viewer를 PC별 union으로 계산**하거나, 새 실행 실패 시 원 슬롯을 안전하게 복구하는 등 장부를 일관되게 한다. standby eviction도 이 PC를 보이지 않는 것으로 취급하지 않는다. 같은 PC의 복수 process를 서로 다른 PC로 세라는 요구는 아니다. 교체 launch 실패/조기 exit·완료 역순·owner 변경을 기존 예약 시험에 추가한다.

## R3-a (P1) — 비어 있는 큐를 application ACK로 오판

viewer_control_client.cpp **:1062–1066**의 inputInFlight는 RAII로 모든 분기에서 false가 된다. **execute_control_action 실패(:1147–1176)**도 마찬가지이며, 그 요청은 재송신하지 않는다. 반면 viewer_window_proc.cpp **:1803–1804**는 **queue Empty && !inputInFlight && !udpControl.TxPending**만으로 “acknowledged by the host”라 판정한다.

**마지막 release를 dequeue한 뒤 TCP write/read 실패**라면 application ACK는 없는데 큐는 비고 inFlight는 false다. TCP에서 udpControl은 비어 있다. UDP도 wire 전송 ACK와 application InputAck는 별개이며 ResumeWith는 txQueue를 비우므로 같은 세 조건만으로 해당 release ACK를 증명하지 못한다.

고정 inFlight scope/종료 조건을 사용하고 마지막 교환을 실패시키는 기록 fixture:

```
failed_last_release app_acks=0 delivered_claim=1
```

**해당 fence가 내보낼 release들의 seq/세대와 실제 InputAck 완료를 추적**한다. 교환 실패·폐기·resume/rekey를 성공으로 바꾸지 않는다. 확인되지 않은 release는 기존 정책에 맞게 처리하고 유한 budget 종료를 “NOT confirmed”로 구분한다. 정상 링크는 session 종료 전에 해당 release ACK가 선행해야 한다. 큐/송신 채널 empty는 보조 조건일 뿐 성공 증거가 아니다. 새 wire를 필수로 요구하지 않는다.

## R3-b (P1) — fence 대기 동안 일반 clipboard/비release 작업이 계속됨

fence는 window의 새 입력/clipboard listener를 막지만 **pending 송신까지 닫지 않는다.** WndProc :1257의 cancel_pastes와 :1274의 RemoveClipboardFormatListener는 일반 text **clip.hasPending/pendingText**를 제거하거나 clip.enabled를 끄지 않는다. ControlClient loop는 2초 drain 중에도 통상 scheduler·credential/IME 처리·**pump_clipboard_sync(:1265)**를 그대로 실행한다.

특히 pump_clipboard_sync(:258–313)는 owner fence를 검사하지 않고 enabled/hostSupports만 확인한 뒤 pending text를 꺼내 **send_clipboard_update**한다. 고정 함수 원문을 사용하고 실제 clipboard/network 대신 기록 backend를 연결한 결과:

```
fenced=1 input_enabled=0 pending_clipboard_writes=1 result=1
```

**새 요청 gate와 release 전용 drain 상태를 control sender까지 연결**한다. fence 뒤에는 이미 held였던 해제/그 확인에 필요한 작업만 허용하고, 미송신 down/text/move/pick·clipboard/file offer/credential 등 비release 요청은 해당 소유권/보안 규칙으로 취소·폐기·필요 시 zero한다. UI bool인 ownerFenced를 control thread에서 무동기 읽는 식으로 연결하지 않는다. 이미 실행 중인 교환의 완료와 fence 뒤 새 송신 시작도 구별해 정의한다.

기존 안전한 기록 대역으로 **pending clipboard 준비→sender 정지→fence→sender 재개**, pending down/text/pick 및 credential의 해당 경계를 확인한다. release는 도달하되 새 비release 효과는 없어야 한다. 실제 사용자 clipboard/패스워드를 사용하지 않는다.

## 결정①의 인계 범위

native_video_client_main.cpp는 connect 시도 watcher를 :154에서 멈추고 :158에서 취소를 판정한 **뒤**, :169 이후 established watcher를 시작한다. **sessionConnected=true/connected 로그는 connect_media_socket 안이 아니라 attach_control_tunnel_and_log(viewer_startup.cpp :771–) 안의 :825–826**이며 main :199에서 호출된다. 따라서 연결 확정 통지가 이미 나간 뒤 옛 watcher가 취소를 처리한다고 판정하지 않는다. parent 통지가 늦은 established 세션을 새 cancel watcher가 owner 종료로 오인하던 원 경로는 닫는다.

연결 확정 전후 두 신호의 실제 인계, replacement 실패 후 기존 창 유지/카드 재탭/owner 종료는 안전한 최종 하네스에서 독립 확인한다. 이 SOURCE 수용이 실제 UI 동작 완료나 R1-a의 누락 장부를 면제하지 않는다.

## 직접 근거·다음 단계

`.claude/codex_ka_pc_review/r4_source_probe.cpp`는 **고정 용량 helper 7개·pump/exit 함수 2개·pump_clipboard_sync 원문**, inFlight scope와 timer 판정식을 사용했다. registry/recent/transport/clipboard는 메모리·기록 fixture이며 전체 UI/protocol/프로세스 시험이 아니다. 반환 0은 **원 반례 수정 확인 + 새 반례 재현**이며 제품 PASS가 아니다.

- exe SHA-256 **a63052b3b63a074f4341dcf287a709ae1b2f970db00a5cebfcfebf6ed0e6472d**.
- 출력의 실제 Host/Viewer/입력·전원·clipboard API 호출 **0**. repo 밖 fixture/삭제·운영/설치·전체 제품 빌드·commit/push 없음.
- EVIDENCE는 작업용의 units/IPC 보고와 구분한다. 실제 release/종료 순서·혼합 카드 UI·owner 전환·실 NAT/비용 등은 아직 독립 확인되지 않았다.

검증용이 **R1-a·R3-a/b**를 기존 작업용2의 PC task 정본에 한 묶음으로 연결한다. R2/R4·결정①의 닫은 원 지적을 반복시키거나 별도 중복 구현 task를 만들지 않는다. 시험 변경은 요청대로 이번 SOURCE 범위에서 제외했고 **검수된 공용 guard와 합친 최종 고정 후보에서** 함께 확인한다. 후속 42782c2/6b9cf79 및 제품 충돌 해결의 승인은 이 판정에 포함하지 않는다. 기존 실제 Host 시험 보류와 prelogin 금지를 유지한다. 사용자 현재 조치 없음.
