# KA r2 ② r3-b SOURCE 재확인

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 keepalive-r2 ② task.

**고정 공용 구현 SOURCE OK — N2-a/b 닫음.** 기존 N1 Ack 검사·N3 저장 보수 수용도 유지한다. 새 `standby_session_link`의 제한된 연결/회수 경계도 아래 호출자 계약 안에서 수용한다. **실제 APK e 배선·독립 EVIDENCE·실 NAT·자동 준비 출하/운영 배포 승인은 아니다.**

## 고정 대상

- ka-r3 **82679b23cd157e4001ef87ba1841c5448c1ab8a3**, diff **232431b..82679b2**,5파일. dcb6607의 session link와82679b2의 lease/시험 보수를 모두 읽었다. whitespace rc0, 검사 시 HEAD 일치·tracked clean.
- APK 병합 **5c871ed2d42c8bd8c41c20cdbde226beb2d45b51**, 고정 diff **dd2925d..5c871ed**. 해당 공용 lease/session-link blob은 ka-r3와 동일하다.
- **APK worktree의 현재 Kotlin/JNI/설정 등은 e 배선 편집 중**이다. 이번 검토는 위 commit까지만이며 미커밋 변경을 읽거나 승인에 포함하지 않았다.

## N2-a: 취소 뒤 완료 채택 — 닫음

Handshake의 packet/Tick 투입 전 `valid()`가 **같은 op·handedOff·cancel 아님·stop 아님**을 lease lock 아래 확인한다. Release가 그 lock에서 cancel을 세워 완료했다면 Receive에서 뒤늦게 돌아온 Pong/Ack가 state에 들어가지 않는다. 마지막 true/features 반환도 같은 lock 아래 다시 검증해 취소가 먼저 완료한 결과를 성공으로 넘기지 않는다.

packet 처리 뒤 취소가 들어와 최종 성공만 거부되는 경우도 별도 처리한다. Host grant의 끝은 실제 session end/peer-lost이며 강제 Release 성공으로 꾸미지 않는다. caller는 실패/Disconnect 종료 뒤 정확한 op의 SessionOver로 lease state를 회수해야 한다.

**Codex 직접 제한 재실행:** 이전에232431b에서 결함을 재현했던 `.claude/codex_ka_review_r3/cancel_completion_probe.cpp`를 이번 후보의 실제 헤더/기존 FakeHost로 다시 빌드했다. 원 파일/이전 exe는 보존하고 새 실행물 `cancel_completion_probe_r3b.exe`를 만들었다.

```
case=0..11: done_before_release=0 handshake_success_after_release=0 state=2
cancel_before_final_pong_success_count=0
```

Ack 선행→Release 완료→current Pong의 **12회 모두 성공0/Parked**였다. 이 프로브는 과거 결함이 나타날 때exit0인 역방향 검사여서 이번exit1은 **과거 결함 미재현**이다. 새 exe SHA-256 **e602c2a0095a85e3743e8348f7960fd49e795bed21a1ef420eafeb8911043cf0**. 제품 파일 수정/제품 빌드는 없으며 자기 loopback 프로브만 실행했다.

검증용이 전달한 작업용 저장 `caplife/ka2n12_mut/restored.txt`에서는 Ack→Release→Pong 및 Pong→Release→Ack 각6회 모두 adopted0/Active0, 이후 정상 activation 복귀를 확인했다. `n2aUnchecked.txt`의 채택 실패 검출, `n2aStepOnly.txt`의 adopted0이더라도 Active가 남는 변이 실패를 읽었다. final return만 고쳐 state credit을 남기는 보수와도 구분되는 근거다. **이 저장 실행들은 작업용 귀속**이다.

## N2-b: handoff/observation intent — 닫음

ActivationArgs/ObserveFrom은 lock 안에 intents를 먼저 등록하고 진행 중 read/observation이 끝날 때까지 기다린다. Pump는 intents가 있거나 observing일 때 새 Receive를 시작하지 않는다. waiter가 재획득하기 전 pump가 reading_=true를 다시 만드는 이전 스케줄을 막는다. 성공/거부에서 intent를 내리고 notify해 계속된 Ping을 막아 두지도 않는다.

ObserveFrom은 observing을 세운 뒤 lock 밖에서 자기 socket을 관찰하고, 끝난 뒤 다시 lock 안에서 해제한다. 이 구간은 pump와 handoff가 함께 읽지 않는다. CapabilityFn이 lock 안에서 호출될 수 있다는 별도 제약도 헤더/보고에 명시됐다. UI 전체 무대기·모든 네트워크 함수가 lock 밖이라는 주장은 유지하지 않는다.

새 시험 hook은 read-done 경계에서 waiter의 intent를 등록시켜 순서를 통제한다. 저장 restored 결과는 handoff/observation 대기 중 새 read0과 정상 Ping 복귀이며, `n2bNoIntent.txt`는 대기 중 read100으로 실패를 검출한다. Codex는 해당 시험과 소스/저장 로그를 읽었고 이 전용 hook 시험을 독립 재실행한 것은 아니다. hook define은 해당 lease 시험 target에만 있고 출하 member는 제외된다.

시험 운영의 작은 보완: hook 안의 `while(pendingIntentsForTest()==0) yield()`와 이후 thread join도 bounded precondition으로 관리한다. 기대 등록이 깨진 변이/준비 실패에서 무기한 기다리지 않게 timeout/failure 및 자기 thread/process 회수를 같은 시험 작업에서 정리한다. 외부 watchdog으로 종료한 경우 정상 exit로 쓰지 않는다. 이 시험 안전 보완 때문에 이미 확인한 제품 N2를 다시 설계하거나 전량 SOURCE 승인 관문을 새로 만들 필요는 없다.

## 새 session link — 범위 내 수용

`standby_adopt`는 ready lease에서 args/op를 얻고 entry의 ConnectOpGate로 채택한다. stale-before-connect와 Connect가 worker를 시작하기 전 거부하는 경로는 Abandon으로 duplicated handle을 회수한다. 성공 후 stale/entry Close는 **controller.Disconnect 반환 뒤 link.Over**로 정확한 activation op의 SessionOver를 한 번 전달한다. 기존 gate의 Adopt/Close serialization 및 controller의 false 반환 전 handle 소유를 대조했다.

`caplife/sh_r2/kae_n2ab.log`의 새8개 결과를 읽었다: stale→Abandon, Connect 거부→Abandon, 실제 제품 controller activation/영상, handedOff, Close→SessionOver, Over 중복 무효, Host 실제 session end, 같은 lease의 Ping 유지. 전체74/74는 **작업용 실행**이며 Codex가 실 Host e2e를 재실행하거나 bridge UI를 확인한 것은 아니다.

호출자 계약은 실제 e/PC 배선까지 유지해야 한다.

- activation은 **새 session entry의 controller/gate/link**에 결합한다. 서로 다른 lease의 채택이 같은 link를 임의로 덮어쓰거나 옛 cleanup이 새 link를 지우는 사용은 허용하지 않는다.
- link의 runner pointer는 비소유 참조다. **모든 link/지연 Close가 끝날 때까지 runner 객체가 생존**해야 한다. ReleaseAll이 map을 비우는 것과 runner 객체 파괴는 다르다. 계정 전환/앱 종료에서 이 수명을 확정한다.
- 실제 reader/worker가 끝나기 전 Over/SessionOver를 부르면 안 된다. lease가 연결된 session reader를 자체 판별해 강제하는 API는 아니다. e/PC의 모든 Disconnect/실패/미사용 args 경로를 이 순서로 검증한다.

이 조건 아래 공용 helper를 다음 배선에서 사용할 수 있다. WIP native bridge의 실제 Close/adoption·owner epoch/incarnation·onStop·late Provide를 이번 소스 검토만으로 완료 처리하지 않는다.

## 다음 담당/완료 경계

검증용은 **82679b2의 고정 산출물/소스**로 N1~N3 핵심과 N2-a/b 제한 반례·제품 session-link 회수를 독립 확인한다. 이미 닫힌 Host/NP 전체를 매 commit 반복할 필요는 없다. 실 server.js의 Host 이동/proof 손실 bounded 회수, APK 실제 activation/목록 복귀/onStop·계정 종료, M1의 옛 park_reuse 제거+새 session UA-2 반례는 통합 EVIDENCE에 연결한다.

e 배선은 확정된 **모델 A·스위치ⓐ**로 계속한다. 현재 작업용의 Kotlin/JNI 컴파일·304개 단위·AVD 준비는 그 단계의 진행이며 독립 제품 행동 검증을 대신하지 않는다. NP/PC/디렉터리 별도 미결도 각 고정 task에 분리한다. 자동 준비의 최종 출하는 승인된 독립 검증 뒤 정상 릴리스로 잇는다.

Codex는 판정서 외 제품 편집·제품 빌드·실 Host/AVD/운영 서버·사용자 설치본 조작·삭제·commit/push를 하지 않았다. **검토 전용 C++ 프로브의 새 빌드/loopback 실행**은 위에 명시했다. 미추적 판정서/프로브 외 history/계획·Git MCP commit은 같은 구현/통합 기록에 취합한다. 사용자 현재 조치 없음.
