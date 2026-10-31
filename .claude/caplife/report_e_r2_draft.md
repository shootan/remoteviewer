
### ② e r2 — Codex `codex_ka_e_src_r1.md` D1·N1–N5 보수 (2026-10-28)

**커밋**
- ka-r3:
  - **da2280e** N2 + D1(관측). 새 capability 로 가는 모든 진입(ObserveFrom·NeedCapability)이 묶인 소켓을 푼다(`UntieForNewCapability`). 이전 세션의 tail 을 센다.
  - **ecf1004** punch 시험 상단 주석을 보고 정정과 맞췄다(검증용 534242b 메모). 시험 동작은 그대로다.
- feat/apk-0.2.33:
  - **c908f5a** N1·N3·N4·N5·D1 표시·held input fence. JNI status 에 `tailActive`, `strays` 를 추가했다(7개).
  - **ac2a157** `standby_back host=.. strays=N tail=..` 로그(D1 비용 계수).
  - **4194957**·**7a0d577** AVD ke_r15·ke_r18 이 찾은 실결함 보수(아래 「AVD 가 찾은 결함」).
  - merge: a0f722b(da2280e), c3bf472(a7773e6 punch thread).
  - ⚠️ feat 에는 ka-r3 의 6c3cb5a·20bc108·534242b·ecf1004 가 **아직 merge 되지 않았다**. 모두 Host·시험 전용이라 APK 에는 영향이 없다.
- NP 소유 파일은 건드리지 않았다.

**D1 — 새 종료 신호 없이, 세 사실을 구분해 표시**
- 새 wire 는 추가하지 않았다. 13 s 타이머를 grant 종료 근거로 쓰지 않는다.
- ⑴ local handoff(소켓 돌려받는 중): 탭을 보류한다(아래 N1 의 최신 의도 규칙 적용).
- ⑵ entry/Ping 생존: 카드 "대기 중 · 대기 연결 있음 · 누르면 엽니다". "바로/준비됨" 같은 정리 완료 의미는 없앴다.
- ⑶ Host 의 이전 grant·영상 정리: 기존 wire 로 확인할 수 없다. lease 가 target 에서 오는 비-standby datagram(이전 세션의 영상·제어)을 센다. 1.5 s 안에 들어오면 tailActive 다. 그동안 카드는 "이전 연결 정리 중일 수 있음 · 누르면 다시 엽니다" 다. 탭은 막지 않는다(사용자의 명시적 rollover).
  - 이 값은 **tail 의 관측**이지 grant 종료의 증명이 아니다.
- tail 비용은 따로 센다(`standby_back strays=`, `standby_tail_quiet`). AVD 실측(이 fixture, loopback relay):
  - ke_r10 E7b: 돌려받을 때 strays 27 → 조용해질 때 49. 그동안 카드는 "정리 중일 수 있음" 이었다.
  - ke_r17: 돌려받을 때 strays 0·2·24·25 (tail=true 1회).
  - ke_r19: 13회 중 12회 strays=0, 1회 strays=3 tail=true. 실행마다 크게 다르다(ke_r16·r17 은 23–25). 원인은 조사하지 않았다.
  - "정리 중일 수 있음" 표시를 화면에서 본 것은 ke_r10 뿐이다. ke_r16·ke_r19 의 E7b 캡처 시점에는 tail 이 이미 끝나 있었다(settling seen=None).
  - 이 숫자는 이 환경의 값이다. 안정 standby 영상 0 과 합산하지 않는다.
- **held input**: "PC 목록" 은 세션을 먼저 fence 한다(native park fence: 새 입력·clipboard 차단, 눌린 키의 release 송신, Ping 으로 정지 확인). 정착하거나 budget(2.5 s)에 이르면 끝낸다. fence 할 수 없는 세션(구 Host·raw IP)은 즉시 끝낸다.
  - AVD E7b: Ctrl 을 누른 채 PC 목록 → **Host 자신의 입력 로그에서 key up 이 세션 종료보다 먼저**였다. `list_return_end how=fenced`.

**N1 — 최신 탭 우선**
- 사용자 탭마다 번호(tapSeq)를 매긴다. 새 탭은 보류된 탭을 버린다.
- 보류·재시도 탭은 만들어질 때의 번호를 지닌다. 그 뒤 탭이 없었을 때만 실행된다. 사용자 끊기가 건 suppression 을 풀지 않는다.
- 카드 끊기·disconnect 는 그 PC 의 보류 탭을 버린다.

**N2 — 동기 capability 경로도 소켓을 푼다** (da2280e)
- 묶인 소켓을 새 소켓으로 바꾸는 일을 ObserveFrom 과 NeedCapability 양쪽에서 한다(공통 함수).
- pump 가 읽는 중에 owner 스레드에서 NeedCapability 가 오면 pump 의 다음 차례(새 read 전)로 미룬다. read 중인 descriptor 를 닫지 않는다.
- 정상 재사용은 같은 endpoint 를 유지한다. shared=false 경로는 그대로다.
- `standby_lease_test` **66/66 ×6**: 세션 뒤 동기 capability 가 새 소켓에서 observe 한다(pump 가 읽는 중에도). Codex probe 의 `after_session_observe=0` 경로다.
- 변이 2종(동기 경로가 묶인 소켓 유지, tail 미계수) FAIL. 이전 변이도 FAIL 유지.

**N3 — clipboard r3 계약 복구**
- standby activation 으로 만든 세션: 폰의 현재 text 를 **그 새 binding 의 baseline** 으로만 seed 한다(보내지 않음). 읽을 수 없으면 실제 변경 전까지 보류한다.
- cold 접속은 이전과 같다. 옛 park 세션 모델은 되살리지 않았다.
- 단위 시험만 했다(`clipboardOnStart` Seed/Hold/Fresh). AVD fixture 의 Host 는 clipboard sync 를 끈 상태라 실 clipboard 는 보지 않았다.

**N4 — active + standby 합계 ≤ 3**
- 확정된 일반·raw-IP 세션을 lease 와 함께 센다. 넘치면 가장 오래된 lease 를 놓는다(화면에 떠 있는 PC 는 제외). 한도를 넘으면 lease 를 시작하지 않는다. 4번째가 실패하면 아무것도 놓지 않는다.
- 단위 시험(`overLimit`/`mayStart`)만 했다. raw-IP 활성 + 최근 3대 AUTO 준비 경로는 AVD 에서 돌리지 않았다.

**N5 — Activity 종료 경계**
- onDestroy: owner 를 먼저 무효화하고 이 Activity 의 lease 를 놓는다. standby 스레드는 shutdown(shutdownNow 아님: queue 의 ReleaseAll 이 실행됨)으로 끝낸다.
- 끝난 Activity 의 늦은 callback 은 아무것도 바꾸지 않는다.
- endStaging 은 key·incarnation 이 현재 것일 때만 staging·back-off 를 고친다.
- 단위 시험(늦은 job)만 했다. Activity 반복 재생성의 thread 누적은 측정하지 않았다.

**AVD 가 찾은 결함**
- **ke_r15 — 실결함(4194957 로 보수)**:
  - PC 목록의 fence 동안(≤ 2.5 s) 다른 PC 를 탭하면 native table 이 activation 을 거부했다(`keep_activate_refused`). 그 사이 만든 세션은 버려졌다.
  - 이제 이 탭도 보류한다(`tap_held_fence`). 최신 의도 규칙을 따른다: 새 탭이 대체하고, 끊기가 버리고, fence 가 끝나면 keepTick 이 실행한다.
  - ke_r16·ke_r17·ke_r19 에서 E7c·E7d PASS, `keep_activate_refused` 0.
- **ke_r18 — 실결함(7a0d577 로 보수)**:
  - E7f: capability job 이 "lease 가 현재 것" 을 확인한 뒤, 사용자 끊기가 UI 스레드에서 그 lease 를 끝냈다(21:55:32.863). 그런데 job 의 Provide 는 그대로 native lease 에 닿았다(32.943 `ok=true`). native Release 가 같은 executor 에서 그 job 뒤에 줄 서 있었기 때문이다. 결과: 끊기 뒤 capability 1회 제공, 약 80 ms 뒤 해제. 재생성은 없었다.
  - ke_r17 에서는 같은 시나리오가 `stale` 로 버려졌다. 순서에 따라 갈리는 경쟁이다.
  - 수정: `Book.ifCurrent` 가 확인과 Provide 를 book 의 lock 안에서 한다. `end()`·`endAll()` 도 같은 lock 을 쓴다. 그래서 끊기는 확인 전(stale, 제공 0)이거나 Provide 뒤(그 뒤 해제)다. 제공 시점은 lock 안에서 `standby_provided` 로 남긴다.
  - lock 안의 작업은 native Provide 하나다(lease mutex, 네트워크 없음). UI 스레드가 기다리는 시간은 그 길이다.
  - 단위 310/0. 새 시험은 `ifCurrent` 의 동기화를 빼면 FAIL 한다(변이 확인).
- ke_r16 — 하네스: 구 Host 의 lease 는 약 4 s 만에 무응답으로 놓인다. UI dump 1회가 약 3 s 라 E7f 가 끊기 버튼을 찾지 못하고 하네스가 멈췄다. → 로그를 보고 고정 위치를 즉시 탭하게 고쳤다.
- ke_r17 — 하네스: E7c–E7f 가 추가되면서 E10 시점에 lease 가 남아 있지 않았다(C 는 E7f 에서 최근 3대에서 빠져 놓였다, `reason=lru`). 앱은 놓을 것이 없어 release 를 하지 않았다. 이것은 맞는 동작이다. → E10 이 먼저 C 를 대기시키게 고쳤다.

**시험 결과** (모두 exit 와 RESULT 를 확인)

| 시험 | 결과 |
|---|---|
| `standby_lease_test` (da2280e) | **66/66**, ×6 |
| e2e `--product-client --real-directory --old-host` (a7773e6) | **78/78**, rc 0 |
| Kotlin 단위 (7a0d577) | **310/0**, rc 0 |
| AVD ke_r19 (7a0d577 APK) | **48/48 PASS**, avd exit 0, scenario rc 0. `keep_activate_refused` 0. 콘솔 세션(qwinsta), SLOT CreateNew→삭제 |
| AVD ke_r16 / ke_r17 / ke_r18 | 33/33 후 하네스 중단 / 45/46(E10 하네스) / 47/48(E7f 실결함 → 7a0d577) |

**AVD 실 UI — 이번에 추가한 시나리오** (ke_r16–ke_r18 에서 확인, ke_r19 에서 고정 APK 로 전체 재확인)

| 시나리오 | 확인한 것 |
|---|---|
| E7g | **화면에 떠 있는(active)** A 를 다른 기기가 가져감 → 세션 종료(`keep_end reason=peer_lost`), **자동 재탈취 0**, A 는 대기 |
| E7b | Ctrl 누른 채 PC 목록 → key up 이 세션 종료보다 먼저 Host 에 도착(Host 입력 로그), fence 경유 종료. 카드는 정리 완료를 주장하지 않음 |
| E7c (N1) | A 를 fence 동안 탭(보류) → B 를 탭 → A 보류는 `tap_held_superseded`, B 가 열리고 A 는 이후 activation 0 |
| E7d (N1) | A 를 보류 → A 끊기 → 보류 탭 `tap_held_dropped why=disconnect`, A 해제(`user_disconnect`), 재생성 0 |
| E7e | **C 의 첫 전환 도중** B 를 탭 → B 는 activation 으로 열리고 C 의 lease 는 방해받지 않음 → C 의 첫 전환 완료(대기 중) |
| E7f | **첫 전환 도중 끊기** → `user_disconnect` 해제, 재생성 0. ke_r17: capability 를 기다리는 중(begin 21:28:12.207)에 끊기(13.355), 늦은 capability 는 0.24 s 뒤 `ok=false … stale` 로 버려짐. ke_r19(7a0d577): 이번에는 Provide 가 먼저였고(`standby_provided`) 끊기가 그 뒤에 와서 해제. 끊기 뒤 제공 0 |

**고정 대상**
- feat **7a0d577**, ka-r3 **ecf1004**. 둘 다 tracked clean.
- APK(7a0d577 에서 빌드, `-Pgnlink.testDirectoryUrl=http://10.0.2.2:18410`, on 은 `+ -Pgnlink.testStandbyAutoPrepare=true`): off be0464994d718471…, on 4e0366b4b7504e51….
- 하네스(미추적, `feat-apk-0233/.claude/e2e`) sha256 앞 16자리: ke_avd.py 644b388cb32087d9, ke_fixture.ps1 1cd59dcb413b9323, ke_slot.ps1 0f908fd3728a6545, ka_avd.py a6f787fc2e540f41. 시험 Host 44a43610…, old b6ed8984…

**남은 것·미검증**
- N3 실 clipboard(AVD fixture 는 sync off), N4 raw-IP+AUTO 실 UI, N5 Activity 반복 재생성 누적: 단위 시험만.
- prelogin 역할의 실 UI 경로: 이번에도 하지 않았다.
- 첫 프레임 시간은 선택 → 첫 프레임 실측(약 1.0–4.1 s)이다. 목표 p95 ≤ 1 s 의 증명이 아니다.
- 실 NAT·실기기 폰·PC 교차·실 server 비용: 기존 후속 그대로.
- release 서명 APK 는 만들지 않았다(검증용 담당).
