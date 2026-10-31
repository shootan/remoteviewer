

### ② e — kept 모델 A 배선 + M1 (2026-10-28)
근거: `codex_keepalive_r2_kept_model.md`(A), `codex_ka_e_switch_decision.md`(ⓐ), `codex_apk0233_merge_src_r1.md`(M1).

**커밋**
- ka-r3:
  - **dcb6607** standby_session_link: 세션 entry 를 lease activation 으로 연결한다.
  - **16a1817** activation 소켓을 공유 소켓으로 다룬다. 세션은 이 소켓을 shutdown 하지 않는다.
  - **3326c84** 세션이 끝난 뒤 lease 는 원래 endpoint 를 유지한다. connect 해제(dissolve)는 하지 않는다.
- feat/apk-0.2.33:
  - **4e88a0b** 모델 A 배선.
  - **62877b4** 무응답 lease 를 구 Host 로 확정하지 않는다. 카드가 lease 상태를 따라 갱신된다.
  - merge: 5c871ed · d130407 · 21f9f17.
- NP 소유 파일은 건드리지 않았다.

**제품 동작**
- **PC 목록**: 화면 fence(입력 해제·선택 중단·decoder 스트림 리셋) → 세션 종료 → closer 스레드에서 Disconnect → 그 activation 의 SessionOver. 이후는 세 갈래다.
  - activation 으로 연 세션: 같은 lease 로 복귀한다.
  - 지원 directory 의 일반 연결: **첫 전환**으로 새 lease 를 만든다. 과정: lease 소켓으로 observe → `/api/connect` standby → relay 가 reserved 면 relay 를 조준. 이 새 directory 요청은 `standby_first_transition` 으로 로그에 남긴다.
  - raw IP·prelogin·standby 없는 directory: "상시 유지 미지원" 안내와 함께 세션을 종료한다.
  - 옛 park/resume(`resumeKept`/park_reuse)은 삭제했다 → **M1 은 경로 제거로 닫힘**.
- **탭**:
  - READY 인 lease → 새 entry 를 `nativeConnectStandby` 로 연결한다(standby_adopt, entry 자신의 gate 경유). directory·observe·punch·일반 Hello 는 0 이다.
  - 소켓을 아직 돌려받는 중인 lease → 탭을 보류했다가 실행한다.
  - 준비되지 않은 lease → 그 lease 를 놓고 일반 연결로 간다.
- **lease 관리**: 순수 `StandbyKeep` 이 담당한다.
  - 표시 상태: 확인된 entry 가 살아 있는 lease 위에 있을 때만 READY.
  - incarnation 으로 늦은 Provide 와 같은 key 재생성을 막는다(fence).
  - staging ≤ 1, PC 별 back-off.
  - 최근 3대에서 빠진 PC 의 lease 는 놓는다. 다만 화면에 떠 있는 PC 는 예외다.
- **해제 시점**: 앱 이탈·로그아웃·세션 거부·모두 끊기·네트워크 변경 → 전체 해제. 카드 끊기 → 그 PC 만 해제하고 suppress.
- **스위치**:
  - `Preconnect.AUTO_PREPARE = BuildConfig.STANDBY_AUTO_PREPARE` 이다. 앱이 **스스로** 최근 PC 를 준비할 때만 이 값을 본다.
  - debug 시험 빌드 `-Pgnlink.testStandbyAutoPrepare=true` 에서만 켜진다. release 빌드에 이 속성을 주면 빌드가 거부된다.
  - 이미 있는 lease 의 Ping 은 스위치와 상관없이 계속된다.
- **DirectoryClient**:
  - `/api/hosts` 의 standby{supported, relay} 를 읽는다(read-only).
  - `connect(standby=true)` 와 standbyRelay 를 지원한다.
  - `standbyAim` 의 조준 순서: relay(reserved) → 사설 후보 → 첫 비-relay 후보.

**AVD 가 찾아낸 실결함**(fixture 실행마다, 모두 고쳤다)
1. **ke_r2 — fixture 문제였다.**
   - Host 가 relay 를 10.0.2.2 로 dial 했다. 이 주소는 에뮬레이터 전용이다.
   - 그래서 host-proof 가 relay 에 닿지 않았다.
   - fixture 에 `REMOTE60_RELAY_HOST_FACING_IP=127.0.0.1` 을 넣어 고쳤다.
   - 다만 이 실행으로 **앱의 결함도 드러났다**: 무응답 lease 를 로그인 동안 계속 "미지원" 으로 굳혔다. 이것은 Codex 의 "미확인/일시 실패를 구버전으로 단정하지 않는다" 와 어긋난다 → 62877b4. 지금은 목록 복귀 때마다 다시 시도한다. 앱의 자동 준비만 10분 쉰다. 안내 문구는 "응답 없음(미지원 PC 이거나 경로 문제)" 이다.
2. **ke_r3 — 카드와 두 번째 activation.**
   - 카드가 "대기 준비" 에 멈췄다. lease 상태가 바뀌어도 목록을 다시 그리지 않았기 때문이다 → 62877b4.
   - **두 번째 activation 이 무응답이었고, 이후 observe 도 계속 실패했다**:
     - 세션이 받은 소켓은 lease 소켓의 **dup** 이다.
     - 세션은 이 소켓을 connect() 하고, 끝날 때 shutdown() 했다.
     - POSIX 에서는 두 호출 모두 공유 소켓 자체에 작용한다. 그래서 lease 소켓이 죽었다.
     - Windows 데스크톱 e2e 는 통과했었다. WSADuplicateSocket 의 shutdown 이 원래 소켓에 닿지 않아 결함이 가려진 것이다.
   - 수정(16a1817): `ClientSessionConnectArgs::preparedSocketShared` 를 두었다. 공유 소켓이면 세션이 shutdown 하지 않는다. reader 는 수신 tick(≤100 ms)에 스스로 끝나고, join 뒤에 close 만 한다. 이 값을 설정하지 않으면 기존 동작 그대로다.
3. **ke_r4 — 두 번째 activation 이 다시 무응답.**
   - Linux 에서 `connect(AF_UNSPEC)` 는 **자동 bind 된 포트까지 놓는다**.
   - 그래서 lease 가 다른 endpoint 로 돌아왔다. relay 는 모르는 endpoint 에서 온 StandbyActivate 를 버린다(relay 로그에서 클라이언트 포트가 바뀐 것을 확인).
   - Windows 는 UDP 소켓의 connect 해제가 아예 불가능하다. 실측: INADDR_ANY:0 → 10049, AF_UNSPEC → 10047.
   - 수정(3326c84): 해제하지 않는다.
     - lease 소켓은 자기 target(=세션의 peer)에 묶인 채 **같은 endpoint** 를 유지한다.
     - 다른 곳(새 capability 를 위한 observe)으로 보낼 때만 새 소켓을 연다. 새 capability 는 어차피 relay 에서 새 lease 다.
4. ke_r5·ke_r6 — 하네스 문제(설정 탭이 유지됨, E12 기대값) → 하네스를 고쳤다.

**시험 결과** (모두 exit 와 RESULT 를 확인)

| 시험 | 결과 |
|---|---|
| `standby_lease_test` | **57/57**, ×3 |
| 변이(신규) | 2종 모두 FAIL 로 잡힘: 묶임 미표시, shared 미설정 |
| e2e `--product-client --real-directory --old-host` | **74/74**, rc 0. standby_session_link 8개 포함: stale → Abandon, Connect 거부 → Abandon, 채택 → bridge 방식으로 Close → 같은 소켓에서 parked, 두 번째 Over 없음 |
| Kotlin 단위 | **305/0**. StandbyKeepTest 15 개 포함 |

**AVD 실 UI**
- 하네스: `feat-apk-0233/.claude/e2e/ke_avd.py` + `ke_fixture.ps1` + `ke_slot.ps1`.
  - sh_run 슬롯을 쓰고, 공용 emulator-5554 에서 돈다.
  - 실 server.js(relay on, 시험 계정) 위에서 pinned 시험 Host 4대를 쓴다: a/b/c 는 이 브랜치, old 는 baseline(standby 이전 빌드).
- **ke_r7 = 30/30 PASS, avd exit 0, scenario rc 0.**
  - APK: off c0e01212…, on 72b2aefd…
  - 시험 Host eef2f0d1…, old b6ed8984…, harness 38ddf6cd…
- 확인한 사용자 행동:

| 시나리오 | 확인한 것 |
|---|---|
| E1 | OFF 에서 앱이 스스로 준비하는 것 0 |
| E2 | A 일반 연결 → PC 목록 → 첫 전환(relay reserved) → 카드 "대기 중" |
| E3 | A 를 activation 으로 연결: connects/observes/punches 0, 첫 프레임 4.3 s |
| E4 | 같은 lease 로 복귀(새 lease·capability 0) |
| E5 (M1) | A 에서 GDI 요청 → B → A 를 다시 activation → 새 세션은 asked=none, sentSeq=0, 자동 재전송 0 |
| E6 | B → A → B: activation 3회, directory 0 |
| E7 | 다른 기기가 A 를 사용(relay 일반 Hello) → 자동 재탈취 0. 사용자가 탭하면 activation |
| E8 | 구 Host: 무응답 → 해제·안내. 다시 PC 목록 → 재시도(구 Host 로 단정하지 않음) |
| E9 | 카드 끊기 → 해제, 재생성 0 |
| E10 | 앱 이탈 → 전체 해제, OFF 재진입 시 준비 0 |
| E11 | 로그아웃 → 해제 |
| E12 (ON 빌드) | 최근 3대(C·old·A)를 자동 준비: A·C 는 READY, old 는 무응답으로 해제, B 는 최근 3대에서 빠져 준비 안 함. 탭하면 그 lease 로 activation |

- 그 전 실행: ke_r2·ke_r3·ke_r4·ke_r5·ke_r6 은 각각 위에 적은 결함·하네스 문제로 실패했다. 보존돼 있다.
- **출하 빌드 gate**:
  - plain 빌드(cae36136…): `STANDBY_AUTO_PREPARE=false`, DIRECTORY_URL 은 고정 주소.
  - `assembleRelease -Pgnlink.testStandbyAutoPrepare=true` → 거부된다.
  - APK native CMake 에 STANDBY/CONNECT_GATE test hook 정의가 없다.

**호출자 계약**
- "SessionOver 는 Disconnect 반환 뒤에만" 을 실제 경로로 확인했다:
  - bridge 의 `SessionEntry::Close` 는 CloseExplicit → Disconnect(StopWorker 가 worker 와 video reader 를 join) → `standby.Over()` 순서다.
  - `nativeCloseIfOwn` 도 같은 순서다.
  - AVD E4/E5/E6 에서 반복 activation 이 성공했다.

**남은 것·미검증**
- **ReturningToStandby 창**: PC 목록 직후 Host 가 세션 끝을 알아차리는 데 control timeout(~13 s)이 걸린다. 그동안 Host 는 영상을 relay 로 계속 보내고, lease 는 이를 받아 버린다. 그 사이 다시 탭하면 Host 표가 새 generation 으로 rollover 한다(E6 의 연속 탭에서 성공). 세션 끝을 Host 에 즉시 알리는 신호는 프로토콜 변경이라 이번 범위에서 다루지 않았다(Codex 확정 사항).
- **기타 미검증**: 실 NAT·실기기 폰, PC 클라이언트(작업용2 범위), 실 server 비용.
- **prelogin 미지원 경로**는 단위 시험(onListReturn)만 했다. AVD 에서 prelogin 역할 Host 로 돌리지는 않았다.
- **release 서명 APK 는 만들지 않았다**(검증용 담당).
