# task keepalive-r2 — 최근 호스트 3대 상시 유지 (APK + PC 클라이언트) 구현 정본 (검증용 remote#wwml7h91, 2026-10-22)

**이 파일이 유일한 정본이다. 끝까지 읽고 CLAUDE.md 대로.** 구속 계약: `.claude/codex_keepalive_r2.md`(수정 조건부 설계 GO) — 여기 요약과 다르면 Codex 파일이 우선.
사용자 지시(10-22): "가장 마지막 접속 기록 있는 host pc 최대 3대, 켜져 있는 동안 연결 상시 유지. apk 도 pc 클라도."
**미결(사용자 답 대기)**: 휴대폰이 홈 이동/화면 꺼짐에도 유지할지 vs 화면에 보일 때만 유지할지(Codex 가 사용자에게 질문함). 답 전에는 onStop 전체 종료를 최종으로 하지 않되, 아래 독립 작업은 진행.

## 단계 (Codex §7)
① **host/admission 격리 계약·spike** — 담당 작업용1 (shared native/Host 단일 writer)
② shared control/Host 기반 + APK — ①의 Codex 확정 뒤
③ PC managed Viewer(셸이 host 별 Viewer 프로세스 관리, 합계 3) — ② 뒤
④ 양 기기·호스트 교차 독립 UI·출하 격리 → ⑤ 다음 릴리스(후보 번호 APK 0.2.32/vc31, PC 0.2.163 — 검토 전 버전 파일 수정 금지)
병행 가능: **APK 세션 재통합 준비** — 담당 작업용2(아래 B). shared control/Host 수명·권한 코드는 작업용1 만 쓴다.

## A. (작업용1) host/admission 격리 spike — Codex §5
목표: 호스트에 **복수의 인증된 제어 전용 대기 entry**, 영상·입력 소유자(active)는 1개. 자동 preconnect/heartbeat/reconnect 는 active 교체·입력·선택·clipboard·서비스 작업 권한 없음. 사용자 명시 activation 만 기존 takeover 규칙으로 active 교체.
1. 현 소스 근거 고정: host_session.hpp 단일 control channel/peer/streamControlActive/epoch, host_startup_control.cpp:497~522 AdmitHello → peer·epoch 교체, :531 이후 adopted peer 만 허용. pause ≠ 다중 admission.
2. 계약 초안(필요 시 capability 협상 wire): 대기 admission vs activation 구분, session/device/connection-generation 식별·재전송 멱등, peer 이동·relay 공유 주소에서 entry 구별 / entry 별 control ACK·retransmit·liveness·lease 와 active input/미디어 generation, 대기 entry 의 Ping/Pause/Close 가 active 의 streamControlActive·held input·LockCred·파일·clipboard 를 못 건드리는 host gate / 제어권 전환 시 held input release·옛 epoch 무효화·새 IDR·선택 경계, 이전 active 지연 패킷 반례 / host 전체·기기별 유한 entry 상한·Busy·회수 / **구 Host 에는 자동 preconnect 를 보내지 않음** — 지원 여부를 active 를 바꾸는 Hello 로만 알 수 있으면 그것부터 해결.
3. 제한된 spike: 위 계약의 최소 구현(또는 기존 프로토콜로 가능하다는 근거)과 **active 무간섭 실행 근거**(시험 host + 시험 클라이언트 2개로 대기 공존 → 한쪽 activation → 다른쪽 heartbeat/재접속이 active 를 안 건드림 → 명시 takeover), direct 와 relay 모의 경로. TCP accept 수만 늘리거나 auto-retry 를 끄는 식 금지.
4. 결과를 한 고정 후보로 취합 → 보고 → 검증용 독립 → Codex 확정. 그 전에 Hello 기반 자동 3대 재접속을 출하 경로에 넣지 않는다.
worktree: main 82b3d14 에서 새 worktree·브랜치(예 `C:/work/gnlink-wt/spike-host-admission`, `spike/host-admission`). 보고 `.claude/report_keepalive_r2_host_spike.md`.

## B. (작업용2) APK 세션 재통합 준비 — Codex §3
1. 새 worktree·브랜치(예 `C:/work/gnlink-wt/feat-keepalive-r2-apk`, `feat/keepalive-r2-apk`) = main **82b3d14**. `feat/keepalive-multi a2e9686` 을 **정상 3-way merge**(실제 merge-base eedd207; `5063cd9..a2e9686` diff 를 패치로 쓰면 prelogin/LK/DW/hello/SH 를 지우는 모양이 되므로 금지). 원본 branch·worktree(`C:/work/gnlink-wt/feat-keepalive-multi`)·evidence 보존.
2. 반드시 보존: SH metadata-before-buffer·surface 별 BLOCKED/late release·source-size/crop·설정, shared punch/hello cadence·full-batch·오류 분류, entry handle/incarnation·entry 별 gate·callback binding·active decoder 1·클립보드 r3. 충돌·수동 해소를 한 번에 정리.
3. 정책 준비(Host 무관 클라이언트 부분만): 5분 park 만료 제거 = **명시적 unlimited 정책**(0 으로 즉시 만료 금지), transport liveness·인증·lease 수명은 유지. **새 preconnect entry 는 보여준 적 없어도 정상 parked**(SessionTable `kUnshownGraceMs=10000`/never_shown 정리, BeginPark 가 Active 만 받는 경로 수정 — 자동 대기 admission 과 사용자 activation 구분, 자동 연결이 decoder bind·다른 active 변경 0). 최근 host 3대의 계정별 영속 저장 신규(directory origin + account stable ID + hostId 키, token/IP/표시명 키 금지, 성공 접속·실제 재사용만 순서 갱신). 개별 '연결 해제' 시 그 PC 자동 유지 억제(앱/로그인 수명 동안), 로그아웃은 전부 무효화. host 별 backoff(1/2/4/8/16/30 s + jitter), 자동 connect 동시 1, 수동 탭 우선.
4. **자동 preconnect 를 실제 Host 에 보내는 배선은 A 의 Codex 확정 전까지 꺼 둔다**(구 Host 탈취 금지). 순수/시험 host 로 검증 가능한 범위까지.
5. 시험: 재통합 회귀(SH 핵심·hello·클립보드 r3·단일 decoder), 정책 단위(LRU·unlimited·never_shown 제외·해제 억제·backoff), AVD 는 fail-closed 시험 host 로.
보고 `.claude/report_keepalive_r2_apk_prep.md`.

## 10-26 갱신 (R2 = PC 0.2.164 + APK 0.2.33, 상위 계획 `plan_rel_0163_2026-10-26.md`)
- 폰 기본(Codex `codex_rel_0163_r1.md` §5): **앱 화면에 보이는 동안만 유지**(onStop 해제, 구성 변경 예외, 전면 복귀 즉시 preconnect). 사용자 미답을 동의로 간주하지 않으며 UI·안내에 범위 명시. PC 는 셸 프로세스 수명 동안.
- A spike 고정 `spike/host-admission-r2` **9f1fac1**(569059f 위) → Codex `codex_keepalive_r2_spike_r1.md`: admission/activation 분리 GO, **SOURCE NEEDS_CHANGES S1~S4**(S1 공개 ID/gen 만으로 임의 token·주소에 secret 발급/만료 우회, S2 active 에서 낮은 gen/0 수용 → 늦은 재탈취, S3 실제 sessionEnd/table owner 미연결·RNG 실패 무시, S4 shared relay parked control gate·mocked 승인 전 lastActivated 반례 — 원문).
- 확정: 실 relay = **클라이언트별 lease + 별도 host-facing 소켓(ⓐ)** — latest-token 1개·49 B Hello 전용 bind/provisional·역방향/NAT 경로 포함, 단순 standby demux(ⓑ) 제외. UDP **311~316** 배정, 8 entries/20 s/status 0~3 GO(104 B·activationAck 의 entry+op 상관은 미동결). secret = 기존 UDP 평문 bearer 수준 조건부(기밀성 주장 금지, '일회용 cap 과 같은 수명' 설명 정정).
- ② shared/API·APK 준비는 격리 조건부 GO. **Host 보수 + 검증용 독립 확인 전까지 자동 preconnect 출하 OFF, 운영 relay 변경 없음.**
- 순서: 작업용1 = capture-lifecycle r2 보수 → R1 통합 → S1~S4 보수 + relay ⓐ. 작업용2 = SH r2 → UA-2 APK → KA APK 준비.

## 10-26 r3 판정 (Codex `codex_keepalive_r2_spike_r3.md`, 원문 우선) — 작업용1, APK 0.2.32 뒤
- 고정 `spike/host-admission-r3` 933814f(93b15f6 = docs 1줄, apps/libs 동일). **SOURCE NEEDS_CHANGES N1~N3 한 묶음**:
  - N1: relay 최초 pin 이 같은 host IP 의 첫 datagram 만 믿음(HostPunch.token 0, host 전용 token 대조 없음) → 같은 NAT 의 다른 socket 이 먼저 pin 가능. host 전용 fresh proof/reservation 결합, 이동 뒤 재pin 도 동일. "Host 변경 없음"은 승인 조건 아님.
  - N2-a: replay 인증을 FNV64 로 축약 → crypto digest, fail-closed. N2-b: parked gen 11 뒤 늦은 미사용 cap gen 10 이 entry 교체 가능 → admission 순서/incarnation 보호.
  - N3: standby 8 뒤 legacy bind 상한 검사 없어 9개 → 반대 입장 순서·비파괴 Busy 대조, env 유효 정수 검사.
- 확정: activation Pong = entry/gen/status/Granted(손실·역순·cancel, bare HelloAck 로 완료 금지) GO / additive standby API·relay 상태 조건부 GO / host lease 당 client UDP 1 GO / 구 directory 지원은 부작용 없는 metadata 로.
- 유지: 자동 preconnect 출하 OFF, 운영 NAS 무변경.

## 10-27 ② 판정 취합 (새 Codex tt7fcvky — 원문 우선)
- 범위: `codex_keepalive_r2_scope2.md` — 5건 조건부 GO, ka-r3 cf8fb70..0a5e71d SOURCE NEEDS_CHANGES N1(HelloAck 검증)·N2(runner 수명/경합)·N3(gen durable) → 작업용1 r3 보수(seq 14584).
- merge: `codex_apk0233_merge_src_r1.md` — feat/apk-0.2.33 picker 잠금/token·pending-slot OK, **M1**(같은 연결 park_reuse 의 UA-2 asked=null reset) → A 모델로 옛 park_reuse 경로를 없애거나, 남는 동안 entry 별 pending/request 보존 + 반례.
- **kept 모델 A 확정**: `codex_keepalive_r2_kept_model.md` — 지원 Host 의 kept 대기 = StandbyLease(장기 parked control 세션과 공존 안 함). 명시 directory 연결도 목록 복귀 시 지원·인증되면 standby 로 전환(첫 전환의 새 directory 요청은 숨기지 않음). raw IP·구 Host·prelogin·지원 unknown 은 자동 standby 없음 → 목록 복귀 시 '상시 유지 미지원' 안내 + 세션 종료. max3 = active+standby 합, staging ≤1, 자동 준비는 LRU 순서 불변. 같은 socket 재사용, Host 종료/reader 직렬화, key + incarnation/epoch/request ID 로 늦은 Provide/SessionOver fence. UA-2 backend 는 활성 세션 범위(새 activation 은 Host 기본부터).
- 필수 행동(고정 후보): A/B/C standby3 → A tap → 목록 → 같은 lease standby → B → A / 명시 연결 → 목록 → 첫 전환·도중 다른 tap·취소 / 구 Host·raw IP 미지원 / 다른 기기 사용 중 Host 무간섭·tap 만 takeover / ReleaseAll·화면 이탈·계정 전환 + 늦은 Provide·같은 key 재생성 / 4번째 실패·성공 eviction / direct·시험 relay 에서 세션 끝 후 같은 socket 보존·proof 손실 회수.
- 작업용1 순서: APK E1 scroll_shimmer 준비(최우선, 게시 막힘) → ② r3 보수(N1~N3) → e 배선(A 모델 + M1) → 고정 후보 → 검증용 독립.

## 10-27 ③ PC 설계 판정 (Codex `codex_ka_pc_design_r1.md`, 원문 우선) — 작업용2 설계안 `proposal_ka_pc_r1.md`
- **O1 수정 조건부 GO**: 호스트별 managed Viewer 프로세스가 자기 StandbyLease 소유(--managed-standby: 창·MF·D3D 없음), 탭 = ready lease 로 activation, 목록 = fence → SessionOver → 같은 소켓 standby.
- 결정: ① host key 별 영속 기기 id + **단일 writer 잠금** ② park 때 창·decoder 해제 ③ 로그아웃 시 셸이 그 계정으로 소유한 일반 Viewer 도 종료 ④ 명시 탭도 ready lease 경유 ⑥ 격리 창 렌더 검증(사용자 화면 금지).
- **⑤ NO**: 네트워크 종료 뒤 자동 재activation 금지 — 현 43/control-lost 로 takeover 와 구분 불가. 자동은 standby 복구까지, 재활성은 새 사용자 행동.
- 로그아웃: epoch/pending 무효화를 **종료 대기보다 먼저**. Job(KILL_ON_JOB_CLOSE)/pipe 수명과 ② r3 보수 API 수명 조건을 같이 맞출 것.
- 구현 순서: 작업용1 ② r3 보수·e 배선 → 공용 API 고정 → 작업용2 PC 구현(viewer_startup.cpp 는 NP 브랜치와 겹침 → NP 병합 뒤).

## 공통 규칙
repo root 밖 삭제 금지(새 worktree 생성만), 사용자 설치본·서비스·세션 무조작, 버전·서명·게시·push 금지, 측정 슬롯 CreateNew, 커밋 Git MCP(`.claude/mcp_git_client_wt.py <worktree>`), 별건 directory-retry 를 섞지 않는다. 비용 측정 기준(§6)과 필수 행동(§7)은 ②~④ 단계에서 적용.
