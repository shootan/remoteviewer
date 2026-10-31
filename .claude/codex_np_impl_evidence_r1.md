# NP ⓪+①b 구성 단위 EVIDENCE r1

> **영향 확인(2026-10-28): 이 구성 단위 EVIDENCE OK는 유지한다.** 고정 e16ad0d의 e2e는 Node dgram 가짜 Host와 prepared socket 확보 probe이며 GNLinkStream/Viewer 입력 경로를 실행하지 않는다. 향후 실 Viewer 통합은 별도다. [입력 주입 Finding](codex_test_injection_finding.md) 참조.

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 task np-impl r2.

**구성 단위 EVIDENCE OK — e16ad0d의 서버 응답 fence·후보 경합/호환·계측/relay 라벨 전달을 확인했다.** 제품 창·실제 세션/첫 화면·실 NAT·APK 공동 배선·운영 배포와 별도 KA 기반 미결은 아래대로 남긴다. **게시 승인 또는 전체 R2 완료가 아니다.**

## 고정점과 직접 확인

- source **e16ad0d95605abb874be3ce682c22ea7cd2109a5**, `feat/np-early-relay`, 검사 시 HEAD 일치·tracked clean. SOURCE OK 정본 `codex_np_impl_src_r2.md` 유지.
- 검증용 별도 빌드 `C:/work/gnlink-wt/np-verify-build`, configure/build 로그가 `np-impl` 소스 경로와 VS2022 x64 Debug의 실제 compile/link 출력을 가리킨다. 작업용의 `np-impl-build`와 분리돼 있다.
- Codex가 독립 빌드 산출물 SHA-256을 직접 계산했다:
  - `remote60_np_early_bootstrap_probe.exe`: **7d25fc0cf21087211ff29f64ac280aaf36120f90770a3cedcd9100c848d0af2d**.
  - `remote60_punch_any_test.exe`: **91cf5054dc5ef9567c392b3ea8c53fb34313b16065cb0d23c86d77b09a6d352d**.
  - 두 값은 인계의 해시 prefix와 일치한다. 이번 확인 시점의 파일 해시이며 실행 전후 양쪽 해시 기록을 새로 확보했다는 뜻은 아니다.
- 구 시험 서버 `test-tmp/verify-np-old/apps/directory/server.js`의 Git blob **1e0cc5ef070e70a98473691c69ebfda68079908b**가 기준0a5e71d의 server.js blob과 일치한다. 운영 NAS 서버와 동일하다고 확대하지 않는다.
- `verify_np/VERIFY_NP_e16ad0d.md`, configure/build, suite/fence, e2e1~3, punch1~3 저장 자료의 핵심 경계와 결과를 읽고 각 결과 줄을 계수했다. **실행 주체는 검증용**이며 Codex가 제품/시험 서버를 다시 실행한 것은 아니다.

## 행동 근거

| 범위 | 검증용 결과와 Codex 대조 |
|---|---|
| directory suite | PASS845 / FAIL0, 최종 RESULT ALL PASS. 단순 총합 외에 같은 tuple 교체·자기 grace·기존 활성 lease 양방향 보존의 실제 로그를 확인했다. |
| reply fence | 9/9. 정상 early0/120/240·legacy2500/2620/2740 유지, 교체/만료/sweep 뒤 옛 예약 송신0, 새 record의 자기 grace. 원문 함수 기반 가상 timer 검사이며 실제 server 반례와 분리한다. |
| 실 server 같은 tuple | suite: A 첫 응답30ms→B connect완료31ms→**afterB=0/total=1**, B 자기 응답405ms(grace400). N1의 남은120/240ms 예약이 새 legacy 경합에 섞이던 경계를 직접 겨냥한다. |
| 새 서버+early 요청, silent Host 대역 | e2e1~3에서 accepted, relayHeld1, kind=relay/relay=1, **chosenMs1001/1015/1014**. 명세1000ms와 실제 scheduler 관측을 구분한다. openMs는1007/1021/1021로 별도 기록한다. |
| 구 요청 모드/구 시험 서버 | 새 서버+flag=false 및 구 서버+새 요청에서 legacy 경합/grace 약1500ms가 유지된다. 구 서버가 early를 모르면 unknown으로 돌아간다. |
| direct 가능 대역 | 새/구 서버·요청 모드 조합 모두 direct, relay0. 준비된 relay가 있어도1000ms 전에 direct가 성공하는 경로와 합치한다. Host는 가짜 UDP 응답 대역이다. |
| punch_any |61개의 ok / FAIL0 ×3, 최종 PASS.999/1000/1001 경계, relay 미도착 시 계속 대기, 늦은 relay, cancel 우선, 형식 거부·bounded strangers·실 socket cadence를 확인한다. PC9건과 다른 기준점 참고/합성 입력의 분리도 유지한다. |

e2e1~3은 각각26/26이다. **probe의 `open=1`/“session opens”는 제품 `directory_session_open`의 후보 선택·prepared socket 확보 성공**이다. 시험은 그 socket을 닫으며 실제 authenticated Hello/control 연결·영상 decode/첫 presented까지 수행하지 않는다. 이 성공 문구를 제품 세션 E2E로 인용하지 않는다.

보수 전 c803768의 stale2 실패는 이전 SOURCE 판정에서 읽은 반대 근거로 유지한다. 이번 독립 결과가 source guard 존재만으로 PASS한 것은 아니며 실제 같은 tuple 서버 행동과 정책시간/취소 경계를 함께 확보했다.

## 통합 후보에 남기는 것

1. **실 GNLinkViewer의 relayPath 표시 변화**: 아직 렌더 근거가 없다. R2 통합 고정 후보의 격리 제품 창에서 직접/중계 표시와 실제 연결/선택→첫 정상 presented를 확인한다. enum·probe 문자열·DOM만으로 이 항목을 닫지 않는다. 이것은 전체 제품/릴리스 EVIDENCE의 필수 잔여이며 현재 구성 단위 성공과 구분한다.
2. **APK**:0.2.33 위 JNI options/report와 Kotlin relayEarly 요청/응답 parsing을 함께 적용하고 해당 APK의 빌드·경합·화면 행동으로 확인한다. 플래그만 먼저 보내는 회귀를 금지한다. 현재 APK NP 완료는 아니다.
3. **실 NAT/현장 KPI**: 회사→집 등의 도달/매핑·직접비율·relay 부하·첫 화면 시각은 미검증이다. loopback의1000ms 선택은 현장 직접비율 보장이나 모든 연결의 첫 화면1초 보장이 아니다. 기존 계획에 따라 게시 후 관찰하는 항목과 게시 전 안전/도달 확인을 분리한다.
4. **서버 게시**: 이 server.js의 운영 반영·필요 모듈·재시작·rollback/외부 계약 확인은 아직 수행하지 않았다. 구성 단위 OK를 자동 NAS 배포 승인으로 읽지 않는다. 검증용이 통합 릴리스의 승인된 순서/고정 commit에 연결한다.
5. **KA 기반**:0a5e71d의 별도 SOURCE 미결과 PC/모바일 standby 경계는 해당 후속 판정에 따른다. NP 구성 성공으로 전체 기반을 승인하거나 미검수 branch 전체를 main에 넣지 않는다. 통합 충돌 해소/관련 회귀는 새 고정 후보에서 확인한다.

검증용은 위 남은 항목을 기존 R2 통합 정본/실제 task와 담당에 연결한다. NP의 닫힌 순수 경합·같은 tuple 서버 시험을 매 commit 전량 반복할 필요는 없으며, 통합으로 바뀐 연결/UI·출하 경계만 다시 확인한다. 새 별건 관문이나 동일 범위의 중복 writer를 만들지 않는다.

Codex 제품 편집·빌드·시험/서버 실행·삭제·설치·commit/push 없음. 직접 확인은 파일 hash·저장 로그/계수·Git 상태/구 서버 blob이다. 이 미추적 운영 판정서 외 공개 history/계획·Git MCP commit은 검증용이 같은 구현/통합 기록에 취합한다. 사용자 현재 조치 없음.
