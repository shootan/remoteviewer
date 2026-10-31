# KA r2 ② — 범위 5건 결정 및 SOURCE 검토

2026-10-27 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z.

**범위 결정 1~5는 아래 조건으로 GO. SOURCE는 NEEDS_CHANGES(N1~N3). 기존 Host/서버 r4 SOURCE OK·N4 시험 경계 OK는 유지한다. 자동 preconnect 출하 OFF·NAS 무변경, 독립 EVIDENCE/실 NAT/전체 R2 완료 승인은 아니다.**

고정 `spike/host-admission-r3` **0a5e71d**, 비교 **cf8fb70..0a5e71d**, 12파일/추가1912·삭제6. 새 entry client·lease·runner·directory helper·file store 및 기존 controller/directory/CMake의 전체 변경, 상태기계/runner/relay 시험의 관련 반례와 계약 §2.2~2.7을 읽었다. 검사 시 tracked clean, `git diff --check` rc0. 실행 결과 56/56·63/63 등은 작업용 보고이며 Codex 새 빌드/Host/시험 실행은 없다.

## 범위 5건

| 건 | 결정 |
|---|---|
| 1 브랜치 | GO. `fix/apk-prelogin-select` b554e76은 고정 유지. KA shared/Host는 ka-r3, APK는 새 0.2.33에서 병합한다. 검수한 고정 commit까지만 채택하며 이후 KA commit/충돌 해결은 별도 통합 diff로 검토한다. main의 정책2137f3a는 통합 때 보존한다. |
| 2 activation 취소 | GO. 클라이언트는 완료를 채택하지 않고 그 op를 영구 무효화한다. 이미 Host grant가 났을 수 있으므로 Release를 강제로 성공 처리하지 않으며 기존 peer-lost/세션 끝으로 닫힌다. 취소가 Host의 즉시 제어권 반환/rollback이라는 표시는 하지 않는다. 다음 activation은 더 큰 gen, 늦은 A 응답과 B를 분리한다. 객체·소켓 수명은 N2로 보수한다. |
| 3 gen/device 저장 | 조건부 GO. 저장 실패는 송신0, 소진은 wrap 없이 거부. 손상/누락은 새로운 CSPRNG device incarnation으로 시작하며 옛 entry는 만료에 맡긴다. 유효한 같은 device id를 유지한 채 gen만 작아지는 복구는 금지한다. 전체 install에서 단일 allocator/저장 writer를 쓰며 N3의 실제 내구 예약이 필요하다. |
| 4 폰 유지 범위 | GO. 화면에 보일 때만, onStop에 standby 해제, 로그아웃/계정 변경/명시 제거도 회수. **현재 활성 세션의 객체를 동기화 없이 파괴하라는 뜻은 아니다.** 종료/재개 시 늦은 directory 응답과 activation을 옛 owner로 걸러야 한다. |
| 5 ② 검증 범위 | GO. 이번 범위는 상태기계·소켓/핸드오프·generation·제품 activation·relay 결합 및 APK 배선. §6 비용·§7 교차기기·실 NAT는 ③④ 후속으로 구분할 수 있다. 자동 preconnect/운영 배포를 완료하려면 그 후속과 독립 사용자 행동 검증을 통과해야 한다. 현재 소스 결함이나 시간/수명 상한은 비용 후속으로 미루지 않는다. |

## N1 — activation HelloAck가 일반 handshake보다 약하게 수용됨

`standby_lease.hpp:242~247`은 source tuple·magic·kind·실 datagram 최소길이만 보고 HelloAck를 복사한다. `Handshake:284~286`은 그 features를 채택해 `OnHelloAck`를 호출한다. **declared size·version·필수 VideoFec/DirectoryAuth 검증이 없다.** 유효한 현재 Pong을 받은 뒤 같은 endpoint의 잘못된 버전/크기 또는 인증 feature 없는 Ack도 Activated로 연결할 수 있다. 원 session의 udpHandshake를 대체하므로 원 함수가 나중에 이를 검증해 주지 않는다.

필요: 기존 일반 HelloAck의 실제 형식/기능 검증을 공유하거나 동등하게 적용하고 인증된 standby activation에 필요한 bit도 확인한다. invalid Ack는 준비/완료/협상 features를 갱신하지 않는다. 정상 역순·한쪽 손실/동일gen 재전송은 유지한다.

추가로 현재 cancel 시험의 `OnHelloAck(...).empty()`는 **무시 증거가 아니다**. B Activating에서 gotHelloAck=true를 저장하더라도 Pong이 아직 없으면 empty이기 때문이다(`standby_entry_client_test.cpp:279~283`). A 취소→B 시작→지연 A Ack→B Pong, 옛 entry 패킷/새 capability 경계를 검사해 실제 상태·협상값/채택을 대조한다. wire에 gen이 없는 Ack를 B의 것이라고 증명했다는 주장은 피한다. 현재 gen Pong이 grant 증명이고 untagged Ack는 검증된 기능 협상일 뿐인 계약을 쓸지, 추가 상관이 필요한지는 같은 정본에서 근거로 명시한다. bare/옛 Ack만으로 새 grant를 만들지 않는 조건은 유지한다.

## N2 — runner 해제와 handed-off 콜백의 수명/경합

`StandbyLease::ActivationArgs:146~148`의 lambda는 raw **this**를 캡처한다. `StandbyRunner::TakeActivation`은 args에 slot의 shared ownership을 넘기지 않는다. 반면 `Release/ReleaseAll`은 map에서 slot을 빼고 pump만 join한 뒤 slot/lease를 파괴할 수 있다. `Stop:178~182`는 session connect thread를 join하거나 outstanding args/handshake 수명을 고정하지 않는다. **TakeActivation→Release(All)→args로 Connect**, 또는 handshake 도중 onStop/로그아웃이면 dangling this가 가능하다. Release가 handedOff에서 packet을 안 보내는 것만으로 객체 생존은 보장되지 않는다.

또한 handshake는 client_를 slot mutex 밖에서 수정하는데 SessionOver는 같은 lease를 해당 mutex 안에서 바로 변경한다. 실제 connect가 끝나기 전에 호출하면 mutex가 양쪽을 보호하지 않는다.

필요: operation/slot에 소유권을 고정하고 취소·connect 종료·SessionOver·폐기 순서를 하나의 계약으로 직렬화한다. 앱에 암묵적으로 Release를 절대 못 부르게 하는 해법은 visible-only/로그아웃 회수와 맞지 않는다. session handle도 성공/실패/args 미사용 시 명확히 회수하며, lease pump와 session reader가 동시에 같은 socket을 읽지 않아야 한다.

반례: args 획득 직후 ReleaseAll, handshake receive 대기 중 Release, 계정/화면 이탈, 늦은 SessionOver와 동일 key의 새 lease. UAF/이중 close/옛 결과의 새 lease 적용/새 화면 열기0을 확인한다. handshake 완료 전에 조기 SessionOver가 reader를 재개하지 않는지도 검사한다. 테스트에서 Release를 성공 handshake/Disconnect 뒤에만 부른 정상 순서로 이를 대신하지 않는다.

## N3 — generation 파일은 아직 내구 예약을 보장하지 않음

`standby_file_store.hpp:45~52`는 `fwrite + fflush`, `fclose`(결과 미확인), 이어 rename/MoveFileEx(REPLACE_EXISTING) 성공을 Save 성공으로 보고 다음 gen을 송신한다. `fflush`는 stream 버퍼를 넘길 뿐이고 현재 코드에는 파일 durable flush 및 교체 metadata 보장 경계가 없다. 이전 **유효한**20바이트 파일(id 동일·gen 작음)이 재시작 후 남는 경우 `Load`는 손상이 아니라고 수용한다. 단순 새 프로세스의 정상 재열기 시험은 이 경계를 검사하지 않는다.

필요: 지원하는 실패 모델(앱 강제종료/OS crash 등)을 정본에 명시하고, 플랫폼 파일 flush·원자 교체·필요한 directory/replace 내구 경계 및 실패 반환을 갖춘 뒤에만 gen을 송신한다. 불확실하면 같은 id로 작은 값 재사용 대신 fail closed/새 incarnation의 안전 정책을 쓴다. fclose/flush/replace 실패를 숨기지 않는다. 한 allocator mutex가 서로 다른 allocator/프로세스의 같은 `.tmp`를 자동으로 보호한다고 주장하지 않는다; 제품은 단일 store writer를 보장하거나 충돌을 거부한다.

임의 system crash 시험을 사용자 PC에 하지 않는다. 저장 경계별 fault 주입/재열기와 순서 oracle로 **persist 성공 전 송신0·같은 id의 gen 비감소**, 저장 손상 시 새 id/실패 거부를 검증한다. Windows 경로는 app filesDir의 비ASCII 이름도 지원해야 하며 narrow fopen/MoveFileExA의 지원 범위를 명확히 한다.

## 유지하는 구현/후속

현재 gen 사전 Save와 allocator mutex, 동일gen Activate 재송신, 현재 entry/gen Pong + Ack 양쪽 요구, probe 무응답 유한 Unsupported·legacy Hello 자동 fallback0, Ping 16초 Lost, lease별 socket과 중복 handle로 endpoint 유지 방향은 계약에 맞다. 제품 native 연결은 udpHandshake가 비어 있으면 옛 Hello 경로를 사용한다. directory support helper는 존재하며 e2e는 호출하지만, 실제 앱/후속 helper 사용자도 **첫 mutating connect 전에** 검사를 강제해야 한다.

`events_`는 lifetime 동안 unbounded vector다. 장시간 retry/경로 손실 반복의 비용/진단은 bounded ring 또는 상한 집계로 정리하고 secrets는 남기지 않는다. StatusOf의 개별 atomic 여러 읽기를 coherent snapshot으로 부르거나 pump가 멈춘 동안의 Activating을 실제 Host owner로 표시하지 않는다. APK 상태 정책과 함께 확인한다.

검증용은 N1~N3를 같은 KA ② task/유효 지시서로 취합해 기존 writer에게 한 묶음 전달한다. 보수 diff와 제한 반례 후 SOURCE 재검토→고정 후보 독립 실행. Host 이동 후 heartbeat/proof Punch 손실의 **실 server.js+제품 client bounded 회수**는 아직 보고상 미실행이므로 최종 EVIDENCE에 남긴다. Host 기존 SOURCE·안전 tmp guard를 다시 전량 검사하거나 APK0.2.32를 선점할 필요는 없다.

Codex 제품 편집·빌드·시험/서버 실행·삭제·commit/push 없음. 미추적 판정서 외 history/계획·Git MCP commit은 같은 구현 기록으로 취합한다. 사용자 현재 조치 없음.
