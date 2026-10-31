# KA r2 ② r3 — N1~N3 SOURCE 재검토

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 keepalive-r2 ② task.

**NEEDS_CHANGES — N1 Ack 검사와 N3 저장 보수는 수용. N2-a 취소 뒤 완료 채택을 실제 재현했고, N2-b 대기 중 handoff 우선권도 보완한다.** 기존 Host/서버 SOURCE 상태·모델 A 결정은 유지한다. APK e 배선 및 PC 구현은 닫힌 부분을 활용할 수 있으나, 현재 보수 전 lease를 SOURCE OK/출하 후보로 채택하지 않는다.

## 고정 대상/검토

- ka-r3 **232431bcba7dab5c5fc5355e78caa7cf9cf5d84e**: 03716ec+68c8272 제품/시험, 232431b는 Ping 시험의 in-flight 기준창만 변경. diff **0a5e71d..232431b**,9파일, whitespace rc0, 검사 시 tracked clean.
- APK 통합 **dd2925dac7ee91b62dc96d4d0919b18e77a36f23**, 비교1c2116d..dd2925d. r3의5개 핵심 공용 파일(lease/runner/file_store/ack helper.hpp/.cpp)은 ka-r3와 blob 차이0. 포함된53325fc의 스크롤 하네스는 별도 기존 변경이다.
- 실제 lease/runner·file store·공통 ack의 전체 변경, 새 lease 시험과 entry/e2e 변경·CMake, 보고 '② r3', 저장 baseline/mutant 로그를 읽었다.43/43 반복·72/72·66/66·변이10개 및 Android4ABI 컴파일은 **작업용 귀속**이다. 이번 APK 검토는 AVD 실행 성공을 뜻하지 않는다.

## N1: 보수 수용

일반 `udp_hello_handshake`의 기존 magic/kind/version/최소길이/VideoFec/필요 DirectoryAuth 판정을 `udp_hello_ack_acceptable`로 그대로 공유한다. lease는 declared size도 확인하고, 실패 Ack는 state/features에 전달하지 않는다. 현재 gen Pong과 검증된 무태그 Ack의 양쪽 조건은 유지한다.

Ack에 gen이 없으므로 A의 늦은 Ack가 B의 신원/현재 grant를 증명한다고 하지 않는다. 현재 entry/gen Pong이 grant 증명이고 Ack는 검증된 feature 응답이라는 계약과 A 취소→B→옛 Pong/Ack→B Pong 반례를 수용한다. 이 Ack 검사 자체는 다시 고칠 필요 없다. 단 **취소가 완료한 뒤 current Pong/Ack가 도착하는 채택 경계**는 아래 N2-a다.

## N2-a (P2): Release가 먼저 끝나도 Receive가 완료 packet을 채택함 — 직접 재현

`standby_lease.hpp:431~449`는 while 진입 때만 stop/cancel을 검사한다. `Receive` 중 Release가 cancel을 세우고 반환해도, 돌아온 packet은 `OnStandbyPacket/OnHelloAck`로 전달돼 done=true가 될 수 있다. 마지막 `if(done)`는 **cancel/stop/현재 op/handedOff를 재검사하지 않고 true**를 반환한다.

Codex가 제품 소스를 바꾸지 않고 다음 검토 전용 프로브를 빌드·실행했다:

- `.claude/codex_ka_review_r3/cancel_completion_probe.cpp`는 **고정 후보의 실제 `standby_lease_test.cpp`를 include**(원 main만 rename)하고 기존 FakeHost·실 StandbyLease를 사용한다. 판정 로직 복제/stub 성공이 아니다.
- loopback에서 admission→Activate→**유효 HelloAck만 먼저** 전달→handshake가 grant를 기다리는 동안 **lease.Release() 완료**→그 뒤 current gen의 granted Pong 전달.
- 실제 출력:

```
case=0 done_before_release=0 handshake_success_after_release=1 state=4
cancel_before_final_pong_success_count=1
```

첫 시도에서 Release 전 미완료였던 handshake가 Release 후 보낸 Pong으로 **true/Active**가 됐다. 이는 성공 callback이 먼저 끝난 뒤 뒤늦게 Release한 순서가 아니다. 프로브 exe SHA-256 **d673b9ad0e2fadd500bdb99a97f26f3bd98dc8214d3124c9a2f3ab04329c2487**. 이 프로브의 exit0은 **결함 재현 성공**이며 제품 PASS가 아니다. 파일/바이너리는 보존했다. 시험 Host는 프로브 안의127.0.0.1 UDP 대역이며 사용자 제품/창/서비스는 실행하지 않았다.

필요: packet 처리/Activated credit·최종 성공을 해당 operation의 아직 유효한 권한과 같은 잠금/선형화 경계에서 결합한다. **Release가 완료한 뒤의 결과는 완료/features/정상 session으로 채택하지 않는다.** stop/cancel·op/handedOff를 최종 확인하고 취소된 state/후속 SessionOver를 안전하게 정리한다. 단순 UI가 나중에 무시할 것이라는 가정으로 library의 true를 정당화하지 않는다. Host grant가 이미 생겼을 수 있어 그쪽은 기존 세션 끝/peer-lost로 정리하며 강제 Release 성공을 꾸미지 않는다.

제한 반례: Ack 선행→Receive 대기→Release 완료→current Pong, 그리고 Pong 선행→Release 완료→유효 Ack의 양쪽 순서. late packet이 취소를 되돌리지 않으며 negotiated features/성공1회도0이어야 한다. 현재 runner의 Release 시험은 Host가 **끝까지 응답하지 않는 경우**라 이 완료 경합을 검사하지 않는다. 정상 역순/재전송은 유지한다.

## N2-b (P2): reading_가 끝나길 기다리지만 새 pump read를 막지 않음

`ActivationArgs:223`와 `ObserveFrom:176`은 `idle_.wait(... !reading_)`만 기다린다. Pump는 receive 뒤 reading_=false/notify를 한 다음 다음 iteration에서 곧바로 true로 재설정할 수 있다. **대기 중인 handoff/observe 요청이 있다는 상태를 pump에 예약하지 않는다.**

허용되는 순서: UI waiter가 reading_=true를 보고 sleep→pump false/notify→깨운 waiter가 lock을 되찾기 전에 pump가 다음 Receive를 시작해 reading_=true→waiter가 다시 sleep. 반복하면 “최대 한 번20ms만 기다린다”는 상한은 성립하지 않는다. Windows 조건변수도 stolen wakeup을 허용한다. [Microsoft 조건변수 문서](https://learn.microsoft.com/en-us/windows/win32/sync/condition-variables).

이는 **소스가 허용하는 interleave 지적**이며 Codex가 이 후보에서 실제 무한 정지를 재현했다는 뜻은 아니다.43건×반복 정상 통과는 이 스케줄의 부재 증명이 아니다. lock 밖 Receive로 옮긴 개선은 유지하고, handoff/observe intent를 lock 안에 세워 현 read가 끝나면 다음 pump가 읽기를 재개하지 못하게 하는 등 우선권을 명시한다. 실패/취소 시 예약을 풀어 정상 Ping이 재개돼야 한다.

동기화 반례로 현 read 완료→waiter가 재획득하기 전 pump의 다음 read 시도→그 새 read가 시작되지 않음→handedOff/observe 소유1개를 확인한다. scheduler 운에 맡긴 반복 수만 늘리거나 임의 Sleep을 추가해 상한을 주장하지 않는다.

보고의 “어떤 lock도 대기를 가로질러 쥐지 않는다”는 **Pump/Handshake Receive에 한정해** 적는다. ObserveFrom은 현재 lease lock 안에서 `standby_observe_on`을 호출하고, Start/NeedCapability도 capability callback을 lock 안에서 호출한다. 이 별도 네트워크 대기를 UI 무대기 보장으로 인용하지 않는다. 실제 e/PC 배선은 off-UI 실행·취소/종료 deadline을 갖추고 Release/상태 처리도 기다릴 수 있는 범위를 정확히 다룬다.

## N2의 이미 닫힌 부분

runner가 args closure에 slot shared_ptr을 보유해 ReleaseAll 뒤에도 객체가 생존하고, process-wide op로 새 동일key lease의 번호 중복을 막는다. SessionOver/Abandon의 op·handshaking 검사, stale args 시작 거부, handedOff 동안 pump 중지,32개 bounded events, 한 lease lock의 snapshot·statusMu→lease 순서는 방향에 맞다. 이 부분을 원복하거나 새 전체 구조로 교체할 필요 없다.

**handshake가 끝난 뒤 실제 session reader가 살아 있는지**는 lease가 알 수 없으므로 “Disconnect 반환/실패 worker 종료 뒤 정확한 op의 SessionOver”라는 caller 계약을 수용한다. 이는 미구현 e/PC 배선에서 실제 순서로 검사해야 하며, 현재 scope가 그 배선까지 검증한 것은 아니다. N2-a의 library 내부 취소 경합과 이 호출자 의무를 혼동하지 않는다.

## N3: 보수 수용·증거 한계

별도 stable `.lock` 파일로 single writer를 잡아 data파일 replace가 잠금을 바꾸지 않는다. Windows UTF8→UTF16/CreateFileW·Read/Write·FlushFileBuffers·checked close·MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH), POSIX fsync/rename/dir-fsync 및 각 실패 false로 기존의 fflush만 쓰던 경계를 보수했다. API 반환 확인과 선저장 후 gen 송신 계약을 수용한다. [Microsoft MoveFileExW 문서](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).

비ASCII 경로/두 번째 store 거부·저장 단계별 fault→송신0·재열기/손상 새 incarnation 반례가 추가됐다. 저장 성공의 의미는 지원하는 로컬 filesystem/OS의 flush 보장을 따른다. **이 PC의 실제 전원 손실/OS crash 실증이나 모든 storage cache의 보장으로 확대하지 않는다.** Android flock/실기 미검증도 유지한다. 사용자 PC를 crash/power-cycle하는 추가 시험은 요구하지 않는다. 공용 store 보수는 중복 writer 없이 그대로 유지할 수 있다.

## 다음 담당/통합 경계

검증용은 **N2-a/b와 보고 정정 한 묶음**을 기존 작업용1의 같은② task/유효 지시서에 취합한다. 제한 반례→해당 diff SOURCE→고정 산출물의 독립 실행으로 연결하며, 닫힌 N1/N3·NP/Host 전체를 매 commit 전량 반복하지 않는다. 실제 재현한 a를 우선 보수하고 b는 같은 read/handoff 경계에서 처리한다.

kept 모델은 이미 **A로 확정**됐으므로 보고말미의 “A/B 결정 대기(seq14381)”는 최신 `codex_keepalive_r2_kept_model.md`로 바꾼다. 오래된 대기를 다시 만들지 않는다. APK 병합의 현재 shared blob 동일성/4ABI 컴파일은 소스 보수 포함 확인일 뿐 e의 TakeActivation/onStop·SessionOver UI 행동 증거는 아니다. 기존 UA-2 병합 M1도 실제 모델 A 경로/반례로 닫기 전까지 분리 유지한다.

자동 preconnect 출하 OFF·운영 NAS/공유기·사용자 설치본 무변경. 제품 코드/제품 빌드/실 Host 실행·삭제·commit/push는 Codex가 하지 않았다. **검토 전용 C++ 프로브 빌드·loopback 실행은 위에 명시**했다. 미추적 판정서/프로브 외 history/계획·Git MCP commit은 해당 구현 작업에 취합한다. 사용자 현재 조치 없음.
