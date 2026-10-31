# task capture-lifecycle r1 — PS(로그인 전 화면 영상 0) + UA(WGC 에서 UAC 안 보임) 묶음 (검증용 remote#yj8mrmrx, 2026-10-26) — 담당 작업용1

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로.** 상위 계획 `.claude/plan_rel_0163_2026-10-26.md`(R1 = PC 0.2.163). 구속 계약: `.claude/codex_rel_0163_r1.md` §1~§4(Codex 확정 10-26 — 여기 요약과 다르면 Codex 파일이 우선). **상태: 확정. LK-UX 를 끝낸 뒤 착수.**

## Codex 확정 조건 요약 (반드시 원문 §1~§4 를 읽을 것)
- **PS-A**: 원인을 'flush 가 원본 삭제' 하나로 확정하지 말 것 — `BootstrapFrameCache` 는 BGRA 원본을 따로 보관하고 flush 가 지우지 않지만 `KickTryFill` 이 **Default 데스크톱이 아니면 무조건 false**, NV12-only 경로도 있음. 보존물은 앱 소유 immutable 복사본(DXGI 원본 텍스처·WGC pool surface·ring 포인터 금지), 최신 1개 유한. 미디어 큐 flush 와 source cache 유효성 분리, source/desktop identity·generation·세션·모니터·크기·device 변화 시 무효화. **Default 원본을 UAC/Winlogon 에, 옛 사용자 원본을 새 세션에 보내지 않음** — guard 삭제나 옛 프레임에 현 gen 덮어쓰기 금지, provenance 와 새 media epoch 별도. idle detach/desktop 전이 뒤엔 재부착으로 새 전체 프레임, 무한 duplication 재생성 금지(유한 backoff). 첫 AU 가 없어도 activation/새 선택/새 세션/명시 keyframe 에서 raw bootstrap 시작(`StaticRefreshDue` 의 `lastEmittedAuCaptureUs != 0` 의존 제거). 새 IDR 실인코딩(옛 AU 재생 금지). static refresh 는 **stream active desktop 경로만**(picker·비활성·parked 는 인코더 0). **정지 화면 갱신 목표 500 ms(2 fps)** — 현 기본은 `host_startup_capture.cpp:343~345` 1000 ms(1 Hz), 출하 시작 경로에서 측정.
- **PS-B**: prelogin 재부착은 일반 secure→WGC 분기보다 먼저 DXGI + 현재 서비스 중인 logon desktop attach, 로컬 mode/신뢰 경계로 판단(원격 flags 금지). r9 W2·r10 T2·requiredInputDesktop 검사 유지, picker idle→active·rollover 자체는 rc15 사유 아님.
- **UA-1**: active=requested=WGC 에서도 입력 데스크톱 edge 독립 감시, lock 상태와 secure desktop 분리(Unknown≠잠금). 옛 callback·cache provenance 무효화 → WGC item/pool/session 재구성 → 새 세대/IDR/출력 확인을 capture owner 에서 직렬, debounce·epoch·bounded retry. **PS-A 캐시 검증과 UA-1 desktop generation 은 같은 계약.** SYSTEM 캡처 확대·UAC 자동 승인·LockCred guard 제거 금지.
- **UA-2 (host)**: backend 요청 = 현재 인증 논리 control 세션 소유(owner cookie/epoch 결합), EndSession/새 세션 채택 시 override·pending 명령 무효, 늦은 옛 Serve() 가 새 세션 요청을 못 지움. 같은 세션 resume/선택/pause 는 유지, 세션 바뀐 reconnect 는 초기화. 기본 = 호스트 startup 기본 + quarantine 항상 적용, prelogin DXGI 규칙 불변. 실제 적용 방식 표시용 상태가 필요하면 ControlPong 몰래 확장 금지 — 최소 capability/상태 응답, 구 peer 에 미지 요청 금지, **새 번호·플래그는 제안 → 검증용이 KA·LK 와 충돌 검사 후 배정**. 반례: 폰 WGC→종료→PC 미요청=DXGI, 격리 시 WGC 유지, 늦은 옛 종료 뒤 새 세션 선택 보존, resume/park 보존, 계정·host 변경 전파 0.
판독: `.claude/field_prelogin_reboot_2026-10-22.md`(워커 로그 `temp/prelogin_worker.log`), `.claude/codex_uac_prompt_check_2026-10-23.md`(+ 검증용 보완 A2A seq 13925).

## 사용자 실기
- 10-22 크래시 재부팅: 진짜 로그인 전 화면에서 PC 뷰어·폰 모두 연결·인증 OK, **영상 0장** → OSLink 로 복구. 사용자 상시 지시 위반: "정지 화면이라고 첫 장 버리고 계속 정지시키지 말 것 — 최소 프레임으로라도 주기 갱신".
- 10-23: 관리자 권한 필요 프로그램 실행 시 UAC '예/아니오' 가 원격에 안 보임 → 재접속해야 보임, 승인 뒤 검은 화면 → 두 번째 재접속으로 복구. 원인 사슬: 폰 설정 WGC 선택(10-22 15:24:23) → 호스트 전역 requested=WGC 유지 → `host_stage_backend.cpp:252` edge 추적이 active≠requested 일 때만.

## 범위
- **PS-A**: DXGI(특히 prelogin) 정지 화면에서 stream 활성화·keyframe 요청·static refresh 때 인코딩할 원본 보장 — 첫 내용 프레임이 stream inactive flush 로 사라지고(`capture-pipeline-flushed reason=stream-inactive`) 그 뒤 새 DXGI 프레임 0 인 경로. 모든 desktop 경로에서 최소 주기 갱신이 원본 부재로 무력해지지 않게.
- **PS-B**: idle detach 뒤 재활성화 시 `host_stage_stream_active.cpp:139-143` 의 보안 데스크톱→WGC 분기 → `host_capture_session.cpp:600` `PreloginStopServing("wgc requested")` 종료(rc 15). prelogin 은 DXGI 재부착, 진짜 불가일 때만 기존 r10 T2 종료.
- **UA-1**: requested=WGC(및 DXGI→WGC 강등, 이미 WGC) 모두에서 같은 연결 동안 Default↔secure 입력 데스크톱 edge 감지 → 현재 WGC 자원 재구성 → 옛 캡처/미디어 세대 정리·새 IDR·출력 확인. 전환 중 무한 restart 금지, transient 조회 실패를 잠금으로 바꾸지 않음. 잠금 암호 입력 경계·입력 agent 불변.
- **UA-2 (host 쪽)**: Codex 확정 정책대로(검증용 제안: 캡처 방식 요청은 그 control 세션 동안만, 종료 시 호스트 기본 복귀, 다른 기기 세션 상속 금지). APK 표시 부분은 작업용2.

## 시험 (사용자 행동 기준, 부정 대조 필수)
- PS: 실제 SYSTEM prelogin 워커 경로(또는 Winlogon 동등 fixture — 무엇이 같고 다른지 명시)로 picker → 10 s 대기 → 바탕화면 → **정지 화면 첫 presented ≤1 s**, 이후 정지 화면 갱신 간격 ≈500 ms(wire/present 측정), 같은 뷰어 재접속 2회·폰 교차 접속·강제 keyframe 중 워커 생존. raw cache 없음/첫 AU 없음/NV12-only·CPU 경로/경계 뒤 옛 snapshot 늦은 도착 케이스. **Default↔Winlogon·session·target 을 바꾼 뒤 옛 cache 가 나가면 FAIL 하는 부정 대조.** setter/Encode 성공이 아니라 새 IDR→수신→decode→표시로 판정. 기존 `viewer_prelogin_login_e2e_test.cpp:1194`(datagram>0)는 이 판정으로 부족 — 첫 디코드 프레임·내용으로 판정. **수정 전 바이너리가 같은 시험에서 FAIL** 함을 보인다.
- UA: requested=WGC / DXGI 각각, 같은 socket·control cookie 로 정상→secure desktop→정상 2회+, 재접속 0, 새 IDR·내용 프레임 확인. 실 UAC 는 자동 승인 금지 — 동등 secure desktop fixture 로 자동, 실 UAC 승인은 사용자 실기 묶음 1회.
- UA-2: 폰(또는 Android 공유 세션 하네스) WGC 요청 세션 종료 뒤 PC 뷰어 세션 = DXGI.
- 회귀: 잠금 화면 LK e2e(--lock-screen·prelogin·not-prelogin), DW quarantine, static refresh/trailing kick, 일반 desktop/window 선택.

## 규칙
새 worktree·브랜치 `C:/work/gnlink-wt/fix-capture-lifecycle`, `fix/capture-lifecycle`(main a1c551c 기준; LK-UX 와 같은 파일이면 LK-UX 위에 쌓아도 됨 — 보고에 명시). repo root 밖 삭제 금지, 사용자 설치본·서비스·세션 무조작, 버전·서명·게시·push 금지(R1 통합 task 에서), 측정 슬롯 CreateNew, 커밋 Git MCP, history 는 task 커밋에. 에뮬레이터·GPU 캡처 장시간 동시 실행 자제.

## 보고 (1회)
`.claude/report_capture_lifecycle_r1.md` + `remote#yj8mrmrx` pin: 고정 commit·변경 파일, PS/UA 각 시험 결과(수정 전 FAIL·수정 후 PASS, 반복 수), 남은 실기(실 로그인 전 재부팅·실 UAC 승인).
