# KA r2 ② 공용 구성 단위 EVIDENCE

> **추가 확인(2026-10-28 Finding-3): 기존 실 Host e2e 사용 보류는 wake/전원 효과까지 포함한다.** input OFF만으로 무조작 실행이 아니며 constructor/stream power 경계를 검증한 뒤 해당 행동을 재확인한다. 순수 근거는 유지한다. [Finding-3](codex_test_injection_finding.md) 참조.

> **판정 정정(2026-10-28): 실제 GNLinkStream e2e 74/0×2는 입력 주입 OFF 실행 전제 미확인으로 재확인 대기/관문 사용 보류다.** 아래 최초 OK는 기능 관측 원문으로 보존하며 무주입 격리 완료를 뜻하지 않는다. table·entry·FakeHost lease 등 확인된 순수 범위는 유지한다. 최신 범위는 [입력 주입 Finding](codex_test_injection_finding.md)을 따른다.

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z.

**공용 구성 단위 EVIDENCE OK — 82679b2의 검증된 admission/activation·회수·기기 간 무간섭·제품 session link 및 시험 relay 흐름을 수용한다.** 실제 앱 배선/AVD·PC③·실 NAT·비용·전체 교차 기기/운영 배포는 아래 미검증으로 남긴다. **출하 승인·전체 R2 완료 판정이 아니다.**

## 고정 대상/직접 확인

- source **82679b23cd157e4001ef87ba1841c5448c1ab8a3**, 검사 시 HEAD 일치·tracked clean. SOURCE 정본 `codex_keepalive_r2_src_r3b.md` 유지.
- 검증용의 별도 `C:/work/gnlink-wt/ka-r3/build-verify`, VS2022 x64 Release. configure2/build2 로그는 해당 경로의 실제 compile/link를 가리킨다. 작업용 출력과 분리돼 있다.
- Codex가 현재 고정 산출물의 전체 SHA-256을 직접 계산해 인계 prefix와 대조했다.

| 파일 | SHA-256 |
|---|---|
| GNLinkStream.exe | 3c02bce3134bde800241ba419d71eec94e9871b687b9748fb18e4260ec571f28 |
| host_standby_admission_e2e_test.exe | 397ac86e314f1231c1b90e6366fcc4b2e37aa44df333130a3d12dc7df4351160 |
| host_standby_table_test.exe | 86b7f3f269d07e68001eafe69dc96ef3352890ba83db632e5706c1821819d980 |
| standby_entry_client_test.exe | cc1b119fd86cf62a5ae2d787c7919af8be72ee669660ac9544cdf307affb76df |
| standby_lease_test.exe | 26273d419cf0ff76a63540b0a12123941ee555ed47d63b55b13d0740c506cec7 |

표의 시험 실행 파일명은 실제 이름의 `remote60_` prefix를 생략했다. 위 값은 이번 확인 시점의 hash이며, Codex가 실행 전후 양쪽 값을 새로 확보했다는 의미는 아니다.

- **구 Host는 실제 공개0.2.163**: `rel-0163/.claude/rel-final/0.2.163/payload/GNLinkStream.exe` SHA **825feb97a067b7b7841a5317fca220c7b6fa59bb917412dc9cc96ed8a5791b35**를 직접 계산했다. 해당 windows.manifest의 SHA **6d89ae487a125ebc620e46e6be5396cb3131dc6380878b596a85b47edf64dfd3**는 기존에 승인한 공개 pair와 일치하고, artifact 행은 같은 Stream hash/1,614,336 bytes를 지정한다. 새 바이너리에서 구 요청 모드만 흉내 낸 대조와 구분된다.

## 채택한 실행 근거

정본 `verify_ka/VERIFY_KA_82679b2.md`와 **`verify_ka/r2/`의7개 txt**를 대조했다. 이 실행은 **검증용 귀속**이며 Codex가 시험/실 Host/Node를 다시 실행한 것은 아니다.

| 독립 검사 | 확인 결과 |
|---|---|
| table |56/0, 최종 ALL PASS. full digest·입장 gen/replay·실패 시 capability 미소비 등 현재 계약을 검사한다. |
| entry/store |72/0. 비ASCII 경로의 실제 파일 소유·두 번째 store 거부·동일id 재시작/단조 gen, 단계별 저장 실패 송신0·재열기, 손상 시 새 incarnation을 확인한다. 실제 전원 손실 시험은 아니다. |
| lease |52 checks/0 failures ×3. 양쪽 Ack/Pong 순서에서 Release 후 late 완료 채택0, pending handoff/observe 중 새 read0, slot 수명·stale op·Abandon·bounded event·Ping 재개를 포함한다. |
| 제품 공용 e2e |**74/0 ×2**. 아래 실제 행동과 최종 RESULT를 각각 확인했다. |

공용 e2e의 행동:

- A·B가 서로 다른 standby entry/socket으로 대기하면서 session0→A 명시 activation/영상·control→B의12초 Ping 중 새 session/control end0·A 유지→B 명시 takeover→A 세션 종료 및 **같은 socket으로 parked 복귀**→더 큰 gen의 명시 재activation.
- runner의 async capability와 op 소유를 사용하며, session-link는 stale-before-connect·Connect 거부의 Abandon, 실제 controller 연결/영상, **Disconnect 반환 뒤 Over/SessionOver**, Over 중복 무효, Host의 실제 session end와 Ping 유지를 검사한다.
- Release는 Host에서 StandbyRelease1회로 회수한다. local 상태만으로 Host grant 반환을 주장하지 않고 실제 session end를 별도 확인한다.
- 실제 시험 `server.js` relay에서 read-only 지원 확인→lease별 standby connect/reservation→각자 relay parked→activation/영상→다른 lease Ping 무간섭·명시 takeover→같은 relay lease/socket 유지·새 connect0으로 회수한다.
- 공개0.2.163 대조물은 **Host 시작/격리 cache 사용이 성공한 뒤** StandbyHello 무응답→Unsupported, ordinary Hello/새 session admission0을 검사한다. Host가 시작하지 않았는데 무응답만으로 미지원 PASS한 1차 로그를 근거로 쓰지 않았다.

이 영상 근거는 공용 controller의 encoded sink 도달이다. 실제 APK/PC Viewer의 decoder·render/presented·버튼 입력 성공을 대신하지 않는다. 공용 fixture 둘의 takeover는 실 휴대폰+PC 조합의 최종 교차 기기 검증과 구분한다.

## 1차 무효 실행의 처리

상위 `verify_ka/e2e_1.txt`는 외부 sibling build의 scratch guard 거부로51개 실패, `entry.txt`는 tmp root 미생성으로10개 실패다. 상위e2e_2에는 미완료 로그도 남아 있다. **이 파일들은 PASS로 승계하지 않고 실패/미완료 원문으로 보존**한다. 정정 후 별도 build-verify·생성한 tmp root와 `r2/`의 완주 결과만 이번 판정에 쓴다.

현재 근거는 setup 실패 설명과 구분돼 있으므로 그 실패를 제품 기능 회귀로 합치지 않는다. guard가 실제로 실행을 막았다는 부수 사실과 정상 기능의 성공도 서로 다른 증거다. 정본문서의 유효 원문 경로에 `r2/`를 명시하면 이후 무효 자료와 혼동하지 않는다. 이 기록 정정 때문에 기능 시험을 다시 전량 반복할 필요는 없다.

## 통합/출하까지 남은 것

1. **APK e 실제 배선/AVD**: 모델A·스위치ⓐ, 실제 tap/목록/준비상태·cold 첫 전환·지원 없는 Host, onStop/계정 전환/늦은 Provide·Close, runner/link 수명·Disconnect→Over의 실제 JNI 연결을 확인한다. 기존 UA-2 M1은 옛 resume 경로 제거와 새session 반례로 닫는다. 구성 단위 helper 성공만으로 완료하지 않는다.
2. **Host 이동/relay proof 손실**: 실제 시험 server.js와 제품 client 조합에서 fresh proof/응답 유실 시 ready를 영구 유지하지 않고 bounded Lost/회수·non-preempt 재입장으로 가는 통합 실패 경로를 확인한다. 현재 정상 relay 유지 결과나 pure state 시험과 구분한다.
3. **PC③·실 기기 교차**: managed 프로세스/IPC/계정·Job 수명, 실제 사용자 창/렌더, 실제APK+PC의 standby 공존·다른 active 무간섭·명시 takeover를 확인한다. network 종료 뒤 자동 재activation 금지도 유지한다.
4. **비용·실 NAT·운영**:0/1/3 standby·active1+standby2의 안정 비용/5분 초과·증가 추세·디렉터리 재시도, 실제 NAT/포트 도달, 운영 relay/NAS 게시·rollback/외부 계약은 별도 후속이다. 이번 loopback 성공을 그 완료로 쓰지 않는다.

검증용은 위 잔여를 기존 R2 통합 정본/담당 task에 연결해 계속한다. 닫힌 공용 내부 행렬을 매 commit 전량 반복하기보다 배선/통합으로 바뀐 영향 범위와 사용자 행동을 확인한다. 부수 문서나 자동 completed로 다음 담당을 대체하지 않는다.

Codex 제품 편집·빌드·시험/프로세스 실행·설치·서명/운영 배포·삭제·commit/push 없음. 이번 직접 확인은 저장 원문/Git 상태·파일 hash/구 공개 manifest 대조다. 미추적 판정서 외 공개 history/계획·Git MCP commit은 검증용이 같은 구현/통합 기록에 취합한다. 사용자 현재 조치 없음.
