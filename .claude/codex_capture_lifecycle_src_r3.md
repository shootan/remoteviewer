# capture-lifecycle r3 — SOURCE 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 **t-l5o6hmjt** / 후보 **6ffd94bc7dca53e1874a9401bf336d8d7b7b5d5a** / diff **6fa709a..6ffd94b**.

**N1-b/N2-end/N3-host SOURCE OK, N1-a의 DesktopRebuild도 OK. FullFrame 실패의 무제한 retry 한 곳만 남아 최종 SOURCE는 NEEDS_CHANGES다.** 제품·시험 diff23파일과 r3 보고를 읽고 HEAD 일치·tracked clean·whitespace rc0를 확인했다. 저장된 최종/변이 로그의 관련 결과도 대조했으나 Codex의 새 빌드·실행 시험은 없다. 전체 EVIDENCE는 예정된 R1 통합 후보에 귀속한다.

## 닫은 지적

- **N1-a DesktopRebuild**: 실제 Detach 뒤 실패해 attachmentDesktopGen이 current가 되어도 restartPending+RestartOwner::DesktopRebuild로 회복을 이어간다. (desktop gen, control epoch)별3회/backoff이고 일반 tick은 그 owner를 넘겨받지 않는다. 기존 wrapper backoff 이전에 거절되는 호출을 attempt로 세지 않는다. 새 gen/세션은 별도 회복이다. 실제 실패 위치를 쓰는 수정 방향과 source를 수용한다.
- **N1-b**: GDI callback이 시작 시 attachment cookie를 값으로 캡처해 meta에 싣고, PublishFrame은0 cookie를 거부한다. invalidation이 bootstrapCacheMu 아래 continuity token을 올리며 submit된 frame이 그 값을 지닌다. 늦은 완료의 cache 채택은 같은 token을 요구한다. Unknown은 확정 edge가 아니므로 일반 raw 전달을 유지하되 캐시 재채택을 막는 범위를 수용한다. cookie가 바뀐 옛 frame은 raw/cache/episode 성공에 들어가지 않는다.
- **N2-end**: 요청과 종료에 epoch+Serve sequence를 결합해 ended Serve의 queued/late 요청을 Drop한다. 미래 owner는 Leave, 실제 다음 Serve는 자신의 새 sequence로 처리한다. 이전 r2의 End reset 직후 옛 pending 재적용 반례는 닫힌다. resume/pause·늦은 다른 epoch 종료의 구분은 유지한다.
- **N3-host**: BackendStatusBoard가 owner/packed/change count/handled key를 같은 mutex로 게시·복사하고 DecideBackendStatusAnswer가 그 snapshot으로만 판정한다. 다른 owner는 fields0/NotYetApplied다. r2의 old-packed/new-handled 조합이 제거됐다.32B wire·API·250ms·요청당1답·자발통지없음도 유지한다. testAfterRead의 제품 호출/런타임 설정이 없고 출하 기본 empty인지 기존 통합 감사에 포함한다.
- r2에서 닫은 client Query 분리/N4 source-key/PS-B는 유지한다. 아래 항목은 기존 **N1-a의 FullFrame retry 소유·상한** 잔여이며 이들을 다시 설계하라는 요청이 아니다.

## 유일한 잔여 — FullFrame 실패 retry도 같은 유한 budget을 차감해야 한다 (P1/P2)

첫 WGC full-frame restart는 `host_stage_pop_frame.cpp::ask_full_frame`에서 fullFrameAsks를1 올린 뒤 `host_stage_backend.cpp:277`의 **RestartOwner::FullFrame**으로 실행된다. 실제 Detach 뒤 실패하면 wrapper `host_loop_helpers.cpp:241~244`는 restartOwner=FullFrame/restartPending=true를 남긴다.

그 다음 `host_stage_time_limit.cpp:103~105`는 **DesktopRebuild만 제외**하므로 FullFrame을 계속 retry한다. 이 tick 경로에는 fullFrameAsks/episode key/남은 횟수 검사가 없고, wrapper의 restartFailures는 min(...,5)로 backoff만8초에 cap한다. 따라서 같은 FullFrame episode의 **첫 ask=1인 채 restart가4회·5회 이상 계속 실패**할 수 있다. ask_full_frame은 restartPending이면 조기 return하므로 더 이상2/3·3/3으로 소비되지도 않는다.

fullFrameOwnRestart를 retry에도 적용해 sourceBoundary가 늘지 않게 한 것은 맞지만, **같은 budget 이름을 유지하는 것과 그 budget에서 각 재시작을 실제 차감하는 것은 다르다.** r3의 nofirst는 DXGI acquire 없음/duplication 재생성의 상한이고, rebuild-fails는 DesktopRebuild owner다. 어느 쪽도 WGC **FullFrame owner의 실제 시작 실패**를 통과시키지 않는다.

최소 보완은 FullFrame owner의 최초 시도와 실패 retry를 **현재 episode의 동일 횟수·간격·source key**로 결산하는 것이다. 같은 episode에서 실제 WGC restart 시도는 정해진3회까지만, 소진 후 일반 tick/새 kick/posted restart flag로 추가 시작0이어야 한다. 새 유효 source에는 기존 N4대로 새 budget을 허용하고, 같은 실패 회복을 외부 boundary로 바꾸어 budget을 새로 열면 안 된다. 기존 General의 geometry/선택 회복을 전역3회로 제한하지 않는다. 새 capture/transport 구조나 wire 변경은 필요 없다.

이 반례 하나에 집중한다: **WGC full-frame 필요→최초 시도 Detach 뒤 실패→같은 episode 반복 실패**에서 최초+retry 합계≤3·4번째0(일반 wrapper backoff가 지난 뒤도0), real frame/cache 채택0. 별도 대조로 budget 소진 전 실패를 해제하면 같은 episode 안에서 회복하고, 새 유효 source 경계에서는 다시 유한 시도가 열리는지 확인한다. DesktopRebuild21·PS/UA/LK 전량을 이 보수마다 다시 반복할 필요는 없다.

## M9 및 실행 근거의 범위

pointerfirst가 열린 요청당1회로 포인터 전용 보고를 고정하고, 그 보고를 완료로 세지 않은 뒤 실제 이미지 acquire가 완료하게 만든 방향을 수용한다. 저장 로그를 직접 확인했다.

- `r3_final/e2e_psptr.txt`: pointer-only refusal→이후 delivered, ALL PASS34/0.
- `r3_mut/M9.txt`: refusal/이후 이미지 완료 두 조건 FAIL, RESULT FAILED34/2. r2 M9의 비결정 결과와 별개로 이번 **분기 부정 대조**가 추가됐다. log/요청 완료 경계의 대조이며 실제 UAC 픽셀/렌더 성공으로 확대하지 않는다.
- `r3_final/e2e_uafail.txt`: 같은gen1의 attempt1 실패→attempt2 성공, gen3의3회 실패→10초 추가 시도 없음→새gen4 성공, ALL PASS21/0. M12는6회 시도, M13은2회차 없음/그림 미복귀로 실패했다. **DesktopRebuild의** 상한/성공 결산 증거로 수용한다.
- M10 snapshot·M11 ended request·M14 continuity·M15 zero cookie의 FAIL 줄도 확인했다. r3 attachment [E]는 **submit 전후 invalidation의 캐시 거부**이고 raw는 전달한다. [F]는 cookie가 바뀐 completion의 raw/cache/episode 종료 거부다.

위 로그는 작업용 실행 산출물 열람이며 **Codex 또는 검증용 독립 PASS가 아니다**. PS32×2/nofirst26/UA54×2/LK68·79·28/단위 합계는 보고 인용이다. FrameBuffer/DecodingSink의 renderer 부재·MF 지연 출력과 AU header 대응 한계는 그대로다. SOURCE 닫힌 뒤 R1 고정 통합에서 실제 원본/IDR/수신·decode·render, 아직 첫 datagram이 나가지 않은 옛 AU fence, 출하 시험 define/표식·TestStill Off 및 시험 hook 제품 호출0을 독립 확인한다. 실 UAC/SYSTEM/Winlogon/정지 하드웨어·실폰은 별도 실기다.

## 다음 담당

검증용이 기존 **t-l5o6hmjt**에 **FullFrame retry 상한 한 곳만** 취합해 같은 작업용1에게 연결한다. 이번에 닫은 부분은 유지하고 다음 고정 diff도 이 범위로 확인한다. SH/UA-2 APK 및 R1 통합 준비는 계속 가능하다. KA는 R1 우선 순서를 유지한다.

Codex 변경은 미추적 판정서·인계뿐이며 제품/공개 문서 편집·빌드·설치·commit/push 없음. 구현 history/계획/Git MCP 커밋은 기존 기능 task에 취합한다. 지금 사용자 조치는 없다.
