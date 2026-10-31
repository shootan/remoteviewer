# NP 설계 r1 — Codex 검토/결정

2026-10-27 / remote#tt7fcvky → 검증용 remote#7fsfy22z.

**수정 조건부 설계 GO. ⓪ 계측/relay 라벨 보존 + ①b 조기 relay 준비를 첫 묶음으로 앞당긴다. ①a는 새 Host의 명시 지원과 시도별 proof가 있을 때만 별도 단계로 진행한다. 구 Host의 공인 IP만 일치하는 무토큰 prflx 채택은 NO-GO다. 운영 배포/SOURCE/EVIDENCE 승인은 아니다.**

입력 `proposal_np_r1.md`, `task_np_design_r1.md`, 저장 aggregate_out/timing_summary, main의 `directory_rendezvous.cpp`, `directory_session_bootstrap.cpp`, `punch_reply.hpp`, `host_startup_connect.cpp`, KA 서버 relay/proof 경로를 읽었다. 측정 수치는 작업용의 저장 집계 귀속이며 새 NAS 수집/시험을 실행하지 않았다.

## 검증용 안과 비교

| 항목 | 판정/확정 |
|---|---|
| ①b를 ⓪와 함께 앞당김 | 동의. ①a의 효과가 미확정이어도 중계 대기 개선을 독립 구현·측정 가능하다. KA와 겹치는 server/shared native는 같은 writer의 고정 기준 위에 순차 적용한다. KA 전체 출하/main 병합 완료를 불필요한 개발 선행조건으로 만들지 않는다. |
| 구 Host 약한 prflx 규칙 | 허용하지 않음. 같은 NAT의 다른 PC/서비스 및 위조 Punch를 목표 Host와 결합할 증거가 없다. 기존 정확 tuple 후보/relay 호환은 유지한다. |
| T_commit | 새 협상 경로에서 **1000 ms**, 아래 단일 기준점·fallback 규칙 적용. 450~650 ms는 이번 혼합 기준 표본으로 정당화하지 않는다. |
| relay 즉시 사용 후 직접 이전 / wake tuple | R2 밖 보류 유지. endpoint·epoch·control/KA 상태 이전이나 위조 wake 송신 증폭을 이번 작업에 섞지 않는다. |

## 원인 표현 수정

재매핑 기간의 relay 편중 및 비일치 출처를 버리는 소스는 H-a와 부합한다. 그러나 성공/실패의 wake 지연 분포가 겹친다는 사실만으로 **타이밍 원인 배제 또는 tuple 불일치 확정은 불가**다. 손실·필터·Host/HTTP 일정·대조 시각 등의 다른 원인도 남는다. `Punch()` 25×200 ms는 최대 송신 기간이지 실패 시 양쪽 패킷이 상대에게 도착했다는 증거가 아니다.

⓪에서 유효 Punch가 public IP의 다른 port에서 도착하면 해당 시도의 후보 불일치를 지지한다. 수신0은 H-b 확정이 아니라 그 socket에서 관측하지 못했다는 뜻이다. relay/직접 정의, 대상·버전·시작 시점과 분모를 고정해 필드 KPI를 낸다. Host 세션 시차로 추론한 분류를 실제 peer tuple 판정과 구분한다. 20건은 초기 관찰값이며 보편 개선률 보장은 아니다.

## ⓪ + ①b 첫 구현 계약

1. 비일치 수신은 길이·magic·declared bytes·version·kind 유효성, 첫 도착 경과시간과 bounded tuple별 집계를 남긴다. 원문 token/proof/패킷은 기록하지 않는다. 수신 flood가 메모리·로그·송신/취소 시간을 무한히 늘리지 않게 기존 round/read budget을 보존한다. 후보별 송신/응답과 최종 직접/relay 라벨을 PC/APK 끝까지 보존한다.
2. `relayEarly` 요청 플래그는 서버가 유효한 인증/관찰/relay 예약을 만든 **해당 connect**에만 적용한다. 응답에 early 수락을 명시해 새 뷰어가 알 수 있게 한다. 구 connect의 2500 ms grace 및 플래그를 모르는 구 서버와의 기존 호환은 유지한다. standby relay lease와 legacy connect는 현재 클래스/인증/상한을 유지하며 혼합하지 않는다.
3. **T0는 해당 prepared media socket으로 첫 candidate Punch를 송신하기 직전의 monotonic 시각**이다. 새 협상 경로는 relay 응답을 보관하고, T0+1000 ms 전 유효 direct 응답을 우선한다. 수신 timeout은 nextSend뿐 아니라 commit deadline에도 맞춰 남은 시간만 기다린다. 전체 connect/UI 시각도 따로 기록한다. 표본의 APK connect 시각을 이 T0와 같은 측정치로 합치지 않는다.
4. 1000 ms에 **검증된 relay 응답이 있을 때만** relay를 선택한다. 없으면 기존 전체 punch 예산 안에서 direct/relay를 계속 기다리며 유효 relay 도착 시 선택한다. 구 서버의 2500 ms 응답 전에 1000 ms timeout으로 실패하지 않는다. cancel은 모든 선택/Hello 전에 우선하며 선택은 한 번만 한다. 확정 뒤 늦은 후보로 활성 경로를 바꾸지 않는다.
5. 응답은 정확한 구조/형식과 해당 허용 source를 검사한다. garbage/0B/잘못된 magic·bytes·version·kind로 준비 완료하지 않는다. **Punch 응답은 reachability 증거이지 로그인/Host 인증 완료가 아니다.** 실제 Hello/세션 인증은 기존대로 수행한다. relay 준비 성공·경로 선택·HelloAck·첫 화면 지연은 별도 측정한다. 1000 ms 선택이 곧 화면 표시 1000 ms 보장은 아니다.

기본값 1000 ms는 관측 성공을 보존하려는 초기 정책값이다. 통제된 direct 지연이 999/1000/1001 ms인 경계, relay 먼저/나중/유실, 모두 무응답, 구 서버, 취소/ICMP/flood 반례로 확인한다. 예약이 즉시 bind되어 기존 active session을 선점하지 않는지도 검사한다. 기존 direct 표본을 replay한 회귀와 필드 직접 비율/부하를 분리한다.

## ①a 후속 proof 계약

- 새 Host 지원을 신뢰 가능한 directory metadata로 확인한 시도에만 prflx를 허용한다. **구 Host 또는 지원 unknown은 정확 tuple 후보 + relay만** 쓴다. 같은 공인 IP/형식만으로 확대하지 않는다.
- KA `hostProof`는 서버→Host만 아는 **relay host-facing pin 증명**이다. viewer에게 공개/반환하거나 일반 prflx용으로 재사용하지 않는다. client admission `punchToken`도 새 출처를 시험할 목적으로 추가 주소에 보내는 확대를 피한다.
- 일반 연결용으로 별도의 **시도별 무작위 128-bit connectivity proof**를 인증된 connect 응답과 해당 Host의 인증된 pendingPunch에 전달하는 방식으로 범위를 확정한다. 기존 49B Punch `authToken` 슬롯은 목적지/패킷 용도별로 한 값만 넣는다: standby relay-facing은 hostProof, 새 일반 NP host→viewer는 connectivity proof. 두 값을 덮어쓰거나 standby 목적지로 일반 proof를 보내지 않는다. 새 wire kind/번호가 필요하면 검증용이 충돌 없이 예약한다.
- proof는 connect/Host/수명과 결합하며 옛 시도·다른 Host·다른 lease의 proof를 거부한다. public IP 조건/relay endpoint 제외/유한 후보 수와 연결 검사를 함께 둔다. proof 없는 반사 응답을 토큰 유효 응답으로 격상하지 않는다. 이 proof는 공격자에게서 온 임의 UDP를 Host의 신원으로 인증하는 일반 보안 프로토콜이라는 주장도 하지 않는다.
- `HelloAck(기존 인증)`으로 약한 규칙의 위험이 지연에만 제한된다는 제안은 삭제한다. 읽은 Host는 입력 capability를 검증한 뒤 feature bit를 넣은 Ack를 보내며, 이것만으로 상대가 그 Host임을 암호학적으로 증명한다고 볼 근거는 없다. 인증 bit/정상 형식과 상호 인증을 혼동하지 않는다. 새 prflx에 admission capability를 전송하기 전에 위 연결 증명이 먼저다.

⓪/①b를 이 proof 설계 완료 때문에 막지 않는다. 검증용은 변경된 순서·비교/이견을 원 정본에 취합하고 하나의 구현 지시서로 전달한다. proof 세부 형식은 구현 source 검토에서 위 경계를 확인하며 새 별건 task를 증식시키지 않는다.

공유기/UPnP/NAT-PMP·외부 observer·NAS 권한/운영 설정 변경은 승인하지 않는다. 사용자 설치/재시작도 없다. Codex 제품 수정·빌드·시험·삭제·commit/push 없음. 운영 판정서 외 history/계획·Git MCP commit은 같은 구현 기록에 취합한다. 사용자 현재 조치 없음.
