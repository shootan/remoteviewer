# WORK_PROGRESS

## 2026-02-23 (Phase 1 kickoff: M1/M2 transport path)

### Scope
- Native video PoC에 Phase 1 초기 작업 적용:
  - `P1-M1` 전송 스위치 도입
  - `P1-M2` UDP 영상(H264) 경로 연결
  - `P1-M5/M6` 큐/계측 보강 일부

### Files changed
- `apps/native_poc/src/native_video_transport.hpp` (new)
- `apps/native_poc/src/poc_protocol.hpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `automation/start_native_video_runtime.ps1`
- `automation/start_native_video_runtime_impl.ps1`
- `automation/status_native_video_runtime.ps1`
- `automation/verify_native_video_runtime.ps1`
- `구현.md`

### Implementation notes
- host/client 런타임 옵션:
  - `--transport tcp|udp` (default: tcp)
  - `--udp-mtu <bytes>` (default: 1200)
- UDP 핸드셰이크:
  - `UdpHelloPacket` / `UdpHelloAck`
- UDP 데이터:
  - `UdpVideoChunkHeader` 기반 H264 AU chunk 전송/재조립
- 현재 정책:
  - `raw over udp` 미지원(의도적으로 차단)

### Build
- `cmake --build build-native2 --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc` -> PASS

### Verification snapshot
- TCP verify: `automation/logs/verify-native-video-20260223-160752`
- UDP verify: `automation/logs/verify-native-video-20260223-160814`
- 결과:
  - UDP 경로 동작 확인
  - 지연 병목은 여전히 코덱 내부 큐(`c2e/encQueue`)에 남아 있음

## 2026-02-16 (one-pass) — Milestones 5~9 execution

### Scope
- Executed/verified milestones M5~M9 in `C:\work\remote\remote60`
- Constraint observed: all commands run with `workdir=C:\work\remote\remote60`
- `rg` not used (PowerShell/standard commands only)

### Implementation status
- Codebase already contains M5~M9 paths and probe handlers.
- Current version string in source: `0.1.0-m9.6` (`libs/common/src/version.cpp`).
- No additional source patch required in this pass.

### Verification performed

#### 1) Build verification
- `cmake --build --preset debug` → **fails** in non-vcpkg preset for host (`opus/opus.h` missing in include path)
- `cmake --build --preset debug-vcpkg` → **PASS**
  - `remote60_client.exe` built
  - `remote60_host.exe` built

#### 2) Host subsystem probes
- `remote60_host.exe --system-probe` → **PASS**
  - capture/audio/MF/input all ok
- `remote60_host.exe --realtime-media-probe` → **PASS**
  - realtime probe ok, loopback connected, RTP send path active

#### 3) E2E signaling probes (host + signaling + client)
- `--video-e2e-probe` → **PASS** (`video-ok ...`)
- `--audio-e2e-probe` → **PASS** (`audio-ok probe=ready`)
- `--input-e2e-probe` → **PASS** (`input-ok sendinput=1`)
- `--realtime-session-probe` → **PASS**
  - host trace confirms: `realtime-session-ok detail=ok connected=1 vSent=76 aSent=94`

### Evidence
- Host trace file: `logs/host_trace.log`
- Realtime SDP snapshots: `logs/realtime_offer.sdp`, `logs/realtime_answer.sdp`

### Notes
- For M9 runtime path, use vcpkg preset/build artifacts (`build-vcpkg/...`).
- Signaling server used during verification: `apps/signaling/server.js`.

## 2026-02-16 (runtime streaming implementation pass) — non-probe runtime path wiring

### Scope
- Added real runtime entry paths (host/client) separate from probe handlers.
- Wired host runtime path to: WGC frame capture -> D3D11 BGRA->NV12 conversion -> MF H.264 encode attempt -> RTP packetization -> WebRTC video track send loop.
- Kept existing probe paths intact (`--realtime-media-probe`, `--realtime-session-probe` unchanged).

### Files changed
- `apps/host/src/main.cpp`
- `apps/host/src/realtime_runtime.hpp` (new)
- `apps/host/src/realtime_runtime.cpp` (new)
- `apps/host/CMakeLists.txt`
- `apps/client/src/main.cpp`
- `apps/client/src/realtime_runtime_client.hpp` (new)
- `apps/client/src/realtime_runtime_client.cpp` (new)
- `apps/client/CMakeLists.txt`
- `libs/common/src/ws_rtc_signaling_client.cpp`

### Runtime entry paths added
- Host: `remote60_host.exe --runtime-stream <seconds>`
- Client: `remote60_client.exe --runtime-view <seconds>`

### Transport/signaling runtime fixes
- WinHTTP websocket recv loop now reassembles fragmented UTF-8 websocket messages.
- JSON string parsing now unescapes `\\n`, `\\r`, `\\t`, `\\"`, `\\\\` (required for SDP/candidate payloads).
- ICE candidate send payload normalized to plain string field for compatibility with local parser.

### Build verification
- `cmake --build --preset debug-vcpkg` -> PASS
- Artifacts:
  - `build-vcpkg/apps/host/Debug/remote60_host.exe`
  - `build-vcpkg/apps/client/Debug/remote60_client.exe`

### Practical smoke test (non-probe runtime path)
Executed with local signaling server:
1. `node apps/signaling/server.js`
2. `remote60_host.exe --runtime-stream 12`
3. `remote60_client.exe --runtime-view 8`

Observed evidence:
- Signaling routed full runtime SDP/ICE offer/answer exchange (non-probe):
  - `offer` routed client->host
  - `answer` routed host->client
  - ICE routed host->client
- Host runtime initialized capture+encoder stack:
  - `webrtcReady=1 connected=1 captureReady=1 encoderReady=1`
  - `capturedFrames` continuously increased (~650+ during run)

Current runtime limitation from smoke output:
- `videoTrackOpen=0`, `encodedFrames=0`, `videoRtpSent=0`
- client returned code `208` (`no_video_frames`)

Representative output (host):
- `[host] runtime_stream detail=runtime_no_video webrtcReady=1 connected=1 captureReady=1 encoderReady=1 videoTrackOpen=0 capturedFrames=671 encodedFrames=0 videoRtpSent=0 videoRtpDropped=0`

Representative signaling log:
- `routed ... type: 'offer', to: 'host'`
- `routed ... type: 'answer', to: 'client'`
- `routed ... type: 'ice', to: 'client'`

### Notes / next debugging focus
- Runtime session establishment is now non-probe and active.
- Remaining blocker for continuous video send is media-track readiness / encoder output availability in this negotiated runtime path.

## 2026-02-19 (host server refactor planning)

### Decision
- Adopt a multithreaded host-server architecture for runtime mode.
- Keep host process alive across client disconnects; client disconnect must not terminate host.
- Separate signaling/network handling from media runtime logic.

### Plan doc
- See `HOST_SERVER_REFACTOR_PLAN.md` for the authoritative implementation plan.

### Scope snapshot
- Introduce explicit runtime state machine (`WAITING/NEGOTIATING/STREAMING/RECOVERING`).
- Split into signaling thread, control/state thread, media worker thread.
- Session lifecycle managed per client connection (cleanup and return to WAITING on disconnect).
- Validate with 10s+ continuous AV streaming and reconnect-repeat scenarios.

### 2026-02-19 quick-start patch (server-style runtime entry)
- `apps/host/src/main.cpp`
  - `--runtime-stream 0` is now allowed and interpreted as server-style no-timeout run.
  - Added `--runtime-server` persistent mode entrypoint (auto-restart cycle wrapper around runtime host loop).
  - No-arg default launch now starts persistent runtime-server mode (F5-friendly).
- `apps/host/src/realtime_runtime.cpp`
  - Runtime loop supports server mode (`seconds<=0` => run continuously until external stop).
  - Added explicit runtime state transitions/logs: `WAITING/NEGOTIATING/STREAMING`.
  - Offer-gated media send path: keep WAITING until remote offer arrives.
  - `pc->onStateChange` now updates `connected` on both connect/disconnect, resets `trackOpen` on disconnect, and transitions back to WAITING/NEGOTIATING instead of exiting.

Build:
- `cmake --build --preset debug-vcpkg --target remote60_host` -> PASS

Intent:
- Stop host from auto-exiting due to short runtime timeout behavior when using server mode.
- Provide explicit persistent server startup path before full thread/state-machine refactor.
- Establish baseline state-machine behavior before splitting signaling/control/media threads.

## 2026-02-19 15:46 (main/runtime entry modularization)

### Change
- Extracted runtime server/stream entry logic out of `main.cpp` into dedicated module:
  - Added `apps/host/src/runtime_server.hpp`
  - Added `apps/host/src/runtime_server.cpp`
- `main.cpp` now dispatches runtime paths via:
  - `remote60::host::run_runtime_server_forever()`
  - `remote60::host::run_runtime_stream_once(seconds)`
- Kept behavior:
  - no-arg launch => persistent runtime server mode
  - `--runtime-server` => persistent runtime server mode
  - `--runtime-stream <sec>` => single run path
- Updated host build sources:
  - `apps/host/CMakeLists.txt` includes `src/runtime_server.cpp`

### Build
- `cmake --build --preset debug-vcpkg --target remote60_host remote60_client` -> PASS

### Intent
- Reduce `main.cpp` coupling and make next refactor step (thread-role split: signaling/control/media) safer and incremental.

## 2026-02-19 12:36 (runtime-debug-loop cron run)

### Loop execution
- Iteration attempted: 1/5
- Build command: `cmake --build --preset debug-vcpkg --target remote60_host remote60_client` (PASS)
- Runtime smoke sequence executed:
  1) `node apps/signaling/server.js`
  2) `build-vcpkg/apps/host/Debug/remote60_host.exe --runtime-stream 15`
  3) `build-vcpkg/apps/client/Debug/remote60_client.exe --runtime-view 10`

### Result classification
- `runtime_no_video`

### Evidence
- client exit code: `208` (`no_video_frames`)
- signaling exchange reached offer/answer/ICE routing (host/client registration + routed messages present)
- logs:
  - `automation/logs/sig-20260219-123605.out.log`
  - `automation/logs/host-20260219-123605.out.log`
  - `automation/logs/client-20260219-123605.out.log`

### Patch status
- No code patch applied in this run.

### Next action
- Patch runtime media-track open/send-path gating in non-probe runtime path; then rebuild + rerun 10s+ AV continuity verification.

## 2026-02-19 14:09 (runtime-debug-loop cron run)

### Loop execution
- Iteration attempted: 1/5
- Build command: `cmake --build --preset debug-vcpkg --target remote60_host remote60_client` (PASS)
- Runtime smoke sequence executed:
  1) `node apps/signaling/server.js`
  2) `build-vcpkg/apps/host/Debug/remote60_host.exe --runtime-stream 15`
  3) `build-vcpkg/apps/client/Debug/remote60_client.exe --runtime-view 10`

### Patch applied
- File: `apps/host/src/realtime_runtime.cpp`
- Change: switched H.264 encoder output extraction from preallocated output buffer lock to `IMFSample::ConvertToContiguousBuffer` path before Annex-B packetization.
- Intent: fix runtime encode-output read path where encoded payload could remain unseen (`encodedFrames` stuck at 0).

### Result classification
- `runtime_no_video` (unchanged class), with early host role offline observed during run.

### Evidence
- client exit code: `208` (`no_video_frames`)
- signaling shows normal offer/answer/ICE routing, then early host offline
- logs:
  - `automation/logs/sig-20260219-140847.out.log`
  - `automation/logs/host-20260219-140847.out.log`
  - `automation/logs/client-20260219-140847.out.log`

### Next action
- Add focused runtime instrumentation for encoder output state, track-open transition, and host runtime termination cause; then patch non-probe AV send path (video+audio) and retest 10s+ continuous runtime.

## 2026-02-19 15:20 (Phase 3 thread-separation/control-queue refactor)

### Scope
- Refactored host runtime signaling/control flow so network/signaling callbacks no longer manipulate runtime/media control state directly.
- Kept persistent server behavior (`no-arg` and `--runtime-server`) unchanged.
- Preserved runtime states and ensured disconnect returns to `WAITING` without host process exit.

### Files changed
- `apps/host/src/realtime_runtime.cpp`

### Code changes (minimal)
- Added queue-based control path in `run_realtime_runtime_host`:
  - Introduced `ControlEventKind` + `ControlEvent`.
  - Added thread-safe control queue (`std::mutex` + `std::deque`) with `push_control` / `pop_control`.
- Updated signaling callback behavior:
  - `sig.set_on_message(...)` now only enqueues events (`SignalingOffer`, `SignalingIce`).
  - No direct `pc->setRemoteDescription`, `pc->addRemoteCandidate`, or state transitions inside signaling callback.
- Updated peerconnection callback behavior:
  - `pc->onStateChange`, `pc->onLocalDescription`, `pc->onLocalCandidate` now enqueue control events only.
- Added control-event consumption in main runtime loop:
  - Applies offer/candidate, sends answer/ICE, and performs state transitions in control logic (single consumer path).
  - On disconnect (`PeerStateChanged != Connected`), explicitly sets:
    - `trackOpen = false`
    - `gotRemoteOffer = false`
    - state transition to `WAITING` (`reason=peer_disconnected`)

### Build verification
- Command:
  - `cmake --build --preset debug-vcpkg --target remote60_host remote60_client`
- Result: **PASS**
  - `remote60_host.vcxproj -> ...\\build-vcpkg\\apps\\host\\Debug\\remote60_host.exe`
  - `remote60_client.vcxproj -> ...\\build-vcpkg\\apps\\client\\Debug\\remote60_client.exe`

### Quick validation (signaling + host + client connect/disconnect)
- Executed:
  1) `node apps/signaling/server.js` (reported port 3000 already in use by existing signaling instance)
  2) `build-vcpkg/apps/host/Debug/remote60_host.exe --runtime-server`
  3) `build-vcpkg/apps/client/Debug/remote60_client.exe --runtime-view 6`
- Observed host runtime log:
  - `[host] runtime_server mode=persistent waiting_for_clients=1`
  - `[host][state] WAITING->NEGOTIATING reason=offer_received`
  - `[host][state] NEGOTIATING->WAITING reason=peer_disconnected`
- Observed client runtime log:
  - `[client] runtime_view detail=no_video_frames webrtcReady=1 connected=1 gotVideoTrack=0 videoRtpPackets=0 videoFrames=0`
- Host liveness after client exit:
  - host process remained running in `--runtime-server` mode (not exited), satisfying persistent-server disconnect behavior.

## 2026-02-19 15:49 (Phase 4 explicit role-thread scaffolding: signaling/control/media)

### Scope
- Continued incremental runtime-host refactor in `apps/host/src/realtime_runtime.cpp` to make thread roles explicit:
  - signaling callbacks enqueue only
  - control thread consumes control queue and mutates peer/signaling state
  - media thread handles frame wait/encode/RTP send only
- Kept no-arg and `--runtime-server` behavior unchanged.

### Files changed
- `apps/host/src/realtime_runtime.cpp`

### Code changes
- Added explicit role-thread scaffolding inside `run_realtime_runtime_host`:
  - `controlCv` + `frameCv` condition variables
  - `push_control_and_wake(...)` helper for enqueue-only callbacks
  - dedicated `controlThread` for consuming `ControlEvent` queue
  - dedicated `mediaThread` for frame wait -> MF encode -> RTP packetize/send
- Updated callback responsibilities:
  - `sig.set_on_message(...)` now enqueues `SignalingOffer` / `SignalingIce` and returns
  - `pc->onStateChange`, `pc->onLocalDescription`, `pc->onLocalCandidate` enqueue events only
- Kept disconnect recovery semantics:
  - on peer disconnect in control thread: `trackOpen=false`, `gotRemoteOffer=false`, state -> `WAITING`
- Main runtime thread now acts as lifetime coordinator:
  - sleeps until timeout (or forever in server mode)
  - signals `stop`, wakes CVs, joins control/media threads, then performs cleanup

### Build commands and results
1. `cmake --build build --target remote60_host remote60_client -j 8`
   - **FAIL** on non-vcpkg build for host (existing environment dependency issue):
   - `audio_runtime.cpp(6,10): error C1083: 'opus/opus.h': No such file or directory`
2. `cmake --build build-vcpkg --target remote60_host remote60_client -j 8`
   - **PASS**
   - `remote60_host.exe` and `remote60_client.exe` updated in `build-vcpkg/.../Debug/`

### Quick validation (host survives disconnect in runtime-server mode)
Commands executed:
1. `build-vcpkg/apps/host/Debug/remote60_host.exe --runtime-server`
2. `build-vcpkg/apps/client/Debug/remote60_client.exe --runtime-view 5`
3. `build-vcpkg/apps/client/Debug/remote60_client.exe --runtime-view 3`

Observed host logs include reconnect/disconnect transitions while process stays alive:
- `[host] runtime_server mode=persistent waiting_for_clients=1`
- `[host][state] WAITING->NEGOTIATING reason=offer_received`
- `[host][state] NEGOTIATING->WAITING reason=peer_disconnected`
- additional later transitions (including another offer/streaming attempt) while same host process remained running

Validation outcome:
- Client runtime still reports `no_video_frames` in this environment.
- **Host process persistence across disconnects is preserved** and returns to `WAITING` as intended.

## 2026-02-19 16:20 (combined refactor pass: diagnostics + runtime modularization + non-runtime separation)

### Scope delivered
- (A) Added structured runtime diagnostics focused on crash/race triage.
- (B) Reduced runtime logic in `apps/host/src/main.cpp` and kept runtime entrypoints modular.
- (C) Isolated non-runtime probe/test/legacy paths from core runtime flow to minimize scattered runtime/non-runtime branching.

### Files changed
- `apps/host/src/main.cpp`
- `apps/host/src/realtime_runtime.cpp`
- `apps/host/src/host_non_runtime_paths.hpp` (new)
- `apps/host/src/host_non_runtime_paths.cpp` (new)
- `apps/host/CMakeLists.txt`

### What changed and why
1) Runtime diagnostics expansion (`realtime_runtime.cpp`)
- Added persistent runtime diagnostics log stream (`logs/host_runtime_diag.log`) plus stdout mirror.
- Added session-scoped logging context with:
  - `session=<pid-tick>`
  - `thread=<hashed_thread_id>`
  - `elapsedMs=<since_runtime_start>`
  - `phase=<lifecycle/state/signaling/offer_apply/track/encoder_output/rtp_send/disconnect_cleanup/...>`
- Added explicit begin/end markers and state transition logs for:
  - offer apply (`offer_apply begin/end`)
  - track open callback (`track ... begin/end=open`)
  - encoder output (`encoder_output begin/end`)
  - RTP send loop (`rtp_send begin/end`)
  - disconnect/stop cleanup (`disconnect_cleanup begin/end`)
- State transition logging now includes reason and is tied to elapsed/session/thread context.

2) Runtime architecture cleanup
- `main.cpp` now primarily dispatches runtime modes:
  - no-arg => `run_runtime_server_forever()`
  - `--runtime-server` => persistent runtime mode
  - `--runtime-stream <sec>` => one-shot runtime mode
- Non-runtime execution path delegated out of `main.cpp`.

3) Non-runtime path isolation
- Added `host_non_runtime_paths` module to contain:
  - probe modes (`--capture-probe`, `--mf-enc-probe`, `--rtp-pack-probe`, `--rtp-size-probe`, `--audio-loop-probe`, `--input-probe`, `--realtime-media-probe`, `--system-probe`, `--capture-loop`)
  - legacy signaling bootstrap behavior (default non-runtime offer/answer probe responder path)
- This keeps core runtime startup path cleaner and reduces mixed runtime/probe branches in `main.cpp`.

### Build commands and results
- `cmake --build --preset debug-vcpkg --target remote60_host remote60_client`
  - **PASS**
  - host/client targets rebuilt successfully.

### Validation commands and results (signaling + host + client connect/disconnect)
Executed:
1. `node apps/signaling/server.js`
2. `build-vcpkg/apps/host/Debug/remote60_host.exe --runtime-server`
3. client connect/disconnect cycle #1:
   - `build-vcpkg/apps/client/Debug/remote60_client.exe --runtime-view 6`
4. client connect/disconnect cycle #2:
   - `build-vcpkg/apps/client/Debug/remote60_client.exe --runtime-view 4`

Observed:
- Signaling server logs show host/client registration and routed offer/answer/ICE for both cycles.
- Host runtime diagnostics show:
  - runtime lifecycle begin
  - `WAITING->NEGOTIATING` on offer
  - offer apply begin/end markers
  - disconnect cleanup begin/end markers
  - subsequent second client offer while same host process remains active
- Host process remained alive in persistent server mode across client disconnects (behavior preserved).
- Client still exits `208` (`no_video_frames`) in this environment; this refactor targeted instrumentation + structure while preserving behavior.

### Behavior compatibility check
- Preserved no-arg persistent host mode.
- Preserved `--runtime-server` persistent mode.
- Preserved intent that client disconnect does not terminate host process (validated in runtime-server flow).

## 2026-02-19 17:55 (runtime media path targeted fix pass: control-state + ICE flush + send gating)

### Scope
- Focused only on unresolved runtime media path (`no_video_frames` / rc=208).
- Kept host persistent runtime-server behavior intact.

### Code changes
1) `apps/host/src/realtime_runtime.cpp`
- Peer state handling fix (transient-state safe):
  - ����: `PeerStateChanged`���� `Connected`�� �ƴϸ� ��� `gotRemoteOffer=false`/cleanup.
  - ����: terminal state(`Disconnected/Failed/Closed`)������ full cleanup ����.
  - Added peer-state diagnostics (`phase=peer_state non_connected state=<int>`).
- Media send gating:
  - Media loop now waits for track readiness: `trackOpen || vtrack->isOpen()`.
  - RTP send loop breaks if connection/track readiness lost mid-frame.
  - Added 1ms pacing between RTP packet sends.
- Encoder path instrumentation:
  - Added `MF_E_TRANSFORM_NEED_MORE_INPUT` counter diagnostics.
  - Added `ProcessInput` failure diagnostics (hex HRESULT).
- Include fix: added `<mferror.h>` for `MF_E_TRANSFORM_NEED_MORE_INPUT`.

2) `apps/client/src/realtime_runtime_client.cpp`
- ICE candidate flush robustness:
  - Added `pendingCandidates` queue.
  - `onLocalCandidate` now queues until signaling+registration ready, then sends.
  - Flushes queued candidates right after pending offer flush.
- Added `<vector>` include.

### Build commands
```powershell
cmake --build --preset debug-vcpkg --target remote60_host remote60_client
```
- Result: success (host+client rebuilt in `build-vcpkg/.../Debug`).

### Validation commands
```powershell
node apps/signaling/server.js
build-vcpkg/apps/host/Debug/remote60_host.exe --runtime-server
build-vcpkg/apps/client/Debug/remote60_client.exe --runtime-view 10
```

### Validation results
- Client exit code remains `208`.
- Signaling now confirms bidirectional ICE routing (client->host ICE messages observed after patch).
- Host runtime diag progression improved:
  - `offer_apply end success=1`
  - `state transition=NEGOTIATING->STREAMING reason=peer_connected`
- New blocker observed:
  - No `video_track_open` event observed.
  - No sustained `encoder_output/rtp_send end` progression in the latest run (media thread gated by unopened track).

### Exact blocker (current)
- WebRTC session reaches connected state, but host media track does not transition to open (`trackOpen`/`vtrack->isOpen()` stays false in runtime path).
- As a result, runtime media send path does not complete end-to-end frame delivery to client (`videoFrames=0`).

### Next concrete patch point
1. In runtime host/client candidate-apply path, stop hardcoding media id to `"video"` in `addRemoteCandidate(...)` and route candidate mid/sdpMLineIndex exactly from signaling payload.
   - Current runtime code still uses:
     - host: `pc->addRemoteCandidate(rtc::Candidate(ev.text, "video"));`
     - client: `pc->addRemoteCandidate(rtc::Candidate(m.candidate, "video"));`
   - This is the highest-probability mismatch point for track-open failure after signaling/ICE connection appears up.
2. Add immediate diagnostics around `onTrack`, `track->isOpen()`, and candidate apply success/failure with mid/index to verify media component binding.

## 2026-02-19 18:10 (focused logging + input guard + low-priority DXGI backlog marker)

### Runtime logging / analysis support
- Updated `apps/host/src/realtime_runtime.cpp`:
  - Added explicit media wait diagnostics (`phase=media_wait`) for:
    - `reason=no_offer`
    - `reason=not_connected`
    - `reason=track_not_open`
  - This clarifies whether host is blocked before encode/send versus send-fail downstream.

### Mouse lock defensive guard
- Added `release_mouse_capture_guard()` in `apps/host/src/realtime_runtime.cpp`:
  - calls `ClipCursor(nullptr)` and `ReleaseCapture()`
  - applied at runtime start and shutdown
  - logs:
    - `phase=input_guard mouse_release_guard applied=1`
    - `phase=input_guard mouse_release_guard applied_on_shutdown=1`

### Capture backend backlog marker (low priority)
- Added placeholder files (not wired yet, history marker only):
  - `apps/host/src/capture_backend_dxgi.hpp`
  - `apps/host/src/capture_backend_dxgi.cpp`
- Intention: Desktop Duplication backend is deferred and explicitly tracked as low priority.

## 2026-02-19 18:15 (subagent: candidate mid/mline runtime fix + validation)

### Scope executed
1) Removed hardcoded ICE candidate mid `"video"` in runtime host/client apply paths and switched to signaling payload-derived mid (fallback `"0"`).
2) Added explicit runtime transition logs for onTrack/isOpen behavior on both host and client.
3) Built host+client with `debug-vcpkg`.
4) Ran signaling + host runtime-server + client runtime-view validation.
5) Captured remaining blocker evidence.

### Exact code diffs

#### A) Signaling payload now carries candidate mid/mline
- `libs/common/include/common/signaling_protocol.hpp`
  - `Message` added fields:
    - `std::string candidateMid;`
    - `std::optional<int> candidateMLineIndex;`

- `libs/common/src/ws_rtc_signaling_client.cpp`
  - Added JSON int parser helper: `find_json_int(...)`.
  - `send_message(...)` now serializes:
    - `candidateMid`
    - `candidateMLineIndex`
  - `parse_message(...)` now deserializes:
    - `m.candidateMid`
    - `m.candidateMLineIndex`

- `apps/signaling/server.js`
  - ICE route payload extended to forward:
    - `candidateMid`
    - `candidateMLineIndex`

#### B) Host runtime: remove hardcoded remote candidate mid + explicit transition logs
- `apps/host/src/realtime_runtime.cpp`
  - Incoming signaling ICE now carries candidate/mid/mline through control queue payload.
  - Replaced hardcoded call:
    - before: `pc->addRemoteCandidate(rtc::Candidate(ev.text, "video"));`
    - after: parse payload and apply `mid` from signaling (fallback `"0"`), then
      `pc->addRemoteCandidate(rtc::Candidate(cand, mid));`
  - Added logs:
    - `phase=ice_apply remote_candidate_add mid=...`
    - `phase=ice_send local_candidate_send mid=0 mline=0 ...`
    - `phase=track video_track_isOpen_transition open=...`
  - Outbound local ICE message now sets:
    - `ice.candidateMid = "0";`
    - `ice.candidateMLineIndex = 0;`

#### C) Client runtime: remove hardcoded remote candidate mid + explicit onTrack logs
- `apps/client/src/realtime_runtime_client.cpp`
  - Replaced hardcoded apply:
    - before: `pc->addRemoteCandidate(rtc::Candidate(m.candidate, "video"));`
    - after: `pc->addRemoteCandidate(rtc::Candidate(m.candidate, mid_from_payload_or_0));`
  - Added logs:
    - `ice_apply remote_candidate mid=... mline=...`
    - `onTrack fired isOpen=...`
    - `track onOpen fired isOpen=...`
    - `ice_send local_candidate mid=0 mline=0 ...`
  - Outbound local ICE now sends:
    - `candidateMid="0"`, `candidateMLineIndex=0`
    - including pending-candidate flush path.

### Exact commands run

```powershell
# Configure/build (debug-vcpkg)
cmake --preset windows-vs2022-vcpkg
cmake --build --preset debug-vcpkg --config Debug

# Validation run
node apps/signaling/server.js
.\build-vcpkg\apps\host\Debug\remote60_host.exe --runtime-server
.\build-vcpkg\apps\client\Debug\remote60_client.exe --runtime-view 10

# Re-check client exit/details
& .\build-vcpkg\apps\client\Debug\remote60_client.exe --runtime-view 6; echo EXIT:$LASTEXITCODE
```

### Build results
- `cmake --preset windows-vs2022-vcpkg` -> PASS
- `cmake --build --preset debug-vcpkg --config Debug` -> PASS
  - `build-vcpkg/apps/host/Debug/remote60_host.exe`
  - `build-vcpkg/apps/client/Debug/remote60_client.exe`

### Runtime validation results
- Signaling routed offer/answer/ICE both directions successfully.
- New ICE logs confirm mid/mline propagation/apply:
  - client: `ice_apply remote_candidate mid=0 mline=0 ...`
  - host diag: `phase=ice_apply remote_candidate_add mid=0 ...`
- Client final status:
  - `[client] runtime_view detail=no_video_frames webrtcReady=1 connected=0 gotVideoTrack=0 videoRtpPackets=0 videoFrames=0`
  - `EXIT:208`

### Remaining blocker (with evidence)
Goal not yet reached (`encoder_output+rtp_send` and `videoFrames > 0`).

Observed blocker is **video sender track never opens on host runtime path** after peer connects:
- `logs/host_runtime_diag.log` shows:
  - `state transition=NEGOTIATING->STREAMING reason=peer_connected`
  - repeated `phase=media_wait reason=track_not_open count=...`
  - **no** `phase=track video_track_open ...`
  - **no** `phase=encoder_output begin ...`
  - **no** `phase=rtp_send begin ...`

This indicates ICE mid hardcode issue is addressed, but runtime send path is still gated by host video track open transition not occurring.

## 2026-02-19 18:27~18:35 (gateway incident check + immediate execution attempt)

### User request context
- User reported: OpenClaw gateway was force-terminated and asked for cause.
- User then requested immediate full progression and result generation (`진행해`).

### A) Gateway incident triage performed

#### Commands executed
```powershell
openclaw gateway status
```
Additional log pull for latest OpenClaw logs (`%LOCALAPPDATA%\Temp\openclaw\openclaw-*.log`, tail).

#### Observed status/evidence
- Gateway currently healthy at check time:
  - `RPC probe: ok`
  - `Listening: 127.0.0.1:18789`
- Log contains clear restart/init sequence:
  - `listening on ws://127.0.0.1:18789 (PID 268)`
  - hooks loaded, cron init, channel provider startup, webchat connected.
- No explicit fatal crash stacktrace observed in the pulled segment.
- Frequent operational warnings/errors seen in historical log spans:
  - `read tool called without path`
  - ENOENT/PathNotFound style file access failures
  - offset/read-range mismatch style failures

#### Interim conclusion
- Most likely pattern: gateway had a down/restart event, then recovered; pulled evidence favors restart/recovery + tool-input/path failures rather than a clearly logged hard crash stacktrace.

### B) Windows event-log correlation attempt (cause narrowing)

#### Commands executed
```powershell
Get-WinEvent -LogName 'Microsoft-Windows-TaskScheduler/Operational' ... (filtered by openclaw/gateway)
Get-WinEvent -FilterHashtable @{LogName='System'; Id=@(7031,7034,1074,6008); StartTime=(Get-Date).AddHours(-24)}
Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddHours(-24)} ...
```

#### Result
- In this run, no directly attributable openclaw-gateway termination event was returned by the filtered queries (empty output for targeted pull).
- Cause remains not fully pinned by Windows event logs from this pass.

### C) Immediate runtime execution attempt (user "전부 진행")

#### Intended sequence (single-pass scripted)
1. ensure single signaling instance on port 3000
2. build:
   - `cmake --build --preset debug-vcpkg --target remote60_host remote60_client`
3. run:
   - `node apps/signaling/server.js`
   - `build-vcpkg/apps/host/Debug/remote60_host.exe --runtime-server`
   - `build-vcpkg/apps/client/Debug/remote60_client.exe --runtime-view 10`
4. collect logs and host diag tail (`media_wait/encoder_output/rtp_send/...`)

#### Actual tool result
- The automated exec call returned `No result provided` from tool channel before summary artifacts were surfaced.
- Therefore this specific pass cannot be treated as verified success/failure for runtime AV outcome.

### D) Current blocker/status snapshot (unchanged)
- Last confirmed runtime blocker remains:
  - client `rc=208`, `detail=no_video_frames`
  - host side frequently waiting on `track_not_open`
- No new confirmed evidence yet proving transition to:
  - `videoTrackOpen=1` + `encodedFrames>0` + `videoRtpSent>0` + client `videoFrames>0`

### Next concrete action
- Re-run runtime smoke in segmented commands (build -> signaling -> host -> client) with per-step persisted logs to avoid tool-channel summary loss, then patch only `track_not_open` send-start gating race and retest.

## 2026-02-19 19:01 (runtime video path unblock: SUCCESS)

### Root cause (confirmed)
- Runtime client offer was missing `m=video` (only `m=application` existed), so host/client media track open path could not complete.
- Cause in code: client `pc->addTrack(v)` return value was discarded, so recvonly video track lifetime was not preserved.

### Fixes applied
1. `apps/client/src/realtime_runtime_client.cpp`
- Keep recvonly video track handle alive:
  - `auto videoTrack = pc->addTrack(v);`
  - fail fast if addTrack fails.
- Bind receive handlers directly on the local recvonly track as well (not only `onTrack` callback path):
  - `onOpen` and `onMessage` handlers attached via `bind_video_track_handlers(...)`.
- Local ICE send now propagates actual candidate `mid` (observed: `video`).

2. `apps/host/src/realtime_runtime.cpp`
- Preserve candidate `mid`/`mline` in control events and signaling send path (no hardcoded `0`).
- `track_not_open` path now waits instead of force-sending (prevents early send-path stalls).
- Runtime offer/answer SDP dump logs added for negotiation diagnosis:
  - `logs/runtime_offer_from_client_latest.sdp`
  - `logs/runtime_answer_from_host_latest.sdp`

3. `apps/client/src/realtime_runtime_client.cpp`
- Runtime SDP dump logs added:
  - `logs/runtime_offer_from_client_latest.sdp`
  - `logs/runtime_answer_from_host_latest.sdp`

### Validation
- Short watchdog runtime run (6s) after patch:
  - command pattern: signaling + host `--runtime-server` + client `--runtime-view 6`
  - result: `CLIENT_EXIT=0` (previously `208`)
- Negotiation now includes video m-line:
  - offer has `m=video ...` + `a=mid:video` + `a=recvonly`
  - answer has `m=video ...` + `a=mid:video`
- Host runtime diag confirms active media send loop:
  - `track video_track_open ...`
  - repeated `encoder_output end encodedFrames=...`
  - repeated `rtp_send end sent=... dropped=0`

### Current status
- Runtime non-probe video path progressed from `no_video_frames` to successful client exit in short run.
- Next recommended verification: 10s+ continuous run and reconnect-repeat (3x) with archived logs.

## 2026-02-19 19:21 (runtime reconnect stabilization: PASS)

### Scope
- Fixed runtime reconnect instability where second/third client sessions could fail with `rc=208` after first successful session.
- Re-validated long run + reconnect repeat in one pass.

### Root causes addressed
1. **Client runtime track lifetime / receive path gap**
- `pc->addTrack(v)` return value had been discarded in runtime client path.
- Added retained recvonly track handle + direct onOpen/onMessage binding on local recvonly track.

2. **Host runtime offer retry / stale session events**
- After disconnect, new offer could hit `offer_apply` failure in existing peer state.
- Added peer recreate + offer retry path (`offer_apply_retry`) in host runtime control flow.
- Added peer generation tagging and stale-event ignore so old peer callbacks cannot tear down new sessions.

3. **ICE mid propagation consistency**
- Runtime host/client signaling paths now preserve and apply candidate `mid`/`mline` instead of static fallback-only behavior.

### Validation (authoritative)
- Run dir: `automation/logs/verify-20260219-192128`
- Result file: `automation/logs/verify-20260219-192128/result.txt`
- Outcome:
  - `continuous12s=0`
  - `reconnect1=0`
  - `reconnect2=0`
  - `reconnect3=0`
  - `host_alive_before_stop=True`
  - `overall_ok=True`

### Notes
- Host process remained alive during reconnect sequence; no lingering runtime host/signaling process left after scripted stop.
- Runtime SDP diagnostics remain available:
  - `logs/runtime_offer_from_client_latest.sdp`
  - `logs/runtime_answer_from_host_latest.sdp`

## 2026-02-19 20:37 (signaling restart recovery + no-hang verifier: PASS)

### What was failing
- Scenario D (`signaling` restart while host alive) regressed:
  - host did not re-register to restarted signaling
  - subsequent client session returned `rc=208`
- Ad-hoc repro command could appear hung when background process handles were not tracked/cleaned safely.

### Root causes fixed
1. `WsRtcSignalingClient` did not surface socket-close disconnect events from `recv_loop`.
2. Host runtime loop did not trigger a server-cycle restart on signaling disconnect.

### Code changes
1. `libs/common/include/common/ws_rtc_signaling_client.hpp`
- Added `disconnected_notified_` guard flag.

2. `libs/common/src/ws_rtc_signaling_client.cpp`
- On successful connect, reset disconnect-notify guard.
- Emit `disconnected:ws` when `recv_loop` terminates on remote close.
- Avoid duplicate disconnected callbacks in `disconnect()`.

3. `apps/host/src/realtime_runtime.cpp`
- Added signaling state callback handling.
- On `disconnected:*`, mark session restart requested and transition to `WAITING` with reason `signaling_disconnected`.
- Wake control/media waits so server mode can break cycle and reconnect quickly.

4. `automation/sig_restart_verify.ps1` (new)
- Added bounded, cleanup-safe verification script for signaling restart scenario.
- Ensures host/signaling processes are always stopped in `finally`.
- Produces archived logs and `result.txt`.

### Validation
- Run dir: `automation/logs/sig-restart-safe-20260219-203752`
- Result:
  - `client_rc=0`
  - `host_alive_at_start=True`
  - `host_alive_after_client=True`
  - `first_connected=True`
  - `reconnected_after_sig_restart=True`
  - `host_connect_success_count=2`
  - `overall_ok=True`

## 2026-02-19 20:41 (full runtime suite rerun: PASS)

### Scope
- Executed full runtime verification sequence (`continuous12s + reconnect x3`) with bounded-time cleanup-safe script.

### Artifacts
- Script: `automation/verify_runtime_suite.ps1`
- Run dir: `automation/logs/verify-20260219-204146`
- Result file: `automation/logs/verify-20260219-204146/result.txt`

### Result
- `continuous12s=0`
- `reconnect1=0`
- `reconnect2=0`
- `reconnect3=0`
- `host_alive_before_stop=True`
- `overall_ok=True`

## 2026-02-19 20:55 (parallel tracks implementation pass: network/security + media + input + ops)

### Summary
- Implemented a bundled pass across 4 tracks in one cycle:
  1. network/security baseline
  2. runtime media expansion
  3. runtime input channel path
  4. repeatable verification/ops scripts

### 1) Network/Security changes
1. `libs/common/src/ws_rtc_signaling_client.cpp`
- Added optional auth token on register:
  - env: `REMOTE60_SIGNALING_TOKEN`
  - register payload now includes `authToken` when set.
- Added `wss://` support in URL parser and connect path.
- Added optional insecure TLS toggle for local/self-signed debugging:
  - env: `REMOTE60_SIGNALING_TLS_INSECURE=1`

2. `apps/signaling/server.js`
- Added optional auth validation on register:
  - env: `REMOTE60_SIGNALING_TOKEN`
  - mismatch -> `E_AUTH_FAILED`.
- Added optional TLS listener:
  - env: `REMOTE60_SIGNALING_TLS_KEY`
  - env: `REMOTE60_SIGNALING_TLS_CERT`
  - TLS enabled => `wss://...` logging.

3. Signaling URL configurability
- Runtime/probe client/host signaling URL now configurable via:
  - env: `REMOTE60_SIGNALING_URL`
  - default: `ws://127.0.0.1:3000`
- Updated:
  - `apps/host/src/realtime_runtime.cpp`
  - `apps/host/src/host_non_runtime_paths.cpp`
  - `apps/client/src/realtime_runtime_client.cpp`
  - `apps/client/src/main.cpp`

### 2) Runtime media changes
1. `apps/host/src/realtime_runtime.cpp`
- Added env-driven runtime config:
  - `REMOTE60_VIDEO_FPS` (10~120, default 60)
  - `REMOTE60_VIDEO_BITRATE_BPS` (default 8Mbps)
  - `REMOTE60_ICE_SERVERS` (comma/semicolon separated URLs)
- Added audio send track in runtime path (`Opus PT=111`) alongside video.
- Added runtime Opus encode/send loop (20ms pacing) integrated with main media loop.
- Added audio RTP counters and diagnostics.
- Added ICE server config ingestion into `rtc::Configuration`.

2. `apps/host/src/realtime_runtime.hpp`
3. `apps/host/src/runtime_server.cpp`
- Extended runtime stats/print fields:
  - `audioTrackOpen`
  - `audioRtpSent`, `audioRtpDropped`
  - `inputEventsReceived`, `inputEventsApplied`

### 3) Runtime input channel changes
1. `apps/host/src/realtime_runtime.cpp`
- Added runtime `onDataChannel` handling for `input` label.
- Parses input JSON via `common/input_protocol` and applies OS input (`SendInput`) for mouse/key events.
- Sends `input_ack` / `input_nack` responses.

2. `apps/client/src/realtime_runtime_client.cpp`
3. `apps/client/src/realtime_runtime_client.hpp`
4. `apps/client/src/main.cpp`
- Added runtime audio recv track (`RecvOnly`) and packet counters.
- Added runtime input datachannel behavior:
  - sends initial + periodic mouse move events
  - sends `input_ping`
  - tracks `inputAcks`
- Extended runtime output stats:
  - `gotAudioTrack`, `audioRtpPackets`
  - `inputChannelOpen`, `inputEventsSent`, `inputAcks`

### 4) Ops/verification changes
1. Added `automation/soak_runtime_suite.ps1`
- Runs `verify_runtime_suite.ps1` repeatedly with summary output.
- Produces `automation/logs/soak-*/summary.txt`.

### Validation
1. Build
- `cmake --build --preset debug-vcpkg --target remote60_host remote60_client` => PASS

2. Full suite
- Run dir: `automation/logs/verify-20260219-205509`
- Result:
  - `continuous12s=0`
  - `reconnect1=0`
  - `reconnect2=0`
  - `reconnect3=0`
  - `host_alive_before_stop=True`
  - `overall_ok=True`

3. Signaling restart recovery
- Run dir: `automation/logs/sig-restart-safe-20260219-205554`
- Result:
  - `client_rc=0`
  - `first_connected=True`
  - `reconnected_after_sig_restart=True`
  - `overall_ok=True`

4. Soak (smoke 2-iter)
- Summary: `automation/logs/soak-20260219-205616/summary.txt`
- `pass=2`, `fail=0`

## 2026-02-19 22:00 (client visible/control path completion via web client)

### Summary
- Added a browser client path to provide real visible playback + real input forwarding immediately.
- Kept native runtime probe client path intact for automated verification.

### Changes
1. `apps/signaling/server.js`
- Added HTTP static serving on same server/port as signaling websocket.
- Added health endpoint: `/healthz`.
- Works for both `http` and `https` server modes.

2. Added web client files
- `apps/signaling/public/index.html`
- `apps/signaling/public/client.js`
- Features:
  - signaling register/offer/answer/ice flow (`role=client`)
  - remote video/audio playback via browser WebRTC
  - input datachannel (`input`) with:
    - mouse move
    - mouse button down/up
    - wheel
    - key down/up
  - input ack/nack stats and connection state panel
  - localStorage persistence for signaling URL/token/ICE servers

3. `apps/host/src/realtime_runtime.cpp`
- Extended runtime input application:
  - added `MouseButtonDown` / `MouseButtonUp`
  - added `MouseWheel`
  - still supports `MouseMove`, `KeyDown`, `KeyUp`

### Validation
1. Static web serve smoke
- `http://127.0.0.1:3000/` => `200`
- `http://127.0.0.1:3000/client.js` => `200`

2. Runtime suite regression
- Run dir: `automation/logs/verify-20260219-215643`
- `overall_ok=True`

3. Signaling restart regression
- Run dir: `automation/logs/sig-restart-safe-20260219-220057`
- `overall_ok=True`

## 2026-02-19 22:06 (implementation finish: run flow and docs polish)

### Summary
- Finalized user-facing run flow so host screen/control can be used immediately without probe command juggling.

### Changes
1. Added unified runtime launcher
- `automation/run_web_runtime.ps1`
- Starts signaling + host runtime server, waits for host registration, prints web URL.
- Optional browser auto-open: `-OpenBrowser`
- Optional timed auto-stop for smoke: `-AutoStopSec`
- Writes run artifacts to `automation/logs/web-live-*`.

2. Updated user test guide
- `USER_TEST_CHECKLIST.md`
- Added browser-client run steps and corrected binary paths to `build-vcpkg-local`.

3. Updated top-level readme
- `README.md`
- Added current status, quick-run command, manual run, and verification script commands.

### Validation
- Launcher smoke:
  - `powershell -ExecutionPolicy Bypass -File automation\run_web_runtime.ps1 -AutoStopSec 6`
  - Result: `READY=1`, `STOPPED=1`

## 2026-02-23 02:35 (native video PoC rollback + optimization baseline reset)

### Summary
- Native video PoC was temporarily switched to encoded H.264 path, but user test found:
  - visible artifact/corruption
  - higher perceived delay
- Rolled back to previous raw BGRA pipeline, confirmed stable behavior.

### Current baseline
- Host: WGC capture -> raw BGRA frame send (TCP)
- Client: raw BGRA receive -> Win32 render
- Local behavior: good responsiveness and stable image
- Limitation: very high bandwidth (~2 Gbps class at 1080p30), not WAN-ready

### Decision
- Keep current raw path as control/baseline.
- Proceed with optimization one-by-one toward Parsec-style goals:
  1. Add end-to-end per-frame telemetry
  2. Reintroduce encoded path behind feature flag (default off)
  3. Validate corruption-free decode/reconnect first
  4. Then move to transport/congestion optimization

### Related docs
- `apps/native_poc/README.md`
- `NEXT_PROJECT_PARSEC_STYLE_PLAN.md`
- `구현.md`

## 2026-02-23 03:00 (native milestones M0~M7 execution pass)

### Summary
- Kept raw BGRA path as stable default and added milestone execution scaffolding through M7.
- Focused on non-regressive changes so current \"works well\" behavior remains default.

### Code changes (native video path)
1. `apps/native_poc/src/poc_protocol.hpp`
- Added `sendQpcUs` to `RawFrameHeader` for better timeline measurement.
- Added control channel ping/pong message structs/types (`ControlPing`, `ControlPong`).

2. `apps/native_poc/src/native_video_host_main.cpp`
- Added runtime options:
  - `--codec` (default `raw`)
  - `--control-port`
  - `--tcp-sendbuf-kb`
  - `--trace-every`, `--trace-max`
- Added startup socket buffer diagnostics.
- Added optional separate control RTT server thread.
- Added trace logs with capture/send timestamps.

3. `apps/native_poc/src/native_video_client_main.cpp`
- Added runtime options:
  - `--codec` (default `raw`)
  - `--control-port`, `--control-interval-ms`
  - `--tcp-recvbuf-kb`, `--tcp-sendbuf-kb`
  - `--trace-every`, `--trace-max`
- Added paint invalidation guard to reduce render queue backlog.
- Added `trace_recv` and `trace_present` timeline logs.
- Added optional separate control RTT client thread.

4. `apps/native_poc/CMakeLists.txt`
- Added `REMOTE60_NATIVE_ENCODED_EXPERIMENT` option (default OFF).
- Wired compile definition to native video host/client targets.

### Ops/automation additions
- `automation/start_native_video_runtime.ps1`
- `automation/start_native_video_runtime_impl.ps1`
- `automation/status_native_video_runtime.ps1`
- `automation/stop_native_video_runtime.ps1`
- `automation/verify_native_video_runtime.ps1`
- `automation/wan_preflight_native_video.ps1`

### Documentation updates
- `apps/native_poc/README.md`
- `NEXT_PROJECT_PARSEC_STYLE_PLAN.md`
- `구현.md`
- New status board: `NATIVE_VIDEO_MILESTONES_M0_M7.md`

### Validation
- Build:
  - `cmake --build build-native2 --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc`
  - PASS

## 2026-02-23 18:35 (Phase1 미완료 구간 재개: M3/M4/M5 후속)

### Summary
- 사용자 요청으로 "미완료 항목부터 재진행" 재개.
- H264 경로에서 실제 지연 누적 완화 목적의 코덱/해상도 후속 반영.

### Code changes
1. `apps/native_poc/src/mf_h264_codec.hpp`
- 인코더/디코더 백엔드 조회 API 추가:
  - `backend_name()`
  - `using_hardware()`

2. `apps/native_poc/src/mf_h264_codec.cpp`
- `MFTEnumEx` 기반 하드웨어 우선 선택 추가:
  - encoder: `MFT_CATEGORY_VIDEO_ENCODER` (NV12->H264)
  - decoder: `MFT_CATEGORY_VIDEO_DECODER` (H264->NV12)
- 실패 시 소프트웨어 MFT, 마지막 CLSID 폴백.
- shutdown 시 백엔드 상태 초기화.

3. `apps/native_poc/src/native_video_host_main.cpp`
- 인코더 선택 백엔드 로그 출력 추가.
- H264 저비트레이트 구간 자동 인코딩 해상도 폴백 추가:
  - 기본 조건: `bitrate <= 1.5Mbps` and source > 720p
  - 자동 타깃: 720p 계열(비율 유지, even dimension)
- 수동 강제 옵션 추가:
  - `--encode-width`, `--encode-height`
- 간단한 BGRA nearest resize 경로 추가(encode 전 단계).

4. `apps/native_poc/src/native_video_client_main.cpp`
- 디코더 선택 백엔드 로그 출력 추가.
- stale frame drop + catchup 조건 보강:
  - `decodeQueueLag` 기준 포함
  - catchup 임계치 보수 조정
- stdout/stderr 즉시 flush(`unitbuf`) 설정.

5. automation script updates
- `automation/start_native_video_runtime.ps1`
  - `-EncodeWidth`, `-EncodeHeight` 파라미터 추가
- `automation/start_native_video_runtime_impl.ps1`
  - host args에 encode size 전달
  - 세션 json에 encode size 저장
- `automation/verify_native_video_runtime.ps1`
  - H264 host verify 시 encode size 전달 가능
- `automation/status_native_video_runtime.ps1`
  - `CODEC`, `BITRATE`, `KEYINT`, `ENCODE_WIDTH`, `ENCODE_HEIGHT` 출력

### Build/Run
- Build:
  - `cmake --build build-native2 --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc`
  - PASS
- Runtime smoke:
  - `start_native_video_runtime.ps1 ... -Codec h264 -EncodeWidth 1280 -EncodeHeight 720 -StartClient`
  - 세션/프로세스/로그 경로 출력 정상 확인

### Note (현재 미완료)
- 이 실행 환경에서는 WGC 캡처 이후 프레임 진행 로그가 제한적으로 관측되어,
  지연 p95 재계측은 사용자 인터랙티브 세션에서 이어서 수행 필요.

## 2026-02-23 20:58 (Phase1 지연/안정화 추가 반영)

### Summary
- 사용자 지시로 "지연 3초급 누적" 원인 제거 중심으로 추가 수정.
- 결과: 무프레임/hang 회귀는 방지했고, SW H264 경로 기준 E2E는 약 0.68s까지 확인.

### Code changes
1. `apps/native_poc/src/mf_h264_codec.cpp`
- 백엔드 파서(`mft_auto/mft_hw/mft_sw`) 정리.
- 기본값을 SW 안정 경로로 고정(미지정 시 `mft_sw`)하여 기본 실행 hang 회귀 방지.
- 하드웨어 강제(`mft_hw`)는 실패 시 false 반환으로 명시적 실패 처리.

2. `apps/native_poc/src/native_video_client_main.cpp`
- 메시지 루프를 `GetMessage` 블로킹에서 `PeekMessage + Sleep`으로 변경:
  - 프레임이 없어도 `--seconds` 종료가 정상 동작.
- catchup keyframe 복귀 조건 단순화(키프레임 도달 시 즉시 exit).
- catchup 임계치 재튜닝(SW 경로에서 과도한 드롭/정지 방지).

3. `apps/native_poc/src/native_video_host_main.cpp`
- stale encoded frame guard 로직 추가 후 기본 OFF로 조정.
- 실험 ON은 `REMOTE60_NATIVE_GUARD_STALE_ENCODED=1`로만 동작.

4. `automation/analyze_native_video_scene_timeline.ps1`
- `-LogDir` 경로 정규화 시 비가시 Unicode 제거 추가.

### Verification
1. SW 기본 경로
- Command:
  - `powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -Transport tcp -Bitrate 1100000 -Keyint 15 -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 8 -NoInputChannel -TraceEvery 30 -TraceMax 120`
- Log dir:
  - `automation/logs/verify-native-video-20260223-205802`
- Result:
  - `OVERALL_OK=True`
  - `STAGE_TOTALUS_AVG_US=679425.86`
  - `BOTTLENECK_STAGE=c2eUs` (`STAGE_C2EUS_AVG_US=640423.71`)

2. HW 강제 경로 진단
- Command:
  - `powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 ... -EncoderBackend mft_hw -DecoderBackend mft_hw`
- Log dir:
  - `automation/logs/verify-native-video-20260223-205821`
- Result:
  - 프레임 0개(컨트롤 채널만 동작), `OVERALL_OK=False`
  - host 로그가 encoder backend 출력 이전에서 멈춤.

### Current conclusion
- 3초급 누적 회귀는 완화됐지만, SW MFT 경로에서는 약 0.6~0.7s 지연 바닥이 남음.
- 다음 핵심 과제는 `P1-M3/M4`의 실하드웨어 저지연 경로 고정(NVENC/QSV/AMF).

## 2026-02-23 21:20 (AMF 하드웨어 백엔드 1차 코드 경로 추가)

### Summary
- NVENC SDK 파일 부재(`nvEncodeAPI.h/.lib/.dll` 미검출)로 NVENC 직접 구현은 현환경에서 진행 불가.
- AMD 런타임(`amfrt64.dll`) 존재를 확인해 AMF MFT 직접 백엔드 코드를 우선 추가.

### Code changes
1. `apps/native_poc/src/mf_h264_codec.cpp`
- `amf_hw` 백엔드 분기 추가:
  - encoder: `amf_mft_h264enc`
  - decoder: `amf_mft_h264dec`
- AMD CLSID 직접 생성 + friendly-name 매칭 enum 경로 추가.
- AMF 경로에서 타입 열거 hang 회피를 위해 direct type set 경로 추가.
- `REMOTE60_NATIVE_DEBUG_CODEC=1` 디버그 로그 추가.

### Verification status
- 빌드: PASS (`remote60_native_video_host_poc`, `remote60_native_video_client_poc`)
- 런타임 검증:
  - `verify-native-video-20260223-211950`
  - `verify-native-video-20260223-212050`
  - `verify-native-video-20260223-212143`
- 해당 구간에서 host 측 `capture item create failed`가 발생해 프레임 계측 불가(캡처 환경 이슈).

## 2026-02-23 21:38 (AMF backend bring-up 후속)

### Summary
- AMF encoder path를 실제 송신 가능한 상태까지 올림.
- decoder AMF path는 아직 초기화 실패.

### Code changes
1. `apps/native_poc/src/mf_h264_codec.hpp/.cpp`
- Encoder:
  - `set_d3d11_device()` 추가.
  - `MF_TRANSFORM_ASYNC_UNLOCK` 적용.
  - AMF 비동기 lock 이슈(`0xC00D6D77`) 해소.
  - output/input type 협상 및 비동기 event poll 보강.
  - output stream flag(`MFT_OUTPUT_STREAM_PROVIDES_SAMPLES`) 대응.
- Decoder:
  - `set_d3d11_device()` 추가.
  - 초기화 디버그 로그 보강.

2. `apps/native_poc/src/native_video_host_main.cpp`
- H264 사용 시 encoder에 host D3D11 device 전달.

3. `apps/native_poc/src/native_video_client_main.cpp`
- H264 사용 시 decoder용 D3D11 device 생성 시도 및 전달.

4. `apps/native_poc/CMakeLists.txt`
- `remote60_native_video_client_poc`에 `d3d11` 링크 추가.

### Verification
1. Encoder AMF + Decoder SW (동작)
- Log: `automation/logs/verify-native-video-20260223-213648`
- Result:
  - `OVERALL_OK=True`
  - `STAGE_TOTALUS_AVG_US=108840.33`
  - `STAGE_C2EUS_AVG_US=66646.5`

2. Encoder AMF + Decoder AMF (미동작)
- Log: `automation/logs/verify-native-video-20260223-213826`
- Result:
  - client decoder init 실패 (`configure_input_type` 실패)
  - `OVERALL_OK=False`

### Current conclusion
- 하드웨어 인코더 고정은 실효성을 확인했고(약 100ms대), 현재 남은 핵심은 하드웨어 디코더 초기화 성공.

## 2026-02-23 21:50 (HW 디코더 안정화 + 타임스탬프 계측 보강)

### Summary
- 다음 단계 요청에 따라 `P1-M3/M4` 후속을 진행.
- 디코더 출력 타임스탬프/버퍼 경로를 보강해 HW 디코더 실사용 안정성을 올리고,
  체감 지연과 로그 불일치 가능성을 줄이는 계측을 추가함.

### Code changes
1. `apps/native_poc/src/mf_h264_codec.hpp`
- `DecodedFrameNv12.sampleTimeFromOutput` 필드 추가.
- `H264Decoder`에 pending input timestamp queue/missing count 상태 추가.

2. `apps/native_poc/src/mf_h264_codec.cpp`
- `sample_to_nv12_bytes_from_2d_buffer()` 추가:
  - contiguous 변환 실패 시 `IMF2DBuffer`에서 NV12 바이트 복사 fallback.
- `H264Decoder::decode_access_unit()` 보강:
  - `MF_E_NOTACCEPTING` 시 drain 후 input 재시도.
  - 출력 sample-time 누락 시 input timestamp fallback 매핑.
  - pending timestamp queue 상한/flush 처리.
- decoder `initialize/reset/shutdown`에서 timestamp queue 상태 초기화.

3. `apps/native_poc/src/native_video_client_main.cpp`
- `trace_recv`에 `tsSource=mft|input_fallback|header_fallback` 추가.

4. `automation/verify_native_video_runtime.ps1`
- 결과 요약에 타임스탬프 소스 카운트 추가:
  - `TS_SOURCE_MFT`
  - `TS_SOURCE_INPUT_FALLBACK`
  - `TS_SOURCE_HEADER_FALLBACK`

### Build
- `cmake --build build-native2 --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc` -> PASS

### Verification snapshots
1. `encoder=amf_hw`, `decoder=amf_hw` 요청
- Log: `automation/logs/verify-native-video-20260223-214743`
- Actual decoder backend: `mft_enum_hw` (fallback)
- `STAGE_TOTALUS_AVG_US=74498.54`
- `TS_SOURCE_MFT=12`, `TS_SOURCE_HEADER_FALLBACK=0`

2. `encoder=amf_hw`, `decoder=mft_sw`
- Log: `automation/logs/verify-native-video-20260223-214803`
- `STAGE_TOTALUS_AVG_US=68833.75`

3. `encoder=mft_hw`, `decoder=mft_hw`
- Log: `automation/logs/verify-native-video-20260223-214859`
- `STAGE_TOTALUS_AVG_US=67123.3`
- `H264 encoder backend=mft_enum_hw hw=1`
- `H264 decoder backend=mft_enum_hw hw=1`

4. `encoder=mft_hw`, `decoder=mft_hw`, bitrate 4Mbps (1080p)
- Log: `automation/logs/verify-native-video-20260223-214923`
- `STAGE_TOTALUS_AVG_US=144685.33`
- `MBPS_AVG=2.2`

### Current conclusion
- 이전 "mft_hw 강제 시 무프레임" 상태는 최신 코드 기준에서 재현되지 않았고,
  현재는 HW MFT 송수신 경로가 로컬에서 안정 동작.
- 현재 병목은 `c2eUs`가 우세하며, 다음 작업은 캡처->인코더 투입 큐를 더 줄이는 튜닝.

## 2026-02-23 22:06 (P1-Gate 수치 재검증 + 저지연 실험 플래그 분리)

### Summary
- `c2eUs` 개선 시도 중 안정성 영향이 있어, 실험 튜닝은 기본값에서 분리하고 env 플래그로만 활성화되도록 정리.
- `720p60` / `1080p60` 게이트 수치를 최신 로그로 재확인.

### Code changes
1. `apps/native_poc/src/native_video_host_main.cpp`
- 기본 포트 변경 반영 상태 유지(`43000`).
- H264 실험 플래그 추가(기본 OFF):
  - `REMOTE60_NATIVE_H264_NO_PACING=1`
  - `REMOTE60_NATIVE_GUARD_STALE_PREENCODE=1`
  - `REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS=1..4`
- 실험값 가시화 로그 추가:
  - `h264 pacing=... stalePreEncodeGuard=... capturePoolBuffers=...`

### Verification snapshots
1. Baseline-like HW check
- Log: `automation/logs/verify-native-video-20260223-220535`
- `EncoderBackend=mft_hw`, `DecoderBackend=mft_hw`
- `STAGE_TOTALUS_AVG_US=76534.5`
- bottleneck: `c2eUs=35838.67`

2. Gate check: 720p60
- Log: `automation/logs/verify-native-video-20260223-220641`
- `bitrate=1100000`, `encode=1280x720`, `fps=60`
- `STAGE_TOTALUS_AVG_US=75233.71`
- `LAT_P95_US=66580`

3. Gate check: 1080p60
- Log: `automation/logs/verify-native-video-20260223-220659`
- `bitrate=4000000`, `encode=1920x1080`, `fps=60`
- `STAGE_TOTALUS_AVG_US=135371.67`
- `LAT_P95_US=137925`
- `PRESENT_GAP_OVER_3S=1`

### Current conclusion
- `720p60`은 목표권(대기/지연 허용 범위 내)으로 수렴.
- `1080p60`은 아직 `p95<=120ms` 게이트 미달이며, decode/render 구간 변동과 프레젠트 갭이 남아 있음.
- 다음 우선순위는 1080p60 전용 튜닝(코덱 설정/큐 상한/해상도 자동 폴백 정책 고도화).
