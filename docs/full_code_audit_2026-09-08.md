# 중단된 멀티에이전트 전수조사 — Codex 재확인 원장

기준 HEAD `c09e4d0`, 제품 소스 `acd8233`(0.2.103). 2026-09-08.

사용자가 중단시킨 분석의 초안 3개(`.claude/full_audit_host.md`, `full_audit_viewer.md`, `full_audit_server.md`)와 당시 프로브 출력을 Codex가 읽고 주요 분기·실제 호출자를 다시 대조했다. 에이전트를 재가동하지 않았다. 아래는 **43개 초안 항목 + 당시 메인 분석의 8개 항목**을 보존한 검토 목록이다. 51개 모두가 새로운 실기 재현 버그라는 뜻은 아니다. 기존 항목 강화, 옵션/오류 조건부 결함, 잠재 위험, 개선안을 구분한다.

기존 [안정화 원장 A01~A17](stabilization_audit_2026-09-08.md) 및 [09:57 실기 분석](field_test_2026-09-08_0.2.103.md)은 유지한다. 이번 목록이 09:59 UDP 유입 중단의 최초 원인을 확정한 것은 아니다. Android/옵션 경로 문제는 기본 Windows UDP 사건과 별도로 처리한다. 제품 수정·설치·배포 승인이 아니라 후속 설계 입력이다.

## 1. 먼저 처리할 묶음

1. **마지막 화면 보존:** HN01. 캡처 제한은 마지막 실제 변경을 버리는 대신 소유권 있는 최신 프레임을 보존해야 한다. 포인터/합성 프레임은 잃어버린 픽셀의 대체물이 아니다.
2. **출력·참조 체인·입력 수명:** HN04/HN07, V06, 기존 A02/A06. bool 하나로 입력 수용과 출력 성공을 표현하지 말고, 만들어진 AU·입력 소유권·복구 필요 상태를 각각 전달한다.
3. **전환 실패의 재시도:** HN05/HN06/HN08. 새 캡처/인코더가 준비되기 전 성공 상태를 확정하지 않는다. 요청한 창 실패가 다른 화면 송출로 바뀌지 않아야 한다.
4. **수신 유지 작업과 렌더 복구:** V14, V07/V08, 기존 A03~A05. 모든 패킷 종류에서 타이머를 돌리고, 렌더 성공과 디코드 성공을 분리한다.
5. **계정·저장·설치 계약:** SV01/SV02/SV06/SV07, AN01, I01. 인증 제한·로그 소유권·저장 성공·업데이트 성공을 각각 실제 결과로 판정한다.

## 2. 호스트 / 캡처 / 인코더 — 12건

아래 기본 파일 경로는 `apps/native_poc/src/`이며, 별도 경로를 적은 경우 그 경로를 따른다. P1은 먼저 설계/수정할 항목, P2는 후속 기능/안정성, P3는 낮은 우선순위다. “조건부”는 코드 분기는 확인했지만 필요한 오류/하드웨어 조건의 발생 빈도를 측정하지 않았다는 뜻이다.

| ID | 판정 | 재확인한 코드·문제 | 수정 방향·회귀 완료 기준 |
|---|---|---|---|
| HN01 | P1, 코드+제품 gate 모델 | `capture_cadence_gate.hpp:65-70`, `host_capture_session.cpp:222-228`: due 이전 마지막 실제 변경은 readback/cache 갱신 전에 버려짐. DXGI timeout은 재제출하지 않고 kick은 이전 cache만 사용 | 최신 거절 프레임을 안전하게 보관해 due에 제출. A→5ms 뒤 B→무변화에서 다음 C 없이 B가 표시되어야 함. duplication texture의 ReleaseFrame 이후 무소유 참조 금지 |
| HN02 | P2, NV12 opt-in 조건부 | `host_stage_encode_send_h264.cpp:298-302,362,371-376`: A 출력이 나온 호출에서 새 입력 B도 같은 requiredOutputs=1로 등록되어 둘의 texture lease를 반환 가능 | 출력 개수 대신 입력별 소비/참조 해제 계약. fake MFT가 B를 보유한 동안 재사용/덮어쓰기 금지. COM AddRef는 픽셀 불변성 보장이 아님 |
| HN03 | P2, 기존 D3 강화 | `mf_h264_codec.cpp:1506,1522,2034`: initialize의 shutdown이 d3dManager를 먼저 삭제. startup의 set-device→initialize 순서로 manager 전달 분기 도달 불가 | 장치 설정 수명과 transform 종료를 분리. 최초/재생성마다 실제 SET_D3D_MANAGER 전달 및 HRESULT를 검사. surface 입력 성공만으로 연결 증명 불가 |
| HN04 | P1, 실제 codec+fake transform 프로브 | `mf_h264_codec.cpp:1911-1925`, `host_stage_encode_send_h264.cpp:338-346`: 이전 출력 AU를 drain한 뒤 새 입력/뒤 출력이 실패하면 false. caller는 이미 생성된 AU까지 버리고 참조 체인을 유지 | `{inputAccepted, outputs, error, discontinuity}` 결과형. 유효 출력 송출 또는 IDR barrier로 참조 단절 복구. 새 입력 거절과 accepted-then-output-error를 분리 |
| HN05 | P1, 재시작 오류 조건부 | `host_stage_geometry.cpp:120-150,154-170`: pending을 지운 뒤 restart 실패는 로그만. restart는 먼저 detach하여 창 캡처 콜백/재시도 신호도 사라짐 | RestartPending/RetryBackoff 상태를 유지. WGC 창 resize의 일회 실패 뒤 외부 이벤트 없이 재시도. 일반 모든 백엔드 실패가 영구 정지라는 주장은 아님 |
| HN06 | P1, 초기화 오류 조건부 | `host_encoder_manager.cpp:123-142`: source 크기부터 확정하고 ApplyTarget 실패를 void로 숨김. `host_loop_helpers.cpp:238-242`는 성공 반환 | 새 encoder 준비 뒤 상태 commit, 실패 전달/rollback. initialize 실패 후 같은 크기의 다음 프레임에서 영구 실패 반복 금지 |
| HN07 | P2, 코드+gate 모델 | `host_epoch_gate.hpp:105-151`: 열린 gate에서 unknown AU를 버려도 awaitingKey는 false. 다음 current P는 Emit, seq gap도 없을 수 있음 | 불명 reference output 손실은 체인 무효화와 결합. AcceptKey→Unknown P drop→다음 P는 차단, 검증된 새 IDR만 재개 |
| HN08 | P1, 명시 창 생성 실패 조건부 | `host_capture_device.cpp:128-209`: preferred window 실패 후 monitor/다른 창으로 fallback. 호출자는 요청 HWND/PID/windowMode를 그대로 유지 | 명시 대상 실패는 fail-closed. 허용된 fallback도 실제 source identity를 함께 반환/확정. 요청 창 실패 시 desktop 픽셀 송출0 검사 |
| HN09 | P3, 장기 실행 산술 | `d3d_capture_readback.cpp:32`: ticks×1,000,000 선곱 overflow. 공용 time helper는 안전한 몫/나머지 구현 | 공용 monotonic helper 사용. 10MHz 예시 약21.35일 경계 전후 elapsed 검사. 현장 원인 귀속 안 함 |
| HN10 | P2, opt-in 값 | `mf_h264_codec.cpp:1962,1973`: `_SLEEP_US` 값을 ms 단위 Sleep에 그대로 전달 | 단위를 타입/함수에 반영하고 남은 deadline으로 clamp. 1000us가1초가 되지 않음. 기본값0은 영향 없음 |
| HN11 | P2, opt-in guard | `host_stage_gate_static.cpp:141-149` stale drop 전에 version 미소비로 같은 입력 재선택 가능. `_au.cpp:237-266`의 stale P 폐기도 참조 복구와 미결합 | 입력은 한 번만 소비/폐기, 출력 reference 손실은 barrier. 무새프레임 조건의 반복 횟수/CPU와 다음 P를 검사. 배포 profile 기본 off와 구분 |
| HN12 | P2, 지속 API 오류 조건부 | `mf_h264_codec.cpp:1375-1402`, `host_capture_device.cpp:395-406`: 열거 종료 코드 외 지속 실패에도 계속 증가/반복 | 알려진 열거 종료와 실패를 분리하고 bounded 실패 처리. E_FAIL 반복 주입 후 시작 단계가 유한 시간 내 종료 |
| HN13 | P1, 해상도 고착 실기 재현 | `host_stage_encode_send_h264.cpp:170-171` 이 프레임 payload 크기를 `encodeSourceW/H` 에 **무조건** 대입하는데, 교정용 `ApplyTarget`(`:177`)은 `!aspectClose`(`:166-169`, 2% 허용)일 때만 실행된다. 1280x720 과 1920x1080 은 둘 다 16:9 라 `aspectClose` 가 참 → **source 만 낮추고 교정을 건너뛴다**. `fit_size_preserving_aspect`(`host_bgra_scale.cpp:43-46`)는 `scale=min(...,1.0)` + `clamp_even_dim(...,2,srcW)` 라 확대가 불가하므로, 오염된 source 로는 `ApplyTarget`(`host_encoder_manager.cpp:44-46`)이 1080p 목표를 줘도 720p 로 고정한다. `nominalEncodeW/H` 의 ratcheting-down 방지(`:42-43` 주석)도 무력화. 대비: `ApplyConfirmedCaptureGeometry`(`:111-142`)는 `:131-133` 주석대로 **aspectClose 예외 없이** 즉시 재적용한다. 실기 정합: 0.2.104 17:56:08.297 에 `profile=high bitrate=12000000` 인데 `encode=1280x720`, 뷰어는 17:55:21.763~18:03:40 내내 720p (`docs/field_test_2026-09-08_0.2.104_720p.md`) | `encodeSource` 를 **원본 geometry 의 진실로 유지**하고 readback 출력 재설정으로 복구. **무조건 upscale 허용은 해법이 아님**(낮은 원본을 확대하게 됨). 완료 기준: 같은 종횡비의 축소 뒤 프로파일이 복귀하면 encode 해상도도 복귀하는 것을 결정론적으로 재현. **런타임 `encodeSource` 실측은 미수행 — 코드 성립과 실기 정합까지이며 증명 아님** |

## 3. 뷰어 / 공용 수신 / Android 디코더 — 14건

`android_video_decoder.cpp`는 `apps/android_direct_client/app/src/main/cpp/`, `MainActivity.kt`는 같은 앱의 `java/com/remote60/androiddirect/` 경로다. 나머지 기본 경로는 native_poc/src다.

| ID | 판정 | 코드·문제 | 수정/완료 기준 |
|---|---|---|---|
| V01 | P1, Android 오류 조건부 | `android_video_decoder.cpp:737-740,786-794`: held output release 실패가 codec을 null로 reset한 뒤 dequeue 호출. immediate 분기에는 null guard 존재 | reset 결과를 전달하고 동일 codec 생존 시만 drain. held release 실패 뒤 null dequeue0. 실제 기기 crash 재현은 없음 |
| V02 | P1, Android 정체 조건부 | `MainActivity.kt:3551-3559`의 in은 네트워크 유입이 아니라 성공 queue-input 횟수. codec input buffer가 막혀 in 불변이면 idle로 판단해 watchdog 초기화 | AU 유입/입력 수용/출력 clock 분리. 계속 오는 AU+TRY_AGAIN 무한 조건에서 bounded rebuild, 실제 무입력은 보호 |
| V03 | P2, 기존 Android 후속 공백 | `native_video_client_session.cpp:507-620`: Windows hold/PopDelivery/key-wait tick 부재. 뒤 완료 AU가25ms NACK grace 전 앞 손실을 지우고 완성 IDR gap에도 추가 요청 | 플랫폼 공용 복구 episode. Android 실제 controller 경로에서 P손실·IDR손실·완성gap IDR·무후속 검사. Windows test PASS를 Android 증거로 쓰지 않음 |
| V04 | P2, 코드 확인 | `native_video_client_session.cpp:327-348`: presentation stats를 drain 후 local snapshot으로만 보유. scheduler에서 Ping이 먼저 선택되면 snapshot 소실 | 소비될 때까지 unsent metrics version 유지. ping/metrics 동시 due에서 다음 iteration에 원본 sample1회 송신 |
| V05 | P2, Android reset 조건부 | `SetSurface`, queue 실패의 ResetCodec와 controller waitForKey가 연결되지 않음. 입력 key 도착만으로 wait를 해제하고 sink 결과는 key 요청만 | sink Accepted/Backpressure/ResetNeedsKey 등 상태 반환. reset 후 P 차단, 첫 IDR 실패+무후속에도 유계 요청 |
| V06 | P2, 비동기 output metadata | `viewer_video_receiver_frame.cpp:268-276,320-337`: 출력의 capture/synthetic는 사용하지만 seq/key/gen/encode/send는 현재 입력 header에서 복사 | decoder FIFO에 완전한 input identity. “현재 IDR 표시” 단정은 해당 IDR의 출력까지 false. 원격 대상 유출까지 입증한 것은 아님 |
| V07 | P1, render 실패 조건부 | `viewer_present.cpp:186-214`: flip swapchain을 유지한 같은 HWND에 GDI fallback, blit 반환값도 성공 판정에 미반영 | D3D→GDI 명시 전환과 실제 blit 성공 확인. 스크린샷/픽셀 검증 및 실패 시 presented anchor 미갱신. 09:59에는 렌더 실패 근거 없음 |
| V08 | P2, device loss 조건부 | `viewer_nv12_renderer.hpp:317-322`: Present 실패 후 ready/device 유지. caller는 ready=false일 때만 재초기화 | HRESULT 분류·UI 소유 device 복구와 decoder surface 수명 조율. reset 뒤 새 프레임 표시, 실패 loop 금지 |
| V09 | P2, surface opt-in | `viewer_nv12_renderer.hpp:133-134,343-361`: Texture2D shader에 Texture2DArray SRV 전달하는 분기 | array shader 또는 선택 slice copy. 2개 다른색 slice의 픽셀 검증. 기본 CPU-copy 경로와 분리 |
| V10 | P2, optional TCP/raw | `viewer_video_receiver.cpp:423,445-466,513`: peer payload 무상한 할당, raw geometry/stride/크기 불일치도 publish→GDI | 공용 checked header validator/메모리 상한. 짧은 BGRA·큰 geometry·padded stride를 renderer 전에 거부 |
| V11 | P2, optional TCP | `run_tcp:410-424,512-514`: select는 첫 byte만 기다림. partial header/body recv_all은 별도 deadline 없고 fg.tick도 없음 | incremental read/deadline 및 별도 maintenance clock. partial 전송 후 연결 유지하는 peer에서도 종료/복구 유계 |
| V12 | P2, 입력 적체 | `native_video_client_shared_core.cpp:75-93,147-161`: move만 버리고 key/text queue는 상한 넘어 계속 성장 | byte/age/backpressure·취소와 release 보장 분리. 통신 정체 뒤 오래된 입력 폭주/키고착 없이 메모리 제한 |
| V13 | P2, malformed peer 조건 | `udp_control_channel.cpp:176-192,279-292`: 개별8MiB 상한뿐 aggregate pending 수/TTL 없음. 조각 중첩도 count로 완료 | window/총byte/TTL/fragment coverage 검증. 무제한 pending과 가짜완료 방지. 정상 sender의 자연 현상으로 단정 안 함 |
| V14 | P1, control traffic 조건부 | `viewer_video_receiver.cpp:207-230,330-333`: timeout/video 끝만 maintenance. control/cursor/짧은패킷 continue가 반복되면 타이머 starvation | 모든 패킷이 지나는 common maintenance. 5ms control ACK + 불완전 영상에서 NACK/key timer 실행. 커서 단독은33ms 제한이 있어 단독 재현 근거 아님 |

## 4. 디렉터리 / 인증 / 저장 — 9건

모두 `apps/directory/server.js`다. VM 검사는 실제 함수의 격리 실행이며 라이브 NAS 공격/부하 검사나 HTTP/UDP 전체 통합 검사가 아니다.

| ID | 판정 | 코드·문제 | 수정/완료 기준 |
|---|---|---|---|
| SV01 | P1, 코드+VM | `:602-625` login 제한이 `:635-647` host/register 비밀번호 검사에 없음. scryptSync는 relay와 같은 event loop | 공용 인증 admission/backoff와 bounded async KDF. login 제한 뒤 register에서도 예산 공유, 정상 relay 지연 측정 |
| SV02 | P1, 코드+VM | `:585`에서 허용된 alice/.alice/..alice가 `logSegment:235-237`에서 모두 alice | 계정 storage key를 injective/immutable ID로. 다른 계정의 업로드·rotation 파일 격리. 경로 탈출/로그 읽기 API 유출 주장 아님 |
| SV03 | P2, 코드+일부VM | `:241,341-352`: 임의 device/stream으로 예산·파일 수 확장, budget map 미회수. 첫 batch는 크기검사 없이 수락 | 계정/전역 byte·cardinality cap, host-token/device 바인딩, stale budget 회수. 첫요청 초과·여러장치/stream 검사 |
| SV04 | P2, 코드+VM | `:1197-1235` relay auth에 광고된 clientIP 저장, bind는 실제 LAN wireIP와 비교 | advertised/wire identity 분리. 서버 같은LAN의 client→원격host에서 relay실패하지 않음. 09:59 회사P2P 사건과 별개 |
| SV05 | P2, 잘못된 인증 요청 조건 | `:1142` udpPort 무검증 저장→`:819` timer callback의 dgram.send에서 유효범위 오류 가능 | state mutation 전 정수/1..65535검증, scheduled send 예외 격리. 전체 서버를 죽이지 않고400/실패계수 |
| SV06 | P2, 저장 실패 조건 | `saveStoreNow:450-461` 실패를 로그만 남기고 signup/register는200. 구토큰부터 삭제 | 디스크 성공과 메모리/토큰 commit 결합. write/rename 실패에서 이전 credential 유지 및 명시 실패 |
| SV07 | P2, store 손상 조건 | `loadStore:409-418` ENOENT 외 오류도 빈 store로 기동, 후속저장으로 원본 대체 가능 | 미존재만 초기화, 손상/권한/스키마 오류는 fail/read-only recovery. 원본 보존과 복원 검사 |
| SV08 | P2, 자원 상한 | `loginFailures`, `observed` 등에 키길이·개수·전역 admission/회수 공백 | TTL뿐 아니라 count/bytes cap. 새 ID/nonce 입력에도 고정된 자원예산 유지 |
| SV09 | P2, 코드+VM | `:1139,1159-1165,1197` observation/punch를 읽을 때 만료 미검사. sweep 전까지 재사용 | 매 사용 시 만료 검증, TTL 남은시간 계약. sweep 지연/시간 경계 검사. 무기한 우회로 과장 금지 |

## 5. Android 계정·로그 / Native JSON — 8건

Kotlin 경로는 `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/`.

| ID | 판정 | 코드·문제 | 수정/완료 기준 |
|---|---|---|---|
| AN01 | P1, 소유권 코드 확인 | `LogUploader.kt:39-51,91-105`, `MainActivity.kt:3037-3044`: 로그아웃에도 업로더 지속, A의 queued log가 B의 새 token/URL로 배출 | owner tuple+epoch, logout clear, 같은owner token교체만보존. 늦은응답/계정/서버변경에서 old body가 새owner에0 |
| AN02 | P2, 비동기 lifecycle | `MainActivity.kt:3154-3213,3225-3229`: 연결 중 logout 가능, 작업 완료가 현재owner/Activity 검증 없이 세션/UI 확정 | operation generation·취소·native adoption 경계. 각 await 중 logout/destroy 후 stale 연결0 |
| AN03 | P2, 장애 관측 | `LogUploader.kt:94-105,128-133`: dequeue 후 실패 폐기, 401/429/5xx 보관·재인증 상태 없음 | bounded pause/retry/status. Windows P10과 기능 동등성을 별도 검증, 서버 session persist 도입과 혼동 금지 |
| AN04 | P2, byte계약 | `LogUploader.kt:60-65,94-98,123`: UTF16 length를 bytes로 세고 초대형1줄은cap초과 수용 | UTF8 encode-once·정확 byte/line cap. 한국어/emoji/단일초과줄 검사 |
| AN05 | 보안 설계 개선, 노출 조건부 | `SessionPersistence.kt:44-56`: Windows unlock 암호가 일반 prefs. manifest backup 허용, 비밀 제외 규칙 없음 | Keystore 기반 암호화와 backup/migration 정책. 실제기기/백업 노출은 미실측, private sandbox 자체가 없는 것처럼 표현하지 않음 |
| NC01 | P2, 코드 확인 | `directory_session_client.cpp:165-189`: JSON문자열 안 brace도 depth로 계산. 합법 hostName `Office { PC`로 목록파싱 누락 | typed JSON parser. brace/escape/Unicode 이름과 뒤 객체 보존 |
| NC02 | P3, 잠재 데이터 오류 | `directory_session_client.cpp:251-252`: epoch ms를u32로 파싱해 clamp. 현재소비자는찾지못함 | checked u64,13자리fixture 값동일 검사 |
| NC03 | P2, 관측 입력 조건부 | `directory_rendezvous.cpp:49-53,108-125`: UDP응답from 미검사·port signed 누적 overflow 가능 | 기대endpoint/nonce와checked숫자검증. 이것만으로 원격제어 인증 우회가 확정된 것은 아님 |

## 6. 메인 Codex의 설치·셸·입력 검토 — 8건

아래는 이전 멀티에이전트 실행 중 메인이 조사했고 이번에 다시 분기를 확인한 항목이다. 실제 설치/삭제/입력 주입은 실행하지 않았다. `.claude/integration_audit_probe/`는 당시 컴파일까지만 완료한 도구이므로 실행 PASS 증거로 세지 않는다.

| ID | 판정 | 코드·문제 | 수정/완료 기준 |
|---|---|---|---|
| I01 | P1, 업데이트 조건부 | `installer/installer_main.cpp:160-162` stop 목록은 Client/Viewer 제외, payload에는 포함. `:293-308` 앞 파일들을 직접 덮은 뒤 client 잠금에서 실패 가능 | payload/프로세스 목록 일원화·사전 준비·원자 교체/rollback. Client/Viewer 실행 중 실패해도 구버전 완전성 또는 새버전 일관성 보장 |
| I02 | P2, 명령행 파싱 | `installer_main.cpp:580-606` 전체 command line substring으로 /s/-s 판정. 실행파일명 `GNLinkSetup-signed.exe`도 silent 판정 | argv 토큰의 정확한 option 비교. 파일명/폴더명/유사옵션에 반응하지 않음 |
| I03 | P2, 삭제 결과 불일치 | `installer_main.cpp:396-418`: 지워지지 않은 payload를 재부팅 삭제한다고 알리지만 예약하는 것은 setup 파일뿐. ui 디렉터리도 남을 수 있음 | 실제 실패 파일별 예약/상태 보존, 빈 하위폴더 정리. 실패 시 registry/성공표시 계약 검증 |
| I04 | P2, 설치 성공 오판 | `installer_main.cpp:319-340`: service install 실패 메시지 뒤에도 최종 성공/return0, firewall/shortcut 결과도 무시 | 성공·부분설치·실패를 구분하고 서비스/방화벽 요구조건을 검증. 실행중 timed-out child 수명도 정리 |
| I05 | P2, 보안입력 agent 정체 조건 | `secure_input_service_main.cpp:133-148`: stop_agent가 blocking write_exact(shutdown)를 먼저 호출하므로 뒤1500ms kill bound에 못 갈 수 있음. broker WriteFile도동기 | agent pipe write에 취소/시간 상한, pipe-owner thread 종료계약. reader가읽지않는격리child에서도 stop유계. 실 서비스 정지 재현 없음 |
| I06 | P2, 매크로 취소 | `client_macro_window.cpp:203-209`, `input_macro.cpp:133`, `viewer_input_forward.cpp:166-174`: DOWN 후 stop/pause/clear에 대응 UP 없음. replay는 physical mouse 상태를 갱신하지 않음 | replay별 pressed ledger·중지/대상전환에release. DOWN→취소 후 원격 버튼 해제. 실제 입력 주입 없이 제품step/queue경계검사부터 |
| I07 | P2, 관리자실행 경로 위험 | `installer_main.cpp:164-174`: 이름만 taskkill을 CreateProcess(NULL,...)로 실행. 작업폴더/설치프로그램 옆 실행파일 검색에 의존 | System32 절대 경로 또는 직접 process API, 설치경로에 묶인 PID만 처리. 신뢰할 수 없는 디렉터리에서 도구 대체 가능성을 방어. 악용 실험 없음 |
| I08 | P3, 매크로 이름 검증 | `macro_shell_bridge.cpp`의 kReserved 비교는 일부 bare device name만 검사. 확장자가 붙은 CON 등과 COM/LPT 일부를 놓침 | OS 파일명 의미에 맞는전체device basename검증과경계테스트. 정상macroname을임의정규화해다른파일로쓰지않음 |

I07의 검색 순서는 [Microsoft CreateProcessW 문서](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)에 따른다. 악성 실행파일이나 권한 상승 동작은 생성/실행하지 않았다.

## 7. 중단 당시 검증 산출물을 다시 확인한 결과

- **HN01:** `.claude/full_audit_host_probe/result.txt`. 제품 CaptureCadenceGate/KickState + 변경기반 source 모델. 30/60fps 모두 마지막 revision2 거부, 합성출력 revision1, 별개 revision3에서만 복귀. 실 DXGI 재현은 아니다.
- **HN02/HN07:** 같은 파일의 수명/제품gate 모델. A 출력으로 A/B 모두 lease 반환, unknown drop 뒤 다음 P Emit 확인. 실제 GPU overwrite를 측정하지 않았다.
- **HN04:** `.claude/full_audit_host_probe/codec_partial_result.txt`. 실제 codec + 제어된 transform에서 `encode_return=0, produced_units=1`을 확인. 재시도 입력 거절 case와 B수용 뒤outputerror case가 각각 존재한다. 초안의 “fake MFT 아직 없음” 문구보다 이 후속 산출물이 최신이며, real HW MFT와 구분한다.
- **SV01/SV02/SV03/SV04/SV09:** `.claude/server_audit_probe.out`. 실제 server함수 VM에서 login 제한→register 검사 진행, 계정 log key 충돌, 첫batch cap, LAN client relay mismatch, 만료 observation 재사용을 확인. 소켓/실 NAS는 사용하지 않았다.
- V01~V14, AN01~AN05, 대부분 오류 주입 항목은 코드 증거이며 Android 기기·D3D failure injection·실 네트워크 재현을 하지 않았다. V14의 새로운 통합 프로브는 완료 근거가 없으므로 실행검증으로 세지 않는다.
- 서버 에이전트의 이후 추가 조사까지 모두 완료된 것으로 취급하지 않는다. 초안 밖의 인증 우회/발신자 검증 가설은 이번 확정 목록에 자동 추가하지 않았다.

## 8. 리팩터링·개선안

1. **EncoderResult/DecodedFrame identity:** 입력수용·출력생성·오류·reference invalidation을 타입으로 분리하고 seq/gen/epoch/lease를 단일 레코드로 관리한다. HN02/HN04/HN07/V06와 A02/A06를 작은 수명 계약 변경으로 묶는다.
2. **Capture lifecycle:** target request, 실제 source, resource 준비, codec 준비, 재시도 상태를 분리한다. geometry 변경의 transactional commit과 최신 거절 프레임 보존이 첫 단계다.
3. **공통 ReceiveMaintenance:** Windows/Android/TCP의 packet parsing과 timer/assembly/recovery를 분리한다. 모든 early return/continue가 maintenance를 통과하도록 한다.
4. **Renderer lifecycle:** D3D·GDI·device-lost 모드를명시하고 실제표시성공만anchor에반영한다. CPU/texture surface 경로를 같은 성공 bool로 숨기지 않는다.
5. **Typed auth/storage/JSON:** Node/Win32/Kotlin의계정owner·URL·숫자범위·만료·JSON파서를공유계약으로정의한다. server는durable store,auth,lease,relay,logs를분리한다. 동기KDF/파일IO가미디어eventloop를막지않게한다.
6. **시간·계측 단일화:** 공용safeQPC, 실제elapsed 분모, offer/accepted/encoded/enqueued/wire/decoded/presented를각각표시. synthetic과실제콘텐츠및입력header/출력identity혼합금지.
7. **실제 실패 경계 테스트:** optionalGPU/Android/설치본은관련경로가실행됐는지assert하고, fixed port/기존서버가응답한test를PASS로인정하지않는다. 각테스트는자신이시작한process/readiness/cleanup을소유한다.

## 9. 최종 상태

- [x] 초안·프로브 출력과 주요 코드 분기 재확인, 중복/기존/조건부 구분
- [x] 추적 문서에 문제·수정 방향·완료 기준 보존
- [ ] 우선 묶음별 수정 설계 확정
- [ ] 제품 수정과 해당 실패 조건 회귀
- [ ] 실기 재검증 및 최초 UDP 유실 위치 규명

중단된 작업을 새 전수조사나 제품 구현으로 재개하지 않았다. 이번 변경은 문서뿐이다.
