# keepalive-multi — 자원표 (2026-10-16, 기준 eedd207 = APK 0.2.30)

계약: `codex_keepalive_multi_r1.md` §2·§4. 0.2.30 의 단일 연결 전역을 **entry(PC 연결 하나)** / **binding(지금 화면에 붙은 entry 의 한 활성 기간)** / **전역 lifecycle epoch** / **UI 전용**으로 나눈다. 목적: 옮길 때 빠뜨리지 않기.

- entry 논리 키: canonical directory origin + account stable ID + hostId. 별도로 프로세스 유일 **entry handle**(같은 host 의 연결 교체도 다른 handle).
- binding token: entry handle + entry connection generation + binding generation + Surface generation. 화면·디코더·입력·selection·통계는 이 token 이 지금 것과 같을 때만.
- 전역 lifecycle epoch: sign-out·account/directory 변경·onStop·확인된 network 변화·명시적 모두 끊기에서만 오른다(0.2.30 `ConnectIntents.lifeEpoch` 를 그대로 씨앗으로 쓴다).

## native (`native_bridge.cpp`)

| 0.2.30 전역 | 무엇 | 분류 | 옮길 곳 |
|---|---|---|---|
| `g_session_controller` (ClientSessionController) | 세션 worker·control·UDP·park 상태기계·clipboard sync·window panel·runtime/stream 요청 | **entry** | `SessionEntry::controller` |
| `g_rendezvous` (DirectoryRendezvous) | observe 한 socket, punch, 인계 | **entry** | `SessionEntry::rendezvous` |
| `g_rendezvous_error` | 마지막 observe/punch 오류 문자열 | **entry** | `SessionEntry::lastError` |
| `g_chosen_candidate` | 응답한 후보 주소 | **entry** | `SessionEntry::chosenCandidate` |
| `g_observe_count`, `g_punch_count` | 재사용 시 0 증가 확인용 계수 | **entry**(+프로세스 합계는 진단만) | `SessionEntry::observes/punches` |
| `g_connect_gate` (ConnectOpGate) | op 번호·step 잠금·Adopt/CloseIfOwn/CloseExplicit | op 번호는 **프로세스 유일**(공용 counter), 나머지(current op·step stop·adopt 기록·rendezvous owner)는 **entry** | `SessionEntry::gate` + 공용 op counter |
| `g_video_decoder_sink` (AndroidVideoDecoderSink) | MediaCodec·Surface·selection gate·D1 첫 IDR·통계 | **전역 하나**(디코더/Surface 단일) — 쓰는 쪽은 **binding** 이 맞을 때만 | 그대로 하나 + entry 별 sink adapter(binding token 검사가 decoder 잠금 안에서) |
| `g_macro` (InputMacro) | 녹화·재생 | 전역 도구, 재생 대상은 **시작 때의 binding** | binding 이 바뀌면 재생 중지 |

ClientSessionController 안쪽(entry 와 함께 옮겨짐): park fence·held input·input queue·runtimeTune·streamState·desktopBackend·clipboard(incoming/baseline/push 요청)·window panel·thumbnails·telemetry window·connectionGeneration·connect/hello 계수. sink 로 나가는 호출(OnEncodedH264Frame·OnVideoStreamReset·OnVideoDiscontinuity·OnDecodeEpisodeStart·selection result/tag·key request/owed key·DecodeGeneration·Drain*Stats)은 **전부 entry 의 adapter 를 거친다.**

## Kotlin (`MainActivity.kt` / `SessionPark.kt`)

| 0.2.30 | 분류 | 비고 |
|---|---|---|
| `parked`, `parkPendingTap`, `parkEpisodeCounter`, `parkConfirmLogged`, park 만료 runnable | **entry** | park episode·deadline·확인 로그는 PC 별(정책 객체: 최대 대수·TTL) |
| `directoryHostKey`, `directoryHostName`, `connectionPathKind` | **entry** | 지금 화면의 entry 를 가리키는 값은 active handle 하나로 |
| `connectOwner` (ConnectOwner) | op 번호 공급은 프로세스, **현재 op 는 entry 별** | B connect 가 A 의 op 를 무효화하지 않는다 |
| `connectIntents` (lifeEpoch, fallback) | **전역 lifecycle epoch** + fallback 은 그 tap 의 entry | 0.2.30 S2-b 그대로 |
| `clipboardResumeHold` | **entry** | 활성 전환 때 baseline 재설정 |
| `pendingRuntimeConfigSync`, `pendingDesktopBackendSync`, `desiredStreamActive`, `lastAppliedStreamActive` | **entry** | 설정 값(requestedRuntime*, backend)은 사용자 전역 선호, "보낼 의무"는 entry 별 |
| `requestedRuntimeBitrateKbps/Fps`, `requestedDesktopBackend` | 전역 선호(저장) | 보낼 때 대상 binding 을 붙인다 |
| `selectionStage`, `pendingSelection*`, `selectionGenerationCounter` | **binding** | 전환 시 버린다 |
| `targetList*`, `windowSheet*`, `activeTargetTab` | **entry**(목록) / **binding**(진행 중 선택) | |
| `videoWidth/Height`, `expectedContent*`, `surfaceBuffer*`, `videoSurface`, `lastVideoOutput*`, `lastViewer*` | **binding** | 디코더·Surface 는 하나 |
| `viewerViewport`, `viewerPanMode`, `viewerTouchRouter`, `viewportPan*`, 터치 상태(`activeTouch*`, `lastTouchVideo*`, `touchDown*`, `pinch*`) | **binding** | 전환 = interrupt(R1) + reset |
| `viewerControlsHidden` | UI 전용(사용자 선택) | |
| `manualConnectMode` | 수동 IP 연결은 다중 park 제외, 같은 관리자 경유 | |
| `directoryHosts`, `directoryBusy`, `directoryObserveEndpoint`, `directoryConnectingName` | UI/디렉터리 전역 | 목록 누락만으로 entry 종료 금지 |
| 대화상자·로그·모드 배너 | UI 전용 | |

## JNI 표면 (66개) — 대상 지정 방식

- **entry 지정(handle)**: park 상태·begin/resume/end park·connection generation·session counters·chosen candidate·last error·disconnect(그 PC)·CloseIfOwn·directory observe/connect.
- **binding 지정(token, 호출 시점에 잡은 값)**: input·text·clipboard push/pull·window list·select*·runtime config·desktop backend·stream active·prepare/abort video switch·ready selection gen·reset video stream·thumbnail·window panel·session bytes·macro 재생.
- **전역**: SetSurface(디코더 하나)·NotifyFrameDisplayed·video debug/size·op 번호 공급·모두 끊기·lifecycle epoch.
- 금지: "active 를 조회해 두었다가 나중에 전역 active 로 보내기", hostId 만 받는 비동기 API.

## 시험 fixture 필요 사항 (발견)

- 디렉터리의 hostId = (account, machineId). 시험 host 의 machineId 는 레지스트리 MachineGuid 로만 정해져 **같은 PC·같은 계정의 host 2대가 같은 hostId** 가 된다. 2-host 검증용으로 **시험 host 빌드 전용**(REMOTE60_PINNED_SELECT_TEST_SEAM) machineId 지정이 필요하다 — 출하 host·wire 변경 아님, build gate 로 부재 확인.
