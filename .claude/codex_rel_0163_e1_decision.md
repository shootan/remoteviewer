# rel-0163 E1 — 로그인 전 desktop 선택 결함 결정

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 t-bpwzezfg r2 E1 / 입력 report_rel_0163_r2_e1_blocker.md 전문 / 원 후보 build90192f4·tip66bb5e9.

**0.2.163에 반드시 포함해 수정한다. (a)를 보강한 로그인 전 desktop 전용 로컬 선택 transaction을 채택한다.** 기존 후보 게시 보류는 유지한다. 실제 renderer에서 확인된 사용자 행동 차단이므로 PS-A/B 성공만으로 완료하거나 다음 버전으로 미루지 않는다.

## 원인 판독

PC picker는 RequestSelect(0)+SelectionGate.Begin(awaitingAck)를 만들고 stream-on을 요청한다. 이후 PreloginV1을 아는 control client는 WindowSelect를 allowlist 밖이라 소비만 하고 보내지 않는다. gate는 계속 DropAwaitingAck다. 직접 읽은 소스와 보고3/3의 Host key 송신·picker 유지·present0이 일치한다. 이는 로그인 전 target 선택 transaction 결함이며 UAC를 잠금으로 잘못 판정한 문제라고 바꾸어 부르지 않는다.

10-22 select→stream-on/window-select 송신 없음은 이 기제와 일치하는 정황이다. 당시 Host 영상0도 별도로 있었으므로 유일 원인으로 확정하지 않는다. APK REQUESTING timeout도 shared RequestDesktopMode→RequestWindowSelect(0)에서 같은 종류일 가능성이 있어 아래 범위로 확인한다.

## 구현 계약

1. 현재 연결/run에서 role이 확인됐고 **kCaptureFlagPreloginV1 + desktop(windowId0)**인 경우에만 로컬 선택을 사용한다. secureDesktopActive/UAC 또는 LockCredV1만으로 발동하지 않는다. HostAtLogonScreen은 LockCred도 포함하므로 이 분기에 그대로 쓰지 않는다. 일반 Host의 기존 window/monitor ack 계약은 유지한다.
2. worker가 이미 서비스하는 로그인 desktop 하나를 StreamState 활성화·필요 시 허용된 keyframe 요청으로 보기 시작한다. Host의 WindowList/WindowSelect/thumbnail allowlist를 확대하거나 credential/input guard를 완화하지 않는다. 가짜 Host WindowSelected ack를 만들지 않는다.
3. **로컬 pending·operation epoch·첫 프레임 gate는 유지하고 ack만 불필요한 mode를 명시**한다. decoder/FrameGate는 새 선택의 IDR 대기로 시작하고, 현재 연결의 정상 IDR/reference sync·디코드 출력에서 실제 확인한 stream generation을 그 transaction에 결합한다. 같은 operation/연결·role일 때 UI thread가 reveal을 commit한다. Begin을 생략하고 클릭 즉시 picker를 숨기거나 expectedGeneration0을 모든 generation 영구 통과로 남기는 수정은 불가하다. 첫 generation 확정 뒤 straggler 필터를 유지한다. 유효 raw source를 새 IDR로 재인코딩한 bootstrap은 인정하며 synthetic flag만으로 무조건 거부하지 않는다.
4. decoded candidate→UI reveal→renderer 성공→restore 보고의 구분을 유지한다. 첫 그림이 없으면 대기/기존 bounded 실패·재시도 안내를 유지하고 선택 성공을 먼저 보고하지 않는다. P-frame·선택 전 buffered decode 결과만으로 완료하지 않는다. MF 지연 출력에서 현재 입력 header를 출력 그림의 provenance로 오인하지 않는다.
5. **첫 pong 전 클릭도 동일 intent로 유예 후 role이 알려지면 한 번 실행**한다(동등한 bounded role gate 허용). Prelogin이면 위 경로, 일반 Host면 기존 ack 선택이다. 이미 queued desktop WindowSelect가 role 확인 뒤 버려지는 경우도 그 intent/op만 원자적으로 전환하거나 명시적으로 취소·복구한다. silent discard·영구 awaitingAck·unrelated pending Clear는 불가하다.
6. cancel/new pick/disconnect/reconnect/role 변경·옛 callback/WindowSelected/reveal이 새 선택을 완료하지 못하게 한다. known Prelogin의 windowId≠0은 보내지 않고 명시 거부·상태 해소한다. 반복 desktop 선택·picker 재진입·idle 재부착·재접속도 검증한다. capture source fence/재시작 상한과 DW의 실제 presented만 복원 보고하는 계약은 보존한다.

## (b)를 채택하지 않는 이유

현재 ControlMonitorSelect는 main mailbox에 post한 직후 MonitorList를 답한다. **MonitorList에는 streamGeneration이나 적용 완료 판정이 없다.** 기존 gate의 target ack 대용이 될 수 없다. 일반 monitor 적용은 WGC capture item 생성도 포함하므로 DXGI 전용 prelogin을 그대로 통과시킬 근거가 없다. modern monitor transaction은 window-select namespace와 WindowList capability를 사용하나 WindowList 자체도 여기서는 금지돼 있다. R1에서 새 monitor protocol·권한 확대까지 추가하지 않는다.

## shared session·APK 경계

PC 우선 수정은 진행한다. shared RequestDesktopMode/RequestWindowSelect(0)도 같은 ack 대기를 좁게 확인하고 공통 정책을 맞춘다. Android selectionTag/incarnation·active decoder1·park inhibit는 유지한다. 필요하면 별도 local mode/event를 전달하며 host ack를 위조하거나 다른 pending tag로 소비하지 않는다. APK UI가 REQUESTING에서 실제 영상 ready로 넘어가는지 확인해야 한다.

APK0.2.32의 기존 SH r2·UA-2 정본에 이 확인/필요 보수를 취합한다. 검증용이 기존 작업용2 창구로 연결하고 단일 native writer를 지킨다. **PC0.2.163을 미착수 APK 때문에 세우지 않는다.** PC/shared 담당 파일은 작업용1이 쓰고 APK UI/JNI가 필요하면 작업용2가 계약을 따른다. APK0.2.31 게시본까지 고쳐졌다고 보고하지 않는다. 휴대폰 sealed 로그인 입력 신규 구현은 범위 밖이다.

## 최소 완료 기준·새 후보

- 수정 전 E1 3/3 FAIL과 로그를 보존한다. 수정 후 실제 제품 WndProc+renderer에서 picker10초→실제 layout/hit-test desktop 클릭→picker 종료·present 성공·구별 가능한 화면 캡처·첫 표시≤1초·500ms 최소 갱신을 확인한다. 시험에서 StreamState(true)만 직접 호출해 선택을 우회하지 않는다.
- role 확인 전/후 클릭, 반복 보기/idle 재부착/재접속, 옛 operation/reveal·잘못된 generation/P-frame의 완료 거부를 포함한다. 일반 Host의 ack 기반 desktop/window/monitor와 UAC/LockCred가 로컬 mode로 잘못 들어가지 않는 대조도 둔다. allowlist/input gate의 기존 경계는 유지한다.
- UA E1 같은 연결2회 왕복/실제 render는 독립 계속한다. 기존 LK/capture SOURCE OK는 유지하고 새 제품 diff만 SOURCE 재검토한다. 새 고정 후보에서 E1·통합 영향 검사는 검증용이 독립 수행한다.
- 새 product commit→재빌드·재 dry-run→새 payload/manifest/SUMS/hash·gate/define·관련 smoke/독립 E1로 고정한다. 미게시이므로 **버전0.2.163 유지 가능**하며 임의로164로 넘길 필요 없다. 기존90192f4/5ea1ac74 산출물은 보존하되 새 후보 근거로 재사용하지 않는다. EVIDENCE 판정 뒤 기존 승인대로 서명·gnlink 게시·외부 확인·main/docs 일반push/dist/FINAL로 잇는다. 기존 후보를 먼저 게시하지 않는다.

다음 담당은 검증용→기존 t-bpwzezfg의 작업용1이다. 이 결정을 단일 유효 정본에 취합해 PC 우선 보수·새 SOURCE diff/E1로 연결한다. APK는 같은 창구에서 기존 정본/담당을 갱신한다. 지금 사용자 조치는 없고 설치·실 UAC 승인·재부팅을 자동 수행하지 않는다.

Codex 산출물은 미추적 판정서·인계뿐이며 제품/공개 문서 편집·빌드·설치·commit/push 없음. 구현 history/계획/Git MCP 커밋은 기존 기능/릴리스 task에 취합한다.
