# Viewer / Android receive-path full audit — 2026-09-08

Reviewer: Codex subagent viewer_audit. Baseline HEAD c09e4d0. Read-only product audit; no product edits, builds, tests, process stops, or commits. This note is untracked. Existing A01–A17 in docs/stabilization_audit_2026-09-08.md were consulted and are not counted again as new findings. Below are current-source control-flow findings, not proof of the 09:59 Windows outage cause. Android findings cannot explain a Windows-only incident.

## Confirmed code defects / condition-triggered defects

### V01 — P1 conditional: Android held-output release failure reaches a null codec
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp:786-794`: if a held output becomes due, `ReleaseHeldOutputLocked(nowUs)` runs, then the function unconditionally calls `AMediaCodec_dequeueOutputBuffer(codec_,...)`.
- `:737-740` calls `ResetCodecLocked` when release fails; `:643-645` stops/deletes the codec and assigns nullptr. The *immediate* release branch correctly checks codec_ at `:818`; the held branch does not.
- Trigger: a scheduled held output exists and `AMediaCodec_releaseOutputBufferAtTime` reports an error (surface/device/codec error). Both the 3ms output pump and UI/native polling reach DrainOutputLocked. This is invalid NDK handle use in an error-recovery branch; likely native crash rather than controlled recovery. Error frequency/device behavior unmeasured.
- Fix: return/status propagation after any release that can destroy codec; make reset a typed outcome and only continue draining when the same codec instance is alive.
- Regression: fake NDK release fails specifically on the held branch; assert stop/delete once and zero subsequent dequeue calls against that instance; compare immediate branch. Device lifecycle test separately.

### V02 — P1 conditional: Android full/wedged codec is classified as an idle source
- `MainActivity.kt:3551-3559` reads debug `in`, and any unchanged input count resets the 5s stall/recovery clock as `nothingArriving`.
- That `in` is *successful codec input enqueue*, incremented in `android_video_decoder.cpp:582`; not datagram/assembled-frame arrivals. `TryQueueFrameLocked:548-550` returns false when the codec has no input buffer. Fresh network frames can keep arriving while inCount never changes.
- `OnEncodedH264Frame:210-229` keeps one pending frame, drops/replaces the rest, and asks for a keyframe. `native_video_client_session.cpp:610-615` sends a keyframe request but does not reset the decoder or close reference admission for this sink failure. A codec stuck with no input/output buffers cannot accept that requested IDR either.
- Consequence: a saturated/wedged MediaCodec with incoming video can evade the watchdog indefinitely, with bounded-per-request but repeated IDR traffic. Normal empty-source protection is desirable but the observed counter is the wrong boundary.
- Fix: separate last datagram / assembled AU / accepted input / released output clocks; codec-progress watchdog must use incoming AU evidence and bounded reset/recreate escalation. Do not turn ordinary static screens into failure.
- Regression: input queue returns TRY_AGAIN forever while complete AUs keep arriving, then verify bounded decoder rebuild; no incoming AUs must remain classified idle.

### V03 — P2 confirmed gap: Android receive path did not acquire Windows NACK ordering/recovery changes
- Android CMake includes `native_video_client_session.cpp`; its `VideoReceiveMain:507-620` creates a legacy assembler and never calls ConfigureInOrderHold or PopDelivery.
- `native_video_client_shared_core.cpp:963` immediately delivers a later complete AU and DeliverAssembly `:654-658` removes older incomplete AUs. At 60fps, a following AU can complete before the scheduler's 25ms hole grace. Thus missing P chunks can be discarded before the first NACK. This is distinct from Windows A03 tailGrace/hold collision.
- `native_video_client_session.cpp:575-580` requests IDR/reset on every reported gap even if the completed AU itself is an IDR (Windows explicitly avoids this). `:543-552` timeout drives NACK/control only; there is no bounded key-wait retry timer after an unanswered request and no more completed AUs.
- Shared scheduler alone does not yield behavioral parity. Previous Windows recovery PASS is not Android coverage. This deficiency was mentioned as an Android follow-up in earlier history; it remains in current product code.
- Fix: share receive recovery/assembly episode policy instead of only NACK scheduling. Preserve sink/decoder abstraction; add real ClientSessionController receive-path tests, not Windows run_udp tests labelled generic.
- Regression: high-fps P hole; complete gap IDR causes zero extra request; lost recovery IDR + no subsequent frames triggers bounded retry; old peer disables NACK.

### V04 — P2 confirmed: Android presentation metrics can be consumed then never sent
- `native_video_client_session.cpp:327` constructs `ClientControlMetricsSnapshot metrics{}` on every loop. `:333-348` destructively drains presentation stats, sets updatedQpcUs and advances static lastPresentReportUs.
- `ClientControlScheduler::NextAction` prioritizes due Ping (`shared_core.cpp:448-457`), stream state and target requests before Metrics (`:540-549`). If one wins on the collection iteration, the local snapshot dies; next iteration has zero metrics and the next drain is delayed a second.
- Trigger: metrics interval and ping/selection due on same loop. No packet loss required. A host can receive intermittent/missing presentation feedback despite healthy decoder stats. More serious than a cosmetic log drop because feedback is intended for pacing/ABR observation.
- Fix: retain last unsent snapshot in session state until scheduler consumes its version; reset per session, not function-static. Distinguish collecting and transmitting timestamps.
- Regression: force ping and stats due at the same virtual instant; execute ping, then next scheduler iteration must emit the unchanged presentation sample exactly once.

### V05 — P2 conditional: Android codec resets do not establish a new reference-chain gate
- `SetSurface:147-165` destroys/recreates the decoder without informing `ClientSessionController`'s waitForKeyframe flag. `TryQueueFrameLocked:555-579` error/oversize/queue-failure branches also reset codec but do not set decoderKeyframeRequest_.
- Session wait flag is cleared at input key arrival (`native_video_client_session.cpp:609`), before sink acceptance, and ConsumeDecoderKeyframeRequest is only a request (`:613-615`), not a reset/gate transition.
- With known CSD and a recreated surface, the next P can be submitted to a new reference-empty decoder while the session believes the chain is healthy. With an oversized first/last IDR on a sparse source, the sink can return without a recovery request and no next frame drives any correction.
- Fix: explicit sink outcomes Accepted / Backpressured / DroppedReference / ResetNeedsKey; decoder reset always transitions key admission. Request recovery on control loop clock, not only after next successful AU delivery.
- Regression: surface clear/rebind followed by P; oversize IDR then quiet source; queueInputBuffer failure on P. Verify no P accepted before a new IDR and one bounded recovery episode.

### V06 — P2 confirmed conditional: Windows decoded output gets the current input's seq/key/generation
- `mf_h264_codec.cpp:2453-2457` explicitly supports MF_E_NOTACCEPTING by draining **previous** outputs before current input is accepted; subsequent `drain_outputs` can append multiple outputs. No 1-input/1-immediate-output guarantee exists in this API implementation.
- `DecodedFrameNv12` carries output sample time and synthetic flag only (`mf_h264_codec.hpp:29-47`; FIFO mapping `cpp:2391-2428`). `viewer_video_receiver_frame.cpp:269-277` correctly selects the last decoded picture and its timestamp/synthetic provenance.
- However publication `:327-343` stamps `seq=h.seq`, `key=keyFrame`, generation/encode/send times from the *current packet*. The old picture can be attributed to a newer IDR/seq. `viewer_present.cpp:125-140,310-328` uses these fields for per-key telemetry; the recovery harness uses published key/seq to certify a specific IDR shown. Hence a test can certify the current IDR while an older picture was published on that call.
- Existing selection epoch reset and same-host monotonic streamGeneration mitigate cross-target pixels; a wrong-generation field on ordinary same-generation delayed output is not itself proof of target leakage. Primary confirmed impact is identity/IDR display evidence; target-safety impact remains conditional.
- Fix: carry one input metadata record through decoder admission/output mapping (seq, key, gen, times, synthetic), keyed by returned timestamp where needed; expose unknown rather than borrow current header. Keep codec acceptance versus displayed-picture success separate.
- Regression: fake decoder first holds IDR A, on P B returns A; then holds P C, on IDR D returns C only. Published key/seq must identify A/C, and D-shown assertion must remain false until D output.

### V07 — P1 conditional: Windows GDI fallback is incompatible with the surviving flip swapchain
- `viewer_nv12_renderer.hpp:108-118` prefers FLIP_DISCARD; `viewer_present.cpp:186-214` falls back to StretchDIBits on the same HWND after render failure without releasing that flip swapchain.
- Existing `release_swapchain()` comment in the same renderer `:53-72` explains exactly this problem for the picker: flip content composites over same-HWND GDI. Picker path releases it; fallback path does not.
- Microsoft primary reference: https://devblogs.microsoft.com/directx/dxgi-flip-model/ states same-HWND multi-API layering is not provided; https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model .
- `presented=true` is set without checking StretchDIBits return (`viewer_present.cpp:208-210`, raw branch `:224-227`), so lastPresentedVersion/capture and success counts advance even if fallback pixels never became visible. This hides a renderer failure and misleads gate/diagnostics. It is **not proof** this occurred in 09:59 (field render failures were not established).
- Fix: real rendering-mode transition (unbind/release swapchain before GDI), validate the blit return, and re-create D3D on a controlled retry. Better separate video HWND/overlay compositor architecture.
- Regression: force render failure after one successful flip Present; verify visible new fallback pixels by screenshot, not just counters, and failed GDI must not advance presented version.

### V08 — P2 confirmed conditional: renderer/device loss never invalidates the 'ready' renderer
- `Nv12D3dRenderer::draw:317-322` returns Present failure but does not reset/mark device state. `viewer_present.cpp:158-178` only initializes when ready=false; a formerly successful renderer stays ready after DEVICE_REMOVED/RESET. Repeated calls use the same lost device indefinitely.
- With CPU output it repeatedly attempts V07 fallback; with DXGI surface output there is no CPU bytes fallback. Existing decoder-only rebuild still uses the same shared D3D device in opt-in mode, so it does not constitute device recovery.
- Fix: classify HRESULT, coordinate UI-owned swapchain/device reconstruction with decoder-surface lifetime, or temporarily switch to CPU/WARP. Preserve bounded teardown; don't destroy shared device under an active decode.
- Regression: injected device-removed Present/Map plus subsequent frames; one coordinated recovery and later valid frame, no endless failed-present loop.

### V09 — P2 conditional: DXGI surface SRV type does not match shader for array decoder textures
- `viewer_nv12_renderer.hpp:343-361` builds TEXTURE2DARRAY SRVs when decoder texture.ArraySize > 1; the shader declares `Texture2D texY/texUV` at `:133-134` and samples float2 coordinates.
- A D3D decoder commonly allocates surface arrays, and this explicit branch intends to support them, but selecting one array slice in the view does not change its resource dimension to Texture2D. This is an opt-in decode-surface defect, not the default CPU-copy path.
- Fix: Texture2DArray shader variant with slice coordinate 0 for one-slice view, or copy selected subresource into standalone textures. Validate with D3D debug layer and an ArraySize>1 fixture.
- Regression: artificial two-slice NV12 texture with distinct colors, select slice1 and verify displayed pixels; default byte path unchanged. Device-specific field frequency unmeasured.

### V10 — P2 confirmed: TCP frame payload allocations and raw pixel extents are unchecked
- `viewer_video_receiver.cpp:423` / `:513` allocate vector using peer uint32 payloadSize with no cap (UDP has explicit max). Raw branch publishes peer width/height/stride regardless of payload consistency at `:445-466`.
- `viewer_present.cpp:214-227` passes the raw vector pointer to StretchDIBits as width*height BGRA, ignoring stored stride and vector byte length. A short nonempty raw payload plus large dimensions violates the source-buffer contract; huge payload header can trigger allocation failure/process termination before body receipt.
- Trigger requires malformed/hostile peer or wire/header corruption on optional TCP/raw path; not claimed for current H264/UDP default.
- Fix: shared bounded frame-header validator before allocation, checked arithmetic, exact expected raw stride/size, maximum geometry; reject before publish.
- Regression: max uint32 payload, zero/odd/extreme geometry, short BGRA data, padded stride; bounded memory/no renderer invocation on malformed frame.

### V11 — P2 confirmed gap: TCP select only bounds the first byte, not frame reads or recovery clock
- `viewer_video_receiver.cpp:410-414` select(200ms) precedes blocking recv_all. Once even one header byte arrives, partial header/body at `:414,422,424,512,514` can block indefinitely; no per-frame deadline.
- run_tcp never calls fg.tick (contrast run_udp). Thus a decoder keyframe wait on a quiet TCP source does not get the newly added timer even though FrameGate is shared. UI --seconds/close can terminate eventually via socket close; that is not session recovery.
- Fix: incremental bounded receive state machine / deadlines, clock tick independent of payload completion. Distinguish quiet idle from partial-progress stall.
- Regression: fake TCP host sends partial header or declared body then keeps connection open; loop remains responsive and times out the partial frame; quiet keyframe wait retries boundedly.

### V12 — P2 confirmed: non-move input queue is unbounded, including bulk text
- `native_video_client_shared_core.cpp:75-93` nominal kMaxInputQueueSize only bounds disposable mouse moves; if no move exists, every other message is accepted beyond the cap. `enqueue_control_input_text:147-161` loops over arbitrary text size creating message records.
- A long paste or sustained button/key input while each control exchange waits for an ACK can build an unbounded backlog. After link delay clears, stale actions execute long after the user intended them. The preserve-key-up motivation is correct but 'temporarily grow' has no time/byte bound or backpressure contract.
- Fix: bounded bytes/age with explicit admission failure; preserve release edges via separate emergency-release state rather than unrestricted queue growth; batch text and allow cancellation. Do not drop arbitrary key-up to obtain a cap.
- Regression: blocked control peer + large text / repeat keys / release edges: bounded memory, visible rejection/cancel, no stuck key and no stale replay after new target.

### V13 — P2 robustness: UDP control reassembly has no aggregate or lifetime bounds
- `udp_control_channel.cpp:176-192` limits each message to8MiB, but inserts rxPending_[messageSeq] for arbitrary new seq and never bounds count/total bytes. Tick `:279-292` repeatedly NACKs every pending message; no expiry, and closed state does not stop OnPacket/Tick processing.
- Unique incomplete seqs from the accepted UDP peer can grow memory/NACK work without limit. Normal stop-and-wait sender does not generate this pattern, so this is malformed-peer defense, not a presumed normal-network cause.
- Fragment validation also checks bounds/count only, not disjoint full coverage: different fragIndex values may overlap the same bytes and still haveCount==fragCount; corrupted control messages are acknowledged as complete.
- Fix: receive-window + aggregate bytes + monotonic expiry; validate fragment geometry/coverage and permitted seq transition; drop/close on protocol violation without allocating unbounded slots.
- Regression: many incomplete messageSeqs, overlap/hole fragments, duplicate controls, delayed legitimate retransmit; bounded allocations and no fabricated complete payload.

### V14 — P1 conditional: non-video UDP traffic can starve the supposedly clock-driven recovery work
- `viewer_video_receiver.cpp:207-212` runs drain/NACK/fg.tick on recv timeout, and `:330-333` runs them after the video path. But claimed control packets (`:228-230`), valid cursor packets (`:235-255`), and short/other packets immediately continue.
- If those packets keep arriving faster than the25ms recv timeout while no video path reaches the epilogue, neither condition runs. Pending assembly expiry/NACK/keyframe retry is postponed for as long as that traffic continues. `udpControl.Tick` itself runs at `:221`, so control remains alive and this is not simply a completely dead socket.
- Concrete legitimate condition: continuous control input/ACK traffic (e.g. low-RTT mouse movement) at >40 packets/s plus no completed video, or control+cursor mixed arrivals. **Host cursor alone is capped33ms/30Hz (`host_loop_helpers.cpp:262`) and does not by itself prove timeout starvation.**
- This differs from A04: even an already-waiting keyframe whose fg.tick should recover can be starved. The09:59 incident had no datagrams at all, so this is not assigned as that event's cause.
- Fix: run time-based receive maintenance in a common iteration boundary that all packet kinds traverse, with bounded work; keep parsing before admission. Use a monotonic next-maintenance deadline rather than assuming absence of datagrams means elapsed time.
- Regression: actual run_udp fake host: incomplete IDR/no further video + well-formed control ACK/data every5ms for2s; NACK retries and reason7 must still meet bounds, control remains responsive. Add unknown/short packet paths and timer-pressure limits.

## Follow-up improvements / limitations (not additional high-severity bugs)
- Renderer ensure_rtv `:193-196` releases the owning RTV but not an OM binding before ResizeBuffers. **Do not claim normal flip resize always fails**: successful flip Present unbinds buffer0. The fallback DISCARD path and failed-Present-before-resize need explicit unbind. Primary refs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers and https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present .
- Windows stats `flush_stats_if_due:130` increments deadline by1s, with accumulators only updated on frames. Long idle then burst produces several very short windows labelled as1s. Use elapsed-duration denominators and explicit zero-traffic clock publication (extends known A12).
- `FrameGate::note_decode_ok` clears hard-failure streak even for accepted/no-output; `note_decode_empty` only flushes and needs another packet to notice elapsed300ms. Permanently-successful-but-empty MFT needs a separate output-progress/rebuild policy. A last accepted-but-unrendered IDR can clear wait then leave no timer. This extends known quiet-recovery A04 to decoder output and requires a fake decoder contract test.
- `FrameGate::admit:183-186` unknown/reversed stamps on a *second gap while pending* clear pending via the legacy branch, despite comments saying unknown cannot end it. Existing tests cover unknown initial resume, not unknown gap inside an active episode. Treat as policy inconsistency requiring decision, not assume all unknown stamps should change behavior.
- UDP control sequence comparisons use ordinary <= and nextTxSeq++ rather than serial arithmetic; wrap breaks very long-lived high-message sessions. Lower priority than bounded resources and recovery.
- Raw TCP skips the H264-only selection epoch/reveal gate. Either state raw viewer is unsupported for picker mode or share target admission independently of codec.
- Android InputConnection treats deleteSurroundingText UTF16 lengths as code-point/backspace counts, ignores afterLength/newCursorPosition, and fakes setComposingRegion success. Unicode/selection IME compatibility needs a real local editable-state adapter; no claim about a particular host editor's backspace semantics without integration.

## Coverage / evidence boundaries
Read current full bodies of: viewer_video_receiver.cpp, viewer_video_receiver_frame.cpp, viewer_frame_gate.cpp, viewer_selection_gate.cpp, viewer_control_client.cpp, viewer_input_forward.cpp, viewer_layout.cpp, viewer_present.cpp, viewer_nv12_renderer.hpp, viewer_startup.cpp, viewer_shutdown.cpp, udp_control_channel.cpp, udp_video_nack.hpp, native_video_client_tcp_control.cpp, native_video_client_shared_core.cpp (assembler/input/scheduler/panel), native_video_client_session.cpp; relevant declarations/state/transport wrappers. Read Android android_video_decoder.cpp entirety, native_bridge.cpp, ImeCaptureView.kt, ViewerVirtualMouse.kt, ViewerKeyPanel.kt; MainActivity lifecycle/surface/control/recovery/connect paths. Read selected regression sections of frame_gate/recovery/shared_core/control tests, not every test body. No claim that every repository file was independently read by this subagent.

Server/Android directory auth/log persistence delegated to sibling server_audit, Windows shell/WebView/installer/secure_input/macro to parent. Existing A01–A17 and prior F findings retained, not counted anew. No new field logs, Android device, graphics failure injection, or MFT run was performed here. Findings above identify exact reachable conditions and requested tests; code analysis is distinguished from measured reproduction.
