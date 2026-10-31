# capture-lifecycle r1 — SOURCE 검토

2026-10-26 / 검증용 remote#yj8mrmrx / task **t-l5o6hmjt** / 최신 후보 **569059fbefda17671b8ff0a2c3695b35320f536d** / diff **8d8655e..569059f**.

범위 갱신 확인: 최초1679bde 위의569059f는 backend-owner 로그 이름, 해당 UA 시험 검색 문자열, history 설명만 변경한다(3파일8+/8−). 동작/분기 변경이 없어 **아래 N1~N4 NEEDS_CHANGES와 PS-B SOURCE OK는 최신569059f에도 동일하게 유효**하다. 추가 diff 전문·HEAD 일치·tracked clean을 확인했다. UA46/46 rc0 재실행은 작업용 인용이며 Codex 실행은 아니다.

**SOURCE: NEEDS_CHANGES. 아래 N1~N4를 기존 task의 한 보수 묶음으로 처리한다.** PS-B의 로컬 prelogin 판단은 OK이고 UA-1/2의 큰 방향·번호·요청/응답 방식은 수용한다. 그러나 경계 이전 영상 차단과 세션 소유/조회 처리에 계약을 충족하지 못하는 경로가 있다. 요청 diff 29파일, 직접 호출부·mailbox·encoder/sender fence, 정본 §1~§4, 작업용 보고를 읽었다. 후보 HEAD 일치·tracked clean·diff whitespace rc0 확인. Codex 빌드/행동 실행 없음. EVIDENCE는 합의한 R1 통합 독립 검사에 귀속하며 아래 반례를 그 입력으로 보강한다.

## N1 (P1) desktop generation이 source·raw/encoded 경계를 끝까지 막지 않는다

`host_capture_session.cpp:1050~1104`의 TrackInputDesktop은 확인된 변화 때 캐시와 desktopGeneration만 바꾼다. `host_stage_backend.cpp:198~229`는 그 뒤 재시작하고 **성공했을 때만** restarted_capture→ResetTimelineAnchors(inputEpoch 증가)/Flush를 한다. 따라서 edge를 확인했어도 WGC 재시작이 지연·실패하는 동안 기존 raw frame/encoder output/sender queue는 이전 inputEpoch로 여전히 유효하다. sender의 기존 fence는 inputEpoch를 보며 새 desktopGeneration은 모른다. 계약의 '이전 영상 무효화 → 재구성' 순서가 반대다. 실패 시에도 이전 화면/AU가 새 desktop 경계를 통과해서는 안 된다.

또한 PublishFrame `:1227~1250`은 **원본에 실린 desktop provenance**를 검증하지 않고 `meta.callbackUs >= desktopChangeDetectedUs`이면 현재 desktopName/generation을 붙인다. callback 시각은 소스 화면이 만들어진 시각/attachment 소유권의 증명이 아니다. WGC collector가 옛 pool frame을 늦게 꺼내면 그 callback은 edge 뒤일 수 있다. 재시작 전 틈에서 온 프레임을 현 generation으로 다시 이름 붙일 수 있다. 기존 attachment 검사는 `:1180`의 선행 1회 검사이며, 그 검사 뒤 detach/캐시 삭제→publish의 최종 캐시/큐 commit 순서도 막지 못한다. 캐시에 attachment identity가 남지 않아 이런 늦은 commit이 새 부착 뒤에도 살아남을 수 있다.

추가로 TrackInputDesktop은 Unknown이면 즉시 return, 첫 다른 이름도 debounce 때문에 그대로 return한다. 그 사이 연속성을 잃었다가 다시 Default로 돌아오면 원래 snapshot이 같은 이름/세대로 살아 있다. **Unknown을 잠금/확정 edge로 취급하지 않는 것**과 **출처 연속성을 확인 못한 snapshot을 폐기하는 것**은 별개다.

보완: source/attachment가 소유하는 provenance를 immutable meta로 전달하고 현재 provenance와 원자적으로 검증한 뒤 cache/raw queue에 채택한다. 경계에서 늦게 publish된 옛 원본에 현 gen을 붙이지 않는다. 확인된 edge의 media/input fence는 재시작 성공 전에 닫고, 실패해도 옛 raw/AU가 재전송·재채택되지 않게 한다. 의심/Unknown 동안 캐시 연속성은 보수적으로 무효화하되 restart debounce와 실제 lock 판정은 유지한다. 매 프레임마다 epoch를 올리거나 보안 화면에 Default 캐시를 허용하는 해결은 불가하다.

반례는 (a) 서로 다른 픽셀/원본 ID의 옛 frame을 callback/PublishFrame 채택 직전에 hold한 뒤 edge/detach를 넘겨 해제 (b) edge 확인 뒤 restart를 지연/실패시키고 이전 raw 및 encoded queue를 남김 (c) Default→Unknown/한 번의 다른 이름→Default 동안 새 source 없음. 옛 source가 cache·encode·wire로 나가면 실패하고, 새 attachment의 정상 원본/IDR은 통과해야 한다. PS-A/UA-1 같은 provenance 계약으로 한 번에 보수한다.

## N2 (P1) UA-2 owner 갱신과 request 소비가 서로 다른 epoch를 사용할 수 있다

`host_stage_backend.cpp:179~195`는 한 번 읽은 curEpoch로 owner를 바꾼다. `:244`의 TakeBackendRequest와 `:258`의 AppliedMark는 epoch를 다시 읽으며, 채택한 `backendReq->epoch`와 ownerEpoch를 묶지 않는다.

확정 가능한 순서: main이 epoch A의 owner 확인을 끝냄 → control이 새 B를 채택하고 B의 WGC 요청을 post → 같은 tick의 TakeBackendRequest는 B를 읽어 정상 소비·적용 → 다음 tick ownerEpoch는 아직 A라 B의 WGC를 startup 기본으로 초기화. **새 세션의 유효한 선택을 자기 초기화가 지운다.** 반대로 A의 request를 Take한 뒤 B로 바뀌면 오래된 request가 backend를 건드리거나 AppliedMark가 B의 key로 표시할 틈도 있다. 기존 mailbox의 소비 시 epoch 필터 하나로 처리 전체의 소유가 보장되지 않는다.

또한 실제 Serve 종료는 `host_control_session.cpp:2171~2174`에서 stream을 비활성화할 뿐 epoch/새 owner 종료 표식을 만들지 않으며, 신규 코드도 EndSession에 귀속된 reset은 없다. override는 다음 Hello 때까지 남는다. 이 상태에서 backend 명령은 `restartNeeded = desktopActive && ...`(`:268`)만 보므로 stream inactive/idleDetached 상태에도 GPU 재시작을 할 수 있다. 정본의 '실제 세션 종료 때 metadata 복귀, inactive에서는 캡처 작업 0'을 충족하지 않는다.

보완: 현재 owner의 시작/실제 종료·pending request 무효화를 하나의 main-owner 계약으로 직렬화하고 request에 든 epoch를 끝까지 사용한다. 현재 owner의 초기화를 마친 뒤 그 owner의 request만 채택/결과 게시한다. 늦은 이전 종료/적용 완료는 새 owner의 선택·status를 지우지 못한다. 실제 End와 같은 세션 resume/pause는 구분하고, inactive에는 요청 metadata만 저장/초기화하여 다음 active attach에 적용한다. R2 admission 설계 전체를 끌어올 필요는 없다.

반례: 위 A 확인→B post→B apply 경계, A take→B 채택 경계, 늦은 A end/completion, 동일 B resume/pause, 실제 B end 후 새 세션 전 metadata, idleDetached 중 backend 요청(캡처 start/restart0). quarantine와 로컬 prelogin DXGI는 유지한다.

## N3 (P2) 조회가 미전송 변경 요청을 없애며 pending 표시도 잃는다

`native_video_client_shared_core.cpp:351~374`의 Request와 새 Query는 **같은 backend_ / pending_**를 쓴다. Request(2) 뒤 control worker가 Consume하기 전에 Query()를 하면 backend_=0으로 바뀌어 **조회만 전송하고 WGC 변경을 잃는다.** 변경 요청과 '현재 상태 재조회'는 서로 대체할 수 있는 값이 아니다. APK가 설정 표시/적용 대기 polling을 함께 쓰므로 공개한 API만으로 일어나는 경로다.

호스트 쪽도 변경 요청이250ms 안에 적용되지 않은 뒤 Query(backend0)를 받으면 `host_control_session.cpp:1924`에서 applied=true로 시작하고 대기 검사를 건너뛰어 **NotYetApplied를 제거한 옛 packed 상태**를 보낸다. packed에는 owner/request pending 정보가 없으므로 이후 조회가 같은 변경의 완료 여부를 정확히 말하지 못한다. 시작 직후 epoch reset을 main이 아직 하지 않은 조회도 옛 owner의 packed 상태를 새 세션의 '이번 연결' 응답으로 받을 수 있다(N2와 함께 해결).

보완: 변경 pending과 조회 pending을 분리/일관되게 직렬화하고, 조회가 변경을 덮거나 먼저 완료된 것처럼 보이지 않게 한다. 새 wire 번호를 더 늘릴 필요 없이 현재 seq/flags와 owner에 맞는 pending/snapshot 의미를 정의한다. 구 host에는0/상태 응답을 보내지 않는 capability gate, 답은 요청당1개·최대250ms·자발 통지 없음은 유지한다.

반례: Request→Query/Query→Request 둘 다 최종 변경 전달, 적용을250ms 넘겨 hold한 뒤 Query는 pending 유지·완료 뒤 해제, 새 owner의 첫 Query에 이전 owner 선택 미노출, resume에서 상태/선택 보존. 설정 UI는 requested/effective/active/pending을 구분하며 radio만 보고 적용 성공이라 하지 않는다.

## N4 (P2) full-frame 재획득 budget이 원본 경계와 연결되지 않는다

`host_stage_pop_frame.cpp:98~120`은 fullFrameEpisodeOpen이 false일 때만 asks를0으로 돌리며, 전체 제품에서 false를 쓰는 곳은 PublishFrame `:1221` 하나다. 캐시 무효화/desktop 변화/새 target·session/외부 detach에는 episode 갱신이 없다. 한 source에서3회 실패해 아무 프레임도 publish되지 않았다면, 이후 새로운 source/attach에서 원본이 또 없을 때도 이전 asks=3 때문에 새 유한 재획득을 시작하지 못한다. 반대로 PublishFrame은 cache provenance가 거부된 경우에도 episode를 닫아 옛/부적격 completion으로 budget을 다시 열 수 있다.

보완: episode를 유효한 source/owner 경계와 명시적으로 묶는다. 새 유효 source 경계에는 새 유한 budget, 같은 episode 안의 내부 WGC 재시작/duplication 재획득에는 기존 budget을 유지하고, **현재 provenance의 유효 원본**만 성공으로 닫는다. 매 kick/poll/내부 재시작마다 budget을 리셋하는 무한 루프는 금지한다. UA-1 재시도도 새 desktop generation과 진행 중 시도의 소유를 함께 관리하여 이전 gen의 시도/완료를 새 gen에 결산하지 않는다.

반례: source A의 budget 소진→source B/새 부착에서 프레임 없음→B의 유한 재획득 가능, A의 늦은 부적격 publish로 B budget 재무장 불가, 같은 source 무응답에서 횟수 상한/간격 유지. DXGI host ask3회와 library request당 duplication3회는 **서로 다른 상한**이므로 총 횟수도 구분해 기록한다(현 구현은 에피소드 전체 duplication3회를 보장하지 않음).

## 수용한 부분·EVIDENCE 귀속

- **PS-B SOURCE OK**: `host_stage_stream_active.cpp:144`는 로컬 preloginServingDesktop을 확인해 일반 secure→WGC 분기를 제외한다. requiredInputDesktop·PreloginStopServing의 실제 실패 경계는 유지한다. 원격 flag로 prelogin 권한을 얻는 변경은 없다.
- 기존 앱 소유 shared BGRA payload를 최신1개 보관하는 방향, 첫 AU 선행조건 제거, 기본1000→500ms 변경, full-frame request의 유한 상한·유예 방향을 수용한다. NV12 경로는 CPU payload가 함께 만들어지는 현재 readback 호출부를 확인했으며 ring surface만 캐시에 붙이는 새 변경은 없다. N1/N4를 해결해야 원본 보장 전체 SOURCE OK다.
- UA-2 새32B 메시지90와 pong0x100000, req34 flags0x1의 명시적 요청/응답, 구 peer capability gate, 자발 통지 없음은 수용한다. 최종 compile/바이트 gate는 새 STREAM_TEST_DESKTOP_ALIAS/STILL_SCREEN/SECURE_DESKTOP_EVENT까지 포함한다.
- 작업용 PS30/UA46, LK68/79/28, 회귀·M1~M4는 **작업용 인용**이다. 다른 LK base 위 PS/UA 실행도 최신 고정569059f 독립 결과로 바꾸지 않는다. 대역·실 UAC 픽셀 아님·duplication 재생성 미실행 한계를 보고한 것은 적절하다. 수정 전 동등 seam mutant 비교를 source 변경의 부정 대조로 수용하되 기준 출하 바이너리 재현이라고 부르지 않는다.
- 현재 PS presentThread는 창 렌더링 없이 FrameBuffer를 읽고 presented 필드를 직접 쓴다. **디코드/FrameBuffer 도착 증거**이고 실제 presented≤1초 증거는 아니다. UA의 KeyCapturedAfter도 synthetic kick에 새 captureQpcUs를 붙이는 경로 때문에 '그 이후 원본 픽셀 캡처'를 홀로 증명하지 못한다. R1 EVIDENCE에서는 원본 ID/색·attachment/provenance와 새 IDR을 연결하고 실제 렌더 출력까지 확인한다. 이 정정으로 실제 UAC/SYSTEM 실기를 자동 성공으로 확대하지 않는다.
- duplication TIMEOUT→실제 recreate 분기는 현재 STILL_SCREEN seam으로 실행되지 않는다. 통합 자동 검사에는 해당 라이브러리 분기의 bounded 재생성/성공·실패·종료를 관찰할 수 있는 제한된 test-only 대조를 포함한다. 실제 정지 화면 하드웨어/UAC 승인만 사용자 실기로 구분하며, 자동 가능한 분기를 단순 '회사 PC 대기'로 넘기지 않는다.

## 다음 담당

검증용이 기존 **t-l5o6hmjt**에 N1~N4를 취합해 같은 담당 작업용1로 보수하고 새 고정 diff를 전달한다. PS-B/번호·메시지 구조/LK r4 SOURCE OK를 다시 열 필요는 없다. 독립 EVIDENCE는 정해진 R1 통합 후보에서 수행하며 이번 요청 때문에 매 커밋 전량 재검사를 만들지 않는다. SH/UA-2 APK의 독립 작업은 계속 가능하되 위 shared API 수정과 통합한다.

이번 Codex 변경은 미추적 판정서·인계뿐이다. 제품/공개 문서 편집·빌드·설치·서비스 조작·게시·commit/push 없음. 작업용이 기존 기능 history와 계획 상태에 소스 보수/남은 자동·실기를 취합하고 Git MCP 커밋으로 고정한다. 현재 사용자 조치는 없다.
