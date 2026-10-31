# KA 공용 FINDING — Host punch 스레드 SOURCE 검토

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / keepalive-r2 FINDING.

**NEEDS_CHANGES — 5초 병목의 원인·스레드 분리 방향은 수용하며, 새 송신 큐의 수명 정리 N1을 보수한다.** PC의 원 FINDING을 relay/replay 30초 문제로 단정했던 추정은 사용하지 않는다. 이 판정은 해당 Host 변경 범위이며 APK e r2·독립 EVIDENCE·통합·출하 승인이 아니다.

## 고정 범위

- ka-r3 **a7773e6619971a160266bc083d1100dcd725f9a3**, 확인 당시 HEAD 일치·tracked clean·해당 diff --check 정상.
- 요청된 `3326c84..a7773e6`는 6파일이다. 중간 **da2280e759bcaaca4abcf1b6642e42a41e94924a**의 standby_lease/시험/runner 3파일은 **별도 보고 예정인 e r2**이며 여기서 묶어 승인하지 않는다.
- 이번 FINDING의 실제 변경은 **da2280e..a7773e6**, directory_client.hpp/cpp 및 host_standby_admission_e2e_test.cpp의 3파일(+160/-14)이다. product SendFn의 실제 호출자와 Host 종료 순서, directory-retry의 같은 파일 변경도 대조했다.
- APK merge **c3bf4728eece8cd3cb13845384299f3a76abd88a**의 두 번째 parent가 a7773e6인 것을 확인했다. merge 전체 APK SOURCE를 이 판정으로 대신하지 않는다.

## 원인과 보수 방향 — 수용

기존 HostAgent::Run은 Heartbeat로 capability를 수거한 뒤 **동기 Punch의 25×200 ms train**을 실행했다. 그동안 다음 wake 플래그를 처리할 수 없었다. capability는 같은 cycle의 heartbeat에 없었고 새 Hello가 먼저 오면, wake가 도착했더라도 Host가 capability를 아직 갖고 있지 않아 거절할 수 있다.

보존된 `.claude/caplife/sh_r2/kae_relapse5.log`에서 capability 수거 약 5.1초 지연, refreshWasPending=1, Host의 capability 거절과 구 실패를 확인했다. 이후 `kae_relapse6.log`의 X 최초 admission·crash 뒤 재입장 599 ms·Release 뒤 재입장 1596 ms 및 78/0 결과는 이 설명과 맞는다. **두 로그/78·55·HTTP contract 실행은 작업용 근거**이며 Codex의 전체 독립 실행은 아니다.

구 실행의 crash 하위 check는 PASS였지만 X 최초 admission 자체가 FAIL이었다. 따라서 구 실행의 모든 crash/Release 하위행이 각각 유효하게 실패했다고 세지 않는다. 새 실행은 X 최초 admission부터 성공한 연속 시나리오다.

새 Punch는 mu_ 아래 capability와 reply window를 먼저 갱신한 뒤 punchMu_에 작업을 넣고 즉시 반환한다. PunchLoop는 target snapshot을 만든 뒤 lock 밖에서 송신하고, 200 ms 간격을 유지한다. directory cycle이 다음 wake를 처리하는 일을 train 전체가 붙잡지 않는 구조를 수용한다. interval을 넘긴 cycle에서 pending refresh를 다음 cycle 원인으로 소비하는 변경도 일관된다.

기존 empty punch와 relay hostProof의 packet 구성은 유지된다. client punchToken을 wire에 새로 넣지 않는다. 동일 endpoint/hostProof의 train 재요청은 남은 횟수를 25로 다시 시작하는 정책이므로, 중복 요청까지 포함해 송신 수/간격은 독립 검사에서 확인한다.

실제 SendFn은 값으로 캡처한 UDP socket에 sendto하며, 기존에도 observe/수신 reply 경로가 같은 callback을 사용한다. 새 worker는 queue lock이나 mu_를 쥐고 send하지 않는다. `host_shutdown.cpp`는 directoryAgent.Stop 반환 뒤 media socket을 닫으므로 새 thread도 같은 수명 안에서 join돼야 한다. worker의 CV 대기를 Stop이 깨우는 방향은 맞다.

일반 connect도 같은 capability 수거 cycle을 쓰므로 **동일한 지연 메커니즘의 영향 범위**에는 들어간다. 다만 특정 현장 재접속 지연 사건의 원인이 이것이었다고 사후 확정하거나 실 NAT KPI가 해결됐다고 확대하지 않는다.

## N1 (P2) — Stop 뒤 남은 train이 같은 HostAgent의 다음 시작에서 재송신됨

새 **punchJobs_는 Stop 또는 Start 어디에서도 비워지지 않는다.** Stop은 running=false→worker join→cycle join만 한다. train 중간 Stop이면 남은 작업이 vector에 남는다. 다음 Start는 send_를 새 callback으로 교체하고 running=true 및 새 PunchLoop를 시작하므로 **새 cycle이 새 capability를 수거하기 전부터 옛 target/hostProof를 송신**할 수 있다.

Stop이 punchThread를 먼저 join하는 동안 기존 Run의 HTTP/heartbeat가 늦게 반환해 Punch를 호출하는 순서도 가능하다. 따라서 큐를 worker 종료 직전에 한 번 비우는 보수만으로는 producer가 나중에 다시 넣는 틈이 남는다.

Codex 직접 제한 재현:

- `.claude/codex_ka_punch_review/make_lifetime_probe.py`가 후보의 **Stop·PunchLoop·SetStatus·Punch 원문 4개와 상수**를 그대로 추출했다. 실제 후보 class header를 사용해 검토용 exe만 컴파일했다.
- 첫 punch callback을 확인한 직후 실제 Stop을 호출하고 queue를 조사했다. 그 다음 Start에 있는 running/send_/worker 재시작 동작을 모사했다. **전체 Start·Run·HTTP는 실행하지 않았다.** callback은 수를 세는 stub이며 실제 datagram은 보내지 않는다.
- 세 번 모두 다음 결과였다. exit0은 결함 재현이다.

```
pending_jobs_after_stop=1
packets_left=24
stale_sends_after_worker_restart=1
```

- exe SHA-256 **60c831b13deb4e5e2c33d6f9147c8f0f3a5264bbe171a97d8a77ccdffe44c8be**, 생성된 cpp SHA-256 **642faebcc6ce15c547bd6fa13bfdb94755e322e607f2938ef2a14599538829bc**.

이는 **같은 HostAgent 인스턴스의 Stop→Start 수명 회귀**다. 현재 Host의 새 프로세스마다 새 객체를 만드는 일반 경로에서 이 옛 큐가 다른 프로세스로 승계된다는 주장은 아니다. 원래 30초 재입장 FINDING과도 별개의 새 큐 정리 문제다.

필요: Stop 완료 시 더 넣을 producer/송신 중 worker가 없고 큐가 비어 있다는 경계를 보장한다. 예를 들어 기존 두 thread join 뒤 punchMu_ 아래에서 자기 잔여 작업을 폐기하고, 새 시작도 이전 작업이 없는 상태에서만 진행하도록 한다. 다른 유효한 순서/세대 fence도 가능하지만 **Run이 늦게 enqueue하는 경우**를 포함해야 한다. Stop 반환 뒤 송신0, 같은 객체 재시작 전 새 요청 없으면 옛 punch0, 새 target만 정상 train, 빈 큐 Stop 및 train 도중 Stop을 확인한다. 외부 SendFn을 무기한 block하는 별도 계약까지 이번 수정으로 해결했다고 주장할 필요는 없다.

## directory-retry 통합과 남은 EVIDENCE

- dirretry **34f22de**도 같은 hpp/cpp의 Start·Stop·Run을 수정한다. 통합 시 새 punch worker의 stop/join/큐 수명과 **시험 전용 cycleCv_ 깨우기·완료 cycle gate**, observe 진단 상태를 함께 보존한다. 한 branch의 Stop을 통째로 택해 다른 쪽 wake를 버리지 않는다.
- 단일 directory cycle의 gate는 이제 별도 punch worker 송신까지 멈추는 barrier는 아니다. 통합된 retry 시험 중 outbound punch live 계수에 의존하는 assertion이 있다면 그 소유 경계를 대조하고, 관련 경우만 필요한 worker 동기화/대기를 사용한다. gate를 되돌리거나 시험을 우연한 sleep으로 통과시키지 않는다. T2 보수 전 migration 시험은 실행하지 않는 기존 조건도 유지한다.
- N1 보수 후 고정 후보에서 독립 real-directory/relay X 최초 admission→즉시 crash 재입장→Release 재입장을 반복한다. 로그의 capability 발급/수거/Host Ack 시각과 새 relay lease를 연결하고, PC 실제 persistent key 경로의 crash/EndAll/축출 뒤 재유지도 **31초 우회 없이** 확인한다. 599/1596 ms 한 회차를 보편 상한·p95로 사용하지 않는다.
- 일반 connect의 연속 capability 수거, punch packet/hostProof·횟수/간격·Stop, 기존 punch-reply 예산/HTTP 계약을 영향 범위로 검사한다. 실제 NAT·비용·APK e r2·PC N1~N6는 각각의 미결/독립 검증을 유지한다.

Codex 제품 수정·제품 빌드·운영/Host/PC 앱 실행·설치·삭제·commit/push 없음. 직접 실행은 위 실제 메서드의 격리 lifetime probe이며 파일은 저장소 안 검토 폴더에 보존했다. 검증용이 **같은 FINDING task**에 N1과 통합 주의를 취합해 기존 작업용1에 연결한다. history/계획/Git MCP 기록도 같은 구현 단위에서 처리한다. 사용자 현재 조치 없음.
