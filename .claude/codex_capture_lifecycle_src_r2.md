# capture-lifecycle r2 — SOURCE 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 **t-l5o6hmjt** / 후보 **6fa709a65a8a940d0b045dc615925ba442ac7872** / diff **569059f..6fa709a**.

**SOURCE NEEDS_CHANGES. N4와 N3의 client Query 덮어쓰기는 닫는다. N1/N2/N3-host의 아래 잔여만 한 묶음으로 보수한다.** 제품·시험 diff19파일과 r2 보고, 직접 호출부를 읽고 HEAD 일치/tracked clean/whitespace rc0를 확인했다. 작업용 저장 로그 일부를 직접 대조했지만 Codex의 새 빌드/행동 시험은 없다. 독립 EVIDENCE는 합의한 R1 통합 후보에 귀속한다.

## 닫은 부분

- **N4 SOURCE OK**: full-frame episode key에 desktop/stream/session/source boundary를 넣고 자체 WGC 재획득은 외부 source 변경과 구별한다. 새 source에서 budget이 열리며 일반 provenance 거부 뒤 episode 종료도 하지 않는다. host ask3×library duplication3의 상한을 구분했다. 아래 N1의 provenance 잔여를 제외하면 이 budget 설계의 추가 보수는 없다.
- **N3 client SOURCE OK**: Query가 backend_를0으로 덮지 않고 queryPending을 따로 사용한다. 변경을 먼저 소비하고 그 답으로 조회를 합류시킨다. Query가 먼저 나간 경우 후속 Request도 보존한다. API 형태와 구 host 조회 금지는 유지됐다.
- **N2 일부 OK**: tick당 epoch를 고정하고 새 epoch의 mailbox 요청은 남겨 다음 owner 초기화 뒤 처리한다. AppliedMark도 request 자신의 epoch를 사용하므로 이전의 'B 선택→다음 tick B reset으로 삭제' 반례는 닫힌다. inactive/idleDetached의 backend 요청은 metadata 저장만 하도록 한 조건도 수용한다.
- **N1 일부 OK**: 확인된 desktop edge의 inputEpoch fence/raw flush/새 key 요청이 재시작보다 앞선다. WGC/DXGI의 nonzero attachment cookie 재검사와 캐시의 cookie 추가로 callback 시각에 현 desktop gen을 붙이던 경로를 보완했다. PS-B의 로컬 prelogin DXGI SOURCE OK도 유지한다.

## N1-a (P1) 실제 재시작 실패는 desktop별3회 상한을 우회한다

`host_stage_backend.cpp:232`의 재구성 조건은 **attachmentDesktopGen != desktopGeneration**이다. 그런데 실제 `RestartCaptureSessionImpl`은 시작하자마자 DetachCaptureSession을 호출하고, Detach `host_capture_session.cpp:474`는 성공 여부와 무관하게 **attachmentDesktopGen을 현재 desktopGeneration으로 갱신**한다.

따라서 실제 WGC item/pool/collector 생성 등이 그 뒤 실패하면 sessionReady=false인데 두 gen은 같아진다. 다음 tick의 stale=false로 **새 desktop 전용3회 카운터에서는 빠진다.** 대신 기존 wrapper `host_loop_helpers.cpp:236~244`가 restartPending=true를 세우고, `host_stage_time_limit.cpp:100~106`의 일반 retry가 계속 재시작한다. restartFailures는5에서 cap될 뿐 횟수 제한이 아니며 이후에도8초 backoff로 계속 시도한다. 즉 재시도 자체가 사라지는 것이 아니라 **이번에 약속한 gen별 상한을 다른 경로가 우회**한다. 이전 failure의 소유/성공 결산도 desktop 전용 카운터와 달라진다. full-frame 자체 WGC 재획득 실패도 일반 retry로 넘어가면 fullFrameOwnRestart=false로 Detach해 자체 회복을 외부 source boundary처럼 셀 수 있다.

현재 `--rebuild-fails`의 `ok = !seamFail && restart_capture_session(hx)`(`:254`)는 함수를 호출하지 않으므로 Detach와 restartPending의 이 부작용이 없다. 시험18 PASS는 **옛 attachment가 그대로 있는 실패**의 fence/3회 상한을 검증한 것이지, 실제 detach 이후 실패의 재시도 상한 증거가 아니다.

보완: 준비 중/실패한 attachment와 **성공해 사용 가능한 attachment의 provenance**를 구별하거나 pending rebuild 상태를 별도로 유지하고, desktop/full-frame 회복의 retry 소유와 budget을 wrapper·tick까지 잇는다. 실패한 시작이 '현재 gen 재구성 완료'를 뜻해서는 안 된다. 현재 gen의 실제 부착 성공 때만 완료 처리, 같은 gen은 기존3회/backoff, 새 gen은 별도 budget을 유지한다. **기존 일반 geometry/선택 회복을 전역으로3회 중단시키라는 요청은 아니다.** 기존 `GNLINK_STREAM_TEST_FAIL_RESTART_FILE`처럼 **Detach 뒤** 실패시키는 경로로 첫 실패→파일/실패 해제→같은 desktop gen에서2번째 시도 성공 및 계속 실패3회→일반retry에서도4회0을 구분한다. 자체 full-frame 실패 후 일반 retry도 episode budget을 다시 열지 않아야 한다.

## N1-b (P1/P2) GDI와 suspect 뒤 늦은 프레임의 출처 예외

GDI callback(`host_capture_session.cpp:835~863`)은 여전히 `meta.attachmentCookie=0` 기본값으로 PublishFrame을 호출한다. 새 provenanceCurrent(`:1247`)는0을 허용하며 live attachmentDesktopGen만 읽는다. edge 뒤 Detach가 gen을 현재로 바꾼 뒤 옛 GDI 자식을 Stop하기 전 들어온 옛 callback은 **cookie 검사 면제 + 현재 gen**으로 cache/raw queue에 채택될 수 있다. cache에는 다시 live cookie를 붙이므로 오래된 프레임이 새 attachment의 소유가 된다. 'backend 무관 같은 generation 계약'은 이 예외로 깨진다.

또 Unknown/첫 다른 이름 때 cache를 삭제하고 suspect 동안 cache 금지한 것은 맞지만, 같은 Default가 다시 관찰되면 suspect만false가 되고 원본 연속성 토큰은 바뀌지 않는다. Unknown 전에 submit되어 지연된 readback이 그 뒤 도착하면 같은 cookie/gen이라 다시 cache에 들어간다. 의심 구간의 cache 삭제가 그 이전 완료의 재채택까지 무효화하지 못한다.

보완: GDI에도 시작 때 값으로 캡처한 nonzero attachment identity를 meta에 전달해 0-cookie 우회를 제품 입력에서 없앤다. Unknown/후보 구간의 캐시 연속성 무효화도 프레임에 귀속된 token/시각 등으로 늦은 원본 재채택을 막는다. Unknown 자체를 확정 lock/desktop edge로 만들거나 매 poll 전체 캡처를 재시작할 필요는 없다. 이미 보완한 WGC/DXGI cookie 계약을 다시 설계할 요청은 아니다.

반례: 옛 GDI publish를 hold→edge/detach→해제하여 cache/raw/episode 성공0, 정상 새 GDI frame은 통과. 옛 readback을 hold→Unknown/다른 이름1회→Default 재확인→해제하여 캐시 재채택0, 그 후 확인된 새 원본은 허용. cache뿐 아니라 raw 채택의 소유도 확인한다.

## N2 잔여 (P1/P2) 실제 End 뒤 queued request가 기본 복귀를 다시 덮는다

StepBackendOwner가 ownerEnded를 보고 startup 기본으로 되돌린 뒤, 같은 tick의 TakeBackendRequestForOwner는 **같은 epoch이면 Handle**한다(`host_backend_owner.hpp:46`). 종료 전에 post했지만 아직 소비하지 않은 WGC 요청이 남아 있으면 **End reset→옛 요청 적용→WGC 저장** 순서가 된다. 다음 tick에는 endHandledEpoch 때문에 reset하지 않아 종료된 세션의 override가 다시 남는다. 다음 새 세션이 초기화하더라도 '실제 End에서 override/pending 무효화'는 아직 닫히지 않는다.

보완: 종료된 owner에 속한 pending과 이후 늦게 post된 명령은 Drop하고, 같은 epoch 번호만으로 종료된 owner를 다시 live로 취급하지 않는다. 새 owner 요청은 남기고 resume/pause는 실제 종료가 아니도록 기존 구분을 유지한다. 단위는 **A post→A end→main consume** 및 **A end 반영→늦은 A post**에서 startup 유지·재시작0, 다음 B의 정상 요청 보존을 넣는다. 현재 owner 단위는 end 뒤 requested를 시험에서 수동으로 WGC로 돌리고 유지됨을 확인할 뿐 이 pending 반례를 구분하지 않는다.

## N3-host 잔여 (P2) status/handled key를 한 snapshot으로 읽지 않는다

publisher는 packed, statusEpoch, appliedKey를 각각 따로 쓴다(`host_stage_backend.cpp:106~110,:291~292`). control reader(`host_control_session.cpp:1936~1944`)는 **packed를 먼저 읽고 handled를 나중에 읽는다**. epoch를 앞뒤 확인해도 같은 세션 내 변경 동안 epoch는 불변이므로 다음 순서가 가능하다.

1. reader가 이전 DXGI packed를 읽음.
2. main이 WGC packed/status와 해당 request의 appliedKey를 게시.
3. reader가 새 handled key를 읽어 changeDone=true, 같은 epoch 앞뒤도 일치.
4. **NotYetApplied 없이 이전 DXGI 상태**를 응답.

새 handled의 acquire가 앞에서 이미 읽은 packed를 새 값으로 바꾸지는 않는다. owner 교체 중에도 packed가 먼저 바뀌고 epoch는 나중에 바뀌므로 epoch 두 번 읽기만으로 '한 publish'가 되지 않는다. 250ms 뒤 아직 이 owner의 status가 없는 경우에도 현재 코드는 이전 owner의 packed 필드를 그대로 응답한다.

보완: owner epoch·packed·handled key·change count를 coherent snapshot으로 읽는다(짧은 mutex 또는 적절한 publication sequence 등). query의 posted pending도 그 snapshot에 대해 판단한다. timeout 때 해당 owner의 snapshot이 없으면0/확인불가+NotYetApplied 등 명시된 의미를 사용하고 이전 owner의 선택을 현재 세션 값처럼 넘기지 않는다.32B wire/API 변경이 필수인 지적은 아니다. request reader를 packed load 뒤 hold해 실제 publish를 넘기는 반례에서 올바른 값 또는 pending만 허용하고, false-applied/이전 owner 값이면 실패하도록 한다. 250ms·요청당1답·자발통지없음은 유지한다.

## M9 비결정 처리와 시험 근거

**M9를 비결정으로 남긴 처리 자체는 타당하다.** 저장 원문을 직접 대조했다: `r2_mut/M9.txt`는 ALL PASS32/0이며, `r2_d/e2e_ps1.txt`와 ps2는 각각 luma 실패3개(A/B/B2 spread0~1), `delivered ... content=0`, RESULT FAILED32/3을 포함한다. 따라서 수정 전 blank **6/6 관찰**은 존재하지만 **M9가 반례를 재현해 실패했다**고 쓸 수 없다. 성공할 때까지/실패할 때까지 반복해서 확정 대조로 바꾸지 않는다.

LastPresentTime!=0일 때만 요청을 완료시키는 새 소스 guard 방향은 수용한다. 다만 r2_d는 관찰 근거이며 모든 경로의 원인/효과 확정 대조는 아니다. 사용자 마우스/우연한 첫 acquire 대신, test-only로 **pending full-frame 요청→포인터 전용 보고(완료0/내용 프레임 승격0)→내용 보고(완료1)** 또는 timeout/recreate를 고정해 같은 guard의 mutation이 확실히 검출되는 제한 대조를 통합 EVIDENCE에 넣는다. 가능한 한 이 분기만 겨냥하고 전체 PS/UA를 무작정 재반복하지 않는다.

새 Still/StillNoFirstFrame은 라이브러리의 실제 duplication 재생성 코드를 지나도록 개선됐다. nofirst의9회(ask3×lib3)·중단·새 source budget과 M8은 적절한 방향이다. config.testStill은 라이브러리에 있지만 제품 기본Off이고 env 배선은 STREAM_TEST_SEAM 안인 점을 확인했다. 통합에서 출하 경로가 이 필드를 켜는 호출이 없고 새 env 표식/시험 define이 빠졌음을 함께 감사한다.

PS32×2/nofirst26/UA54×2/rebuild-fails18/LK68·79·28/단위·M5~M8은 작업용 실행 인용이다. 원문 일부의 PASS/FAIL을 읽은 것은 Codex 독립 실행이 아니다. **M5는 provenance raw 거부의 대조이며 inputEpoch fence 자체의 독립 대조가 아니다.** fence 로그 순서는 source 순서와 일치하나, 실제 sender queue의 아직 송신하지 않은 옛 AU를 gate에서 버리는 것은 통합에서 별도 확인한다. 첫 datagram이 fence 전 나간 AU가 기존 P11에 따라 끝나는 것은 이미 승인된 sender 계약의 한계로 구분한다.

PS/PC UA의 present thread는 여전히 FrameBuffer 계수이지 실제 renderer presented가 아니다. shared-session DecodingSink도 decoder에 현재 넣은 AU의 header를 그 호출의 출력 그림에 붙이므로, MF가 지연 출력하면 key/capture/synthetic이 그 그림의 provenance와 다를 수 있다. host key 줄과 이후 그림 계수만으로 특정 새 원본이 실제 표시됐다고 확대하지 않는다. 통합 EVIDENCE에서 원본 marker/출력 timestamp 매핑·실제 렌더를 연결한다. 실 UAC 픽셀·SYSTEM/Winlogon·실 정지 하드웨어/실폰은 계속 별도 미검증이다.

## 다음 담당

검증용이 기존 **t-l5o6hmjt**에 N1-a/b·N2-end·N3-host 잔여를 한 묶음으로 연결한다. N4/client Query/PS-B를 다시 열지 않는다. SOURCE 고정 뒤 독립 행동·출하 gate는 예정된 R1 통합 후보에서 취합한다. SH/UA-2 APK의 분리 준비는 계속 가능하며 status snapshot 의미만 맞춘다. KA는 R1 우선 순서 그대로다.

Codex 변경은 미추적 판정서·인계만이며 제품/공개 문서 수정·빌드·설치·commit/push 없음. 구현 기록/계획/Git MCP 커밋은 같은 기능 task에 취합한다. 지금 사용자 조치는 없다.
