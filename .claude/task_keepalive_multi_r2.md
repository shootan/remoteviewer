# task keepalive-multi / r2 — M1 연결 성립 전 LRU 금지 · M2 연결 중 최신 탭 · L1 종료 정책 · V1 핵심 경계 실행 (검증용 remote#pvbntb3z, 2026-10-16)

**r1 을 대체하는 통합본.** 담당 작업용2, task `t-f4nz270q`. 보고는 검증용 remote#pvbntb3z pin.
계약 최신: **`codex_keepalive_multi_2c22313.md`(전부 읽기, 우선)** + `D:/remote/remote/.claude/codex_keepalive_multi_r1.md`. 기준 HEAD `2c22313`. 검증용 r1 독립 결과 `verify_keepalive_multi_r1.md`(2-host 20/20, 0.2.30 부정대조 13/20).

## r1 → r2 변경 요약
- **인정**: entry 별 controller/rendezvous/error/gate·shared_ptr 수명, 프로세스 유일 op·entry 별 취소, 단일 decoder token 검사(sink 12종·같은 잠금), map 분리 뒤 closer teardown, 2-host 긍정 경로. 다시 설계하지 않는다. 같은 entry 재활성화를 구분하는 binding generation 은 유지.
- **M1 [고칠 것]**: `ClientSessionController::Connect` 는 Connecting 으로 두고 WorkerMain 을 띄운 뒤 true 를 반환(인증 Hello/제어 연결은 비동기). 그런데 `madeGeneration != 0` 이면 UI 가 즉시 `nativeCommit` → `SessionRegistry::Commit` 이 예약된 parked victim 을 Closing → **B Hello 가 거절/timeout 돼도 이미 A 를 축출**. 위치: MainActivity ~5035~5073(nativeDirectoryConnectAny→madeGeneration→nativeCommit), 수동 연결 ~2328, bridge session_adopt_steps, native_video_client_session.cpp Connect ~65~120, SessionRegistry::Commit.
- **M2 [고칠 것]**: `connectToDirectoryHost` 첫 줄 `if (directoryBusy) return` 이 B 연결/observe/punch 중 A/C 탭 intent 를 버림. r1 §3 '빠른 탭 최신 intent 하나'는 연결 준비 구간도 포함.
- **L1 [확정 정책]**: 다중 keepalive 는 **앱 foreground 범위**. **onStop = active + parked + connecting 전부 종료 + global lifecycle epoch 증가**(endParkedKept → endAllKept). 0.2.30 의 'active 세션 백그라운드 유지'와 달라짐 — 문서/history 에 "앱을 벗어나면 모든 PC 연결 종료" 명시. configuration 변경(onConfigurationChanged) 예외는 유지. 확인된 network path 변화도 active 여부와 무관하게 전부 종료·epoch 무효화, active 만 남은 경우 network 감시 범위와 문서 일치. '모두 끊기' 는 activeHandle 유무 분기 없이 이름대로 전체(UI 도달 불가면 그 사실만 기록). PC 카드 끊기·화면 연결 해제는 자기 entry 만.
- **정책값**: 최대 3대·PC 별 5분은 **여전히 사용자 답 대기**(답 오면 검증용이 알림 → 정책 객체·UI·시험 기대값 함께 고정). 답 없는 상태로 출하 안 함.

## 할 일
1. **M1 보수**: 동일 handle/incarnation·connection generation·op/lifecycle epoch 의 **실제 Connected/인증 성립 확인 뒤에만** commit/eviction. UI 만 늦추지 말고 **native commit 도 Connecting 의 성급한 commit 을 거절/보류**하는 경계를 닫음. 확인 전 sign-out/새 intent/실패 → 예약 취소·피해 연결 0. handshake 성립까지 무한 UI 대기·테이블 락 동기 wait 금지. 한도 중 임시 연결 예약/closing 자원 수와 최종 live 한도를 구분해 기록. 수동 연결 경로(~2328)도 같이 점검.
   - 반례(같은 제어 순서, 수정 전 FAIL → PASS): 한도 N 의 parked 들 → 새 D 예약/worker 시작 → D Hello ACK/인증 보류 → (수정 전) LRU 먼저 종료 → D 실패. **수정 후: 실패면 기존 parked 0 종료, 성공이면 LRU 1 회·피해 대상 정확.** native scripted Alive=true 만으로 인증 연결을 대신하지 않음.
2. **M2 보수**: 연결 준비 중 새 PC 탭 → 기존 B 는 자기 op 만 취소·정리하고 최신 탭을 한 번 실행하거나, 진행 중 표시와 함께 최신 선택을 보관해 이어감. 최신이 parked A 면 B 네트워크 timeout 을 기다리지 않음. 단일 전환 소유자·다른 parked 보존·native 소유권 gate 재사용. 취소와 동시에 fallback 이 B 를 다시 시작하지 않음.
   - 반례: fixture 응답 지연(delayed observe/Hello)으로 실제 앱 경로에서 B 진행 중 A→C 탭 → **최신 C 1 회·옛 B 채택 0·관계없는 A park 보존**. 시간 운 금지. sign-out/stop 뒤 지연 callback 도 같은 묶음.
3. **L1 정합**: 위 정책대로 onStop·network·모두 끊기. in-flight 입력 release/gesture 정리·Surface/scene 복귀 확인. 회전은 전체 종료가 아님을 확인.
4. **V1 좁은 실행(실제 앱·fixture, 전체 2-host 왕복 반복 불필요)**:
   - M1: 인증 성립 전 실패 → LRU 0, 정상 Connected → LRU 1. **한도 N+1 번째 PC 의 실제 fixture 흐름**(한도 3 이면 host 4 대 — fixture 를 4 host 로 확장, 서로 다른 hostId).
   - 독립 TTL: A 를 쓰는 동안 B 만 만료 → B 복귀는 새 연결, A 는 그대로. native expiry 가 UI polling 없이 실제 controller 경로에서 집행. TTL 을 단축하면 **시험 전용 정책**으로, 출하 기본값과 구분 기록.
   - delayed connect/park-confirm tap + onStop/sign-out/네트워크 종료 → 늦은 adopt/fallback 0, active/parked 전체 종료, 재시작 시 새 연결(AVD 행동 + callback/host 카운터).
   - 구 host pause 미지원(그 PC 만 종료·업데이트 안내·다른 PC 유지), relay 의 다른 기기 대체 뒤 자동 재탈취 0 — 최소 반례. **direct 경로 smoke 1 회**(새 entry/handle 결합).
   - A/B 전환 뒤 crop/크게보기·6M 저장/적용 대상, stale clipboard/preview 가 다른 PC/로컬에 적용되지 않음 — 1 회.
   - 최종 한도에서 폰 프로세스 스레드/메모리 + **양방향** control bytes 측정(host→폰만으로 총량·배터리 주장 금지; 실폰 배터리는 미검증으로).
5. **비차단 정리(같이 보면 좋음)**: `native_bridge.run_detached` 가 `std::thread(std::move(f))` 생성 예외 뒤 f() 를 부르는데 이동된 빈 f 일 수 있음 → 별도 복사본/안전한 동기 fallback, 그리고 transitionMu_ 를 쥔 문맥의 동기 fallback 이 Close/join 을 잠금 아래 실행하지 않는지 확인. binding 조회 후 장기 JNI 작업(privileged action·비동기 clipboard·매크로)의 승인 시점(시작 binding 유지)을 설계표와 호출부에 명시.
6. 회귀: 단위 7종(session_registry·session_table·connect_op_gate·decoder_binding·owed_key·geometry·park_state) + 새 반례, Android 전체, build gate(**실제 게시 방식 assembleDebug** 기준 + 기전 기준, hook 이름 리터럴과 실행 기능 구분).

## 작업 공간·규칙 (동일)
`C:/work/gnlink-wt/feat-keepalive-multi`, branch `feat/keepalive-multi`. junction 재귀 삭제 금지. fail-closed 격리 시험 host 만(제품 host 를 시험 host 대신 띄우지 않음), 사용자 화면·host·폰·실 마우스 미접촉. **측정 슬롯은 CreateNew 로 먼저 요청 → 작업용1 의 가장 최근 `fix-catchup-only/.claude/*runs.log` 마지막 줄 end 확인(매번 다시 확인) → 실행 → 삭제.** main 머지·push·게시·버전 변경 금지. Git MCP 커밋, 메시지 끝 "제품/테스트/문서" 한 줄.

## 보고 (1회)
`.claude/report_keepalive_multi_r2.md`: HEAD·해시, M1/M2 반례 FAIL→PASS(종료 코드), L1 변경 위치와 문서, V1 각 항목 결과(카운터·host 로그·시간), 4-host LRU, 비용(양방향·스레드·메모리), build gate, 남은 미검증. 검증용에 1~3줄 pin.
