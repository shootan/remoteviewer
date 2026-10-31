# Host/capture/encoder full audit — 2026-09-08

Baseline HEAD c09e4d0. Read-only product audit; only this untracked note and an isolated `.claude/full_audit_host_probe/` harness changed. No product patch, live process interruption, installer, or whole test sweep. Existing A01–A17 in `docs/stabilization_audit_2026-09-08.md` read first. Findings below are additional defects or stronger boundaries, not proof of the 09:59 field freeze's initiating cause.

## Confirmed code findings

### HN01 / P1 conditional content loss — last content update discarded by capture cadence

- `capture_cadence_gate.hpp:65-70` drops a real content offer before `nextContentDueUs_`; `host_capture_session.cpp:222-228` returns before readback/cache update. DXGI callback comes from `capture_backend_dxgi.cpp:496-500`; `ReleaseFrame` follows even if host callback discarded content. DXGI timeout path `:398-417` only sleeps/continues, no resubmit. Pointer-only reports are rejected by the same gate and cannot recover the image.
- Trigger: accepted revision A, final revision B 5ms later, then static screen. At 60fps default 25% tolerance the latter is earlier than the 12.5ms acceptance bound (30fps likewise). The change-driven producer does not promise a repeat of B.
- `PublishFrame:899-915` is the sole bootstrap writer; `KickTryFill:805-806,824` serves that old payload. 150ms kick and 1s refresh therefore repeat A. A later independent content change C eventually recovers; the no-new-content case has no deadline that captures B.
- Direct isolated probe, product `CaptureCadenceGate`/`KickState`, fake change-driven producer: `.claude/full_audit_host_probe/result.txt`, exit 0. Both 30/60fps: final_revision_2_accepted=0, idle_synthetic_revision=1, recovered_on_separate_revision=3. This is not real DXGI/GPU reproduction; producer/cache assignment is modeled, exact admission policy is product code. Console Active/RDP absent checked before build.
- Fix: preserve the latest rejected content in an owned texture/frame with due-time scheduling, or a safe trailing capture request; do not retain an unowned duplication texture beyond ReleaseFrame. Gate expensive processing, not the only copy of the final state.
- Regression: A→B before deadline→no more content, with pointer/timeouts and synthetic refresh, must eventually present B without a third content event; bound latency and GPU copies.

### HN02 / P2 correctness, opt-in NV12 — output count releases a newer MFT-held texture

- `host_stage_encode_send_h264.cpp:298-302` assigns every newly accepted surface `requiredOutputs = encoder.outputSamplesTotal + 1`; `:362,371-376` adds outputs returned by that call and releases all thresholds reached. No accepted-input index is associated with the release.
- Example: A accepted/no output → A threshold1. B accepted/emits A → B also threshold1. outputs becomes1 and releases **both A and B**, although B is the asynchronous held input. The next capture can select B's free slot and VideoProcessorBlt overwrites the same texture while MFT still reads it.
- `mf_h264_codec.cpp:1722-1746` wraps the texture with MFCreateDXGISurfaceBuffer; holding a COM reference preserves allocation, not immutability. `d3d_capture_readback.cpp:326-330` simply clears busy, and `:439-446` chooses the first non-busy texture.
- Direct arithmetic model in the same isolated harness: `async_MFT_model_output=A release_A=1 release_B=1 B_still_in_MFT=1`. No real MFT race/visual corruption repro. Product default surface mode is off (`host_capture_session.cpp:205-209`), so this is NOT assigned to current BGRA field failure.
- Fix: tracked sample/reference-release ownership acknowledgement, or a proven per-input completion contract (including skipped output/error). A raw output count is not that acknowledgement. [Microsoft ProcessInput lifetime](https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-processinput), [IMFTrackedSample reference-release mechanism](https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nn-mfidl-imftrackedsample).
- Regression: fake transform holds B's IMFSample after emitting A; all capture attempts must leave B's pixels unchanged until B is released. Cover accepted-input followed by output error and reinit.

### HN03 / P2 existing D3 strengthened — device manager is erased before any initialization can use it

- `H264Encoder::initialize` starts with `shutdown()` (`mf_h264_codec.cpp:1506`); shutdown clears `d3dManager_` (`:2034`). The purported manager hand-off `if(d3dManager_) ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER)` (`:1522-1525`) is therefore unreachable through the current public set-device→initialize sequence.
- Actual call order: startup `host_startup_graphics.cpp:140 set_d3d11_device` → `:394 initialize`; surface test `host_encode_epoch_test.cpp:174 set` → `:175 initialize`; live recreation `host_capture_session.cpp:109 set` does not send to running MFT, then any later ApplyTarget/rebuild initialization clears it again. ApplyTarget (`host_encoder_manager.cpp:54,61,71-72`) and epoch/stale rebuild (`host_stage_encode_send_h264_au.cpp:188-189,250-251`) never restore the configured device afterward.
- Existing ledger D3 says only live MFT rebinding is absent, with NV12 affected. This strengthens D3: **initial** manager setup is also absent, and existing shared-MFT-device assumptions/`mf-device-set` log label do not establish the actual transform's device. Some backends may accept surface inputs without this manager; success cannot prove manager wiring.
- Fix: separate persistent device configuration from transform-instance shutdown; preserve/reapply manager, check MF_SA_D3D11_AWARE and ProcessMessage HRESULT, expose actual accepted binding. Recreate encoder and readback together when actual device changes.
- Regression: test seam records exactly which manager pointer reaches every initialize/reinitialize; null manager only when deliberately CPU/no-device. Surface test must assert binding, not merely successful encode.

### HN04 / P1 conditional reference corruption — produced output discarded on later input/output failure

- `mf_h264_codec.cpp:1911-1925`: MF_E_NOTACCEPTING drains existing AUs into `outUnits`, then retries ProcessInput. If retry fails, returns false with produced AUs still in vector. Likewise a later error in drain after earlier successful outputs returns false.
- Product caller `host_stage_encode_send_h264.cpp:338-346` on false increments failure count and `Flow::Continue`, throwing away all produced AUs. No reference-chain barrier/forceKey is requested. New wire seq is assigned only at emit (`host_stage_encode_send_h264_au.cpp:296`), so the next P can have a contiguous seq while referencing a never-sent P.
- Surface caller (`:291-335`) additionally treats false as rejection and lets popped slot release, although ProcessInput may already have accepted the surface before a later output failure. Output failure is not equivalent to non-acceptance.
- Fix: structured result `{inputAccepted, producedUnits, errorClass, referenceDiscontinuity}`; preserve/send valid output independently from whether new input accepted, or close chain and force IDR when intentionally discarding it. Defer accepted surface ownership even when call's later step fails.
- Regression: controlled transform NOTACCEPTING→output P(A)→retry fails; verify A is emitted or barrier closes before later P. Also accepted B→output error must retain B until release/reset.

### HN05 / P1 conditional recovery loss — failed capture restart consumes its only retry trigger

- `host_stage_geometry.cpp:120-125` clears WGC mismatch pending before restart; failure `:147-150` only logs. `:154` exchanges sizeChangePending to0; failure `:168` only logs. `RestartCaptureSessionImpl:419` first detaches; `DetachCaptureSession:379` sets sessionReady=false. Resource/pool/device failures after that leave no running capture.
- A lost WGC window-mode restart now has no callback to re-arm geometry flags. `stage_watchdogs:93,138` requires sessionReady and excludes windowMode; desktop promotion `host_stage_backend:201` excludes windowMode. `stage_stream_active` retries only on a false→true stream edge with `idleDetached`, not because current sessionReady became false.
- Similar same-backend restart failures in frozen-ring paths can leave no automatic attempt when active==requested. Desktop backend demotion may incidentally provide another retry, so do not generalize every failure to permanent stop.
- Fix: capture lifecycle states (Running/RestartPending/RetryBackoff/Unavailable), retain failed intent and scheduled backoff independent of future callbacks; report recoverable failure separately from established target.
- Regression: one transient CreateStaging/CreateCaptureSession failure during window resize, no later user/source events → next bounded retry restores current target. Repeated failure capped/backoff, stop remains responsive.

### HN06 / P1 conditional dead encoder — confirmed geometry hides ApplyTarget failure

- `host_encoder_manager.cpp:123` updates encodeSourceW/H, `:134-142` calls ApplyTarget only inside success branch and returns void on failure. ApplyTarget first destroys codec and can fail initialize (`:54-63`). `restart_capture_session` nevertheless returns true (`host_loop_helpers.cpp:238-242`), as does selection flow after the call at `:521`.
- The capture may run while codec remains unstarted; per-frame encode then returns false, counted only, no reinit. Since encodeSourceW/H already match, automatic per-frame refit condition is false; lost initialize will not retry itself.
- Fix: propagate failure or make apply transactional (prepare replacement codec before commit); a capture-ready state cannot imply encoder-ready. Unify all callers with existing explicit failure handling in regular refit/runtime tune/ABR.
- Regression: force encoder initialization failure during confirmed geometry; caller must report failure/retry or keep old working codec, never declare restart success and silently discard every subsequent frame.

### HN07 / P2 fail-closed only for AU, not reference chain

- `host_epoch_gate.hpp:105-151`: after current IDR opens the gate, auEpoch0/future is dropped but does not set awaitingKey. Next current P returns Emit at `:113`. Emit caller's unknown case (`host_stage_encode_send_h264_au.cpp:170-175`) does not re-force key; seq has not yet been consumed.
- If the untracked dropped AU was a current-chain reference P, its dependent successor is emitted without a visible seq discontinuity. This matters when implementing A06 overflow unknown-latch as well: invalidating provenance alone must also close the chain.
- Fix: any unknown current stream output invalidates chain until a trustworthy IDR, with explicit bounded reset/recovery; do not silently continue current P after dropped unknown content.
- Regression: AcceptKey(current)→DropUnknown(P)→current P must stay gated; only verified current IDR reopens. Unknown/future key cannot open.

### HN08 / P1 conditional wrong-target capture — window failure falls back to desktop without identity change

- `host_capture_device.cpp:CreateItemForPrimaryMonitor` tries preferredWindow at `:128-130`; if no item, continues through preferredMonitor/primary/foreground/other windows (`:134-209`). It returns only GraphicsCaptureItem, not actual target identity.
- Window-mode callers (`host_startup_graphics.cpp` initial target, `host_loop_helpers.cpp` window selection, `host_capture_session.cpp:443` restart) keep windowMode/targetHwnd/PID set to requested window and accept any nonnull item. Failure to capture one selected window can thus yield unrelated desktop/window pixels labelled as the selected window. Crop calculation is then based on requested HWND against unrelated frame extent.
- Distinct from A16 monitor-choice regression. Trigger is CreateForWindow failing while a fallback source succeeds. No live protected-window repro.
- Fix: fail closed for explicit target; fallback only with explicit desktop policy and update actual source identity as one transaction. Factory return should include resolved type/handle/rect, not just item.
- Regression: force preferred-window create failure + successful desktop create; explicit window request must fail without sending desktop pixels. Desktop fallback, if allowed, must advertise actual identity and dimensions.

## Lower-priority confirmed/code-conditional defects

- **HN09 / P3 clock arithmetic**: `d3d_capture_readback.cpp:32` does QPC ticks×1,000,000 before divide (uint64 overflow), unlike safe quotient/remainder `time_utils.hpp`. Wrap interval is `2^64 / frequency / 1e6` seconds: 10MHz ≈21.35days; 3GHz ≈1.71hours. Cross-wrap elapsed subtraction can underflow, oldest-age clamp can hide pending time. Use common helper; tests around quotient wrap. Not assigned as field cause.
- **HN10 / P2 opt-in config unit mismatch**: `mf_h264_codec.cpp:1962` reads `REMOTE60_NATIVE_H264_ASYNC_POLL_SLEEP_US`; `:1973` passes the integer directly to Windows Sleep (milliseconds). Setting1000 means1second, not1ms, and deadline is checked only after the sleep. Default0 avoids it. Rename asMS or chrono microseconds and clamp to remaining deadline; test e.g1000us elapsed order with fake wait seam.
- **HN11 / P2 opt-in stale guards**: `host_stage_gate_static.cpp:141-143` returns for stale input before advancing stats.lastVersionSent (`:149`); `stage_pop_frame:258-267` immediately selects same version again, and realWaiting blocks kick. With guard enabled and no newer source, tight repeated stale-drop loop. `guardStaleEncoded` separately drops P before seq assignment without IDR chain reset (`host_stage_encode_send_h264_au.cpp:237-266`), allowing dependent P onward. All bundled profiles inspected set both false, so no 09:59 attribution. Consume/drop input once; output loss must invalidate chain.
- **HN12 / P2 rare startup failure loop**: `mf_h264_codec.cpp:1375-1402` enum loops only stop on MF_E_NO_MORE_TYPES; persistent FAILED(hr) continues through DWORD range. Capture device enumeration (`host_capture_device.cpp:395-406`) similarly only recognizes NOT_FOUND and continues on error. Device/MFT error should fail boundedly, not spin startup indefinitely; main watchdog is started after startup init. Inject persistent E_FAIL and require bounded failure result.

## Architecture / performance improvements (not asserted bugs)

- Separate encoder input admission, output event pumping, and capture arrival. `encode_sample_common` polls only during new input calls, so output arriving after the last call awaits synthetic input; this is the mechanism A10's150ms kick works around. Use async event queue with NeedInput credits/HaveOutput consumption, output wake independent of real pixels; maintain one transform-owner thread and epoch fencing. Validate all current CPU/NV12 backends; no promise marker/drain forces every vendor immediately.
- Replace loose timestamp/synthetic/epoch deques with one input record carrying stable id, target generation, source times, lease handle. Structured output outcome closes HN02/HN04/HN07/A02/A06 as one lifetime design, without giant monolithic MFT refactor.
- `d3d_capture_readback` always copies/maps BGRA even when successful GPU NV12 conversion is available; surface input is encoder-zero-copy, not zero-copy capture pipeline. CPU BGRA is needed for current gating/cache, so optimize only with lazy cache readback or GPU content-change evidence and measure memory bandwidth/power.
- Stage translation units repeat ~80 includes plus tens of reference aliases for a few real operations. Narrow contexts per stage and shared interfaces reduce compile dependencies and make ownership/error paths reviewable. Keep lifecycle changes separate from mechanical include cleanup.
- ABR `AbrInputs.sentFrames` says actual wire, but caller supplies enqueue-side `sender.sentFrames`; async drops/media-epoch drops/synthetic provenance may diverge from actual tx counters. Feed real content tx deltas and actual elapsed window, keep offer/decode/wire rates separate; add profile transitions on absent feedback to existing A07 plan, not a second rate controller.
- MFT API HRESULTs (D3D manager, low-latency flags, force key, begin/start) are mostly ignored. Record requested/accepted/readback capability once per encoder generation, expose unsupported requirements explicitly. This is especially relevant before treating successful initialization as evidence of device linkage or no-B-frame policy.

## Coverage and validation limits (intermediate; final pass pending)

Direct function/body reads: d3d_capture_readback.{hpp,cpp}; capture_cadence_gate.hpp; host_capture_session.{hpp,cpp}; host_capture_device.cpp; host_encoder_manager.{hpp,cpp}; host_epoch_gate.hpp; host_kick.hpp; host_frame_gate.hpp; host_gpu_scaler.hpp; host_bgra_scale.cpp; host_stage_encode_send_h264.cpp; host_stage_encode_send_h264_au.cpp; host_stage_gate_static.cpp; host_stage_pop_frame.cpp; host_stage_geometry.cpp; host_stage_watchdogs.cpp; host_stage_backend.cpp; host_stage_stream_active.cpp; host_stage_pace.cpp; host_stage_time_limit.cpp; host_encoded_sender.cpp; host_sender_queue_policy.hpp; host_abr.hpp; host_watchdog.hpp; host_shutdown.cpp; libs/capture/src/capture_backend_dxgi.{hpp,cpp}; time_utils.hpp.

Targeted caller/contract reads: host_startup_graphics.cpp, host_startup_capture.cpp, host_startup_config.cpp, host_startup.hpp, native_video_host_main.cpp, host_loop_helpers.cpp, host_stage_runtime_tune.cpp, host_stage_stats{,_h264}.cpp, mf_h264_codec.cpp (encoder functions/format helpers; decoder beyond device binding is other reviewer scope), mf_h264_codec.hpp, host_encode_epoch_test.cpp, host_backend_policy_test.cpp, host_bgra_scale_test.cpp. Existing ledgers read: stabilization audit A01–A17, host refactor H01–H28, worklist D3, relevant history#357.

Direct test: only isolated header/model harness above (MSVC Release assertions explicitly enabled). No live hardware stress, no new NAS pull, no MFT fake implementation yet. Build-local untouched. Findings are code-path/model proofs with explicit trigger conditions, not a claim that every path occurs on this PC.
