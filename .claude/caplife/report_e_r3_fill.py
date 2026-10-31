src = 'D:/remote/remote/.claude/caplife/report_e_r3_draft.md'
dst = 'D:/remote/remote/.claude/caplife/report_e_r3.md'
s = open(src, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''| e2e `--product-client --real-directory --old-host` (ka-r3 0fb4676, `kae_e3`) | **78/78**, scenario rc 0 |''',
    '''| e2e `--product-client --real-directory --old-host` (ka-r3 0fb4676, `kae_e3`) | **78/78**, scenario rc 0. ⚠️ **과거 실행**: 10-28 07:50–07:53(KST), 10-28 입력·전원 안전 규칙(d9a3a91, 09:08) 이전이다. 이 하네스(0fb4676 의 `host_standby_admission_e2e_test`)는 Host 에 `--input-injection-mode none` 을 주지 않았다. 그래서 Host 는 **제품 기본(입력 주입 켜짐)** 으로 떴고, 전원 정책 옵션도 없었다. 그 실행에서 OS 입력이 실제로 일어났는지는 **미확인**이다. 현재 정책상 `--old-host` 경우는 **NOT RUN** 이므로 이 수치는 그 정책 이전의 기록일 뿐이다. |''')

rep('''| AVD ke_r20 (6451ec1 APK) | __KE_R20__ |''',
    '''| AVD ke_r20 (6451ec1 APK, 10-28 07:55–08:13) | **40/40 PASS**(E1–E9 와 E7h a 의 준비 단계). 이어서 E7h(a) 동작 직전에 하네스 오류(`StopIteration`: 끊기 위치를 잴 카드 없음)로 중단했다. E7h 결과는 없다. |
| AVD ke_r21 (같은 APK, 10-28 08:17–08:33) | **29/29 PASS** 뒤 E7e 에서 중단했다(B 세션 `peer_lost` → viewer 메뉴를 찾지 못함). E7h 에는 도달하지 못했다. |''')

rep('''**AVD 실 UI — R1** (E7h, ke_r20)''', '''**AVD 실 UI — R1** (E7h) — **미실행**''')
for k in 'ABCD':
    rep('__E7H_' + k + '__', '미실행')
rep('''| d | 로그아웃 → 다시 로그인 | 미실행 |''', '''| d | 로그아웃 → 다시 로그인 | 미실행 |

- 미실행 이유:
  - ke_r20 은 a 의 준비 단계 뒤 하네스 오류로 멈췄다.
  - ke_r21 은 E7e 에서 멈춰 E7h 에 닿지 못했다.
  - 10-28 입력·전원 안전 규칙 이후에는 AVD 를 다시 실행하지 않았다.
  - 그 뒤 하네스의 위치 측정을 고쳤지만(아래 하네스 해시), 고친 판으로는 실행한 적이 없다.
- R1 의 근거는 지금은 소스와 단위 시험뿐이다.''')

rep('''**AVD 실행 경과**''', '''**AVD 실행 경과** (둘 다 10-28 입력·전원 안전 규칙 **이전**의 과거 실행)
- 두 실행에서 Host 4개(a·b·c·old)의 입력·전원 상태. 각 Host 로그(`directoryhost_host.log`)를 직접 확인했다.
  - 입력: **`input injection disabled (unsupported mode) mode=none` 한 줄이 모두 있다**. 하네스(feat 의 `mobile_abr_session_e2e_test --scenario directoryhost`)가 `--input-injection-mode none` 을 주었다. 수신 입력 로그는 `injectionEnabled=0` 이다(a: 2줄).
  - 전원: `host power management=` 줄이 **0** 이다. 두 시험 Host 빌드(test 44a43610…, old b6ed8984…)는 전원 정책 옵션 이전 빌드이므로 제품 기본 전원 동작(실행 상태 유지, 스트림 시작 시 display wake)이었을 것이다. 그 실제 효과는 따로 측정하지 않았다.
  - gate: 그때 하네스는 명령줄에 인자가 있는지만 검사했다(`e2e_command_is_isolated`). **출력 두 줄 gate(공용 guard)는 적용되지 않았다.**''')

rep('''- 하네스 sha256 앞 16자리: __HARNESS__''', '''- 하네스 sha256 (feat-apk-0233 `.claude/e2e/`, 저장소 미추적):
  - `ke_avd.py` 현재 `8c204d970614e2b9…`. mtime 08:14 로, ke_r21(08:17)이 쓴 판이다. ke_r20(07:55)이 쓴 그 이전 판의 해시는 **미확인**이다.
  - `ka_avd.py` `a6f787fc2e540f41…`, `ke_fixture.ps1` `1cd59dcb413b9323…`, `ke_slot.ps1` `0f908fd3728a6545…`, `ka_delay_proxy.js` `6303f50e995a20bc…`. 모두 두 실행 이전 mtime 이다.
  - fixture 기록(`ke_r20/fixture.txt`): host 하네스 exe `38ddf6cdf51ffb33…`, test host `44a43610c61590c2…`, old host `b6ed8984e202255c…`, server.js `b329e22249e87aec`.''')

s += '''

**AVD 하네스가 띄우는 Host 와 공용 guard** (확인만 했고 실행하지 않았다)

| 구성 | 입력 OFF 인자 | 전원 OFF 인자 | 출력 두 줄 gate (`e2e_injection_guard.hpp`) | 비고 |
|---|---|---|---|---|
| `ke_fixture.ps1` → feat `mobile_abr_session_e2e_test --scenario directoryhost` (a·b·c) | `--input-injection-mode none` 있음 | **없음** | **없음**. 명령줄 검사만 한다. feat-apk-0233 에는 guard 헤더가 없다(ka-r3 쪽에만 있음). | 다음 AVD 실행 전에 ka-r3 guard(입력 none + 전원 off + 두 줄 gate + 실행 중 감시)를 이 하네스에 통합해야 한다. |
| 같은 하네스, old host (`GNLinkStreamPinnedOld.exe`) | 있음 | 없음, **줄 수도 없음** | 없음 | 전원 옵션 이전 빌드라 `--host-power-management off` 를 거부한다. guard 를 적용하면 gate 가 거부하므로, 지금 정책(old-host NOT RUN)과 같다. |
| pinned test host (`GNLinkStreamPinnedSelectTest.exe` 44a43610…) | 받음 | 이 빌드는 전원 옵션 이전 | — | guard 를 적용하려면 전원 정책이 들어간 빌드로 pinned host 를 다시 만들어야 한다. |

→ **지금 상태로는 AVD 하네스를 다시 실행하지 않는다.** 위 통합이 먼저다(이번 패킷 범위 밖, 지시를 기다린다).

**결정 요청**
1. **R3 실 clipboard**: AVD clipboard 공유가 꺼져 있는지 확인할 수 없어 차단 중이다. 공용 emulator-5554 는 `-no-window` 이고 clipboard 옵션이 없다. 선택지는 셋이다.
   - (a) clipboard 공유가 꺼진 것을 확인한 **별도 에뮬레이터**
   - (b) **실기기**(사용자 1회 협조)
   - (c) **미검증으로 둠**(지금 근거: 소스 + 순수 판단 단위 시험 + 변이)
2. **fallback 경로용 pinned 시험 Host fault 추가 여부**: activation 실패를 결정적으로 만드는 fault 가 없다. 공용 Host 코드에 seam 빌드 한정 fault 를 추가할지(+ pinned host 재빌드, 위 guard 통합과 함께), 아니면 fallback 경로를 단위 시험 근거로만 둘지 정해야 한다.

**고정 commit 정리**
- **e r3 의 SOURCE 대상은 feat `1aecc48`** (+ ka-r3 `0fb4676`) 이다. 이 보고의 R1–R3·D1 은 모두 1aecc48 까지의 내용이다.
- feat HEAD `7f07f1a` 는 1aecc48 위에 APK X 경계(3ef6ee6·50340ce·7c5d027·7f07f1a)만 더한 것이다. X 경계는 따로 SOURCE OK 를 받았다. `git diff 1aecc48..7f07f1a` 는 X 경계 파일(`client_mouse_xbutton_fence.hpp`, session cpp/hpp, X 시험, CMake)뿐이다.
- 통합 후보로 한 번에 보려면 7f07f1a 를 쓰면 된다. e r3 범위 자체는 1aecc48 로 고정한다.
- ke_r21 E7e `peer_lost`: **원인 미확정, 간헐 가능**. 위 기록 그대로이며 1회 통과로 닫지 않는다.
'''
open(dst, 'w', encoding='utf-8', newline='\n').write(s)
print('ok', s.count('__'))
