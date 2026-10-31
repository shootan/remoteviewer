# 화면 안정화 후속 수정 계획 — P10 · P9 · P11 (A2A task t-xox45oo5)

기준: HEAD a18708c, 제품 0.2.100, 작업트리 깨끗, 콘솔 세션 Active(RDP 없음). 실행 순서 P10 → P9 → P11, 단계별 scoped commit(Git MCP), push 보류, 설치본은 마지막 1회.

---

## 0. 독립 확인 결과 (코드·로그 근거)

### P10 로그 업로드 401
- `log_upload.cpp:155-160` `log_upload_start` 는 **이미 실행 중이면 새 config 를 버리고 true 반환**. 헤더(`s.headers`)는 `:192` 에서 1회만 조립(`build_headers`), 갱신 API 없음. `log_upload_stop/running` 호출처 0.
- 호스트: `host_app_main.cpp:1335-1336` 재로그인 결과에서 `g.cache.hostToken = 새 토큰` 직후 `start_log_upload()` → no-op. `sign_out()`(`:896-905`)은 업로더를 세우지 않아 폐기 토큰으로 계속 전송.
- 서버: `server.js:657` 재등록 시 이전 해시 즉시 삭제(유예 없음) → 재등록 직후부터 영구 401. `sessions`(`:490`)는 in-memory 라 재시작 시 클라 Bearer 전부 소멸(`restart_test.js` 는 host token 만 검증).
- 클라 셸: `client_shell_main.cpp:329-336` 로그인마다 `log_upload_start` → 두 번째 로그인부터 no-op(15:09 회복은 프로세스 재시작 때문). 비밀번호는 저장하지 않음(`:94`, `:157`) → **자동 재로그인 불가**, 401 은 "재인증 필요" 상태로만 처리 가능. 뷰어 프로세스는 업로더를 안 씀(셸이 stdout 릴레이).
- `send_batch`(`:88-101`) 는 실패를 세기만 하고 배치는 이미 큐에서 빠진 뒤라 영구 유실. 토큰은 어디에도 출력 안 됨(유지).
- 실측: `log_upload.diag` 13:48:39 첫 401 → 오늘 15:55 까지 `failedBatches=2395` 계속(호스트 프로세스 그대로).

### P9 UAC 좌표
- `host_startup_graphics.cpp:218/234` 가 유일한 `SetTargetRect` 호출(전 저장소). 캡처 재시작 17곳(`restart_capture_session` 경유)과 `RestartCaptureSessionImpl`(`host_capture_session.cpp:422` 에서 monitorInfo 갱신)은 브로커에 알리지 않음.
- 매핑 수학(`secure_input_mapping.hpp:42-60`)은 정상. rect 는 매 `SecureInputMessage` 에 실려 감(프로토콜 변경 불필요). 에이전트(`secure_input_service_main.cpp:887-907`)는 rect 가 0 일 때만 virtual screen 폴백 → stale 비영 rect 가 이김.
- secure 경로는 **데스크톱 모드에서만** 사용(`host_control_session.cpp:433-436` 창모드는 `secureSkipWindowMode`). 따라서 필요한 rect = "지금 실제로 캡처 중인 모니터의 물리 rect+원점". DXGI 는 `capture.monitorInfo->monitor`(주모니터), WGC 재시작은 `CreateItemForPrimaryMonitor(nullptr)`(주모니터), GDI 워커도 주모니터(`gdi_capture_worker_main.cpp:97`). 모니터 선택(`host_stage_selection.cpp:108`)만 `target.handle` 로 특정 모니터를 열지만 **다음 재시작에서 주모니터로 되돌아감**(별도 결함, 아래 ledger).
- 일반 데스크톱 경로(`host_input_inject.cpp:254-271`)는 매 이벤트 live 주모니터 rect 를 읽어 정상 → "UAC 만 어긋남" 과 부합.
- 진단 예산 `diag_inject_landing`(`:974-987`) `static atomic<int> remaining{12}` 는 에이전트 프로세스당 1회성. 에이전트는 desktop 이름이 바뀔 때만 재생성(`:286-313`)이라 같은 프로세스가 여러 UAC 를 처리 → 15:15 기록 없음.

### P11 UAC 복귀 혼잡 (원인 후보 → 근거로 좁힘)
- 와이어 `captureQpcUs` 는 이 호출 입력의 stamp 가 아니라 **인코더 accepted-input FIFO 의 front**(`mf_h264_codec.cpp:1857-1873`, `host_stage_encode_send_h264_au.cpp:143/247`). 비동기 MFT 는 입력 N 을 넣을 때 N-1 의 AU 를 내놓으므로, AU 의 `sendQpcUs−captureQpcUs` = **입력 간 간격**(stamp 는 진실, 픽셀도 N-1 의 것).
- 일반 데스크톱에서는 trailing kick(150ms, `host_kick.hpp:51`)이 이 보유를 ≤150ms 로 묶는다(호스트 로그 15:15:53.605 seq 24408: `auCaptureUs` 가 `encodeInputUs` 보다 250ms 오래됨 = 그 구간 입력 간격).
- **secure desktop 에서는 kick 이 차단**: `KickTryFill`(`host_capture_session.cpp:814`) `if (!interactive_desktop_is_default_uncached()) return false;`. 실측: 15:15:50~56.05 에 초당 2회 kick → WGC(secure) 구간 56.177~58.9 에 **kick 0회** → 58.989 부터 재개. 그 구간의 실제 AU 들이 0.5~0.8s 보유(seq 24469 c2eUs 503ms, seq 24484 c2eUs 812ms).
- flush(`FlushCapturePipelineState` `:723-776`) 는 캡처 링만 비우고 **MFT 안의 보유 입력·FIFO·KickState 는 그대로**, MFT 는 geometry/fps/keyint 가 바뀔 때만 재생성(`host_encoder_manager.cpp:53-68`) → 복귀 flush 뒤 첫 호출에서 **전환 전 입력의 AU 가 현재 generation·real 플래그로 송출**(seq 24484: 101B P, stamp 820ms 전; 그 다음 24486 이 forceKey IDR). `hdr.streamGeneration`(`:246`) 도 이 tick 의 값이라 오표기.
- 뷰어 `decodeQueueLagUs` = 들어온 stamp − present 된 stamp(`viewer_frame_gate.cpp:155-165`), 침묵(>250ms) 뒤 실제 프레임이 floor 를 재고정 → 오래된 24469/24484 가 anchor → 신선한 burst 가 567/848ms "적체" 로 읽힘. 뷰어 자체는 presentBacklog=0 이라 실제 적체가 아니었다.
- 결론(근거 수준): 뷰어 측 메커니즘은 seq 로 확정, 호스트 측 출처는 "비동기 MFT 1프레임 보유 + secure desktop 에서 kick 차단 + flush 가 인코더를 비우지 않음" 이 코드·로그 모두와 일치. WGC/DXGI 시계 차는 아님(`captureStampUs` 는 callbackUs=qpc_now, `host_stage_gate_static.cpp:151`). 최종 확정은 아래 계측(FIFO epoch 로그)과 결정론적 재현으로 한다.

---

## 1. 수정 계획

### P10 (관측성 복구) — 파일: `log_upload.{hpp,cpp}`, `host_app_main.cpp`, `client_shell_main.cpp`, `apps/directory/server.js`, 테스트
1. **업로더 재구성 API**: `log_upload_configure(config, reason)` — 실행 중이면 토큰/URL/identity 를 **잠금 아래에서 교체**하고 워커는 매 배치마다 헤더를 읽음(현재 `:91` 무잠금 읽기 제거). `LogUploadConfig.identity`(계정+장치) 추가: identity 가 바뀌면 **대기 큐·재시도 슬롯 폐기**(개수 diag) → 이전 계정/장치의 줄이 새 계정으로 가지 않음. 같은 identity 의 토큰 교체는 큐 유지·즉시 재개. `log_upload_clear_credentials()`: 로그아웃/사인아웃 시 헤더 제거+큐 폐기(워커는 유지, 무잠금 join 없음).
2. **실패 분류·재시도(상한)**: 401 → `authRejected` 상태로 전환, 해당 배치를 재시도 슬롯에 보관(스트림당 1개, 바이트 상한 = batchMaxBytes), 새 토큰이 올 때까지 전송 중단(큐는 queueCapBytes 4MB 로 계속 bounded, 오래된 줄부터 탈락). 전송 실패(reached=0)/5xx/408/429 → 일시 실패: 재시도 슬롯에서 2s→4s→8s 백오프, **최대 3회·최대 60s 나이** 뒤 폐기(카운트). 나머지 4xx(400/403/404/413…) → 영구 거부: 즉시 폐기(카운트). 토큰 원문은 diag 에 절대 안 씀(현행 유지, 테스트로 고정).
3. **관측 가능성**: `log_upload_status()` → {running, authRejected, sent/failed/retried/discarded, lastStatus, lastChangeUs}. diag 에 `token replaced identity-same/identity-changed discarded=N`, `auth rejected status=401 paused`, `auth restored`, `retry ok/gave up` 1줄씩(rate-limit). 호스트 UI: `refresh_status_text`/트레이 팁에 "Log upload: sign in again" 추가(기존 SIGN IN AGAIN 배지와 같은 조건군). 클라 셸: 401 시 페이지에 `status error "로그 업로드 인증 만료 — 다시 로그인해 주세요"` 1회, 로그인 성공 시 재개.
4. **호출처**: 호스트 sign-in 결과(`:1335`)·시작 시(`:1433`) → `configure`; `sign_out` → `clear_credentials`. 클라 로그인 → `configure`(identity=account+machine); logout → `clear_credentials`.
5. **서버**: (a) `hostTokens` 이전 해시 **유예 없이 삭제 유지**(요구: 인증 약화 금지) — 클라 측 즉시 교체+재시도로 충분. (b) `sessions` 를 store 에 **해시+만료로 persist**(`store.sessions`), 로드 시 만료 제거, sweep 시 저장 → 서버 재시작 뒤 12h TTL 내 Bearer 유지. 배포는 지시 없이는 안 함(코드+테스트만). ※ (b) 가 범위 밖이라 판단되면 알려주면 제외 — 나머지는 (b) 없이도 성립.
6. **테스트**: 신규 `remote60_log_upload_test`(in-process 가짜 HTTP 서버, WinSock): 토큰 교체(같은 identity → 큐 유지, 새 헤더로 성공), identity 교체 → 큐 폐기, 401 → pause → 새 토큰 → 보관 배치 재전송 성공, 401 지속 → 큐 상한에서 오래된 줄 탈락(메모리 bounded), 일시 실패 3회 → 폐기, 영구 4xx 즉시 폐기, diag 에 토큰 문자열 부재. 서버 `logs_test.js` 확장: x-host-token 성공, 재등록 뒤 구토큰 401/신토큰 200, stale Bearer 401; `restart_test.js` 확장: 세션 재시작 생존. 실제 NAS 업로드 복구: 이 PC 호스트 재시작은 사용자 실기 항목으로 남김(라이브 프로세스 교체 금지).

### P9 (UAC 좌표) — 파일: `host_capture_session.{hpp,cpp}`, `host_loop_helpers.cpp`, `host_startup_graphics.cpp`, 신규 `host_input_target_rect.hpp` + 테스트, `secure_input_service_main.cpp`
1. **권위 있는 캡처 rect**: `CaptureState::inputTargetRect{originX,originY,w,h,source}` 를 `RestartCaptureSessionImpl` 성공 직후에 **실제로 연 캡처 원본**으로 확정 — DXGI: `monitorInfo->monitor` 의 `GetMonitorInfo` rect; WGC 모니터: 아이템을 만든 HMONITOR(선택 모니터면 그 handle, 아니면 주모니터) 의 rect; GDI: 주모니터 rect; 창모드: rect 0(secure 경로 미사용, 에이전트 폴백 유지). 순수 함수 `derive_input_target_rect(backend, windowMode, monitorRect)` 로 분리해 단위테스트.
2. **재푸시**: `restart_capture_session` 꼬리와 시작 경로에서 `broker.SetTargetRect(rect)` + 변경 시 로그 `secure-input target rect=(x,y)/WxH source=... reason=...`(같은 값이면 무로그). 캡처 재시작이 RDP↔콘솔·DXGI↔WGC↔GDI·모니터 선택·geometry 변경을 모두 지나므로 WM_DISPLAYCHANGE 별도 훅 없이 커버(호스트에 메시지 루프 훅 없음 확인). 전체 virtual rect 치환 없음.
3. **DPI**: 에이전트 per-monitor-v2 + 호스트 `rcMonitor` 물리 픽셀 → 스케일 없음(현행 유지, 로그 `virt` 로 검증).
4. **진단 예산 재충전**: `diag_inject_landing` 예산을 "마지막 버튼 이벤트로부터 2s 이상 비었으면 12 재충전 + 분당 최대 60건" 으로 변경(폭주 방지 유지). 첫 이벤트마다 target/mapped/cursor/virt 기록.
5. **테스트**: `secure_input_mapping_test` 회귀 — (841,703)/1920x1080 → 정상 rect (0,0)/1920x1080 에서 (841,703), stale (0,0)/2236x1232 에서 (979,802)(결함 재현 고정); 신규 `host_input_target_rect_test`: 2236x1232→1920x1080 전환 시 rect 갱신, 선택 모니터 음수 원점 (-1920,0)/1920x1080, 창모드 → 0, GDI/WGC/DXGI 분기; 예산 재충전 순수 함수 테스트. **실제 클릭 육안 검증은 사용자 실기**(UAC 2회 이상, 로그 `target==virt`·`mapped==in`).
6. Ledger(수정 안 함, 기록만): 모니터 선택 뒤 첫 재시작에서 주모니터로 복귀(`host_capture_session.cpp:443` `CreateItemForPrimaryMonitor(nullptr)`), 일반 경로도 선택 모니터 무시(`host_input_inject.cpp:257`). 이번 rect 는 "실제 캡처 중인 모니터" 를 따르므로 입력-화면 일관성은 유지됨.

### P11 (UAC 복귀 혼잡) — 파일: `mf_h264_codec.{hpp,cpp}`, `host_stage_encode_send_h264*.cpp`, `host_capture_session.{hpp,cpp}`, `host_kick.hpp`, 테스트
1. **계측(먼저)**: FIFO 입력에 `inputEpoch`(flush 마다 +1, 호스트 내부) 동반 → AU 에 `inputEpoch` 노출. `capture-switch first-frame` 에 `auEpoch= curEpoch= auCaptureUs= holdUs=(sendStart−auCapture)` 추가, `user-feedback`/trace 에 `auEpoch`·`fifoDepth`. 이걸로 "복귀 첫 AU 가 전환 전 입력" 을 로그로 직접 구분(완료조건 1).
2. **flush 계약**: flush 시 epoch++ 만 하고 MFT 는 건드리지 않음(비우면 IDR 스톰/재생성 비용). 대신 emit 단계에서 **`au.inputEpoch < currentEpoch` 인 AU 는 보류(pre-flush hold)** 하고, 첫 현재-epoch AU 가 도착했을 때: 그것이 키프레임이면 보류분 폐기(로그 `pre-flush-au discarded n= holdUs=`), 키프레임이 아니면(강제 IDR 미이행) 보류분을 순서대로 먼저 송출한 뒤 현재 AU 송출 → **참조 체인·미디어 순서 유지, 임의 P 폐기 아님**. flush 는 항상 `forceKeyNext` 를 세우므로 정상 경로는 폐기.
3. **secure desktop 의 kick 허용(보유 상한 복원)**: `BootstrapFrameCache` 에 채움 시점의 `desktopDefault`(uncached 질의)·`inputEpoch` 기록. `KickTryFill` 의 무조건 차단을 "캐시의 desktop 상태 == 현재 desktop 상태 && 캐시 epoch == 현재 epoch" 로 교체 → UAC 중에도 최신 secure 화면(WGC 실제 프레임) 만 재서빙, Default 화면을 secure 위에 재서빙하는 원래 위험은 그대로 차단. 효과: secure desktop 에서도 실제 AU 보유 ≤150ms, 복귀 flush 시 MFT 가 쥔 것은 synthetic kick AU(2 로 폐기).
4. **뷰어 게이트는 변경하지 않음**: `decodeQueueLag` 의 진짜 backlog 감지·anchor 재고정·IDR 요청 경로 그대로. (임계 상향·stamp=now 치환 없음.)
5. **anchor·IDR·timeline 수명 계약(요약)**: (i) UAC 이전(Default/DXGI): 실제 프레임 stamp=callback 시각, MFT 보유는 kick 으로 ≤150ms, 뷰어 anchor 는 실제 stamp 만 전진, synthetic 은 제외. (ii) UAC 진입(flush→epoch+1, forceKey): 전환 전 입력의 AU 는 emit 단계에서 보류→IDR 확인 후 폐기, 새 epoch 의 첫 송출 AU = IDR, generation 표기는 그 입력의 것. secure 화면에서도 kick 이 보유를 묶는다. (iii) 복귀(flush→epoch+1, forceKey): 동일 규칙 → 복귀 첫 실제 AU 는 복귀 후 캡처된 것이고 stamp 는 그 시각 → 뷰어 재개 anchor 가 신선 → 직후 burst 가 적체로 읽히지 않음. 타임라인 origin(`ResetTimelineAnchors`) 은 flush 마다 재설정(현행).
6. **테스트**: (a) `mf_h264_codec_test`: epoch 가 synthetic 과 함께 FIFO 를 타고 AU 에 나오는지(실 MFT, S11 방식). (b) 신규 `remote60_host_encode_epoch_test`(실 H264Encoder): 입력 A(epoch1) → flush(epoch2, forceKey) → 입력 B: **수정 전** A 의 AU 가 epoch2·real·오래된 stamp 로 송출됨을 재현(실패), **수정 후** 보류→B 의 IDR 확인→폐기; forceKey 미이행 분기(B 가 P) 는 A→B 순서 송출. (c) kick 정책 순수 테스트: secure 상태에서 캐시 desktop 일치 시 허용, 불일치/epoch 불일치 거부; 입력 A 후 800ms 무입력 시 kick 없으면 A 의 AU 가 800ms 뒤 나오고(재현) kick 있으면 150ms 내 나옴. (d) `viewer_udp_recovery_test` S13/S14: FakeHost 가 두 실제 트레이스 형태를 그대로 보냄 — S13 "수정 전 형태"(침묵 뒤 old real 800ms → fresh burst / flush 뒤 old AU → IDR → fresh): 뷰어는 Congested 1회·IDR 1회로 회복하고 폭주·reset 없음(비용 상한 고정), S14 "수정 후 형태"(kick synthetic + fresh, IDR 첫 송출): Congested 0. 기존 S1~S12 유지. RDP Active 면 캡처 실기 판정 제외(현재 콘솔).

---

## 2. 위험
- P10: 워커가 헤더를 잠금 아래 복사하도록 바꾸므로 경합 제거; 재시도 슬롯이 큐 상한에 포함되게 회계 주의. 서버 세션 persist 는 store 파일 쓰기 빈도 증가(로그인 시·sweep 시만) — 배포 전 Codex 확인.
- P9: rect 가 "실제 캡처 모니터" 를 따르므로 선택 모니터가 재시작 후 주모니터로 되돌아가는 기존 결함이 있어도 입력은 화면과 일치. 창모드는 rect 0 유지(secure 경로 미사용).
- P11: pre-flush 보류는 flush 직후 1~2 AU 만 지연; forceKey 미이행 분기는 순서 송출로 안전. secure kick 허용은 desktop 상태·epoch 이중 일치가 조건이라 Default 화면 재서빙 위험 없음. 실기 미검증 항목: 실제 UAC 2회에서 `capture-switch first-frame holdUs` ≤ ~200ms, 뷰어 Congested 0.

## 3. 범위 밖(기록만)
12Mbps 대비 33~38Mbps 송신, 홀펀칭 성립 요인, util 기능, 모니터 선택 복귀 결함(ledger), `guardStaleEncoded`(기본 off, 그대로).

## 4. 산출
단계별 커밋 3~5개(P10 클라/서버, P9, P11 계측+수정, docs), history #401~, 구현계획 상태, 작업목록 1.1/1.2(+P11 줄), 버전 0.2.101, `dist/GNLinkSetup-0.2.101.exe` 1회. 최종 보고에 항목별 원인·변경 한계·실행 테스트·미검증·사용자 최소 실기 목록 포함.
