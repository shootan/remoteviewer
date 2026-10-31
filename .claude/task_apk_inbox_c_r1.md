# task=keepalive-r2(e) / sub=apk-decoder-inbox / stage=C 구현 / rev=r4 (2026-10-30 r3 대체, 검증용 remote#7fsfy22z)

정본 판정: `.claude/codex_apk_inbox_c_impl_go_r1.md` (562af512…, 10-30 정본 hash 갱신본). 이 지시서는 그 판정을 기존 작업용1 task 에 합친 것이다.

- 목표: 설계 r3(`design_apk_decoder_inbox_r2.md` 57d68eb0… r3 보충)의 C — chain-aware bounded inbox(8 frame / 4 MiB, usable IDR + suffix),
  recovery episode(최초 1 + 추가 3, t0/1/3/7 + 마지막 4 s 응답/commit window → exhausted / 명시 retry), owed·coalesce 원인 집합,
  held+accepted descriptor 취소 경계, N1 예약/실제 queue 에서만 KeyDelivered, 21회/2 s detach·RX/control codec 대기 0 유지.
- 기준: 계측 79b5c25 + 설계 r3. 별도 branch/worktree 로 B 산출물(plain-debug-r5 32bb788f 등)과 기존 원문·snapshot 을 덮지 않는다.
- 금지: wire/limiter/transport·보안·자동 재탈취·사용자 설정·상한 임의 변경. presentation 생략/ABR 확대가 필요하면 범위를 source packet 에 명시. PC 0.2.164 에 합치지 않음.
- 자체 대조(판정 "C 제출과 검증 조건 1" 전체): lossless steady recovery 0, 단발 gap bounded 회복, IDR 재손실·gap·재정렬·중복·wrap, bytes/frame/age·4 MiB 초과,
  held/accepted 취소 경합·부분 무효화, 실제 Android binding 의 owner/park/session 변경, t0/1/3/7·exhausted·retry, 느린/실패 codec, 지속 overload 의 control/ACK·메모리 bound.
  새 핵심 경계는 이전 source/mutant 로 실패함을 보인다. **요청을 막고 영상을 멈춰 PASS 하는 대조 금지.** NDK 4 ABI, plain/trace·release 경계 고정.
- 제출: 고정 commit + diff/SUMS + 자체 대조 결과 + **검증용 독립 Android 120 s 관측 계획(동일 SH-off·fixture·metric 정의, run/hash)** 을 한 패킷으로. 실기(AVD) 실행은 하지 않는다 — 새 C 실기 GO 는 SOURCE 뒤.
- 순서: old-apk-lane r7b(seq16085, PC 0.2.164 필수 경로)를 먼저 끝내고 C 를 시작한다. 슬롯은 쓰지 않는다.

## r2 (2026-10-30) — SOURCE NEEDS_CHANGES 보수 (정본 `.claude/codex_apk_inbox_c_src_r1.md` eb37e192…)
r1 의 범위·금지·제출 조건은 그대로이고 아래를 같은 task 에서 보수한다. 근거·재현·관련 대조는 정본에 있다.
- N1(P1): MarkLoss 가 이미 큐에 들어간 손실 뒤 delta prefix 를 막지 못함(N1-a), pending IDR 이 실제 commit 으로 복구한 뒤에도 tailGap=1 이 남아 정상 delta 를 버리고 새 episode 를 엶(N1-b, 제품 sink 재현). 큐 내부·pending·in-flight 의 chain 상태를 하나로 맞춘다.
- N2(P1): action dequeue 뒤 ledger take 직전에 owner 변경 + 새 pick 으로 descriptor 가 지워지면 legacy 로 오인해 stale key 를 1회 송신함(제품 session + loopback 재현). RX publish / held snapshot 경합도 같은 정합 범위.
- N3(P2): 30회 중복/역순 뒤 오래된 delta 를 허용하는 자동 resync 제거. 명시 reset 으로만 순서를 바꾼다.
- §8 = (a): owed 는 기존 8회 limiter 채택 상한 유지, 소진 뒤 새 episode 4회 추가 없음. sink 에 token/epoch 별 covered-exhausted 와 최종 응답 window 를 전달. 늦은 IDR / 명시 새 선택 회복은 유지. owedAttempts_ 는 Request().queued 기준이라 "실제 wire 8회" 라고 쓰지 않는다.
- 패킷 문구 정정: B-only batch 는 "미검토" 가 아니라 한정 검토·허용됨.
- 각 보수에 이전 source 로 실패하는 음성 대조. 제출은 고정 commit·diff·SUMS·자체 대조 한 패킷. C Android 실기는 보수본 SOURCE 뒤(검증용 독립 clean build hash 로 offSha 고정 허용).
- 순서: old-apk r7e(seq16113, 작은 보수)를 먼저 끝내고 이 r2 를 진행한다.

## r3 (2026-10-30) — C r2 SOURCE NEEDS_CHANGES, N2 잔여 1건 (정본 `.claude/codex_apk_inbox_c_src_r2.md`)
d50415e 에서 N1-a/b·N3 는 닫혔고 §8(a) 유지. 제품 session 시험 22/22 는 Codex 재빌드로 확인됨.
- N2 잔여: Take 와 RequestKey 사이 producer 선점으로 과거 descriptor 가 나중에 게시될 수 있는데, DecoderKeyRequest::Merge 가 들어온 sinkEpoch/episodeId 를 무조건 최신으로 본다.
  재현(실제 sink 발급 descriptor + Merge): (epoch2 episode3 에 늦은 episode2), (epoch3 에 늦은 epoch2) 둘 다 validNew=1 → validMerged=0, 최신 유효 원인이 사라짐.
- 보수: 최신 발급 원인 보존, 과거 원인만 제외(과거 bit 를 최신 epoch 로 승격 금지), pipeline/legacy 원인 보존. held version / 원자 publish 는 유지.
- 대조: held/accepted 지점의 늦은 게시(역행 epoch/episode) 음성 대조 + 관련 회귀만. 전체 14×5 반복은 요구 아님.
- 제출: 고정 commit·diff·SUMS·대조 한 패킷. C Android 120 s 는 보수 SOURCE 뒤(검증용 독립 빌드는 새 commit 으로 다시 함).
- 순서: PC old-apk lane 관련 요청이 오면 그것이 먼저.

## r4 (2026-10-30) — C3 채택 HOLD, 유효 prefix·최신성 설계 GO (정본 `.claude/codex_apk_inbox_prefix_design_go_r1.md`)
B→C pair(verify_c2/RESULT_bc_pair_verify.md): 요청 −43 %, 같은 구간 출력 −9.3 %(16.12 → 14.63, Host 공급 같음). 인과 회귀 확정은 아님. 추가 반복 0.
C 887 = 전체 폐기 77회 693 + tail 2 + gap 187 + trim 5 (Codex 확인).
- 할 일: 유효 prefix·최신성·pending/codec backpressure 를 함께 다루는 **설계 + 실행 가능한 소형 대역 원형**.
- 완료 기준(정본이 우선):
  - 현재 codec/stream/epoch·inflight/pending 참조가 실제로 유효한지 증명.
  - 유한 age 추천값과 검사·만료 지점, local residence 와 표시 latency 구분.
  - buffer 가 없을 때 후속 delta Pop 폐기와 hot loop 방지.
  - cap 8 / 4 MiB, N1/N2/N3/§8 예산, RX/control 비차단, detach/terminal 계약 유지.
  - B 의 높은 out 은 참조 정확성 golden 이 아님 — 깨진 delta 로 fps 만 맞추기 금지.
- 증명: 같은 schedule 에서 현재 C 와 추천안의 burst · buffer 없음 · age 만료 · 새 IDR/reset 경합 · 지속 과부하 → 현재 frame 회복을 실제 정책/sink + 독립 reference oracle 로.
- 제출: 고정 설계/decision 표 · source/diff/commit · 대조/한계 한 패킷. 현재 2b492ef·APK·원문 보존, 새 AVD 실행·출하 default 변경 0, 전량 14×5·새 ABR/wire 확대 0.
- 순서: old-apk-lane 구·신 참조 준비(task_old_apk_ref_r1.md, PC blocker)가 먼저. 큰 빌드는 검증용 실기와 겹치지 않게.
