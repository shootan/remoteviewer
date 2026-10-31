# KA ② e 배선 계획 r1 (작업용1, 2026-10-28) — kept 모델 A + M1

근거: codex_keepalive_r2_kept_model.md(A), codex_apk0233_merge_src_r1.md(M1), task_keepalive_r2.md '10-27 ② 판정 취합'.
브랜치: feat/apk-0.2.33 (dd2925d). 공용 native 는 ka-r3 에서 고친 뒤 merge.

## 구조
- **lease 는 테이블 밖**: StandbyRunner(키 = origin|account|hostId)가 standby 를 들고 있다. 테이블(SessionTable)에는 **활성 세션만** 들어간다. 장기 parked control 세션 모델은 쓰지 않는다(모델 A).
- **탭 = 새 테이블 entry + standby activation**:
  - 새 JNI `nativeConnectStandby(handle, key, op): Long`.
  - 흐름: `TakeActivation(key, &args, &sop)` → encodedFrameSink=&e->sink → `gate.Adopt(op, steps)`.
  - steps:
    - closePrepared = `runner.Abandon(key, sop, &args)`.
    - disconnect = `controller.Disconnect()` 다음 `runner.SessionOver(key, sop)`.
  - SessionEntry 는 standbyKey 와 sop 를 기억한다.
    - `Close()` → Disconnect 반환 → SessionOver(key, sop). 늦은 것은 op 로 무시된다.
    - Connect 가 false 면(worker 기동 전) Abandon 한다(handle 회수).
- **목록 복귀(PC 목록)**:
  1. 기존 화면 이탈 fence(입력 release, 선택 중단, decoder unbind).
  2. `nativeEnd(h, "to_standby")` → closer 스레드에서 Disconnect → SessionOver → 같은 lease 가 Ping 을 재개한다.
  3. Kotlin 상태는 ReturningToStandby 다. lease 가 Parked 이고 !handedOff 일 때만 StandbyReady 로 본다.
- **일반 directory 연결의 첫 전환**(지원 Host):
  - 세션 종료를 직렬화한 뒤 지원 확인(`/api/hosts` standby) → nativeStandbyStart → Observe → `connect(standby=true)` → Provide.
  - 이 새 요청은 로그에 `standby_first_transition` 으로 남긴다.
- **미지원**(raw IP, 구 Host, prelogin, 지원 unknown): 목록 복귀 시 "상시 유지 미지원" 토스트 + 세션 종료. standby 0.
- **준비**(PreparingStandby, staging ≤ 1):
  - 순서: executor 에서 Observe(lease 소켓) → `/api/connect` standby → 대상 선택(relay reserved → relay, 아니면 사설 후보 → 첫 후보) → Provide.
  - incarnation(키별 단조 증가) + 계정 epoch 를 Provide 직전에 검사해 늦은 Provide 를 버린다.
- **max3 = active + standby 합.**
  - 4번째 준비가 실패해도 축출 0. 성공하면 standby LRU 를 Release 한다.
  - RecentHosts 순서는 사용자의 성공 사용만 바꾼다(지금과 같다).
- **수명**:
  - onStop → endEverything + ReleaseAll. onStart → 최근 3대 준비(스위치가 켜졌을 때만).
  - 로그아웃·계정 변경 → epoch 증가 → ReleaseAll.
  - 개별 연결 해제 → Release + Suppression.
  - 네트워크 변경 → ReleaseAll(lease 소켓이 옛 경로에 묶여 있다).
- **M1**: park_reuse 경로(resumeKept)를 지운다. 모든 활성화가 새 controller 이므로 `resetSessionDesktopBackend("commit")` 이 맞다. 반례 시험: A 요청 미응답 → 목록 → B → A 재활성화 → 요청 없음으로 시작, 자동 재전송 0.

## Kotlin
- 순수 `StandbyKeep`(새 파일):
  - 키별 상태 PreparingStandby / StandbyReady / Activating / Active / ReturningToStandby / Unsupported / Retry(notBefore).
  - incarnation, epoch fence, max3 판정, 표시 문구.
  - 단위 시험 대상이다.
- `DirectoryClient`:
  - `hosts()` 가 `standby{supported, relay}` 를 읽는다.
  - `connect(..., standby: Boolean)` 가 standbyRelay·relayIp·relayPort 를 읽는다.
- MainActivity:
  - parkToHostList → returnToList.
  - keepTick 은 lease 상태를 poll 한다.
  - 탭 분기: StandbyReady → activation, 아니면 cold connect.
  - 카드 표시는 실제 lease 상태 기준이다.

## 결정 필요(검증용) — 스위치 OFF(출하) 빌드의 목록 복귀
- ⓐ 사용자의 목록 복귀(명시 행동)는 스위치와 무관하게 모델 A 로 standby 전환. 자동 준비(앱 시작 시 최근 3대)만 스위치로 막는다.
- ⓑ 스위치 OFF 면 목록 복귀 = 세션 종료(유지 없음). 모델 A 는 스위치 ON(시험 빌드 `-Pgnlink.standbyReal=true`)에서만 켠다.
- ⓒ 스위치 OFF 면 옛 park 유지 + M1 을 entry 별 asked 보존으로 보수(두 모델 공존).
- 작업용 제안: ⓑ. 출하 동작에서 Host 로 가는 새 패킷이 0 이고, 옛 park 를 지울 수 있으며, M1 이 경로 제거로 닫힌다. 시험 빌드에서 모델 A 전체를 검증한다.

## 시험
- native: `nativeConnectStandby` 의 수명.
  - bridge 단위로는 어렵다. e2e(desktop)에서 테이블+gate+runner 결합 시험 `standby_table_e2e` 를 추가한다: Adopt stale → Abandon, Close → SessionOver, Connect 실패 → Abandon, 같은 key 재생성.
- Kotlin 단위: StandbyKeep 의 상태, fence, max3, LRU, 늦은 Provide, 같은 key 재생성.
- AVD(시험 빌드, 실 GNLinkStream 시험 Host + 시험 directory):
  - A/B/C standby 3 → A tap → 목록 → 같은 lease → B → A.
  - 명시 연결 → 첫 전환.
  - 구 Host 미지원.
  - 하네스: sh_filter_avd 에 standby 시나리오 추가. directory 는 시험 server.js(loopback 포트, adb reverse)를 쓴다.
