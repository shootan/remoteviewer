# keepalive-multi r1 (2c22313) — 검증용 독립 검증 (remote#pvbntb3z, 2026-10-16 16:54~17:10)
- 고정: 내 worktree C:/work/gnlink-wt/verify-km detached **2c22313**, 새 build-verify(VS2022 x64). 출하형 APK(plain assembleDebug) `860457a4569a68ec0e2e1153e601a1d37a30aa81b8346239164e973cf0763d0e`, fixture APK `b6d86b48…9303`, 시험 host `f62101a0…def1`, 출하 GNLinkStream `6d4401bd…cab4`. 슬롯: 16:43 요청(CreateNew) → 작업용1 c11on_45m30_r2 end 대기 → 16:54:52~17:10:09, 겹침 0.
- 단위(exit 0 반복): session_registry ×2, **session_table ×5, connect_op_gate ×5, android_decoder_binding ×5**, owed_key ×2, geometry ×2, client_park_state ×2. Android unit **213/0**.
- 소스 검토: EntrySinkAdapter 가 ClientEncodedFrameSink 가상 12종 전부 override, 매 호출 token 을 디코더 *As(token) 로 넘겨 디코더 잠금 안에서 비교(TOCTOU 없음, token 0 이면 디코더 미접촉). session_table: map(mu_)·전환(transitionMu_) 분리, 닫기는 Detach 후 closer(분리 스레드)에서 shared_ptr 보유 채 Close — 테이블 잠금 아래 join 없음.
- **2-host AVD(실 APK UI, fail-closed 시험 host 2대, emulator-5624)**:
  - run1 후보(내 fixture): **20/20**. A handle 1 / B handle 2, B 새 연결 때 A 종료 없음, A 재사용·B 재사용 시 각 카운터(connects/hellos/observes/punches) 1→1, 첫 화면 534/437/685 ms, A 끊기 → A 만 종료·B 유지. ROUTE PASS(A 화면: host_a 입력 2·config 6000000, host_b 없음 / B 화면: host_b 입력 2, host_a 없음). host 로그: 각 host 연결 1회, 안정 park 4 구간 **encoded 0 / video datagram 0**, control 559~1274 B/s(3.1~4.7 dgram/s).
  - run2 **부정 대조 0.2.30 APK(b46ddbf fixture 4d63e2af)**: **13/20, exit 1** — FAIL 은 정확히 다중 유지 단계(3 A 카드 유지 표시, **4 B 연결이 A 를 끝냄(A=None)**, 5 둘 다 유지, 6·7 재사용, 8 개별 끊기, 10). 나머지(로그인·새 연결·첫 화면) PASS.
- build gate(출하형 APK, 실제 게시 방식 assembleDebug): dex·.so 에 10.0.2.2·testHook·REMOTE60_CONNECT_GATE_TEST_HOOKS·abr-trace·input-trace **0**, 새 기능(SessionTable·EntrySinkAdapter·nativeActivate·nativeSetKeepalivePolicy) 존재. 출하 host REMOTE60_PINNED_TEST_MACHINE_ID·input-trace 0 / 시험 host 1.
- 미검증(인용·미실행): 빠른 A/B/C 탭(최신 intent), LRU 4 번째 PC·PC 별 TTL 만료(native 시험만), sign-out/stop 뒤 지연 connect(native 만), known refused fallback, 구 host pause 미지원, direct 경로(AVD 는 relay), 다른 기기 탈취 뒤 재탈취 0, 전환 뒤 crop·크게보기·6M 대상 유지, 폰 쪽 스레드·메모리, 실폰·배터리.
- Codex 결정 필요: onStop 정책(현재 0.2.30 처럼 parked 만 종료·화면 세션 유지 vs 계약 §6 전부 종료).
- 판정: 후보 **검증용 OK**(위 미검증·결정 1건 제외).
# keepalive-multi r2 (92956b1) — 검증용 독립 검증 (remote#pvbntb3z, 2026-10-16 18:20~18:44)
- 고정: verify-km detached **92956b1**, build-verify. 출하형 APK(plain assembleDebug) `5e30e30d3441d0195726940dbe18d1e3152a60c375d46280068122d3f532a943`, fixture `ee52a00a…d48c`, TTL 시험 fixture(-Pgnlink.testKeepParkTtlMs=45000) `def68544…3128`. 슬롯 18:09 요청 → 작업용1 c11on_45m30_r3 end 18:20 → 18:20:30~18:44:57, 겹침 0.
- 단위: session_table·connect_op_gate·decoder_binding ×5, 나머지 ×2 — 실패 0. Android **213/0**.
- 소스: SessionTable::Commit 이 Established()==1 + 같은 generation 일 때만 registry Commit·LRU, 0 이면 Pending(무변경·대기 없음), <0/세대 불일치면 Fail(축출 0). transitionMu_ 아래 wait 없음.
- **run1 후보 6-host(km2_avd, 정책 3대·5분): 35/35** — P3 M2 B 연결 중 A 탭 → B superseded·A 즉시 재사용(2111 ms)·B 늦은 응답 stale(before_native_connect); P4 C superseded·B 채택·A 무관; P8 M1 X(Hello 무응답) 실패 → 축출 0·A/B/C 유지; P9 D direct(192.168.0.76 private)·첫 화면 124 ms·LRU 1 = A; P11·P13 백그라운드/네트워크 전부 종료. host 안정 park 전부 encoded 0 / video 0, control 0.33~0.73 KB/s.
- **run2 TTL 시험 정책(45 s): 15/15** — A 사용 중 B 만 ttl 만료, B 재접속은 새 handle(2→3), A 는 재활성화가 새 기한(첫 park 135 s 전·두 번째 33 s 전인데 재사용 성공).
- **run3 부정 대조 r1 APK(2c22313, 내 v_km1 fixture b6d86b48): 21/35, exit 1** — **P3·P4(M2: 연결 중 탭이 B 를 대체하지 못함, A 복귀 8234 ms)·P8(M1: X 실패 뒤 A/B/C 유지 실패)** 등 FAIL. 후보와 같은 순서·같은 fixture.
- build gate(출하형): dex·.so 에 10.0.2.2·testHook·REMOTE60_CONNECT_GATE_TEST_HOOKS·abr-trace·input-trace 0 (TTL 45000 은 아래 출력 참조).
- 미검증: 실기 clipboard stale 결합(fixture host clipboard 꺼짐), crop 전환 결합(480×270), 실폰·Wi-Fi↔LTE·회전 실기·스레드 누수 판정·배터리. 정책값 사용자 답 대기.
- 판정: r2 후보 **검증용 OK**.
# keepalive-multi r3 클립보드 결합 (시험 a2e9686, 제품 92956b1 그대로) — 검증용 독립 (2026-10-16 19:17~19:18)
- verify-km detached a2e9686(92956b1 대비 차이는 시험 파일 km_multiclip_e2e_test.cpp 와 CMake 대상뿐, 제품 소스 변경 0). 내 빌드 시험 exe `0cbd18be…9195`, 시험 host `f62101a0…def1`.
- 슬롯: 19:04 요청 → 작업용1 c11off_45m30_r3 end(19:17:03 R11-MEASURE-PASS-END) → 19:17:08~19:18:26, 겹침 0.
- 결과: 일반 **30/0 ×3(exit 0)**, held 대조 **15/0(exit 0)**. 종료 뒤 남은 시험 host 0.
- 인용 범위: 대체된 것 = JNI 마샬링·Kotlin drain/push 루프·Android ClipboardManager(모의 sink). 두 host 가 격리 station 클립보드 공유 → host 자기 로그 길이로 귀속 판정. 실폰·실제 2-PC 다른 클립보드 미검증.
- 판정: Codex 92956b1 의 클립보드 결합 조건 **충족**(독립 재현).