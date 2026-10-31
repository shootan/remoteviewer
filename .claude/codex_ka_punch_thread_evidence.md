# KA Host punch thread — 독립 EVIDENCE 판정

> **추가 확인(2026-10-28 Finding-3): 기존 실 Host e2e 사용 보류는 wake/전원 효과까지 포함한다.** input OFF만으로 무조작 실행이 아니며 constructor/stream power 경계를 검증한 뒤 해당 행동을 재확인한다. recorder/FakeHost 등 순수 근거는 유지한다. [Finding-3](codex_test_injection_finding.md) 참조.

> **판정 정정(2026-10-28): 실제 GNLinkStream e2e 78/0×2는 입력 주입 OFF 실행 전제 미확인으로 재확인 대기/관문 사용 보류다.** 아래 최초 OK의 기능 관측과 해시는 보존한다. 송신 기록기 punch·entry·FakeHost lease의 순수 근거는 유지한다. 무주입 격리 완료로 인용하지 않으며 최신 범위는 [입력 주입 Finding](codex_test_injection_finding.md)을 따른다.

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 keepalive-r2 FINDING.

**EVIDENCE OK — 534242b의 Host punch 병목·큐 수명 구성 단위 범위.** 기존 제품 N1 및 시험 T2 SOURCE OK와 연결한다. 실제 PC 유지 배선·APK e r2·dirretry 통합·전체 KA 출하 완료를 뜻하지 않는다.

## 고정 후보·독립 산출물

- 고정 commit **534242b8186d5951318c6f39d5f19326c51c2446**. 정본 보고 `.claude/verify_ka/VERIFY_KA_534242b.md`, 유효 로그 **`.claude/verify_ka/r3/`**를 대조했다.
- `configure3.log`와 `build3.log`에서 별도 **C:/work/gnlink-wt/ka-r3/build-verify** 구성·directory_client/새 수명 시험 재컴파일·산출물 경로를 확인했다. 현재 exe의 SHA-256을 직접 계산해 보고와 대조했다.

| 독립 산출물 | 직접 확인한 SHA-256 |
|---|---|
| GNLinkStream.exe | bac75dc0fac625fc448776ee3223fdb7921176197928999ab6806e25760d67eb |
| host_standby_admission_e2e | a9d16925e9fc85f821a95e7601d0ab5957c0769320d89947cc83760d44a15f4c |
| host_agent_punch | 5c73dc73ea14a469a69b5453eb708d2ce97ed5085b4afdb2d639690b92aceaa8 |
| standby_entry_client | cc1b119fd86cf62a5ae2d787c7919af8be72ee669660ac9544cdf307affb76df |
| standby_lease | 7b20b785e2ba5782eac00e70ebd56d54314a0ae13a5b76495dfd94c9804caa69 |

- 구 Host는 이전에 공개 서명 manifest와 연결해 확인한 **0.2.163 payload GNLinkStream**이며 이번에도 hash **825feb97a067b7b7841a5317fca220c7b6fa59bb917412dc9cc96ed8a5791b35**를 확인했다. 가짜 구 Host의 stub 결과가 아니다.
- 현재 tracked 변경은 시험 상단의 과거 삭제 기록을 정확히 고친 **주석만**이다. diff를 확인했고 실행 코드·제품 변경은 없다. 이전 SOURCE에서 같은 기록 취합 때 처리하도록 허용했던 정정이며 새 검증 대상 동작으로 취급하지 않는다. 최종 통합 기록에는 이 사실도 연결한다.

## 원문 행동 대조

**검증용의 독립 실행**을 다음과 같이 수용한다. Codex가 이 전체 suite를 새로 실행한 것은 아니다.

- `punch_1/2/3.txt`: **각 9/9, RESULT PASS**, 보고 rc0×3. train 도중 Stop 뒤 pending 0·추가 callback 0, stopped Punch 큐잉 0, 실제 같은 객체 재Start 뒤 old/late 0, 새 target/proof train 정상, 재Stop 뒤 빈 큐를 대조했다.
- `entry.txt`: **72/0**. 실제 영속 id/gen·비ASCII 경로를 포함한 기존 entry 검사다.
- `lease.txt`: **66 checks / 0 failures, RESULT PASS**. 이번 산출물의 공용 회귀 결과로 기록한다. 이를 별도 SOURCE 판정이 남은 e r2 전체 승인으로 확장하지 않는다.
- `e2e_1/2.txt`: 실제 product-client + real-directory + 게시본 old-host 경로의 **각 terminal ALL PASS (78 checks, 0 failed)** 및 보고 rc0×2를 확인했다. 각 새 X가 relay에서 최초 admission된 뒤 다음 연속 동작이 성공한다.

| 동작 | 1회차 | 2회차 |
|---|---:|---:|
| 같은 기기 crash 직후 새 admission | **627 ms** | **629 ms** |
| Release 직후 다시 admission | **1619 ms** | **594 ms** |

X 최초 성공·서로 다른 relay lease의 ready 증가·Host capability 수거/standby 응답을 원문과 연결했다. 같은 기기의 즉시 재입장을 31초 기다려 통과시킨 결과가 아니다. 기존 direct/relay activation·제품 controller 영상·takeover·세션 종료 뒤 같은 lease 복귀, 구 0.2.163 Unsupported와 자동 세션 생성 없음의 회귀 결과도 유지한다.

첫 e2e 로그는 일부 check 행의 PASS 접두가 깨져 있다. 단순 `^PASS` 줄 수만으로 총합을 만들지 않고, 해당 행 본문·check의 실패 카운터/마지막 78/0·두 번째 로그의 온전한 결과를 대조했다. 핵심 즉시 재입장 행과 두 로그의 최종 성공은 보존돼 있다. 이 출력 손상을 지우거나 그 행이 미실행이었다고 바꾸지 않는다.

기존 보수 전 약 5초 capability 수거 지연/거절은 SOURCE 판정에서 확인한 `kae_relapse5` 근거와 연결한다. 그 구 실행의 모든 하위 check가 각각 실패한 것은 아니라는 이전 한계도 유지한다. 이번 두 회차의 시각은 **관측값**이며 보편 상한·p95·실 NAT 개선율이 아니다.

## 실행 경계·남은 일

- 시험 root **`.claude/test-tmp/verify-ka`**는 현재 일반 디렉터리이며 직접 확인 시 비어 있다. 시험 T2의 경로 선거부·기존 파일 보존은 앞선 Codex 직접 r4 검사와 함께 유지한다. 바깥 과거 삭제 호출의 확인 한계를 이번 결과로 소급 해소하지 않는다.
- **실제 PC persistent key의 crash/EndAll/축출/해제 직후 재유지**는 feat/ka-pc에 검수된 공용 후보를 합친 뒤 검증한다. 이 구성 단위의 StandbyLease 시나리오가 실제 셸 카드/프로세스 배선을 대신하지 않는다.
- **e r2의 별도 SOURCE 및 AVD 행동**은 별도다. 현재 branch가 그 공용 변경을 포함하고 있다는 이유로 이전 APK/PC 미결을 자동 승인하지 않는다.
- dirretry d11ef62/d6bb951 계열 통합 때 **cycleCv_ notify/gate + punch worker wake/join/큐 clear**를 함께 보존하고 병합 후보의 해당 경계를 확인한다. SOURCE가 통과한 각 branch의 결과를 충돌 해결 후 제품 회귀의 대체물로 쓰지 않는다.
- 실 NAT·비용·현장 일반 재접속 지연의 원인/개선량은 미검증이다. 운영 배포·설치·사용자 실기도 이번 local EVIDENCE가 아니다.

검증용은 이 구성 단위 통과를 같은 FINDING 기록에 반영하고 PC/AVD·통합의 실제 다음 담당/고정 후보로 연결한다. 불필요한 이 component의 재승인 대기 없이 독립적으로 가능한 후속 작업을 계속한다. Codex 제품 편집·빌드·전체 시험 재실행·운영/NAS·삭제·commit/push 없음. 사용자 현재 조치 없음.
